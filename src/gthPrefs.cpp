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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "think.h"

#include "gthPrefs.h"
#include "gthPatchfile.h"
#include "gui-util.h"

gthPrefs *gthPrefs::instance_ = NULL;

#if 0
static void remove_string(char *line, int index, int numchars)
{
    /* removes a specific number of chars from a string starting 
     * at the position index. */
    unsigned int i;
    
    for(i = index; i + numchars < strlen(line); i++)
        line[i] = line[i + numchars];
    line[i] = '\0';
}
#endif

gthPrefs::gthPrefs (void)
{
	prefsPath_ = string(getenv("HOME")) + string("/") + PREFS_FILE;

	if (instance_ == NULL)
		instance_ = this;
}

gthPrefs::gthPrefs (const string &path)
{
	prefsPath_ = path;

	if (instance_ == NULL)
		instance_ = this;
}

gthPrefs::~gthPrefs (void)
{
	if (instance_ == this)
		instance_ = NULL;
}

void gthPrefs::Load (void)
{
	FILE *prefsFile;
	char buffer[256];

	debug("loading preferences");

	if((prefsFile = fopen(prefsPath_.c_str(), "r")) == NULL)
	{
		fprintf(stderr, "could not open %s: %s\n", prefsPath_.c_str(),
			strerror(errno));
	  	if ((prefsFile = fopen(DEFAULT_THINKRC, "r")) == NULL)
		{
			/* just give up */
			fprintf(stderr, "could not open " DEFAULT_THINKRC ": %s\n",
				strerror(errno));
			return;
		}
		else
			printf("opened default configuration " DEFAULT_THINKRC "\n");
	}

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
			}

			values[len] = NULL;

			/* XXX: handle specific cases here for now */
			if (key == "channel" && values[0] && values[1])
			{
				int chan = atoi(values[0]->c_str());
				gthPatchManager *patchMgr = gthPatchManager::instance();
				patchMgr->loadPatch(*values[1], chan);

				/* Amplitude setting */
				if (values[2] != NULL)
				{
					thSynth *s = thSynth::instance();
					float *f = new float (atof(values[2]->c_str()));
					thArg *arg = new thArg(string("amp"), f, 1);

					s->setChanArg(chan, arg);
				}
			}	
			else
			{
				prefs_[key] = values;
			}
		}
	}
}

void gthPrefs::Save (void)
{
	gthPatchManager *patchMgr = gthPatchManager::instance();
	FILE *prefsFile;

	if ((prefsFile = fopen(prefsPath_.c_str(), "w")) == NULL)
	{
		fprintf(stderr, "%s: %s\n", prefsPath_.c_str(), strerror(errno));
		return;
	}

	debug("writing to '%s'", prefsPath_.c_str());

	fprintf(prefsFile, "# %s configuration file\n", PACKAGE_STRING);
	fprintf(prefsFile, "# lines beginning with '#' are comments\n\n");
	
	/* save variables here */
	for (map<string, string**>::const_iterator i = prefs_.begin();
		 i != prefs_.end(); i++)
	{
		string key = i->first;
		string **values = i->second;
		
		if (values == NULL)
			continue;
		
		fprintf(prefsFile, "%s ", key.c_str());

		for(int j = 0; values[j]; j++)
		{
			fprintf(prefsFile, "%s", values[j]->c_str());

			if (values[j+1])
				fprintf(prefsFile, ",");
		}

		fprintf(prefsFile, "\n");
	}

	/* save channel mappings */
	{
		int chans = patchMgr->numPatches();
		thSynth *synth = thSynth::instance();

		for(int i = 0; i < chans; i++)
		{
			gthPatchManager::PatchFile *patch = patchMgr->getPatch(i);

			if (patch == NULL)
				continue;

			string file = patch->filename;

			/* after all, the .dsp file is the determining factor in a
			   channel */
			if (file.length() > 0)
			{
				thArg *amp = synth->getChanArg(i, "amp");
 				fprintf(prefsFile, "channel %d,%s,%d\n", i, file.c_str(),
						(int)((*amp)[0]));
			} 
		}
	}

	fclose(prefsFile);
}

string **gthPrefs::Get (const string &key)
{
	return prefs_[key];
}

void gthPrefs::Set (const string &key, string **vals)
{
	prefs_[key] = vals;
}
