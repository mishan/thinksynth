/*
 * Copyright (C) 2004-2026 The thinksynth authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* The composer screen. A task of its own on core 0 is the only thing that
   calls into the scheduler: it steps it every STEP_MS while the screen
   shows, and the control task only sets what it is to do. The scheduler
   sends its notes to the host (setNotesOut) and asks it for each channel's
   chanargs (setChanArgSource), with no graph loaded: an instrument is a
   pool of its compiled patch's voices, made in internal RAM as the piece
   loads and freed as the next one does. Notes and chanargs reach the audio
   task over a queue, stamped and played LEAD_US behind. */

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <strings.h>
#include <vector>

#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "box_hal.h"
#include "composer.h"
#include "patches.h"

#include "think.h"
#include "thDynLib.h"
#include "thcGenFile.h"
#include "thcPlugin.h"
#include "thcScheduler.h"

#define STEP_MS 20
/* A step delivers what fell due in the step just run, so an event is
   stamped up to a step late; played this much after its stamp, the
   events keep their spacing to within a block. */
#define LEAD_US 30000
#define BLOCK_US (1000000LL * BLOCK / BOX_RATE)
#define QUEUE 1024

/* ---- the queue, the scheduler's task to the audio task ------------------ */

enum { EV_ON, EV_OFF, EV_KNOB, EV_BIND, EV_SILENCE };

struct Event
{
    int64_t     due;
    uint8_t     type;
    int8_t      channel;
    int16_t     note, velocity;
    float       value;              /* EV_ON's level, EV_KNOB's value */
    const char *name;               /* EV_KNOB's: a patch's declared name */
    Patch      *patch;              /* EV_BIND's */
};

/* One writer and one reader, no lock: the scheduler never waits on a
   render, nor the audio task on a step. Full, the writer waits rather
   than drop one: a dropped off would hang a voice. */
static struct
{
    Event buf[QUEUE];
    std::atomic<uint32_t> head{0}, tail{0};

    void push (const Event &e)
    {
        const uint32_t h = head.load(std::memory_order_relaxed);

        while (h - tail.load(std::memory_order_acquire) == QUEUE)
            vTaskDelay(1);
        buf[h % QUEUE] = e;
        head.store(h + 1, std::memory_order_release);
    }

    const Event *peek ()
    {
        const uint32_t t = tail.load(std::memory_order_relaxed);

        return t == head.load(std::memory_order_acquire) ? NULL : &buf[t % QUEUE];
    }

    void pop () { tail.store(tail.load(std::memory_order_relaxed) + 1,
                             std::memory_order_release); }
} queue;

Patch *composerChannel[COMPOSER_CHANNELS];

/* The channels emptied, counted by the audio task once the voices an
   EV_SILENCE faded have had their block, for the pools to be freed. */
static std::atomic<unsigned> emptied{0};
static bool emptying = false;
/* Events played more than a block after they were due; those due at 0
   are due at once. And the chanargs a step moved that reached a pool,
   past those an instrument starts from. */
static volatile int late = 0, knobsSet = 0;

void
composerApply (int64_t now)
{
    if (emptying)
    {
        for (Patch *&p : composerChannel)
            p = NULL;
        emptying = false;
        emptied++;
    }
    for (const Event *e; (e = queue.peek()) != NULL && e->due <= now; queue.pop())
    {
        Patch *p = e->channel >= 0 && e->channel < COMPOSER_CHANNELS
                 ? composerChannel[e->channel] : NULL;
        thhAction a;

        late += e->due > 0 && now - e->due > BLOCK_US;
        a.note = e->note;
        a.vel = e->velocity;
        switch (e->type)
        {
        case EV_ON:
            a.kind = thhAction::NOTE_ON;
            if (p)
                boxPlay(p, a, 0, e->value);
            break;
        case EV_OFF:
            a.kind = thhAction::NOTE_OFF;
            if (p)
                boxPlay(p, a, 0, 1);
            break;
        case EV_KNOB:
            if (p)
            {
                p->set(e->name, e->value);
                knobsSet += e->due > 0;
            }
            break;
        case EV_BIND:
            /* An instrument swapped mid-piece: the last one's voices stop
               with it. */
            composerChannel[e->channel] = e->patch;
            break;
        case EV_SILENCE:
            for (Patch *c : composerChannel)
                if (c)
                    c->silence();
            emptying = true;
            break;
        }
    }
}

/* ---- the patches, by name ---------------------------------------------- */

typedef std::map<std::string, thArg *> Surface;

