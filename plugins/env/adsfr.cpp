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

enum {IN_A, IN_D, IN_S, IN_F, IN_R, IN_P, IN_TRIGGER, IN_RESET, OUT_ARG, OUT_PLAY, INOUT_POSITION };
int args[INOUT_POSITION + 1];

char        *desc = "ADSR Envelope Generator";
thPlugin::State    mystate = thPlugin::ACTIVE;


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
    args[IN_F] = plugin->regArg("f");
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
    thArg *in_a, *in_d, *in_s, *in_r, *in_f, *in_p, *in_trigger, *in_reset;  
    thArg *inout_position;  /* [0] = position in stage, [1] = current stage */
    thArg *out_out, *out_play;

    float *out, *play;

    float *out_pos;
    float temp;  /* each (*in_a)[] thing uses a modulus and all that */
    float falloff;
    float peak;
    float position;
    int phase;
    unsigned int i = 0;
    float val_a, val_d, val_s, val_f, val_r, val_p, val_trigger, val_reset;
 
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
    in_f = mod->getArg(node, args[IN_F]); /* Falloff */
    in_r = mod->getArg(node, args[IN_R]); /* Release */
    in_p = mod->getArg(node, args[IN_P]); /* Peak */
    in_trigger = mod->getArg(node, args[IN_TRIGGER]); /* Note Trigger */
    in_reset = mod->getArg(node, args[IN_RESET]); /* Reset to A phase */

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
            out[i] = ((position++)/val_a)*peak;
            if(position >= val_a) {
                phase = 1;   /* A ended, go to D */
                position = 0;  /* ...and go back to the beginnning of the phase */
            }
            break;
        case 1:
            play[i] = 1;

            if (val_d < 1)
                val_d = 1;
            out[i] = val_s + ((log(M_E - (M_E - 1) * ((position++)/val_d))) *
                                (peak - val_s));
            if(position >= val_d) {
                phase = 2;   /* D ended, go to S */
                position = 0;
            }
            break;
        case 2:
            falloff = val_f;
            temp = (1 - log(1 + (M_E - 1) *
                            ((position++) / falloff))) * val_s;
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
                out[i] = (1 - log(1 + (M_E - 1) *
                                  ((position++)/val_r))) * val_s;
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
/*    node->SetArg("position", out_pos, 2);
    node->SetArg("out", out, windowlen);
    node->SetArg("play", play, windowlen);
*/
    return 0;
}
