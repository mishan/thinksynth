/* $Id: echo.cpp,v 1.2 2003/05/30 00:55:41 aaronl Exp $ */

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

char		*desc = "Echo (echo echo echo)";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Echo plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Echo plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	float *buffer, *bufpos;
	thArg *in_arg, *in_size, *in_delay, *in_feedback, *in_dry;
	thArg *out_arg;
	thArg *inout_buffer, *inout_bufpos;
	unsigned int i;
	int index;
	float delay, feedback, dry, in;

	in_arg = mod->GetArg(node, "in");
	in_size = mod->GetArg(node, "size"); /* Buffer size */
	in_delay = mod->GetArg(node, "delay"); /* Delay length */
	in_feedback = mod->GetArg(node, "feedback");
	in_dry = mod->GetArg(node, "dry"); /* How much of the origional signal is passed */

	inout_buffer = mod->GetArg(node, "buffer");
	inout_bufpos = mod->GetArg(node, "bufpos");
	bufpos = inout_bufpos->allocate(1);

	out_arg = mod->GetArg(node, "out");
	out = out_arg->allocate(windowlen);

	for(i=0;i<windowlen;i++) {
		buffer = inout_buffer->allocate((int)(*in_size)[i]);
		in = (*in_arg)[i];
		feedback = (*in_feedback)[i];
		dry = (*in_dry)[i];

		if(*bufpos > inout_buffer->argNum) {
			*bufpos = 0;
		}
		index = (int)(*bufpos - (*in_delay)[i]);
		while(index < 0) {
			index += inout_buffer->argNum;
		}
		delay = buffer[index];
		buffer[(int)*bufpos] = (feedback * delay) + ((1-feedback) * in);
		out[i] = ((1 - dry) * delay) + (dry * in);
		(*bufpos)++;
	}

	return 0;
}

