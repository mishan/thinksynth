/* $Id$ */
/*
 * Copyright (C) 2004 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "think.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

thMidiChan::thMidiChan (thMod *mod, float amp, int windowlen)
{
	float *allocatedamp = new float[1];
	const thArg *chanarg = NULL;

	if(!mod) {
		fprintf(stderr, "thMidiChan::thMidiChan: NULL mod passed\n");
	}

	CopyChanArgs(mod);
	AssignChanArgPointers(mod);

	modnode = mod;
	windowlength = windowlen;
	allocatedamp[0] = amp;
	dirty = 1;

	args[string("amp")] = new thArg(string("amp"), allocatedamp, 1);

	chanarg = modnode->GetArg("channels");
	channels = (int)chanarg->values_[0];

	output = new float[channels*windowlen];
	outputnamelen = strlen(OUTPUTPREFIX) + GetLen(channels);

	polymax = 10;    /* XXX We need to be able to set this somehow */

	notecount = 0;
	notecount_decay = 0;

	argSustain_ = new thArg(string("SusPedal"), new float(0), 1);
	argSustain_->setWidgetType(thArg::CHANARG);
	argSustain_->setLabel(string("Sustain Pedal"));
	args["SusPedal"] = argSustain_;
}

thMidiChan::~thMidiChan (void)
{
	DestroyMap(args);
	DestroyMap(notes);
	delete[] output;
}

void thMidiChan::SetArg (thArg *arg)
{
	thArg *oldArg = args[arg->getName()];

	if (oldArg)
	{
		delete oldArg;
	}

	args[arg->getName()] = arg;
}

thMidiNote *thMidiChan::AddNote (float note, float velocity)
{
	thMidiNote *midinote;
	int id = (int)note;
	map<int, thMidiNote*>::iterator i = notes.find(id);
	if(i != notes.end()) {
		/* Make sure to turn off the old note, or it will hang! */
		/* XXX: Maybe we should keep track of who owned the note instead
		   of just making it die */
		float *pbuf = new float;
		*pbuf = 0;

		i->second->SetArg("trigger", pbuf, 1);

		decaying.push_front(i->second);
		notes.erase(i);
		notecount_decay++; /* we are keeping track of polyphony this way until
							  the advanced cool method is implemented */
		/* no need to dec notecounter since the new note replaces this one */
	}
	notecount++; /* see notecount_decay++ comment */

/* XXXXXXXXXXXXXXXXXXX: THIS IS BAD   WE MUST REMAP ALLLLL MIDI VALUS AT THE SAME TIME (but we only really have this one right now) */
	midinote = new thMidiNote(modnode, note, velocity * TH_MAX / MIDIVALMAX);
	notes[id] = midinote;

	return midinote;
}

void thMidiChan::DelNote (int note)
{
	map<int, thMidiNote*>::iterator i = notes.find(note);
	delete i->second;
	notes.erase(i);
}

