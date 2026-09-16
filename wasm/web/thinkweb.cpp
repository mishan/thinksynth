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
 * thinkweb -- the synth and the composer scheduler, for an AudioWorklet.
 *
 * worklet.js holds one of these and calls a handful of things: load a .dsp
 * or a .gen, press and release a key, move a knob, start the transport,
 * render a block. The synth runs in windows of its own length -- 256 in the
 * browser, JAM.md section 2 -- and a worklet asks for 128-frame quanta;
 * gthSynthSource is the ring between the two, the same one the sound card's
 * callback uses on the desktop.
 *
 * Everything here runs on the worklet's thread, the thread the desktop calls
 * the GUI's included: a command is queued, and the next process() applies
 * it. There is no second thread for the queue to protect, but it is the same
 * path, so the synth does not know it is in a browser.
 *
 * Commands carry the frame they apply at. A window is rendered whole, so a
 * command lands at the start of the window its frame falls in: early by less
 * than a window, never late, and at the same frame on every run and every
 * machine, which is what a stamp is for. One stamped in the past -- or -1 --
 * goes into the next window rendered, which is how a key pressed now plays.
 *
 * What a note does once it has landed is the engine's: a note added before
 * a window is silent through that window and sounds from the next, here as
 * on the desktop. So a note is heard one window after the window its stamp
 * falls in -- which is the other reason the browser wants a short window.
 *
 * THE SCHEDULER RUNS HERE, stepped once per window by the audio clock
 * itself, which is genwav's loop exactly: step the transport by a window,
 * render the window. SCHEDULER_PLACEMENT.md measured the alternative -- the
 * scheduler on the main thread, ahead of the audio clock -- and JAM.md
 * section 3 says why this is where it went: the scheduler holds the synth
 * and drives it directly, and much of what it does (the note-offs it
 * derives from durations, the chanarg writes a knob binding makes,
 * instrument application at load and rewind) never appears on the tape, so
 * a bridge carrying it would be a bridge whose bugs the tape cannot see.
 *
 * The tape it does deliver is posted back for the page to draw, and is what
 * M2's gate compares against genwav's for the same piece and the same
 * seconds. twevent.h is its layout, shared with the Node host so there is
 * one spelling of an event rather than two.
 */

#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <emscripten.h>

#include "think.h"

#include "gthSynthSource.h"

#include "thcGenFile.h"
#include "thcPlugin.h"
#include "thcScheduler.h"

#include "thDynLib.h"

#include "twevent.h"

/* The synth always mixes to two; so does this. */
#define TW_CHANNELS 2

/* Where a .dsp handed over as text is written, so the parser can open it
   like any other, and where a piece's instruments are looked for. The
   module's own MEMFS; nothing leaves the page.
 *
 * `dsp/' is one of the places thUtil::findDataFile tries relative to the
   working directory, which here is the root, so an `instrument { dsp =
   "amb01.dsp"; }' resolves with nothing configured -- exactly as it does in
   a source tree. */
#define TW_PATCH_FILE "/patch.dsp"
#define TW_PIECE_FILE "/piece.gen"
#define TW_DSP_DIR    "/dsp"

