/* $Id: ink.cpp 1605 2005-02-17 23:35:55Z misha $ */
/*
 * Copyright (C) 2004-2005 Metaphonic Labs
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

#define SQR(x) ((x)*(x))

enum {IN_ARG, IN_CUTOFF, IN_RES, IN_SHAPE, OUT_ARG, OUT_BAND, OUT_HIGH,
	  OUT_NOTCH, INOUT_LAST};
int args[INOUT_LAST + 1];

char		*desc = "`INK Filter`  this algorithm was in my head when I woke up";
thPlugin::State	mystate = thPlugin::ACTIVE;

void module_cleanup (struct module *mod)
{
}

int module_init (thPlugin *plugin)
{
	plugin->setDesc (desc);
	plugin->setState (mystate);

	args[IN_ARG] = plugin->regArg("in");
	args[IN_CUTOFF] = plugin->regArg("cutoff");
	args[IN_RES] = plugin->regArg("res");
	args[IN_SHAPE] = plugin->regArg("shape");

	args[OUT_ARG] = plugin->regArg("out");
	args[OUT_BAND] = plugin->regArg("out_band");
	args[OUT_HIGH] = plugin->regArg("out_high");
	args[OUT_NOTCH] = plugin->regArg("out_notch");

	args[INOUT_LAST] = plugin->regArg("last");

	return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
					 unsigned int sample)
{
	float *out;
	float *band_out;
	float *high_out;
	float *notch_out;
	float *out_last;
//	float *out_last_accel = new float[1];
	thArg *in_arg, *in_cutoff, *in_res, *in_shape;
	thArg *out_arg, *out_band, *out_high, *out_notch;
	thArg *inout_last;
	float buf_in[windowlen], buf_cut[windowlen], buf_res[windowlen],
		buf_shape[windowlen];
	unsigned int i;
	float in, last, diff, accel;
	float val_arg, val_cutoff, val_res, val_shape;

	out_arg = mod->getArg(node, args[OUT_ARG]);
	out_band = mod->getArg(node, args[OUT_BAND]);
	out_high = mod->getArg(node, args[OUT_HIGH]);
	out_notch = mod->getArg(node, args[OUT_NOTCH]);
	inout_last = mod->getArg(node, args[INOUT_LAST]);

	last = (*inout_last)[0];
	accel = (*inout_last)[1];
	out_last = inout_last->allocate(2);

	out = out_arg->allocate(windowlen);
	band_out = out_band->allocate(windowlen);
	high_out = out_high->allocate(windowlen);
	notch_out = out_notch->allocate(windowlen);

	in_arg = mod->getArg(node, args[IN_ARG]);
	in_cutoff = mod->getArg(node, args[IN_CUTOFF]);
	in_res = mod->getArg(node, args[IN_RES]);
	in_shape = mod->getArg(node, args[IN_SHAPE]);
	
	in_arg->getBuffer(buf_in, windowlen);
	in_cutoff->getBuffer(buf_cut, windowlen);
	in_res->getBuffer(buf_res, windowlen);
	in_shape->getBuffer(buf_shape, windowlen);

	for(i = 0; i < windowlen; i++) {
		val_arg = buf_in[i];
		val_cutoff = buf_cut[i];
		val_res = buf_res[i];
		val_shape = buf_shape[i];

		diff = val_arg - (1 - 0.5 * val_res) * last + tanh(last * val_shape * (1 - 0.5 * val_res));
		accel += diff * SQR(val_cutoff);
		accel *= val_res;

		last += accel;

		band_out[i] = accel;

		out[i] = last;
		high_out[i] = val_arg - last;
		notch_out[i] = val_arg - accel;

		//printf("%f\n", out[i]);
		//printf("in: %f, last: %f, diff: %f, cut: %f\n", val_arg, last, diff, val_cutoff);
	}

	out_last[0] = last;
	out_last[1] = accel;

	return 0;
}