template <class V>
static Patch *
make (const char *name)
{
    /* Internal RAM: the render reads it every sample. */
    void *at = heap_caps_malloc(sizeof(PatchOf<V>), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    return at ? new (at) PatchOf<V>(name, 1) : NULL;
}

/* A patch's knobs as the scheduler sees a loaded graph's chanargs: as
   dspcompile declared them. */
template <class V>
static void
declare (Surface &s, std::vector<thArg *> &made)
{
    for (const auto &d : V::Knobs::declared)
    {
        thArg *a = new thArg(d.name, d.value);

        a->setMin(d.min);
        a->setMax(d.max);
        a->setUnits(d.units);
        s[d.name] = a;
        made.push_back(a);
    }
}

/* Every patch, the percussion too, at the level the desktop's mix plays
   it: a piece's amps are balanced there. */
static const struct
{
    const char *name;
    Patch *(*make) (const char *);
    void (*declare) (Surface &, std::vector<thArg *> &);
} COMPILED[] = {
#define PATCH(type, name, gain) { name, make<thv##type>, declare<thv##type> },
    PATCHES
#undef PATCH
#define PERC_ROW(type) { #type, make<thv##type>, declare<thv##type> }
#define PERC_ROW_OF(type) PERC_ROW(type)
    PERC_ROW_OF(PERCUSSION),
};
#define NCOMPILED (int)(sizeof(COMPILED) / sizeof(COMPILED[0]))

/* An instrument's dsp, "dxbell.dsp" or "dsp/clave.dsp", as a patch. */
static int
compiledFor (const std::string &dsp)
{
    std::string base = dsp.substr(dsp.find_last_of('/') + 1);

    if (base.size() > 4 && base.compare(base.size() - 4, 4, ".dsp") == 0)
        base.resize(base.size() - 4);
    for (int k = 0; k < NCOMPILED; k++)
        if (!strcasecmp(base.c_str(), COMPILED[k].name))
            return k;
    return -1;
}

/* ---- the pieces --------------------------------------------------------- */

#define EMBEDDED(sym) \
    extern const char sym##_start[] asm("_binary_" #sym "_start"); \
    extern const char sym##_end[] asm("_binary_" #sym "_end");

EMBEDDED(bells_gen)

static const struct { const char *file, *start, *end; } PIECES[] = {
    { "bells.gen", bells_gen_start, bells_gen_end },
};
#define NPIECES (int)(sizeof(PIECES) / sizeof(PIECES[0]))

/* ---- the scheduler's task ---------------------------------------------- */

static std::atomic<bool> shown{false};
static std::atomic<unsigned> nexts{0};

void composerShow (bool on) { shown = on; }
void composerNext (void) { nexts++; }

/* The step's clock, for the stamps. */
static int64_t stepMono = 0;
static double stepTransport = 0;

/* Where the scheduler's notes go instead of a graph's voices. */
class BoxNotes : public thcMidiOut
{
public:

    bool attach (int, const thcInstrument &, std::string &) override { return false; }
    void detach (int) override {}
    void noteOn (int ch, int note, int velocity, float level, int64_t when) override
    {
        Event e = {};

        e.due = when + LEAD_US;
        e.type = EV_ON;
        e.channel = (int8_t)ch;
        e.note = (int16_t)note;
        e.velocity = (int16_t)velocity;
        e.value = level;
        queue.push(e);
    }
    void noteOff (int ch, int note, int64_t when) override
    {
        Event e = {};

        e.due = when + LEAD_US;
        e.type = EV_OFF;
        e.channel = (int8_t)ch;
        e.note = (int16_t)note;
        queue.push(e);
    }
    void control (int, const std::string &, double, int64_t) override {}
    void flush (int) override {}
};

static BoxNotes notes;
static std::map<std::string, thcPlugin *> composers;
static thSynth *synth = NULL;
static thcScheduler *sched = NULL;

/* The piece's: its pools, each channel's chanargs, and every thArg made
   for them, kept until it goes so the scheduler never holds one that
   went. And what each was last sent as. */
static std::vector<Patch *> pools;
/* Each channel's pool of each patch, made once a piece: an instrument
   swapped back and forth, or applied again as a piece loops, takes the
   one it had. */
static std::map<std::pair<int, int>, Patch *> poolOf;
static Surface surface[COMPOSER_CHANNELS];
static std::vector<thArg *> made;

struct Watched
{
    const char *name;
    thArg      *arg;
    float       last;
};
static std::vector<Watched> watched[COMPOSER_CHANNELS];

/* What the screen and the console say, under `said'. */
static portMUX_TYPE said = portMUX_INITIALIZER_UNLOCKED;
static char title[48] = "", whyNot[160] = "";
static int pieceAt = 0;
static volatile unsigned saidChanged = 0;

static void
say (const char *t, const std::string &w)
{
    portENTER_CRITICAL(&said);
    snprintf(title, sizeof(title), "%s", t);
    snprintf(whyNot, sizeof(whyNot), "%s", w.c_str());
    portEXIT_CRITICAL(&said);
    saidChanged++;
}

/* Every chanarg that moved since it was last sent, to the voices: an
   instrument's values as it is applied, a chain's, a piece knob pushed
   into one, as the scheduler folded them. */
static void
syncKnobs (int64_t due)
{
    for (int c = 0; c < COMPOSER_CHANNELS; c++)
        for (Watched &w : watched[c])
        {
            const float v = (*w.arg)[0];

            /* By its bits: a NaN is no move from a NaN. */
            if (!memcmp(&v, &w.last, sizeof(v)))
                continue;
            w.last = v;

            Event e = {};

            e.due = due;
            e.type = EV_KNOB;
            e.channel = (int8_t)c;
            e.name = w.name;
            e.value = v;
            queue.push(e);
        }
}

/* The piece gone: its notes let go, its voices faded out, and once the
   audio task has let go of its pools, those freed. */
static void
endPiece ()
{
    if (!sched)
        return;
    sched->stop();
    delete sched;
    delete synth;
    sched = NULL;
    synth = NULL;

    const unsigned before = emptied;
    Event e = {};

    e.type = EV_SILENCE;
    queue.push(e);
    while (emptied == before)
        vTaskDelay(1);
    for (Patch *p : pools)
    {
        p->~Patch();
        heap_caps_free(p);
    }
    pools.clear();
    poolOf.clear();
    for (int c = 0; c < COMPOSER_CHANNELS; c++)
    {
        surface[c].clear();
        watched[c].clear();
    }
    for (thArg *a : made)
        delete a;
    made.clear();
}

static bool
loadInstrument (const thcInstrument &inst, std::string &why)
{
    const int k = compiledFor(inst.dsp), c = inst.channel;

    if (k < 0 || c < 0 || c >= COMPOSER_CHANNELS)
    {
        why = "'" + inst.dsp + "' has no compiled voice on the BOX";
        return false;
    }

    Patch *&p = poolOf[std::make_pair(c, k)];

    if (!p)
    {
        p = COMPILED[k].make(COMPILED[k].name);
        if (!p)
        {
            why = "no memory for " + inst.name + "'s voices";
            return false;
        }
        pools.push_back(p);
    }

    /* Every channel has an amp, which no patch declares. */
    thArg *amp = new thArg("amp", TH_DEFAULT_CHAN_AMP);

    amp->setMin(0);
    amp->setMax(MIDIVALMAX);
    made.push_back(amp);
    surface[c].clear();
    surface[c]["amp"] = amp;
    COMPILED[k].declare(surface[c], made);

    /* Each sent as it first stands, so the voices start from the
       instrument's values rather than the patch's. A name is its thArg's,
       which outlives every event naming it: endPiece waits for the queue
       to empty before it deletes one. */
    watched[c].clear();
    for (const auto &d : surface[c])
        watched[c].push_back({ d.second->name().c_str(), d.second, NAN });

    Event e = {};

    e.type = EV_BIND;
    e.channel = (int8_t)c;
    e.patch = p;
    queue.push(e);
    return true;
}

static bool
startPiece (int i)
{
    const size_t free0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t psram0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    synth = new thSynth("", BLOCK, BOX_RATE);
    sched = new thcScheduler(synth);
    sched->setLocalEar(false);
    sched->setNotesOut(&notes);
    sched->setMidiClock(
        [](double at) { return stepMono + (int64_t)llround((at - stepTransport) * 1e6); },
        []() { return (int64_t)esp_timer_get_time(); });
    sched->setChanArgSource([](int c, const std::string &n) -> thArg * {
        if (c < 0 || c >= COMPOSER_CHANNELS)
            return NULL;

        const auto at = surface[c].find(n);

        return at == surface[c].end() ? NULL : at->second;
    });
    sched->setInstrumentLoader(loadInstrument);
    sched->setInstrumentUnloader([](const thcInstrument &) { return true; });
    /* No instrument's effect: there is no graph for one. */
    sched->setEffectLoader([](int, const std::string &, int, std::string &) { return true; });

    thcGenLoader loader(composers);
    const std::string text(PIECES[i].start, PIECES[i].end - PIECES[i].start - 1);

    if (!loader.loadText(text, PIECES[i].file, sched))
    {
        const std::string first = loader.errors().empty() ? "" : loader.errors()[0];

        printf("composer: %s did not load: %s\n", PIECES[i].file, first.c_str());
        endPiece();
        say(PIECES[i].file, first);
        return false;
    }
    syncKnobs(0);
    say(loader.pieceName().c_str(), "");
    printf("composer: playing \"%s\" (%s), %zu chains, %u KB internal and %u KB "
           "PSRAM\n", loader.pieceName().c_str(), PIECES[i].file, sched->chainCount(),
           (unsigned)((free0 - heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) / 1024),
           (unsigned)((psram0 - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)) / 1024));
    return true;
}

