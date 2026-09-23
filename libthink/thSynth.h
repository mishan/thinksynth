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

#ifndef TH_SYNTH_H
#define TH_SYNTH_H

#include <atomic>
#include <mutex>

#include "thExport.h"

class thMidiNote;
class thMidiChan;

class THINK_API thSynth {
public:
    thSynth (int windowlen=TH_DEFAULT_WINDOW_LENGTH,
             int samples=TH_DEFAULT_SAMPLES);
    thSynth (const string &plugin_path, int windowlen, int samples);
    ~thSynth (void);

    static thSynth *instance (void) {
        return instance_;
    }

    thSynthTree *loadTree(const string &filename);
    thSynthTree *loadTree(const string &filename, int channum, float amp);
    thSynthTree *loadTree(FILE *input);

    /* Parses a .dsp and hands back a tree that thSynth neither owns nor
       tracks: the caller deletes it.

       loadTree() registers into treelist_ keyed by the tree's *name*, and a
       name collision deletes whatever was registered before -- possibly the
       tree a channel is playing right now. That risk is acceptable when
       loading a patch and absurd when merely looking at one, so the node
       editor uses this instead. */
    thSynthTree *parseTree(const string &filename);

    void listTrees (void);
    thPluginManager *getPluginManager (void) { return pluginmanager_; };

    /* ---- GUI thread ----
     *
     * These no longer touch the live graph. They allocate whatever is needed
     * and queue the change; the audio thread applies it on its next window,
     * within one window's latency. See thSynthCommand.h.
     *
     * addNote returned a thMidiNote* that no caller used, and handing back a
     * pointer to an object the audio thread now owns would be a trap, so it
     * returns success instead. */
    bool addNote(int channum, float note, float velocity, float level = 1,
                 const float *aux = NULL);
    int delNote (int channum, float note);
    void clearAll (void);

    /* Everything down to nothing in TH_STOP_FADE_MS: every voice on every
     * channel, releases included, and every effect's tail, the master
     * effect's too. What a transport's Stop means, where clearAll is a cut
     * and a note-off lets everything ring for as long as it was going to.
     * See thMidiChan::silence and thChanEffect::fadeOut. */
    void silence (void);

    /* ---- audio thread ---- */
    void process(void);

    /* ---- live input ----
     *
     * What the machine is hearing, for the window process() is about to
     * render. A graph reaches it through live<N> on a channel effect's io
     * node (LIVEPREFIX in think.h); nothing else in the engine reads it.
     *
     * MONO, and the sum is the host's to do. A vocoder is mono by
     * construction and says so at length (dsp/fx/vocoder.dsp); osc::sample
     * is mono for the same reason; and a graph that wants two signals
     * instantiates two nodes, which is the rule everywhere else in the tree.
     * One buffer here rather than a stride is also what makes live<N> and
     * live<N+1> the same signal without a special case.
     *
     * AUDIO THREAD, BOTH SIDES, AND NO RING. feedCapture is called by the
     * host from the same callback that will drive process(), which is the
     * arrangement gthSynthSource's header argues for on the output side:
     * the producer and the consumer are one thread, so there is nothing to
     * synchronise and a lock-free ring would be a ring with one thread on
     * both ends of it.
     *
     * SILENCE IS THE DEFAULT AND HAS TO BE. The buffer is allocated zeroed
     * and a host that feeds nothing leaves it that way, so genwav, gencheck,
     * dspcheck and a mirror all render a graph with a live input in it
     * identically and reproducibly. A host that *was* feeding and stops --
     * a mic revoked, a stream closed -- has to feed silence rather than stop
     * calling, for the reason thChanEffect gives about a side channel going
     * away: a buffer nobody writes any more is the last window of it
     * repeating for ever.
     *
     * `frames' longer than a window is truncated and shorter is zero-filled
     * to the end, so what a graph reads is always a whole window. */
    void feedCapture (const float *mono, unsigned frames);

    /* One window of it, or NULL on a synth with none. Audio thread. */
    const float *capture (void) const { return capture_; }

