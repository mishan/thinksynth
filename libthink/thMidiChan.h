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

#ifndef TH_MIDICHAN_H
#define TH_MIDICHAN_H 1

#include <atomic>

#include "thExport.h"

class THINK_API thMidiChan {
public:
    /* Takes ownership of `mod' and destroys it. Each channel needs its own
       tree: assignChanArgPointers() caches raw thArg pointers into the tree's
       nodes, so a tree shared between two channels has its pointers overwritten
       by whichever was constructed last, and destroying either channel leaves
       the other dereferencing freed args. */
    thMidiChan (thSynthTree *mod, float amp, int windowlen);
    ~thMidiChan (void);

    typedef map<int, thMidiNote*> NoteMap;
    typedef list<thMidiNote*> NoteList;

    typedef thRing<thRetired, TH_RETIRE_QUEUE_SIZE> RetireQueue;

    /* ---- GUI thread ---- */

    /* Slot number, graph name and tally: what the non-finite guard in
     * mixNote() needs to name the graph it dropped a voice from. A channel
     * knows neither of the first two otherwise.
     *
     * The counter is a bare pointer into thSynth rather than a back-reference
     * to the synth: the guard only writes to it, and thSynth's destructor
     * frees its channels, so it outlives them.
     *
     * Called by thSynth::loadTree after construction, on the GUI thread and
     * before the channel is queued; never touched again. That is also where
     * the diagnostic line is formatted, so that the audio thread's whole part
     * in printing it is one write(2) of bytes that already exist. An
     * undescribed channel still drops the voice -- it just says and counts
     * nothing. */
    void describe (int channum, const string &graph,
                   std::atomic<unsigned long> *nonFinite);

    /* Allocates the note, which means copy-constructing the whole synth tree.
       Deliberately separate from installing it: this is far too expensive to
       do in an audio callback, so the GUI thread builds and thSynth hands the
       finished object over through the command queue. */
    thMidiNote *buildNote (float note, float velocity);

    /* ---- audio thread ---- */

    /* Installs a note built by buildNote(), applying the polyphony limit.
       Anything displaced goes on `retire' for the GUI thread to free. */
    void insertNote (thMidiNote *note, RetireQueue *retire);

    /* Releases a sounding note (sustain pedal permitting). */
    void releaseNote (int note);

    void clearAll (RetireQueue *retire);

    /* `probes' are the armed probes pointing at this channel, already filtered
       by thSynth, and already zeroed for this window. They are accumulated
       inside the note loops rather than after process() returns, because a
       note whose envelope ended this window is retired before this call is
       over -- tapping afterwards would clip the last window off every release,
       which is exactly the part of a sound anyone is looking at a scope to
       see. */
    void process (RetireQueue *retire, thProbe *const *probes = NULL,
                  int nprobes = 0);

    /* ---- either, with care ---- */

    thMidiNote *getNote (int note);
    int setNoteArg (int note, const string &name, float value);
    int setNoteArg (int note, const string &name, const float *value, int len);

    /* NB: deliberately not args_[argName] -- map::operator[] inserts a NULL on
       every miss, which allocates on the audio thread and leaves NULLs behind
       for every iteration site to trip over. */
    thArg *getArg (const string &argName) const {
        const thArgMap::const_iterator i = args_.find(argName);
        if (i != args_.end()) return i->second;
        return NULL;
    }
    /* Audio thread: replaces the arg of the same name and retires the old one
       rather than deleting it under the GUI thread's feet. */
    void setArg (thArg *arg, RetireQueue *retire);

    /* Audio thread: installs the graph that runs on this channel's summed
     * voices, or NULL to take one off, and retires whatever was there.
     *
     * Built on the GUI thread like a channel and a note, and for the same
     * reason: it parses a file and allocates a tree. */
    void setEffect (thChanEffect *effect, RetireQueue *retire);

