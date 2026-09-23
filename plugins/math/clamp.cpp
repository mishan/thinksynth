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

enum {IN_ARG, IN_LO, IN_HI, OUT_ARG};
int args[OUT_ARG + 1];

static const char desc[] = "Holds a stream between two others";
thPlugin::State    mystate = thPlugin::PASSIVE;

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    args[IN_LO] = plugin->regArg("lo", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_LO], "The bottom");
    args[IN_HI] = plugin->regArg("hi", thPlugin::ARG_IN);
    /* Not sorted first: a hi under a lo is a graph saying two contradictory
       things, and picking one for the author would hide it. What the order
       below does with one is test lo first, so the answer is lo everywhere
       under lo and hi from there up. Neither one wins -- the earlier reading
       of this comment, and the description that went with it, said hi did. */
    plugin->setArgDesc(args[IN_HI],
                       "The top; under lo, the answer is lo below lo and hi "
                       "above it");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "in, held between lo and hi");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    thArg *in_arg, *in_lo, *in_hi;
    thArg *out_arg;
    unsigned int i, n;

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_lo = mod->getArg(node, args[IN_LO]);
    in_hi = mod->getArg(node, args[IN_HI]);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    n = thOutLen(windowlen, in_arg, in_lo, in_hi);
    out = out_arg->allocate(n);

    for(i = 0; i < n; i++) {
        const float x = (*in_arg)[i], lo = (*in_lo)[i], hi = (*in_hi)[i];

        out[i] = (x < lo) ? lo : ((x > hi) ? hi : x);
    }

    return 0;
}
