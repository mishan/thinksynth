/*
 * Copyright (C) 2026 The thinksynth authors
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

/* A bank of modes, struck: what filt::bar and its kin share.
 *
 * Struck metal is a set of modes that have nothing to do with each other.
 * Each rings at its own frequency and dies at its own rate, the high ones
 * first, and the ratios between them are what tell a bar from a bell. A
 * material is its ratios and four numbers (thkModal::Material); this file
 * is everything else, so each plugin's .cpp is a table and its words.
 *
 * EACH MODE IS A ROTATION SHRUNK BY r every sample, the coupled form:
 *
 *     u' = r (c u - s v) + x,    v' = r (s u + c v),    out = g v'
 *
 * with c, s the cosine and sine of the mode's step w and r the radius that
 * makes it fall 60 dB in its decay. A click of height A rings it at
 * A g r^n sin(n w): amplitude A g, exactly.
 *
 * The coupled form and not the direct one (y = 2 r cos(w) y1 - r^2 y2)
 * because its state is two floats, which is what a state arg holds, and
 * it holds a low pole in float: the direct form's poles crowd together
 * near DC, so at 20 Hz it needs doubles to stay in tune, and a double
 * rounded to float at each window would make a window of one sample and a
 * window of five hundred disagree. With float coefficients here a 60 s
 * decay comes out 0.2% short at 20 Hz and 0.9% at 100 Hz, and the pitch is
 * exact to a thousandth of a cent.
 *
 * LEVEL. Each mode's gain follows the material's slope plus `tilt' over
 * octaves of its ratio, and the gains are scaled to sum to 1: a click no
 * taller than full scale never rings past it. A longer strike adds up in
 * phase in the low modes, so the only bound is the strike's own sum of
 * |x|; a 2 ms burst rings a 110 Hz mode some 80 times as loud as a click.
 * A patch sets its strike's level with that in mind. The gains are
 * scaled over the modes that ring, so a change to `modes' changes the
 * level of what is still ringing: it is a timbre to set, not one to sweep.
 *
 * ABOVE NYQUIST a mode fades out between 0.40 and 0.45 of the rate and is
 * silent past it, and the others are not rescaled as it goes: a glide
 * across the line neither clicks nor jumps.
 *
 * `play' is the ring's own: 1 for PLAY_HOLD after the node first runs and
 * while a peak follower on the output is above PLAY_FLOOR (-80 dBFS), as
 * filt::pianostring's is. A bell does not stop when the key comes up.
 *
 * COST. The radii again only when `modes', `decay', `damp' or `tilt'
 * moves, and the sines only when `freq' does, so a held note pays them
 * once a window rather than once a sample. Measured with dspbench on a Ryzen 9 5900X, per sample
 * of one voice, against filt::svf's 3.7 ns: 7.0 ns at 1 mode, 7.8 at 8,
 * 10.6 at 16. A `freq' that moves every sample, a vibrato, pays a sine
 * and a cosine a mode a sample: 144 ns at 16 modes, a tenth of a core for
 * sixteen voices. Fewer modes is the way to make that cheaper.
 */

#ifndef TH_PLUGIN_MODAL_H
#define TH_PLUGIN_MODAL_H 1

#include <math.h>
#include <string.h>

#include <algorithm>
#include <atomic>

#include "think.h"

struct thkModal
{
    static constexpr int MODES_MAX = 16;
    static constexpr float DECAY_MIN = 0.001f, DECAY_MAX = 60;
    /* A ratio under 1 with a steep exponent outlasts `decay'; this keeps
       its radius short of 1 in float. */
    static constexpr double MODE_DECAY_MAX = 4 * DECAY_MAX;
    static constexpr double FADE_LO = 0.40, FADE_HI = 0.45;
    static constexpr double PLAY_HOLD = 0.05, PLAY_RELEASE = 0.05;
    static constexpr float PLAY_FLOOR = 1e-4f;
    /* A mode quieter than this is set to 0: a decaying state otherwise
       ends in subnormals, which cost a hundred times a normal multiply. */
    static constexpr float QUIET = 1e-30f;

    struct Material
    {
        const double *ratios;      /* to the mode `freq' names */
        int count;
        int modes;                 /* what a 0 in `modes' means */
        float decay;               /* what a 0 in `decay' means, seconds */
        float d;                   /* how much faster high modes die */
        float slope;               /* dB per octave of ratio */
        const char *freqDesc;
    };

    /* What the controls make of the bank. */
    struct Ctl
    {
        int n;
        float rc[MODES_MAX], rs[MODES_MAX], g[MODES_MAX];
    };

    /* The radius and gain of each mode, from what does not move with
       pitch. */
    static void shape (const Material &m, int n, float decay, float damp,
                       float tilt, double rate, double r[], double g[])
    {
        const double e = std::max(0.0, (double)m.d + damp);
        double sum = 0;

        for (int k = 0; k < n; k++)
        {
            const double t = std::min(MODE_DECAY_MAX,
                                      decay / pow(m.ratios[k], e));

            r[k] = pow(10.0, -3.0 / (t * rate));
            g[k] = pow(10.0, (m.slope + tilt) * log2(m.ratios[k]) / 20.0);
            sum += g[k];
        }
        for (int k = 0; k < n; k++)
            g[k] /= sum;
    }

