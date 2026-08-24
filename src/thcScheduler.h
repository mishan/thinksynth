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

#ifndef THCSCHEDULER_H
#define THCSCHEDULER_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <glibmm.h>
#include <sigc++/sigc++.h>

#include "libthink/thcomposer.h"

class thSynth;
class thArg;
class thcPlugin;

/* The host side of the composer framework: one object, living in src/
 * next to the MIDI plumbing, GUI thread only. It owns the transport, the
 * chains, and three small priority queues:
 *
 *   wakeups_   when each generator next wants its tick() called
 *   pending_   fully-transformed events waiting for their delivery time
 *   noteOffs_  offs derived from delivered notes' durations
 *
 * The one structural decision worth stating out loud: transformer
 * propagation is SYNCHRONOUS, delivery is SCHEDULED. When a stage emits,
 * the event runs through the remaining stages of its chain immediately,
 * in stage order, as plain function calls -- however far in the future
 * its `at' is. Only what falls out the end of the chain goes into
 * pending_, keyed by `at'. So an echo emitting copies at +2s and +4s is
 * just three emits; humanize is a one-line adjustment of `at'; and the
 * whole pipeline stays deterministic because nothing about transformer
 * order depends on wall-clock interleaving.
 *
 * Threading: everything here runs on the GUI thread off one Glib timeout,
 * exactly like MIDI input runs off its Dispatcher. The synth is only ever
 * touched through the same calls the on-screen keyboard already uses, so
 * the two-thread invariant and the command queue stay the whole story.
 */

/* Backing store for one instance's params; implements the thcParams
 * accessors handed to composer_create. set() is what the panel and the
 * .gen loader call; it forwards to composer_param_changed if exported,
 * and re-arms a THC_NEVER sleeper through the scheduler. */
class thcParamStore
{
public:
    thcParamStore (thcPlugin *plugin, unsigned seed);

    double      get (int index) const;
    const char *getString (int index) const;

    void set (int index, double v);
    void setString (int index, const std::string &v);

    /* By name, for callers holding a .gen line rather than an index.
       Returns false when the plugin has no such param. */
    bool set (const std::string &name, double v);
    bool setString (const std::string &name, const std::string &v);

    /* `period = 4 beats' -- the stored number is beats, and get()
       converts through the transport's tempo at read time, so the value
       keeps meaning something across tempo automation. The unit lives in
       the value, not the plugin: the same param is free-running seconds
       in one .gen file and clocked beats in another. */
    void setBeats (int index, bool beats);

    /* True if any value in this store is beat-valued, and therefore
       scaled by the transport's tempo. Asked by the window, which has a
       tempo control to offer or withhold. */
    bool anyBeats (void) const;

    /* `prob = @density' -- the composer-world ARG_CHAN. While bound,
       get() reads the knob and the stored value is shadowed; the plugin
       just calls get() as always. NULL unbinds. The scheduler is what
       connects the knob's changed signal to param_changed/rearm --
       see thcScheduler::bindKnob. */
    void bindKnob (int index, thArg *knob);
    thArg *knobBinding (int index) const;

    /* What the .gen loader calls after composer_create to push a fresh
       value at a module that caches (a NOTESET reparse), without
       changing anything -- and what a knob's changed signal funnels
       through. */
    void notifyChanged (int index);

    /* Handed to composer_create; its address must stay stable for the
       instance's lifetime -- the module is entitled to keep the pointer,
       and eno_line does. That is why thcStage below is heap-allocated
       rather than sitting by value in a reallocating vector. */
    const thcParams *params (void) const { return &params_; }

    thcPlugin *plugin (void) const { return plugin_; }

private:
    friend class thcScheduler;

    static double      cbGet (void *ctx, int index);
    static const char *cbGetString (void *ctx, int index);

    thcPlugin                *plugin_;
    thcParams                 params_;     /* ctx points back at this    */
    std::vector<double>       values_;
    std::vector<std::string>  strings_;
    std::vector<char>         beats_;      /* value is beats, not seconds */
    std::vector<thArg *>      knobs_;      /* live binding, NULL = value  */

    /* Set by the scheduler once composer_create has run: where to send
       param_changed forwards, how to re-arm a sleeping generator, and
       where tempo comes from for the beats conversion. */
    void                    *instance_;
    std::function<void()>    rearm_;
    std::function<double()>  tempo_;
};

