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

enum {IN_BASE, IN_EXP, OUT_ARG};
int args[OUT_ARG + 1];

/* What `pow(a, b)' in a .dsp expression becomes, and a node in its own
   right. Here rather than as a `^' operator so the language gains no
   precedence anyone has to remember. */
static const char desc[] = "Raises one stream to the power of another";
thPlugin::State    mystate = thPlugin::PASSIVE;

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_BASE] = plugin->regArg("base", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_BASE], "What is raised");
    args[IN_EXP] = plugin->regArg("exp", thPlugin::ARG_IN);
    /* powf's own rule, not this plugin's: a negative base with a fractional
       exponent has no real answer and comes back NaN, which the voice guard
       reports. Saying so here is cheaper than finding out from a silent
       channel. */
    plugin->setArgDesc(args[IN_EXP],
                       "The power; a fraction of a negative base is "
                       "non-finite");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "base raised to exp");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    thArg *in_base, *in_exp;
    thArg *out_arg;
    unsigned int i;

    in_base = mod->getArg(node, args[IN_BASE]);
    in_exp = mod->getArg(node, args[IN_EXP]);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);

    for(i = 0; i < windowlen; i++) {
        out[i] = powf((*in_base)[i], (*in_exp)[i]);
    }

    return 0;
}
