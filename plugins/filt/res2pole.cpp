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
/* Written by Leif Ames <ink@bespin.org>
   Algorithm taken from musicdsp.org
   References : Hal Chamberlin, "Musical Applications of Microprocessors,"
   2nd Ed, Hayden Book Company 1985. pp 490-492
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

char        *desc = "Resonant 2-pole Chamberlin filter";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (struct module *mod)
{
}

enum { OUT_LOW, OUT_HIGH, OUT_BAND, OUT_NOTCH, INOUT_DELAY, IN_ARG, IN_CUTOFF,
       IN_RES };

int args[IN_RES + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[OUT_LOW] = plugin->regArg("out");
    args[OUT_HIGH] = plugin->regArg("out_high");
    args[OUT_BAND] = plugin->regArg("out_band");
    args[OUT_NOTCH] = plugin->regArg("out_notch");
    args[INOUT_DELAY] = plugin->regArg("delay");
    args[IN_ARG] = plugin->regArg("in");
    args[IN_CUTOFF] = plugin->regArg("cutoff");
    args[IN_RES] = plugin->regArg("res");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out, *highout, *bandout, *notchout, *delay;
    thArg *in_arg, *in_cutoff, *in_res;
    thArg *out_low, *out_high, *out_band, *out_notch;
    thArg *inout_delay;
    float f, q;
    unsigned int i;

    out_low = mod->getArg(node, args[OUT_LOW]);
    out_high = mod->getArg(node, args[OUT_HIGH]);
    out_band = mod->getArg(node, args[OUT_BAND]);
    out_notch = mod->getArg(node, args[OUT_NOTCH]);
    out = out_low->allocate(windowlen);
    highout = out_high->allocate(windowlen);
    bandout = out_band->allocate(windowlen);
    notchout = out_notch->allocate(windowlen);

    inout_delay = mod->getArg(node, args[INOUT_DELAY]);
    delay = inout_delay->allocate(2);

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_cutoff = mod->getArg(node, args[IN_CUTOFF]);
    in_res = mod->getArg(node, args[IN_RES]);

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
