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
/* Written by Leif Ames <ink@bespni.org>
   Algorithm taken from musicdsp.org posted by Paul Kellett */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

#define SQR(x) ((x) * (x))

enum {IN_ARG, IN_CUTOFF, IN_RES, OUT_ARG, INOUT_BUFFER};
int args[INOUT_BUFFER + 1];

static const char desc[] = "INK Filter ][";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* The stable region. f is cutoff squared, so the knob's ceiling is 1; QMARGIN
   is the slack that keeps the q bound derived in the callback inside the
   region rather than on its edge. */
#define CMAX     0.999f
#define QCEIL    0.999f
#define QMARGIN  0.95f

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_CUTOFF] = plugin->regArg("cutoff", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_CUTOFF],
                       "Cutoff, 0 to 1 -- a fraction of the sample rate, "
                       "squared, not hertz");
    plugin->setArgRange(args[IN_CUTOFF], 0, CMAX);
    plugin->setArgUnits(args[IN_CUTOFF], "fraction of the rate");
    args[IN_RES] = plugin->regArg("res", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RES],
                       "Resonance, 0 to 1; what is stable near 1 narrows as "
                       "the cutoff rises, and is clamped");
    plugin->setArgRange(args[IN_RES], 0, QCEIL);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "Filtered signal");
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out, *buffer;
    thArg *in_arg, *in_cutoff, *in_res;
    thArg *out_arg;
    thArg *inout_buffer;
    float buf0, buf1, in;
    float f, q;  /* cutoff, res */
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

    for(i=0;i<windowlen;i++) {
        const float cut = thClampMag((*in_cutoff)[i], CMAX);

        f = SQR(cut);
        q = (*in_res)[i];

        /* The state matrix has determinant (1 - f)(1 - f*q) and trace
         * 2 - f - 2*f*q, so both eigenvalues are inside the unit circle
         * exactly when 0 < f < 1, q > 0 and 4 - 2f - 3*f*q + f*f*q > 0. The
         * last is a ceiling on q that comes down as f rises, landing exactly
         * on the circle at f = q = 1.
         *
         * Neither arg was bounded before; past the region the state
         * diverged. */
        {
            float qmax = (f > 0)
                ? QMARGIN * (4.0f - 2.0f * f) / (f * (3.0f - f))
                : QCEIL;

            if (qmax > QCEIL)
                qmax = QCEIL;

            q = thClampArg(q, 0.0f, qmax);
        }

        in = (*in_arg)[i];

        buf0 *= 1 - f;
        buf0 += (buf1 - in) * f;
        buf1 *= 1 - (f * q);
        buf1 += (in - buf0) * q;

        out[i] = (buf0 - buf1);
    }

    buffer[0] = buf0;
    buffer[1] = buf1;

    return 0;
}

