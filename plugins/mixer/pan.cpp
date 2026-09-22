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

/* A pan: one signal to two sides, by constant power.
 *
 *     out0 = in * cos(theta),  out1 = in * sin(theta),
 *     theta = (pan + 1) * pi / 4
 *
 * so -1 is all left, 1 all right, and 0 both at 0.707 -- three decibels
 * down each, which is what keeps a sound panned across the field at one
 * loudness: the two sides' powers always sum to the input's. A linear pan,
 * the two expressions a graph would write by hand, is a sound that dips
 * six decibels as it passes the middle.
 *
 * It is two expressions, and the reason it is a node is that the node
 * knows the law. The one a graph reaches for is `ionode->aux0', a note's
 * own place in the field, which xform::vary and xform::cloud write.
 *
 * `pan' is held to -1..1, and a non-finite one is the middle.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

enum {IN_ARG, IN_PAN, OUT_ARG0, OUT_ARG1};
int args[OUT_ARG1 + 1];

static const char desc[] = "Pan (constant power)";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_PAN] = plugin->regArg("pan", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_PAN], "-1 left, 0 the middle, 1 right");
    plugin->setArgRange(args[IN_PAN], -1, 1);
    args[OUT_ARG0] = plugin->regArg("out0", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG0], "The left side");
    plugin->setArgUnits(args[OUT_ARG0], "full scale");
    args[OUT_ARG1] = plugin->regArg("out1", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG1], "The right side");
    plugin->setArgUnits(args[OUT_ARG1], "full scale");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg = mod->getArg(node, args[IN_ARG]);
    thArg *in_pan = mod->getArg(node, args[IN_PAN]);
    float *out0 = mod->getArg(node, args[OUT_ARG0])->allocate(windowlen);
    float *out1 = mod->getArg(node, args[OUT_ARG1])->allocate(windowlen);

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float pan = thClampMag((*in_pan)[i], 1);
        const double theta = (pan + 1) * (M_PI / 4);
        const float in = (*in_arg)[i];

        out0[i] = in * (float)cos(theta);
        out1[i] = in * (float)sin(theta);
    }

    return 0;
}
