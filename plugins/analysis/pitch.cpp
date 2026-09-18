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
#include <math.h>

#include "think.h"

static const char desc[] = "Follows the pitch of the input";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

enum { OUT_ARG,INOUT_LAST,IN_ARG,IN_FALLOFF };

int args[IN_FALLOFF + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    /* Zero until the second rising edge, which is the first time there is a
       period to measure. Whatever reads this has to cope with that -- a
       misc::freq2samples fed a zero answers an infinity. */
    plugin->setArgDesc(args[OUT_ARG],
                       "The measured pitch, and 0 until it has measured one");
    plugin->setArgUnits(args[OUT_ARG], "Hz");
    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);
    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    /* Rising zero crossings and nothing else -- no window, no filter -- so
       anything with harmonics above the fundamental reads high. */
    plugin->setArgDesc(args[IN_ARG],
                       "Signal in; the period is counted between rising zero "
                       "crossings");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_FALLOFF] = plugin->regArg("falloff", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FALLOFF],
                       "Registered and never read; see the callback");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out, *out_last;
    thArg *in_arg;
    thArg *out_arg;
    thArg *inout_last;
    unsigned int i;
    float input, last;
    float freq;
    int sign, wavelength;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    inout_last = mod->getArg(node, args[INOUT_LAST]);

    last = (*inout_last)[0];
    freq = (*inout_last)[1];
    wavelength = (int)(*inout_last)[2];

    out_last = inout_last->allocate(3);

    out = out_arg->allocate(windowlen);

    in_arg = mod->getArg(node, args[IN_ARG]);

    /* `falloff' is registered and stays registered -- it is part of the
       published arg list and a .dsp may set it -- but nothing reads it.
       The loop used to compute `pow(0.1, (*in_falloff)[i])' into a local
       and throw it away: the smoothing it was meant to feed was never
       written. Implementing one here would be inventing a sound. */

    for(i = 0; i < windowlen; i++)
    {
        input = (*in_arg)[i];

        if(last > 0)
            sign = 1;
        else
            sign = 0;
        
        if(sign == 0 && input > 0) /* trigger on the rising edge */
        {
            /* Both operands are integers, so this is integer division and a
               wavelength of zero is undefined -- a trap on x86 rather than an
               infinity. It is zero on the first sample of a fresh note, and
               reached whenever that sample is already above zero: a graph
               whose oscillator starts positive, with no envelope in front of
               it, is one edit away. A period shorter than one sample is not a
               period, so there is nothing to report for it. */
            if(wavelength > 0)
            {
                freq = (float)samples / (float)wavelength;
            }

            wavelength = 0;
        }
        else
        {
            wavelength++;
        }

        out[i] = freq;

        last = input;
    }

    out_last[0] = last;
    out_last[1] = freq;
    out_last[2] = wavelength;
    

    return 0;
}
