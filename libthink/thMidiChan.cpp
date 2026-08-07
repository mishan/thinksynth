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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"
#include "thUtil.h"

thMidiChan::thMidiChan (thSynthTree *mod, float amp, int windowlen)
{
    const thArg *chanarg = NULL;

    modnode_ = mod;
    windowlength_ = windowlen;
    dirty_ = 1;
    channels_ = 1;
    output_ = NULL;
    outputnamelen_ = 0;
    polymax_ = 10;
    notecount_ = 0;
    notecount_decay_ = 0;
    argSustain_ = NULL;

    if (!mod) {
        /* This used to print and then dereference mod anyway. */
        fprintf(stderr, "thMidiChan::thMidiChan: NULL mod passed\n");
    }
    else {
        copyChanArgs(mod);
        assignChanArgPointers(mod);
    }

    args_[string("amp")] = new thArg(string("amp"), amp);

    if (modnode_) {
        chanarg = modnode_->getArg("channels");
    }

    /* `channels' comes straight out of the .dsp, so it can be absent, zero, or
       absurd. It sizes an allocation and drives the mix loop in thSynth. */
    if (chanarg && chanarg->values()) {
        channels_ = (int)chanarg->values()[0];
    }

    if (channels_ < 1 || channels_ > TH_MAX_CHANNELS) {
        fprintf(stderr, "thMidiChan::thMidiChan: channel count %d out of "
                "range, clamping to 1\n", channels_);
        channels_ = 1;
    }

    output_ = new float[channels_*windowlen];
    memset(output_, 0, channels_ * windowlen * sizeof(float));
    outputnamelen_ = strlen(OUTPUTPREFIX) + thUtil::getNumLength(channels_);

    argSustain_ = new thArg(string("SusPedal"), 0);
    argSustain_->setWidgetType(thArg::CHANARG);
    argSustain_->setLabel(string("Sustain Pedal"));
    args_["SusPedal"] = argSustain_;
}

thMidiChan::~thMidiChan (void)
{
    /* clearAll() covers notes_, decaying_ and noteorder_; the old destructor
       only walked notes_ and leaked every decaying note. Notes hold copies of
       modnode_, so they have to go first.

       NULL retire queue: by the time a channel is destroyed the GUI thread has
       already taken it back off the audio thread, so there is nobody left to
       hand the notes to and they are freed directly. */
    clearAll(NULL);

    DestroyMap(args_);

    /* We own the tree (see the constructor comment). */
    delete modnode_;
    modnode_ = NULL;

    delete[] output_;
    output_ = NULL;
}

void thMidiChan::retireNote (thMidiNote *note, RetireQueue *retire)
{
    if (note == NULL)
        return;

    thRetired item;

    item.kind = thRetired::NOTE;
    item.note = note;

    /* Destroying a note tears down a whole synth tree, so hand it to the GUI
       thread. If the queue is backed up, take the hit here rather than leak. */
    if (retire == NULL || !retire->push(item))
        delete note;
}

void thMidiChan::setArg (thArg *arg, RetireQueue *retire)
{
    if (arg == NULL)
    {
        return;
    }

    thArg *oldArg = args_[arg->name()];

    /* Guard self-assignment: deleting and then storing the same pointer back
       would leave the map holding freed memory. */
    if (oldArg == arg)
    {
        return;
    }

    if (oldArg)
    {
        thRetired item;

        item.kind = thRetired::ARG;
        item.arg = oldArg;

        /* Node args in live note trees still point at oldArg until
           assignChanArgPointers() below re-resolves them, so it cannot be
           freed here. */
        if (retire == NULL || !retire->push(item))
            delete oldArg;
    }

    args_[arg->name()] = arg;

    if (arg->name() == "SusPedal")
    {
        argSustain_ = arg;
    }

    /* Node args in the shared tree hold raw thArg* into this map (see
       assignChanArgPointers), so swapping an arg out from under them leaves
       those pointers dangling. Re-resolve them. */
    if (modnode_)
    {
        assignChanArgPointers(modnode_);
    }
}

/* GUI thread.
 *
 * All this does is allocate. It reads modnode_, which the audio thread never
 * writes -- notes run on their own copies of the tree, not on the prototype.
 */
thMidiNote *thMidiChan::buildNote (float note, float velocity)
{
    if (modnode_ == NULL)
        return NULL;

    return new thMidiNote(modnode_, note, velocity * TH_MAX / MIDIVALMAX);
}

/* Audio thread. This is what used to be the second half of addNote(), and it
   is the part that has to be here: touching notes_ and noteorder_ from the GUI
   thread while process() walked them is what produced the static. */
