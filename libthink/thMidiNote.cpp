/* $Id: thMidiNote.cpp,v 1.29 2004/01/26 00:09:42 ink Exp $ */

#include "think.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thArg.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"

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

