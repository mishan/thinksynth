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
 * browser, docs/JAM.md -- and a worklet asks for 128-frame quanta;
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
 * render the window. The alternative -- the scheduler on the main thread,
 * ahead of the audio clock -- was measured and rejected, and docs/JAM.md
 * says why this is where it went: the scheduler holds the synth and drives
 * it directly, and much of what it does (the note-offs it derives from
 * durations, the chanarg writes a knob binding makes, instrument
 * application at load and rewind) never appears on the tape, so a bridge
 * carrying it would be a bridge whose bugs the tape cannot see.
 *
 * The tape it does deliver is posted back for the page to draw, and is what
 * the tape gate compares against genwav's for the same piece and the same
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

#include "cairo2d.h"
#include "cairomm/context.h"

#include "twdraw.h"

#include "ComposerCanvas.h"
#include "thcGenEdit.h"

#include "ArgPanel.h"
#include "DspCatalog.h"
#include "GenCatalog.h"
#include "JsonOut.h"
#include "PatchApply.h"
#include "PatchFile.h"
#include "PatchSet.h"
#include "KnobPanel.h"

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

/* Where the shipped pieces are kept, for the menu's sake alone: the piece
   being played is written to TW_PIECE_FILE above and loaded from there. */
#define TW_GEN_DIR    "/gen"
#define TW_DSP_DIR    "/dsp"

namespace {

/* One input, stamped with the frame it applies at.
 *
 * Every input is one of these, the page's own included: a knob moved, a key
 * pressed or a transport button has to be applied at the same point in the
 * piece on every peer or their tapes diverge from there on, so the local page
 * has no privileged access to the scheduler (docs/JAM.md). It is the nearest
 * peer, and its commands take a remote peer's path.
 *
 * A load is the one input that is not stamped. It is a synchronous answer
 * -- did this parse? -- and a piece is loaded before its transport runs, so
 * there is no point in the piece for the stamp to name. The apply-at-bar
 * rule still to come is where an edit acquires a beat. */
enum CmdType
{
    CMD_NOTE_ON,        /* the keyboard, straight to the loaded .dsp */
    CMD_NOTE_OFF,
    CMD_MIDI_ON,        /* the keyboard, into the piece's `input midi' */
    CMD_MIDI_OFF,
    CMD_TRANSPORT,
};

/* CMD_TRANSPORT's `op', and a Scheduled's. worklet.js spells the first
   four too; TW_KNOB and TW_INPUT have entry points of their own and
   never arrive as an op from there. */
enum TransportOp
{
    TW_START,
    TW_STOP,
    TW_REWIND,
    TW_TEMPO,
    TW_KNOB,
    TW_INPUT,
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
   frames. A window is 5 ms and two peers' windows are not aligned to
   each other or to the origin, so a knob applied "at the top of the
   window containing t" lands at different transport times on different
   peers, and a composer that reads it at a tick between those two times
   composes two different pieces. So these are applied at `at' inside the
   step: the transport is stepped to `at', which runs the stages that
   tick at or before it, the command is applied, and the step goes on.
   The same on every peer, whatever its window or its rate.

   `at' below zero means the top of the next window, which is what the
   solo page sends and what a peer sends while the transport is stopped.
   Those sort ahead of every stamped one and are applied first in the
   step; they are never late, because "now" cannot have gone by. One with
   a time that has already passed is applied at once and counted (late_):
   the tape has parted from the other peers' from that time on, and the
   room page's job is to make that visible. */
struct Scheduled
{
    double at;
    int    op;                  /* TW_STOP, TW_TEMPO, TW_KNOB, TW_INPUT */
    int    knob;                /* TW_KNOB: an index into knobs_       */
    double value;               /* TW_KNOB's value, TW_TEMPO's bpm     */

    /* TW_INPUT: a gesture on a stage's picture, in the coordinates the
       draw was handed. The stage is named by chain and stage index --
       the canvas's own key, and the same on every peer holding the same
       document revision -- and w and h come along because the ABI
       requires them: the draw's size is the host's business and an
       enlarged view is the same draw at a different size. Every peer
       inverts the same arithmetic and reaches the same cell. */
    int    chain, stage;
    int    kind;                /* thcInputType                        */
    double x, y, w, h;
    int    button;
};

/* Room for this many commands in flight before the queue has to grow. */
#define TW_PENDING 1024

thSynth              *synth_;
gthSynthSource       *source_;
std::vector<float>    block_;
std::vector<Command>  pending_;     /* in order: see push() */
double                rendered_;    /* frames handed out so far */
double                rate_;

/* The scheduler's commands, in order of `at' and in arrival order within
   one: the ones for the top of the next window carry an `at' below zero
   and so come first, and the stamped ones follow in the order the step
   will want them. */
std::vector<Scheduled> scheduled_;
int                    late_;

/* Where transport zero is, as a frame of this synth's output, or -1 while
   the transport has never been started. Transport time at the end of a
   window is (frame - originFrame_) / rate_ exactly, and the step is taken
   to that rather than by adding a window's length each time, so the clock
   does not drift from the frames by a rounding error per window. A begin
   sets it to the frame it was asked for; a resume sets it so that the
   transport continues from where it stopped. */
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

/* The channels the loaded piece's sinks name that no instrument of its
   own occupies -- the ones a reader is being asked to aim. Collected at
   the load, like knobs_: a sink's channel is fixed once
   allocateChannels has run. */
std::vector<int>     sinks_;

/* "channel:name" for every chanarg override that named something the tree
   on that channel does not declare. Kept so the complaint is made once:
   every piece load re-aims, and a .patch carrying a stale name would
   otherwise say so again on every one of them. Never cleared -- a .patch
   is a file on the server, and it does not stop being wrong. */
std::vector<std::string> unknownChanargs_;
twTape               tape_;
sigc::connection     delivery_;

/* Bumped by anything that makes the tape so far meaningless: a piece
   loaded, and a rewind, after which `at' starts again from zero. The page
   clears its roll when this changes rather than trying to read a rewind off
   the times. */
int epoch_;

/* Whatever was stamped for the run that is ending names a transport time
   that is about to mean something else, so it goes. What is stamped for
   "the top of the next window" -- an `at' below zero -- is not for a run
   at all and stays: it arrived a moment ago and has not been stepped over
   yet. */
void dropStamped (void)
{
    scheduled_.erase(std::remove_if(scheduled_.begin(), scheduled_.end(),
                                    [](const Scheduled &c)
                                    { return c.at >= 0; }),
                     scheduled_.end());
}

/* Everything due before the end of the window about to be rendered, whose
   first frame is `start'.
 *
 * addNote is the desktop's GUI-thread call, and on the desktop it runs on the
 * GUI thread: it takes the synth's lock, uncontended here, and builds the
 * note's copy of the graph, which allocates. A worklet has no other thread to
 * put that on, so a key-on spends its render quantum on it -- measured at 0.02
 * to 0.38 ms across the shipped patches, against the 2.67 ms a 128-frame
 * quantum has at 48 kHz. Making a note-on free of allocation is the engine's
 * work, not this file's; docs/JAM.md, has it as risk 3. What this file keeps
 * off the render path is its own: the queue arrives sorted and has its room
 * already. */
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
                        sched_->reset();
                        dropStamped();
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

