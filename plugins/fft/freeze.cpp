/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

/* Spectral freeze: a moment of the input held for as long as `freeze' is
 * up, as a sound that goes on.
 *
 * IT COSTS NOTHING UNTIL IT FREEZES. Nothing needs a spectrum but the
 * moment of capture and the frozen sound, which is made from a held
 * spectrum and reads no input at all. So until `freeze' rises the output is
 * the input, bit for bit and with no latency, and the only work is keeping
 * the last 5120 samples in a ring. Frozen, the work is one inverse FFT a
 * hop, a quarter of `size': about what eight filt::svf cost at 2048, which
 * is what lets an instrument freeze its own voices.
 *
 * THE CAPTURE. Two frames of the ring a hop apart, each through a periodic
 * Hann window and plugins/fft/fft.h, give each bin its magnitude M and its
 * rotation over a hop, z = A conj(B) / |A B|, the phase-vocoder estimate of
 * its true frequency without an atan2. Each bin then keeps a unit phasor P,
 * from A, and each frame turns it by z and by a random angle scaled by
 * `random', renormalized: a phasor cannot drift as a phase summed in float
 * does, so an hour's freeze sounds as its first second did. The frame
 * synthesized is M P, windowed again and overlap-added; a hop of N/4 makes
 * the squared windows sum to 3/2, hence the 2/3.
 *
 * At `random' 0 a steady input -- a held sine, a held chord -- simply
 * carries on, at its own frequency and level, between bins too. At 1 each
 * frame's phases are new and the frames add in power rather than in
 * amplitude, 6 dB down; a gain in `random' (GAIN below, measured) holds the
 * level where it was, on average. Noise frozen at 0 comes out 2.8 to 3 dB
 * down, since neighboring bins' phases disagree: the phase vocoder's
 * phasiness. A sine at a small `random' -- under about 0.2 -- wanders by a
 * few dB over seconds, its few bins drifting against each other slowly
 * enough to be heard as a swell; the table holds its mean.
 *
 * A capture's work, two forward transforms and the four inverse ones that
 * fill the overlap-add, would land in one sample. The two forward ones do,
 * on the rise, since the frames they read are there only then; the inverse
 * ones are spread over the hop after it, a quarter hop apart, and the
 * frozen sound starts a hop late while the dry plays on. A chord whose
 * voices all freeze together then costs no window more than a few of them.
 *
 * WHEN IT CAPTURES. Only on a rise of `freeze' while the output is wholly
 * dry. A rise during the fade back, or a fall and a rise inside the fade
 * in, resumes the sound already held: there is one frozen sound at a time,
 * and replacing it under a fade would be a jump. Before 5120 samples have
 * played the ring is partly silence, and a freeze then holds what there
 * was; an instrument delays the gate.
 *
 * THE FADE between dry and frozen is linear at `random' 0, where frozen is
 * an exact continuation of dry, and equal-power at 1, where the two are
 * unrelated, blended in `random' between.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "thKernel.h"
#include "fft.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thSynthTree.h"
#include "thSynth.h"

enum {IN_ARG, IN_FREEZE, IN_SIZE, IN_RANDOM, IN_FADE, IN_SEED, OUT_ARG, INOUT_STATE};

std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "Spectral freeze: a moment of the input, held";
thPlugin::State    mystate = thPlugin::ACTIVE;

#define MAXN THK_FFT_MAX
#define MAXH (MAXN / 4)
#define BINS (MAXN / 2 + 1)
#define RING (MAXN + MAXH)

/* The longest fade: g steps by 1 / fade in float, and much past this a step
   is under half an ulp of g and the fade stops where it is. 95 s at 44.1k. */
#define FADEMAX 4194304.0f

/* The state, one arg of constant length: thArg zeroes a buffer whose length
   changes, and a zeroed state is a dry one. */
enum {
    S_RINGPOS, S_STAGE, S_G, S_N, S_HOPPOS, S_FRAME, S_HIGH,
    S_SCALARS = 8,
    S_RING = S_SCALARS,
    S_ACC = S_RING + RING,
    S_MAG = S_ACC + MAXN,
    S_ZR = S_MAG + BINS, S_ZI = S_ZR + BINS,
    S_PR = S_ZI + BINS, S_PI = S_PR + BINS,
    S_LEN = S_PI + BINS
};

