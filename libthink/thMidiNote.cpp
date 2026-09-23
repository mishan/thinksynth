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

thMidiNote::thMidiNote (thSynthTree *tree, float note, float velocity,
                        float level, const float *aux)
    : synthTree_(*tree)
{
    synthTree_.buildSynthTree();
    channel_ = 0;
    start(note, velocity, level, aux);
}

/* GUI thread. See the header. */
bool thMidiNote::restart (const thSynthTree *tree, float note, float velocity,
                          float level, const float *aux)
{
    if (tree == NULL || !synthTree_.restore(*tree))
        return false;

    start(note, velocity, level, aux);

    return true;
}

/* What a new voice's io node is told, and what it starts out as. */
void thMidiNote::start (float note, float velocity, float level,
                        const float *aux)
{
    thNode *ionode = synthTree_.IONode();

    ionode->setArg("note", note);
    ionode->setArg("velocity", velocity);
    ionode->setArg("trigger", 1);

    /* Only where the graph reads one. The tree's parse made an arg for every
       `ionode->aux<N>' the file wrote, so a getArg that finds nothing is a
       graph that never asked -- and creating the arg in this copy would take
       an index the prototype does not have, which is the trouble finishParse
       declares `note' and `velocity' up front to avoid. */
    for (int i = 0; i < TH_NOTE_AUX; i++)
    {
        char name[16];

        snprintf(name, sizeof(name), AUXPREFIX "%d", i);

        if (ionode->getArg(name) != NULL)
            ionode->setArg(name, aux != NULL ? aux[i] : 0.0f);
    }

    note_ = note;
    level_ = level;
    noteid_ = (int)note;
    fadelen_ = faderemaining_ = 0;
}

thMidiNote::thMidiNote (thSynthTree *tree)
    : synthTree_(*tree)
{
    synthTree_.buildSynthTree();
    thNode *ionode = synthTree_.IONode();

    channel_ = 0;

    ionode->setArg("note", 0);     /* set these to 0, it may matter when */
    ionode->setArg("velocity", 0); /* the args are indexed as well */
    ionode->setArg("trigger", 0);

    note_ = 0;
    level_ = 1;
    noteid_ = 0;
    fadelen_ = faderemaining_ = 0;
}

thMidiNote::~thMidiNote ()
{
}

/* Audio thread. See the header. */
void thMidiNote::retune (float note)
{
    thNode *ionode = synthTree_.IONode();

    if (ionode == NULL)
        return;

    ionode->setArg("note", note);

    note_ = note;
    noteid_ = (int)note;
}

/* Audio thread. See the header. */
void thMidiNote::beginFade (int samples)
{
    /* Already going: a second steal in the same window must not hand the
       voice a fresh ramp and a longer life. */
    if (fadelen_ > 0)
        return;

    /* A fade of nothing is a cut, and a cut is what this exists to stop, so
       the floor is one sample -- at which the voice is simply gone after the
       window it is in, as it was before. */
    fadelen_ = faderemaining_ = (samples > 0) ? samples : 1;
}

/* Audio thread. See the header. */
float thMidiNote::fadeGain (int offset) const
{
    if (fadelen_ <= 0)
        return 1.0f;

    const int left = faderemaining_ - offset;

    if (left <= 0)
        return 0.0f;

    return (float)left / (float)fadelen_;
}

/* Audio thread. See the header. */
bool thMidiNote::advanceFade (int samples)
{
    if (fadelen_ <= 0)
        return false;

    faderemaining_ -= samples;

    return faderemaining_ <= 0;
}

void thMidiNote::process (int length)
{
    synthTree_.setActiveNodes();
    synthTree_.process(length);
}

void thMidiNote::setArg (const string &name, float value)
{
    thNode *ionode = synthTree_.IONode();
    ionode->setArg(name, value);
}

void thMidiNote::setArg (const string &name, const float *value, int len)
{
    thNode *ionode = synthTree_.IONode();
    ionode->setArg(name, value, len);
}
