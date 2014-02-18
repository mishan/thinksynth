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
#include <time.h>
#include <math.h>

#include "think.h"

char        *desc = "Square wave with sine-like transitions, proportional to the frequency";
thPlugin::State    mystate = thPlugin::ACTIVE;

enum {OUT_ARG, INOUT_LAST, IN_FREQ, IN_PW, IN_SW};

int args[IN_SW + 1];

void module_cleanup (struct module *mod)
{
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[OUT_ARG] = plugin->regArg("out");
    args[INOUT_LAST] = plugin->regArg("last");
    args[IN_FREQ] = plugin->regArg("freq");
    args[IN_PW] = plugin->regArg("pw");
    args[IN_SW] = plugin->regArg("sw");
    
    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    int i;
    float *out;
    float *out_last;
    float wavelength, ratio;
    float sinewidth, minsqrwidth, maxsqrwidth;
    float position;
    int phase;
    thArg *in_freq, *in_pw, *in_sw;
    thArg *out_arg;
    thArg *inout_last;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    inout_last = mod->getArg(node, args[INOUT_LAST]);
    position = (*inout_last)[0];
    phase = (int)(*inout_last)[1];
    out_last = inout_last->allocate(2);
    out = out_arg->allocate(windowlen);

    in_freq = mod->getArg(node, args[IN_FREQ]);
    in_pw = mod->getArg(node, args[IN_PW]); // Pulse Width
    in_sw = mod->getArg(node, args[IN_SW]); // Sine Width

    /*  0 = sine from low-hi, 1 = high, 2 = hi-low, 3 = low  */

    for(i=0; i < (int)windowlen; i++) {
        wavelength = samples * (1.0/(*in_freq)[i]);
        
        sinewidth = wavelength * (*in_sw)[i];
        maxsqrwidth = (wavelength - (2*sinewidth)) * (*in_pw)[i];
        minsqrwidth = (wavelength - (2*sinewidth)) * (1-(*in_pw)[i]);
        switch(phase) {
        case 0:    /* Sine segment from low to high */
            ratio = position++/sinewidth;
            ratio = (ratio/2)+0.75; // We need the right part of the sine wave
            if(position >= sinewidth) { // End when its over
                position = 0;
                phase++;
            }
            out[i] = TH_MAX*sin(ratio*(2*M_PI)); /* This will fuck up if TH_MIX is not the negative of TH_MIN */
            break;
        case 1:    /* Maximum square */
            if(position++>maxsqrwidth) {
                position = 0;
                phase++;
            }
            out[i] = TH_MAX;
            break;
        case 2:    /* Sine segment from high to low */
            ratio = position++/sinewidth;
            ratio = (ratio/2)+0.25; // We need the right part of the sine wave
            if(position >= sinewidth) {
                position = 0;
                phase++;
            }
            out[i] = TH_MAX*sin(ratio*(2*M_PI)); /* This will fuck up if TH_MIX is not the negative of TH_MIN */
            break;
        case 3:    /* Minimum square */
            if(position++>minsqrwidth) {
                position = 0;
                phase = 0;
            }
            out[i] = TH_MIN;
            break;
        }
    }
    
/*    node->SetArg("out", out, windowlen); */
    out_last[0] = position;
    out_last[1] = (float)phase;
/*    node->SetArg("position", (float*)last, 2); */
    
    return 0;
}