/* One value an instrument sets on top of its graph.
 *
 * `units' is the suffix the author wrote -- "ms" or "%", or empty for the
 * raw terms the engine works in. A .patch has neither, because it stores
 * every chanarg already folded; that is why a patch file is full of
 * numbers like 39690 and cannot be read by a person. A piece file is
 * meant to be read, so the fold happens on the way in, through
 * thFoldUnit and at the rate the synth was actually built with -- the
 * same arithmetic, at the same rate, as the .dsp the value lands on.
 */
struct thcInstrumentArg
{
    std::string name;
    double      value;
    std::string units;
};

/* An instrument the piece carries: a DSP graph named by file, the
 * chanarg values that make it this instrument rather than that graph's
 * defaults, and the channel it was given.
 *
 * The channel is an *allocation*, not a declaration. A .gen used to name
 * MIDI channels in its sinks and leave what was loaded on them to
 * whoever opened the file -- which is why every shipped piece carries a
 * paragraph at the top telling you what to go and load first. An
 * instrument answers that question inside the file, and the number
 * underneath becomes the loader's business rather than the author's;
 * thcGenLoader says how one is chosen. `channel = N' survives in the
 * language for the other case, an externally loaded patch this piece
 * does not own.
 */
struct thcInstrument
{
    std::string name;
    std::string dsp;
    std::vector<thcInstrumentArg> args;

    int channel;                /* 0-15, engine numbering; -1 unallocated */

    thcInstrument (void) : channel(-1) {}
};

/* One placement of a plugin in a chain. */
struct thcStage
{
    thcPlugin     *plugin;
    void          *state;       /* from composer_create                  */
    thcParamStore  params;
    bool           sleeping;    /* tick returned THC_NEVER               */

    /* Whether THIS placement is clocked. A plugin that exports both
       entry points is a generator as a gen:: stage and only a
       transformer as an xform:: one -- the file's declared role, not
       the module's capability, decides what gets scheduled. */
    bool           ticks;

    thcStage (thcPlugin *p, unsigned seed, bool wantTick)
        : plugin(p), state(NULL), params(p, seed), sleeping(false),
          ticks(wantTick) {}
};

/* Where a chain's events go when they fall off the end. A plain sink
 * takes notes; a chanarg sink takes THC_EV_CHANARG events and names the
 * patch knob they land on. Type filtering happens here, not in a stage,
 * so one generator can drive a melody and a filter sweep at once; two
 * sinks is fan-out. */
struct thcSink
{
    int         channel;
    std::string chanarg;     /* empty: a note sink; "*": see below       */

    bool isChanarg (void) const { return !chanarg.empty(); }

    /* A chanarg sink written `chanarg = "*";' -- one that lets each
     * event name its own target instead of overwriting it.
     *
     * The ordinary chanarg sink names one knob, which is right for a
     * walk or an envelope: the plugin produces a number and has no
     * business knowing which knob it lands on. A morph produces a whole
     * *vector* -- several knobs at once, each with its own name -- and
     * one sink per knob cannot express that, because every sink would
     * deliver the same value.
     *
     * `*' cannot collide with a real name: a chanarg is a .dsp
     * identifier, and identifiers do not contain it. */
    bool namesItsOwn (void) const { return chanarg == "*"; }
};

/* A linear pipeline: stage 0 is usually a generator, the rest
 * transformers. Live MIDI can also be routed in at stage 0. */
struct thcChain
{
    std::string  name;
    bool         muted;
    bool         inputMidi;  /* fed by live MIDI on the sink channel     */

    /* unique_ptr for the address stability thcParamStore::params()
       documents, not for shared ownership. */
    std::vector<std::unique_ptr<thcStage> > stages;

    /* Empty means "deliver events exactly as emitted" -- the
       programmatic-chain case harnesses use. A .gen chain always has at
       least one (the loader enforces it). */
    std::vector<thcSink> sinks;
};

class thcScheduler
{
public:
    explicit thcScheduler (thSynth *synth);
    ~thcScheduler (void);

    /* ---- building chains ----
     *
     * Programmatic for now: the .gen loader (milestone 4 in the handoff's
     * build order) will sit on top of exactly these calls. addStage
     * creates the instance immediately, with a seed derived from the
     * master seed and the stage's position, so the same seed and the
     * same construction order replay the same piece. */
    size_t    addChain (const std::string &name);

