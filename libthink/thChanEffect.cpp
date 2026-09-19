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

thChanEffect::thChanEffect (thSynthTree *tree, int channels, int windowlen,
                            int sideChan)
{
    tree_ = tree;
    channels_ = 0;
    scratch_ = NULL;
    sideChan_ = sideChan;

    for (int i = 0; i < TH_MAX_CHANNELS; i++)
    {
        inindex_[i] = -1;
        outindex_[i] = -1;
        sideindex_[i] = -1;
    }

    if (tree_ == NULL || tree_->IONode() == NULL || windowlen <= 0)
    {
        return;
    }

    if (channels < 1)
        channels = 1;

    if (channels > TH_MAX_CHANNELS)
        channels = TH_MAX_CHANNELS;

    /* What the graph says it carries, against what the channel has. The
       smaller: an effect wired for one channel on a stereo instrument is a
       mono effect, not a broken one. */
    {
        const thArg *decl = tree_->IONode()->getArg("channels");
        int declared = channels;

        if (decl && decl->values())
            declared = (int)decl->values()[0];

        channels_ = (declared < channels) ? declared : channels;

        if (channels_ < 0)
            channels_ = 0;
    }

    /* One window per channel, planar: the whole of the effect's output is
       read into this and checked before any of it is written back, which is
       what makes "the dry signal goes out instead" true rather than true of
       whichever channels came before the bad one. */
    if (channels_ > 0)
        scratch_ = new float[(size_t)channels_ * windowlen]();

    copyChanArgs();
    assignChanArgPointers();
    indexIOArgs(windowlen);
}

thChanEffect::~thChanEffect ()
{
    DestroyMap(args_);

    delete tree_;
    tree_ = NULL;

    delete[] scratch_;
    scratch_ = NULL;
}

/* The effect's declared chanargs, copied out of the tree into a map this
   object owns -- thMidiChan::copyChanArgs, against the effect's own map. The
   copy is what lets a value be changed while the graph is playing: the node
   args point at these, not at the tree's. */
void thChanEffect::copyChanArgs (void)
{
    if (tree_ == NULL)
    {
        return;
    }

    const thArgMap &source = tree_->chanArgs();

    for (thArgMap::const_iterator i = source.begin(); i != source.end(); ++i)
    {
        if (i->second == NULL)
            continue;

        thArg *copy = new thArg(i->second);
        thArgMap::iterator existing = args_.find(i->first);

        if (existing != args_.end())
        {
            delete existing->second;
            existing->second = copy;
        }
        else
        {
            args_[i->first] = copy;
        }
    }
}

/* Every ARG_CHANNEL in the graph pointed at this object's copy of the arg it
   names -- thMidiChan::assignChanArgPointers, against the effect's own map.
   An undeclared name is said rather than swallowed, for the same reason. */
void thChanEffect::assignChanArgPointers (void)
{
    if (tree_ == NULL)
    {
        return;
    }

    const thSynthTree::NodeMap &nodes = tree_->nodes();

    for (thSynthTree::NodeMap::const_iterator i = nodes.begin();
         i != nodes.end(); ++i)
    {
        thNode *node = i->second;

        if (node == NULL)
            continue;

        const thArgMap &args = node->args();

        for (thArgMap::const_iterator j = args.begin(); j != args.end(); ++j)
        {
            thArg *arg = j->second;

            if (arg == NULL || arg->type() != thArg::ARG_CHANNEL)
                continue;

            thArg *target = getArg(arg->argPtrName());

            if (target == NULL)
            {
                fprintf(stderr, "thChanEffect: node '%s' arg '%s' references "
                        "undeclared chanarg '@%s'\n", node->name().c_str(),
                        arg->name().c_str(), arg->argPtrName().c_str());
            }

            arg->setArgPtr(target);
        }
    }
}

/* Where in<N> and out<N> live, and the buffers in<N> will be written into.
 *
 * in<N> is *made* a plain value arg holding a window, whatever the file said
 * about it. The engine writes these -- `in0 = 0' is the honest way for a file
 * to declare one, and a file that wires something into in0 has written
 * something the engine is about to overwrite. Doing it here rather than on
 * the first window is what keeps the allocation off the audio thread.
 */
void thChanEffect::indexIOArgs (int windowlen)
{
    thNode *io = tree_ ? tree_->IONode() : NULL;

    if (io == NULL)
    {
        return;
    }

    /* One digit: TH_MAX_CHANNELS is ten for exactly this reason. */
    for (int i = 0; i < channels_ && i < TH_MAX_CHANNELS; i++)
    {
        string name = INPUTPREFIX;

        name += (char)(i + '0');

        thArg *arg = io->setArg(name, 0);

        if (arg == NULL)
            continue;

        arg->allocate(windowlen);
        inindex_[i] = arg->index();
    }

    for (int i = 0; i < channels_ && i < TH_MAX_CHANNELS; i++)
    {
        string name = OUTPUTPREFIX;

        name += (char)(i + '0');

        thArg *arg = io->getArg(name);

        /* Created where the file did not write one, so that the read below is
           a read of zeros rather than a lookup that allocates. */
        if (arg == NULL)
            arg = io->setArg(name, 0);

        if (arg)
            outindex_[i] = arg->index();
    }

    /* side<N>, for the graphs that asked. Unlike in<N> these are not
       invented where the file is silent: in<N> is what makes a .dsp an
       effect at all and every effect has one, while a side is the unusual
       case, and creating a window-sized buffer on every echo in the tree to
       hold silence nothing reads is a cost with nothing on the other side of
       it.

       A file that declares side0 and is given no side channel reads zeros,
       which is the same thing a vocoder with no carrier should sound like. */
    for (int i = 0; i < channels_ && i < TH_MAX_CHANNELS; i++)
    {
        string name = SIDEPREFIX;

        name += (char)(i + '0');

        if (io->getArg(name) == NULL)
            continue;

        thArg *arg = io->setArg(name, 0);

        if (arg == NULL)
            continue;

        arg->allocate(windowlen);
        sideindex_[i] = arg->index();
    }
}

