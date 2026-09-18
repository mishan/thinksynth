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

#define SQR(x) (x*x)

/* The largest pole magnitude this filter will build, whatever `res' and
   `cutoff' work out to. Short of 1 by enough that float rounding cannot carry
   it over. */
#define QMAX 0.9995f

enum {IN_ARG, IN_CUTOFF, IN_RES, OUT_ARG, INOUT_LAST};
int args[INOUT_LAST + 1];

static const char desc[] = "12db IIR LPF";
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
    args[IN_CUTOFF] = plugin->regArg("cutoff", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_CUTOFF],
                       "Cutoff in hertz, 0 to half the sample rate");
    args[IN_RES] = plugin->regArg("res", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RES],
                       "Resonance; under about 0.5 the poles are clamped, "
                       "so 0.6 upwards is the usable range");

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "Filtered signal");

    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    float *out_last;
    thArg *in_arg, *in_cutoff, *in_res;
    thArg *out_arg;
    thArg *inout_last;
    float buf_in[windowlen], buf_cut[windowlen], buf_res[windowlen];

    unsigned int streamofs;
    float w; // Pole angle
    float q; // Pole magnitude
    float r;
    float c;
    float vibrapos;
    float vibraspeed;


    out_arg = mod->getArg(node, args[OUT_ARG]);
    inout_last = mod->getArg(node, args[INOUT_LAST]);

    vibrapos = (*inout_last)[0];
    vibraspeed = (*inout_last)[1];
    out_last = inout_last->allocate(2);

    /* Feedback state: one non-finite input is read back for ever after, so
       start over rather than stay dead for the life of the note. */
    if (!thIsFinite(vibrapos) || !thIsFinite(vibraspeed))
    {
        vibrapos = 0;
        vibraspeed = 0;
    }

    out = out_arg->allocate(windowlen);

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_cutoff = mod->getArg(node, args[IN_CUTOFF]);
    in_res = mod->getArg(node, args[IN_RES]);
    
    in_arg->getBuffer(buf_in, windowlen);
    in_cutoff->getBuffer(buf_cut, windowlen);
    in_res->getBuffer(buf_res, windowlen);

    for(streamofs = 0; streamofs < windowlen; streamofs++)
    {
        /* Hertz. Past Nyquist cos(w) has wrapped and the pole angle names a
           different filter from the one asked for. */
        w = 2.0*M_PI*thClampArg(buf_cut[streamofs], 0.0f, samples / 2.0f)
            / samples;                                          // Pole angle
        q = 1.0-w/(2.0*(buf_res[streamofs]+0.5/(1.0+w))+w-2.0); // Pole magnitude

        /* The poles are q e^(+-iw), so |q| < 1 is the stability condition
         * itself: the state matrix has determinant q*q and trace 2q cos(w),
         * and 1 + q*q > |2q cos(w)| holds for every w once |q| < 1 does.
         *
         * The coefficients do not respect it. The denominator above crosses
         * zero as res approaches 0.5 from above -- at DC it is zero there --
         * so res below about 0.5 asks for |q| >= 1 and the state diverged.
         *
         * Clamp the magnitude rather than the resonance: what leaves the
         * region is a magnitude, and the res that produced it is only out of
         * range at some cutoffs. Below 0.5 every res gives the same filter,
         * which the arg description says. A non-finite q is the denominator
         * landing on zero -- the same case, so the same answer. */
        if (!thIsFinite(q) || q > QMAX)
            q = QMAX;
        else if (q < -QMAX)
            q = -QMAX;

        r = q*q;
        c = r+1.0-2.0*cos(w)*q;

        /* Accelerate vibra by signal-vibra, multiplied by lowpasscutoff */
        vibraspeed += (buf_in[streamofs] - vibrapos) * c;
        
        /* Add velocity to vibra's position */
        vibrapos += vibraspeed;
        
        /* Attenuate/amplify vibra's velocity by resonance */
        vibraspeed *= r;

        out[streamofs] = vibrapos;
        if (vibrapos > TH_MAX)      /* cliping */
            out[streamofs] = TH_MAX;
        else if (vibrapos < TH_MIN)
            out[streamofs] = TH_MIN;
    }

    out_last[0] = vibrapos;
    out_last[1] = vibraspeed;

    return 0;
}

