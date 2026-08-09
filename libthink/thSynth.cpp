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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>

#include "think.h"
#include "parser.h"

thSynth *thSynth::instance_ = NULL;

thSynth::thSynth (int windowlen, int samples)
{

    /* XXX: these should all be arguments and we should have corresponding
       accessor/mutator methods for these arguments */
    windowlen_ = windowlen;

    channels_ = 2;  /* mono / stereo / etc */

    /* intialize default sample rate */
    sampleRate_ = samples;

    /* We should make a function to allocate this, so we can easily change
       thChans and thWindowlen */
    output_ = new float[channels_*windowlen_];

    /* Fixed capacity -- see TH_MIDI_CHANNELS. Never reallocated, so the audio
       thread can iterate it without racing a resize. */
    midiChannelCnt_ = TH_MIDI_CHANNELS;
    midiChannels_ = (thMidiChan **)calloc(midiChannelCnt_, sizeof(thMidiChan *));
    guiChannels_ = (thMidiChan **)calloc(midiChannelCnt_, sizeof(thMidiChan *));

    masterGain_ = TH_MASTER_GAIN_DEFAULT;

    /* default path */
    pluginmanager_ = new thPluginManager(PLUGIN_PATH);

    controllerHandler_ = new thMidiController();

    if (instance_ == NULL)
        instance_ = this;
}

thSynth::thSynth (const string &plugin_path, int windowlen, int samples)
{

    /* XXX: these should all be arguments and we should have corresponding
       accessor/mutator methods for these arguments */
    windowlen_ = windowlen;

    channels_ = 2;  /* mono / stereo / etc */

    /* intialize default sample rate */
    sampleRate_ = samples;

    /* We should make a function to allocate this, so we can easily change
       thChans and thWindowlen */
    output_ = new float[channels_*windowlen_];

    /* Fixed capacity -- see TH_MIDI_CHANNELS. Never reallocated, so the audio
       thread can iterate it without racing a resize. */
    midiChannelCnt_ = TH_MIDI_CHANNELS;
    midiChannels_ = (thMidiChan **)calloc(midiChannelCnt_, sizeof(thMidiChan *));
    guiChannels_ = (thMidiChan **)calloc(midiChannelCnt_, sizeof(thMidiChan *));

    masterGain_ = TH_MASTER_GAIN_DEFAULT;

    pluginmanager_ = new thPluginManager(plugin_path);

    controllerHandler_ = new thMidiController();

    if (instance_ == NULL)
        instance_ = this;
}

thSynth::~thSynth (void)
{
    delete [] output_;

    /* The audio thread is expected to be stopped by now, so both queues can be
       emptied here without racing anything.
     *
     * The same thMidiChan can be reachable from up to three places at once: a
     * SET_CHANNEL command that was queued but never applied, guiChannels_, and
     * midiChannels_. Collect every candidate and delete each exactly once --
     * deleting per-slot double-freed any channel that was still in flight. */
    vector<thMidiChan *> doomed;

    thSynthCommand cmd;

    while (commands_.pop(cmd))
    {
        delete cmd.note;
        delete cmd.arg;

        if (cmd.channel)
            doomed.push_back(cmd.channel);
    }

    collectRetired();

    /* The channels were never destroyed here at all -- each one leaked its
       args, its notes and (now) its tree. */
    for (int i = 0; i < midiChannelCnt_; i++)
    {
        if (midiChannels_[i])
            doomed.push_back(midiChannels_[i]);

        if (guiChannels_[i])
            doomed.push_back(guiChannels_[i]);

        midiChannels_[i] = NULL;
        guiChannels_[i] = NULL;
    }

    sort(doomed.begin(), doomed.end());
    doomed.erase(unique(doomed.begin(), doomed.end()), doomed.end());

    for (vector<thMidiChan *>::iterator i = doomed.begin();
         i != doomed.end(); ++i)
    {
        delete *i;
    }

    DestroyMap(treelist_);
    free(midiChannels_);
    free(guiChannels_);
    midiChannels_ = NULL;
    guiChannels_ = NULL;

    delete controllerHandler_;
    delete pluginmanager_;

    if (instance_ == this)
        instance_ = NULL;
}

/* ------------------------------------------------------------------------
 * Command plumbing. See thSynthCommand.h for why this exists.
 * ------------------------------------------------------------------------ */

