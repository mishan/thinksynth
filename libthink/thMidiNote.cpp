/* $Id: thMidiNote.cpp,v 1.26 2003/05/30 00:55:42 aaronl Exp $ */

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

	//	args = new thBSTree(StringCompare);

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
  //	args = new thBSTree(StringCompare);
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
