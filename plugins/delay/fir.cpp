/* $Id: fir.cpp,v 1.9 2003/09/16 01:02:28 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

char		*desc = "Applies an impulse response";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Impulse plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Impulse plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	float *buffer, *bufpos;
	thArg *in_arg, *in_impulse, *in_mix;
	thArg *out_arg;
	thArg *inout_buffer, *inout_bufpos;
	unsigned int i;
	int j, index;
	float impulse, mix;

	in_arg = mod->GetArg(node, "in");
	in_impulse = mod->GetArg(node, "impulse");
	in_mix = mod->GetArg(node, "mix");

	inout_buffer = mod->GetArg(node, "buffer");
	inout_bufpos = mod->GetArg(node, "bufpos");
	buffer = inout_buffer->Allocate(in_impulse->argNum);
	bufpos = inout_bufpos->Allocate(1);

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate(windowlen);

	for(i=0;i<windowlen;i++) {
		out[i] = 0;

		if(*bufpos > inout_buffer->argNum) {
			*bufpos = 0;
		}
		buffer[(int)*bufpos] = (*in_arg)[i];

		for(j=0 ; j < in_impulse->argNum ; j++) {
			index = (int)*bufpos - j;
			if(index < 0) {
				index += in_impulse->argNum;
			}
			impulse = (*in_impulse)[j];
			if(impulse) {
				out[i] += impulse * buffer[index];
			}
		}
		mix = (*in_mix)[i];
		if(mix) {
			buffer[(int)*bufpos] = (buffer[(int)*bufpos] * (1-mix)) + (out[i] * mix); // recursion!
		}
		(*bufpos)++;
	}

	return 0;
}