namespace {

/* One input, stamped with the frame it applies at.
 *
 * Every input is one of these, the page's own included: a knob moved, a key
 * pressed or a transport button has to be applied at the same point in the
 * piece on every peer or their tapes diverge from there on, so the local
 * page has no privileged access to the scheduler (JAM.md, section 3). It is
 * the nearest peer, and its commands take a remote peer's path.
 *
 * A load is the one input that is not stamped. It is a synchronous answer
 * -- did this parse? -- and in M2 a piece is loaded before its transport
 * runs, so there is no point in the piece for the stamp to name. M4's
 * apply-at-bar rule is where an edit acquires a beat. */
enum CmdType
{
    CMD_NOTE_ON,        /* the keyboard, straight to the loaded .dsp */
    CMD_NOTE_OFF,
    CMD_MIDI_ON,        /* the keyboard, into the piece's `input midi' */
    CMD_MIDI_OFF,
    CMD_TRANSPORT,
};

/* CMD_TRANSPORT's `op'. worklet.js spells these too. */
enum TransportOp
{
    TW_START,
    TW_STOP,
    TW_REWIND,
    TW_TEMPO,
};

struct Command
{
    double frame;
    int    type;
    int    op;                  /* CMD_TRANSPORT                       */
    int    channel;             /* CMD_NOTE_*, CMD_MIDI_*              */
    float  note, velocity;      /* CMD_NOTE_*, CMD_MIDI_*              */
    double value;               /* CMD_TRANSPORT's                     */
};

/* A command for the scheduler, stamped in transport seconds rather than
   frames (JAM_M3.md, section 1). A window is 5 ms and two peers' windows
   are not aligned to each other or to the origin, so a knob applied "at
   the top of the window containing t" lands at different transport times
   on different peers, and a composer that reads it at a tick between
   those two times composes two different pieces. So these are applied at
   `at' inside the step: the transport is stepped to `at', which runs the
   stages that tick at or before it, the command is applied, and the step
   goes on. The same on every peer, whatever its window or its rate.

   `at' below zero means the top of the next window, which is what the
   solo page sends and what a peer sends while the transport is stopped.
   One with a time that has already passed is applied at once and counted
   (late_): the tape has parted from the other peers' from that time on,
   and M3's job is to make that visible (JAM_M3.md, section 1). */
enum AtType
{
    AT_STOP,
    AT_TEMPO,
    AT_KNOB,
};

struct Scheduled
{
    double at;
    int    type;
    int    knob;                /* AT_KNOB: an index into knobs_       */
    double value;               /* AT_KNOB's value, AT_TEMPO's bpm     */
};

/* Room for this many commands in flight before the queue has to grow. */
#define TW_PENDING 1024

thSynth              *synth_;
gthSynthSource       *source_;
std::vector<float>    block_;
std::vector<Command>  pending_;     /* in order: see push() */
double                rendered_;    /* frames handed out so far */
double                rate_;

/* The scheduler's commands: the stamped ones in order of `at', and the
   ones for the top of the next window in order of arrival. */
std::vector<Scheduled> scheduled_;
std::vector<Scheduled> immediate_;
int                    late_;

/* Where transport zero is, as a frame of this synth's output, or -1 while
   the transport has never been started. Transport time at the end of a
   window is (frame - originFrame_) / rate_ exactly, and the step is taken
   to that rather than by adding a window's length each time, so the clock
   does not drift from the frames by a rounding error per window
   (JAM_M3.md, section 2, property 3). A begin sets it to the frame it was
   asked for; a resume sets it so that the transport continues from where
   it stopped. */
double originFrame_ = -1;

/* A begin waiting for its frame: at the window that frame falls in the
   transport is rewound and started, with a partial first step so that
   transport zero is that frame and not the start of its window. */
bool   armed_;
double armFrame_;

/* The piece side. plugins_ is built once, from the table the build wrote,
   and outlives every load; the loader clears the scheduler's chains itself. */
std::map<std::string, thcPlugin *> plugins_;
thcScheduler        *sched_;
thcGenLoader        *loader_;
std::vector<thArg *> knobs_;
twTape               tape_;
sigc::connection     delivery_;

/* Bumped by anything that makes the tape so far meaningless: a piece
   loaded, and a rewind, after which `at' starts again from zero. The page
   clears its roll when this changes rather than trying to read a rewind off
   the times. */
int epoch_;

/* Everything due before the end of the window about to be rendered, whose
   first frame is `start'.
 *
 * addNote is the desktop's GUI-thread call, and on the desktop it runs on
 * the GUI thread: it takes the synth's lock, uncontended here, and builds
 * the note's copy of the graph, which allocates. A worklet has no other
 * thread to put that on, so a key-on spends its render quantum on it --
 * measured at 0.02 to 0.38 ms across the shipped patches, against the
 * 2.67 ms a 128-frame quantum has at 48 kHz. Making a note-on free of
 * allocation is the engine's work, not this file's; JAM.md, section 7, has
 * it as risk 3. What this file keeps off the render path is its own: the
 * queue arrives sorted and has its room already. */
void applyDue (double start, int len)
{
    size_t k = 0;

    for (; k < pending_.size() && pending_[k].frame < start + len; k++)
    {
        const Command &c = pending_[k];

        switch (c.type)
        {
            case CMD_NOTE_ON:
                synth_->addNote(c.channel, c.note, c.velocity);
                break;

            case CMD_NOTE_OFF:
                synth_->delNote(c.channel, c.note);
                break;

            case CMD_MIDI_ON:
            case CMD_MIDI_OFF:
            {
                /* The `input midi;' route: every chain that declared it
                   and sinks to this channel sees the event, and what it
                   does with it is the composer's -- a Markov trains on it,
                   an arpeggiator holds it.
                 *
                 * Held, with a duration of zero, and the release is its
                 * own event. A composed note declares how long it lasts
                 * and the scheduler derives the off from that; a key held
                 * down has no idea, which is the whole reason the ABI has
                 * a THC_EV_NOTEOFF (gen/hands.gen says it at length). */
                thcEvent ev = {};

                ev.type = c.type == CMD_MIDI_ON ? THC_EV_NOTE
                                                : THC_EV_NOTEOFF;
                ev.at = sched_->now();
                ev.channel = c.channel;
                ev.u.note.note = (int)c.note;
                ev.u.note.velocity = (int)c.velocity;
                ev.u.note.duration = 0;

                sched_->injectMidiEvent(ev);
                break;
            }

            case CMD_TRANSPORT:
                switch (c.op)
                {
                    case TW_START:
                        /* A resume, the solo page's Play: the transport
                           goes on from where it is, from the start of
                           this window. */
                        sched_->start();
                        originFrame_ = start - sched_->now() * rate_;
                        break;

                    case TW_STOP:
                        sched_->stop();
                        break;

                    case TW_REWIND:
                        /* Whatever was stamped for the run being rewound
                           names a time that is about to mean something
                           else. */
                        sched_->reset();
                        scheduled_.clear();
                        epoch_++;
                        break;

                    case TW_TEMPO:
                        sched_->setTempo(c.value);
                        break;
                }
                break;
        }
    }

    pending_.erase(pending_.begin(), pending_.begin() + k);
}

/* A begin whose frame falls in the window about to be rendered. */
void beginDue (double start, int len)
{
    if (!armed_ || armFrame_ >= start + len)
        return;

    armed_ = false;

    /* From the top: the instances recreated from their seeds, the
       transport at zero, and nothing held over from the last run. */
    sched_->reset();
    scheduled_.clear();
    epoch_++;

    /* A begin whose frame has already gone by -- it arrived late, or was
       stamped for a frame this synth had already rendered -- starts now,
       and is counted: the peers that started on time are ahead of this
       one by the difference, for good. */
    if (armFrame_ < start)
    {
        originFrame_ = start;
        late_++;
    }
    else
        originFrame_ = armFrame_;

    sched_->start();
}

void applyScheduled (const Scheduled &c)
{
    switch (c.type)
    {
        case AT_STOP:
            sched_->stop();
            break;

        case AT_TEMPO:
            sched_->setTempo(c.value);
            break;

        case AT_KNOB:
            /* setValue is the whole knob path: every param bound to it
               reads through it, and the scheduler has the changed signal
               wired to whatever rebuilding or re-arming that implies
               (thcScheduler::bindKnob). By index into the list the page
               was handed at the load: a name would have to be copied into
               the command, and a copy has a length, and a knob whose name
               ran past it was silently never moved. */
            if (c.knob >= 0 && c.knob < (int)knobs_.size())
                knobs_[c.knob]->setValue((float)c.value);

            break;
    }
}

/* The transport across the window whose first frame is `start', with the
   scheduler's commands applied where they fall in it. */
void step (double start, int len)
{
    for (size_t i = 0; i < immediate_.size(); i++)
        applyScheduled(immediate_[i]);

    immediate_.clear();

    if (!sched_->running())
    {
        /* Time is not passing, so nothing stamped for later can come due;
           what is stamped for a time already passed is late wherever it
           lands, and lands now. */
        while (!scheduled_.empty() && scheduled_[0].at <= sched_->now())
        {
            const Scheduled c = scheduled_[0];

            scheduled_.erase(scheduled_.begin());
            late_++;
            applyScheduled(c);
        }

        return;
    }

    const double target = (start + len - originFrame_) / rate_;

    while (!scheduled_.empty() && scheduled_[0].at <= target)
    {
        const Scheduled c = scheduled_[0];

        scheduled_.erase(scheduled_.begin());

        if (c.at > sched_->now())
            sched_->stepTransportTo(c.at);
        else if (c.at < sched_->now())
            late_++;

        applyScheduled(c);

        /* A stop: the transport is where the stop said, and stays. */
        if (!sched_->running())
            return;
    }

    sched_->stepTransportTo(target);
}

/* In order of `at', arrival order within one, like push(). */
void schedule (const Scheduled &c)
{
    if (c.at < 0)
    {
        immediate_.push_back(c);
        return;
    }

    scheduled_.insert(std::upper_bound(scheduled_.begin(), scheduled_.end(),
                                       c,
                                       [](const Scheduled &a,
                                          const Scheduled &b)
                                       { return a.at < b.at; }),
                      c);
}

/* Kept in order as commands arrive -- by frame, and in arrival order within
   a frame, which upper_bound gives for nothing -- so the render path only
   ever takes from the front and never sorts. */
void push (const Command &c)
{
    pending_.insert(std::upper_bound(pending_.begin(), pending_.end(), c,
                                     [](const Command &a, const Command &b)
                                     { return a.frame < b.frame; }),
                    c);
}

void pushNote (double frame, int type, int channel, float note,
               float velocity)
{
    Command c = {};

    c.frame = frame;
    c.type = type;
    c.channel = channel;
    c.note = note;
    c.velocity = velocity;

    push(c);
}

/* The composers the build compiled in, opened once.
 *
 * genwav and the Node host scan plugins/composer/ for modules; there is no
 * directory here and nothing to dlopen, so the list is the one the table
 * carries (libthink/thDynLib.h). A composer that refuses to load is left
 * out, exactly as a module that refuses is, and a piece naming it fails at
 * the load with something to read. */
void openComposers (void)
{
    for (size_t i = 0; i < thStaticComposerCount; i++)
    {
        thcPlugin *p = new thcPlugin(thStaticComposers[i]);

        if (p->state() != thcPlugin::LOADED)
        {
            delete p;
            continue;
        }

        plugins_[p->name()] = p;
    }
}

bool writeFile (const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL)
        return false;

