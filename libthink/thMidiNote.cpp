/* $Id: thMidiNote.cpp,v 1.30 2004/02/18 23:41:16 ink Exp $ */

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

