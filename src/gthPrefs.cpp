/* $Id: gthPrefs.cpp,v 1.6 2004/08/16 09:34:48 misha Exp $ */
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

static void trim_leadspc (char *line)
{
    /* delete the leading white spaces from the string 'line' */
    char *line_strt;
    char *ptr1, *ptr2;
    
    /* find the first non-space and point to it with 'line_strt' */
    for (line_strt = line; *line_strt != '\0'; line_strt++)
	if (*line_strt != ' ' && *line_strt != '\t')
	    break;
    
    /* copy the string beginning at 'line_strt' into 'line' */
    for (ptr1 = line, ptr2 = line_strt; *ptr2 != '\0'; ptr1++, ptr2++)
	*ptr1 = *ptr2;
    *ptr1 = '\0';
}

gthPrefs::gthPrefs (thSynth *argsynth)
{
	prefsPath = string(getenv("HOME")) + string("/") + PREFS_FILE;
	synth = argsynth;
}

gthPrefs::gthPrefs (thSynth *argsynth, const string &path)
{
	prefsPath = path;
	synth = argsynth;
}

gthPrefs::~gthPrefs (void)
{
}

void gthPrefs::Load (void)
{
	FILE *prefsFile;
	char buffer[256];
	int channel = 0;

	if((prefsFile = fopen(prefsPath.c_str(), "r")) == NULL)
	{
		/* XXX: set the defaults */
		return;
	}

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
			}

			values[len] = NULL;


			/* XXX: handle specific cases here for now */
			if (key == "channel" && values[0] && values[1])
			{
				synth->LoadMod(values[1]->c_str(),
							   atoi(values[0]->c_str()),
							   values[2] ? atof(values[2]->c_str()) : 0 );
			}
			else
			{
				prefs[key] = values;
				
			}
		}

	}
}

void gthPrefs::Save (void)
{
	FILE *prefsFile;

	if ((prefsFile = fopen(prefsPath.c_str(), "w")) == NULL)
	{
		fprintf(stderr, "%s: %s\n", prefsPath.c_str(), strerror(errno));
		return;
	}

	debug("writing to '%s'", prefsPath.c_str());

	fprintf(prefsFile, "# %s configuration file\n", PACKAGE_STRING);
	fprintf(prefsFile, "# lines beginning with '#' are comments\n\n");

	/* save variables here */
	for (map<string, string**>::const_iterator i = prefs.begin();
		 i != prefs.end(); i++)
	{
		string key = i->first;
		string **values = i->second;
		
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
		int chans = synth->GetChannelCount();
		std::map<int, string> *patchlist = synth->GetPatchlist();

		for(int i = 0; i < chans; i++)
		{
			string file = (*patchlist)[i];
			thArg *amp = synth->GetChanArg(i, "amp");

			/* after all, the .dsp file is the determining factor in a
			   channel */
			if (file.length() > 0)
			{
				fprintf(prefsFile, "channel %d,%s,%f\n", i, file.c_str(),
						amp ? (*amp)[0] : 0.0);
			}
		}
	}

	fclose(prefsFile);
}

string **gthPrefs::Get (const string &key)
{
	return prefs[key];
}

void gthPrefs::Set (const string &key, string **vals)
{
	prefs[key] = vals;
}