/* GUI thread. */
bool thSynth::postCommand (const thSynthCommand &cmd)
{
    if (commands_.push(cmd))
        return true;

    /* The audio thread is not draining -- either it is wedged or there is no
       audio backend running at all. Drop the command rather than block the GUI,
       and clean up whatever it was carrying so nothing leaks. */
    fprintf(stderr, "thSynth: command queue full, dropping command %d\n",
            (int)cmd.type);

    delete cmd.note;
    delete cmd.channel;
    delete cmd.arg;

    return false;
}

/* GUI thread. */
void thSynth::collectRetired (void)
{
    thRetired item;

    while (retired_.pop(item))
    {
        switch (item.kind)
        {
            case thRetired::NOTE:    delete item.note;    break;
            case thRetired::CHANNEL: delete item.channel; break;
            case thRetired::ARG:     delete item.arg;     break;
        }
    }
}

/* Audio thread. */
void thSynth::applyCommand (const thSynthCommand &cmd)
{
    thRetired item;

    if (cmd.chan < 0 || cmd.chan >= midiChannelCnt_)
    {
        /* Should not happen -- the GUI side range-checks -- but the payload
           still has to go somewhere. */
        if (cmd.note)    { item.kind = thRetired::NOTE;    item.note = cmd.note;
                           retired_.push(item); }
        if (cmd.channel) { item.kind = thRetired::CHANNEL; item.channel = cmd.channel;
                           retired_.push(item); }
        if (cmd.arg)     { item.kind = thRetired::ARG;     item.arg = cmd.arg;
                           retired_.push(item); }
        return;
    }

    thMidiChan *chan = midiChannels_[cmd.chan];

    switch (cmd.type)
    {
        case thSynthCommand::NOTE_ON:
            if (chan)
            {
                chan->insertNote(cmd.note, &retired_);
            }
            else if (cmd.note)
            {
                item.kind = thRetired::NOTE;
                item.note = cmd.note;
                if (!retired_.push(item))
                    delete cmd.note;
            }
            break;

        case thSynthCommand::NOTE_OFF:
            if (chan)
                chan->releaseNote(cmd.noteId);
            break;

        case thSynthCommand::ALL_NOTES_OFF:
            for (int i = 0; i < midiChannelCnt_; i++)
            {
                if (midiChannels_[i])
                    midiChannels_[i]->clearAll(&retired_);
            }
            break;

        case thSynthCommand::SET_CHANNEL:
            midiChannels_[cmd.chan] = cmd.channel;

            /* The old channel is now unreachable from this array, so it is
               safe for the GUI thread to destroy. */
            if (chan)
            {
                item.kind = thRetired::CHANNEL;
                item.channel = chan;
                if (!retired_.push(item))
                    delete chan;
            }
            break;

        case thSynthCommand::SET_CHAN_ARG:
            if (chan)
            {
                chan->setArg(cmd.arg, &retired_);
            }
            else if (cmd.arg)
            {
                item.kind = thRetired::ARG;
                item.arg = cmd.arg;
                if (!retired_.push(item))
                    delete cmd.arg;
            }
            break;
    }
}

/* Audio thread, at the top of process(). */
void thSynth::drainCommands (void)
{
    thSynthCommand cmd;

    while (commands_.pop(cmd))
        applyCommand(cmd);
}

void thSynth::removeChan (int channum)
{
    if ((channum < 0) || (channum >= midiChannelCnt_))
        return;

    synthMutex_.lock();
    collectRetired();

    if (guiChannels_[channum] != NULL)
    {
        thSynthCommand cmd;

        /* Queue the removal instead of deleting here: the callback may be
           inside this very channel. The audio thread hands it back through the
           retire queue once it is unreachable, and collectRetired() frees it.
           The delete used to be commented out entirely, so every removed
           channel leaked its notes, args and tree. */
        cmd.type = thSynthCommand::SET_CHANNEL;
        cmd.chan = channum;
        cmd.channel = NULL;

        /* Only forget the channel if the audio thread has actually been told
           to drop it. Clearing guiChannels_ first meant that a full queue left
           the GUI believing the channel was gone while the callback carried on
           playing it -- and with the GUI's only reference dropped, nothing was
           ever going to free it. The bookkeeping below has to go the same way,
           or the patch list and controller map disagree with reality. */
        if (postCommand(cmd))
        {
            guiChannels_[channum] = NULL;
            patchlist_[channum] = "";
            controllerHandler_->clearByDestChan(channum);
        }
    }

    synthMutex_.unlock();
}

