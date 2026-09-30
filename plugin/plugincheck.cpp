/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

/*
 * plugincheck -- the built CLAP plays what the engine plays.
 *
 *   plugincheck build/bin/thinksynth-juno.clap dsp/juno.dsp juno
 *
 * A host of its own, as small as a CLAP host can be: it dlopens the
 * plugin, creates it, lists its parameters, and plays a phrase through it
 * in blocks of uneven sizes -- 100 frames, then 37, then 256, then 1, and
 * so on, as no audio device would but as a DAW's automation splits can --
 * with a note on, a parameter change and a note off at frames of their
 * own. Beside it a thSynth plays the same .dsp with the same events at
 * the same windows, and the two have to agree bit for bit. At 48 kHz and
 * again at 44.1, which is where a control given in ms has to be folded at
 * the host's rate and not the one its range was read at.
 *
 * What that holds down: the window adapter in ThinkPlugin::run (a window
 * handed out across blocks, an event applied before the window its frame
 * falls in), the parameters reaching the graph folded at the right rate,
 * and the plugin carrying the graph it was built from. And the parameter
 * list itself: the .dsp's controls in its order, its labels and ranges,
 * ms as ms, and the level last.
 *
 * And a project's round trip: controls set on one instance, its state
 * saved, loaded into a fresh instance, and the values and the sound have
 * to come back -- which is what a DAW does between saving a project and
 * opening it again.
 *
 * Headless and silent: nothing here opens a device. Exit status is the
 * number of failures.
 */

#include <dlfcn.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "clap/entry.h"
#include "clap/events.h"
#include "clap/ext/latency.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/host.h"
#include "clap/plugin-factory.h"
#include "clap/plugin.h"
#include "clap/process.h"

#include "think.h"

#include "thUnits.h"

#include "Controls.h"

static int failed = 0;

static void check (bool ok, const std::string &what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());

    if (!ok)
        failed++;
}

/* ---- the host's side of CLAP ------------------------------------------ */

static const void *hostExtension (const clap_host_t *, const char *)
{
    return NULL;
}

static void hostRequest (const clap_host_t *) { }

static const clap_host_t host = {
    CLAP_VERSION, NULL, "plugincheck", "thinksynth", "", "0",
    hostExtension, hostRequest, hostRequest, hostRequest,
};

/* One block's events, in time order. */
struct Events
{
    std::vector<clap_event_midi_t> midi;
    std::vector<clap_event_param_value_t> params;
    std::vector<const clap_event_header_t *> list;

    void clear (void)
    {
        midi.clear();
        params.clear();
        list.clear();
    }

    /* After every push, since the vectors may move. */
    void index (void)
    {
        list.clear();

        size_t m = 0, p = 0;

        while (m < midi.size() || p < params.size())
        {
            if (p >= params.size() ||
                (m < midi.size() && midi[m].header.time <= params[p].header.time))
                list.push_back(&midi[m++].header);
            else
                list.push_back(&params[p++].header);
        }
    }
};

static uint32_t eventsSize (const clap_input_events_t *in)
{
    return (uint32_t)((const Events *)in->ctx)->list.size();
}

static const clap_event_header_t *eventsGet (const clap_input_events_t *in,
                                             uint32_t i)
{
    return ((const Events *)in->ctx)->list[i];
}

static bool eventsPush (const clap_output_events_t *,
                        const clap_event_header_t *)
{
    return true;
}

/* ---- the phrase ------------------------------------------------------- */

/* kWindow, ThinkPlugin's window and its latency, is Controls.h's. */
/* Long enough to hear a release: juno's envelopes play their attack and
   decay out before releasing, 840 ms at the most. */
const int kWindows = 700;
/* The graph, as the command line names it, and what the engine reads off
   it: the reference every check is held to. */
static std::string gName, gText;
static std::vector<Control> gControls;

/* The control the phrase changes, and to what, in the graph's units: the
   first that is not the level or a choice between names, 70% of the way
   up its range. */
static int gParam = -1;
static double gParamValue = 0;

/* What happens at an absolute frame: a parameter change, or MIDI. */
struct Cue
{
    uint32_t frame;
    bool param;
    uint8_t midi[3];
};

static Cue midiCue (uint32_t frame, uint8_t a, uint8_t b, uint8_t c)
{
    Cue cue;

    cue.frame = frame;
    cue.param = false;
    cue.midi[0] = a; cue.midi[1] = b; cue.midi[2] = c;

    return cue;
}

