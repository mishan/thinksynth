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

	modnode_ = mod;
	windowlength_ = windowlen;
	allocatedamp[0] = amp;
	dirty_ = 1;

	args_[string("amp")] = new thArg(string("amp"), allocatedamp, 1);

	chanarg = modnode_->getArg("channels");
	channels_ = (int)chanarg->values()[0];

	output_ = new float[channels_*windowlen];
	outputnamelen_ = strlen(OUTPUTPREFIX) + GetLen(channels_);

	polymax_ = 10;    /* XXX We need to be able to set this somehow */

	notecount_ = 0;
	notecount_decay_ = 0;

	argSustain_ = new thArg(string("SusPedal"), new float(0), 1);
	argSustain_->setWidgetType(thArg::CHANARG);
	argSustain_->setLabel(string("Sustain Pedal"));
	args_["SusPedal"] = argSustain_;
}

thMidiChan::~thMidiChan (void)
{
	DestroyMap(args_);
	DestroyMap(notes_);
	delete[] output_;
}

void thMidiChan::SetArg (thArg *arg)
{
	thArg *oldArg = args_[arg->name()];

	if (oldArg)
	{
		delete oldArg;
	}

	args_[arg->name()] = arg;
}

thMidiNote *thMidiChan::AddNote (float note, float velocity)
{
	thMidiNote *midinote;
	int id = (int)note;
	NoteMap::iterator i = notes_.find(id);
	if(i != notes_.end()) {
		/* Make sure to turn off the old note, or it will hang! */
		float *pbuf = new float;
		*pbuf = 0;

		i->second->SetArg("trigger", pbuf, 1);

		noteorder_.remove(i->second);
		decaying_.push_front(i->second);
		notes_.erase(i);
		notecount_decay_++; /* we are keeping track of polyphony this way until
							  the advanced cool method is implemented */
		/* no need to dec notecounter since the new note replaces this one */
	}
	notecount_++; /* see notecount_decay_++ comment */

	midinote = new thMidiNote(modnode_, note, velocity * TH_MAX / MIDIVALMAX);
	notes_[id] = midinote;
	noteorder_.push_back(midinote);

	return midinote;
}

void thMidiChan::DelNote (int note)
{
	NoteMap::iterator i = notes_.find(note);
	delete i->second;
	notes_.erase(i);
}

void thMidiChan::ClearAll (void)
{
	NoteMap::iterator i;
	NoteList::iterator j;

	for (i = notes_.begin(); i != notes_.end(); i++)
	{
		delete i->second;
		notes_.erase(i);
	}
	for (j = decaying_.begin(); j != decaying_.end(); j++)
	{
		delete *j;
		j = decaying_.erase(j);
	}
}

thMidiNote *thMidiChan::GetNote (int note)
{
	NoteMap::iterator i = notes_.find(note);

	if(i != notes_.end()) {
		return i->second;
	}

	return NULL;
}

int thMidiChan::SetNoteArg (int note, char *name, float *value, int len)
{
	NoteMap::iterator i = notes_.find(note);

	if(i != notes_.end()) {
		i->second->SetArg(name, value, len);
		return 1;
	}
	return 0;
}

void thMidiChan::CopyChanArgs (thMod *mod)
{
	thArg *data, *newdata;
	thMod::ArgMap sourceargs = mod->getChanArgs();

	for (thMod::ArgMap::const_iterator i = sourceargs.begin();
		 i != sourceargs.end(); i++)
	{
		data = i->second;

		newdata = new thArg(data);

		args_[(*i).first] = newdata;
	}
}

