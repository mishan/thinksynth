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

#define CONSTANT TH_MAX/MIDIVALMAX

enum {IN_ARG, OUT_ARG};
int args[OUT_ARG + 1];

char        *desc = "Maps a midi controller value from 0 to TH_MAX";
thPlugin::State    mystate = thPlugin::PASSIVE;

void module_cleanup (struct module *mod)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in");
    args[OUT_ARG] = plugin->regArg("out");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    thArg *in_arg;
    thArg *out_arg;
    unsigned int i, argnum;

    in_arg = mod->getArg(node, args[IN_ARG]);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    argnum = (unsigned int) in_arg->len();
    out = out_arg->allocate(argnum);

    for(i = 0; i < argnum; i++)
    {
        out[i] = (*in_arg)[i] * CONSTANT;
    }

    return 0;
}

