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

enum {IN_ARG, OUT_ARG};
int args[OUT_ARG + 1];

/* 2 to the power of a stream.
 *
 * Separate from math::pow because this is the one the tuning arithmetic
 * wants and the one that is exact at the octaves: `mul = exp2(@cents/1200)'
 * is a detune in cents, and `exp2(@semis/12)' one in semitones. */
static const char desc[] = "Two raised to the power of a stream";
thPlugin::State    mystate = thPlugin::PASSIVE;

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "The power; 1 is an octave up");
    plugin->setArgUnits(args[IN_ARG], "octaves");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "2 to the power of in; a ratio");
    plugin->setArgUnits(args[OUT_ARG], "ratio");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    thArg *in_arg;
    thArg *out_arg;
    unsigned int i;

    in_arg = mod->getArg(node, args[IN_ARG]);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);

    for(i = 0; i < windowlen; i++) {
        out[i] = exp2f((*in_arg)[i]);
    }

    return 0;
}