    /* `asGenerator' is the stage's declared role -- gen:: or xform:: in
       a .gen file. The two-argument form takes the plugin's word for
       it. NULL when the module refuses to create an instance; nothing
       is left half-added. */
    thcStage *addStage (size_t chain, thcPlugin *plugin);
    thcStage *addStage (size_t chain, thcPlugin *plugin, bool asGenerator);

    void      addSink (size_t chain, int channel,
                       const std::string &chanarg = "");
    void      setChainInput (size_t chain, bool midi);
    void      clearChains (void);

    /* ---- piece knobs ----
     *
     * `@density' in a .gen file: a thArg, so the same widget/min/max/
     * label metadata the .dsp parser stores drives the same kind of
     * panel. The scheduler owns them; binding one to a stage param
     * (bindKnob) is the live ARG_CHAN-style link, including waking a
     * THC_NEVER sleeper when the knob moves. */
    thArg *addKnob (const std::string &name, float value);
    thArg *knob (const std::string &name);
    const std::map<std::string, thArg *> &knobs (void) const
    {
        return knobs_;
    }

    void bindKnob (thcStage *stage, int paramIndex, thArg *knob);

    /* ---- instruments ----
     *
     * The instruments the piece declares, in the order it declares them,
     * each with the channel the loader allocated for it. Owned here for
     * the reason the knobs are: they belong to the piece, and the piece
     * is what clearChains takes away. */
    size_t addInstrument (const thcInstrument &inst);

    const std::vector<thcInstrument> &instruments (void) const
    {
        return instruments_;
    }

    const thcInstrument *instrument (const std::string &name) const;
    thcInstrument       *instrument (size_t index);

    /* Who turns a named .dsp into a sounding channel.
     *
     * The scheduler knows what the piece declared; how *this program*
     * loads a patch is not its business and must not become its
     * business. In the application an instrument has to appear on a
     * patch tab, with a filename, a dirty flag and an arg panel behind
     * it, and every bit of that lives in gthPatchManager where the
     * composer host cannot see it. So the app installs a hook.
     *
     * The default below -- parse the graph, put it on the channel -- is
     * not a fallback nobody runs. It is precisely what a headless
     * harness wants, which is what makes the instrument path gateable
     * at all rather than only reachable through the GUI. */
    typedef std::function<bool (const thcInstrument &inst,
                                std::string &why)> InstrumentLoader;

    /* And the way back. A .gen that fails to load is documented to load
       nothing, and until this existed that was true of the chains and
       false of the channels: an instrument that came up before a later
       one failed stayed up, on a tab, for a piece nobody was going to
       hear. The host needs to hear about it for the same reason it
       handles the load -- what has to be undone is a patch tab, not
       just a graph. */
    typedef std::function<bool (const thcInstrument &inst)>
        InstrumentUnloader;

    void setInstrumentLoader (const InstrumentLoader &fn) { loadDsp_ = fn; }
    void setInstrumentUnloader (const InstrumentUnloader &fn)
    {
        unloadDsp_ = fn;
    }

    /* Is this channel somebody else's?
     *
     * The loader allocates the lowest channel a `channel = N' sink has
     * not claimed, which is the whole story in a harness and only half
     * of it in the application: there, channel 1 may already hold a
     * patch the person loaded by hand, or one their thinkrc loaded at
     * startup, and taking it would replace their instrument with the
     * piece's without asking. The host is the only thing that knows.
     *
     * "Somebody else's" and not merely "loaded", because the channels
     * this piece is already on have to stay reusable -- an instrument
     * that moved one to the right on every reload would be worse than
     * the problem. The default is that nothing is anybody's, which is
     * true of a harness and of a synth with an empty rack. */
    typedef std::function<bool (int channel)> ChannelTaken;

    void setChannelTaken (const ChannelTaken &fn) { taken_ = fn; }
    bool channelTaken (int channel) const
    {
        return taken_ ? taken_(channel) : false;
    }

