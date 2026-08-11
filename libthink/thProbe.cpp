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

#include <string.h>

#include "think.h"

thProbe::thProbe (int chan, unsigned long chanSerial, int nodeId, int argIndex,
                  const string &nodeName, const string &argName,
                  unsigned int windowlen, unsigned int ringsamples)
    : chan_(chan), chanSerial_(chanSerial), nodeId_(nodeId),
      argIndex_(argIndex),
      nodeName_(nodeName), argName_(argName),
      windowlen_(windowlen < 1 ? 1 : windowlen), accum_(NULL),
      ring_(ringsamples), windows_(0)
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
}

/* Audio thread. */
void thProbe::accumulate (thSynthTree *tree)
{
    if (tree == NULL)
        return;

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
    for (unsigned int i = 0; i < windowlen_; i++)
        accum_[i] += (*arg)[i];
}

/* Audio thread. */
void thProbe::publish (void)
{
    ring_.write(accum_, windowlen_);

    /* Counted whether or not the ring took it, so that a consumer can tell a
       silent probe from one nothing is feeding. */
    windows_.fetch_add(1, std::memory_order_relaxed);
}
