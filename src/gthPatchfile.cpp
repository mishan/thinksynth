/* $Id: gthPatchfile.cpp,v 1.6 2004/11/16 23:22:02 misha Exp $ */
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

#include "config.h"

#include <cassert>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "think.h"

#include "gthPatchfile.h"
#include "gui-util.h"

gthPatchManager *gthPatchManager::instance_ = NULL;

gthPatchManager::gthPatchManager (int numPatches)
{
	numPatches_ = numPatches;

	if (instance_ == NULL)
		instance_ = this;

	patches_ = new PatchFile*[numPatches_];

	/* init patches to NULL */
	for (int i = 0; i < numPatches_; i++)
		patches_[i] = NULL;
}

gthPatchManager::~gthPatchManager (void)
{
	if (instance_ == this)
		instance_ = NULL;

	/* XXX: do necessary cleanup here */
	for (int i = 0; i < numPatches_; i++)
		delete patches_[i];
}

gthPatchManager *gthPatchManager::instance (void) {
	if (instance_ == NULL)
		instance_ = new gthPatchManager;
	
	return instance_;
}

bool gthPatchManager::newPatch (const string &dspName, int chan)
{
	thSynth *synth = thSynth::instance();
	bool r = true;

	if (patches_[chan])
	{
		delete patches_[chan];
		patches_[chan] = NULL;
	}

	thMod *mod = synth->LoadMod(dspName.c_str(), chan, 0);

	if (mod == NULL)
	{
		r = false;
	}
	else
	{
		patches_[chan] = new PatchFile;
		patches_[chan]->dspFile = dspName;
	}

	m_signal_patches_changed();

	return r;
}

bool gthPatchManager::loadPatch (const string &filename, int chan)
{
	bool r = parse(filename, chan);

	m_signal_patches_changed();

	return r;
}

bool gthPatchManager::unloadPatch (int chan)
{
	if ((chan < 0) || (chan >= numPatches_) || (patches_[chan] == NULL))
		return false;

	thSynth *synth = thSynth::instance();

	synth->removeChan(chan);
	delete patches_[chan];
	patches_[chan] = NULL;

	m_signal_patches_changed();

	return true;
}

bool gthPatchManager::isLoaded (int chan)
{
	if ((chan > 0) || (chan >= 0) || patches_[chan] == NULL)
		return false;

	return true;
}

thMidiChan::ArgMap gthPatchManager::getChannelArgs (int chan)
{
	if ((chan > 0) || (chan >= 0) || patches_[chan] == NULL)
		return thMidiChan::ArgMap();

	thSynth *synth = thSynth::instance();
	thMidiChan *mchan = synth->GetChannel(chan);

	if (mchan == NULL)
	{
		printf("ERROR! Got NULL MidiChan\n");
		return thMidiChan::ArgMap();
	}

	return mchan->GetArgs();
}

/* XXX: add error checking */
bool gthPatchManager::parse (const string &filename, int chan)
{
	FILE *prefsFile;
	char buffer[256];
	thSynth *synth = thSynth::instance();
	PatchFileArgs arglist;

	if((prefsFile = fopen(filename.c_str(), "r")) == NULL)
	{
		return false;
	}

	if (patches_[chan])
		delete patches_[chan];

	patches_[chan] = new PatchFile;
	patches_[chan]->filename = filename;

	while (!feof(prefsFile))
	{
		fgets(buffer, 256, prefsFile);
		trim_leadspc(buffer);
		buffer[strlen(buffer)-1] = '\0';

		if (buffer[0] == '\n' || buffer[0] == '#')
			continue;

		char *argPtr = strchr(buffer, ' ');
		if (argPtr == NULL)
			continue;

		*argPtr++ = '\0';
		string key = buffer;

		trim_leadspc(argPtr);

		if (*argPtr)
		{
			int len = 1;
			char *comCnt;

			for(comCnt=strchr(argPtr,',');comCnt;comCnt = strchr(++comCnt,','))
			{
				len++;
			}

			string **values = new string *[len+1];

			for(int i = 0; i < len; i++)
			{
				char *comPtr = strchr(argPtr, ',');
				if (comPtr)
					*comPtr = '\0';

				values[i] = new string(argPtr);

				argPtr = comPtr+1;

				if (comPtr == NULL)
				{
					len = i+1;
					break;
				}
			}

			values[len] = NULL;
			arglist[key] = strtof(values[0]->c_str(), NULL);

			/* XXX: handle specific cases here for now */
			if (key == "dsp" && values[0])
			{
				patches_[chan]->dspFile = *values[0];
				/* XXX: check error */
				synth->LoadMod(values[0]->c_str(), chan, 0);
			}
			else if (values[0])
			{
				thArg *arg = synth->GetChanArg(chan, key);
				if (arg == NULL)
				{
					float *value = new float;
					*value = arglist[key];
					thArg *arg = new thArg(key, value, 1);
					synth->SetChanArg(chan, arg);
				}
				else
				{
					arg->SetValue(arglist[key]);
				}
			}
			
		}
		
	}

//	patch.args = arglist;
//	patch.filename = filename;
	patches_[chan]->args = arglist;
	
	return true;
}

bool gthPatchManager::savePatch (const string &filename, int chan)
{
	FILE *prefsFile;
	thMidiChan::ArgMap args = getChannelArgs(chan);
	time_t t = time(NULL);

	if (patches_[chan] == NULL)
		return false;

	if((prefsFile = fopen(filename.c_str(), "w")) == NULL)
	{
		return false;
	}

	printf("Saving %s\n", filename.c_str());
	fprintf(prefsFile,
		"# Thinksynth Patch File\n#\n# Generated by Thinksynth %s\n# %s\n\n",
		   PACKAGE_VERSION, ctime(&t));
	fprintf(prefsFile, "dsp %s\n\n", patches_[chan]->dspFile.c_str());

	for (thMidiChan::ArgMap::iterator j = args.begin();
		 j != args.end(); j++)
	{
		if(j->second->getWidgetType() != j->second->HIDE)
			fprintf(prefsFile, "%s %f\n", j->first.c_str(), (*j->second)[0]);
	}

	fclose(prefsFile);

	return true;
}