    /* ---- a synth that never renders ----
     *
     * A mirror -- the composer view's second scheduler in the browser, or
     * a harness holding a piece up against itself -- needs a thSynth to
     * hand its scheduler, and needs it to behave like the real one in
     * every way but the sound: instruments load, chanargs are written and
     * read back, channels come and go. What it must not do is build a
     * note graph for every note the scheduler delivers and queue it for
     * an audio thread that is not there. Stepping a scheduler over an
     * ordinary synth without rendering was measured filling the 1024-deep
     * command ring and dropping commands within one fast-forward -- and
     * among what it dropped was a SET_CHANNEL, so an instrument silently
     * failed to load.
     *
     * Silent, a synth drops notes at the door: addNote copies nothing
     * and queues nothing, delNote and clearAll post nothing, and
     * process() applies the queue and skips the DSP. Everything else is
     * the same object doing the same thing, so a scheduler over a
     * silent synth composes the tape a scheduler over a rendering one
     * composes -- which gencheck holds it to, every seeded piece.
     *
     * Set once, before the first load. It is a kind of synth and not a
     * mode a running one flips: the notes a rendering synth is holding
     * would never be released, and the ones a silent one dropped would
     * never arrive. */
    void setSilent (bool silent);
    bool silent (void) const { return silent_; }

    /* GUI thread. Whether a finished voice goes back to its channel to be
       started over (see thMidiChan::recycle) or is deleted. On by default;
       off is every note a fresh copy, which is what scripts/poolcheck
       compares against. */
    void setVoicePool (bool on) { pool_ = on; }

    /* Commands postCommand could not queue because the ring was full,
       since construction. Read on the GUI thread, which is the one that
       posts. The number a mirror watches: a rendering synth drains its
       ring every window, and a silent one drains it on every step it is
       given, so on either this should stay at zero, and a page or a
       harness that finds it moving has found a synth nobody is
       stepping. */
    unsigned long droppedCommands (void) const { return dropped_; }

    /* Voices thMidiChan's guard dropped for going non-finite, since
     * construction. What lets a render report `non-finite voices: 3' rather
     * than hand back a file that is quiet for no stated reason.
     *
     * Written by the audio thread and read by anything, hence the atomic;
     * a tally, so relaxed ordering suffices. Never reset -- the question is
     * "did it ever happen". */
    unsigned long nonFiniteVoices (void) const {
        return nonFinite_.load(std::memory_order_relaxed);
    }

    /* Audio thread, from thMidiChan's guard. Public because a channel is not
       a friend and does not need to be: the counter is write-only from there. */
    void countNonFiniteVoice (void) {
        nonFinite_.fetch_add(1, std::memory_order_relaxed);
    }

    void printChan(int chan);

    /* False when the audio thread could not be told -- a full command ring.
       The channel is then still loaded and still sounding, so a caller
       keeping its own record of what is on it has to keep that record too;
       throwing it away is how a graph ends up playing with nothing left able
       to name it. */
    bool removeChan (int channum);

    int audioChannelCount (void) const { return channels_; }

    /* GUI thread: reads the GUI's own view of the channels, never the audio
       thread's array. */
    thArgMap getChanArgs (int chan) {
        if ((chan < 0) || (chan >= midiChannelCnt_) ||
            (guiChannels_[chan] == NULL))
            return thArgMap();

        return guiChannels_[chan]->args();
    }

    /* The effect's chanargs, which are a second set and not merged with the
       instrument's: an instrument's `@a' and an effect's must not collide.
       Addressed as `fx.<name>' wherever one map of names is wanted. */
    thArgMap getEffectArgs (int chan) {
        if ((chan < 0) || (chan >= midiChannelCnt_) ||
            (guiEffects_[chan] == NULL))
            return thArgMap();

        return guiEffects_[chan]->args();
    }

    thChanEffect *getEffect (int chan) {
        if ((chan < 0) || (chan >= midiChannelCnt_))
            return NULL;

        return guiEffects_[chan];
    }

    int getWindowlen (void) const { return windowlen_; }
    void setWindowlen (int);

    float *getOutput (void) const;

    /* Audio thread only, immediately after process(): the channel's
       interleaved sum after its effect, before the master mix. NULL when
       the slot is empty. The buffer lasts until the next process() call.
       `channels' receives the number of channels in that buffer. */
    const float *getChannelOutput (int chan, int *channels) const;

    float *getChanBuffer (int chan);

    long getSampleRate (void) const { return sampleRate_; }
    void setSampleRate (long samples) { sampleRate_ = samples; }

