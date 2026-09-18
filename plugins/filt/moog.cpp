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
// Moog 24 dB/oct resonant lowpass VCF
// References: CSound source code, Stilson/Smith CCRMA paper.
// Modified by paul.kellett@maxim.abel.co.uk July 2000

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

static const char desc[] = "Moog Filter";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* The coefficients are fitted for cutoff and res in 0..1 and mean nothing
   outside it: past 1 the cutoff turns the one-pole coefficient inside out and
   every stage of the ladder diverges. FMAX stops short of 1, where each
   stage's pole sits exactly on the unit circle.

   BMAX is where the cubic soft clip below stops being a soft clip: y = x -
   x*x*x/6 turns over at sqrt(2) and changes sign past sqrt(6), so a signal
   loud enough to push the ladder that far took the filter with it. Clamping at
   the turning point keeps the saturation monotonic. */
#define FMAX 0.999f
#define QMAX 1.0f
#define BMAX 1.4142135f

void module_cleanup (thPlugin *plugin)
{
}

enum { INOUT_BUFFER,IN_ARG,IN_CUTOFF,IN_RES,
       OUT_LOW,OUT_HIGH,OUT_BANDPASS };

int args[OUT_BANDPASS + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);
    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_CUTOFF] = plugin->regArg("cutoff", thPlugin::ARG_IN);
    /* A fraction of the rate, against filt::res2pole2's hertz. */
    plugin->setArgDesc(args[IN_CUTOFF],
                       "Cutoff, 0 to 1 -- a fraction of the sample rate, "
                       "not hertz. Clamped: the fit means nothing past 1");
    plugin->setArgRange(args[IN_CUTOFF], 0, FMAX);
    plugin->setArgUnits(args[IN_CUTOFF], "fraction of the rate");
    args[IN_RES] = plugin->regArg("res", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RES],
                       "Resonance, 0 to 1; 1 self-oscillates. Clamped for "
                       "the same reason the cutoff is");
    plugin->setArgRange(args[IN_RES], 0, QMAX);

    /* These three are what the filter is *for*, and until now they existed
       only as string lookups in the callback -- created on first use, invisible
       to anything asking the plugin what it produces. A .dsp reading
       filt->out_low was therefore reading an output nothing declared. */
    args[OUT_LOW] = plugin->regArg("out_low", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_LOW], "Low pass, the ladder's fourth stage");
    plugin->setArgUnits(args[OUT_LOW], "full scale");
    args[OUT_HIGH] = plugin->regArg("out_high", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_HIGH], "High pass");
    plugin->setArgUnits(args[OUT_HIGH], "full scale");
    args[OUT_BANDPASS] = plugin->regArg("out_bandpass", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_BANDPASS], "Band pass");
    plugin->setArgUnits(args[OUT_BANDPASS], "full scale");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *buffer;
    thArg *in_arg, *in_cutoff, *in_res;
    thArg *inout_buffer;
    float b0, b1, b2, b3, b4;  //filter buffers (beware denormals!)
    float f, p, q;  /* feedback, cutoff, resonance */
    float t1, t2;
    unsigned int i;

    float *out_low = mod->getArg(node, args[OUT_LOW])->allocate(windowlen);
    float *out_high = mod->getArg(node, args[OUT_HIGH])->allocate(windowlen);
    float *out_band = mod->getArg(node, args[OUT_BANDPASS])->allocate(windowlen);

    inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
    b0 = (*inout_buffer)[0];
    b1 = (*inout_buffer)[1];
    b2 = (*inout_buffer)[2];
    b3 = (*inout_buffer)[3];
    b4 = (*inout_buffer)[4];
    buffer = inout_buffer->allocate(5);

    /* Feedback state: one non-finite input is read back for ever after, so
       start over rather than stay dead for the life of the note. */
    if (!thIsFinite(b0) || !thIsFinite(b1) || !thIsFinite(b2) ||
        !thIsFinite(b3) || !thIsFinite(b4))
    {
        b0 = b1 = b2 = b3 = b4 = 0;
    }

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_cutoff = mod->getArg(node, args[IN_CUTOFF]);
    in_res = mod->getArg(node, args[IN_RES]);

    for(i = 0; i < windowlen; i++) {
        float frequency = thClampArg((*in_cutoff)[i], 0.0f, FMAX);
        float res = thClampArg((*in_res)[i], 0.0f, QMAX);
        float in = (*in_arg)[i] / TH_MAX;

        // Set coefficients given frequency & resonance [0.0...1.0]
        q = 1.0f - frequency;
        p = frequency + 0.8f * frequency * q;
        f = p + p - 1.0f;
        q = res* (1.0f + 0.5f * q * (1.0f - q + 5.6f * q * q));

        // Filter (in [-1.0...+1.0])
        in -= q * b4;                          //feedback
        t1 = b1;  b1 = (in + b0) * p - b1 * f;
        t2 = b2;  b2 = (b1 + t1) * p - b2 * f;
        t1 = b3;  b3 = (b2 + t2) * p - b3 * f;
        b4 = (b3 + t1) * p - b4 * f;
        b4 = thClampMag(b4, BMAX);             //see BMAX: keep the clip soft
        b4 = b4 - b4 * b4 * b4 * 0.166667f;    //clipping
        b0 = in;

        out_low[i] = b4 * TH_MAX;
        out_high[i] = (in - b4) * TH_MAX;
        out_band[i] = 3.0f * (b3 - b4) * TH_MAX;
    }

    buffer[0] = b0;
    buffer[1] = b1;
    buffer[2] = b2;
    buffer[3] = b3;
    buffer[4] = b4;

    return 0;
}