/* Frames chosen to fall inside windows and inside blocks, not on either's
   edge -- 959 is one frame into a three-frame block, in the tail of a
   window rendered a block earlier -- and every one audible within the
   render: notes on, a parameter change, all notes off once the envelopes
   can release, and all sound off over a note played after it. */
static std::vector<Cue> phrase (void)
{
    std::vector<Cue> cues;
    Cue param;

    param.frame = 1300;
    param.param = true;

    cues.push_back(midiCue(0, 0x90, 60, 100));
    cues.push_back(midiCue(959, 0x90, 67, 90));
    cues.push_back(param);
    cues.push_back(midiCue(1793, 0x91, 64, 80));    /* another channel */
    cues.push_back(midiCue(41003, 0xb0, 123, 0));   /* all notes off */
    cues.push_back(midiCue(42017, 0x90, 72, 100));
    cues.push_back(midiCue(43001, 0xb0, 120, 0));   /* all sound off */

    return cues;
}

/* The host's blocks: sizes no device would pick, as automation splits
   make them. Enough to cover the render and the one window of latency. */
static const uint32_t kSizes[] = { 100, 37, 256, 1, 64, 500, 3, 127, 800 };
static const uint32_t kTotal = (kWindows + 1) * kWindow;

/* The start of the block `frame' is in. */
static uint32_t blockStart (uint32_t frame)
{
    uint32_t pos = 0;

    for (uint32_t s = 0; ; s++)
    {
        const uint32_t n = kSizes[s % (sizeof(kSizes) / sizeof(kSizes[0]))];

        if (frame < pos + n)
            return pos;

        pos += n;
    }
}

/* ---- the plugin's render ---------------------------------------------- */

struct Rendered
{
    std::vector<float> left, right;
};

/* ---- state ------------------------------------------------------------ */

typedef std::vector<uint8_t> Blob;

static int64_t blobWrite (const clap_ostream_t *s, const void *data,
                          uint64_t size)
{
    Blob *b = (Blob *)s->ctx;
    const uint8_t *p = (const uint8_t *)data;

    b->insert(b->end(), p, p + size);

    return (int64_t)size;
}

struct Reader
{
    const Blob *blob;
    size_t pos;
};

static int64_t blobRead (const clap_istream_t *s, void *data, uint64_t size)
{
    Reader *r = (Reader *)s->ctx;
    const uint64_t left = r->blob->size() - r->pos;
    const uint64_t n = size < left ? size : left;

    memcpy(data, r->blob->data() + r->pos, n);
    r->pos += n;

    return (int64_t)n;
}

/* Values set out of process, as a host's own controls or a preset do. */
struct Setting
{
    clap_id id;
    double value;
};

/* How an instance starts: from settings, from a saved state, or neither;
   and whether to save its state once they are in. */
struct Start
{
    std::vector<Setting> settings;
    const Blob *load;
    Blob *save;

    Start (void) : load(NULL), save(NULL) { }
};

static void setOutOfProcess (const clap_plugin_t *plugin,
                             const std::vector<Setting> &settings)
{
    const clap_plugin_params_t *params = (const clap_plugin_params_t *)
        plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    Events events;

    for (size_t i = 0; i < settings.size(); i++)
    {
        clap_event_param_value_t e;

        memset(&e, 0, sizeof(e));
        e.header.size = sizeof(e);
        e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.type = CLAP_EVENT_PARAM_VALUE;
        e.param_id = settings[i].id;
        e.note_id = -1;
        e.port_index = -1;
        e.channel = -1;
        e.key = -1;
        e.value = settings[i].value;
        events.params.push_back(e);
    }

    events.index();

    clap_input_events_t in = { &events, eventsSize, eventsGet };
    clap_output_events_t out = { NULL, eventsPush };

    params->flush(plugin, &in, &out);
}

