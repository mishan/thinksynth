/* $Id: gthPatchfile.cpp,v 1.3 2004/09/24 00:38:40 misha Exp $ */
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

#include <stdlib.h>
#include <stdio.h>

#include "think.h"

#include "gthPatchfile.h"
#include "gui-util.h"

gthPatchfile::gthPatchfile (const char *filename, thSynth *synth, int chan)
{
	synth_    = synth;
	chanNum_  = chan;
	midiChan_ = NULL;

	parse(filename);
}

gthPatchfile::~gthPatchfile (void)
{
}

bool gthPatchfile::parse (const char *filename)
{
	FILE *prefsFile;
	char buffer[256];

	if((prefsFile = fopen(filename, "r")) == NULL)
	{
		return false;
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
			args_[key] = values;

			/* XXX: handle specific cases here for now */
			if (key == "dsp" && values[0])
			{
				printf("associating with dsp %s\n", values[0]->c_str());
			}
			else if (values[0])
			{
				printf ("arg %s %f\n", key.c_str(), strtof(values[0]->c_str(),
														   NULL));
			}

		}

	}

	return true;
}