/* Common tail for the loadTree() overloads.
 *
 * Historically the return value of YYPARSE() was thrown away and the tree was
 * resolved regardless. A .dsp with a syntax error, a missing `io' declaration,
 * or a node referencing a plugin that failed to load would then produce a tree
 * with a NULL ionode and an unbuilt node index, which the audio thread would
 * walk straight off the end of. Callers already handle a NULL return.
 *
 * `registerTree' decides ownership. The per-channel overload passes false and
 * hands the tree to the thMidiChan, which owns it outright; the two whole-synth
 * overloads pass true and the tree goes into treelist_, owned by thSynth.
 *
 * Trees used to *always* go into treelist_, keyed by the DSP's `name'
 * statement. Since every .dsp without a `name' parses as "newmod", they all
 * collided -- and thMidiChan::assignChanArgPointers caches raw thArg pointers
 * into the tree it is handed, so two channels sharing one tree silently
 * overwrote each other's chanarg pointers, and destroying either left the other
 * pointing at freed memory.
 *
 * Must be called with synthMutex_ held; parsetree is consumed either way.
 */
thSynthTree *thSynth::finishParse (const string &what, int parseResult,
                                   bool registerTree)
{
    thSynthTree *tree = parsetree;

    parsetree = NULL;

    if (tree == NULL)
    {
        return NULL;
    }

    if (parseResult != 0)
    {
        fprintf(stderr, "%s: parse failed, discarding\n", what.c_str());
        delete tree;
        return NULL;
    }

    if (tree->IONode() == NULL)
    {
        fprintf(stderr, "%s: DSP does not have a valid IO node!\n",
                what.c_str());
        delete tree;
        return NULL;
    }

    tree->buildArgMap(); /* build the index of args */
    tree->setPointers();
    tree->buildSynthTree();

    if (registerTree)
    {
        /* Still name-keyed, so still collision-prone -- but nothing owns these
           except thSynth itself, so a collision only leaks. */
        map<string, thSynthTree*>::iterator existing =
            treelist_.find(tree->name());

        if (existing != treelist_.end() && existing->second != tree)
        {
            delete existing->second;
        }

        treelist_[tree->name()] = tree;
    }

    return tree;
}

thSynthTree * thSynth::loadTree (const string &filename)
{
    struct stat dspinfo;

    if (stat(filename.c_str(), &dspinfo) < 0)
    {
        fprintf (stderr, "couldn't open %s: %s\n", filename.c_str(),
                 strerror(errno));
        return NULL;
    }
    else if (S_ISDIR(dspinfo.st_mode))
    {
        fprintf(stderr, "%s is a directory\n", filename.c_str());

#ifdef EISDIR
        errno = EISDIR; /* XXX */
#endif

        return NULL;
    }

    if ((yyin = fopen(filename.c_str(), "r")) == NULL) { /* ENOENT or smth */
        fprintf (stderr, "couldn't open %s: %s\n", filename.c_str(),
                 strerror(errno));
        return NULL;
    }

    synthMutex_.lock();

    /* XXX: do we re-allocate these everytime we read a new input file?? */
     /* these are used by the parser */
    parsetree = new thSynthTree("newmod", this);
    parsenode = new thNode("newnode", NULL);

    int parseResult = YYPARSE(this);

    fclose(yyin);
    yyin = NULL;

    delete parsenode;
    parsenode = NULL;

    /* No channel involved, so thSynth keeps this one. */
    thSynthTree *tree = finishParse(filename, parseResult, true);

    synthMutex_.unlock();

    return tree;
}