static bool viaPlugin (const clap_plugin_factory_t *factory, const char *id,
                       double rate, Rendered &out,
                       const Start &start = Start())
{
    const clap_plugin_t *plugin = factory->create_plugin(factory, &host, id);

    if (plugin == NULL || !plugin->init(plugin))
        return false;

    const clap_plugin_state_t *state = (const clap_plugin_state_t *)
        plugin->get_extension(plugin, CLAP_EXT_STATE);

    if (!start.settings.empty())
        setOutOfProcess(plugin, start.settings);

    if (start.load != NULL)
    {
        Reader r = { start.load, 0 };
        clap_istream_t in = { &r, blobRead };

        if (state == NULL || !state->load(plugin, &in))
            return false;
    }

    if (start.save != NULL)
    {
        clap_ostream_t os = { start.save, blobWrite };

        if (state == NULL || !state->save(plugin, &os))
            return false;
    }

    if (!plugin->activate(plugin, rate, 1, 4096) ||
        !plugin->start_processing(plugin))
        return false;

    const uint32_t total = kTotal;
    const std::vector<Cue> cues = phrase();

    std::vector<float> left(1024), right(1024);
    float *channels[2];
    Events events;
    clap_input_events_t in = { &events, eventsSize, eventsGet };
    clap_output_events_t outEvents = { NULL, eventsPush };

    for (uint32_t pos = 0, s = 0; pos < total; s++)
    {
        uint32_t n = kSizes[s % (sizeof(kSizes) / sizeof(kSizes[0]))];

        if (n > total - pos)
            n = total - pos;

        events.clear();

        for (size_t i = 0; i < cues.size(); i++)
        {
            if (cues[i].frame < pos || cues[i].frame >= pos + n)
                continue;

            if (cues[i].param)
            {
                clap_event_param_value_t e;

                memset(&e, 0, sizeof(e));
                e.header.size = sizeof(e);
                e.header.time = cues[i].frame - pos;
                e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                e.header.type = CLAP_EVENT_PARAM_VALUE;
                e.param_id = (clap_id)gParam;
                e.note_id = -1;
                e.port_index = -1;
                e.channel = -1;
                e.key = -1;
                e.value = gParamValue;
                events.params.push_back(e);
            }
            else
            {
                clap_event_midi_t e;

                memset(&e, 0, sizeof(e));
                e.header.size = sizeof(e);
                e.header.time = cues[i].frame - pos;
                e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                e.header.type = CLAP_EVENT_MIDI;
                e.port_index = 0;
                memcpy(e.data, cues[i].midi, 3);
                events.midi.push_back(e);
            }
        }

        events.index();

        channels[0] = &left[0];
        channels[1] = &right[0];

        clap_audio_buffer_t buffer;

        memset(&buffer, 0, sizeof(buffer));
        buffer.data32 = channels;
        buffer.channel_count = 2;

        clap_process_t process;

        memset(&process, 0, sizeof(process));
        process.steady_time = pos;
        process.frames_count = n;
        process.audio_outputs = &buffer;
        process.audio_outputs_count = 1;
        process.in_events = &in;
        process.out_events = &outEvents;

        if (plugin->process(plugin, &process) == CLAP_PROCESS_ERROR)
            return false;

        out.left.insert(out.left.end(), left.begin(), left.begin() + n);
        out.right.insert(out.right.end(), right.begin(), right.begin() + n);

        pos += n;
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);

    return true;
}

/* The same phrase, each event applied at the start of the window it falls
   in -- a parameter change at the window its block starts in, since that
   is all the plugin is told of its timing. No latency here: the plugin's
   output is this, one window later. */
static bool viaEngine (double rate, Rendered &out)
{
    thSynth synth("", kWindow, (int)rate);

    if (synth.loadTreeText(gName, gText, 0, TH_DEFAULT_CHAN_AMP) == NULL)
        return false;

    const int channels = synth.audioChannelCount();
    const std::vector<Cue> cues = phrase();
    bool held[128] = { false };

    for (int w = 0; w < kWindows; w++)
    {
        for (size_t i = 0; i < cues.size(); i++)
        {
            const Cue &c = cues[i];
            const uint32_t at = c.param ? blockStart(c.frame) : c.frame;

            if ((int)(at / kWindow) != w)
                continue;

            if (c.param)
            {
                const Control &p = gControls[gParam];

                synth.getChanArg(0, p.name)->setValue(
                    (float)thFoldUnit((float)gParamValue, p.units,
                                      synth.getSampleRate()));
            }
            else if ((c.midi[0] & 0xf0) == 0x90)
            {
                synth.addNote(0, c.midi[1], c.midi[2]);
                held[c.midi[1]] = true;
            }
            else if ((c.midi[0] & 0xf0) == 0xb0 &&
                     (c.midi[1] == 123 || c.midi[1] == 120))
            {
                for (int k = 0; k < 128; k++)
                    if (held[k])
                    {
                        synth.delNote(0, k);
                        held[k] = false;
                    }

                if (c.midi[1] == 120)
                    synth.clearAll();
            }
        }

        synth.process();

        const float *buf = synth.getOutput();

        out.left.insert(out.left.end(), buf, buf + kWindow);
        out.right.insert(out.right.end(), buf + (channels > 1 ? kWindow : 0),
                         buf + (channels > 1 ? 2 * kWindow : kWindow));
    }

    /* The plugin flushes subnormals to zero on the way out, and nothing
       else; so does this. */
    for (size_t i = 0; i < out.left.size(); i++)
    {
        if (fabsf(out.left[i]) < FLT_MIN)
            out.left[i] = 0;
        if (fabsf(out.right[i]) < FLT_MIN)
            out.right[i] = 0;
    }

    return true;
}