/* Audio thread. */
bool thChanEffect::process (float *buf, int channels, int windowlen,
                            const float *side, int sidechannels)
{
    return run(buf, channels, windowlen, channels, 1, side, sidechannels);
}

/* Audio thread. */
bool thChanEffect::processPlanar (float *buf, int channels, int windowlen)
{
    /* No side on the mix: there is no second channel to name once every
       channel is already in `buf'. */
    return run(buf, channels, windowlen, 1, windowlen, NULL, 0);
}

/* Audio thread. */
bool thChanEffect::run (float *buf, int channels, int windowlen, int step,
                        int hop, const float *side, int sidechannels)
{
    if (tree_ == NULL || buf == NULL || scratch_ == NULL || channels_ <= 0 ||
        channels <= 0 || windowlen <= 0)
    {
        return true;
    }

    /* In, one buffer per channel, which is what a graph wants and what
       neither caller has: thMidiChan mixes its voices interleaved by the
       channel count and thSynth sums its channels one whole window after
       another. */
    for (int c = 0; c < channels_ && c < channels; c++)
    {
        thArg *arg = tree_->resolveIOArg(inindex_[c]);

        if (arg == NULL || arg->values() == NULL ||
            (int)arg->len() != windowlen)
            continue;

        float *dst = arg->values();

        for (int j = 0; j < windowlen; j++)
            dst[j] = buf[j * step + c * hop];
    }

    /* And the other channel, where the graph asked for one.
     *
     * Three cases, and the middle one is the one worth knowing. A piece
     * that named a channel gets that channel; a piece that named *none*
     * gets this one, so that a graph reading side0 always has a signal
     * there -- a compressor keyed from `side' is an ordinary compressor
     * until somebody names a kick, which is the same rule dyn::compressor
     * itself follows for an unwired `side', one level down. And a side
     * naming a channel with nothing on it is silence, because that is what
     * that channel is putting out.
     *
     * Zeroed rather than left alone in that last case: the side channel can
     * go away -- an instrument unloaded, a piece closed -- and a buffer
     * nobody writes any more is the last window of a kick repeating under a
     * compressor for ever. */
    for (int c = 0; c < channels_ && c < TH_MAX_CHANNELS; c++)
    {
        if (sideindex_[c] < 0)
            continue;

        thArg *arg = tree_->resolveIOArg(sideindex_[c]);

        if (arg == NULL || arg->values() == NULL ||
            (int)arg->len() != windowlen)
            continue;

        float *dst = arg->values();

        if (sideChan_ < 0)
        {
            /* Nobody named: this channel, which is what `buf' holds. */
            const int from = (c < channels) ? c : channels - 1;

            for (int j = 0; j < windowlen; j++)
                dst[j] = buf[j * step + from * hop];

            continue;
        }

        if (side == NULL || sidechannels <= 0)
        {
            memset(dst, 0, (size_t)windowlen * sizeof(float));
            continue;
        }

        /* A mono side into a stereo effect gives both sides the one signal,
           rather than silence in side1: what a side carries is a key or a
           carrier, and half of one is not a thing anybody wants. */
        const int from = (c < sidechannels) ? c : sidechannels - 1;

        for (int j = 0; j < windowlen; j++)
            dst[j] = side[j * sidechannels + from];
    }

    /* Every node, not setActiveNodes(): an effect is entitled to be nothing
       but a dist::clip, and arithmetic is PASSIVE. See markAllNodes. */
    tree_->markAllNodes();
    tree_->process((unsigned int)windowlen);

    /* Out, into the scratch, and not a sample of it back into `buf' until
       all of it has been looked at. A NaN written back here is every voice on
       the channel for as long as the effect is loaded -- the voices are
       already summed by now, so there is no bad one to drop. */
    const int carried = (channels_ < channels) ? channels_ : channels;

    for (int c = 0; c < carried; c++)
    {
        thArg *arg = tree_->resolveIOArg(outindex_[c]);
        float *dst = scratch_ + (size_t)c * windowlen;

        if (arg == NULL)
        {
            memset(dst, 0, (size_t)windowlen * sizeof(float));
            continue;
        }

        arg->getBuffer(dst, windowlen);

        for (int j = 0; j < windowlen; j++)
            if (!thIsFinite(dst[j]))
                return false;
    }

    for (int c = 0; c < carried; c++)
    {
        const float *src = scratch_ + (size_t)c * windowlen;

        for (int j = 0; j < windowlen; j++)
            buf[j * step + c * hop] = src[j];
    }

    return true;
}