thSynthTree * thSynth::parseTree (const string &filename)
{
    struct stat dspinfo;

    if (stat(filename.c_str(), &dspinfo) < 0 || S_ISDIR(dspinfo.st_mode))
        return NULL;

    /* Opened into a local, and only handed to the parser once the mutex is
       held. Assigning the global yyin first left a window in which a second
       parse could overwrite it -- or close it out from under this one -- and
       taking a lock immediately afterwards does nothing about that: by then
       the damage is a store that already happened. The other parser globals
       (parsetree, parsenode) are set inside the lock for the same reason. */
    FILE *input = fopen(filename.c_str(), "r");

    if (input == NULL)
        return NULL;

    /* Same mutex as loadTree: the parser's globals are shared, so two parses
       at once would interleave. */
    synthMutex_.lock();

    yyin = input;

    parsetree = new thSynthTree("newmod", this);
    parsenode = new thNode("newnode", NULL);

    int parseResult = YYPARSE(this);

    fclose(yyin);
    yyin = NULL;

    delete parsenode;
    parsenode = NULL;

    thSynthTree *tree = finishParse(filename, parseResult, false);

    synthMutex_.unlock();

    return tree;
}

thSynthTree * thSynth::loadTree (FILE *input)
{
    if (!input)
        return NULL;

    synthMutex_.lock();
    
    yyin = input;

    /* XXX: do we re-allocate these everytime we read a new input file?? */
    /* these are used by the parser */
    parsetree = new thSynthTree("newmod", this);
    parsenode = new thNode("newnode", NULL);

    int parseResult = YYPARSE(this);

    delete parsenode;
    parsenode = NULL;

    thSynthTree *tree = finishParse("<stream>", parseResult, true);

    synthMutex_.unlock();

    return tree;
}

/* GUI thread. Takes ownership of `arg'. */
void thSynth::setChanArg (int channum, thArg *arg)
{
    if ((channum < 0) || (channum >= midiChannelCnt_) || arg == NULL)
    {
        delete arg;
        return;
    }

    synthMutex_.lock();
    collectRetired();

    if (!guiChannels_[channum])
    {
        synthMutex_.unlock();
        delete arg;
        return;
    }

    thArg *existing = guiChannels_[channum]->getArg(arg->name());

    /* Fast path: changing the value of an arg that is already a single float.
     *
     * thArg::setValue does not reallocate in that case, so it is one relaxed
     * atomic store into a buffer the audio thread is only reading -- safe to
     * do right here, and it has to be done right here, because callers read
     * their own writes straight back. gthPatchManager::newPatch saves the
     * amplitude, reloads the DSP, restores it with setChanArg and then
     * repopulates the slider table from the channel; queueing that made the
     * GUI show the pre-restore value. Patch loading does the same for every
     * `name value' line it parses.
     *
     * The metadata carried across is GUI-only state (widget type, range,
     * label); the audio thread never reads it. */
    if (existing != NULL
        && existing->type() == thArg::ARG_VALUE && existing->len() == 1
        && arg->type() == thArg::ARG_VALUE && arg->len() == 1)
    {
        existing->setWidgetType(arg->widgetType());
        existing->setMin(arg->min());
        existing->setMax(arg->max());

        if (!arg->label().empty())
            existing->setLabel(arg->label());
        if (!arg->units().empty())
            existing->setUnits(arg->units());

        existing->setValue((*arg)[0]);

        synthMutex_.unlock();

        delete arg;
        return;
    }

    /* Anything else really is a replacement -- a new arg, or one changing
       length -- and installing it deletes the arg it displaces while live note
       trees still point at that one until the channel re-resolves them. That
       has to happen on the audio thread. */
    thSynthCommand cmd;

    cmd.type = thSynthCommand::SET_CHAN_ARG;
    cmd.chan = channum;
    cmd.arg = arg;

    postCommand(cmd);

    synthMutex_.unlock();
}

/* GUI thread.
 *
 * The returned thArg is shared with the audio thread, which reads it every
 * window. Writing a single float through setValue() is safe (see thArg.cpp);
 * anything that reallocates has to go through setChanArg() instead.
 */
thArg *thSynth::getChanArg (int channum, const string &argname)
{
    if ((channum < 0) || (channum >= midiChannelCnt_))
    {
        return NULL;
    }

    thMidiChan *chan = guiChannels_[channum];

    if (!chan)
    {
        return NULL;
    }

    return chan->getArg(argname);
}

void thSynth::handleMidiController (unsigned char channel, unsigned int param,
                                    unsigned int value)
{
    controllerHandler_->handleMidi(channel, param, value);
}

void thSynth::newMidiControllerConnection (unsigned char channel,
                                        unsigned int param,
                                        thMidiControllerConnection *connection)
{
    controllerHandler_->newConnection(channel, param, connection);
}

