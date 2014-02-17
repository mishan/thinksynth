/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

enum {IN_A, IN_D, IN_S, IN_R, IN_P, IN_TRIGGER, IN_RESET, OUT_ARG, OUT_PLAY, INOUT_POSITION };
int args[INOUT_POSITION + 1];

char		*desc = "ADSR Envelope Generator";
thPlugin::State	mystate = thPlugin::ACTIVE;

static inline float SQR (float x)
{
	return x*x;
}

void module_cleanup (struct module *mod)
{
}

int module_init (thPlugin *plugin)
{
	plugin->setDesc (desc);
	plugin->setState (mystate);

	args[IN_A] = plugin->regArg("a");
	args[IN_D] = plugin->regArg("d");
	args[IN_S] = plugin->regArg("s");
	args[IN_R] = plugin->regArg("r");
	args[IN_P] = plugin->regArg("p");
	args[IN_TRIGGER] = plugin->regArg("trigger");
	args[IN_RESET] = plugin->regArg("reset");

	args[OUT_ARG] = plugin->regArg("out");
	args[OUT_PLAY] = plugin->regArg("play");

	args[INOUT_POSITION] = plugin->regArg("position");

	return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
					 unsigned int samples)
{
	/* User args */
	thArg *in_a, *in_d, *in_s, *in_r, *in_p, *in_trigger, *in_reset;
	thArg *inout_position;  /* [0] = position in stage, [1] = current stage */
	thArg *out_out, *out_play;

	float *out, *play;

	float *out_pos;
	float temp, temp2;  /* each (*in_a)[] thing uses a modulus and all that */
	float peak;
	float position;
	int phase;
	unsigned int i;
 
	out_out = mod->getArg(node, args[OUT_ARG]);
	out_play = mod->getArg(node, args[OUT_PLAY]);
	inout_position = mod->getArg(node, args[INOUT_POSITION]);

	position = (*inout_position)[0];
	phase = (int)(*inout_position)[1];
	out_pos = inout_position->allocate(2);

	out = out_out->allocate(windowlen);
	play = out_play->allocate(windowlen);

	in_a = mod->getArg(node, args[IN_A]); /* Attack */
	in_d = mod->getArg(node, args[IN_D]); /* Decay */
	in_s = mod->getArg(node, args[IN_S]); /* Sustain */
	in_r = mod->getArg(node, args[IN_R]); /* Release */
	in_p = mod->getArg(node, args[IN_P]); /* Peak */
	in_trigger = mod->getArg(node, args[IN_TRIGGER]); /* Note Trigger */
	in_reset = mod->getArg(node, args[IN_RESET]); /* Reset to A phase */

	if(phase == 0 && position == 0 && (*in_a)[0] == 0) {
		phase = 1;
	}

	for(i = 0; i < windowlen; i++) {
		if((*in_p)[i] == 0) {
			peak = TH_MAX;
		} else {
			peak = (*in_p)[i];
		}

		if((*in_reset)[i] > 0) {
                        position = 0;
			if((*in_a)[i] == 0) {
				phase = 1;
			} else {
                        	phase = 0;
			}
                }
		switch (phase) {  /* Which phase of the ADSR are we in? */
		case 0:   /* Attack */
			temp = (*in_a)[i];

			play[i] = 1;  /* Dont kill this note yet! */
			out[i] = ((position++)/temp)*peak;
			if(position >= temp) {
				phase = 1;   /* A ended, go to D */
				position = 0;  /* ...and go back to the beginnning of the phase */
			}
			break;
		case 1:
			temp = (*in_d)[i];
			temp2 = (*in_s)[i];

			play[i] = 1;

			out[i] = temp2+((log(M_E - (M_E - 1) * ((position++)/temp))) *
								 (peak-temp2));
			if(position >= temp) {
				phase = 2;   /* D ended, go to S */
				position = 0;
			}
			break;
		case 2:
		  temp = (*in_s)[i];
			if(temp == 0) {  /* If there is no D section, make it end */
				out[i] = 0;
				play[i] = 0;
				phase = 4;
			} else {
				play[i] = 1;
			}

			out[i] = temp;

			if((*in_trigger)[i] <= 0) {
				phase = 3;   /* D ended, time to fade out the note */
				/* position should already be 0 from the D section */
			}
			break;
		case 3:
			temp = (*in_r)[i];
			temp2 = (*in_s)[i];

			if(temp == 0 || position >= temp) {  /* Make it end if it needs to */
				out[i] = 0;
				play[i] = 0;
				phase = 4;
			} else {
				play[i] = 1;
				out[i] = (1 - log(1 + (M_E - 1) *
								  ((position++)/temp))) * temp2;
			}

			if((*in_trigger)[i] > 0) { // We have been retriggered
				position = 0;
				phase = 0;
			}
			break;
		case 4:   /* The note has ended and we are padding with 0 */
			play[i] = 0;
			out[i] = 0;

			 if((*in_trigger)[i] > 0) { // We have been retriggered
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