void thMidiChan::insertNote (thMidiNote *midinote, RetireQueue *retire)
{
    if (midinote == NULL)
        return;

    int id = midinote->id();

    NoteMap::iterator i = notes_.find(id);

    if (i != notes_.end()) {
        /* Make sure to turn off the old note, or it will hang! */
        i->second->setArg("trigger", 0);

        noteorder_.remove(i->second);
        decaying_.push_front(i->second);
        notes_.erase(i);
        notecount_decay_++; /* we are keeping track of polyphony this way until
                              the advanced cool method is implemented */
        /* no need to dec notecounter since the new note replaces this one */
    }
    notecount_++; /* see notecount_decay_++ comment */

    notes_[id] = midinote;
    noteorder_.push_back(midinote);

    (void)retire;
}

/* Audio thread. Was thSynth::delNote poking the note's `trigger' arg directly
   from the GUI thread. */
void thMidiChan::releaseNote (int note)
{
    NoteMap::iterator i = notes_.find(note);

    /* find() returning end() was not checked, so an unknown note dereferenced
       the past-the-end iterator. */
    if (i == notes_.end())
    {
        return;
    }

    int sustain = argSustain_ ? (int)(*argSustain_)[0] : 0;

    /* 2 means "released but held by the pedal"; process() turns it into 0 when
       the pedal comes up. */
    i->second->setArg("trigger", sustain ? 2 : 0);
}

/* Audio thread (or the destructor, once the audio thread has stopped). */
void thMidiChan::clearAll (RetireQueue *retire)
{
    /* This used to be:
     *
     *   for (i = notes_.begin(); i != notes_.end(); i++) {
     *       delete i->second; notes_.erase(i);
     *   }
     *
     * -- erase() invalidates i, and then i++ advances the dead iterator. The
     * decaying_ loop had the same bug plus a double advance (erase() returns
     * the next element and the loop then incremented past it).
     *
     * noteorder_ was never cleared either, so it was left holding pointers to
     * every note this function had just freed.
     */
    for (NoteMap::iterator i = notes_.begin(); i != notes_.end(); ++i)
    {
        retireNote(i->second, retire);
    }
    notes_.clear();

    for (NoteList::iterator j = decaying_.begin(); j != decaying_.end(); ++j)
    {
        retireNote(*j, retire);
    }
    decaying_.clear();

    noteorder_.clear();

    notecount_ = 0;
    notecount_decay_ = 0;
}

thMidiNote *thMidiChan::getNote (int note)
{
    NoteMap::iterator i = notes_.find(note);

    if (i != notes_.end()) {
        return i->second;
    }

    return NULL;
}

int thMidiChan::setNoteArg (int note, const string &name, float value)
{
    NoteMap::iterator i = notes_.find(note);

    if (i != notes_.end()) {
        i->second->setArg(name, value);
        return 1;
    }

    return 0;
}

int thMidiChan::setNoteArg (int note, const string &name, const float *value,
                            int len)
{
    NoteMap::iterator i = notes_.find(note);

    if (i != notes_.end()) {
        i->second->setArg(name, value, len);
        return 1;
    }

    return 0;
}

void thMidiChan::copyChanArgs (thSynthTree *tree)
{
    thArg *data, *newdata;

    if (tree == NULL)
    {
        return;
    }

    const thArgMap &sourceargs = tree->chanArgs();

    for (thArgMap::const_iterator i = sourceargs.begin();
         i != sourceargs.end(); i++)
    {
        data = i->second;

        if (data == NULL)
        {
            continue;
        }

        newdata = new thArg(data);

        /* Don't leak the arg this replaces. */
        thArgMap::iterator existing = args_.find(i->first);

        if (existing != args_.end())
        {
            delete existing->second;
            existing->second = newdata;
        }
        else
        {
            args_[i->first] = newdata;
        }
    }
}