void thMidiChan::Process (void)
{
	if (dirty_) 
	{
		memset (output_, 0, windowlength_*channels_*sizeof(float));
	}
	dirty_ = false;


	thMidiNote *data;
	thArg *arg, *amp, *play, *trigger;
	thMod *mod;
	int i, j, index;
	float buf_mix[windowlength_];
	float buf_amp[windowlength_];

	string argname;

	int sustain = (int)(*argSustain_)[0];

	/* Before any processing, we shall do a polyphony test. */
	if(notecount_ + notecount_decay_ > polymax_ && polymax_ > 0) 
	{
		/* we have too many notes, and polyphony > 0 */

		if(notecount_decay_ > 0) /* there are some notes not being held down */
		{
			NoteList::iterator iter = decaying_.begin();

			/* more to do */
			while(iter != decaying_.end() && notecount_decay_ > 0 &&
				  notecount_ + notecount_decay_ > polymax_)
			{
				delete *iter;
				iter = decaying_.erase(iter);
				notecount_decay_--;
			}
		}
		if(notecount_ > polymax_) /* too many notes held down */
		{
			NoteList::iterator iter = noteorder_.begin();
			while(iter != noteorder_.end() && notecount_ > polymax_)
			{
				notes_.erase((*iter)->GetID());
				delete *iter;
				iter = noteorder_.erase(iter);
				notecount_--;
			}	
		}
	}

	/* re-count the notes as we process, just to be sure nothing screws up */
	notecount_ = 0;
	notecount_decay_ = 0;

	NoteMap::iterator iter = notes_.begin();
	while(iter != notes_.end())
	{
		notecount_++;

		data = iter->second;

		dirty_ = true;
		
		data->Process(windowlength_);
		
		mod = data->GetMod();
		amp = args_["amp"];
		play = mod->getArg("play");
		trigger = mod->getArg("trigger");

		if ((*trigger)[0] == 2 && sustain < 0x40)
			trigger->setValue(0);
		
		for(i = 0; i < channels_; i++)
		{
			argname = OUTPUTPREFIX;
			argname += (char)(i+'0');
			arg = mod->getArg(argname);
			arg->getBuffer(buf_mix, windowlength_);
			amp->getBuffer(buf_amp, windowlength_);

			for(j = 0; j < windowlength_; j++)
			{
				index = i+(j*channels_);
				output_[index] += buf_mix[j]*(buf_amp[j]/MIDIVALMAX);
			}
		}

		NoteMap::iterator olditer = iter++;  /* a copy of the
												old iterator to erase */
		
		if(play && (*play)[windowlength_ - 1] == 0)
		{
			noteorder_.remove(data);
			delete data;
			notes_.erase(olditer);
			notecount_--;  /* polyphony stuff */
		}
	}

	/* Now, the [almost] exact same thing for the list of decaying notes */
	NoteList::iterator diter = decaying_.begin();

	while(diter != decaying_.end())
	{
		notecount_decay_++;
		data = *diter;
		dirty_ = 1;
		data->Process(windowlength_);
		mod = data->GetMod();
		amp = args_["amp"];
		play = mod->getArg("play");
		trigger = mod->getArg("trigger");

		if ((*trigger)[0] == 2 && sustain < 0x40)
			trigger->setValue(0);
		
		for(i = 0; i < channels_; i++)
		{
			argname = OUTPUTPREFIX;
			argname += (char)(i+'0');
			arg = mod->getArg(argname);
			arg->getBuffer(buf_mix, windowlength_);
			amp->getBuffer(buf_amp, windowlength_);

			index = i;
			for(j = 0; j < windowlength_; j++)
			{
				output_[index] += buf_mix[j]*(buf_amp[j]/MIDIVALMAX);
				index += channels_;
			}
		}
		
		if(play && (*play)[windowlength_ - 1] == 0)
		{
			diter = decaying_.erase(diter);
			delete data;
			notecount_decay_--;  /* more polyphony stuff */
		}
		else
		{
			diter++;
		}
	}
}

/* XXX: this is ghetto; but this method can be used globally, move it to
   util */
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
	thMod::NodeMap sourcenodes = mod->getNodeList();
	thMod::ArgMap sourceargs;

	for (thMod::NodeMap::const_iterator i = sourcenodes.begin();
		 i != sourcenodes.end(); i++)
	{
		curnode = i->second;
		sourceargs = curnode->GetArgTree();

		for (thMod::ArgMap::const_iterator j = sourceargs.begin();
		 j != sourceargs.end(); j++)
		{
			curarg = j->second;

			if(curarg->type() == thArg::ARG_CHANNEL)
			{
				curarg->setArgPtr(args_[curarg->argPtrName()]);
			}
		}
	}
}