static void
composerTask (void *)
{
    for (size_t i = 0; i < thStaticComposerCount; i++)
    {
        thcPlugin *p = new thcPlugin(thStaticComposers[i]);

        if (p->state() != thcPlugin::LOADED)
        {
            printf("composer: %s did not load\n", thStaticComposers[i]);
            delete p;
            continue;
        }
        composers[p->name()] = p;
    }

    unsigned nextSeen = nexts;
    bool failed = false;
    int64_t last = esp_timer_get_time(), printed = last;
    double stepSum = 0, stepMax = 0;
    long steps = 0;
    TickType_t wake = xTaskGetTickCount();

    for (;;)
    {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(STEP_MS));

        const int64_t now = esp_timer_get_time();

        if (nexts != nextSeen)
        {
            nextSeen = nexts;
            endPiece();
            pieceAt = (pieceAt + 1) % NPIECES;
            failed = false;
        }
        if (shown && !sched && !failed)
            failed = !startPiece(pieceAt);
        if (sched && shown != sched->running())
        {
            if (shown)
                sched->start();
            else
                sched->stop();
        }
        if (sched && sched->running())
        {
            const double dt = (now - last) / 1e6;

            stepMono = now;
            stepTransport = sched->now() + dt;
            sched->stepTransportAt(dt, now);
            syncKnobs(now + LEAD_US);

            const double us = esp_timer_get_time() - now;

            stepSum += us;
            stepMax = us > stepMax ? us : stepMax;
            steps++;
        }
        last = now;

        if (sched && sched->running() && now - printed >= 5000000)
        {
            printf("composer: %s t %.0f s  step %.0f us mean %.0f max  late %d  "
                   "knobs %d  non-finite %d  internal %u free\n", title, sched->now(),
                   steps ? stepSum / steps : 0.0, stepMax, late, knobsSet, boxNonFinite,
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            printed = now;
            stepSum = stepMax = 0;
            steps = 0;
        }
    }
}

