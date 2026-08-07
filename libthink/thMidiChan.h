/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

class thMidiChan {
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

    thMidiNote *addNote (float note, float velocity);
    void delNote (int note);

    void clearAll (void);

    void process (void);

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
    void setArg (thArg *arg);

    const thArgMap &args (void) const { return args_; }

    float *output (void) const { return output_; }
    int numChannels (void) const { return channels_; }

    thSynthTree *modnode (void) { return modnode_; }

    thArg *sustainPedal (void) { return argSustain_; }

    void copyChanArgs (thSynthTree *mod);
    
private:
    void assignChanArgPointers(thSynthTree *mod);

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
};

#endif /* TH_MIDICHAN_H */