    /* Either thread, with the care every shared pointer here needs. The GUI
       reads it to reach the effect's chanargs; the audio thread runs it. */
    thChanEffect *effect (void) const { return effect_; }

    const thArgMap &args (void) const { return args_; }

    float *output (void) const { return output_; }
    int numChannels (void) const { return channels_; }

    /* How this channel allocates voices: `poly' and `mono' off the io node,
     * read once at construction and never written again, so either thread may
     * ask.
     *
     * For anything that plays notes and then expects to find them -- a
     * harness, a panel, a voice display. A mono channel answers one note with
     * one voice however many are played, and a poly-limited one retires down
     * to its limit, so "I played three, where are they" is a question with a
     * different answer per graph. polyMax() of 0 is no limit. */
    int polyMax (void) const { return polymax_; }
    bool mono (void) const { return mono_; }

    thSynthTree *modnode (void) { return modnode_; }

    /* A number no other channel object has ever had.
     *
     * Probes resolve a node to an id against the tree a particular channel is
     * playing, and ids are assigned in parse order -- so the same id in the
     * next patch loaded onto that slot names a different node. The audio
     * thread checks this before accumulating, and a probe whose serial no
     * longer matches contributes nothing.
     *
     * Monotonic rather than a pointer comparison because the replacement
     * channel is routinely allocated at the address the old one was freed
     * from, and rather than a per-slot generation because the GUI resolves
     * against a channel object it holds, not against a counter the audio
     * thread has or has not caught up with yet. */
    unsigned long serial (void) const { return serial_; }

    thArg *sustainPedal (void) { return argSustain_; }

    void copyChanArgs (thSynthTree *mod);
    
private:
    void assignChanArgPointers(thSynthTree *mod);

    /* Resolves the io-node args process() reads, and creates any this .dsp did
       not write. Constructor only: it allocates, and it is what stops the
       audio thread having to. */
    void indexIOArgs (void);

    /* thSynthTree::getArg(node, int), plus the ARG_CHANNEL dereference the
       by-name overload does at the end of its pointer chase and the indexed one
       does only at the start. Same answer as the name lookup this replaced --
       without the lookup, and without the insert it performs on a miss.

       Not folded into thSynthTree::getArg(node, int) itself: every plugin's
       `mod->getArg(node, args[OUT_ARG])' goes through that overload and then
       *writes* to what comes back, so teaching it to follow a chanarg pointer
       would let a plugin write into a chanarg. Here the result is only read. */
    static thArg *resolveIOArg (thSynthTree *tree, int index);

    /* Audio thread. The part of process() the held and the decaying loop have
       in common: run the note, tap it, apply the pedal, mix it. Returns the
       note's `play' arg, since deciding what to do about it is the only thing
       the two loops do differently. */
    thArg *mixNote (thMidiNote *note, int sustain, thProbe *const *probes,
                    int nprobes);

    /* Audio thread. True if every sample this voice put on the io node's
       out0..outN-1 is finite -- asked before any of it is mixed, because the
       sum is where a NaN becomes everybody's problem. */
    bool voiceIsFinite (thSynthTree *tree);

    /* Audio thread. Counts what the guard just dropped, and says so once per
       channel per load -- once for a voice and once for the effect, which are
       two different failures and two different lines.
     *
       `which' picks the message describe() formatted; both increment the same
       counter, because what the counter is for is "this render had a graph
       that went non-finite in it" and that is true either way. */
    enum Guard { GUARD_VOICE, GUARD_EFFECT };

    void reportNonFinite (Guard which);

    /* Hands `note' to the GUI thread to destroy. Falls back to deleting it
       here if the retire queue is full -- that costs RT-safety in a case that
       should not arise, but never correctness. */
    void retireNote (thMidiNote *note, RetireQueue *retire);

    /* Audio thread. Moves a voice out of notes_ and into decaying_, which is
       what has to happen to any voice that is no longer the one a new note
       will be keyed as. Was the body of insertNote's same-pitch collision
       case; mono needs it for a collision on any pitch. */
    void decayNote (NoteMap::iterator i);

