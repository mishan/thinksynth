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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

/* Never reset, never reused. See thMidiChan::serial(). */
std::atomic<unsigned long> thMidiChan::nextSerial_(1);

thMidiChan::thMidiChan (thSynthTree *mod, float amp, int windowlen)
{
    const thArg *chanarg = NULL;

    serial_ = nextSerial_.fetch_add(1, std::memory_order_relaxed);

    modnode_ = mod;
    windowlength_ = windowlen;
    dirty_ = 1;
    channels_ = 1;
    output_ = NULL;
    bufmix_ = NULL;
    bufamp_ = NULL;
    playindex_ = -1;
    triggerindex_ = -1;

    for (int i = 0; i < TH_MAX_CHANNELS; i++)
    {
        outindex_[i] = -1;
    }

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

    /* The channel's own level. Given a range, a label and a widget type so
       it shows up as a slider wherever a channel's parameters are listed --
       it is the one control every patch has, and it was the one control you
       had to open another window to reach. 0..MIDIVALMAX because that is the
       scale process() mixes with, dividing by it per sample. */
    {
        thArg *a = new thArg(string("amp"), amp);

        a->setMin(0);
        a->setMax(MIDIVALMAX);
        a->setLabel("Amplitude");
        a->setWidgetType(thArg::SLIDER);

        args_[string("amp")] = a;
    }

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

    output_ = new float[thOutputSamples(channels_, windowlength_)];
    memset(output_, 0,
           thOutputSamples(channels_, windowlength_) * sizeof(float));

    /* windowlength_ never changes for a given channel -- a window-length change
       builds new channels -- so the mix loop's scratch is allocated here rather
       than grown on the stack of every call. Value-initialised for the same
       reason thArg::allocate() is: getBuffer() leaves the buffer alone when the
       arg it was handed is not a plain value, and a window of stale stack is
       not something to hand to an output mixer. */
    if (windowlength_ > 0)
    {
        bufmix_ = new float[windowlength_]();
        bufamp_ = new float[windowlength_]();
    }

    indexIOArgs();

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

    delete[] bufmix_;
    bufmix_ = NULL;

    delete[] bufamp_;
    bufamp_ = NULL;
}

/* GUI thread, once, at construction.
 *
 * Two jobs, and the second is the one that matters. It records where the io
 * node keeps out0..outN-1 and play so process() can reach them by subscript;
 * and where the .dsp did not write one, it creates it *here*, so that every
 * note copied from this tree already has it.
 *
 * That second half is not tidiness. thSynthTree::getArg(node, name) invents a
 * zero-valued arg for a name it cannot find, which allocates and inserts into a
 * std::map -- and process() called it by name, on the note's own tree. So a
 * .dsp whose io node declares `channels = 2' and only ever assigns out0 had the
 * first window of every note allocate and insert, on the audio thread. It is
 * the same argument finishParse() already makes for note, velocity and trigger,
 * one layer down.
 *
 * On the channel's prototype and not in finishParse() with those three,
 * deliberately: the node editor parses its own tree through the same call, and
 * an out1 the author never wrote is exactly the phantom port that
 * NodeGraph::ioArgIsSink exists to keep off the audio-out box. The engine's
 * copy is where an arg the engine invents belongs.
 */
void thMidiChan::indexIOArgs (void)
{
    if (modnode_ == NULL || modnode_->IONode() == NULL)
    {
        return;
    }

    thNode *io = modnode_->IONode();

    /* One digit: TH_MAX_CHANNELS is ten for exactly this reason. */
    string name;

    for (int i = 0; i < channels_ && i < TH_MAX_CHANNELS; i++)
    {
        name = OUTPUTPREFIX;
        name += (char)(i + '0');

        thArg *arg = io->getArg(name);

        if (arg == NULL)
        {
            arg = io->setArg(name, 0);
        }

        if (arg)
        {
            outindex_[i] = arg->index();
        }
    }

    /* play and trigger by the same rule. trigger is already guaranteed by
       finishParse(); asking for it here costs nothing and means this function
       does not depend on that staying true. */
    thArg *play = io->getArg("play");

    if (play == NULL)
    {
        play = io->setArg("play", 0);
    }

    if (play)
    {
        playindex_ = play->index();
    }

    thArg *trigger = io->getArg("trigger");

    if (trigger == NULL)
    {
        trigger = io->setArg("trigger", 0);
    }

    if (trigger)
    {
        triggerindex_ = trigger->index();
    }
}

