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
/* Written by Leif Ames <ink@bespin.org>
   Algorithm taken from musicdsp.org posted by Paul Kellett */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

static const char desc[] = "Resonant 1-pole LPF";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* The edges of the stable region, held short of themselves against float
   rounding. See the callback for where 1 and 1 come from. */
#define FMAX 0.999f
#define QMAX 0.999f

void module_cleanup (thPlugin *plugin)
{
}

enum { OUT_ARG,INOUT_BUFFER,IN_ARG,IN_CUTOFF,IN_RES };

int args[IN_RES + 1];

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
    args[IN_CUTOFF] = plugin->regArg("cutoff", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_CUTOFF],
                       "Cutoff, 0 to 1 -- a fraction of the sample rate, "
                       "not hertz");
    plugin->setArgRange(args[IN_CUTOFF], 0, FMAX);
    plugin->setArgUnits(args[IN_CUTOFF], "fraction of the rate");
    args[IN_RES] = plugin->regArg("res", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RES],
                       "Resonance, 0 to 1; 1 is self-oscillation and is the "
                       "edge of the stable region, so it is clamped short");
    plugin->setArgRange(args[IN_RES], 0, QMAX);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out, *buffer;
    thArg *in_arg, *in_cutoff, *in_res;
    thArg *out_arg;
    thArg *inout_buffer;
    float buf0, buf1;
    float fb, f, q;  /* feedback, cutoff, resonance */
    unsigned int i;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);

    inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
    buf0 = (*inout_buffer)[0];
    buf1 = (*inout_buffer)[1];
    buffer = inout_buffer->allocate(2);

    /* Feedback state: one non-finite input is read back for ever after, so
       start over rather than stay dead for the life of the note. */
    if (!thIsFinite(buf0) || !thIsFinite(buf1))
    {
        buf0 = 0;
        buf1 = 0;
    }

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_cutoff = mod->getArg(node, args[IN_CUTOFF]);
    in_res = mod->getArg(node, args[IN_RES]);

    for(i = 0; i < windowlen; i++)
    {
        /* Writing g for 1 - f, the state matrix has determinant
         * g*g + f*q*(2 - f) and trace g*(2 + f*fb); both eigenvalues are
         * inside the unit circle exactly when 0 < f < 1 and 0 <= q < 1. The
         * determinant reaches 1 at q = 1 -- the self-oscillation this filter
         * is liked for, and where it stops coming back.
         *
         * Neither arg was bounded before: f = 1 divides by zero on the next
         * line, and anything past that diverged. */
        f = thClampArg((*in_cutoff)[i], 0.0f, FMAX);
        q = thClampArg((*in_res)[i], 0.0f, QMAX);
        fb = q + q/(1.0 - f);

        buf0 = buf0 + f * ((*in_arg)[i] - buf0 + fb * (buf0 - buf1));
        buf1 = buf1 + f * (buf0 - buf1);
        out[i] = buf1;
    }

    buffer[0] = buf0;
    buffer[1] = buf1;

    return 0;
}
