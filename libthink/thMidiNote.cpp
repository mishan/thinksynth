/* $Id: thMidiNote.cpp,v 1.32 2004/08/16 09:34:48 misha Exp $ */
/*
 * Copyright (C) 2004 Metaphonic Labs
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

thMidiNote::thMidiNote (thMod *mod, float note, float velocity)
	: modnode(*mod)
{
	float *notep = new float[1], *velocityp = new float[1], *triggerp = new float[1];

	modnode.BuildSynthTree();
	thNode *ionode = modnode.GetIONode();

	*notep = note, *velocityp = velocity, *triggerp = 1;

	ionode->SetArg("note", notep, 1);
	ionode->SetArg("velocity", velocityp, 1);
	ionode->SetArg("trigger", triggerp, 1);

	noteid = (int)note;
}

thMidiNote::thMidiNote (thMod *mod)
	: modnode (*mod)
{
	modnode.BuildSynthTree();
	thNode *ionode = modnode.GetIONode();

	ionode->SetArg("note", 0, 1);  /* set these to 0, it may matter when */
	ionode->SetArg("velocity", 0, 1); /* the args are indexed as well */
	ionode->SetArg("trigger", 0, 1);
}

thMidiNote::~thMidiNote ()
{
}

void thMidiNote::Process (int length)
{
	modnode.SetActiveNodes();
	modnode.Process(length);
}

void thMidiNote::SetArg (char *name, float *value, int len)
{
	thNode *ionode = modnode.GetIONode();
	ionode->SetArg(name, value, len);
}

