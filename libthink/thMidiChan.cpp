/* $Id: thMidiChan.cpp,v 1.55 2004/03/26 08:11:29 misha Exp $ */

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
	DestroyMap(args);
	DestroyMap(notes);
	delete[] output;
}

void thMidiChan::SetArg (thArg *arg)
{
	thArg *oldArg = args[arg->GetArgName()];

	if(oldArg)
	{
		delete oldArg;
	}

	args[arg->GetArgName()] = arg;
}

thMidiNote *thMidiChan::AddNote (float note, float velocity)
{
	thMidiNote *midinote;
	int id = (int)note;
	map<int, thMidiNote*>::const_iterator i = notes.find(id);
/*	if(i == notes.end()) {  
these lines stopped new notes while old ones were still finishing.
maybe we should keep track of these better */
		midinote = new thMidiNote(modnode, note, velocity);
		notes[id] = midinote;
/*	}
	else midinote = i->second; */
	return midinote;
}

void thMidiChan::DelNote (int note)
{
	map<int, thMidiNote*>::iterator i = notes.find(note);
	delete i->second;
	notes.erase(i);
}

thMidiNote *thMidiChan::GetNote (int note)
{
	map<int, thMidiNote*>::iterator i = notes.find(note);

	if(i != notes.end()) {
		return i->second;
	}

	return NULL;
}

int thMidiChan::SetNoteArg (int note, char *name, float *value, int len)
{
	map<int, thMidiNote*>::iterator i = notes.find(note);

	if(i != notes.end()) {
		i->second->SetArg(name, value, len);
		return 1;
	}
	return 0;
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
			}
		}
		
		if(play && (*play)[j] == 0) {
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
