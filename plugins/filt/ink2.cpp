/* $Id: ink2.cpp,v 1.4 2004/04/08 00:34:56 misha Exp $ */

/* Written by Leif Ames <ink@bespni.org>
   Algorithm taken from musicdsp.org posted by Paul Kellett */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

#define SQR(x) ((x) * (x))

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

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate(windowlen);

	inout_buffer = mod->GetArg(node, "buffer");
	buf0 = (*inout_buffer)[0];
	buf1 = (*inout_buffer)[1];
	buffer = inout_buffer->Allocate(2);

	in_arg = mod->GetArg(node, "in");
	in_cutoff = mod->GetArg(node, "cutoff");
	in_res = mod->GetArg(node, "res");

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

