/* $Id: thMidiChan.cpp,v 1.49 2003/06/01 18:47:48 ink Exp $ */

#include "think.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"
#include "thMidiChan.h"

thMidiChan::thMidiChan (thMod *mod, float amp, int windowlen)
{
	float *allocatedamp = new float[1];
	const thArg *chanarg = NULL;

	if(!mod) {
		fprintf(stderr, "thMidiChan::thMidiChan: NULL mod passed\n");
	}

	modnode = mod;
	windowlength = windowlen;
	allocatedamp[0] = amp;
	dirty = 1;

	args[string("amp")] = new thArg(string("amp"), allocatedamp, 1);

	chanarg = modnode->GetArg("channels");
	channels = (int)chanarg->argValues[0];

	output = new float[channels*windowlen];
	outputnamelen = strlen(OUTPUTPREFIX) + GetLen(channels);
}

thMidiChan::~thMidiChan (void)
{
	thArg::DestroyMap(args);
	thMidiNote::DestroyMap(notes);
	delete[] output;
}

thMidiNote *thMidiChan::AddNote (float note, float velocity)
{
	thMidiNote *midinote;
	int id = (int)note;
	map<int, thMidiNote*>::const_iterator i = notes.find(id);
	if(i == notes.end()) {
		midinote = new thMidiNote(modnode, note, velocity);
		notes[id] = midinote;
	}
	else midinote = i->second;
	return midinote;
}

void thMidiChan::DelNote (int note)
{
	map<int, thMidiNote*>::iterator i = notes.find(note);
	delete i->second;
	notes.erase(i);
}

void thMidiChan::Process (void)
{
	if (dirty) memset (output, 0, windowlength*channels*sizeof(float));
	dirty = 0;
	thMidiNote *data;
	thArg *arg, *amp, *play;
	thMod *mod;
	int i, j, index;
	int delnote = 0;

	string argname;

	for (map<int, thMidiNote*>::iterator iter = notes.begin(); iter != notes.end(); ++iter)
	{
		data = iter->second;
		dirty = 1;
		data->Process(windowlength);
		mod = data->GetMod();
		amp = args["amp"];
		play = mod->GetArg("play");

		for(i=0;i<channels;i++) {
			argname = OUTPUTPREFIX;
			argname += (char)(i+'0');
			arg = mod->GetArg(argname);
			for(j=0;j<windowlength;j++) {
				index = i+(j*channels);
				output[index] += (*arg)[j]*((*amp)[j]/MIDIVALMAX);
				if(play && (*play)[j] == 0) {
					delnote = 1;
				}
				/* output += channel output * (amplitude/amplitude maximum) */
				if(output[index] > TH_MAX) {  /* clipping! */
					output[index] = TH_MAX;
				}
				else if(output[index] < TH_MIN) {
					output[index] = TH_MIN;
				}
			}
		}
		
		if(delnote == 1) {
			delete data;
			notes.erase(iter);
		}
	}
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