void thMidiChan::process (RetireQueue *retire)
{
    if (output_ == NULL || windowlength_ <= 0)
    {
        return;
    }

    if (dirty_)
    {
        memset (output_, 0, windowlength_*channels_*sizeof(float));
    }
    dirty_ = false;


    thMidiNote *data;
    thArg *arg, *amp, *play, *trigger;
    thSynthTree *tree;
    int i, j, index;
    float buf_mix[windowlength_];
    float buf_amp[windowlength_];

    string argname;

    int sustain = argSustain_ ? (int)(*argSustain_)[0] : 0;

    amp = getArg("amp");

    if (amp == NULL)
    {
        return;
    }

    /* Before any processing, we shall do a polyphony test. */
    if (notecount_ + notecount_decay_ > polymax_ && polymax_ > 0) 
    {
        /* we have too many notes, and polyphony > 0 */

        if (notecount_decay_ > 0) /* there are some notes not being held down */
        {
            NoteList::iterator iter = decaying_.begin();

            /* more to do */
            while (iter != decaying_.end() && notecount_decay_ > 0 &&
                  notecount_ + notecount_decay_ > polymax_)
            {
                retireNote(*iter, retire);
                iter = decaying_.erase(iter);
                notecount_decay_--;
            }
        }
        if (notecount_ > polymax_) /* too many notes held down */
        {
            NoteList::iterator iter = noteorder_.begin();
            while (iter != noteorder_.end() && notecount_ > polymax_)
            {
                notes_.erase((*iter)->id());
                retireNote(*iter, retire);
                iter = noteorder_.erase(iter);
                notecount_--;
            }
        }
    }

    /* re-count the notes as we process, just to be sure nothing screws up */
    notecount_ = 0;
    notecount_decay_ = 0;

    NoteMap::iterator iter = notes_.begin();
    while (iter != notes_.end())
    {
        notecount_++;

        data = iter->second;

        dirty_ = true;
        
        data->process(windowlength_);

        tree = data->synthTree();
        play = tree ? tree->getArg("play") : NULL;
        trigger = tree ? tree->getArg("trigger") : NULL;

        if (trigger && (*trigger)[0] == 2 && sustain < 0x40)
            trigger->setValue(0);

        for (i = 0; tree && i < channels_; i++)
        {
            argname = OUTPUTPREFIX;
            argname += (char)(i+'0');
            arg = tree->getArg(argname);

            /* A DSP whose io node declares more channels than it wires up has
               no outN arg for the missing ones. */
            if (arg == NULL)
                continue;

            arg->getBuffer(buf_mix, windowlength_);
            amp->getBuffer(buf_amp, windowlength_);

            index = i;
            for (j = 0; j < windowlength_; j++)
            {
                output_[index] += buf_mix[j]*(buf_amp[j]/MIDIVALMAX);
                index += channels_;
            }
        }

        NoteMap::iterator olditer = iter++;  /* a copy of the
                                                old iterator to erase */

        if (play && (*play)[windowlength_ - 1] == 0)
        {
            noteorder_.remove(data);
            notes_.erase(olditer);
            notecount_--;  /* polyphony stuff */

            /* Hand it to the GUI thread: this frees an entire synth tree. */
            retireNote(data, retire);
        }
    }

    /* Now, the [almost] exact same thing for the list of decaying notes */
    NoteList::iterator diter = decaying_.begin();

    while (diter != decaying_.end())
    {
        notecount_decay_++;
        data = *diter;
        dirty_ = 1;
        data->process(windowlength_);
        tree = data->synthTree();
        play = tree ? tree->getArg("play") : NULL;
        trigger = tree ? tree->getArg("trigger") : NULL;

        if (trigger && (*trigger)[0] == 2 && sustain < 0x40)
            trigger->setValue(0);

        for (i = 0; tree && i < channels_; i++)
        {
            argname = OUTPUTPREFIX;
            argname += (char)(i+'0');
            arg = tree->getArg(argname);

            if (arg == NULL)
                continue;

            arg->getBuffer(buf_mix, windowlength_);
            amp->getBuffer(buf_amp, windowlength_);

            index = i;
            for (j = 0; j < windowlength_; j++)
            {
                output_[index] += buf_mix[j]*(buf_amp[j]/MIDIVALMAX);
                index += channels_;
            }
        }
        
        if (play && (*play)[windowlength_ - 1] == 0)
        {
            diter = decaying_.erase(diter);
            retireNote(data, retire);
            notecount_decay_--;  /* more polyphony stuff */
        }
        else
        {
            diter++;
        }
    }
}

/* XXX: the tree this walks is shared between every channel that loaded the same
   .dsp (thSynth::treelist_), so the last channel constructed wins and the
   others are left pointing into a thMidiChan that may since have been
   destroyed. Fixing that properly means giving each channel its own tree, or
   resolving chanargs at process time rather than caching pointers. */
void thMidiChan::assignChanArgPointers (thSynthTree *tree)
{
    thNode *curnode;
    thArg *curarg;

    if (tree == NULL)
    {
        return;
    }

    const thSynthTree::NodeMap &sourcenodes = tree->nodes();

    for (thSynthTree::NodeMap::const_iterator i = sourcenodes.begin();
         i != sourcenodes.end(); i++)
    {
        curnode = i->second;

        if (curnode == NULL)
        {
            continue;
        }

        const thArgMap &sourceargs = curnode->args();

        for (thArgMap::const_iterator j = sourceargs.begin();
         j != sourceargs.end(); j++)
        {
            curarg = j->second;

            if (curarg && curarg->type() == thArg::ARG_CHANNEL)
            {
                /* getArg() rather than args_[...]: the latter inserted a NULL
                   for every unknown chanarg name, and the NULL then got handed
                   to the audio thread as an arg pointer. */
                thArg *target = getArg(curarg->argPtrName());

                if (target == NULL)
                {
                    fprintf(stderr, "thMidiChan: node '%s' arg '%s' references "
                            "undeclared chanarg '@%s'\n",
                            curnode->name().c_str(), curarg->name().c_str(),
                            curarg->argPtrName().c_str());
                }

                curarg->setArgPtr(target);
            }
        }
    }
}
