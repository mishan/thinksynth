/* $Id: moog.cpp,v 1.2 2003/05/30 00:55:41 aaronl Exp $ */

// Moog 24 dB/oct resonant lowpass VCF
// References: CSound source code, Stilson/Smith CCRMA paper.
// Modified by paul.kellett@maxim.abel.co.uk July 2000

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

char		*desc = "Moog Filter";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Moog Filter plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Moog Filter plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *buffer;
	thArg *in_arg, *in_cutoff, *in_res;
	thArg *inout_buffer;
	float b0, b1, b2, b3, b4;  //filter buffers (beware denormals!)
	float f, p, q;  /* feedback, cutoff, resonance */
	float t1, t2;
	unsigned int i;

	float *out_low = (mod->GetArg(node, "out_low"))->allocate(windowlen);
	float *out_high = (mod->GetArg(node, "out_high"))->allocate(windowlen);
	float *out_band = (mod->GetArg(node, "out_bandpass"))->allocate(windowlen);

	inout_buffer = mod->GetArg(node, "buffer");
	b0 = (*inout_buffer)[0];
	b1 = (*inout_buffer)[1];
	b2 = (*inout_buffer)[2];
	b3 = (*inout_buffer)[3];
	b4 = (*inout_buffer)[4];
	buffer = inout_buffer->allocate(5);

	in_arg = mod->GetArg(node, "in");
	in_cutoff = mod->GetArg(node, "cutoff");
	in_res = mod->GetArg(node, "res");

	for(i=0;i<windowlen;i++) {
		float frequency = (*in_cutoff)[i];
		float res = (*in_res)[i];
		float in = (*in_arg)[i] / TH_MAX;

		// Set coefficients given frequency & resonance [0.0...1.0]
		q = 1.0f - frequency;
		p = frequency + 0.8f * frequency * q;
		f = p + p - 1.0f;
		q = res* (1.0f + 0.5f * q * (1.0f - q + 5.6f * q * q));

		// Filter (in [-1.0...+1.0])
		in -= q * b4;                          //feedback
		t1 = b1;  b1 = (in + b0) * p - b1 * f;
		t2 = b2;  b2 = (b1 + t1) * p - b2 * f;
		t1 = b3;  b3 = (b2 + t2) * p - b3 * f;
		b4 = (b3 + t1) * p - b4 * f;
		b4 = b4 - b4 * b4 * b4 * 0.166667f;    //clipping
		b0 = in;

		out_low[i] = b4 * TH_MAX;
		out_high[i] = (in - b4) * TH_MAX;
		out_band[i] = 3.0f * (b3 - b4) * TH_MAX;
	}

	buffer[0] = b0;
	buffer[1] = b1;
	buffer[2] = b2;
	buffer[3] = b3;
	buffer[4] = b4;
	return 0;
}