void
composerStart (void)
{
#if CONFIG_SPIRAM
    /* From here on, ordinary allocations to PSRAM: the scheduler's many
       small ones left the voices and the screen's DMA no internal RAM.
       What must be internal asks for it: the pools, the screens' strips
       (MALLOC_CAP_DMA) and the tasks' stacks. */
    heap_caps_malloc_extmem_enable(0);
#endif
    xTaskCreatePinnedToCore(composerTask, "composer", 32 * 1024, NULL, 8, NULL, 0);
}

void
composerStatus (char *line, size_t size)
{
    portENTER_CRITICAL(&said);
    snprintf(line, size, "  piece %s (%d of %d)  late %d%s%s", title, pieceAt + 1, NPIECES,
             late, whyNot[0] ? "  " : "", whyNot);
    portEXIT_CRITICAL(&said);
}

/* ---- the screen: the piece's name, and why it isn't playing ------------- */

#define W 320
#define H 240
#define STRIP 16

static uint16_t
rgb (int r, int g, int b)
{
    const uint16_t c = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));

    return (uint16_t)((c >> 8) | (c << 8));         /* as the wire takes it */
}

/* 5x7, a column a byte, the top row bit 0: A-Z, then 0-9. */
static const uint8_t FONT[36][5] = {
    { 0x7E, 0x11, 0x11, 0x11, 0x7E }, { 0x7F, 0x49, 0x49, 0x49, 0x36 },
    { 0x3E, 0x41, 0x41, 0x41, 0x22 }, { 0x7F, 0x41, 0x41, 0x22, 0x1C },
    { 0x7F, 0x49, 0x49, 0x49, 0x41 }, { 0x7F, 0x09, 0x09, 0x09, 0x01 },
    { 0x3E, 0x41, 0x49, 0x49, 0x7A }, { 0x7F, 0x08, 0x08, 0x08, 0x7F },
    { 0x00, 0x41, 0x7F, 0x41, 0x00 }, { 0x20, 0x40, 0x41, 0x3F, 0x01 },
    { 0x7F, 0x08, 0x14, 0x22, 0x41 }, { 0x7F, 0x40, 0x40, 0x40, 0x40 },
    { 0x7F, 0x02, 0x0C, 0x02, 0x7F }, { 0x7F, 0x04, 0x08, 0x10, 0x7F },
    { 0x3E, 0x41, 0x41, 0x41, 0x3E }, { 0x7F, 0x09, 0x09, 0x09, 0x06 },
    { 0x3E, 0x41, 0x51, 0x21, 0x5E }, { 0x7F, 0x09, 0x19, 0x29, 0x46 },
    { 0x46, 0x49, 0x49, 0x49, 0x31 }, { 0x01, 0x01, 0x7F, 0x01, 0x01 },
    { 0x3F, 0x40, 0x40, 0x40, 0x3F }, { 0x1F, 0x20, 0x40, 0x20, 0x1F },
    { 0x3F, 0x40, 0x38, 0x40, 0x3F }, { 0x63, 0x14, 0x08, 0x14, 0x63 },
    { 0x07, 0x08, 0x70, 0x08, 0x07 }, { 0x61, 0x51, 0x49, 0x45, 0x43 },
    { 0x3E, 0x51, 0x49, 0x45, 0x3E }, { 0x00, 0x42, 0x7F, 0x40, 0x00 },
    { 0x42, 0x61, 0x51, 0x49, 0x46 }, { 0x21, 0x41, 0x45, 0x4B, 0x31 },
    { 0x18, 0x14, 0x12, 0x7F, 0x10 }, { 0x27, 0x45, 0x45, 0x45, 0x39 },
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, { 0x01, 0x71, 0x09, 0x05, 0x03 },
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, { 0x06, 0x49, 0x49, 0x29, 0x1E } };