    /* From the top: the instances recreated from their seeds and the
       transport at zero. The queue is not touched: what is in it arrived
       after the arm, stamped for the run that is starting here, and was
       held through the arm window by step() below. The run being left
       behind had its commands dropped at the arm (tw_begin), and before
       that at the load (tw_piece_load). */
    sched_->reset();
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

/* A stage by chain and stage index, or NULL. The index pair is the
   canvas's own key and is the same on every peer holding the same document
   revision, so it is what the page names a picture by. Out of range is
   answered rather than trusted: these indices come off a page. */
thcStage *stageAt (int chain, int stage)
{
    if (sched_ == NULL || chain < 0 || (size_t)chain >= sched_->chainCount())
        return NULL;

    const thcChain *c = sched_->chain((size_t)chain);

    if (c == NULL || stage < 0 || (size_t)stage >= c->stages.size())
        return NULL;

    return c->stages[(size_t)stage].get();
}

void applyScheduled (const Scheduled &c)
{
    switch (c.op)
    {
        case TW_STOP:
            sched_->stop();
            break;

        case TW_TEMPO:
            sched_->setTempo(c.value);
            break;

        case TW_KNOB:
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

        case TW_INPUT:
        {
            /* Straight into the plugin's own state, at `at', inside the
               step -- before any stage ticks at or after it, which is
               the property every scheduler-facing command has here. The
               stage is named by index, and an index that names nothing
               is dropped and said rather than applied to a neighbour:
               these come off a page, and a page's idea of the piece can
               be a revision behind. */
            thcStage *st = stageAt(c.chain, c.stage);

            if (st == NULL || st->plugin == NULL || st->state == NULL)
            {
                fprintf(stderr, "input for stage %d.%d, which is not "
                                "there\n", c.chain, c.stage);
                break;
            }

            if (!st->plugin->hasInput())
                break;          /* its picture is not a control */

            thcInputEvent ev = {};

            ev.type = (thcInputType)c.kind;
            ev.x = c.x;
            ev.y = c.y;
            ev.w = c.w;
            ev.h = c.h;
            ev.button = c.button;

            st->plugin->input(st->state, &ev);
            break;
        }
    }
}

/* The transport across the window whose first frame is `start', with the
   scheduler's commands applied where they fall in it. */
void step (double start, int len)
{
    /* The ones for the top of this window, in arrival order: they sort
       ahead of everything stamped, they are what "now" means to a solo
       page and to a peer whose transport is stopped, and a time that has
       not been named cannot have gone by, so none of them is late. */
    size_t k = 0;

    for (; k < scheduled_.size() && scheduled_[k].at < 0; k++)
        applyScheduled(scheduled_[k]);

    scheduled_.erase(scheduled_.begin(), scheduled_.begin() + k);

    if (!sched_->running())
    {
        /* A begin is armed: the run these stamps are in is a window or
           two away and its transport zero is not here yet. Nothing is
           due and nothing is late; they wait for it. */
        if (armed_)
            return;

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

/* In order of `at', arrival order within one, like push(). An `at' below
   zero is "the top of the next window", and every one of those is below
   every stamped one, so they land at the front in the order they came. */
void schedule (const Scheduled &c)
{
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

/* Which channels the piece is asking somebody else to fill.
 *
 * Both note sinks and chanarg sinks count: a chanarg sink writes a knob
 * of whatever is on the channel and needs something there as much as a
 * note does. A channel one of the piece's own instrument blocks took is
 * not among them -- the piece has aimed that one itself, and a default
 * dropped on it would take the piece's instrument away.
 *
 * An instrument sink carries its instrument's channel by now
 * (thcGenFile.cpp, allocateChannels), so the two spellings need no
 * distinguishing here: the exclusion covers both. */
void collectSinks (void)
{
    sinks_.clear();

    for (size_t i = 0; i < sched_->chainCount(); i++)
    {
        const thcChain *c = sched_->chain(i);

        for (size_t k = 0; k < c->sinks.size(); k++)
        {
            const int ch = c->sinks[k].channel;

            /* A programmatic chain may have none at all, and a sink
               whose instrument went missing keeps the loader's -1. */
            if (ch < 0)
                continue;

            if (sched_->channelOf(ch) != NULL)
                continue;

            if (std::find(sinks_.begin(), sinks_.end(), ch) == sinks_.end())
                sinks_.push_back(ch);
        }
    }

    std::sort(sinks_.begin(), sinks_.end());
}

/* ---- the composer canvas, in a module ----
 *
 * The desktop's ComposerCanvas, compiled again here and given a shell of
 * page and worker instead of a gtk widget. What it draws, what it lays
 * out and what a click on it means are the desktop's, unchanged; what
 * this class is, is the four answers a shell owes it.
 *
 * It lives in the mirror, beside the scheduler whose stages it draws. The
 * worklet has one of these too -- it is the same module -- and never
 * touches it.
 */
class WebComposerCanvas : public ComposerCanvas
{
public:
    WebComposerCanvas (void)
        : dirty_(true), width_(0), height_(0),
          viewX_(0), viewY_(0), viewW_(0), viewH_(0) {}

    /* Whether anything has asked to be drawn again since this was last
       asked. The shell draws on an animation frame when it has, which is
       what queue_draw buys on the desktop. */
    bool takeDirty (void)
    {
        const bool was = dirty_;

        dirty_ = false;
        return was;
    }

    int width (void) const { return width_; }
    int height (void) const { return height_; }

    /* What the page can see of the drawing, in shell pixels: the scroll
       position and the element's size. The enlarged stage is laid out
       against this, so a canvas that was never told would put it across
       the whole drawing and somewhere off screen. */
    void setViewport (double x, double y, double w, double h)
    {
        viewX_ = x;
        viewY_ = y;
        viewW_ = w;
        viewH_ = h;

        shellResized();
        dirty_ = true;
    }

protected:
    void requestRedraw (void) override { dirty_ = true; }

    void resizeShell (int w, int h) override
    {
        width_ = w;
        height_ = h;
        dirty_ = true;
    }

    bool shellViewport (double &x, double &y, double &w,
                        double &h) const override
    {
        if (viewW_ <= 0.0 || viewH_ <= 0.0)
            return false;

        x = viewX_;
        y = viewY_;
        w = viewW_;
        h = viewH_;

        return true;
    }

private:
    bool dirty_;
    int width_, height_;
    double viewX_, viewY_, viewW_, viewH_;
};

WebComposerCanvas *canvas_ = NULL;
Cairo::RefPtr<Cairo::Context> canvasContext_;

/* A params popover the canvas asked for: which stage, and where its box
   is in shell pixels so the page can put the panel beside it. The last
   one asked for, since a second request replaces the first -- there is
   one popover. */
struct CanvasParams
{
    int    chain, stage;
    int    x, y, w, h;
    bool   wanted;

    CanvasParams (void) : chain(-1), stage(-1), x(0), y(0), w(0), h(0),
                          wanted(false) {}
};

CanvasParams canvasParams_;

/* The gestures the canvas took and did not hand to a plugin, waiting for
   the shell to turn each into a command. See tw_canvas_input_count(). */
struct CanvasInput
{
    int    chain, stage, kind, button;
    double x, y, w, h;
};

std::vector<CanvasInput> canvasInputs_;

/* The piece as thcGenEdit reads it back: the authored spellings, the
   chains and their stages in order, which is what the canvas lays out.
   Kept because the canvas holds a pointer to it. */
thcGenEdit::Doc canvasDoc_;

/* ---- the one index a stage has across this boundary ----
 *
 * Every tw_* call that names a stage names it by its place in the
 * *scheduler's* list -- tw_stage_count counts that list, stageAt() reads
 * it, and the worklet, which applies the commands, has nothing else: it
 * never calls tw_canvas_show and so has no document to count instead.
 *
 * The canvas counts the document, because that is what it draws, and a
 * dsp node is a stage in the file with no thcStage behind it. So the two
 * translate here, at the edge, and nowhere else. Read raw in either
 * direction, a chain with an LFO at the top hands back its neighbour's
 * params, or paints on the wrong picture, or -- for the last composer in
 * such a chain -- silently does nothing at all.
 */
int liveOf (int chain, int docStage)
{
    if (chain < 0 || (size_t)chain >= canvasDoc_.chains.size())
        return -1;

    return thcGenEdit::liveIndex(canvasDoc_.chains[(size_t)chain],
                                 (size_t)docStage);
}

int docOf (int chain, int liveStage)
{
    if (chain < 0 || (size_t)chain >= canvasDoc_.chains.size())
        return -1;

    return thcGenEdit::docIndex(canvasDoc_.chains[(size_t)chain], liveStage);
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

    mkdir(TW_DSP_DIR, 0777);

    openComposers();

    sched_ = new thcScheduler(synth_);
    loader_ = new thcGenLoader(plugins_);

    return synth_->getWindowlen();
}

/* Where this module's frame counter starts, in the host's numbering.
 *
 * rendered_ counts from zero at tw_create, which runs when the module has
 * finished instantiating -- and in a browser the audio context has been
 * running for a while by then, its own frame counter already some
 * thousands of frames past zero, while process() handed out silence. But
 * every frame that crosses this boundary is in the host's numbering: the
 * origin a Play arms with comes from the page's audio clock, and tw_frame
 * and tw_origin are read there against the same clock. Left unaligned,
 * this peer's transport zero lands its own setup time after the instant
 * the room agreed on -- a different amount on every peer, and invisible
 * to every peer, since each one's stamps are inflated by its own.
 *
 * The host calls this once before the first tw_render, with the frame
 * that render begins at. Nothing has been handed out yet, so there is
 * nothing numbered the old way. */
EMSCRIPTEN_KEEPALIVE void tw_align (double frame)
{
    rendered_ = frame;
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

    /* An effect graph is `fx/echo.dsp': a name with a directory in it,
       looked up under dsp/ the way a piece's `effect' clause spells it.
       MEMFS does not make parents on a write, so each one is made here,
       and one that already exists is not an error. */
    for (size_t slash = path.find('/', strlen(TW_DSP_DIR) + 1);
         slash != std::string::npos; slash = path.find('/', slash + 1))
        mkdir(path.substr(0, slash).c_str(), 0777);

    return writeFile(path.c_str(), text) ? 1 : 0;
}

/* What the page's menus are drawn from: every .dsp this module has been
 * handed, with the title and the description its author wrote and whether it
 * is an effect graph or an instrument.
 *
 * A directory walk, exactly as on the desktop, because by here there is a
 * directory to walk: tw_instrument above has written every shipped graph into
 * MEMFS under /dsp, `fx/' and all, and DspCatalog::scan is happy with a
 * filesystem that lives in a heap. So the page gets the same reading of the
 * same headers the application gets -- not a second one in JavaScript, which
 * is how the .patch format came to be read two different ways.
 *
 * Scanned on the call rather than kept up to date: the page asks once, after
 * it has handed the graphs over. The dump is dspCatalogToJson's, diffed
 * against the native harness's byte for byte
 * (wasm/web/dspcatalogcheck.mjs). Valid until the next call.
 */
static std::string dspsJson_;

EMSCRIPTEN_KEEPALIVE const char *tw_dsps_json (void)
{
    DspCatalog catalog;

    catalog.scan(TW_DSP_DIR);

    dspsJson_ = dspCatalogToJson(catalog);

    return dspsJson_.c_str();
}

/* One of the shipped .gen files, as text, so that the menu can say what it
 * is. Nothing plays from here -- a piece is loaded by handing its text to
 * tw_piece_load, which writes TW_PIECE_FILE -- and nothing needs to: what
 * this is for is the same thing tw_instrument turned out to be for, a
 * directory to walk.
 *
 * The page could have kept the headers to itself and parsed them in
 * JavaScript. It did that for .patch once and the two readings drifted. */
EMSCRIPTEN_KEEPALIVE int tw_gen_file (const char *name, const char *text)
{
    const std::string path = std::string(TW_GEN_DIR) + "/" + name;

    mkdir(TW_GEN_DIR, 0777);

    return writeFile(path.c_str(), text) ? 1 : 0;
}

/* What the page's piece menu is drawn from: every .gen handed over above, by
 * category, with the title and the description its author wrote.
 *
 * Read through thcGenEdit, which is the .gen reader the Composer edits
 * pieces with -- so the browser's menu and the Composer's Open are the same
 * list (src/GenCatalog.h). The dump is genCatalogToJson's, diffed against
 * scripts/gencheck --json byte for byte
 * (wasm/web/gencatalogcheck.mjs). Valid until the next call. */
static std::string gensJson_;

EMSCRIPTEN_KEEPALIVE const char *tw_gens_json (void)
{
    GenCatalog catalog;

    catalog.scan(TW_GEN_DIR);

    gensJson_ = genCatalogToJson(catalog);

    return gensJson_.c_str();
}

/* And the same for a wav, which is the one shipped file that is not text.
 *
 * osc::sample looks a file up with thUtil::findDataFile under `samples/',
 * so what it wants is bytes at /dsp/samples/<name> -- the same MEMFS the
 * .dsp files above are written into, by the same page, before the first
 * piece is loaded. `name' carries the `samples/' on it, the way
 * tw_instrument's carries an `fx/', because both are index.json entries
 * and index.json lists a path rather than a basename.
 *
 * A length and a pointer rather than a string: a wav has a NUL in its
 * header before it has anything else in it. */
EMSCRIPTEN_KEEPALIVE int tw_sample (const char *name, const char *bytes,
                                    int len)
{
    const std::string path = std::string(TW_DSP_DIR) + "/" + name;

    for (size_t slash = path.find('/', strlen(TW_DSP_DIR) + 1);
         slash != std::string::npos; slash = path.find('/', slash + 1))
        mkdir(path.substr(0, slash).c_str(), 0777);

    if (bytes == NULL || len < 0)
        return 0;

    FILE *f = fopen(path.c_str(), "wb");

    if (f == NULL)
        return 0;

    const bool whole =
        fwrite(bytes, 1, (size_t)len, f) == (size_t)len;

    return (fclose(f) == 0 && whole) ? 1 : 0;
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
 * different ones -- and the tape is what the gate compares. The cost is
 * that a piece takes channel 0 from the .dsp the keyboard was playing,
 * which is why the page has the two as modes rather than side by side. A
 * keyboard still reaches a piece: through `input midi', as tw_midi_on, the
 * way a peer's keyboard will. */
/* `seed' is the master seed to compose from when the piece pins none, or
   below zero to draw one, as the desktop does. Two peers composing from
   different seeds are playing different pieces, so in a room the one who
   presses Play picks it and everyone loads with it. A piece that pins
   its own is not moved by this: the loader sets the file's after. */
EMSCRIPTEN_KEEPALIVE int tw_piece_load (const char *text, double seed)
{
    delivery_.disconnect();
    pending_.clear();
    scheduled_.clear();
    armed_ = false;
    originFrame_ = -1;
    tape_.clear();
    knobs_.clear();
    sinks_.clear();

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
    {
        /* The canvas holds a thcStage * per box and the load has just
           freed every one of them. On the way out through the other door
           the page calls tw_canvas_show, which hands it the new ones; on
           this one nothing does -- mirror.js only re-shows on a load that
           worked -- so the draw loop would go on reading the old ones
           every frame. Emptied here, which is also what is true: there is
           no piece. */
        if (canvas_ != NULL)
            canvas_->SetPiece(NULL, NULL);

        return 0;
    }

    /* In the map's order, which is by name: the .gen's own order is not
       kept anywhere, and a page drawing sliders wants some order. */
    for (const auto &k : sched_->knobs())
        knobs_.push_back(k.second);

    collectSinks();

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

/* The .dsp an instrument plays, which is what connects a file in the
   document to a channel in the synth -- what the node editor needs to know
   before it can arm a tap on the instrument it is showing. */
EMSCRIPTEN_KEEPALIVE const char *tw_instrument_dsp (int k)
{
    return k >= 0 && k < (int)sched_->instruments().size()
        ? sched_->instruments()[k].dsp.c_str() : "";
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

/* ---- the channels the piece is asking somebody to aim ---- */

/* How many distinct channels the loaded piece's sinks name that no
 * instrument of its own occupies, and which they are.
 *
 * What a channel sounds like is the piece's to decide, and where the
 * piece is silent on the matter it is the page's defaults -- never what
 * the page did before. This is the question the page has to ask to keep
 * that rule: gen/fern.gen declares no instrument and sinks to two
 * channels, and a page that puts nothing on them renders three and a half
 * thousand notes at a peak of zero.
 *
 * The engine's numbering, as everything here is. A .gen file writes
 * `channel = 4' and the loader hands over 3.
 */
EMSCRIPTEN_KEEPALIVE int tw_sink_count (void)
{
    return (int)sinks_.size();
}

EMSCRIPTEN_KEEPALIVE int tw_sink_channel (int k)
{
    return k >= 0 && k < (int)sinks_.size() ? sinks_[k] : -1;
}

/* A chanarg on the tree loaded on `channel', through the path the
 * application's slider uses. Nonzero if it was set.
 *
 * This is the second half of loading a .patch: a .patch is a `dsp' line and
 * flat `name value[,value]' overrides for that DSP's chanargs
 * (docs/DSP_FORMAT.md), and gthPatchManager::parse loads the one and then
 * sets the others in exactly this order, at exactly this level --
 * TH_DEFAULT_CHAN_AMP, which tw_load already applies.
 *
 * A name the tree does not declare is ignored, as tw_knob ignores an
 * index outside the list, and said rather than passed over: an override
 * for a chanarg that does not exist is a .patch aimed at some other .dsp,
 * and the page's log is where a person would look. Said once per channel
 * and name -- re-aiming happens at every piece load, and a thing repeated
 * at every load is a thing nobody reads.
 */
EMSCRIPTEN_KEEPALIVE int tw_chanarg (int channel, const char *name,
                                     const float *values, int count)
{
    if (name == NULL || values == NULL || count < 1)
        return 0;

    thArg *arg = synth_->getChanArg(channel, name);

    if (arg == NULL)
    {
        const std::string said = std::to_string(channel) + ":" + name;

        if (std::find(unknownChanargs_.begin(), unknownChanargs_.end(),
                      said) == unknownChanargs_.end())
        {
            unknownChanargs_.push_back(said);

            /* channel + 1, alone among the numbers here: this line is
               read by a person in the page's log, beside the page's own
               complaints, and those count channels the way a .gen file
               does. Everything else this file says is said to the page. */
            fprintf(stderr, "channel %d declares no '%s'; the override is "
                            "ignored\n", channel + 1, name);
        }

        return 0;
    }

    /* setValue for the single-value case, which is every line in the
       shipped patches and cannot reallocate; setChanArg for a longer one,
       which can, and which is the call that queues the swap for the
       render path. thSynth.cpp argues both. */
    if (count == 1 && arg->type() == thArg::ARG_VALUE && arg->len() == 1)
        arg->setValue(values[0]);
    else
        synth_->setChanArg(channel, new thArg(name, values, count));

    return 1;
}

/* ---- a .patch, read and put on a channel ----
 *
 * The page fetches the file and hands over its text; everything the bytes
 * mean happens here, in the same code the application runs
 * (src/PatchFile.h, src/PatchApply.h).
 *
 * It used to be a second parser in wasm/web/patch.js, written from
 * docs/DSP_FORMAT.md separately, and it had drifted: an `effect' line
 * parsed as a chanarg whose value was not a number and was dropped, so a
 * patch with a channel effect on it sounded different in a browser and
 * said nothing about why; `side' went to the engine as a chanarg called
 * `side'. Both of those arrive for free now, by deletion -- there is no
 * new page code behind them.
 *
 * What does not cross is finding the file. A browser has no PATCH_PATH and
 * should not pretend to; the page has fetch() and an index.json, and hands
 * text in exactly as it already does for tw_load and tw_instrument.
 */

static std::string patchWhy_;
static std::string patchJson_;

/* What each channel was given, and whether it has been edited since.
 *
 * The same slots the application keeps (src/PatchSet.h), instantiated rather
 * than reached through a singleton -- which is what the singleton being the
 * application's idea, and staying there, is for. The signals go unconnected:
 * a page polls, and there is nothing on this side of the ABI to hang a
 * handler on. */
static thPatchSet patches_(TH_MIDI_CHANNELS);

/* Reads `text' and puts it on `channel': the graph it names, its side, its
 * effect and its overrides, in the order the format requires.
 *
 * 1 if the patch is on the channel. 0 leaves the channel exactly as it was,
 * with tw_patch_why saying which of the two things went wrong -- the text
 * is not a patch, or the graph it names would not load.
 *
 * The .dsp is not fetched here either. The page hands every shipped graph
 * to tw_instrument before the first load, and a patch's `dsp' line is
 * resolved against those -- the same lookup a piece's `instrument' block
 * uses, and the same one the application makes under DSP_PATH.
 *
 * `name' is what to call the slot -- `leads/SuperRes.patch', the name the
 * page fetched it by. The text does not carry it and a document does not
 * know it, and it is what a Save would offer back; "" for a patch that came
 * from nowhere a name would mean anything.
 */
EMSCRIPTEN_KEEPALIVE int tw_patch_apply (int channel, const char *text,
                                         const char *name)
{
    patchWhy_.clear();

    if (text == NULL)
    {
        patchWhy_ = "no patch text";
        return 0;
    }

    thPatchDoc doc;

    if (!thPatchParse(text, doc, patchWhy_))
        return 0;

    const thPatchApplied got = thPatchApply(synth_, channel, doc);

    /* Both lists, in the order they happened: the lines the reader could not
       use, then what the channel could not do with the rest. The page logs
       them; neither is a failure on its own. */
    for (size_t i = 0; i < doc.complaints.size(); i++)
        fprintf(stderr, "channel %d: %s\n", channel + 1,
                doc.complaints[i].c_str());

    for (size_t i = 0; i < got.complaints.size(); i++)
        fprintf(stderr, "channel %d: %s\n", channel + 1,
                got.complaints[i].c_str());

    if (!got.ok)
    {
        patchWhy_ = got.why;
        return 0;
    }

    /* What went on, not what was asked for: the effect may have failed and
       the side may have been clamped. */
    doc.effect = got.effect;
    doc.side = got.side;

    /* Not dirty: this is what the text said, and nothing has changed it
       yet. A chanarg edit through tw_panel_edit is what does. */
    patches_.put(channel, doc, name != NULL ? name : "", false);

    return 1;
}

/* What a .patch says, without putting it anywhere.
 *
 * The reading on its own: the same thPatchParse the application runs, with no
 * channel and no synth involved, returned as the document it made. "" for
 * text that is not a patch, with tw_patch_why saying so.
 *
 * Its own entry point rather than a mode of tw_patch_apply because applying
 * changes the answer -- an effect that would not load is not in the document
 * the channel ends up with -- and what the parity gate has to compare is the
 * reading, which is the part compiled twice. It is also what a menu wants: a
 * patch's `info title' is in the file, and showing it should not mean loading
 * the patch to find out.
 */
EMSCRIPTEN_KEEPALIVE const char *tw_patch_read (const char *text)
{
    patchWhy_.clear();
    patchJson_.clear();

    thPatchDoc doc;

    if (text != NULL && thPatchParse(text, doc, patchWhy_))
        patchJson_ = thPatchDocToJson(doc);
    else if (text == NULL)
        patchWhy_ = "no patch text";

    return patchJson_.c_str();
}

/* Why the last tw_patch_apply or tw_patch_read refused, or "". Valid until
   the next call. */
EMSCRIPTEN_KEEPALIVE const char *tw_patch_why (void)
{
    return patchWhy_.c_str();
}

/* What is on `channel', as the document it was given -- its graph, its
 * effect, its info and its overrides. "" for a channel no patch has been put
 * on.
 *
 * The dump is thPatchDocToJson's, which is compiled into the native harness
 * as well and diffed against this byte for byte (wasm/web/patchcheck.mjs):
 * one reading of the format, or the build fails. Valid until the next call.
 */
EMSCRIPTEN_KEEPALIVE const char *tw_patch_json (int channel)
{
    const thPatchSet::Slot *slot = patches_.get(channel);

    patchJson_.clear();

    if (slot != NULL)
    {
        /* The document, and what the slot knows that the file does not: what
           it was called, whether it has been edited since it was read, and
           which load this is. The page draws a row out of these. */
        patchJson_ = thPatchDocToJson(slot->doc);
        patchJson_.erase(patchJson_.size() - 1);      /* the closing brace */
        patchJson_ += ",\"name\":";
        jsonString(patchJson_, slot->filename);
        patchJson_ += ",\"dirty\":";
        patchJson_ += slot->dirty ? "true" : "false";
        patchJson_ += ",\"generation\":";
        jsonUnsigned(patchJson_, slot->generation);
        patchJson_ += "}";
    }

    return patchJson_.c_str();
}

/* What a Save would write for `channel': the bytes of a .patch, or "" for a
 * channel nothing has been put on.
 *
 * The graph, the effect, the side and the info come off the slot; the values
 * come off the live channel, which is the whole point -- what a Save is for
 * is writing down what somebody has moved. Both halves are shared with the
 * application (thPatchCapture, thPatchCompose), so these are the bytes the
 * desktop writes, which is what makes this worth having at all: a .patch
 * downloaded from a page opens in the application, and neither shell
 * rewrites the other's file on its first save.
 *
 * `stamp' is the date for the banner comment -- the one thing in the output
 * that a document cannot know. The application passes ctime()'s line; a page
 * passes a Date. There is no locale here to make one from.
 *
 * This is the first time a browser can save a patch at all. It does not
 * write anything: a page has nowhere to write to, so what it gets is the
 * bytes and what it does with them is its own business. tw_patch_saved is
 * how it says it kept them.
 */
EMSCRIPTEN_KEEPALIVE const char *tw_patch_compose (int channel,
                                                   const char *stamp)
{
    const thPatchSet::Slot *slot = patches_.get(channel);

    patchJson_ = (slot == NULL)
        ? std::string()
        : thPatchCompose(thPatchCapture(synth_, channel, slot->doc),
                         stamp != NULL ? stamp : "");

    return patchJson_.c_str();
}

/* And that it was kept, under this name.
 *
 * The other half of tw_patch_dirty, and the reason the name is an argument:
 * a page downloading a patch has named the file it went into. Separate from
 * composing because composing is a read and this is not -- a page that asked
 * what a Save would write and then thought better of it has not saved
 * anything.
 */
EMSCRIPTEN_KEEPALIVE int tw_patch_saved (int channel, const char *name)
{
    if (!patches_.loaded(channel))
        return 0;

    patches_.markSaved(channel, name != NULL ? name : "");

    return 1;
}

/* What belongs on a channel nothing has aimed: the first-run configuration,
 * by relative name (src/PatchSet.h). "" for a channel with no answer.
 *
 * The page fetches what this names; it does not get the bytes from here, for
 * the reason nothing else does either. What it does get is the rule -- a
 * channel above the last entry takes the one at `c mod count' -- which used
 * to be written down twice, once here as an array in patch.js and once in
 * gthPrefs.cpp, with a comment on the page's promising they were the same.
 */
EMSCRIPTEN_KEEPALIVE const char *tw_patch_default (int channel)
{
    patchJson_ = thPatchDefaultFor(channel);

    return patchJson_.c_str();
}

/* How many there are, so a page can fetch each of them once before the first
   load -- the aiming happens inside a load, and a load has no time to wait
   for the network. */
EMSCRIPTEN_KEEPALIVE int tw_patch_default_count (void)
{
    return thPatchDefaultCount();
}

/* A channel's patch has been edited.
 *
 * The browser's markDirty. On the desktop, moving a control goes through
 * ArgPanelView, which says so to the patch manager; here the same edit
 * arrives as a command and is applied by tw_panel_edit, which says so to
 * the slots. Nothing writes a .patch by itself in either shell, so this flag
 * is the whole of the record that the file and the channel have parted
 * company. */
EMSCRIPTEN_KEEPALIVE int tw_patch_dirty (int channel)
{
    return patches_.isDirty(channel) ? 1 : 0;
}

/* ---- the piece's chains and stages, and their pictures ----
 *
 * What a composer draws is what the composer view shows: a Life board, a
 * CA's grid, a Euclid ring, drawn by the plugin itself through the same
 * composer_draw the desktop calls. Here the cairo it draws through is
 * cairo-canvas2d, which records rather than rasterises, so a draw is a list
 * of ops the page replays on a Canvas2D.
 *
 * The three tables below -- the ops, the strings they index, the surfaces
 * they blit -- stay valid until the next draw. The page reads them out of
 * the heap between the two.
 */

EMSCRIPTEN_KEEPALIVE int tw_chain_count (void)
{
    return sched_ != NULL ? (int)sched_->chainCount() : 0;
}

EMSCRIPTEN_KEEPALIVE const char *tw_chain_name (int chain)
{
    if (sched_ == NULL || chain < 0 || (size_t)chain >= sched_->chainCount())
        return "";

    const thcChain *c = sched_->chain((size_t)chain);

    return c != NULL ? c->name.c_str() : "";
}

/* The channel a chain plays, in the engine's numbering, or -1.
 *
 * Its first note sink's, and deliberately only that: a chain may fan out
 * to several, and a chanarg sink is not a voice at all. What asks is a
 * view that wants to say which instrument a track is heard on, and a
 * track with two answers is a track that view should not be drawing as
 * one row. */
EMSCRIPTEN_KEEPALIVE int tw_chain_channel (int chain)
{
    if (sched_ == NULL || chain < 0 || (size_t)chain >= sched_->chainCount())
        return -1;

    const thcChain *c = sched_->chain((size_t)chain);

    if (c == NULL)
        return -1;

    for (size_t i = 0; i < c->sinks.size(); i++)
        if (!c->sinks[i].isChanarg())
            return c->sinks[i].channel;

    return -1;
}

EMSCRIPTEN_KEEPALIVE int tw_stage_count (int chain)
{
    if (sched_ == NULL || chain < 0 || (size_t)chain >= sched_->chainCount())
        return 0;

    const thcChain *c = sched_->chain((size_t)chain);

    return c != NULL ? (int)c->stages.size() : 0;
}

EMSCRIPTEN_KEEPALIVE const char *tw_stage_name (int chain, int stage)
{
    const thcStage *s = stageAt(chain, stage);

    return s != NULL && s->plugin != NULL ? s->plugin->name().c_str() : "";
}

/* Whether this stage has a picture at all. Nine composers draw and the
   rest do not, and the canvas shows an empty box for the rest. */
EMSCRIPTEN_KEEPALIVE int tw_stage_draws (int chain, int stage)
{
    const thcStage *s = stageAt(chain, stage);

    return s != NULL && s->plugin != NULL && s->plugin->hasDraw() ? 1 : 0;
}

/* Draw one stage at w x h, and answer with the length of the list. Zero is
   a stage that drew nothing; -1 is no such stage, or one that does not
   draw. */
EMSCRIPTEN_KEEPALIVE int tw_stage_draw (int chain, int stage, double w,
                                        double h)
{
    thcStage *s = stageAt(chain, stage);

    if (s == NULL || s->plugin == NULL || !s->plugin->hasDraw() ||
        s->state == NULL)
        return -1;

    cairo_t *cr = twDrawingBegin();

    s->plugin->draw(s->state, cr, w, h);

    return cairo2d_op_words(cr);
}

/* ---- the composer canvas ----
 *
 * One canvas per instance, made on the first call. The mirror's is the one
 * that matters: it draws the stages of the scheduler it shares a heap
 * with, which are the instances that are composing what is being heard.
 *
 * The list a draw produces is the same three tables a stage's picture
 * produces -- tw_draw_ops and its neighbours -- because a stage's picture
 * is drawn inside the canvas's own list anyway, by the plugin, through the
 * cairo the canvas handed it.
 */

/* Show the piece that is loaded. Called after a piece message; nonzero if
   the .gen described. The worklet never calls this, which is why it is not
   part of the load. */
EMSCRIPTEN_KEEPALIVE int tw_canvas_show (void)
{
    std::string why;

    if (canvas_ == NULL)
    {
        canvas_ = new WebComposerCanvas();

        /* A gesture on an enlarged picture does not reach the plugin from
           here. It leaves as a command, is stamped, goes round the mesh
           and comes back at its time -- to this instance as to every
           other. Connecting this is what tells the canvas so; the desktop
           connects nothing and the plugin hears the click at once, as it
           always has. */
        /* A stage's params handle was clicked. The canvas says which
           stage and where its box is; what a form looks like is the
           page's business, on this platform as on the desktop. */
        canvas_->sigParams.connect(
            [](size_t chain, size_t stage, CanvasRect at)
            {
                canvasParams_.chain = (int)chain;
                canvasParams_.stage = liveOf((int)chain, (int)stage);
                canvasParams_.x = at.x;
                canvasParams_.y = at.y;
                canvasParams_.w = at.w;
                canvasParams_.h = at.h;
                canvasParams_.wanted = true;
            });

        canvas_->sigInput.connect(
            [](size_t chain, size_t stage, const thcInputEvent &ev)
            {
                CanvasInput in;

                in.chain = (int)chain;
                in.stage = liveOf((int)chain, (int)stage);
                in.kind = (int)ev.type;
                in.button = ev.button;
                in.x = ev.x;
                in.y = ev.y;
                in.w = ev.w;
                in.h = ev.h;

                canvasInputs_.push_back(in);
            });
    }

    if (thcGenEdit::describe(TW_PIECE_FILE, canvasDoc_, why) !=
        thcGenEdit::OK)
    {
        fprintf(stderr, "the composer view cannot read the piece: %s\n",
                why.c_str());
        canvas_->SetPiece(NULL, NULL);
        return 0;
    }

    canvas_->SetPiece(&canvasDoc_, sched_);

    return 1;
}

/* Draw it at w x h, and answer with the length of the list. */
EMSCRIPTEN_KEEPALIVE int tw_canvas_draw (int w, int h)
{
    if (canvas_ == NULL)
        return -1;

    cairo_t *cr = twDrawingBegin();

    if (!canvasContext_)
        canvasContext_ = Cairo::Context::create(cr);

    canvas_->draw(canvasContext_, w, h);

    return cairo2d_op_words(cr);
}

/* The gestures, in shell pixels, as the desktop's controllers deliver
   them. The conversion to the drawing's own coordinates is the content's,
   on every platform, which is what stops a click landing somewhere else at
   a zoom nobody tested at. */
EMSCRIPTEN_KEEPALIVE void tw_canvas_press (double x, double y, int button,
                                           int nPress)
{
    if (canvas_ != NULL)
        canvas_->pressAt(x, y, button, nPress);
}

EMSCRIPTEN_KEEPALIVE void tw_canvas_motion (double x, double y)
{
    if (canvas_ != NULL)
        canvas_->motionTo(x, y);
}

EMSCRIPTEN_KEEPALIVE void tw_canvas_release (double x, double y, int button)
{
    if (canvas_ != NULL)
        canvas_->releaseAt(x, y, button);
}

/* CanvasContent::Key, not a keysym: the shell maps its own spelling to
   one of these, as the gtk shell maps GDK_KEY_Escape. */
EMSCRIPTEN_KEEPALIVE int tw_canvas_key (int key)
{
    return canvas_ != NULL &&
           canvas_->keyPressed((CanvasContent::Key)key) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void tw_canvas_viewport (double x, double y, double w,
                                              double h)
{
    if (canvas_ != NULL)
        canvas_->setViewport(x, y, w, h);
}

EMSCRIPTEN_KEEPALIVE double tw_canvas_zoom (void)
{
    return canvas_ != NULL ? canvas_->zoom() : 1.0;
}

EMSCRIPTEN_KEEPALIVE void tw_canvas_set_zoom (double z)
{
    if (canvas_ != NULL)
        canvas_->setZoom(z);
}

EMSCRIPTEN_KEEPALIVE void tw_canvas_zoom_to_fit (void)
{
    if (canvas_ != NULL)
        canvas_->zoomToFit();
}

/* The width alone, which is what the composer view opens at: the drawing
   is one row per chain and wants reading across, not shrinking until ten
   of them fit the box (CanvasContent::zoomToWidth). */
EMSCRIPTEN_KEEPALIVE void tw_canvas_zoom_to_width (void)
{
    if (canvas_ != NULL)
        canvas_->zoomToWidth();
}

/* How big the drawing is, in shell pixels, for the scroller around it. */
EMSCRIPTEN_KEEPALIVE int tw_canvas_width (void)
{
    return canvas_ != NULL ? canvas_->width() : 0;
}

EMSCRIPTEN_KEEPALIVE int tw_canvas_height (void)
{
    return canvas_ != NULL ? canvas_->height() : 0;
}

/* ---- the gestures the canvas wants sent ----
 *
 * Drained by the shell after every press, drag and release it delivered:
 * each one becomes an `input' command, stamped and sent, and comes back
 * to this instance at its time like anyone else's. The shell sends at
 * most one drag per animation frame, which is the rate a knob's slider
 * already produces.
 */

EMSCRIPTEN_KEEPALIVE int tw_canvas_input_count (void)
{
    return (int)canvasInputs_.size();
}

EMSCRIPTEN_KEEPALIVE void tw_canvas_inputs_clear (void)
{
    canvasInputs_.clear();
}

#define TW_INPUT_FIELD(name, type, member, empty)                          \
    EMSCRIPTEN_KEEPALIVE type tw_canvas_input_##name (int k)               \
    {                                                                      \
        return k >= 0 && k < (int)canvasInputs_.size()                     \
            ? canvasInputs_[k].member : empty;                             \
    }

TW_INPUT_FIELD(chain,  int,    chain,  -1)
TW_INPUT_FIELD(stage,  int,    stage,  -1)
TW_INPUT_FIELD(kind,   int,    kind,   -1)
TW_INPUT_FIELD(button, int,    button,  0)
TW_INPUT_FIELD(x,      double, x,     0.0)
TW_INPUT_FIELD(y,      double, y,     0.0)
TW_INPUT_FIELD(w,      double, w,     0.0)
TW_INPUT_FIELD(h,      double, h,     0.0)

#undef TW_INPUT_FIELD

/* Where a stage's params handle is, in shell pixels: the three little
   sliders in its title bar. Exported for the reason the desktop makes it
   public -- the only other way to find out is to repeat the layout
   arithmetic, and a caller that repeated it would be testing its own
   copy of it. */
#define TW_HANDLE(which, member)                                           \
    EMSCRIPTEN_KEEPALIVE double tw_canvas_handle_##which (int chain,       \
                                                          int stage)       \
    {                                                                      \
        double x = 0, y = 0;                                               \
                                                                           \
        if (canvas_ == NULL ||                                             \
            !canvas_->paramsHandle((size_t)chain, (size_t)stage, x, y))     \
            return -1.0;                                                   \
                                                                           \
        return member;                                                     \
    }

TW_HANDLE(x, x)
TW_HANDLE(y, y)

#undef TW_HANDLE

/* ---- the params the canvas asked for ----
 *
 * Drained by the shell after a gesture, like the input queue: nonzero
 * when somebody clicked a stage's params handle, and then the four
 * numbers say where to put the popover.
 */

EMSCRIPTEN_KEEPALIVE int tw_canvas_params_wanted (void)
{
    const bool was = canvasParams_.wanted;

    canvasParams_.wanted = false;

    return was ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int tw_canvas_params_chain (void)
{
    return canvasParams_.chain;
}

EMSCRIPTEN_KEEPALIVE int tw_canvas_params_stage (void)
{
    return canvasParams_.stage;
}

#define TW_PARAMS_AT(name, member)                                         \
    EMSCRIPTEN_KEEPALIVE int tw_canvas_params_##name (void)                \
    {                                                                      \
        return canvasParams_.member;                                       \
    }

TW_PARAMS_AT(x, x)
TW_PARAMS_AT(y, y)
TW_PARAMS_AT(w, w)
TW_PARAMS_AT(h, h)

#undef TW_PARAMS_AT

/* Which stage the canvas has enlarged, or -1: the one a gesture would
   reach, and what the page labels the view with. */
EMSCRIPTEN_KEEPALIVE int tw_canvas_enlarged_chain (void)
{
    return canvas_ != NULL &&
           canvas_->enlarged().kind == ComposerCanvas::Selection::STAGE
        ? (int)canvas_->enlarged().chain : -1;
}

EMSCRIPTEN_KEEPALIVE int tw_canvas_enlarged_stage (void)
{
    if (canvas_ == NULL ||
        canvas_->enlarged().kind != ComposerCanvas::Selection::STAGE)
        return -1;

    /* Out in the scheduler's numbering, which is the only one the page
       and the worklet share -- see liveOf. */
    return liveOf((int)canvas_->enlarged().chain,
                  (int)canvas_->enlarged().index);
}

/* Where the enlarged picture is, in the content's own coordinates -- the
   shell multiplies by the zoom to reach its own pixels. Zero width when
   nothing is enlarged.
 *
   Exported because the alternative is a shell that repeats the layout
   arithmetic and then tests its own copy of it. Same reason the desktop's
   harnesses can ask. */
#define TW_ENLARGED(name, which)                                           \
    EMSCRIPTEN_KEEPALIVE double tw_canvas_enlarged_##name (void)           \
    {                                                                      \
        double a[4];                                                       \
                                                                           \
        if (canvas_ == NULL || !canvas_->enlargedArea(a[0], a[1], a[2],    \
                                                      a[3]))               \
            return 0.0;                                                    \
                                                                           \
        return a[which];                                                   \
    }

TW_ENLARGED(x, 0)
TW_ENLARGED(y, 1)
TW_ENLARGED(w, 2)
TW_ENLARGED(h, 3)

#undef TW_ENLARGED

/* Enlarge a stage, or put it back with a chain below zero. The canvas
   does this itself on a double click; the page has a button too. */
EMSCRIPTEN_KEEPALIVE void tw_canvas_enlarge (int chain, int stage)
{
    if (canvas_ == NULL)
        return;

    ComposerCanvas::Selection sel;

    if (chain >= 0)
    {
        /* In, from the scheduler's numbering to the canvas's: a
           Selection carries the document's stage number. */
        const int at = docOf(chain, stage);

        if (at < 0)
            return;

        sel.kind = ComposerCanvas::Selection::STAGE;
        sel.chain = (size_t)chain;
        sel.index = (size_t)at;
    }

    canvas_->setEnlarged(sel);
}

/* Has anything asked for a redraw since this was last asked? The shell
   draws on the next animation frame when it has. */
EMSCRIPTEN_KEEPALIVE int tw_canvas_dirty (void)
{
    return canvas_ != NULL && canvas_->takeDirty() ? 1 : 0;
}

/* ---- the knobs the piece declared ----
 *
 * One accessor, where there were eight. What a knob is -- its label, its
 * range, the resolution worth showing it at -- is a panel now
 * (src/KnobPanel.cpp), read through tw_panel_open(KNOB) like any other; all
 * that is left here is the number a command names one by, which is a panel
 * row's id and is wanted the other way round when a peer's command arrives.
 */

EMSCRIPTEN_KEEPALIVE int tw_knob_index (const char *name)
{
    if (name == NULL)
        return -1;

    for (size_t i = 0; i < knobs_.size(); i++)
        if (knobs_[i]->name() == name)
            return (int)i;

    return -1;
}

/* ---- parameter panels ----
 *
 * One family for every panel the page draws, over the description in
 * src/PanelModel.h. What it replaces is four descriptions of a panel written
 * three times each -- as widgets, as a flat C ABI of ten accessors, and as
 * DOM -- and what it buys immediately is the channel-arg panel, which this
 * page has never had at all: a patch's parameters could be set by a .patch
 * file and by the node editor, and not by a person.
 *
 * A panel is opened by naming what it is about, not by an index handed out
 * here. `kind' is thPanel::Kind; `a' and `b' are the channel, or the chain
 * and the stage, or the box. That is what lets an edit be a command: an
 * intent carries the same three numbers and the row's id, so the peer that
 * applies it needs no panel open and no agreement about row numbering.
 *
 * The open panel is kept because the page reads it twice -- once as JSON
 * when it appears, and then value by value while it is up. Rows are tens,
 * not the thousands of ops a drawing is, so the JSON is written fresh each
 * time it is asked for rather than cached.
 */

static ArgPanel argPanel_;
static KnobPanel knobPanel_;
static thPanel openPanel_;
static std::string panelJson_;
static std::string panelWhy_;

/* What the open panel is about, held here rather than read back off
   openPanel_: a rebuild that finds nothing leaves an empty panel behind, and
   an empty panel has forgotten which of a channel's two arg maps it was
   over. -1 for none open. */
static int panelKind_ = -1;
static int panelA_ = -1;
static int panelB_ = 0;

/* Configures the provider and builds. False for a panel with no rows, which
   is a channel with nothing on it. */
static bool buildPanel (int kind, int a, int b)
{
    openPanel_ = thPanel();

    switch (kind)
    {
        case thPanel::CHANARG:
            argPanel_ = ArgPanel();
            argPanel_.setChannel(a);

            /* b as a flag rather than a second subject: a channel's two
               chanarg maps are the instrument's and the effect's, and which
               of them a panel is over is the prefix its lookups carry. */
            argPanel_.setPrefix(b != 0 ? TH_EFFECT_PREFIX : "");

            return argPanel_.build(openPanel_);

        case thPanel::KNOB:
            /* No subject: a piece has one set of knobs. knobs_ is the list
               a command numbers them by, rebuilt at every load, and a row's
               id is its place in it. */
            knobPanel_.setKnobs(knobs_);

            return knobPanel_.build(openPanel_);
    }

    return false;
}

/* Opens a panel and returns its shape, or 0 when there is none.
 *
 * A shape rather than a handle: the page has one panel up at a time and
 * names the next one the same way it named this one, so there is nothing for
 * a handle to disambiguate -- and what the page does want back is the number
 * it polls to find out whether its widgets are still the right ones. */
EMSCRIPTEN_KEEPALIVE unsigned tw_panel_open (int kind, int a, int b)
{
    panelWhy_.clear();

    if (!buildPanel(kind, a, b))
    {
        panelKind_ = -1;

        return 0;
    }

    panelKind_ = kind;
    panelA_ = a;
    panelB_ = b;

    return openPanel_.shape;
}

/* The whole panel. Valid until the next call to any of these. */
EMSCRIPTEN_KEEPALIVE const char *tw_panel_json (void)
{
    panelJson_ = thPanelToJson(openPanel_);

    return panelJson_.c_str();
}

/* Rebuilds from live state and returns the shape.
 *
 * The rebuild is the point: a panel follows the arg, and the values it holds
 * after this are the ones tw_panel_value reads. What the page compares is
 * the number, which changes only when rows appear, vanish or change
 * editability -- so a MIDI controller moving a control costs a poll and not
 * a torn-down panel. */
EMSCRIPTEN_KEEPALIVE unsigned tw_panel_shape (void)
{
    if (panelKind_ < 0)
        return 0;

    if (!buildPanel(panelKind_, panelA_, panelB_))
        return 0;

    return openPanel_.shape;
}

/* One row's value, in the units its row is drawn in. The cheap poll: no
   string crosses, and nothing is serialized. */
EMSCRIPTEN_KEEPALIVE double tw_panel_value (int row)
{
    if (panelKind_ < 0 || row < 0 || row >= (int)openPanel_.rows.size())
        return 0;

    double value = openPanel_.rows[row].value;

    switch (openPanel_.kind)
    {
        case thPanel::CHANARG:
            argPanel_.valueFor(openPanel_.rows[row].id, value);
            break;
        case thPanel::KNOB:
            knobPanel_.valueFor(openPanel_.rows[row].id, value);
            break;
        default:
            break;
    }

    return value;
}

/* An edit, named in full so that it can have arrived from anywhere.
 *
 * This is the far end of a command: the page posts the intent, every peer
 * receives it, and each of them calls this -- including the peer that typed
 * it, which has no privileged path to the synth (docs/JAM.md). So it takes
 * the panel's subject rather than reading the open one; the instance that
 * applies an edit need never have drawn the panel it came from.
 *
 * Returns 1 if the arg moved, 0 otherwise -- which covers both a refusal
 * and an intent that matched what was already there. tw_panel_why is empty
 * for the second, since catching up is not an error. */
EMSCRIPTEN_KEEPALIVE int tw_panel_edit (int kind, int a, int b,
                                        const char *row,
                                        const char *valueText)
{
    panelWhy_.clear();

    if (row == NULL || valueText == NULL)
        return 0;

    switch (kind)
    {
        case thPanel::CHANARG:
        {
            /* A provider of its own: the panel this names may not be the
               one that is open, and applying an edit must not disturb what
               the page is looking at. */
            ArgPanel target;

            target.setChannel(a);
            target.setPrefix(b != 0 ? TH_EFFECT_PREFIX : "");

            thPanelEdit edit;

            const thPanelResult r = target.propose(row, valueText, edit);

            if (!r.ok)
            {
                panelWhy_ = r.why;

                return 0;
            }

            if (!r.changed)
                return 0;

            if (!target.deliver(edit))
                return 0;

            /* Moving a control is editing the patch, and the page has a
               Save to light up over it now that the slots are shared. The
               desktop says the same thing in the same words, from
               ArgPanelView. */
            patches_.markDirty(a);

            return 1;
        }
    }

    /* A knob is not set here, and the refusal says so rather than
     * pretending there is nothing to set.
     *
     * Its delivery is tw_knob, which carries a transport time: a knob is
     * heard, so a move has to land at the same time on every peer or the
     * peers stop composing the same piece. Applying one immediately here
     * would be a second door to the same write, opening at a different
     * moment -- which is exactly the divergence the stamp exists to stop.
     * The panel describes a knob; the command moves it. */
    if (kind == thPanel::KNOB)
    {
        panelWhy_ = "a knob is moved by a stamped command, not by an edit";

        return 0;
    }

    panelWhy_ = "no such panel";

    return 0;
}

/* Why the last edit was refused, or "" -- including when it was allowed. */
EMSCRIPTEN_KEEPALIVE const char *tw_panel_why (void)
{
    return panelWhy_.c_str();
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
   counts as late.
 *
 * The queue is emptied here and not when the frame comes round, because
 * between the two a peer whose transport is already running goes on
 * sending: a knob or a tempo stamped for the coming run, arriving while
 * this peer is still armed. Cleared at the arm, those survive to be
 * applied at the time they name; cleared at the begin, they were thrown
 * away with the old run's and counted as nothing. What is in the queue
 * now is the old run's, and the load that a start always comes with has
 * dropped it already. */
EMSCRIPTEN_KEEPALIVE void tw_begin (double originFrame)
{
    dropStamped();
    armed_ = true;
    armFrame_ = originFrame;
}

/* A stop or a tempo, at transport time `at', inside the step. TW_START and
   TW_REWIND are frame-stamped -- before a start there is no transport time
   to stamp with -- and are refused here, as is TW_KNOB, which has
   tw_knob. */
EMSCRIPTEN_KEEPALIVE void tw_at (double at, int op, double value)
{
    if (op != TW_STOP && op != TW_TEMPO)
        return;

    Scheduled c = {};

    c.at = at;
    c.op = op;
    c.value = value;

    schedule(c);
}

/* Knob `k' of the loaded piece to `value', at transport time `at'.
 *
 * `k' is the number a KNOB panel's row carries as its id, which is the
 * knob's place in the list this module built at the load; tw_knob_index
 * reads it the other way round, for a command arriving by name. An index
 * outside the list is ignored. */
EMSCRIPTEN_KEEPALIVE void tw_knob (double at, int k, double value)
{
    Scheduled c = {};

    c.at = at;
    c.op = TW_KNOB;
    c.knob = k;
    c.value = value;

    schedule(c);
}

/* A gesture on a stage's picture, at a transport time.
 *
 * One more stamped command, made and sent the way a knob is: applied at
 * `at' in the step on every peer, the sender included, so a Life board
 * that was clicked on one screen is the same board everywhere from that
 * moment. The clicker hears their own click a knob lead late, as they
 * hear their own knob.
 *
 * `at' below zero is "now", as for a knob on a stopped transport, which
 * is what a solo page sends.
 */
EMSCRIPTEN_KEEPALIVE void tw_input (double at, int chain, int stage,
                                    int kind, double x, double y, double w,
                                    double h, int button)
{
    Scheduled c = {};

    c.at = at;
    c.op = TW_INPUT;
    c.chain = chain;
    c.stage = stage;
    c.kind = kind;
    c.x = x;
    c.y = y;
    c.w = w;
    c.h = h;
    c.button = button;

    schedule(c);
}

/* ---- a stage's parameters ----
 *
 * What the params popover shows. The canvas asks for one and says where
 * to put it; what goes in it is a form, and a form is the platform's --
 * so the page builds it out of these.
 *
 * Read-only. The canvas reports rather than edits, and editing the piece
 * from it is the step after this one: on the desktop a param goes through
 * thcGenEdit into the file, and in a room the text in the editor is the
 * piece.
 */

static const thcPlugin::ParamInfo *paramAt (int chain, int stage, int p)
{
    const thcStage *s = stageAt(chain, stage);

    return s != NULL && s->plugin != NULL ? s->plugin->paramInfo(p) : NULL;
}

EMSCRIPTEN_KEEPALIVE int tw_stage_param_count (int chain, int stage)
{
    const thcStage *s = stageAt(chain, stage);

    return s != NULL && s->plugin != NULL ? s->plugin->paramCount() : 0;
}

EMSCRIPTEN_KEEPALIVE const char *tw_stage_param_name (int chain, int stage,
                                                      int p)
{
    const thcPlugin::ParamInfo *info = paramAt(chain, stage, p);

    return info != NULL ? info->name.c_str() : "";
}

EMSCRIPTEN_KEEPALIVE const char *tw_stage_param_desc (int chain, int stage,
                                                      int p)
{
    const thcPlugin::ParamInfo *info = paramAt(chain, stage, p);

    return info != NULL ? info->desc.c_str() : "";
}

/* The unit the plugin declares: "" for a plain number, "s" for a duration
   -- which the store keeps in seconds and a .gen file may have written in
   beats (thcParamStore::setBeats). */
EMSCRIPTEN_KEEPALIVE const char *tw_stage_param_units (int chain, int stage,
                                                       int p)
{
    const thcPlugin::ParamInfo *info = paramAt(chain, stage, p);

    return info != NULL ? info->units.c_str() : "";
}

/* thcParamType: a float, an int, a note, a note set, a string... which is
   what decides whether the popover shows a number or a word. */
EMSCRIPTEN_KEEPALIVE int tw_stage_param_type (int chain, int stage, int p)
{
    const thcPlugin::ParamInfo *info = paramAt(chain, stage, p);

    return info != NULL ? (int)info->type : -1;
}

EMSCRIPTEN_KEEPALIVE double tw_stage_param_min (int chain, int stage, int p)
{
    const thcPlugin::ParamInfo *info = paramAt(chain, stage, p);

    return info != NULL ? info->min : 0.0;
}

EMSCRIPTEN_KEEPALIVE double tw_stage_param_max (int chain, int stage, int p)
{
    const thcPlugin::ParamInfo *info = paramAt(chain, stage, p);

    return info != NULL ? info->max : 0.0;
}

/* The value as the stage is playing it now -- read through the knob when
   one is bound, which is what the plugin itself sees. */
EMSCRIPTEN_KEEPALIVE double tw_stage_param_value (int chain, int stage, int p)
{
    thcStage *s = stageAt(chain, stage);

    if (s == NULL || s->plugin == NULL || p < 0 ||
        p >= s->plugin->paramCount())
        return 0.0;

    return s->params.get(p);
}

/* And as text, for the types that are text: a note set is "C3 E3 G3" and
   an axiom is an axiom. Empty for the numeric ones. */
EMSCRIPTEN_KEEPALIVE const char *tw_stage_param_text (int chain, int stage,
                                                      int p)
{
    thcStage *s = stageAt(chain, stage);

    if (s == NULL || s->plugin == NULL || p < 0 ||
        p >= s->plugin->paramCount())
        return "";

    const char *text = s->params.getString(p);

    return text != NULL ? text : "";
}

/* The piece knob this param is read through, or "": `prob = @density' in
   the .gen file. Worth showing, because a number that moves on its own is
   otherwise a mystery. */
EMSCRIPTEN_KEEPALIVE const char *tw_stage_param_knob (int chain, int stage,
                                                      int p)
{
    thcStage *s = stageAt(chain, stage);

    if (s == NULL || s->plugin == NULL || p < 0 ||
        p >= s->plugin->paramCount())
        return "";

    const thArg *knob = s->params.knobBinding(p);

    return knob != NULL ? knob->name().c_str() : "";
}

/* Whether a stage's picture is a control -- its module exports
   composer_input. The canvas asks before it enlarges one. */
EMSCRIPTEN_KEEPALIVE int tw_stage_takes_input (int chain, int stage)
{
    const thcStage *s = stageAt(chain, stage);

    return s != NULL && s->plugin != NULL && s->plugin->hasInput() ? 1 : 0;
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
 * That order is genwav's, and it has to be: a command is meant to be in force
 * for the window it lands in, and what the step delivers is meant to sound in
 * the window it is delivered for. The step is to the transport time the
 * window's last frame falls on, counted from the origin, and by nothing
 * measured, so the piece is a function of the file and the seed and not of the
 * clock -- which is the property the step-size fix put in the scheduler and
 * this is the first host to lean on (docs/JAM.md). The scheduler's own commands
 * are applied inside the step, at the time each was stamped for; step() above
 * says why. */
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

/* ---- probes ------------------------------------------------------------
 *
 * A tap on one arg of one node of whatever is loaded on a channel: eight
 * slots in the synth, armed by name on the GUI thread, accumulated across
 * voices on the audio thread and published a window at a time into a ring
 * (thSynth.h). The desktop's editor drains that ring on a frame tick and
 * feeds a visual module; here the worklet drains it after every render and
 * posts the samples to the page with the tape batch, and the page's own
 * instance of the module holds the visuals.
 *
 * The worklet's thread is the only thread this module has, so armProbe --
 * a GUI-thread call -- is made on it, and the ring is drained on it too,
 * after process(). Which is the same discipline the desktop has, with the
 * two threads collapsed into one.
 */

/* Where a drained probe's samples land, for the page to read out of the
   heap. One window of a probe is the synth's window; a tape batch is
   sixteen quanta, so a couple of thousand samples is the most that can be
   waiting. */
#define TW_PROBE_DRAIN 4096

static float probeDrain_[TW_PROBE_DRAIN];
static std::string probeWhy_;

/* Arm a probe on a channel's node and arg. The slot, or -1 with the reason
   in tw_probe_why(). A slot is only good until the next load on that
   channel, which disarms every probe pointing at it. */
EMSCRIPTEN_KEEPALIVE int tw_probe_arm (int channel, const char *node,
                                       const char *arg)
{
    if (synth_ == NULL || node == NULL || arg == NULL)
        return -1;

    probeWhy_.clear();

    return synth_->armProbe(channel, node, arg, probeWhy_);
}

EMSCRIPTEN_KEEPALIVE const char *tw_probe_why (void)
{
    return probeWhy_.c_str();
}

EMSCRIPTEN_KEEPALIVE void tw_probe_disarm (int slot)
{
    if (synth_ != NULL)
        synth_->disarmProbe(slot);
}

/* Everything waiting on a slot, into the drain buffer: how many samples,
   or 0 for a slot that is not armed or has published nothing since the
   last call. Read after a render, as the desktop reads it on a frame
   tick. */
EMSCRIPTEN_KEEPALIVE int tw_probe_read (int slot)
{
    if (synth_ == NULL)
        return 0;

    thProbe *tap = synth_->probe(slot);

    if (tap == NULL)
        return 0;

    return (int)tap->read(probeDrain_, TW_PROBE_DRAIN);
}

EMSCRIPTEN_KEEPALIVE const float *tw_probe_samples (void)
{
    return probeDrain_;
}

/* ---- the mirror ----
 *
 * A second instance of this module, in a worker, fed the messages the
 * worklet is fed, holding real composer instances so that the composer
 * view has something to draw. It renders nothing: its synth is silent,
 * and instead of tw_render it is told how far the worklet's has got and
 * steps to there.
 *
 * Everything else about it is the same object doing the same thing, which
 * is what makes its tape the worklet's tape -- and what makes the two
 * tapes worth comparing, since a difference is a determinism bug in the
 * piece or in this module rather than in the mirror.
 */

/* Silent from here on: notes stop at the door, the queue is still applied,
   and process() skips the DSP (thSynth::setSilent). Called straight after
   tw_create and before any load -- it is a kind of synth, not a mode a
   running one flips. */
EMSCRIPTEN_KEEPALIVE void tw_silent (void)
{
    synth_->setSilent(true);
}

/* Step to `toFrame': tw_render without the render.
 *
 * The same three calls per window, at the same window boundaries -- both
 * instances count windows from frame zero and tw_align puts them on one
 * numbering -- so a command lands in the window it lands in over there.
 * The window containing `toFrame' is stepped too, because the worklet
 * that reported it had already applied that window whole before handing
 * out the frames inside it.
 *
 * process() is called here rather than by a gthSynthSource, which is the
 * thing this instance does not have: it is what drains the command ring,
 * and a ring nobody drains was measured filling up.
 *
 * Returns the frame it reached.
 */
EMSCRIPTEN_KEEPALIVE double tw_step (double toFrame)
{
    const int len = synth_->getWindowlen();

    while (rendered_ < toFrame)
    {
        applyDue(rendered_, len);
        beginDue(rendered_, len);
        step(rendered_, len);
        synth_->process();

        rendered_ += len;
    }

    return rendered_;
}

/* Commands this instance could not queue, which on a mirror is the number
   worth watching: a silent synth drains its ring on every step it is
   given, so a moving number is a mirror nobody is stepping. */
EMSCRIPTEN_KEEPALIVE double tw_dropped (void)
{
    return (double)synth_->droppedCommands();
}

/* The frame the next tw_render starts at. */
EMSCRIPTEN_KEEPALIVE double tw_frame (void)
{
    return rendered_;
}

} /* extern "C" */
