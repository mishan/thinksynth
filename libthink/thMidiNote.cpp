/* $Id: thMidiNote.cpp,v 1.27 2003/06/03 22:14:07 aaronl Exp $ */

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
	float *notep = new float[1], *velocityp = new float[1];

	modnode.BuildSynthTree();
	thNode *ionode = modnode.GetIONode();

	*notep = note, *velocityp = velocity;

	ionode->SetArg("note", notep, 1);
	ionode->SetArg("velocity", velocityp, 1);

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