/* Audio thread. */
thArg *thMidiChan::resolveIOArg (thSynthTree *tree, int index)
{
    if (tree == NULL || index < 0)
    {
        return NULL;
    }

    thArg *arg = tree->getArg(tree->IONode(), index);

    /* See the header: the indexed overload dereferences a chanarg before its
       pointer chase and not after, so a chase that ends on one comes back
       undereferenced. The by-name overload this replaced did it at the end. */
    if (arg && arg->type() == thArg::ARG_CHANNEL)
    {
        arg = arg->argPtr();
    }

    return arg;
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

void thMidiChan::process (RetireQueue *retire, thProbe *const *probes,
                          int nprobes)
{
    if (output_ == NULL || bufmix_ == NULL || bufamp_ == NULL ||
        windowlength_ <= 0)
    {
        return;
    }

    if (dirty_)
    {
        memset(output_, 0,
               thOutputSamples(channels_, windowlength_) * sizeof(float));
    }
    dirty_ = false;


    thMidiNote *data;
    thArg *amp, *play;

    int sustain = argSustain_ ? (int)(*argSustain_)[0] : 0;

    amp = getArg("amp");

    if (amp == NULL)
    {
        return;
    }

    /* Once, rather than per channel per note. amp is the channel's own arg, not
       the note's: it was the same buffer every time round both loops, fetched
       afresh each time. */
    amp->getBuffer(bufamp_, windowlength_);

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

        play = mixNote(data, sustain, probes, nprobes);

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

    /* Now, the [almost] exact same thing for the list of decaying notes -- the
       `almost' being only the bookkeeping below, which is why the rest of it is
       mixNote() rather than a second copy. */
    NoteList::iterator diter = decaying_.begin();

    while (diter != decaying_.end())
    {
        notecount_decay_++;
        data = *diter;

        play = mixNote(data, sustain, probes, nprobes);

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

/* Audio thread. One note: run it, tap it, apply the pedal, and mix its channels
 * into output_. Returns the note's `play' arg, which is the only thing the two
 * loops in process() do differently with -- one erases from a map and a list,
 * the other from a list.
 *
 * Nothing here looks anything up by name. Every arg it needs was resolved to an
 * index at construction (see indexIOArgs), and both halves of an arg's address
 * survive the per-note tree copy, so reaching the same arg in this voice's own
 * tree is a subscript.
 */
thArg *thMidiChan::mixNote (thMidiNote *note, int sustain,
                            thProbe *const *probes, int nprobes)
{
    if (note == NULL)
    {
        return NULL;
    }

    dirty_ = true;

    note->process(windowlength_);

    thSynthTree *tree = note->synthTree();

    if (tree == NULL)
    {
        return NULL;
    }

    /* Called for held and decaying notes alike, not just the held ones. A note
       in release is still sounding, and a display that went blank the instant a
       key came up would be wrong in the most visible way available. */
    for (int p = 0; p < nprobes; p++)
    {
        probes[p]->accumulate(tree);
    }

    thArg *play = resolveIOArg(tree, playindex_);
    thArg *trigger = resolveIOArg(tree, triggerindex_);

    /* 2 means "released but held by the pedal". */
    if (trigger && (*trigger)[0] == 2 && sustain < 0x40)
    {
        trigger->setValue(0);
    }

    /* channels_ is clamped to TH_MAX_CHANNELS in the constructor, which is what
       makes outindex_ big enough. */
    for (int i = 0; i < channels_; i++)
    {
        thArg *arg = resolveIOArg(tree, outindex_[i]);

        /* A DSP whose io node declares more channels than it wires up now has a
           zero-valued out<i> put there by indexIOArgs(), so reaching this means
           the pointer chase itself failed. */
        if (arg == NULL)
        {
            continue;
        }

        arg->getBuffer(bufmix_, windowlength_);

        int index = i;

        for (int j = 0; j < windowlength_; j++)
        {
            output_[index] += bufmix_[j] * (bufamp_[j] / MIDIVALMAX);
            index += channels_;
        }
    }

    return play;
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
