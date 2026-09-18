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

#include "think.h"

#define SQR(x) ((x)*(x))

static const char desc[] = "Generates a small parabola";
thPlugin::State    mystate = thPlugin::PASSIVE;

void module_cleanup (thPlugin *plugin)
{
}

enum { IN_LEN,IN_MAX,IN_PERCENT,OUT_ARG };

int args[OUT_ARG + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    /* An impulse response for delay::fir, not a signal: the output is `len'
       samples long and read whole. All three args come from sample 0, so none
       of them modulates. */
    args[IN_LEN] = plugin->regArg("len", thPlugin::ARG_IN);
    /* `(int)(*in_len)[0]' -- the impulse is this many samples long. */
    plugin->setArgStep(args[IN_LEN], 1);
    plugin->setArgDesc(args[IN_LEN], "How long the whole response is");
    plugin->setArgUnits(args[IN_LEN], "samples");
    args[IN_MAX] = plugin->regArg("max", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_MAX], "The height of the curve");
    plugin->setArgUnits(args[IN_MAX], "full scale");
    args[IN_PERCENT] = plugin->regArg("percent", thPlugin::ARG_IN);
    /* Its own zero case: `if (percent == 0) percent = 1', the whole length.
       What is left over is silence, split either side. */
    plugin->setArgDesc(args[IN_PERCENT],
                       "How much of `len' the curve fills; 0 means all of it");
    plugin->setArgRange(args[IN_PERCENT], 0, 1);
    plugin->setArgDefault(args[IN_PERCENT], 1);
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The response: a parabola over "
                       "`percent' of `len', and silence either side");
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    thArg *in_len, *in_max, *in_percent;
    thArg *out_arg;
    float max, percent, parabolalen, offset, half;
    unsigned int i, len;

    in_len = mod->getArg(node, args[IN_LEN]);
    in_max = mod->getArg(node, args[IN_MAX]);
    in_percent = mod->getArg(node, args[IN_PERCENT]);
    len = (int)(*in_len)[0];
    max = (*in_max)[0];
    percent = (*in_percent)[0];
    half = len/2;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(len);

    if(percent == 0) {
        percent = 1;
    }
    parabolalen = percent * len;
    offset = (len-parabolalen)/2;

    for(i = 0; i < offset; i++)
    {
        out[i] = 0;
    }
    for(; i < (parabolalen/2) + offset; i++)
    {
        out[i] = max*(1-SQR((i/((float)len-offset))*2-1));
    }
    for(; i < len; i++)
    {
        out[i] = out[(int)(half-(i-half))];
    }

    return 0;
}
