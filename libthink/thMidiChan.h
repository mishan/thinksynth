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

    const thArgMap &args (void) const { return args_; }

    float *output (void) const { return output_; }
    int numChannels (void) const { return channels_; }

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

    /* Hands `note' to the GUI thread to destroy. Falls back to deleting it
       here if the retire queue is full -- that costs RT-safety in a case that
       should not arise, but never correctness. */
    void retireNote (thMidiNote *note, RetireQueue *retire);

    bool dirty_;
    thSynthTree *modnode_;
    thArgMap args_;
    NoteMap notes_;
    NoteList decaying_;  /* linked list for decaying notes */
    NoteList noteorder_; /* order of the notes for polyphony limits */
    int channels_, windowlength_;
    float *output_;
    int outputnamelen_;
    int polymax_;  /* maximum polyphony */
    int notecount_, notecount_decay_;  /* keeping track of polyphony this way
                                        for now */
    thArg *argSustain_; /* for the sustain pedal */

    unsigned long serial_;

    static std::atomic<unsigned long> nextSerial_;
};

#endif /* TH_MIDICHAN_H */