thSynthTree * thSynth::loadTree (const string &filename, int channum, float amp)
{
    struct stat dspinfo;

    if (channum < 0)
    {
        fprintf(stderr, "thSynth::loadTree: negative channel %d\n", channum);
        return NULL;
    }

    if (stat(filename.c_str(), &dspinfo) < 0)
    {
        fprintf (stderr, "couldn't open %s: %s\n", filename.c_str(),
                 strerror(errno));
        return NULL;
    }
    else if (S_ISDIR(dspinfo.st_mode))
    {
        fprintf(stderr, "%s is a directory\n", filename.c_str());

#ifdef EISDIR
        errno = EISDIR; /* XXX */
#endif

        return NULL;
    }

     if ((yyin = fopen(filename.c_str(), "r")) == NULL) { /* ENOENT or smth */
         fprintf (stderr, "couldn't open %s: %s\n", filename.c_str(),
                  strerror(errno));
         return NULL;
    }

    synthMutex_.lock();
    collectRetired();

    /* XXX: do we re-allocate these everytime we read a new input file?? */
    /* these are used by the parser */
    parsetree = new thSynthTree("newmod", this);
    parsenode = new thNode("newnode", NULL);

    int parseResult = YYPARSE(this);

    fclose(yyin);
    yyin = NULL;

    delete parsenode;
    parsenode = NULL;

    /* registerTree false: the thMidiChan below takes ownership. */
    thSynthTree *tree = finishParse(filename, parseResult, false);

    if (tree == NULL)
    {
        synthMutex_.unlock();
        return NULL;
    }

    /* The array is a fixed TH_MIDI_CHANNELS slots and is never resized, so
       there is nothing to grow here any more. */
    if (channum >= midiChannelCnt_)
    {
        fprintf(stderr, "thSynth::loadTree: channel %d is beyond the %d "
                "available channels\n", channum, midiChannelCnt_);
        delete tree;
        synthMutex_.unlock();
        return NULL;
    }

    /* Build the replacement fully before publishing it, then queue the swap.
       Deleting the old channel here would free it under a callback that may be
       inside it; the audio thread hands it back once it is unreachable. */
    thMidiChan *newchan = new thMidiChan(tree, amp, windowlen_);

    thSynthCommand cmd;

    cmd.type = thSynthCommand::SET_CHANNEL;
    cmd.chan = channum;
    cmd.channel = newchan;

    /* Publish only once the swap is queued. Assigning first and then resetting
       to NULL on failure would drop the GUI's reference to whatever channel was
       already there, leaving it playing with nothing able to reach it. */
    if (!postCommand(cmd))
    {
        /* postCommand deleted newchan, which owns the tree; the previous
           channel is untouched and still current. */
        synthMutex_.unlock();
        return NULL;
    }

    guiChannels_[channum] = newchan;

    patchlist_[channum] = filename;

    /* make sure there are no midi controllers set up for this channel */
    controllerHandler_->clearByDestChan(channum);

    synthMutex_.unlock();

    return tree;
}

/* Make these voids return something and add error checking everywhere! */
void thSynth::listTrees (void)
{
    for (map<string, thSynthTree*>::const_iterator im = treelist_.begin(); 
         im != treelist_.end(); ++im) {
        printf("%s\n", im->first.c_str());
    }
}

/* GUI thread.
 *
 * Building the note is the expensive half -- it copy-constructs the channel's
 * whole synth tree -- and it stays here. Only the container insert crosses over
 * to the audio thread. Doing both here, while the callback walked notes_, is
 * what produced the static when two notes sounded together.
 */
bool thSynth::addNote (int channum, float note, float velocity)
{
    /* was `> midiChannelCnt_' -- midiChannels_[midiChannelCnt_] is one past
       the end of the array. */
    if ((channum < 0) || (channum >= midiChannelCnt_))
    {
        debug("thSynth::addNote: no such channel %d", channum);

        return false;
    }

    synthMutex_.lock();
    collectRetired();

    thMidiChan *chan = guiChannels_[channum];

    if (!chan)
    {
        debug("thSynth::addNote: no such channel %d", channum);
        synthMutex_.unlock();

        return false;
    }

    thMidiNote *newnote = chan->buildNote(note, velocity);

    if (newnote == NULL)
    {
        synthMutex_.unlock();
        return false;
    }

    thSynthCommand cmd;

    cmd.type = thSynthCommand::NOTE_ON;
    cmd.chan = channum;
    cmd.note = newnote;

    bool ok = postCommand(cmd);

    synthMutex_.unlock();

    return ok;
}

