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
   Algorithm taken from musicdsp.org
   References : Hal Chamberlin, "Musical Applications of Microprocessors,"
   2nd Ed, Hayden Book Company 1985. pp 490-492
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

static const char desc[] = "Resonant 2-pole Chamberlin filter";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* The stable region, as constants. f is 2 sin(pi fc / fs) and so at most 2;
   FMAX keeps clear of the top, where the damping bound below collapses. QCEIL
   caps q when there is no cutoff to derive a bound from, QMIN keeps the
   determinant strictly inside the unit circle, and QMARGIN is the slack
   against float rounding. */
#define FMAX     1.98f
#define QCEIL    1.90f
#define QMIN     1e-4f
#define QMARGIN  0.98f

void module_cleanup (thPlugin *plugin)
{
}

enum { OUT_LOW, OUT_HIGH, OUT_BAND, OUT_NOTCH, INOUT_DELAY, IN_ARG, IN_CUTOFF,
       IN_RES };

int args[IN_RES + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[OUT_LOW] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_LOW], "Low pass");
    plugin->setArgUnits(args[OUT_LOW], "full scale");
    args[OUT_HIGH] = plugin->regArg("out_high", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_HIGH], "High pass");
    plugin->setArgUnits(args[OUT_HIGH], "full scale");
    args[OUT_BAND] = plugin->regArg("out_band", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_BAND], "Band pass");
    plugin->setArgUnits(args[OUT_BAND], "full scale");
    args[OUT_NOTCH] = plugin->regArg("out_notch", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_NOTCH], "Notch");
    plugin->setArgUnits(args[OUT_NOTCH], "full scale");
    args[INOUT_DELAY] = plugin->regArg("delay", thPlugin::ARG_STATE);
    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_CUTOFF] = plugin->regArg("cutoff", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_CUTOFF],
                       "Cutoff in hertz; honest to about a sixth of the "
                       "sample rate, clamped above that");
    plugin->setArgUnits(args[IN_CUTOFF], "Hz");
    args[IN_RES] = plugin->regArg("res", thPlugin::ARG_IN);
    /* No range: the floor moves with the cutoff (see the callback) and there
       is no ceiling. */
    plugin->setArgDesc(args[IN_RES],
                       "Resonance as Q: 0.5 is damped, higher rings. The "
                       "damping it can ask for is bounded by the cutoff");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out, *highout, *bandout, *notchout, *delay;
    thArg *in_arg, *in_cutoff, *in_res;
    thArg *out_low, *out_high, *out_band, *out_notch;
    thArg *inout_delay;
    float f, q;
    unsigned int i;

    out_low = mod->getArg(node, args[OUT_LOW]);
    out_high = mod->getArg(node, args[OUT_HIGH]);
    out_band = mod->getArg(node, args[OUT_BAND]);
    out_notch = mod->getArg(node, args[OUT_NOTCH]);
    out = out_low->allocate(windowlen);
    highout = out_high->allocate(windowlen);
    bandout = out_band->allocate(windowlen);
    notchout = out_notch->allocate(windowlen);

    inout_delay = mod->getArg(node, args[INOUT_DELAY]);
    delay = inout_delay->allocate(2);

    /* Feedback state: one non-finite input is read back for ever after, so
       start over rather than stay dead for the life of the note. */
    if (!thIsFinite(delay[0]) || !thIsFinite(delay[1]))
    {
        delay[0] = 0;
        delay[1] = 0;
    }

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_cutoff = mod->getArg(node, args[IN_CUTOFF]);
    in_res = mod->getArg(node, args[IN_RES]);

    for(i=0;i<windowlen;i++) {
        /* Hertz, meaningful up to Nyquist: past that the sine has turned
           back and names a different filter. */
        f = 2*sin(M_PI * thClampArg((*in_cutoff)[i], 0.0f, samples / 2.0f)
                  / samples);

        if (f > FMAX)
            f = FMAX;

        q = 1/((*in_res)[i]*2);

        /* Chamberlin's state matrix has determinant 1 - f*q and trace
         * 2 - f*f - f*q, so both eigenvalues are inside the unit circle
         * exactly when f*q > 0 and f*f + 2*f*q < 4. The second binds, and it
         * bounds the damping: q is 1/(2*res), so it is a *small* res that
         * asks for a step this explicit integrator cannot take, and res = 0
         * asks for an infinite one.
         *
         * The floor moves with the cutoff, hence a bound on q rather than a
         * range on the knob. A non-finite q is res = 0, asking for all the
         * damping there is; it gets all there is. */
        {
            float qmax = (f > 0) ? QMARGIN * (4 - f * f) / (2 * f) : QCEIL;

            if (qmax > QCEIL)
                qmax = QCEIL;

            if (!thIsFinite(q) || q > qmax)
                q = qmax;
            else if (q < QMIN)
                q = QMIN;
        }

        out[i] = delay[1] + f * delay[0];  /* Low Pass */
        highout[i] = (*in_arg)[i] - out[i] - q * delay[0]; /* High Pass */
        bandout[i] = f * highout[i] + delay[0];
        notchout[i] = highout[i] + out[i];
    
        delay[0] = bandout[i];
        delay[1] = out[i];
    }

    return 0;
}