    fputs(text, f);

    return fclose(f) == 0;
}

} /* namespace */

extern "C" {

/* A synth at the page's rate and the given window, rendering blocks of up
   to maxFrames, and the scheduler that will drive it. Returns the window
   length the synth actually took. */
EMSCRIPTEN_KEEPALIVE int tw_create (int sampleRate, int windowlen,
                                    int maxFrames)
{
    synth_ = new thSynth("", windowlen, sampleRate);
    source_ = new gthSynthSource(synth_);
    rate_ = sampleRate;

    source_->prepare((unsigned)maxFrames, TW_CHANNELS);
    block_.assign((size_t)maxFrames * TW_CHANNELS, 0.0f);
    pending_.reserve(TW_PENDING);
    scheduled_.reserve(TW_PENDING);
    immediate_.reserve(TW_PENDING);

    mkdir(TW_DSP_DIR, 0777);

    openComposers();

    sched_ = new thcScheduler(synth_);
    loader_ = new thcGenLoader(plugins_);

    return synth_->getWindowlen();
}

/* A .dsp, as text, onto a channel in place of whatever was there. Nonzero
   if it parsed; the parser's complaints go to stderr, which the worklet
   forwards to the page.
 *
 * The channel is the caller's because a piece may want one: a chain that
 * takes `input midi' and sinks to channel 1 needs something on channel 1 to
 * sound, and the piece does not always declare it -- gen/hands.gen declares
 * no instruments at all and says to aim three channels at three patches by
 * hand. That is the desktop's Patch Selector, and on the page it is this.
 * Aiming one at a channel a piece's instrument is already on replaces it,
 * here as there. */
EMSCRIPTEN_KEEPALIVE int tw_load (int channel, const char *text)
{
    if (!writeFile(TW_PATCH_FILE, text))
        return 0;

    /* The queue is left alone. It used to be cleared here, when it held
       nothing but notes for the patch being replaced; now it also holds
       the stop a mode switch posts just ahead of this load and the
       releases of keys held into a piece, and clearing those left the
       piece running under patch mode and an arpeggiator holding a chord
       for ever. A note stamped for the old patch plays on the new one,
       which is nothing. */

    /* At the level the Patch Selector loads one at, on MIDI's 0..127 --
       gthPatchfile.cpp says why that level is where it is. */
    return synth_->loadTree(TW_PATCH_FILE, channel,
                            TH_DEFAULT_CHAN_AMP) != NULL ? 1 : 0;
}

/* One of the shipped .dsp files, as text, into the place a piece's
   instruments are looked up from. The page fetches these and hands them
   over before the first piece is loaded; a worklet cannot fetch. */
EMSCRIPTEN_KEEPALIVE int tw_instrument (const char *name, const char *text)
{
    const std::string path = std::string(TW_DSP_DIR) + "/" + name;

    return writeFile(path.c_str(), text) ? 1 : 0;
}

/* A .gen, as text. Nonzero if it parsed and built; tw_error_count and
   tw_error say what did not, in the loader's own words and with line
   numbers, because a piece is something a person is editing.
 *
 * Everything the transport was doing stops first. A load rebuilds the
 * chains under the scheduler, and the queue may hold commands stamped for
 * a piece that is about to stop existing.
 *
 * No thcScheduler::setChannelTaken hook, deliberately, and this is
 * load-bearing: the loader gives each instrument the lowest channel nothing
 * else has claimed, and what "nothing else" means is exactly that hook
 * (thcGenFile.cpp, allocateChannels). genwav installs none, so a piece
 * loaded here must find the same channels free or its tape would name
 * different ones -- and the tape is what M2's gate compares. The cost is
 * that a piece takes channel 0 from the .dsp the keyboard was playing,
 * which is why the page has the two as modes rather than side by side. A
 * keyboard still reaches a piece: through `input midi', as tw_midi_on, the
 * way a peer's keyboard will. */
/* `seed' is the master seed to compose from when the piece pins none, or
   below zero to draw one, as the desktop does. Two peers composing from
   different seeds are playing different pieces, so in a room the one who
   presses Play picks it and everyone loads with it (JAM_M3.md, section
   5.3). A piece that pins its own is not moved by this: the loader sets
   the file's after. */
EMSCRIPTEN_KEEPALIVE int tw_piece_load (const char *text, double seed)
{
    delivery_.disconnect();
    pending_.clear();
    scheduled_.clear();
    immediate_.clear();
    armed_ = false;
    originFrame_ = -1;
    tape_.clear();
    knobs_.clear();

    sched_->stop();

    if (!writeFile(TW_PIECE_FILE, text))
        return 0;

    /* setMasterSeed takes effect only while no stage exists, which is
       what the loader is about to make true anyway; made true here first
       so the seed is in before the first stage draws from it. */
    if (seed >= 0)
    {
        sched_->clearChains();
        sched_->setMasterSeed((unsigned)seed);
    }

    const bool ok = loader_->load(TW_PIECE_FILE, sched_);

    epoch_++;

    if (!ok)
        return 0;

    /* In the map's order, which is by name: the .gen's own order is not
       kept anywhere, and a page drawing sliders wants some order. */
    for (const auto &k : sched_->knobs())
        knobs_.push_back(k.second);

    /* Listening starts after the load, where genwav connects too, so the
       instrument application the load itself delivers is not on the tape. */
    delivery_ = sched_->sigDelivered.connect(
        sigc::mem_fun(tape_, &twTape::deliver));

    return 1;
}

EMSCRIPTEN_KEEPALIVE int tw_error_count (void)
{
    return (int)loader_->errors().size();
}

EMSCRIPTEN_KEEPALIVE const char *tw_error (int k)
{
    if (k < 0 || k >= (int)loader_->errors().size())
        return "";

    return loader_->errors()[k].c_str();
}

EMSCRIPTEN_KEEPALIVE const char *tw_piece_name (void)
{
    return loader_->pieceName().c_str();
}

EMSCRIPTEN_KEEPALIVE const char *tw_piece_description (void)
{
    return loader_->pieceDescription().c_str();
}

/* Whether the file pins its seed, and the seed the piece is composing
   from either way -- the file's, the one handed to the load, or the one
   drawn. What a peer has to send with Play for the others to load with. */
EMSCRIPTEN_KEEPALIVE int tw_piece_seeded (void)
{
    return loader_->hasSeed() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE double tw_seed (void)
{
    return sched_->masterSeed();
}

/* ---- the instruments the piece declared, and the channels it listens on ---- */

EMSCRIPTEN_KEEPALIVE int tw_instrument_count (void)
{
    return (int)sched_->instruments().size();
}

EMSCRIPTEN_KEEPALIVE const char *tw_instrument_name (int k)
{
    return k >= 0 && k < (int)sched_->instruments().size()
        ? sched_->instruments()[k].name.c_str() : "";
}

EMSCRIPTEN_KEEPALIVE int tw_instrument_channel (int k)
{
    return k >= 0 && k < (int)sched_->instruments().size()
        ? sched_->instruments()[k].channel : -1;
}

/* Does a chain take `input midi' on this channel? A key arriving on a
   channel the piece listens on goes into the piece; on any other it goes
   straight to whatever instrument is there. The page's rule for a seat's
   keys, answered by the piece rather than guessed at. */
EMSCRIPTEN_KEEPALIVE int tw_listens (int channel)
{
    for (size_t i = 0; i < sched_->chainCount(); i++)
    {
        const thcChain *c = sched_->chain(i);

        if (!c->inputMidi)
            continue;

        if (c->sinks.empty())
            return 1;

        for (size_t k = 0; k < c->sinks.size(); k++)
            if (c->sinks[k].channel == channel)
                return 1;
    }

    return 0;
}

/* ---- the knobs the piece declared ---- */

EMSCRIPTEN_KEEPALIVE int tw_knob_count (void)
{
    return (int)knobs_.size();
}

EMSCRIPTEN_KEEPALIVE const char *tw_knob_name (int k)
{
    return k >= 0 && k < (int)knobs_.size() ? knobs_[k]->name().c_str() : "";
}

EMSCRIPTEN_KEEPALIVE const char *tw_knob_label (int k)
{
    return k >= 0 && k < (int)knobs_.size() ? knobs_[k]->label().c_str() : "";
}

/* 0 for one the piece marked hidden, which a page draws no slider for. */
EMSCRIPTEN_KEEPALIVE int tw_knob_shown (int k)
{
    return k >= 0 && k < (int)knobs_.size() &&
           knobs_[k]->widgetType() != thArg::HIDE;
}

EMSCRIPTEN_KEEPALIVE double tw_knob_min (int k)
{
    return k >= 0 && k < (int)knobs_.size() ? knobs_[k]->min() : 0;
}

EMSCRIPTEN_KEEPALIVE double tw_knob_max (int k)
{
    return k >= 0 && k < (int)knobs_.size() ? knobs_[k]->max() : 0;
}

EMSCRIPTEN_KEEPALIVE double tw_knob_value (int k)
{
    return k >= 0 && k < (int)knobs_.size() ? (*knobs_[k])[0] : 0;
}

/* `@x.step = 1', or 0 for a knob the piece left continuous. */
EMSCRIPTEN_KEEPALIVE double tw_knob_step (int k)
{
    return k >= 0 && k < (int)knobs_.size() ? knobs_[k]->step() : 0;
}

/* ---- stamped commands ---- */

EMSCRIPTEN_KEEPALIVE void tw_note_on (double frame, int channel, float note,
                                      float velocity)
{
    pushNote(frame, CMD_NOTE_ON, channel, note, velocity);
}

EMSCRIPTEN_KEEPALIVE void tw_note_off (double frame, int channel, float note)
{
    pushNote(frame, CMD_NOTE_OFF, channel, note, 0);
}

/* The same key, into the piece rather than straight onto its channel: the
   chains that declared `input midi' and sink to this channel receive it,
   and what sounds is whatever they emit. */
EMSCRIPTEN_KEEPALIVE void tw_midi_on (double frame, int channel, float note,
                                      float velocity)
{
    pushNote(frame, CMD_MIDI_ON, channel, note, velocity);
}

EMSCRIPTEN_KEEPALIVE void tw_midi_off (double frame, int channel, float note)
{
    pushNote(frame, CMD_MIDI_OFF, channel, note, 0);
}

EMSCRIPTEN_KEEPALIVE void tw_transport (double frame, int op, double value)
{
    Command c = {};

    c.frame = frame;
    c.type = CMD_TRANSPORT;
    c.op = op;
    c.value = value;

    push(c);
}

/* A start from the top, with transport zero at `originFrame' exactly. A
   frame already rendered, or below zero, starts at the next window and
   counts as late. */
EMSCRIPTEN_KEEPALIVE void tw_begin (double originFrame)
{
    armed_ = true;
    armFrame_ = originFrame;
}

/* A stop or a tempo, at transport time `at', inside the step. TW_START and
   TW_REWIND are frame-stamped -- before a start there is no transport time
   to stamp with -- and are refused here. */
EMSCRIPTEN_KEEPALIVE void tw_at (double at, int op, double value)
{
    Scheduled c = {};

    c.at = at;
    c.value = value;

    switch (op)
    {
        case TW_STOP:  c.type = AT_STOP; break;
        case TW_TEMPO: c.type = AT_TEMPO; break;
        default:       return;
    }

    schedule(c);
}

/* Knob `k' of the loaded piece -- tw_knob_count's numbering -- to `value',
   at transport time `at'. An index outside the list is ignored. */
EMSCRIPTEN_KEEPALIVE void tw_knob (double at, int k, double value)
{
    Scheduled c = {};

    c.at = at;
    c.type = AT_KNOB;
    c.knob = k;
    c.value = value;

    schedule(c);
}

/* Every sounding note released, now. */
EMSCRIPTEN_KEEPALIVE void tw_all_off (void)
{
    pending_.clear();
    synth_->clearAll();
}

/* ---- the transport, and what it delivered ---- */

EMSCRIPTEN_KEEPALIVE double tw_now (void)
{
    return sched_->now();
}

EMSCRIPTEN_KEEPALIVE int tw_running (void)
{
    return sched_->running() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int tw_epoch (void)
{
    return epoch_;
}

/* How many commands have been applied after the time they were stamped
   for, since the module was made. Every one is a point from which this
   peer's tape may differ from the others'. */
EMSCRIPTEN_KEEPALIVE int tw_late (void)
{
    return late_;
}

/* The frame transport zero falls on, or -1 before any start. */
EMSCRIPTEN_KEEPALIVE double tw_origin (void)
{
    return originFrame_;
}

EMSCRIPTEN_KEEPALIVE int tw_event_count (void)
{
    return (int)tape_.count();
}

EMSCRIPTEN_KEEPALIVE const twEvent *tw_events (void)
{
    return tape_.data();
}

EMSCRIPTEN_KEEPALIVE void tw_events_clear (void)
{
    tape_.clear();
}

/* `frames' frames, interleaved stereo, in a buffer that is overwritten by
   the next call. Windows are rendered as they are needed; the commands due
   in each are applied just before it, and then the transport is stepped
   across it.
 *
 * That order is genwav's, and it has to be: a command is meant to be in
 * force for the window it lands in, and what the step delivers is meant to
 * sound in the window it is delivered for. The step is to the transport
 * time the window's last frame falls on, counted from the origin, and by
 * nothing measured, so the piece is a function of the file and the seed
 * and not of the clock -- which is the property the step-size fix put in
 * the scheduler and this is the first host to lean on (JAM.md, section 3).
 * The scheduler's own commands are applied inside the step, at the time
 * each was stamped for; step() above says why. */
EMSCRIPTEN_KEEPALIVE const float *tw_render (int frames)
{
    const int len = synth_->getWindowlen();

    for (int done = 0; done < frames; )
    {
        unsigned held = source_->pending();

        /* Nothing held: the next render() makes a window, and its first
           frame is the next one handed out. */
        if (held == 0)
        {
            applyDue(rendered_, len);
            beginDue(rendered_, len);
            step(rendered_, len);
            held = (unsigned)len;
        }

        const int n = std::min(frames - done, (int)held);

        source_->render(&block_[(size_t)done * TW_CHANNELS], (unsigned)n,
                        TW_CHANNELS);

        done += n;
        rendered_ += n;
    }

    return block_.data();
}

/* The frame the next tw_render starts at. */
EMSCRIPTEN_KEEPALIVE double tw_frame (void)
{
    return rendered_;
}

} /* extern "C" */
