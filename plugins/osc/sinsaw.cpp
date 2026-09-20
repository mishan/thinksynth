/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

static const char desc[] = "Sin-Saw oscillator";
thPlugin::State    mystate = thPlugin::ACTIVE;

enum { OUT_ARG, IN_FREQ, IN_FACTOR, INOUT_LAST };

int args[INOUT_LAST + 1];

void module_cleanup (thPlugin *plugin)
{
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The wave");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FREQ], "Frequency");
    plugin->setArgUnits(args[IN_FREQ], "Hz");
    args[IN_FACTOR] = plugin->regArg("factor", thPlugin::ARG_IN);
    /* Over a ramp x from -1 to 1: factor 0 flattens the wave to nothing,
       1 is a parabola-ish bow, and large exponents approach the ramp. */
    plugin->setArgDesc(args[IN_FACTOR],
                       "Shapes the ramp: (1 - abs(x)^factor) * x");
    plugin->setArgUnits(args[IN_FACTOR], "exponent");
    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    int i;
    float *out;
    float *out_last;
    float wavelength;
    float position, posratio;
    thArg *in_freq, *in_factor;
    thArg *out_arg;
    thArg *inout_last;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    inout_last = mod->getArg(node, args[INOUT_LAST]);

    position = (*inout_last)[0]; /* Where in the phase we are */
    out_last = inout_last->allocate(1);
    out = out_arg->allocate(windowlen);

    in_freq = mod->getArg(node, args[IN_FREQ]);
    in_factor = mod->getArg(node, args[IN_FACTOR]); // (1-abs(x^factor))*x

    for(i=0; i < (int)windowlen; i++) {
        wavelength = samples * (1.0/thBoundFreq((*in_freq)[i], samples));

        /* The wrap below takes the phase back to zero a cycle at a time,
           which is enough while `position' only ever advances by a sample.
           It is not enough when `wavelength' moves instead: a note retuned
           upwards shortens it by the interval, and the phase left behind is
           then several cycles long. `posratio' is that phase over the
           wavelength, so it arrives at ten or fifteen rather than at one,
           and pow() raises it to `factor' before it is mixed. Reduced here
           because it has to happen before the phase is read, not after. */
        if(position > wavelength) {
            position = fmod(position, wavelength);
        }

        posratio = 2*(position/wavelength)-1;
        out[i] = (1-pow(fabs(posratio), (*in_factor)[i]))*posratio*TH_MAX;
        //    printf("%f \t%f\n", position, out[i]);
        if(++position > wavelength) {
          position = 0;
        }
    }
    
    out_last[0] = position;
    
    return 0;
}
