/* $Id: thMidiChan.cpp,v 1.18 2003/04/27 10:17:12 aaronl Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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
#include "thArg.h"
#include "thMidiChan.h"

thMidiChan::thMidiChan (thMod *mod, float amp)
{
	float *allocatedamp = (float *)malloc(sizeof(float));
	modnode = mod;
	args = new thBSTree(StringCompare);
	notes = new thBSTree(IntCompare);
	allocatedamp[0] = amp;
	args->Insert((void *)strdup("amp"), (void *)allocatedamp);
}

thMidiChan::~thMidiChan ()
{
	delete modnode;
	delete args;
	delete notes;
}

thMidiNote *thMidiChan::AddNote (float note, float velocity)
{
	thMidiNote *midinote = new thMidiNote(modnode, note, velocity);
	int *id = (int *)malloc(sizeof(int));

	*id = (int)note;
	notes->Insert(id, midinote);
	return midinote;
}

void thMidiChan::DelNote (int note)
{
	thBSTree *node = notes->Find(&note);

	delete (thMidiNote *)node->GetData();
	notes->Remove(&note);
}