    /* Audio thread. The voice a mono channel is playing, or NULL.
     *
     * Not simply notes_.begin(): a voice whose key has come up is still in
     * notes_ until its release finishes, and a new note then is a new voice
     * rather than a slide -- which is the rule that makes `hold' longer than
     * `step' a slide and shorter a retrigger. So this is the voice that is
     * still being *held*, by a key or by the pedal, which is a non-zero
     * trigger. */
    thMidiNote *monoVoice (void);

    /* Audio thread. The stack of pitches whose keys are down, last-note
       priority. A pitch already on it is moved to the top rather than
       repeated, so the stack cannot exceed one entry per distinct pitch. */
    void monoPush (float note);
    bool monoPop (float note);

    bool dirty_;
    thSynthTree *modnode_;

    /* The channel's effect, or NULL. Owned here, installed by setEffect. */
    thChanEffect *effect_;
    thArgMap args_;
    NoteMap notes_;
    NoteList decaying_;  /* linked list for decaying notes */
    NoteList noteorder_; /* order of the notes for polyphony limits */
    int channels_, windowlength_;
    float *output_;

    /* Scratch for the mix loop, sized once. These were VLAs declared inside
       process(), so every call moved the stack pointer by twice the window
       length -- 8k at the default window, unbounded in principle, and on the
       one thread that cannot afford to find out. */
    float *bufmix_, *bufamp_;

    /* Where the io node keeps out0..outN-1 and play, as arg indices.
     *
     * process() used to spell these out per channel per note per window:
     * `argname = OUTPUTPREFIX; argname += (char)(i + '0')', then a lookup by
     * that string. Three separate things wrong with it on an audio thread --
     * it builds a std::string, it searches a std::map, and
     * thSynthTree::getArg() *creates* the arg when it does not find one, which
     * allocates a thArg and inserts it. That last one fired on the first
     * window of every note of any .dsp whose io node declares more channels
     * than it wires up.
     *
     * An index instead, because both halves of an arg's address survive the
     * per-note tree copy -- the property thProbe documents and rides on, and
     * the one every plugin's `mod->getArg(node, args[IN_FREQ])' already uses.
     * -1 where the arg is genuinely absent. */
    int outindex_[TH_MAX_CHANNELS];
    int playindex_;
    int triggerindex_;

    int polymax_;  /* maximum polyphony; see TH_DEFAULT_POLY */
    int notecount_, notecount_decay_;  /* keeping track of polyphony this way
                                        for now */

    /* `mono = 1' on the io node: a new note while one is held retunes the
       voice that is sounding instead of starting another. See insertNote. */
    bool mono_;

    /* The pitches whose keys are down, oldest first, so the top of the stack
       is the one sounding. A fixed array rather than a vector because this is
       pushed and popped on the audio thread: the 128 is MIDI's pitch count,
       and a stack that somehow fills drops its oldest entry rather than
       growing. */
    float monoStack_[TH_MONO_STACK];
    int monoCount_;
    thArg *argSustain_; /* for the sustain pedal */

    unsigned long serial_;

    /* See describe(). -1 and empty for a channel nobody named.
     *
     * `message_' is the whole diagnostic line, formatted at load time so that
     * the audio thread has nothing to do but hand the bytes to write(2). */
    int channum_;
    string graph_;
    string message_;
    string effectMessage_;
    std::atomic<unsigned long> *nonFinite_;

    /* A diverging graph goes non-finite on every window of every note, so
       the message is printed once and suppressed after. Never reset: loading
       a patch builds a new channel. One flag per message, so an effect that
       misbehaves is not silenced by a voice that did first. */
    bool saidNonFinite_;
    bool saidNonFiniteEffect_;

    static std::atomic<unsigned long> nextSerial_;
};

#endif /* TH_MIDICHAN_H */
