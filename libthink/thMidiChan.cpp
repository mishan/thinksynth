/* $Id: thMidiChan.cpp,v 1.20 2003/04/28 22:32:48 ink Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

#include "thList.h"
#include "thBSTree.h"
#include "thArg.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"
#include "thMidiChan.h"

thMidiChan::thMidiChan (thMod *mod, float amp, int windowlen)
{
	float *allocatedamp = (float *)malloc(sizeof(float));
	modnode = mod;
	args = new thBSTree(StringCompare);
	notes = new thBSTree(IntCompare);
	allocatedamp[0] = amp;
	args->Insert((void *)strdup("amp"), (void *)allocatedamp);

	channels = (int)((thArgValue *)mod->GetArg(((thNode *)modnode->GetIONode())->GetName(), "channels"))->argValues[0];
	output = new float[channels][windowlen];
	outputnamelen = strlen(OUTPUTPREFIX) + GetLen(channels);
}

thMidiChan::~thMidiChan ()
{
	delete modnode;
	delete args;
	delete notes;
	delete output;
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

void thMidiChan::Process (void)
{
}

int thMidiChan::GetLen (int num)
{
  int retval = 1;

  while(num >= 10) {
	num /= 10;
	retval++;
  }

  return retval;
}