    /* Master gain, applied to the summed mix before the output limiter.
     *
     * Written from the GUI thread and read by the audio thread every window,
     * so it goes through a relaxed atomic rather than the command queue -- the
     * same reasoning as thArg::setValue for a slider. A torn read of a gain
     * would be audible; last-writer-wins is exactly what a fader wants. */
    void setMasterGain (float gain)
    {
        if (gain < 0.0f)
            gain = 0.0f;
        else if (gain > TH_MASTER_GAIN_MAX)
            gain = TH_MASTER_GAIN_MAX;

        __atomic_store(&masterGain_, &gain, __ATOMIC_RELAXED);
    }

    float masterGain (void) const
    {
        float gain;

        __atomic_load(&masterGain_, &gain, __ATOMIC_RELAXED);

        return gain;
    }

    int midiChanCount (void) const { return midiChannelCnt_; }

    /* A chanarg by name. `fx.<name>' reaches the channel effect's, anything
       else the instrument's -- see TH_EFFECT_PREFIX. */
    thArg *getChanArg (int channum, const string &argname);
    void setChanArg (int channum, thArg *arg);

    /* The graph that runs on a channel's summed voices, once per window.
     *
     * Beside loadTree, and after it: a channel swap takes its effect with it,
     * so an instrument is loaded first and its effect put on afterwards. The
     * returned tree is owned by the effect, which is owned by the channel;
     * NULL means the file did not load, and nothing changed.
     *
     * A graph whose io node has no in0 is refused. It would run -- every
     * window, on nothing -- and produce whatever a graph with no input
     * produces, which is the failure mode the .dsp/effect distinction exists
     * to make impossible to reach by accident.
     *
     * `sideChan' is a second channel the effect hears, in its io node's
     * side0..side<N-1>, or -1 for the usual effect that hears only its own.
     * That channel is run first, every window, so the side carries what is
     * being mixed rather than what was mixed last time -- which is why a
     * channel may not name itself, directly or through a chain of other
     * channels' sides, and why such a load is refused here rather than
     * sorted out on the audio thread. A side naming a channel with nothing
     * on it is not an error: it is silence, and the instrument may yet
     * arrive. -1 is not silence but this channel's own audio; see
     * thChanEffect. */
    thSynthTree *loadEffect (const string &filename, int channum,
                             int sideChan = -1);

    /* Takes the effect off `channum'. True if the audio thread was told;
       false only when the command queue is full, in which case the effect is
       still running and the caller has to try again. */
    bool removeEffect (int channum);

    /* The same graph on the sum of every channel, run after the mix and
     * before the master gain and the limiter.
     *
     * A reverb on four channels is four reverbs; a limiter on a channel is
     * not limiting the thing that clips. Both want one graph at the end of
     * the piece, and this is it -- the channel effect's object, its chanarg
     * map and its non-finite guard, on the buffer getOutput() hands back.
     *
     * NULL means the file did not load and nothing changed. The refusal of a
     * graph with no in0 is loadEffect's, for loadEffect's reason. */
    thSynthTree *loadMasterEffect (const string &filename);

    /* Takes the master effect off. False only when the command queue is
       full, in which case it is still running. */
    bool removeMasterEffect (void);

    thChanEffect *getMasterEffect (void) { return guiMaster_; }

    /* Its chanargs, which are its own for the reason a channel effect's are
       its own, and named without the `fx.' prefix: nothing else is addressed
       on the mix, so there is nothing here for them to collide with. */
    thArgMap getMasterEffectArgs (void)
    {
        return guiMaster_ ? guiMaster_->args() : thArgMap();
    }

    thArg *getMasterArg (const string &argname)
    {
        return guiMaster_ ? guiMaster_->getArg(argname) : NULL;
    }

    /* Writes one of them. Takes ownership of `arg' either way, like
       setChanArg, and refuses a name the graph did not declare for the
       reason the `fx.' side of setChanArg refuses one. */
    void setMasterArg (thArg *arg);

    /* A controller message off the wire. MIDI Map's bindings get it first;
     * the sustain pedal, TH_MIDI_CC_SUSTAIN, that nothing is bound to on its
     * channel goes to that channel's SusPedal, which is what a keyboard's
     * pedal is expected to do without anybody having to say so. A binding on
     * CC 64 replaces that on its channel, as a binding replaces what any
     * controller did before. */
    void handleMidiController (unsigned char channel, unsigned int param,
                               unsigned int value);

    void newMidiControllerConnection (unsigned char channel,
                                      unsigned int param,
                                      thMidiControllerConnection *connection);