    /* The rotation and the fade of each mode at `freq', folded into the
       radius and gain. */
    static void tune (const Material &m, int n, float freq, double rate,
                      const double r[], const double g[], Ctl &c)
    {
        c.n = n;
        for (int k = 0; k < n; k++)
        {
            const double at = freq * m.ratios[k] / rate;
            const double w = 2 * M_PI * at;
            const double fade = at <= FADE_LO ? 1 : at >= FADE_HI ? 0
                              : (FADE_HI - at) / (FADE_HI - FADE_LO);

            c.rc[k] = (float)(r[k] * cos(w));
            c.rs[k] = (float)(r[k] * sin(w));
            c.g[k] = (float)(g[k] * fade);

            /* Rounding to float can put a radius a hair from 1 on it or
               past it, where the mode would ring for ever or grow. */
            while ((double)c.rc[k] * c.rc[k] + (double)c.rs[k] * c.rs[k] >= 1)
            {
                c.rc[k] = nextafterf(c.rc[k], 0);
                c.rs[k] = nextafterf(c.rs[k], 0);
            }
        }
    }

    /* One sample through the bank. */
    static float tick (const Ctl &c, float x, float u[], float v[])
    {
        float out = 0;

        for (int k = 0; k < c.n; k++)
        {
            const float nu = c.rc[k] * u[k] - c.rs[k] * v[k] + x;
            const float nv = c.rs[k] * u[k] + c.rc[k] * v[k];

            if (nu * nu + nv * nv < QUIET)
                u[k] = v[k] = 0;
            else
            {
                u[k] = nu;
                v[k] = nv;
            }
            out += c.g[k] * v[k];
        }
        return out;
    }
};

/* The plugin around the kernel, the same for every material. */
enum { MODAL_IN, MODAL_FREQ, MODAL_MODES, MODAL_DECAY, MODAL_DAMP, MODAL_TILT,
       MODAL_OUT, MODAL_PLAY, MODAL_LAST, MODAL_ARGS };

/* The state arg: u and v of each mode, then the follower's age and peak. */
enum { MODAL_AGE = 2 * thkModal::MODES_MAX, MODAL_PEAK, MODAL_STATE };

static inline void thModalInit (thPlugin *plugin, std::atomic<int> args[],
                                const thkModal::Material &m)
{
    args[MODAL_IN] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[MODAL_IN], "The strike: a click, a burst of "
                       "noise, a mallet. A longer strike rings louder");
    plugin->setArgRange(args[MODAL_IN], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[MODAL_IN], "full scale");

    args[MODAL_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[MODAL_FREQ], TH_SMOOTH_MS);
    plugin->setArgDesc(args[MODAL_FREQ], m.freqDesc);
    plugin->setArgUnits(args[MODAL_FREQ], "Hz");

    args[MODAL_MODES] = plugin->regArg("modes", thPlugin::ARG_IN);
    plugin->setArgDesc(args[MODAL_MODES], "How many modes ring, lowest "
                       "first; fewer is darker and cheaper");
    plugin->setArgRange(args[MODAL_MODES], 1, m.count);
    plugin->setArgStep(args[MODAL_MODES], 1);
    plugin->setArgDefault(args[MODAL_MODES], m.modes);

    args[MODAL_DECAY] = plugin->regArg("decay", thPlugin::ARG_IN);
    /* Seconds as a bare number, as filt::pianostring's decay: a `s' or
       `ms' suffix is folded to samples before the plugin sees it. */
    plugin->setArgDesc(args[MODAL_DECAY], "Seconds for the `freq' mode to "
                       "fall 60 dB, as a bare number: `8 s' would be samples");
    plugin->setArgUnits(args[MODAL_DECAY], "seconds");
    plugin->setArgRange(args[MODAL_DECAY], thkModal::DECAY_MIN,
                        thkModal::DECAY_MAX);
    plugin->setArgDefault(args[MODAL_DECAY], m.decay);

    args[MODAL_DAMP] = plugin->regArg("damp", thPlugin::ARG_IN);
    plugin->setArgDesc(args[MODAL_DAMP], "How much faster the high modes "
                       "die than the material's own; 0 is the material");
    plugin->setArgRange(args[MODAL_DAMP], -1, 1);

    args[MODAL_TILT] = plugin->regArg("tilt", thPlugin::ARG_IN);
    plugin->setArgDesc(args[MODAL_TILT], "Brighter or darker than the "
                       "material struck, per octave of mode; 0 is the "
                       "material");
    plugin->setArgRange(args[MODAL_TILT], -24, 24);
    plugin->setArgUnits(args[MODAL_TILT], "dB/oct");

    args[MODAL_OUT] = plugin->regArg("out", thPlugin::ARG_OUT);
    /* No range: a strike longer than a click rings past its own height. */
    plugin->setArgDesc(args[MODAL_OUT], "The modes summed");
    plugin->setArgUnits(args[MODAL_OUT], "full scale");

    args[MODAL_PLAY] = plugin->regArg("play", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[MODAL_PLAY], "1 while the ring is heard, for "
                       "the io node's play");
    plugin->setArgRange(args[MODAL_PLAY], 0, 1);

    args[MODAL_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);
}

