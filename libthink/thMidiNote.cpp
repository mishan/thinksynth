/* $Id: thMidiNote.cpp,v 1.25 2003/05/29 03:11:03 ink Exp $ */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thList.h"
#include "thBSTree.h"
#include "thArg.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"

thMidiNote::thMidiNote (thMod *mod, float note, float velocity)
{
	float *notep = new float, *velocityp = new float;

	//	args = new thBSTree(StringCompare);

	modnode = new thMod(mod);
	modnode->BuildSynthTree();
	ionode = modnode->GetIONode();

	*notep = note, *velocityp = velocity;

	ionode->SetArg("note", notep, 1);
	ionode->SetArg("velocity", velocityp, 1);

	noteid = (int)note;
}

thMidiNote::thMidiNote (thMod *mod)
{
  //	args = new thBSTree(StringCompare);

	modnode = new thMod(mod);
	modnode->BuildSynthTree();
	ionode = modnode->GetIONode();
}

thMidiNote::~thMidiNote ()
{
  	delete modnode;
}

void thMidiNote::Process (int length)
{
  modnode->SetActiveNodes();
  modnode->Process(length);
}
