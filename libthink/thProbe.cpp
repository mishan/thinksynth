/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#include <string.h>

#include <algorithm>

#include "think.h"

thProbe::thProbe (int chan, unsigned long chanSerial, int nodeId, int argIndex,
                  const string &nodeName, const string &argName,
                  unsigned int windowlen, unsigned int ringsamples,
                  Mode mode)
    : chan_(chan), chanSerial_(chanSerial), nodeId_(nodeId),
      argIndex_(argIndex),
      nodeName_(nodeName), argName_(argName), mode_(mode),
      windowlen_(windowlen < 1 ? 1 : windowlen), accum_(NULL),
      voices_(0), compiled_(0), newest_(0), seq_(0), last_(0), lo_(0), hi_(0),
      heard_(0), skipped_(0),
      ring_(mode == NEWEST ? 0 : ringsamples), windows_(0)
{
    accum_ = new float[windowlen_]();
}

thProbe::~thProbe (void)
{
    delete [] accum_;
}

unsigned long thProbe::windows (void) const
{
    return windows_.load(std::memory_order_relaxed);
}

/* Audio thread. */
void thProbe::beginWindow (void)
{
    memset(accum_, 0, windowlen_ * sizeof(float));
    voices_ = 0;
    compiled_ = 0;
    newest_ = 0;
}

/* Audio thread. */
void thProbe::accumulate (thSynthTree *tree, unsigned long born)
{
    if (tree == NULL)
        return;

    /* A voice running its compiled graph writes none of the args read here,
       so it would show whatever they held when the graph was parsed. Voices
       started after the arm run on their nodes (thSynth::armProbe). */
    if (tree->compiled())
    {
        compiled_++;
        return;
    }

    /* Bounds-checked: ids come out of parsed .dsp files and out of a GUI that
       may have resolved them against a tree this channel no longer plays. */
    thNode *node = tree->nodeAt(nodeId_);

    if (node == NULL)
        return;

    thArg *arg = node->getArg(argIndex_);

    if (arg == NULL)
        return;

    /* operator[] is what makes a scalar arg work here without a special case:
       length 0 reads as zero, length 1 as a constant across the window, and
       anything shorter than the window repeats. 79 of the 126 allocate() calls
       across the plugins ask for windowlen and the rest do not, so probing a
       filter's `last' or an envelope's position has to mean something rather
       than reading off the end. */
    if (mode_ == NEWEST)
    {
        if (voices_++ > 0 && born < newest_)
            return;

        newest_ = born;

        for (unsigned int i = 0; i < windowlen_; i++)
            accum_[i] = (*arg)[i];

        return;
    }

    for (unsigned int i = 0; i < windowlen_; i++)
        accum_[i] += (*arg)[i];
}

/* Audio thread. */
void thProbe::publish (void)
{
    if (mode_ == NEWEST)
    {
        float lo = accum_[0], hi = accum_[0];

        for (unsigned int i = 1; i < windowlen_; i++)
        {
            lo = std::min(lo, accum_[i]);
            hi = std::max(hi, accum_[i]);
        }

        const unsigned long s = seq_.load(std::memory_order_relaxed);

        seq_.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        last_.store(accum_[windowlen_ - 1], std::memory_order_relaxed);
        lo_.store(lo, std::memory_order_relaxed);
        hi_.store(hi, std::memory_order_relaxed);
        heard_.store(voices_, std::memory_order_relaxed);
        skipped_.store(compiled_, std::memory_order_relaxed);
        seq_.store(s + 2, std::memory_order_release);
    }
    else
        ring_.write(accum_, windowlen_);

    /* Counted whether or not the ring took it, so that a consumer can tell a
       silent probe from one nothing is feeding. */
    windows_.fetch_add(1, std::memory_order_relaxed);
}

/* GUI thread. */
bool thProbe::reading (Reading &r) const
{
    const unsigned long before = seq_.load(std::memory_order_acquire);

    if (before == 0 || (before & 1))
        return false;

    r.last = last_.load(std::memory_order_relaxed);
    r.lo = lo_.load(std::memory_order_relaxed);
    r.hi = hi_.load(std::memory_order_relaxed);
    r.voices = heard_.load(std::memory_order_relaxed);
    r.compiled = skipped_.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);

    return seq_.load(std::memory_order_relaxed) == before;
}