/* Dry; capturing, the inverse transforms going in through the hop; frozen. */
enum { DRY, PRIMING, FROZEN };

/* The gain in `random' that holds the frozen level to the captured one, at
   each tenth from 0 to 1. Random phases spread a frame's energy across it,
   and frames whose phases no longer agree add in power rather than in
   amplitude. Measured against the level captured, on sines and on noise at
   256 and 2048 over three seeds, the two agree from 0.1 up -- any random
   at all soon scrambles a sine's bins against each other, and from then it
   is as noise is -- and these are their mean. At 0 it is 1, where a steady
   sound carries on exactly and noise is 2.8 dB down at 2048, 3 at 256. */
static const float GAIN[11] = { 1, 1.409f, 1.445f, 1.468f, 1.524f, 1.567f,
                                1.656f, 1.721f, 1.818f, 1.918f, 1.993f };

static float gainAt (float r)
{
    const float x = r * 10;
    const int i = x >= 10 ? 9 : (int)x;

    return GAIN[i] + (GAIN[i + 1] - GAIN[i]) * (x - (float)i);
}

void module_cleanup (thPlugin *plugin)
{
}

void module_prepare (thNode *node, thSynthTree *mod, unsigned int samples,
                     int voice)
{
    if (voice)
        mod->getArg(node, args[INOUT_STATE])->reserve(S_LEN);
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    /* Built here rather than on the audio thread's first capture. */
    thkFftTwiddles();

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");

    args[IN_FREEZE] = plugin->regArg("freeze", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FREEZE],
                       "Over 0.5 holds; a rise from a wholly dry output captures. "
                       "Not finite, it does not hold");
    plugin->setArgRange(args[IN_FREEZE], 0, 1);

    args[IN_SIZE] = plugin->regArg("size", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SIZE],
                       "The frame, read at each capture: larger holds pitch "
                       "cleaner and smears the attack");
    plugin->setArgRange(args[IN_SIZE], 256, MAXN);
    plugin->setArgDefault(args[IN_SIZE], 2048);
    plugin->setArgUnits(args[IN_SIZE], "samples");

    args[IN_RANDOM] = plugin->regArg("random", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_RANDOM], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_RANDOM],
                       "0 carries the held sound on as itself; 1 new phases "
                       "every frame, a shimmer");
    plugin->setArgRange(args[IN_RANDOM], 0, 1);

    args[IN_FADE] = plugin->regArg("fade", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FADE],
                       "The crossfade from dry to frozen and back, a hop to 2^22 samples");
    plugin->setArgUnits(args[IN_FADE], "samples");

    args[IN_SEED] = plugin->regArg("seed", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SEED],
                       "Which random phases: two sides or voices with different "
                       "seeds shimmer apart");

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The input, or what is held, or a fade between");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

/* `size' to the power of two nearest it in octaves, 256 to MAXN; 0 or not a
   number is 2048. */
static int frameSize (float size)
{
    if (!thkFinite(size) || size == 0)
        return 2048;

    int n = 256;

    while (n < MAXN && size > (float)n * 1.41421356f)
        n *= 2;
    return n;
}

/* The periodic Hann window at j of n, from the FFT's own table. */
static inline float hann (const thkFftTable &t, int j, int n)
{
    const int at = (j <= n / 2 ? j : n - j) * (MAXN / n);

    return 0.5f - 0.5f * t.c[at];
}

/* A uniform angle in turns, -0.5 to 0.5, from a seed, a frame and a bin. */
static inline float jitter (uint32_t seed, uint32_t frame, uint32_t k)
{
    uint32_t h = seed * 0x9e3779b1u ^ frame * 0x85ebca77u ^ k * 0xc2b2ae3du;

    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return (float)(h >> 8) * (1.0f / 16777216.0f) - 0.5f;
}

