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

#include "config.h"

#include <sys/stat.h>
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
	thArg *amparg = NULL;
	bool r = true;

	if (patches_[chan])
	{
		/* keep copy of amplitude */
		amparg = new thArg (synth->getChanArg(chan, "amp"));
		delete patches_[chan];
		patches_[chan] = NULL;
	}

	thMod *mod = synth->loadMod(dspName.c_str(), chan, 0);

	if (mod == NULL)
	{
		r = false;
	}
	else
	{
		patches_[chan] = new PatchFile;
		patches_[chan]->dspFile = dspName;

		if (amparg != NULL)
			synth->setChanArg(chan, amparg); 
	}

	m_signal_patches_changed();

	return r;
}

bool gthPatchManager::loadPatch (const string &filename, int chan)
{
	if ((chan < 0) || (chan >= numPatches_))
		return false;

	bool r = parse(filename, chan);

	if (r)
		m_signal_patches_changed();
	else
		m_signal_patch_load_error(filename.c_str());
	
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
	if ((chan < 0) || (chan >= numPatches_) || patches_[chan] == NULL)
		return false;

	return true;
}

thMidiChan::ArgMap gthPatchManager::getChannelArgs (int chan)
{
	if ((chan < 0) || (chan >= numPatches_) || patches_[chan] == NULL)
		return thMidiChan::ArgMap();

	thSynth *synth = thSynth::instance();
	thMidiChan *mchan = synth->getChannel(chan);

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
	bool seen_dsp = false;
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

	while (fgets(buffer, 256, prefsFile) != NULL)
	{
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

			/* Parse special info field */
			if (key == "info")
			{
				/* first find the prop name */
				char* p = strchr(argPtr, ' ');
				*p++ = '\0';

				if (*p)
				{
					/* Replace escaped newlines. */
					string t = p;
					unsigned int i;

					while ((i = t.find ("\\n")) != string::npos)
						t.replace (i, 2, "\n");

					/* Now argPtr is the property name */
					patches_[chan]->info[argPtr] = t;
				}
				else
				{
					goto owned;
				}
			}
			else
			{
			
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
				struct stat st;
				const char *t = values[0]->c_str();
				string f;
				
				patches_[chan]->dspFile = *values[0];
			
				/* check if we're in an absolute or relative path */
				if (t[0] != '/' && stat(t, &st) == -1)
					f = DSP_PATH + *values[0];
				else
					f = *values[0];
					
				if (synth->loadMod(f.c_str(), chan, 0) == NULL)
					goto owned;
				
				seen_dsp = true;
			}
			else if (values[0])
			{
				thArg *arg = synth->getChanArg(chan, key);
				if (arg == NULL)
				{
					float *value = new float;
					*value = arglist[key];
					thArg *arg = new thArg(key, value, 1);
					synth->setChanArg(chan, arg);
				}
				else
				{
					arg->setValue(arglist[key]);
				}
			}
			else
			{
				/* erroneous directive ... */
				goto owned;
			}
			}
		}
		
	}

	/* OK, as far as we can tell */
	if (seen_dsp)
	{
		fclose(prefsFile);
		patches_[chan]->args = arglist;
	
		return true;
	}

owned:
	fclose(prefsFile);
	delete patches_[chan];
	patches_[chan] = NULL;
	return false;
}

bool gthPatchManager::savePatch (const string &filename, int chan)
{
	FILE *prefsFile;
	thMidiChan::ArgMap args = getChannelArgs(chan);
	time_t t = time(NULL);

	if (patches_[chan] == NULL)
		return false;

	if((prefsFile = fopen(filename.c_str(), "w")) == NULL)
		return false;

	printf("Saving %s\n", filename.c_str());
	fprintf(prefsFile,
		"# Thinksynth Patch File\n#\n# Generated by Thinksynth %s\n# %s\n\n",
		   PACKAGE_VERSION, ctime(&t));
	fprintf(prefsFile, "dsp %s\n\n", patches_[chan]->dspFile.c_str());

	for (PatchFileInfo::iterator k = patches_[chan]->info.begin();
		k != patches_[chan]->info.end(); k++)
	{
		/* replace with \n */
		string t = k->second;
		unsigned int i;

		if (t.size() > 0)
		{
			while ((i = t.find("\n")) != string::npos)
				t.replace(i, 1, "\\n");
	
			fprintf(prefsFile, "info %s %s\n", k->first.c_str(), t.c_str());
		}
	}
	
	for (thMidiChan::ArgMap::iterator j = args.begin();
		 j != args.end(); j++)
	{
		if(j->second->widgetType() != j->second->HIDE)
			fprintf(prefsFile, "%s %f\n", j->first.c_str(), (*j->second)[0]);
	}

	fclose(prefsFile);

	patches_[chan]->filename = filename;

	m_signal_patches_changed();

	return true;
}