void thMidiChan::ClearAll (void)
{
	map<int, thMidiNote*>::iterator i;

	for (i = notes.begin(); i != notes.end(); i++)
	{
		delete i->second;
		notes.erase(i);
	}
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

void thMidiChan::CopyChanArgs (thMod *mod)
{
	thArg *data, *newdata;
	map<string, thArg*> sourceargs = mod->GetChanArgs();

	for (map<string,thArg*>::const_iterator i = sourceargs.begin(); i != sourceargs.end(); i++)
	{
		data = i->second;

		newdata = new thArg(data);

		args[(*i).first] = newdata;
	}
}

void thMidiChan::Process (void)
{
	if (dirty) 
	{
		memset (output, 0, windowlength*channels*sizeof(float));
	}
	dirty = false;


	thMidiNote *data;
	thArg *arg, *amp, *play, *trigger;
	thMod *mod;
	int i, j, index;
	float buf_mix[windowlength];
	float buf_amp[windowlength];

	string argname;

	int sustain = (int)(*argSustain_)[0];

/* Before any processing, we shall do a polyphony test. */
	if(notecount + notecount_decay > polymax && polymax > 0) /* we have too
											many notes, and polyphony > 0 */
	{
		if(notecount_decay > 0) /* there are some notes not being held down */
		{
			list<thMidiNote*>::iterator iter = decaying.begin();
			while(iter != decaying.end() && notecount_decay > 0 &&
				  notecount + notecount_decay > polymax) /* more to do */
			{
				data = *iter;
				iter = decaying.erase(iter);
				delete data;
				notecount_decay--;
			}
		}
		if(notecount > polymax) /* too many notes held down */
		{
			map<int, thMidiNote*>::iterator iter = notes.begin();
			while(iter != notes.end() && notecount > polymax)
			{
				map<int, thMidiNote*>::iterator olditer = iter;  /* a copy of
												the old iterator to erase */
				iter++;
				data = olditer->second;
				notes.erase(olditer);
				delete data;
				notecount--;
			}	
		}
	}

	/* re-count the notes as we process, just to be sure nothing screws up */
	notecount = 0;
	notecount_decay = 0;

	map<int, thMidiNote*>::iterator iter = notes.begin();
	while(iter != notes.end())
	{
		notecount++;

		data = iter->second;

		dirty = true;
		
		data->Process(windowlength);
		
		mod = data->GetMod();
		amp = args["amp"];
		play = mod->GetArg("play");
		trigger = mod->GetArg("trigger");
		if ((*trigger)[0] == 2 && sustain < 0x40)
			trigger->SetValue(0);
		
		for(i = 0; i < channels; i++)
		{
			argname = OUTPUTPREFIX;
			argname += (char)(i+'0');
			arg = mod->GetArg(argname);
			arg->GetBuffer(buf_mix, windowlength);
			amp->GetBuffer(buf_amp, windowlength);
			for(j = 0; j < windowlength; j++)
			{
				index = i+(j*channels);
				output[index] += buf_mix[j]*(buf_amp[j]/MIDIVALMAX);
			}
		}

		map<int, thMidiNote*>::iterator olditer = iter++;  /* a copy of the
													old iterator to erase */
		
		if(play && (*play)[windowlength - 1] == 0)
		{
			delete data;
			notes.erase(olditer);
			notecount--;  /* polyphony stuff */
		}
	}

/* Now, the [almost] exact same thing for the list of decaying notes */

	list<thMidiNote*>::iterator diter = decaying.begin();
	while(diter != decaying.end())
	{
		notecount_decay++;
		data = *diter;
		dirty = 1;
		data->Process(windowlength);
		mod = data->GetMod();
		amp = args["amp"];
		play = mod->GetArg("play");
		trigger = mod->GetArg("trigger");
		if ((*trigger)[0] == 2 && sustain < 0x40)
			trigger->SetValue(0);
		
		for(i=0;i<channels;i++) {
			argname = OUTPUTPREFIX;
			argname += (char)(i+'0');
			arg = mod->GetArg(argname);
			arg->GetBuffer(buf_mix, windowlength);
			amp->GetBuffer(buf_amp, windowlength);
			for(j=0;j<windowlength;j++) {
				index = i+(j*channels);
				output[index] += buf_mix[j]*(buf_amp[j]/MIDIVALMAX);
			}
		}
		
		if(play && (*play)[windowlength - 1] == 0) {
			diter = decaying.erase(diter);
			delete data;
			notecount_decay--;  /* more polyphony stuff */
		}
		else
		{
			diter++;
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

void thMidiChan::AssignChanArgPointers (thMod *mod)
{
	thNode *curnode;
	thArg *curarg;
	map<string, thNode*> sourcenodes = mod->GetNodeList();
	map<string, thArg*> sourceargs;

	for (map<string, thNode*>::const_iterator i = sourcenodes.begin();
		 i != sourcenodes.end(); i++)
	{
		curnode = i->second;
		sourceargs = curnode->GetArgTree();

		for (map<string, thArg*>::const_iterator j = sourceargs.begin();
		 j != sourceargs.end(); j++)
		{
			curarg = j->second;

			if(curarg->argType == thArg::ARG_CHANNEL)
			{
				curarg->argPointArg = args[curarg->argPointName];
			}
		}
	}
}
