/* $Id: res2pole.cpp,v 1.9 2004/09/08 22:32:51 misha Exp $ */
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
/* Written by Leif Ames <ink@bespni.org>
   Algorithm taken from musicdsp.org
   References : Hal Chamberlin, "Musical Applications of Microprocessors,"
   2nd Ed, Hayden Book Company 1985. pp 490-492
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

char		*desc = "Resonant 2-pole Chamberlin filter";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
}

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out, *highout, *bandout, *notchout, *delay;
	thArg *in_arg, *in_cutoff, *in_res;
	thArg *out_low, *out_high, *out_band, *out_notch;
	thArg *inout_delay;
	float f, q;
	unsigned int i;

	out_low = mod->GetArg(node, "out");
	out_high = mod->GetArg(node, "out_high");
	out_band = mod->GetArg(node, "out_band");
	out_notch = mod->GetArg(node, "out_notch");
	out = out_low->Allocate(windowlen);
	highout = out_high->Allocate(windowlen);
	bandout = out_band->Allocate(windowlen);
	notchout = out_notch->Allocate(windowlen);

	inout_delay = mod->GetArg(node, "delay");
	delay = inout_delay->Allocate(2);

	in_arg = mod->GetArg(node, "in");
	in_cutoff = mod->GetArg(node, "cutoff");
	in_res = mod->GetArg(node, "res");

	for(i=0;i<windowlen;i++) {
		f = 2*sin(M_PI * (*in_cutoff)[i] / samples);
		q = 1/((*in_res)[i]*2);

		out[i] = delay[1] + f * delay[0];  /* Low Pass */
		highout[i] = (*in_arg)[i] - out[i] - q * delay[0]; /* High Pass */
		bandout[i] = f * highout[i] + delay[0];
		notchout[i] = highout[i] + out[i];
	
		delay[0] = bandout[i];
		delay[1] = out[i];
	}

	return 0;
}

