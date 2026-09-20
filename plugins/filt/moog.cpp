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

/* How near Nyquist `cutoffhz' may ask for, as a fraction of the rate. The
   conversion below runs on cos(2*pi*hz/rate), which is flat at Nyquist and
   where the arithmetic it feeds degenerates; filt::svf stops at the same
   fraction, for the same kind of reason. What is past it is a filter that
   passes everything, and FMAX is already that. */
#define CUTOFF_CEIL 0.49f

/* The 0 to 1 cutoff whose four-pole response is 3 dB down at `hz'.
 *
 * Why this is not a division. The fit's `cutoff' is not a fraction of
 * anything measurable: each of the four stages is y = (x + x[-1])*p - y[-1]*f
 * with p = 1.8c - 0.8c^2 and f = 2p - 1, and what a listener calls the cutoff
 * is where all four together have lost 3 dB -- which is a long way below
 * where one of them has. A `cutoff' of 0.18, which is what dsp/ladder.dsp
 * shipped with, is 2564 Hz at 44.1k and not the 3969 Hz that reading it as a
 * fraction of Nyquist would suggest. So the mapping is derived rather than
 * assumed, and it is exact:
 *
 *   one stage is H(z) = p(1 + z^-1) / (1 + f z^-1), so with c = cos(w),
 *
 *       |H|^2 = 2p^2 (1 + c) / (1 + f^2 + 2 f c)
 *
 *   the cascade is 3 dB down where |H|^8 = 1/2, so where |H|^2 is K below.
 *   Substituting f = 2p - 1 and u = 1 - c leaves a quadratic in p,
 *
 *       (2 - 2K - u) p^2 + 2K u p - K u = 0
 *
 *   and p = 1.8c - 0.8c^2 is a second one in the cutoff. Two roots to pick
 *   and both are the smaller.
 *
 * The first is taken in the form that does not cancel: its leading
 * coefficient passes through zero at about a fifth of the rate -- 5760 Hz at
 * 44.1k, squarely inside the range anything asks for -- where the usual
 * spelling is 0/0.
 *
 * Measured against a swept response afterwards rather than trusted: asking
 * for 40, 250, 1000, 4000 and 20000 Hz puts the 3 dB point within a hertz of
 * each, which is what statecheck holds it to. Resonance moves the peak, as
 * it does in every filter here and in filt::svf's hertz cutoff too; this is
 * the design frequency, not a promise about the peak. */
static float moogCutoffFromHz (float hz, unsigned int rate)
{
    /* 2^(-1/4): one stage's |H|^2 where the four of them are 3 dB down. */
    static const double K = 0.8408964152537145;

    const double top = (double)rate * (double)CUTOFF_CEIL;
    double w, u, A, B, C, disc, p;

    /* Written so a NaN takes this branch: it is `no cutoff in hertz', which
       is what a 0 on this arg already means. */
    if (!(hz > 0))
        return 0;

    if (!((double)hz < top))
        hz = (float)top;

    w = 2.0 * M_PI * (double)hz / (double)rate;
    u = 1.0 - cos(w);

    A = 2.0 - 2.0 * K - u;
    B = 2.0 * K * u;
    C = -K * u;

    /* Positive for every w the ceiling allows, reaching zero only at
       Nyquist itself. */
    disc = B * B - 4.0 * A * C;

    if (disc < 0)
        disc = 0;

    p = 2.0 * C / (-B - sqrt(disc));

    /* p is 1 at Nyquist and the radicand below is 0.04 there. Clamped so
       that a last ulp the other side of 1 cannot make it negative. */
    if (p > 1.0)
        p = 1.0;
    else if (p < 0.0)
        p = 0.0;

    return (float)((1.8 - sqrt(3.24 - 3.2 * p)) / 1.6);
}

void module_cleanup (thPlugin *plugin)
{
}

enum { INOUT_BUFFER,IN_ARG,IN_CUTOFF,IN_CUTOFFHZ,IN_RES,
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
    /* Not a fraction of the rate, whatever this said before `cutoffhz'
       existed to measure it against: 0.18 is 2564 Hz at 44.1k, which is a
       seventeenth of the rate and not a fifth. It is the fit's own number and
       there is no shorter true thing to call it. */
    plugin->setArgDesc(args[IN_CUTOFF],
                       "Cutoff, 0 to 1 -- the fit's own scale, not hertz and "
                       "not a fraction of the rate; see `cutoffhz'. Clamped: "
                       "the fit means nothing past 1");
    plugin->setArgRange(args[IN_CUTOFF], 0, FMAX);
    plugin->setArgUnits(args[IN_CUTOFF], "0..1");
    args[IN_CUTOFFHZ] = plugin->regArg("cutoffhz", thPlugin::ARG_IN);
    /* The same cutoff the arg above sets, in the units filt::svf and
       filt::res2pole2 take it in -- and the reason a graph can key-track this
       filter at all, since hertz of pitch is what there is to track with and
       the language has no rate-aware way to spell a fraction of the rate.

       An override rather than a second cutoff summed with the first: two
       spellings of one number, and 0 is "not this one", which is what a rate
       fraction of 0 already meant. No numeric range, for the reason every
       hertz arg in the tree gives. */
    plugin->setArgDesc(args[IN_CUTOFFHZ],
                       "Cutoff in hertz. Overrides `cutoff' when above 0, "
                       "which is what it is unless a graph says otherwise");
    plugin->setArgUnits(args[IN_CUTOFFHZ], "Hz");

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
    thArg *in_arg, *in_cutoff, *in_cutoffhz, *in_res;
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
    in_cutoffhz = mod->getArg(node, args[IN_CUTOFFHZ]);
    in_res = mod->getArg(node, args[IN_RES]);

    /* The hertz cutoff and what it converted to. A cos() and two sqrts a
       sample would cost more than the filter does, and a cutoff usually does
       not move; a NaN never compares equal to itself, so it takes the
       recompute path, where moogCutoffFromHz answers for it. */
    float lastHz = 0, fromHz = 0;
    bool haveHz = false;

    for(i = 0; i < windowlen; i++) {
        const float hz = (*in_cutoffhz)[i];
        float asked;

        if (hz > 0)
        {
            if (!haveHz || hz != lastHz)
            {
                fromHz = moogCutoffFromHz(hz, samples);
                lastHz = hz;
                haveHz = true;
            }

            asked = fromHz;
        }
        else
            asked = (*in_cutoff)[i];

        float frequency = thClampArg(asked, 0.0f, FMAX);
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