    thMidiController::ConnectionMap *getMidiConnectionMap (void) { 
        return controllerHandler_->connectionMap();
    }

    thMidiControllerConnection *getMidiControllerConnection
    (unsigned char channel, unsigned int param) { 
        return controllerHandler_->getConnection(channel, param);
    }

    /* GUI thread. Deliberately the GUI's view: the audio thread's array is
       written by the audio thread and must not be read from here. */
    thMidiChan *getChannel (int chan) const
    {
        if ((chan < midiChannelCnt_) && (chan >= 0))
            return guiChannels_[chan];
        else
            return NULL;
    }

    /* GUI thread: frees whatever the audio thread has handed back. Called at
       the top of every GUI-side entry point, so an idle GUI is the only way
       for retired objects to sit around. */
    void collectRetired (void);
    void recycleNote (thMidiNote *note);

    /* ---- probes ----
     *
     * A tap on one node's arg on one channel, summed across every sounding
     * voice and published a window at a time. See thProbe.h for why the
     * address of a node survives the per-note tree copy, which is what makes
     * this affordable inside the callback.
     *
     * armProbe resolves `node' and `arg' against the channel's current tree on
     * the GUI thread, allocates the probe there, and queues the install.
     * Returns the slot it took, or -1 with `why' set.
     *
     * The slot is an output, not an input: there is no way to ask for a
     * particular one. Arming a point that is already armed returns the slot it
     * already has, which makes arming twice idempotent rather than a way to
     * burn two of the eight; anything else takes the lowest free slot and
     * fails when there is none. So a slot number is only good until the probe
     * holding it is disarmed -- callers should hold the node and arg names,
     * which is what everything else about a probe is keyed on.
     *
     * The resolution is only meaningful for the tree the channel is playing
     * *now*: ids are assigned in parse order, so adding or removing a node
     * shifts every later one. Loading a patch therefore disarms every probe on
     * that channel (see applyCommand), and the caller re-arms by name. */
    int armProbe (int channum, const string &node, const string &arg,
                  string &why);
    int armProbe (int channum, const string &node, const string &arg)
    {
        string why;
        return armProbe(channum, node, arg, why);
    }

    void disarmProbe (int slot);

    /* GUI thread: the GUI's own view, for the same reason getChannel() is.
       NULL for a slot that is not armed. */
    thProbe *probe (int slot) const
    {
        if ((slot < 0) || (slot >= TH_MAX_PROBES))
            return NULL;

        return guiProbes_[slot];
    }

    int probeCount (void) const { return TH_MAX_PROBES; }

private:
    /* Shared tail of the three loadTree() overloads: checks the parse result,
       validates the tree, and resolves it. Returns NULL (having discarded the
       half-built tree) if the .dsp did not parse into something usable.

       registerTree: true puts the tree in treelist_ under thSynth's ownership;
       false gives the caller an unowned tree to hand to a thMidiChan. */
    thSynthTree *finishParse (const string &what, thSynthTree *tree,
                              int parseResult,
                              bool registerTree);

    /* Applies one queued command. Audio thread. */
    void applyCommand (const thSynthCommand &cmd);

    /* Drains the command queue. Audio thread, at the top of process(). */
    void drainCommands (void);

    /* GUI thread: queue a command, cleaning up the payload if the ring is
       full. Returns false if it was dropped. */
    bool postCommand (const thSynthCommand &cmd);

    /* Audio thread: retires whatever is in `probes_[slot]' and installs
       `probe' (which may be NULL). */
    void installProbe (int slot, thProbe *probe);

    /* GUI thread: disarms every probe pointing at `channum'. Called before
       queueing a SET_CHANNEL, so that the disarm is applied first -- the queue
       is FIFO, which is what makes the ordering a guarantee rather than a
       hope. Assumes synthMutex_ is already held. */
    void disarmProbesOn (int channum);

    /* The file half of loading an effect graph, shared by the channel's and
       the mix's. Assumes synthMutex_ is already held. */
    thSynthTree *parseEffect (const string &filename);

    /* GUI thread, with synthMutex_ held. True if putting an effect with
       `sideChan' onto `channum' would make a channel wait on itself --
       directly, or around a ring of channels that each hear the next.
       Walking the GUI's view of the effects, because that is the one the
       loading thread owns. */
    bool sideWouldCycle (int channum, int sideChan) const;