    /* Loads instrument `index' onto its channel and sets its values.
       False with `why' saying what went wrong, which the .gen loader
       turns into a load error against the instrument's line: a piece
       whose instrument is missing will not play, and should say so
       rather than open silent and let the person hunt for it.

       All or nothing. A value can only be checked once its graph is on
       the channel, so a refusal usually happens with the .dsp already
       loaded -- and this takes it back rather than leaving the caller
       to know which failures did and did not install something. The one
       case it cannot keep that promise in is a command ring too full to
       carry the removal, and then it says so in `why' rather than
       claiming the channel is clear. */
    bool applyInstrument (size_t index, std::string &why);

    /* Takes instrument `index' back off its channel. Idempotent, and
       harmless on one that never got there.

       False when it could not be taken back -- the audio thread has to
       be told to drop a channel and the command ring can be full. The
       channel is then still loaded and still sounding, and the host
       still owns it. */
    bool unapplyInstrument (size_t index);

    size_t chainCount (void) const { return chains_.size(); }
    thcChain *chain (size_t i)
    {
        return i < chains_.size() ? &chains_[i] : NULL;
    }

    /* Mute drops events at end-of-chain, not at the source: the
       algorithm keeps evolving silently, so un-muting mid-piece rejoins
       a living process rather than restarting a cold one. */
    void setMuted (size_t chain, bool muted);

    /* Only effective before any stage exists: a seed that changed under
       running instances would be a lie about what they were created
       with. .gen files with a pinned seed call this first. */
    void setMasterSeed (unsigned seed);
    unsigned masterSeed (void) const { return masterSeed_; }

    /* ---- transport ----
     *
     * now() freezes across pause; tempo changes take effect from the
     * moment of the call (beat position is integrated, not derived, so
     * clocked composers survive tempo automation). */
    void   start (void);
    void   stop (void);          /* pause; sounding notes get their offs */
    void   reset (void);         /* rewind to 0 and reseed -- a replay   */
    void   setTempo (double bpm);
    double tempo (void) const { return tempo_; }

    /* Does the tempo mean anything to this piece?
     *
     * It scales beat-valued durations and nothing else, so a piece whose
     * every duration is written in seconds is one the tempo cannot
     * touch -- which is most of them, and which the window had no way to
     * know. Offering a control that does nothing is worse than offering
     * none, and nudging one that then writes a `tempo' line into a file
     * that never had one is worse again. */
    bool usesBeats (void) const;
    double now (void) const { return transportNow_; }
    bool   running (void) const { return running_; }

    /* Advance the musical clock by `dt' seconds without the Glib timer:
       the virtual-clock spelling of one timerCallback, for harnesses.
       Deterministic because everything below is keyed in transport time:
       two renders with the same seed and the same step size deliver the
       same stream, which is what makes a replay gate writable at all.
       In the app the timer owns time and nothing calls this. */
    void stepTransport (double dt);

    /* Route a live MIDI note into a chain's receive() path (Markov
     * training, arpeggiators). Called from the m_sigNoteOn/Off hop --
     * same thread, so it is a plain call into propagate(). On a stopped
     * transport, whatever falls out of the chains is delivered
     * immediately: keys pressed while paused should sound. */
    void injectMidi (size_t chainIndex, const thcEvent &ev);

    /* The `input midi;' route: hand the event to every chain that
     * declared the input and whose sink channel matches the event's.
     * What dispatchmidi (or anything else) calls when it does not know
     * chain indices -- which is always. */
    void injectMidiEvent (const thcEvent &ev);

    /* ---- for the tier-one piano roll ----
     *
     * Every delivered event, plus a copy of pending_ on demand. Same
     * thread as the widget, no snapshotting. peekPending's vector is
     * rebuilt per call and unordered (it mirrors a heap); the roll just
     * wants to draw every entry, so order is not its business. */
    /* One emission per delivered event, on the GUI thread, synchronous.
     * BORROW, DO NOT KEEP: a chanarg event's name points into storage
     * the scheduler releases when the emission returns, so a handler
     * that wants the name beyond its own stack frame copies the string.
     * The same borrow applies to peekPending()'s vector, which is
     * rebuilt on every call. */
    sigc::signal<void (const thcEvent &)> sigDelivered;
    const std::vector<thcEvent> &peekPending (void) const;

    /* Does the patch on `channel' declare this knob? What the .gen
       loader asks about a sink bound to one of the piece's own
       instruments, where the graph is known and a typo is therefore
       catchable instead of silent at delivery time. */
    bool chanArgExists (int channel, const std::string &name) const;

