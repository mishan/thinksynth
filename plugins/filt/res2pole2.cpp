/* $Id: res2pole2.cpp,v 1.3 2004/09/08 22:32:51 misha Exp $ */
/*
 * Copyright (C) 2004 Metaphonic Labs
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
#include <math.h>

#include "think.h"

#define SQR(x) (x*x)

enum {IN_ARG, IN_CUTOFF, IN_RES, OUT_ARG, INOUT_LAST};
int args[INOUT_LAST + 1];

char		*desc = "12db IIR LPF";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
}

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[IN_ARG] = plugin->RegArg("in");
	args[IN_CUTOFF] = plugin->RegArg("cutoff");
	args[IN_RES] = plugin->RegArg("res");

	args[OUT_ARG] = plugin->RegArg("out");

	args[INOUT_LAST] = plugin->RegArg("last");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	float *out_last;
	thArg *in_arg, *in_cutoff, *in_res;
	thArg *out_arg;
	thArg *inout_last;
	float buf_in[windowlen], buf_cut[windowlen], buf_res[windowlen];

	unsigned int streamofs;
	float w; // Pole angle
	float q; // Pole magnitude
	float r;
	float c;
	float vibrapos;
	float vibraspeed;


	out_arg = mod->GetArg(node, args[OUT_ARG]);
	inout_last = mod->GetArg(node, args[INOUT_LAST]);

	vibrapos = (*inout_last)[0];
	vibraspeed = (*inout_last)[1];
	out_last = inout_last->Allocate(2);

	out = out_arg->Allocate(windowlen);

	in_arg = mod->GetArg(node, args[IN_ARG]);
	in_cutoff = mod->GetArg(node, args[IN_CUTOFF]);
	in_res = mod->GetArg(node, args[IN_RES]);
	
	in_arg->GetBuffer(buf_in, windowlen);
	in_cutoff->GetBuffer(buf_cut, windowlen);
	in_res->GetBuffer(buf_res, windowlen);

	for(streamofs = 0; streamofs < windowlen; streamofs++)
	{
		w = 2.0*M_PI*buf_cut[streamofs]/samples; // Pole angle
		q = 1.0-w/(2.0*(buf_res[streamofs]+0.5/(1.0+w))+w-2.0); // Pole magnitude
		r = q*q;
		c = r+1.0-2.0*cos(w)*q;

		/* Accelerate vibra by signal-vibra, multiplied by lowpasscutoff */
		vibraspeed += (buf_in[streamofs] - vibrapos) * c;
		
		/* Add velocity to vibra's position */
		vibrapos += vibraspeed;
		
		/* Attenuate/amplify vibra's velocity by resonance */
		vibraspeed *= r;

		out[streamofs] = vibrapos;
		if (vibrapos > TH_MAX)      /* cliping */
			out[streamofs] = TH_MAX;
		else if (vibrapos < TH_MIN)
			out[streamofs] = TH_MIN;
	}

	out_last[0] = vibrapos;
	out_last[1] = vibraspeed;

	return 0;
}

