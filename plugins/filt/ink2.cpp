/* $Id: ink2.cpp,v 1.5 2004/04/09 07:10:44 ink Exp $ */

/* Written by Leif Ames <ink@bespni.org>
   Algorithm taken from musicdsp.org posted by Paul Kellett */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

#define SQR(x) ((x) * (x))

enum {IN_ARG, IN_CUTOFF, IN_RES, OUT_ARG, INOUT_BUFFER};
int args[INOUT_BUFFER + 1];

char		*desc = "INK Filter ][";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
	printf("INK Filt ][ plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("INK Filt ][ plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[IN_ARG] = plugin->RegArg("in");
	args[IN_CUTOFF] = plugin->RegArg("cutoff");
	args[IN_RES] = plugin->RegArg("res");

	args[OUT_ARG] = plugin->RegArg("out");

	args[INOUT_BUFFER] = plugin->RegArg("buffer");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out, *buffer;
	thArg *in_arg, *in_cutoff, *in_res;
	thArg *out_arg;
	thArg *inout_buffer;
	float buf0, buf1, in;
	float f, q;  /* cutoff, res */
	unsigned int i;

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out = out_arg->Allocate(windowlen);

	inout_buffer = mod->GetArg(node, args[INOUT_BUFFER]);
	buf0 = (*inout_buffer)[0];
	buf1 = (*inout_buffer)[1];
	buffer = inout_buffer->Allocate(2);

	in_arg = mod->GetArg(node, args[IN_ARG]);
	in_cutoff = mod->GetArg(node, args[IN_CUTOFF]);
	in_res = mod->GetArg(node, args[IN_RES]);

	for(i=0;i<windowlen;i++) {
		f = SQR((*in_cutoff)[i]);
		q = (*in_res)[i];
		in = (*in_arg)[i];

		buf0 *= 1 - f;
		buf0 += (buf1 - in) * f;
		buf1 *= 1 - (f * q);
		buf1 += (in - buf0) * q;

		out[i] = (buf0 - buf1);
	}

	buffer[0] = buf0;
	buffer[1] = buf1;
	return 0;
}