    /* The declared range of a patch chanarg, for anyone drawing its
       values honestly -- the roll's strip normalizes by this instead of
       assuming 0-1. False when the channel has no such arg or its range
       is degenerate; the caller falls back to assuming. */
    bool chanArgRange (int channel, const char *name,
                       float &lo, float &hi) const;

    /* Emitted by reset(): the transport has rewound to zero and every
     * instance has been recreated. Anything keeping history keyed to
     * transport time -- the piano roll's delivered notes -- must drop
     * it, because time zero is about to mean a different piece (or the
     * same piece from the top, which for a history is the same thing:
     * notes stamped with times the transport is about to live through
     * again would draw as a future that already happened). */
    sigc::signal<void ()> sigReset;

private:
    /* The values half of applyInstrument, on a channel whose graph is
       already up. Split out so every refusal has one caller, and that
       caller can take the graph back down. */
    bool applyValues (const thcInstrument &inst, std::string &why);

    bool timerCallback (void);                   /* the ~20ms Glib tick  */
    void queuePending (const thcEvent &ev, const std::string *nameOverride);
    void releaseHeld (int channel, int note);
    void flushHeld (void);
    void runDueTicks (double now);
    void deliverDue (double now);
    void sendDueNoteOffs (double now);
    void propagate (thcChain &c, size_t fromStage, const thcEvent &ev);
    void deliver (const thcEvent &ev);           /* -> synth addNote /
                                                    chanarg, derive off  */
    void flushNoteOffs (void);
    void rearmStage (size_t chain, size_t stage);
    unsigned stageSeed (size_t chain, size_t stage) const;

    thSynth               *synth_;
    std::vector<thcChain>  chains_;
    sigc::connection       timer_;

    /* Piece knobs, owned here; and the signal connections that carry a
       knob's movement to the params bound to it (param_changed forward
       plus the THC_NEVER rearm). Dropped in clearChains. */
    std::map<std::string, thArg *>  knobs_;
    std::vector<sigc::connection>   knobConns_;

    /* The piece's instruments, and the host's way of loading one. A
       vector rather than a map: declaration order is what the loader
       allocates channels in, and what the editor draws. */
    std::vector<thcInstrument> instruments_;
    InstrumentLoader           loadDsp_;
    InstrumentUnloader         unloadDsp_;
    ChannelTaken               taken_;

    /* transport */
    bool     running_;
    double   transportNow_;    /* integrated musical seconds             */
    double   beat_;            /* integrated beats                       */
    double   tempo_;
    gint64   lastMono_;        /* g_get_monotonic_time at last tick      */
    unsigned masterSeed_;      /* stage seeds derive from this           */

    struct Wakeup  { double at; size_t chain, stage; };
    struct NoteOff { double at; int channel, note; };

    /* A queued event. The chanarg name a composer emitted is a pointer
       into memory it owns and may rewrite on its next tick, so the copy
       happens here, at the sink -- exactly what thcEventSink's contract
       promises. shared_ptr for stable c_str storage across the heap's
       copies, not for sharing. */
    struct Pending
    {
        double   at;
        thcEvent ev;
        std::shared_ptr<std::string> chanargName;
    };

    /* min-heaps on .at, kept as vectors with std::push_heap/pop_heap --
       priority_queue hides its container, and pending_ has to be
       iterable for peekPending. */
    struct Later
    {
        template <typename T>
        bool operator() (const T &a, const T &b) const { return a.at > b.at; }
    };

    std::vector<Wakeup>  wakeups_;
    std::vector<Pending> pending_;
    std::vector<NoteOff> noteOffs_;

    /* Notes delivered with duration <= 0: held until a THC_EV_NOTEOFF
       releases them, or until stop()/clearChains flushes them -- a
       pause must not hang a key any more than it hangs a note. */
    std::vector<NoteOff> held_;      /* .at unused                       */

    /* True while an injectMidi* call is propagating on a stopped
       transport; what falls out of the chains is delivered immediately
       rather than parked in pending_ behind a frozen clock. Decided
       (the handoff had it flagged): an arpeggio on a stopped transport
       should still sound. */
    bool injectingLive_;

    mutable std::vector<thcEvent> peekCache_;
};

#endif /* THCSCHEDULER_H */
