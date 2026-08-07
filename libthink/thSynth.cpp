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
#include <pthread.h>

#include "think.h"
#include "parser.h"

thSynth *thSynth::instance_ = NULL;

thSynth::thSynth (int windowlen, int samples)
{
    synthMutex_ = new pthread_mutex_t;
    pthread_mutex_init(synthMutex_, NULL);

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

    /* default path */
    pluginmanager_ = new thPluginManager(PLUGIN_PATH);

    controllerHandler_ = new thMidiController();

    if (instance_ == NULL)
        instance_ = this;
}

thSynth::thSynth (const string &plugin_path, int windowlen, int samples)
{
    synthMutex_ = new pthread_mutex_t;
    pthread_mutex_init(synthMutex_, NULL);

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

    pluginmanager_ = new thPluginManager(plugin_path);

    controllerHandler_ = new thMidiController();

    if (instance_ == NULL)
        instance_ = this;
}

thSynth::~thSynth (void)
{
    delete [] output_;

    /* The channels were never destroyed here at all -- each one leaked its
       args, its notes and (now) its tree. */
    for (int i = 0; i < midiChannelCnt_; i++)
    {
        delete midiChannels_[i];
        midiChannels_[i] = NULL;
    }

    DestroyMap(treelist_);
    free(midiChannels_);
    midiChannels_ = NULL;

    delete controllerHandler_;
    delete pluginmanager_;

    pthread_mutex_destroy(synthMutex_);
    delete synthMutex_;

    if (instance_ == this)
        instance_ = NULL;
}

void thSynth::removeChan (int channum)
{
    if ((channum < 0) || (channum >= midiChannelCnt_))
        return;

    pthread_mutex_lock(synthMutex_);

    thMidiChan *chan = midiChannels_[channum];

    /* Clear the slot before destroying, not after: process() walks this array.
       The delete used to be commented out entirely, so every removed channel
       leaked its notes, args and tree. */
    midiChannels_[channum] = NULL;

    pthread_mutex_unlock(synthMutex_);

    if (chan)
    {
        delete chan;
        patchlist_[channum] = "";
        controllerHandler_->clearByDestChan(channum);
    }
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

    pthread_mutex_lock(synthMutex_);

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

    pthread_mutex_unlock(synthMutex_);

    return tree;
}

thSynthTree * thSynth::loadTree (FILE *input)
{
    if (!input)
        return NULL;

    pthread_mutex_lock(synthMutex_);
    
    yyin = input;

    /* XXX: do we re-allocate these everytime we read a new input file?? */
    /* these are used by the parser */
    parsetree = new thSynthTree("newmod", this);
    parsenode = new thNode("newnode", NULL);

    int parseResult = YYPARSE(this);

    delete parsenode;
    parsenode = NULL;

    thSynthTree *tree = finishParse("<stream>", parseResult, true);

    pthread_mutex_unlock(synthMutex_);

    return tree;
}

void thSynth::setChanArg (int channum, thArg *arg)
{
    if ((channum < 0) || (channum >= midiChannelCnt_) || arg == NULL)
    {
        delete arg;
        return;
    }

    pthread_mutex_lock(synthMutex_);

    thMidiChan *chan = midiChannels_[channum];

    if (chan)
        chan->setArg(arg);
    else
        delete arg;

    pthread_mutex_unlock(synthMutex_);
}

thArg *thSynth::getChanArg (int channum, const string &argname)
{
    if ((channum < 0) || (channum >= midiChannelCnt_))
    {
        return NULL;
    }

    thMidiChan *chan = midiChannels_[channum];

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

    pthread_mutex_lock(synthMutex_);

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
        pthread_mutex_unlock(synthMutex_);
        return NULL;
    }

    /* The array is a fixed TH_MIDI_CHANNELS slots and is never resized, so
       there is nothing to grow here any more. */
    if (channum >= midiChannelCnt_)
    {
        fprintf(stderr, "thSynth::loadTree: channel %d is beyond the %d "
                "available channels\n", channum, midiChannelCnt_);
        delete tree;
        pthread_mutex_unlock(synthMutex_);
        return NULL;
    }

    if (midiChannels_[channum] != NULL)
    {
        delete midiChannels_[channum];
    }

    midiChannels_[channum] = new thMidiChan(tree, amp, windowlen_);

    patchlist_[channum] = filename;

    /* make sure there are no midi controllers set up for this channel */
    controllerHandler_->clearByDestChan(channum);

    pthread_mutex_unlock(synthMutex_);

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

thMidiNote *thSynth::addNote (int channum, float note,
                              float velocity)
{
    /* was `> midiChannelCnt_' -- midiChannels_[midiChannelCnt_] is one past
       the end of the array. */
    if ((channum < 0) || (channum >= midiChannelCnt_))
    {
        debug("thSynth::addNote: no such channel %d", channum);

        return NULL;
    }

    thMidiChan *chan = midiChannels_[channum];

    if (!chan)
    {
        debug("thSynth::addNote: no such channel %d", channum);

        return NULL;
    }

    pthread_mutex_lock(synthMutex_);

    thMidiNote *newnote = chan->addNote(note, velocity);

    pthread_mutex_unlock(synthMutex_);

    return newnote;
}

int thSynth::delNote (int channum, float note)
{
    /* was `> midiChannelCnt_' -- one past the end of the array. */
    if ((channum < 0) || (channum >= midiChannelCnt_))
        return 1;

    thMidiChan *chan = midiChannels_[channum];

    if (!chan)
        return 1;

    thArg *pedal = chan->sustainPedal();

    if (pedal == NULL)
        return 1;

    int sustain = (int)(*pedal)[0];

    pthread_mutex_lock(synthMutex_);

    chan->setNoteArg((int)note, "trigger", sustain ? 2 : 0);

    pthread_mutex_unlock(synthMutex_);

    return 0;
}

void thSynth::clearAll (void)
{
    pthread_mutex_lock(synthMutex_);

    /* This used to walk `while (*c) (*c++)->clearAll()', relying on a NULL
       terminator that midiChannels_ does not have -- with every slot occupied
       it ran straight off the end of the array. */
    for (int i = 0; i < midiChannelCnt_; i++)
    {
        if (midiChannels_[i])
            midiChannels_[i]->clearAll();
    }

    pthread_mutex_unlock(synthMutex_);
}

void thSynth::process (void)
{
    int mixchannels, notechannels;
    thMidiChan *chan;
    float *chanoutput;

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
            
            chan->process();
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
    pthread_mutex_lock(synthMutex_);
    windowlen_ = windowlen;
    delete [] output_;
    output_ = new float[channels_*windowlen_];
    pthread_mutex_unlock(synthMutex_);
#endif
}
