/* $Id: thMidiChan.cpp,v 1.17 2003/04/27 09:56:36 aaronl Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
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
#include "thUtils.h"

thMidiChan::thMidiChan (thMod *mod, float amp)
{
	float *allocatedamp = new float;
	modnode = mod;
	args = new thBSTree(StringCompare);
	notes = new thBSTree(IntCompare);
	allocatedamp[0] = amp;
	args->Insert((void *)thstrdup("amp"), (void *)allocatedamp);
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
	int *id = new int;

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
