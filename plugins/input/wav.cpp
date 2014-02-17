/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

//#include "thAudio.h"
//#include "thWav.h"

char		*desc = "Wav Input (BROKEN)";
thPlugin::State	mystate = thPlugin::ACTIVE;
//static thWav *thwav = NULL;

void module_cleanup (struct module *mod)
{
}

int module_init (thPlugin *plugin)
{
//	thwav = new_thWav("testwav.wav");
	plugin->setDesc (desc);
	plugin->setState (mystate);
	
	return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
					 unsigned int samples)
{
/*
	thArg *out_arg = mod->GetArg(node, "out");
	thArg *out_play = mod->GetArg(node, "play");
	signed short buf[windowlen * thwav->GetChannels()];
	float *out = out_arg->allocate(windowlen);
	float *play = out_play->allocate(windowlen);
	unsigned int i;
	int channels = thwav->GetChannels();

	thwav->Read(buf, windowlen * channels);

	for(i = 0; i < windowlen; i++) {
		out[i] = (((float)buf[i * channels]) / 32767) * TH_MAX;
		play[i] = !thwav->CheckEOF();
		} */

	return 0;
}

