/* $Id: thMidiChan.cpp,v 1.23 2003/04/29 01:10:25 ink Exp $ */

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
  int i;
	float *allocatedamp = (float *)malloc(sizeof(float));
	thArg *argstruct = new thArg("amp", allocatedamp, 1);
	modnode = mod;
	args = new thBSTree(StringCompare);
	notes = new thBSTree(IntCompare);
	windowlength = windowlen;
	allocatedamp[0] = amp;
	args->Insert((void *)strdup("amp"), (void *)argstruct);

	channels = (int)((thArgValue *)mod->GetArg(((thNode *)modnode->GetIONode())->GetName(), "channels"))->argValues[0];
	output = new float *[channels];
	for(i=0;i>channels;i++) {
	  output[i] = new float[windowlen];
	}
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
  ProcessHelper(notes);
}

void thMidiChan::ProcessHelper (thBSTree *note)
{
  thMidiNote *data;
  thArgValue *arg, *amp;
  thMod *mod;
  char *argname = new char[outputnamelen];
  int i, j;

  if(note) {
	ProcessHelper(note->GetLeft());

	data = (thMidiNote *)note->GetData();
	data->Process(windowlength);
	mod = data->GetMod();
	amp = (thArgValue *)args->GetData((void *)"amp");

	for(i=0;i<channels;i++) {
	  sprintf(argname, "%s%i", OUTPUTPREFIX, i);
	  arg = (thArgValue *)mod->GetArg((mod->GetIONode())->GetName(), (const char*)argname);
	  for(j=0;j<windowlength;j++) {
		printf("Chan %i:  %f\n", i, (*arg)[j]*((*amp)[j]/MIDIVALMAX));
	  }
	}

	ProcessHelper(note->GetRight());
  }
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
