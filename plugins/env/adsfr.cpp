/* $Id: adsfr.cpp,v 1.9 2004/09/08 22:32:51 misha Exp $ */
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

#include "think.h"

enum {IN_A, IN_D, IN_S, IN_F, IN_R, IN_P, IN_TRIGGER, IN_RESET, OUT_ARG, OUT_PLAY, INOUT_POSITION };
int args[INOUT_POSITION + 1];

char		*desc = "ADSR Envelope Generator";
thPluginState	mystate = thActive;


static inline float SQR (float x)
{
	return x*x;
}


void module_cleanup (struct module *mod)
{
}

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[IN_A] = plugin->RegArg("a");
	args[IN_D] = plugin->RegArg("d");
	args[IN_S] = plugin->RegArg("s");
	args[IN_F] = plugin->RegArg("f");
	args[IN_R] = plugin->RegArg("r");
	args[IN_P] = plugin->RegArg("p");
	args[IN_TRIGGER] = plugin->RegArg("trigger");
	args[IN_RESET] = plugin->RegArg("reset");

	args[OUT_ARG] = plugin->RegArg("out");
	args[OUT_PLAY] = plugin->RegArg("play");

	args[INOUT_POSITION] = plugin->RegArg("position");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	/* User args */
	thArg *in_a, *in_d, *in_s, *in_r, *in_f, *in_p, *in_trigger, *in_reset;  
	thArg *inout_position;  /* [0] = position in stage, [1] = current stage */
	thArg *out_out, *out_play;

	float *out, *play;

	float *out_pos;
	float temp, temp2;  /* each (*in_a)[] thing uses a modulus and all that */
	float falloff;
	float peak;
	float position;
	int phase;
	unsigned int i;
	float val_a, val_d, val_s, val_f, val_r, val_p, val_trigger, val_reset;
 
	out_out = mod->GetArg(node, args[OUT_ARG]);
	out_play = mod->GetArg(node, args[OUT_PLAY]);
	inout_position = mod->GetArg(node, args[INOUT_POSITION]);

	position = (*inout_position)[0];
	phase = (int)(*inout_position)[1];
	out_pos = inout_position->Allocate(2);

	out = out_out->Allocate(windowlen);
	play = out_play->Allocate(windowlen);
	in_a = mod->GetArg(node, args[IN_A]); /* Attack */
	in_d = mod->GetArg(node, args[IN_D]); /* Decay */
	in_s = mod->GetArg(node, args[IN_S]); /* Sustain */
	in_f = mod->GetArg(node, args[IN_F]); /* Falloff */
	in_r = mod->GetArg(node, args[IN_R]); /* Release */
	in_p = mod->GetArg(node, args[IN_P]); /* Peak */
	in_trigger = mod->GetArg(node, args[IN_TRIGGER]); /* Note Trigger */
	in_reset = mod->GetArg(node, args[IN_RESET]); /* Reset to A phase */

	if(phase == 0 && position == 0 && (*in_a)[0] == 0) {
		phase = 1;
	}

	val_a = (*in_a)[i];
	val_d = (*in_d)[i];
	val_s = (*in_s)[i];
	val_f = (*in_f)[i];
	val_r = (*in_r)[i];
	val_p = (*in_p)[i];

	if(val_p == 0)
		peak = TH_MAX;
	else
		peak = val_p;

	for(i = 0; i < windowlen; i++) {
		val_trigger = (*in_trigger)[i];
		val_reset = (*in_reset)[i];


		if(val_reset > 0)
		{
			position = 0;
			if(val_a == 0)
			{
				phase = 1;
			} 
			else
			{
				phase = 0;
			}
		}

		switch (phase) {  /* Which phase of the ADSR are we in? */
		case 0:   /* Attack */
			play[i] = 1;  /* Dont kill this note yet! */
			if (val_a < 1)
				val_a = 1;
			out[i] = SQR((position++)/val_a)*peak;
			if(position >= val_a) {
				phase = 1;   /* A ended, go to D */
				position = 0;  /* ...and go back to the beginnning of the phase */
			}
			break;
		case 1:
			play[i] = 1;

			if (val_d < 1)
				val_d = 1;
			out[i] = val_s+SQR(((val_d-(position++))/val_d)*(peak-val_s));
			if(position >= val_d) {
				phase = 2;   /* D ended, go to S */
				position = 0;
			}
			break;
		case 2:
			falloff = val_f;
			temp = val_s * SQR((falloff - position++) / falloff);
			if(temp == 0 || position > falloff) {  /* If there is no D section,
													  make it end */
				out[i] = 0;
				play[i] = 0;
			}
			else {
				play[i] = 1;
			}

			out[i] = temp;

			if(val_trigger <= 0) {
				phase = 3;   /* D ended, time to fade out the note */
				position = (position / falloff) * val_r;
			}
			break;
		case 3:
			if(val_r == 0 || position >= val_r) {  /* Make it end if it needs
													to */
				out[i] = 0;
				play[i] = 0;
				phase = 4;
			}
			else {
				play[i] = 1;
				out[i] = SQR((val_r-(position++))/val_r)*val_s;
			}

			if(val_trigger > 0) { // We have been retriggered
				position = 0;
				phase = 0;
			}
			break;
		case 4:   /* The note has ended and we are padding with 0 */
			play[i] = 0;
			out[i] = 0;
			
			if(val_trigger > 0) { // We have been retriggered
				position = 0;
				phase = 0;
			}
			break;
		}
	}

	out_pos[0] = position;
	out_pos[1] = phase;
/*	node->SetArg("position", out_pos, 2);
	node->SetArg("out", out, windowlen);
	node->SetArg("play", play, windowlen);
*/
	return 0;
}
