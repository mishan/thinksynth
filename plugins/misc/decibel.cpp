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

static const char desc[] = "Converts dB to an amplitude value. Arg should be <= 0.";
thPlugin::State    mystate = thPlugin::PASSIVE;

void module_cleanup (thPlugin *plugin)
{
}

enum { DB,OUT_ARG };

int args[OUT_ARG + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[DB] = plugin->regArg("db", thPlugin::ARG_IN);
    /* exp(db * 0.11512925), which is 10^(db/20): 0 comes out as 1, -6 as
       about a half, -60 as a thousandth. Positive decibels work and give a
       gain above 1, which is why the ceiling is 0 rather than the arithmetic
       breaking there. */
    plugin->setArgDesc(args[DB],
                       "Decibels: 0 is unity, below it attenuates, and above "
                       "it amplifies -- the plugin's own ceiling of 0 is a "
                       "convention, not a limit");
    plugin->setArgUnits(args[DB], "dB");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The gain those decibels mean");
    plugin->setArgUnits(args[OUT_ARG], "ratio");
    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    thArg *db;
    thArg *out_arg;
    unsigned int i, argnum;

    db = mod->getArg(node, args[DB]);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    argnum = (unsigned int)db->len();
    out = out_arg->allocate(argnum);

    for(i=0;i<argnum;i++) {
        out[i] = exp((*db)[i]*.11512925f); // thx vorbis
    }

/*    node->SetArg("out", out, windowlen); */
    return 0;
}
