/* $Id: fir.cpp,v 1.7 2003/05/25 17:31:25 ink Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
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
	thArgValue *in_arg, *in_impulse, *in_mix;
	thArgValue *out_arg;
	thArgValue *inout_buffer, *inout_bufpos;
	unsigned int i;
	int j, index;
	float impulse, mix;

	in_arg = (thArgValue *)mod->GetArg(node, "in");
	in_impulse = (thArgValue *)mod->GetArg(node, "impulse");
	in_mix = (thArgValue *)mod->GetArg(node, "mix");

	inout_buffer = (thArgValue *)mod->GetArg(node, "buffer");
	inout_bufpos = (thArgValue *)mod->GetArg(node, "bufpos");
	buffer = inout_buffer->allocate(in_impulse->argNum);
	bufpos = inout_bufpos->allocate(1);

	out_arg = (thArgValue *)mod->GetArg(node, "out");
	out = out_arg->allocate(windowlen);

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