/* The plugin's output past its latency, which has to be silence. */
static bool pastLatency (const Rendered &plugin, Rendered &out)
{
    if (plugin.left.size() < (size_t)kWindow)
        return false;

    for (int i = 0; i < kWindow; i++)
        if (plugin.left[i] != 0 || plugin.right[i] != 0)
            return false;

    out.left.assign(plugin.left.begin() + kWindow, plugin.left.end());
    out.right.assign(plugin.right.begin() + kWindow, plugin.right.end());

    return true;
}

static bool identical (const std::vector<float> &a, const std::vector<float> &b,
                       size_t &firstBad)
{
    if (a.size() != b.size())
    {
        firstBad = 0;
        return false;
    }

    for (size_t i = 0; i < a.size(); i++)
        if (memcmp(&a[i], &b[i], sizeof(float)) != 0)
        {
            firstBad = i;
            return false;
        }

    return true;
}

static double peak (const std::vector<float> &v)
{
    double p = 0;

    for (size_t i = 0; i < v.size(); i++)
        if (fabs(v[i]) > p)
            p = fabs(v[i]);

    return p;
}

int main (int argc, char **argv)
{
    if (argc != 4)
    {
        printf("usage: %s PLUGIN.clap GRAPH.dsp ID\n", argv[0]);
        return 2;
    }

    const std::string id = argv[3];

    {
        std::ifstream in(argv[2], std::ios::binary);
        std::ostringstream text;

        text << in.rdbuf();
        gText = text.str();
        gName = std::string(argv[2]).substr(
            std::string(argv[2]).find_last_of("/\\") + 1);
    }

    gControls = readControls(gName, gText);

    if (gControls.empty())
    {
        printf("FAIL  %s does not load\n", argv[2]);
        return 2;
    }

    for (size_t i = 0; i < gControls.size() && gParam < 0; i++)
        if (gControls[i].name != "amp" && gControls[i].valueNames.empty() &&
            gControls[i].max > gControls[i].min)
            gParam = (int)i;

    if (gParam < 0)
        gParam = (int)gControls.size() - 1;     /* the level */

    {
        const Control &p = gControls[gParam];

        gParamValue = p.min + 0.7 * (p.max - p.min);

        if (p.step == 1.0f)
            gParamValue = floor(gParamValue + 0.5);

        gParamValue = (float)gParamValue;
    }

    void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);

    if (module == NULL)
    {
        printf("FAIL  %s\n", dlerror());
        return 2;
    }

    const clap_plugin_entry_t *entry =
        (const clap_plugin_entry_t *)dlsym(module, "clap_entry");

    if (entry == NULL || !entry->init(argv[1]))
    {
        printf("FAIL  no clap_entry, or it would not initialize\n");
        return 2;
    }

    const clap_plugin_factory_t *factory = (const clap_plugin_factory_t *)
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID);

    check(factory != NULL && factory->get_plugin_count(factory) == 1,
          "one plugin in the factory");

    if (factory == NULL)
        return failed;

    const clap_plugin_descriptor_t *desc =
        factory->get_plugin_descriptor(factory, 0);

    const std::string want = "org.thinksynth." + id;

    check(desc != NULL && want == desc->id,
          "its id is " + want + ": " + (desc ? desc->id : "(none)"));

    if (desc == NULL)
        return failed;

    /* ---- the parameters, against what the engine reads off the graph ---- */
    {
        const clap_plugin_t *plugin =
            factory->create_plugin(factory, &host, desc->id);

        plugin->init(plugin);

        const clap_plugin_params_t *params = (const clap_plugin_params_t *)
            plugin->get_extension(plugin, CLAP_EXT_PARAMS);

        const uint32_t n = params ? params->count(plugin) : 0;
        bool same = n == gControls.size();
        std::string first;

        for (uint32_t i = 0; same && i < n; i++)
        {
            const Control &c = gControls[i];
            clap_param_info_t info;

            params->get_info(plugin, i, &info);

            if (i == 0)
                first = info.name;

            same = info.id == i && c.label == info.name &&
                   (float)info.min_value == c.min &&
                   (float)info.max_value == c.max &&
                   (float)info.default_value == c.def;

            if (!same)
                printf("        %s: plugin %s %g..%g at %g, graph %s "
                       "%g..%g at %g\n", c.name.c_str(), info.name,
                       info.min_value, info.max_value, info.default_value,
                       c.label.c_str(), c.min, c.max, c.def);
        }

        check(same, std::to_string(n) + " parameters, the graph's " +
                    std::to_string(gControls.size() - 1) +
                    " controls and the level, in its order, labels, "
                    "ranges and defaults" + (first.empty() ? "" :
                    ": " + first + " first"));

        clap_param_info_t level;

        memset(&level, 0, sizeof(level));

        if (n > 0)
            params->get_info(plugin, n - 1, &level);

        check(!strcmp(level.name, "Level") && level.default_value == 30 &&
              level.max_value == 127, "the level last, 0..127 at 30");

        plugin->destroy(plugin);
    }

    printf("      the phrase changes %s to %g\n",
           gControls[gParam].label.c_str(), gParamValue);

    /* ---- the latency it reports ---- */
    {
        const clap_plugin_t *plugin =
            factory->create_plugin(factory, &host, desc->id);

        plugin->init(plugin);
        plugin->activate(plugin, 48000, 1, 4096);

        const clap_plugin_latency_t *latency = (const clap_plugin_latency_t *)
            plugin->get_extension(plugin, CLAP_EXT_LATENCY);

        check(latency != NULL && latency->get(plugin) == (uint32_t)kWindow,
              "a latency of one window, 64 frames");

        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }

    /* ---- stopped and started again ---- */
    {
        const clap_plugin_t *plugin =
            factory->create_plugin(factory, &host, desc->id);

        plugin->init(plugin);

        std::vector<float> left(512), right(512);
        float *channels[2] = { &left[0], &right[0] };
        clap_audio_buffer_t buffer;
        Events events;
        clap_input_events_t in = { &events, eventsSize, eventsGet };
        clap_output_events_t outEvents = { NULL, eventsPush };
        clap_process_t process;

        memset(&buffer, 0, sizeof(buffer));
        buffer.data32 = channels;
        buffer.channel_count = 2;

        memset(&process, 0, sizeof(process));
        process.frames_count = 512;
        process.audio_outputs = &buffer;
        process.audio_outputs_count = 1;
        process.in_events = &in;
        process.out_events = &outEvents;

        /* A chord held when the host stops... */
        plugin->activate(plugin, 48000, 1, 4096);
        plugin->start_processing(plugin);

        for (int k = 0; k < 3; k++)
        {
            clap_event_midi_t e;

            memset(&e, 0, sizeof(e));
            e.header.size = sizeof(e);
            e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type = CLAP_EVENT_MIDI;
            e.data[0] = 0x90;
            e.data[1] = (uint8_t)(60 + 4 * k);
            e.data[2] = 100;
            events.midi.push_back(e);
        }

        events.index();

        double before = 0;

        for (int b = 0; b < 8; b++)
        {
            plugin->process(plugin, &process);
            events.clear();
            before = std::max(before, peak(left));
        }

        const bool sounding = before > 0;

        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);

        /* ...is not there when it starts again. */
        plugin->activate(plugin, 48000, 1, 4096);
        plugin->start_processing(plugin);

        double after = 0;

        for (int b = 0; b < 8; b++)
        {
            plugin->process(plugin, &process);
            after = std::max(after, peak(left));
        }

        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);

        check(sounding && after == 0,
              "a chord sounding when the host stops is gone when it starts "
              "again");
    }

    /* ---- the phrase, through both, at two rates ---- */

    const double rates[] = { 48000, 44100 };

    for (int r = 0; r < 2; r++)
    {
        Rendered raw, plugin, engine;
        char what[160];

        const bool ran = viaPlugin(factory, desc->id, rates[r], raw) &&
                         viaEngine(rates[r], engine);
        const bool silent = ran && pastLatency(raw, plugin);

        check(silent, "its first window is silence, the latency");

        size_t bad = 0;
        const bool same = silent &&
                          identical(plugin.left, engine.left, bad) &&
                          identical(plugin.right, engine.right, bad);

        if (same)
            snprintf(what, sizeof(what),
                     "at %g Hz the plugin plays what the engine plays one "
                     "window later, bit for bit (peak %.3f)", rates[r],
                     peak(engine.left));
        else
            snprintf(what, sizeof(what),
                     "at %g Hz the plugin plays what the engine plays one "
                     "window later, bit for bit (first difference at "
                     "frame %zu)", rates[r], bad);

        check(same, what);
        char loud[64];

        snprintf(loud, sizeof(loud), "and it is not silence: peak %.4f",
                 ran ? peak(plugin.left) : 0.0);
        check(ran && peak(plugin.left) > 0, loud);
    }

    /* ---- a project saved and opened again ---- */
    {
        const clap_plugin_t *probe =
            factory->create_plugin(factory, &host, desc->id);

        probe->init(probe);

        const clap_plugin_params_t *params = (const clap_plugin_params_t *)
            probe->get_extension(probe, CLAP_EXT_PARAMS);

        /* Up to three controls away from their defaults: the first, one
           in ms if the graph has one, and the level; each 30% of the way
           up its range, or down for one already above that. */
        Start first;
        std::vector<std::string> set;
        std::vector<int> picks;

        picks.push_back(0);

        for (size_t i = 0; i < gControls.size(); i++)
            if (gControls[i].units == "ms" && (int)i != picks[0])
            {
                picks.push_back((int)i);
                break;
            }

        if ((int)gControls.size() - 1 != picks[0])
            picks.push_back((int)gControls.size() - 1);

        for (size_t k = 0; k < picks.size(); k++)
        {
            const Control &c = gControls[picks[k]];
            double v = c.min + 0.3 * (c.max - c.min);

            if (fabs(v - c.def) < 1e-6 * (c.max - c.min + 1))
                v = c.min + 0.6 * (c.max - c.min);

            if (c.step == 1.0f || !c.valueNames.empty())
                v = floor(v + 0.5);

            Setting st = { (clap_id)picks[k], (double)(float)v };

            first.settings.push_back(st);
            set.push_back(c.label);
        }

        (void)params;

        probe->destroy(probe);

        check(!first.settings.empty(), std::to_string(set.size()) +
              " controls to set");

        Blob saved;
        Rendered a, b, plain;

        first.save = &saved;

        Start second;

        second.load = &saved;

        const bool ran =
            viaPlugin(factory, desc->id, 48000, a, first) &&
            viaPlugin(factory, desc->id, 48000, b, second) &&
            viaPlugin(factory, desc->id, 48000, plain);

        check(ran && !saved.empty(),
              "a state was saved, " + std::to_string(saved.size()) +
              " bytes, and loaded into a fresh instance");

        /* The values, read back off an instance that only loaded. */
        const clap_plugin_t *reader =
            factory->create_plugin(factory, &host, desc->id);

        reader->init(reader);

        Reader r = { &saved, 0 };
        clap_istream_t in = { &r, blobRead };
        const clap_plugin_state_t *state = (const clap_plugin_state_t *)
            reader->get_extension(reader, CLAP_EXT_STATE);
        const clap_plugin_params_t *rp = (const clap_plugin_params_t *)
            reader->get_extension(reader, CLAP_EXT_PARAMS);

        bool values = state != NULL && state->load(reader, &in);

        for (size_t i = 0; values && i < first.settings.size(); i++)
        {
            double v = 0;

            values = rp->get_value(reader, first.settings[i].id, &v) &&
                     (float)v == (float)first.settings[i].value;
        }

        reader->destroy(reader);

        std::string listed;

        for (size_t k = 0; k < set.size(); k++)
        {
            char buf[48];

            snprintf(buf, sizeof(buf), " %g", first.settings[k].value);
            listed += (k ? ", " : "") + set[k] + buf;
        }

        check(values, "and every value came back exactly: " + listed);

        size_t bad = 0;

        check(ran && identical(a.left, b.left, bad) &&
              identical(a.right, b.right, bad),
              "the reopened instance plays what the saved one played");
        check(ran && !identical(a.left, plain.left, bad),
              "which is not what the defaults play");
    }

    entry->deinit();
    dlclose(module);

    printf("\n%d failure(s)\n", failed);

    return failed;
}