/* One frame of the held sound: every phasor turned a hop and by `random' of
   a jitter, and, when `add', M P through the inverse transform, windowed and
   added to the accumulator at its start. */
static void advance (float *st, int n, float r, uint32_t seed, bool add, float gain)
{
    float *mag = st + S_MAG, *zr = st + S_ZR, *zi = st + S_ZI;
    float *pr = st + S_PR, *pi = st + S_PI, *acc = st + S_ACC;
    const uint32_t frame = (uint32_t)st[S_FRAME];
    float re[BINS], im[BINS], x[MAXN];

    for (int k = 0; k <= n / 2; k++)
    {
        float a = pr[k] * zr[k] - pi[k] * zi[k];
        float b = pr[k] * zi[k] + pi[k] * zr[k];

        if (r > 0)
        {
            const float t = r * jitter(seed, frame, (uint32_t)k);
            const float c = thkSin2pi(t + 0.25f), s = thkSin2pi(t);
            const float a2 = a * c - b * s;

            b = a * s + b * c;
            a = a2;
        }

        const float len = sqrtf(a * a + b * b);

        pr[k] = len > 0 ? a / len : 1;
        pi[k] = len > 0 ? b / len : 0;
        re[k] = mag[k] * pr[k];
        im[k] = mag[k] * pi[k];
    }
    st[S_FRAME] = (float)((frame + 1) & 0xffffff);
    if (!add)
        return;

    thkFftRealInverse(re, im, x, n);

    const thkFftTable &t = thkFftTwiddles();

    for (int j = 0; j < n; j++)
        acc[j] += gain * (2.0f / 3) * hann(t, j, n) * x[j];
}

/* The ring's last n samples ending `back' samples ago, windowed, into a
   spectrum. */
static void analyze (const float *st, int n, int back, float *re, float *im)
{
    const float *ring = st + S_RING;
    const int end = (int)st[S_RINGPOS] - back;
    const thkFftTable &t = thkFftTwiddles();
    float x[MAXN];

    for (int j = 0; j < n; j++)
        x[j] = hann(t, j, n) * ring[((end - n + j) % RING + RING) % RING];
    thkFftReal(x, re, im, n);
}

/* The capture: magnitudes and phasors from the frame ending now, each bin's
   rotation over a hop from the frame a hop before, and the accumulator
   cleared for the frames to come. */