/* Whether pixel (x, y) of `s', drawn `scale' times over from (x0, y0), is
   ink. Letters of either case and digits; anything else a space. */
static bool
ink (const char *s, int x0, int y0, int scale, int x, int y)
{
    if (x < x0 || y < y0)
        return false;

    const int col = (x - x0) / scale % 6, row = (y - y0) / scale;
    const size_t i = (x - x0) / (6 * scale);

    if (row > 6 || col > 4 || i >= strlen(s))
        return false;

    const char c = s[i];
    const int g = c >= 'a' && c <= 'z' ? c - 'a' : c >= 'A' && c <= 'Z' ? c - 'A'
                : c >= '0' && c <= '9' ? 26 + c - '0' : -1;

    return g >= 0 && (FONT[g][col] >> row) & 1;
}

void
composerDraw (bool all)
{
    static unsigned drawn = ~0u;
    /* Two, each filled while the other is still being sent. */
    static uint16_t *strips[2] = { NULL, NULL };

    if (!all && saidChanged == drawn)
        return;
    drawn = saidChanged;
    for (int b = 0; b < 2; b++)
        if (!strips[b])
            strips[b] = (uint16_t *)heap_caps_malloc(W * STRIP * 2, MALLOC_CAP_DMA);
    if (!strips[0] || !strips[1])
        return;

    char t[sizeof(title)], w[sizeof(whyNot)];

    portENTER_CRITICAL(&said);
    memcpy(t, title, sizeof(t));
    memcpy(w, whyNot, sizeof(w));
    portEXIT_CRITICAL(&said);

    /* The name as big as fits, amber; why it didn't load, small, gray. */
    const int len = strlen(t) > 0 ? (int)strlen(t) : 1;
    int scale = W / (6 * len);

    scale = scale > 6 ? 6 : scale < 1 ? 1 : scale;

    const int x0 = (W - 6 * scale * len + scale) / 2, y0 = 80 - 7 * scale / 2;
    const uint16_t fg = rgb(255, 210, 120), dim = rgb(150, 150, 150), bg = rgb(0, 0, 0);

    for (int s = 0; s < H; s += STRIP)
    {
        uint16_t *strip = strips[(s / STRIP) % 2];

        for (int y = s; y < s + STRIP; y++)
            for (int x = 0; x < W; x++)
                strip[(y - s) * W + x] = ink(t, x0, y0, scale, x, y) ? fg
                                       : ink(w, 4, 160, 1, x, y) ? dim : bg;
        box_lcd_draw(0, s, W, s + STRIP, strip);
    }
}
