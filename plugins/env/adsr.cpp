/* $Id: adsr.cpp,v 1.22 2004/04/08 00:34:56 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

char		*desc = "ADSR Envelope Generator";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
	printf("ADSR module unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("ADSR plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	thArg *in_a, *in_d, *in_s, *in_r, *in_p, *in_trigger, *in_reset;  /* User args */
	thArg *inout_position;  /* [0] = position in stage, [1] = current stage */
	thArg *out_out, *out_play;

	float *out, *play;

	float *out_pos;
	float temp, temp2;  /* each (*in_a)[] thing uses a modulus and all that */
	float peak;
	float position;
	int phase;
	unsigned int i;
 
	out_out = mod->GetArg(node, "out");
	out_play = mod->GetArg(node, "play");
	inout_position = mod->GetArg(node, "position");

	position = (*inout_position)[0];
	phase = (int)(*inout_position)[1];
	out_pos = inout_position->Allocate(2);

	out = out_out->Allocate(windowlen);
	play = out_play->Allocate(windowlen);

	in_a = mod->GetArg(node, "a"); /* Attack */
	in_d = mod->GetArg(node, "d"); /* Decay */
	in_s = mod->GetArg(node, "s"); /* Sustain */
	in_r = mod->GetArg(node, "r"); /* Release */
	in_p = mod->GetArg(node, "p"); /* Peak */
	in_trigger = mod->GetArg(node, "trigger"); /* Note Trigger */
	in_reset = mod->GetArg(node, "reset"); /* Reset to A phase */

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

			out[i] = temp2+(((temp-(position++))/temp)*(peak-temp2));
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
				out[i] = ((temp-(position++))/temp)*temp2;
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
