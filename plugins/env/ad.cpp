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

char		*desc = "ADSR Envelope Generator";
thPlugin::State	mystate = thPlugin::ACTIVE;

void module_cleanup (struct module *mod)
{
}

enum { OUT_OUT,OUT_PLAY,INOUT_POSITION,IN_A,IN_D,IN_P,IN_RESET };

int args[IN_RESET + 1];

int module_init (thPlugin *plugin)
{
	plugin->setDesc (desc);
	plugin->setState (mystate);

	args[OUT_OUT] = plugin->regArg("out");
	args[OUT_PLAY] = plugin->regArg("play");
	args[INOUT_POSITION] = plugin->regArg("position");
	args[IN_A] = plugin->regArg("a");
	args[IN_D] = plugin->regArg("d");
	args[IN_P] = plugin->regArg("p");
	args[IN_RESET] = plugin->regArg("reset");

	return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
					 unsigned int samples)
{
	thArg *in_a, *in_d, *in_p, *in_reset;  /* User args */
	thArg *inout_position;  /* [0] = position in stage, [1] = current stage */
	thArg *out_out, *out_play;

	float *out, *play;

	float *out_pos;
	float temp;
	float peak;
	float position;
	int phase;
	unsigned int i;
 
	out_out = mod->getArg(node, args[OUT_OUT]);
	out_play = mod->getArg(node, args[OUT_PLAY]);
	inout_position = mod->getArg(node, args[INOUT_POSITION]);

	position = (*inout_position)[0];
	phase = (int)(*inout_position)[1];
	out_pos = inout_position->allocate(2);

	out = out_out->allocate(windowlen);
	play = out_play->allocate(windowlen);

	in_a = mod->getArg(node, args[IN_A]);
	in_d = mod->getArg(node, args[IN_D]);
	in_p = mod->getArg(node, args[IN_P]);
	in_reset = mod->getArg(node, args[IN_RESET]);

	if(phase == 0 && position == 0 && (*in_a)[0] == 0) {
		phase = 1;
	}

	for(i=0;i<windowlen;i++) {
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
		switch (phase) {  /* Which phase of the AD are we in? */
		case 0:   /* Attack */
			temp = (*in_a)[i];

			play[i] = 1;  /* Dont kill this note yet! */
			out[i] = (sin((((position++)/temp)-0.5)*M_PI)+1)*(peak/2);
			if(position >= temp) {
				phase = 1;   /* A ended, go to D */
				position = 0;  /* ...and go back to the beginnning of the phase */
			}
			break;
		case 1:  /* Decay */
			temp = (*in_d)[i];

			play[i] = 1;

			out[i] = (sin((((position++)/temp)+0.5)*M_PI)+1)*(peak/2);
			if(position >= temp) {
				phase = 2;   /* D ended, pad until note is over */
			}
			break;
		case 2:   /* The note has ended and we are padding with 0 */
			play[i] = 0;
			out[i] = 0;

			 if((*in_reset)[i] > 0) { // We have been retriggered
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
