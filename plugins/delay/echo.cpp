/* $Id: echo.cpp,v 1.5 2004/03/26 09:38:37 joshk Exp $ */

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

extern "C" {
  int	module_init (thPlugin *plugin);
  int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
  void module_cleanup (struct module *mod);
}

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
	bufpos = inout_bufpos->Allocate(1);

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate(windowlen);

	for(i = 0; i < windowlen; i++) {
		unsigned int mySize = (int)(*in_size)[i];
		unsigned int myBufpos = (int)*bufpos;

		buffer = inout_buffer->Allocate(mySize);
		in = (*in_arg)[i];
		feedback = (*in_feedback)[i];
		dry = (*in_dry)[i];

		if(myBufpos > inout_buffer->argNum) {
			myBufpos = 0;
		}
		index = (int)(myBufpos - (*in_delay)[i]);
		while(index < 0) {
			index += inout_buffer->argNum;
		}
		delay = buffer[index];

		buffer[myBufpos] = (feedback * delay) + ((1-feedback) * in);

		out[i] = ((1 - dry) * delay) + (dry * in);
		++myBufpos;
		*bufpos = (float)myBufpos;
	}

	return 0;
}