static inline int thModalCallback (thNode *node, thSynthTree *mod,
                                   unsigned int windowlen, unsigned int samples,
                                   std::atomic<int> args[],
                                   const thkModal::Material &m)
{
    float *out = mod->getArg(node, args[MODAL_OUT])->allocate(windowlen);
    float *play = mod->getArg(node, args[MODAL_PLAY])->allocate(windowlen);
    thArg *in_arg = mod->getArg(node, args[MODAL_IN]);
    thArg *in_freq = mod->getArg(node, args[MODAL_FREQ]);
    thArg *in_modes = mod->getArg(node, args[MODAL_MODES]);
    thArg *in_decay = mod->getArg(node, args[MODAL_DECAY]);
    thArg *in_damp = mod->getArg(node, args[MODAL_DAMP]);
    thArg *in_tilt = mod->getArg(node, args[MODAL_TILT]);
    thArg *inout_last = mod->getArg(node, args[MODAL_LAST]);
    const double rate = samples;
    const double release = exp(-1.0 / (thkModal::PLAY_RELEASE * rate));
    const double hold = thkModal::PLAY_HOLD * rate;
    float st[MODAL_STATE];

    /* A non-finite state would ring for the life of the note, and one
       non-finite strike would make every mode one. The bank cannot make
       either; this is about what arrives. */
    for (int i = 0; i < MODAL_STATE; i++)
    {
        st[i] = (*inout_last)[i];
        if (!thIsFinite(st[i]))
            st[i] = 0;
    }

    float in[windowlen];

    in_arg->getBuffer(in, windowlen);
    for (unsigned int i = 0; i < windowlen; i++)
        if (!thIsFinite(in[i]))
            in[i] = 0;

    float *u = st, *v = st + thkModal::MODES_MAX;
    float age = st[MODAL_AGE], peak = st[MODAL_PEAK];
    bool quiet = true;

    for (int k = 0; quiet && k < 2 * thkModal::MODES_MAX; k++)
        quiet = st[k] == 0;
    for (unsigned int i = 0; quiet && i < windowlen; i++)
        quiet = in[i] == 0;

    if (quiet)
        memset(out, 0, windowlen * sizeof(float));
    else
    {
        double r[thkModal::MODES_MAX], g[thkModal::MODES_MAX];
        thkModal::Ctl c;
        float lastShape[4], lastFreq = 0;
        int n = 0;

        for (unsigned int i = 0; i < windowlen; i++)
        {
            const float shapeNow[4] = { (*in_modes)[i], (*in_decay)[i],
                                        (*in_damp)[i], (*in_tilt)[i] };
            const float freq = (*in_freq)[i];
            /* memcmp, not ==: a NaN never equals itself and would take the
               recompute every sample; the clamps below answer for it once. */
            const bool reshape = i == 0 ||
                memcmp(shapeNow, lastShape, sizeof shapeNow) != 0;

            if (reshape)
            {
                const float modes = shapeNow[0], decay = shapeNow[1];

                n = modes == 0 ? m.modes
                  : (int)lrintf(thClampArg(modes, 1, (float)m.count));
                /* A mode let go stops: held, it would ring again from
                   where it was when `modes' next rose. */
                for (int k = n; k < thkModal::MODES_MAX; k++)
                    u[k] = v[k] = 0;
                thkModal::shape(m, n, decay == 0 ? m.decay
                            : thClampArg(decay, thkModal::DECAY_MIN,
                                         thkModal::DECAY_MAX),
                      thClampArg(shapeNow[2], -1, 1),
                      thClampArg(shapeNow[3], -24, 24), rate, r, g);
                memcpy(lastShape, shapeNow, sizeof shapeNow);
            }
            if (reshape || memcmp(&freq, &lastFreq, sizeof freq) != 0)
            {
                thkModal::tune(m, n, thClampArg(freq, 0, (float)(rate / 2)),
                               rate, r, g, c);
                lastFreq = freq;
            }
            out[i] = thkModal::tick(c, in[i], u, v);
        }
    }

    for (unsigned int i = 0; i < windowlen; i++)
    {
        peak = std::max(fabsf(out[i]), (float)(peak * release));
        play[i] = (age < hold || peak > thkModal::PLAY_FLOOR) ? 1 : 0;
        if (age < hold)
            age++;
    }
    if (peak < thkModal::PLAY_FLOOR * 1e-3f)
        peak = 0;

    st[MODAL_AGE] = age;
    st[MODAL_PEAK] = peak;

    float *last = inout_last->allocate(MODAL_STATE);

    memcpy(last, st, sizeof st);

    return 0;
}

#endif
