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

#define SQR(a) (a*a)

static const char desc[] = "Cheap IIR-ish Filter";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* The whole filter is buffer = in*k + buffer*(1 - k), stable exactly while
   |1 - k| < 1, that is for k in (0, 2). k is the factor to the fourth power,
   so the arg's ceiling is the fourth root of 2 and FACTORMAX sits under it.

   dsp/noargs/bd1.dsp drives the factor from an envelope sustaining at a
   hundred times full scale: k arrived at 1e8 and the graph had never made a
   sound. */
#define FACTORMAX 1.18f

void module_cleanup (thPlugin *plugin)
{
}

enum { OUT_ARG,INOUT_BUFFER,IN_ARG,IN_FACTOR };

int args[IN_FACTOR + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "Filtered signal");
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);
    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_FACTOR] = plugin->regArg("factor", thPlugin::ARG_IN);
    /* Raised to the fourth power to get the one-pole coefficient, so the
       useful travel is all in the top of the range. */
    plugin->setArgDesc(args[IN_FACTOR],
                       "Cutoff: the fourth power of this is the one-pole "
                       "coefficient");
    plugin->setArgRange(args[IN_FACTOR], 0, FACTORMAX);
    plugin->setArgUnits(args[IN_FACTOR], "one-pole coefficient, to the fourth");
    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    float *out_buf;
    thArg *in_arg, *in_factor;
    thArg *out_arg;
    thArg *inout_buffer;
    
    unsigned int i;
    float factor, buffer;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
    buffer = (*inout_buffer)[0];
    out = out_arg->allocate(windowlen);
    out_buf = inout_buffer->allocate(1);

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_factor = mod->getArg(node, args[IN_FACTOR]);

    /* Feedback state: one non-finite input is read back for ever after, so
       start over rather than stay dead for the life of the note. */
    if (!thIsFinite(buffer))
        buffer = 0;

    for(i=0;i<windowlen;i++) {
      const float k = thClampMag((*in_factor)[i], FACTORMAX);

      factor = SQR(SQR(k));

      buffer = ((*in_arg)[i] * factor) + (buffer * (1-factor));

      out[i] = buffer;
    }

    out_buf[0] = buffer;

/*    node->SetArg("out", out, windowlen);
    node->SetArg("buffer", out_buffer, 1); */

    return 0;
}
