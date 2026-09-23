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

enum {IN_0, IN_1, OUT_ARG};
int args[OUT_ARG + 1];

static const char desc[] = "Subtracts two streams";
thPlugin::State    mystate = thPlugin::PASSIVE;

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_0] = plugin->regArg("in0", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_0], "What to subtract from");
    args[IN_1] = plugin->regArg("in1", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_1], "What to subtract");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "in0 - in1");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out ;
    thArg *in_0, *in_1;
    thArg *out_arg;
    unsigned int i, n;

    in_0 = mod->getArg(node, args[IN_0]);
    in_1 = mod->getArg(node, args[IN_1]);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    n = thOutLen(windowlen, in_0, in_1);
    out = out_arg->allocate(n);

    for(i = 0; i < n; i++)
    {
        out[i] = (*in_0)[i]-(*in_1)[i];
    }

    return 0;
}