    /* Audio thread. What order to run the channels in, so that a channel
     * named as somebody's side has been mixed before the effect that listens
     * to it runs. Writes `midiChanCount()' entries into `order' and returns
     * how many -- which is every live slot, in index order, for a rack with
     * no side in it, so a piece that uses none sums its channels in exactly
     * the order it always did. */
    int orderChannels (int *order) const;

    map<string, thSynthTree*> treelist_;
    map<int, string> patchlist_;
    thPluginManager *pluginmanager_;

    /* Two views of the same channel objects.
     *
     * midiChannels_ is the audio thread's, written only by applyCommand().
     * guiChannels_ is the GUI thread's, written only by the GUI. They agree
     * except in the window between the GUI queueing a SET_CHANNEL and the
     * audio thread applying it, during which both objects are alive and each
     * thread is looking at one of them. Neither thread ever reads the other's
     * array, which is what removes the race. */
    thMidiChan **midiChannels_; /* MIDI channels -- audio thread */
    thMidiChan **guiChannels_;  /* the same channels -- GUI thread */

    /* The GUI's view of each channel's effect, for the same reason
     * guiChannels_ exists: the audio thread writes thMidiChan::effect_, and
     * the GUI wants to reach the effect's chanargs without reading what the
     * other thread is writing.
     *
     * A reference rather than ownership -- the channel owns its effect, and a
     * channel swap destroys it -- so these entries are cleared when a channel
     * goes rather than freed. */
    thChanEffect **guiEffects_;

    /* The graph on the mix. master_ is the audio thread's and is owned by
     * it -- installed and retired through the command queue, exactly as a
     * channel is -- and guiMaster_ is the GUI's name for the same object,
     * so that a slider can reach its args without reading what the audio
     * thread is reading.
     *
     * Unlike guiEffects_, which point at effects their channels own, this
     * pair is the whole ownership story: there is no channel underneath to
     * take it down, so the destructor does. */
    thChanEffect *master_;
    thChanEffect *guiMaster_;

    /* Whether the divergence above has already been reported for this
       master effect. Cleared when one is installed, so a graph that is
       loaded, fails, and is loaded again says so twice and not once. */
    bool masterSaidSo_;

    /* Two views of the same probes, for exactly the reason the channel arrays
       are two: probes_ is written only by applyCommand() on the audio thread,
       guiProbes_ only by the GUI. Both name the same object, and it is the
       audio thread's release of a slot -- through retired_ -- that says the
       GUI may destroy it. */
    thProbe *probes_[TH_MAX_PROBES];     /* audio thread */
    thProbe *guiProbes_[TH_MAX_PROBES];  /* the same probes -- GUI thread */

    int midiChannelCnt_;
    float *output_;

    /* Every channel's output scaled by its send, summed, laid out like
       output_ -- what the master effect hears as send<N>. */
    float *send_;
    int channels_;  /* Number of channels (mono/stereo/etc) */
    int windowlen_;
    float masterGain_;  /* see setMasterGain(); accessed atomically */
    long sampleRate_; /* the number of samples per second*/

    bool silent_;               /* see setSilent()                     */
    bool pool_;                 /* see setVoicePool()                  */

    /* One window of mono capture, zeroed at construction. See feedCapture. */
    float *capture_;
    unsigned long dropped_;     /* see droppedCommands()               */

    std::atomic<unsigned long> nonFinite_;  /* see nonFiniteVoices() */

    thMidiController *controllerHandler_;

    thRing<thSynthCommand, TH_COMMAND_QUEUE_SIZE> commands_;  /* GUI -> audio */
    thRing<thRetired, TH_RETIRE_QUEUE_SIZE> retired_;         /* audio -> GUI */

    /* Serialises GUI-thread callers against each other (the parser globals are
       not reentrant either). The audio thread does not take it -- that is the
       whole point of the queues.
     *
     * std::mutex rather than pthread_mutex_t. This header never included
     * <pthread.h>; on glibc the type arrived transitively through something
     * else, which is exactly the sort of thing that only shows up when a
     * second platform compiles it -- MinGW-w64 has winpthreads but does not
     * leak the type, so every translation unit failed with "pthread_mutex_t
     * does not name a type". */
    std::mutex synthMutex_;

    static thSynth *instance_;
};

#endif /* TH_SYNTH_H */