int thSynth::delNote (int channum, float note)
{
    /* was `> midiChannelCnt_' -- one past the end of the array. */
    if ((channum < 0) || (channum >= midiChannelCnt_))
        return 1;

    synthMutex_.lock();
    collectRetired();

    if (!guiChannels_[channum])
    {
        synthMutex_.unlock();
        return 1;
    }

    /* The sustain-pedal test moved to thMidiChan::releaseNote, on the audio
       thread: reading the pedal and poking the note's `trigger' arg from here
       meant writing into a note the callback was mixing. */
    thSynthCommand cmd;

    cmd.type = thSynthCommand::NOTE_OFF;
    cmd.chan = channum;
    cmd.noteId = (int)note;

    postCommand(cmd);

    synthMutex_.unlock();

    return 0;
}

void thSynth::clearAll (void)
{
    synthMutex_.lock();
    collectRetired();

    /* This used to walk `while (*c) (*c++)->clearAll()', relying on a NULL
       terminator that midiChannels_ does not have -- with every slot occupied
       it ran straight off the end of the array. */
    thSynthCommand cmd;

    cmd.type = thSynthCommand::ALL_NOTES_OFF;
    cmd.chan = 0;

    postCommand(cmd);

    synthMutex_.unlock();
}

/* Audio thread. */
void thSynth::process (void)
{
    int mixchannels, notechannels;
    thMidiChan *chan;
    float *chanoutput;

    /* Apply anything the GUI thread has queued. This is the only place
       midiChannels_ and everything below it is mutated, which is what makes
       the mutex the old code had commented out here unnecessary. */
    drainCommands();

    memset(output_, 0, channels_ * windowlen_ * sizeof(float));

    for (int i = 0; i < midiChannelCnt_; i++)
    {
        chan = midiChannels_[i];

        if (chan)
        {
            notechannels = chan->numChannels();
            mixchannels = notechannels;
            
            if (mixchannels > channels_) {
                mixchannels = channels_;
            }
            
            chan->process(&retired_);
            chanoutput = chan->output();

            if (chanoutput == NULL || mixchannels <= 0) {
                continue;
            }

            int bufferoffset = 0;
            int inneroffset, chanoffset;

            for (int j = 0; j < mixchannels; j++)
            {
                inneroffset = bufferoffset;
                chanoffset = j;
                for (int k = 0; k < windowlen_; k++)
                {
                    output_[inneroffset] += chanoutput[chanoffset];
                    inneroffset++;
                    chanoffset += mixchannels;
                }

                bufferoffset += windowlen_;
            }
        }
    }

    /* Master gain, then the limiter.
     *
     * Voices are summed above with no headroom management and they sum
     * coherently -- every envelope peaks together on the attack -- so the mix
     * routinely runs past full scale once more than one note sounds. Doing
     * this here rather than in each audio backend means every output path gets
     * it, and getOutput() hands back the signal that will actually be heard.
     * The clamps in the ALSA and JACK paths stay as a backstop for anything
     * the limiter cannot tame (a NaN out of a diverging DSP, say). */
    const float gain = masterGain();
    const int samples = channels_ * windowlen_;

    for (int i = 0; i < samples; i++)
        output_[i] = thSoftLimit(output_[i] * gain);
}

void thSynth::printChan(int chan)
{
    for (int i = 0; i < windowlen_; i++)
    {
        printf("-=- %f\n", output_[(i*channels_)+chan]);
    }
}

float *thSynth::getOutput (void) const
{
    /* output_ is allocated once in the constructor and never moved, so there is
       nothing to lock here -- the commented-out mutex was pointless. */
    return output_;
}

float *thSynth::getChanBuffer (int chan)
{
    return &output_[chan * windowlen_];
}

void thSynth::setWindowlen (int windowlen)
{
    /* XXX: fixme */
#if 0
    synthMutex_.lock();
    windowlen_ = windowlen;
    delete [] output_;
    output_ = new float[channels_*windowlen_];
    synthMutex_.unlock();
#endif
}
