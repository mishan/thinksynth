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

thMidiNote::thMidiNote (thSynthTree *tree, float note, float velocity)
    : synthTree_(*tree)
{
    synthTree_.buildSynthTree();
    thNode *ionode = synthTree_.IONode();

    ionode->setArg("note", note);
    ionode->setArg("velocity", velocity);
    ionode->setArg("trigger", 1);

    noteid_ = (int)note;
}

thMidiNote::thMidiNote (thSynthTree *tree)
    : synthTree_(*tree)
{
    synthTree_.buildSynthTree();
    thNode *ionode = synthTree_.IONode();

    ionode->setArg("note", 0);     /* set these to 0, it may matter when */
    ionode->setArg("velocity", 0); /* the args are indexed as well */
    ionode->setArg("trigger", 0);

    noteid_ = 0;
}

thMidiNote::~thMidiNote ()
{
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

