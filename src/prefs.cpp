/* $Id: prefs.cpp,v 1.4 2004/04/13 10:30:49 misha Exp $ */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <gtkmm.h>

#include "think.h"
#include "prefs.h"

static void trim_leadspc (char *line);
static char *get_config_path (void);
static void remove_string(char *line, int index, int numchars);

void save_prefs (thSynth *synth)
{
	FILE *prefsFile;
	char *path = get_config_path();

	prefsFile = fopen(path, "w");

	if (prefsFile == NULL)
	{
		fprintf(stderr, "could not open '%s': %s\n", path, strerror(errno));
		free(path);
		return;
	}

	debug("writing to '%s'", path);

	fprintf(prefsFile, "# %s configuration file\n", PACKAGE_STRING);
	fprintf(prefsFile, "# lines beginning with '#' are comments\n\n");

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
	free(path);
}

void read_prefs (thSynth *synth)
{
	FILE *prefsFile;
	char *path = get_config_path();
	char buffer[256];

	prefsFile = fopen(path, "r");

	if (prefsFile == NULL)
	{
		return;
	}

	while (!feof(prefsFile))
	{
		fgets(buffer, 256, prefsFile);
		trim_leadspc(buffer);
		buffer[strlen(buffer)-1] = '\0'; /* remove trailing \n */

		if(buffer[0] == '\n' || buffer[0] == '#')
			continue;
 
		if(!strncmp("channel", buffer, strlen("channel")))
		{
			char *chan, *file, *amp;
			char strtokbuf[256];

			remove_string(buffer, 0, strlen("channel"));
			trim_leadspc(buffer);

			chan = strtok_r(buffer, ",", (char **)&strtokbuf);
			file = strtok_r(NULL,   ",", (char **)&strtokbuf);
			amp  = strtok_r(NULL,   ",", (char **)&strtokbuf);

			synth->LoadMod(file, atoi(chan), atof(amp));
		}

		
	}

	free(path);
	fclose(prefsFile);
}

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

static char *get_config_path (void)
{
	char *home_dir = getenv("HOME"); 
	char *buffer = (char *)malloc(strlen(home_dir) + 1 + strlen(PREFS_FILE) + 1);
	sprintf(buffer, "%s/%s", home_dir, PREFS_FILE);

	return buffer;
}

static void remove_string(char *line, int index, int numchars)
{
    /* removes a specific number of chars from a string starting 
     * at the position index. */
    unsigned int i;
    
    for(i = index; i + numchars < strlen(line); i++)
        line[i] = line[i + numchars];
    line[i] = '\0';
}
