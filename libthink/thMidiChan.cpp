/* $Id: thMidiChan.cpp,v 1.35 2003/05/07 16:07:17 misha Exp $ */

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
	float *allocatedamp = new float[1];
	thArg *argstruct = new thArg("amp", allocatedamp, 1);
	const thArgValue *chanarg = NULL;

	modnode = mod;
	windowlength = windowlen;
	allocatedamp[0] = amp;

	args = new thBSTree(StringCompare);
	notes = new thBSTree(IntCompare);

	args->Insert((void *)strdup("amp"), (void *)argstruct);

	chanarg = modnode->GetArg(modnode->GetIONode()->GetName(), "channels");
	channels = (int)chanarg->argValues[0];

	output = new float[channels*windowlen];
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
	int *id = new int[1];

	id[0] = (int)note;
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
  memset (output, 0, windowlength*channels*sizeof(float));

  ProcessHelper(notes);
}

void thMidiChan::ProcessHelper (thBSTree *note)
{
	thMidiNote *data;
	thArgValue *arg, *amp, *play, *noteval;
	thMod *mod;
	char *argname = new char[outputnamelen];
	int i, j;
	int delnote = 0;

	if(!note) {
		return;
	}

	ProcessHelper(note->GetLeft());

	data = (thMidiNote *)note->GetData();
	if(data) { /* XXX We should not have to do this! */
		const char *ionodename = NULL;

		data->Process(windowlength);
		mod = data->GetMod();
		ionodename = mod->GetIONode()->GetName();
		amp = (thArgValue *)args->GetData((void *)"amp");
		play = (thArgValue *)mod->GetArg(ionodename, (const char*)"play");
	  
		for(i=0;i<channels;i++) {
			sprintf(argname, "%s%i", OUTPUTPREFIX, i);
			arg = (thArgValue *)mod->GetArg(ionodename, (const char*)argname);
			for(j=0;j<windowlength;j++) {
				output[i+(j*channels)] += (*arg)[j]*((*amp)[j]/MIDIVALMAX);
				if((*play)[i] == 0) {
					delnote = 1;
				}
				/* output += channel output * (amplitude/amplitude maximum) */
			}
		}
	  
		if(delnote == 1) {
			noteval = (thArgValue *)mod->GetArg(ionodename, 
												(const char*)"note");
			DelNote((int)(*noteval)[0]);
		}
	}

	ProcessHelper(note->GetRight());
}

static int RangeArray[] = {10, 100, 1000, 10000, 100000, 1000000, 10000000,
						   100000000, 1000000000};

static int RangeSize = sizeof(RangeArray)/sizeof(int);

int thMidiChan::GetLen (int num)
{
	num = abs(num);
	int i;

	for(i = 0; i < RangeSize; i++) {
		if(num < RangeArray[i]) {
			return i+1;
		}
	}

	return RangeSize+1;
}