static void capture (float *st, int n)
{
    float *mag = st + S_MAG, *zr = st + S_ZR, *zi = st + S_ZI;
    float *pr = st + S_PR, *pi = st + S_PI;
    float ar[BINS], ai[BINS];

    analyze(st, n, n / 4, pr, pi);      /* B, held in the phasors for now */
    analyze(st, n, 0, ar, ai);          /* A */
    for (int k = 0; k <= n / 2; k++)
    {
        const float m = sqrtf(ar[k] * ar[k] + ai[k] * ai[k]);
        const float br = pr[k], bi = pi[k];
        const float cr = ar[k] * br + ai[k] * bi, ci = ai[k] * br - ar[k] * bi;
        const float d = sqrtf(cr * cr + ci * ci);

        mag[k] = m < 1e-30f ? 0 : m;
        zr[k] = d > 0 ? cr / d : 1;
        zi[k] = d > 0 ? ci / d : 0;
        pr[k] = m > 0 ? ar[k] / m : 1;
        pi[k] = m > 0 ? ai[k] / m : 0;
    }
    memset(st + S_ACC, 0, MAXN * sizeof(float));
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    const thArgReader in(mod->getArg(node, args[IN_ARG])),
                      freeze(mod->getArg(node, args[IN_FREEZE])),
                      size(mod->getArg(node, args[IN_SIZE])),
                      random(mod->getArg(node, args[IN_RANDOM])),
                      fade(mod->getArg(node, args[IN_FADE])),
                      seedArg(mod->getArg(node, args[IN_SEED]));
    float *st = mod->getArg(node, args[INOUT_STATE])->allocate(S_LEN);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    float *ring = st + S_RING, *acc = st + S_ACC;

    /* Only the scalars can go bad, since what goes into the ring is
       guarded; one that is not a number, or out of its range, starts the
       node over, dry. */
    bool bad = false;

    for (int s = 0; s < S_SCALARS; s++)
        bad = bad || !thkFinite(st[s]);
    if (!bad)
    {
        const int sn = (int)st[S_N];

        bad = st[S_RINGPOS] < 0 || st[S_RINGPOS] >= RING || st[S_STAGE] < DRY ||
              st[S_STAGE] > FROZEN || (sn != 0 && frameSize((float)sn) != sn) ||
              st[S_HOPPOS] < 0 || st[S_HOPPOS] >= (sn ? sn : 2048) / 4;
    }
    if (bad)
        memset(st, 0, S_LEN * sizeof(float));

    int pos = (int)st[S_RINGPOS], stage = (int)st[S_STAGE], hop = (int)st[S_HOPPOS];
    int n = st[S_N] > 0 ? (int)st[S_N] : 2048;
    float g = st[S_G];
    bool high = st[S_HIGH] > 0;

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float v = in[i], dry = thkFinite(v) ? v : 0;
        const bool up = thkClamp(freeze[i], 0, 1) > 0.5f;
        const float r = thkClamp(random[i], 0, 1);
        float seedf = thkFinite(seedArg[i]) ? seedArg[i] : 0;
        uint32_t seed;

        if (seedf == 0)
            seedf = 0;      /* -0 too */
        memcpy(&seed, &seedf, sizeof(seed));

        /* The frames end before this sample, so that the frozen sound a hop
           from now is the one that would have been playing. */
        if (stage == DRY && up && !high)
        {
            st[S_RINGPOS] = (float)pos;
            n = frameSize(size[i]);
            capture(st, n);
            advance(st, n, r, seed, false, 1);      /* the frame a hop on */
            stage = PRIMING;
            hop = 0;
        }
        high = up;

        /* Bounded, so that no finite input can carry a frame's magnitudes
           past a float. */
        ring[pos] = thkClampMag(dry, 1e6f);
        pos = (pos + 1) % RING;

        const int h = n / 4;

        if (stage == PRIMING)
        {
            /* The four frames that overlap the first frozen hop, a quarter
               hop apart, each in its place: two hops past the capture, which
               starts two hops before the frozen sound, to five, which starts
               at it. */
            if (hop % (h / 4) == 0)
            {
                const int m = hop / (h / 4);

                advance(st, n, r, seed, true, gainAt(r));
                if (m < 3)
                {
                    memmove(acc, acc + h, (n - h) * sizeof(float));
                    memset(acc + n - h, 0, h * sizeof(float));
                }
            }
            out[i] = dry;
            /* Frozen at g = 0, which a fall meanwhile takes straight back
               to dry. */
            if (++hop == h)
            {
                stage = FROZEN;
                hop = 0;
            }
            continue;
        }

        if (stage == FROZEN)
        {
            const float f = thkClamp(fade[i], (float)h, FADEMAX);

            g = up ? fminf(1, g + 1 / f) : fmaxf(0, g - 1 / f);
            if (g == 0 && !up)
            {
                stage = DRY;
                out[i] = dry;
                continue;
            }

            const float lin = 1 - g, a = thkSin2pi((1 - g) * 0.25f), b = thkSin2pi(g * 0.25f);
            const float dryGain = (1 - r) * lin + r * a, wetGain = (1 - r) * g + r * b;

            out[i] = dryGain * dry + wetGain * acc[hop];
            if (++hop == h)
            {
                memmove(acc, acc + h, (n - h) * sizeof(float));
                memset(acc + n - h, 0, h * sizeof(float));
                advance(st, n, r, seed, true, gainAt(r));
                hop = 0;
            }
            continue;
        }

        out[i] = dry;
    }

    st[S_RINGPOS] = (float)pos;
    st[S_STAGE] = (float)stage;
    st[S_HOPPOS] = (float)hop;
    st[S_N] = (float)n;
    st[S_G] = g;
    st[S_HIGH] = high ? 1 : 0;

    return 0;
}
