#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include "gthPatchfile.h"

gthPatchfile::gthPatchfile (thSynth *synth)
{
	synth_ = synth;
}

gthPatchfile::~gthPatchfile (void)
{
}

bool gthPatchfile::parse (char *filename)
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
