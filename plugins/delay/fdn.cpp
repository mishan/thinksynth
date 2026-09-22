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

/* A feedback delay network: a reverb whose tail does not ring.
 *
 * Eight delay lines of prime lengths, read together, mixed through an
 * 8x8 Hadamard matrix and written back. The matrix is the whole idea. A
 * comb bank is a set of lines each fed back into itself, so each one is
 * a resonance at its own pitch, and a long decay is those pitches
 * standing out of the tail -- which is why fx/hall.dsp stops at 0.96.
 * Here every line feeds every other line on every pass, with a sign
 * pattern chosen so that nothing is lost: the Hadamard matrix scaled by
 * 1/sqrt(8) is orthogonal, so the energy that comes out of the eight
 * lines is the energy that goes back into them. One echo becomes eight,
 * eight become thirty-six distinct arrival times, and so on, and there is
 * no loop in the network that is a single line's alone to ring at.
 *
 * DECAY IS A TIME, NOT A FRACTION. Each line's own gain is worked out
 * from its length so that the round trip loses sixty decibels in `decay'
 * seconds:
 *
 *     g = 10^(-3 * length / (decay * rate))
 *
 * A long line loses more per pass than a short one and they all fall at
 * the same rate, which is what lets `decay' be a number of seconds that
 * the tail actually lasts. Sixty seconds is a gain of 0.995 a pass, still
 * inside the unit circle, and the orthogonal matrix is what keeps a gain
 * that near one from turning into a pitch.
 *
 * DAMPING is a one-pole low-pass on each line, in the loop, so the top
 * of the tail dies faster than its middle the way air and walls make it
 * do. Every pass goes through it again, twenty-odd a second, so it is
 * strong: at 4 kHz a 2 kHz tone loses a decibel a pass and falls sixty
 * in under three seconds whatever `decay' says. A long bright tail wants
 * it high. At zero, or at or above Nyquist, it is not there at all.
 *
 * MODULATION is what keeps a long tail from settling. Each line's read
 * position swings `mod' samples either side of its length at `rate',
 * every line a different eighth of the cycle, so the network's modes
 * wander by a fraction of a hertz and none of them stays put long enough
 * to be heard as a note.
 *
 * READ BETWEEN SAMPLES BY AN ALLPASS, not by the straight line
 * delay::chorus draws, and the reason is the loop. Linear interpolation
 * halfway between two samples is a low-pass -- a cosine that is zero at
 * Nyquist -- and a chorus hears it once. Here every pass goes through it
 * again, twenty-odd passes a second: read linearly with the lines
 * swinging twelve samples, a thirty-second tail has 8 kHz thirty
 * decibels under 250 Hz after one second. A first-order
 * allpass reads the same fraction with a flat magnitude,
 *
 *     y[n] = eta * x[n - m] + x[n - m - 1] - eta * y[n - 1],
 *     eta  = (1 - f) / (1 + f),
 *
 * for a delay of m + f. The fraction is kept between a half and one and
 * a half, where eta stays well inside the unit circle, which also means a
 * whole-sample delay is read as f = 1, eta = 0 and y[n] = x[n - m - 1]
 * exactly: at `mod = 0' the network is the static one to the bit.
 *
 * DIFFUSION comes first. Four of Schroeder's allpasses in series on the
 * input, a few milliseconds each, smear an impulse into a burst before
 * it reaches the lines -- without them the first hundred milliseconds
 * are the eight line lengths arriving one at a time, which is eight
 * slaps. `diffuse' is their gain; at zero they are plain short delays.
 *
 * TWO OUTPUTS from one network. `out' and `out2' read all eight lines
 * through two different rows of the same Hadamard matrix, which are
 * orthogonal to each other: the two sides carry the same tail with no
 * correlation between them, which is what a stereo reverb is. Both are
 * the lines' own output, so neither has the dry signal in it; the graph
 * mixes that in.
 *
 * LEVEL. The network loses nothing it is not told to, so a steady input
 * comes out louder the longer the decay: the tail is every pass of the
 * input still sounding: its power is 1 / (1 - g^2) of the input's, which
 * measures six decibels over at two seconds and twenty at sixty. That
 * is what a reverb is, and a graph that runs one long sets its wet level
 * down to suit.
 *
 * SHIMMER is a pitch shift inside the loop. A third row of the matrix
 * reads the lines into delay::pitchshift's two heads (plugins/delay/
 * shifter.h), `interval' times faster than they are written, and
 * `shimmer' of what they read goes back into the input with the dry
 * signal -- so the tail goes up an interval each time round, and the
 * copy of the copy goes up again. The damping low-pass is on that path
 * too, ahead of the heads: it darkens each interval up further than the
 * last, and it is the shifter's anti-alias filter, since heads reading at
 * twice the speed fold everything above a quarter of the rate back down.
 * An octave is the pad of the eighties; a fifth and a fourth down are the
 * other two everybody reaches for. It has to be in the node: a graph may
 * not hold a cycle, so the tail cannot be taken out, shifted and put back
 * by nodes.
 *
 * And a high-pass at FDN_SHIMMER_LOW, because the one frequency a shift
 * cannot move is zero. Twice nothing is nothing, so whatever DC or
 * near-DC the tail carries comes back where it was, and there it meets
 * the network at a mode rather than on average: a steady frequency on a
 * mode is gained 1/(1 - g), not 1/sqrt(1 - g^2), which at a minute's
 * decay is two hundred times rather than ten. Without the high-pass the
 * loop runs away at DC within seconds at any `shimmer'.
 *
 * The shifted copy goes back in scaled by sqrt(1 - g^2) for the average
 * line gain -- the reciprocal of what the network adds to a steady input,
 * see LEVEL -- so that `shimmer' is the share of the tail's power that
 * returns shifted whatever the decay: the loop's power gain is
 * shimmer^2, so each interval up comes back quieter than the last and the
 * whole of it dies away, at any decay and with no damping at all.
 *
 * `size' scales every length together -- lines and diffusers -- and each
 * scaled length is moved up to the next prime, so the lines stay
 * mutually prime at any size. The lines are allocated for the largest
 * size the node allows and never again, so moving `size' mid-note moves
 * the read heads over what is already in them rather than starting from
 * silence; it jumps, and a graph that wants to sweep it should not.
 *
 * Deterministic: the lines' modulation phases are fixed eighths, and
 * nothing here is random.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"
#include "shifter.h"

enum {IN_ARG, IN_SIZE, IN_DECAY, IN_DAMPING, IN_MOD, IN_RATE, IN_DIFFUSE,
      IN_SHIMMER, IN_INTERVAL, OUT_ARG, OUT_ARG2, INOUT_BUFFER, INOUT_STATE};
int args[INOUT_STATE + 1];

static const char desc[] = "Feedback delay network (eight lines, a reverb)";
thPlugin::State    mystate = thPlugin::ACTIVE;

#define FDN_LINES 8
#define FDN_DIFFUSERS 4

/* At 44.1 kHz and `size = 1': 25 to 62 milliseconds, primes, spread so
   that no two are near a simple ratio. Sorted, which is what lets the
   rounding to primes below keep them distinct. */
static const unsigned int lineBase[FDN_LINES] = {
    1103, 1301, 1531, 1759, 1999, 2239, 2477, 2729
};

/* 3.2, 2.4, 8.6 and 6.3 milliseconds: short enough to be heard as one
   smeared arrival and not as echoes, and prime for the lines' reason. */
static const unsigned int diffBase[FDN_DIFFUSERS] = { 139, 107, 379, 277 };

/* How far `size' may go. The lines are allocated for the top of it, so
   it is also what the node costs: eight lines of 2729 * 4 samples at
   44.1 kHz is a third of a megabyte. */
#define FDN_SIZE_MIN 0.1f
#define FDN_SIZE_MAX 4.0f

/* A decay of zero is a division by zero, and one past a quarter of an
   hour is a gain so near one that float can no longer tell them apart. */
#define FDN_DECAY_MIN 0.01f
#define FDN_DECAY_MAX 1000.0f

/* Samples. A read swinging `mod' samples at `rate' is at most
   2 pi * mod * rate / (sample rate) out of tune, so these are generous:
   at 32 samples and 5 Hz a line is forty cents out, which is a warble
   and no longer a room. */
#define FDN_MOD_MAX 64.0f
#define FDN_RATE_MAX 20.0f

/* Just inside the allpasses' own stability, for delay::allpass's reason. */
#define FDN_DIFFUSE_MAX 0.9f

/* The shimmer: how much may come back, the widest interval it may shift
   by, and the window its heads travel. Eighty milliseconds wraps an
   octave up twelve times a second, which is the warble the sound is
   known by and slow enough not to be a buzz. */
#define FDN_SHIMMER_MAX 0.9f
#define FDN_INTERVAL_MAX 4.0f
#define FDN_SHIFT_WINDOW 0.08f
#define FDN_SHIMMER_LOW 80.0f

/* Where each piece of the state lives, in INOUT_STATE. */
enum {
    S_HEAD,                     /* the lines' write position */
    S_DHEAD,                    /* the diffusers' write position */
    S_PHASE,                    /* the modulation LFO, in turns */
    S_SIZE,                     /* the size the lengths below are for */
    S_DECAY,                    /* the decay the gains below are for */
    S_DAMPING,                  /* the damping the coefficient is for */
    S_DAMPA,                    /* the one-pole's coefficient */
    S_LEN,                      /* FDN_LINES lengths, in samples */
    S_GAIN = S_LEN + FDN_LINES, /* FDN_LINES per-pass gains */
    S_LP = S_GAIN + FDN_LINES,  /* FDN_LINES low-pass histories */
    S_AP = S_LP + FDN_LINES,    /* FDN_LINES interpolators' last outputs */
    S_DLEN = S_AP + FDN_LINES,  /* FDN_DIFFUSERS lengths */
    S_SAT = S_DLEN + FDN_DIFFUSERS, /* the shimmer ring's write position */
    S_SPHASE,                   /* its heads' phase, in windows */
    S_SLP,                      /* its low-pass history */
    S_SHPX,                     /* its high-pass's last input */
    S_SHPY,                     /* and last output */
    S_SHIFTED,                  /* what the heads last read */
    S_NORM,                     /* sqrt(1 - g^2), for the average gain */
    S_COUNT
};

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
    args[IN_SIZE] = plugin->regArg("size", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SIZE],
                       "How big the space is: every line's length, scaled "
                       "together");
    plugin->setArgRange(args[IN_SIZE], 0.25f, 3.0f);
    plugin->setArgDefault(args[IN_SIZE], 1);
    args[IN_DECAY] = plugin->regArg("decay", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DECAY],
                       "How long the tail takes to fall sixty decibels");
    plugin->setArgUnits(args[IN_DECAY], "seconds");
    plugin->setArgRange(args[IN_DECAY], 0.1f, 100.0f);
    plugin->setArgDefault(args[IN_DECAY], 2);
    args[IN_DAMPING] = plugin->regArg("damping", thPlugin::ARG_IN);
    /* A low-pass on each line, inside the loop, so it is not a tone
       control on the tail but how much faster the top of it dies: every
       pass goes through it again. */
    plugin->setArgDesc(args[IN_DAMPING],
                       "Where each line's low-pass sits, the tail above it "
                       "dying faster; 0 is none");
    plugin->setArgUnits(args[IN_DAMPING], "Hz");
    plugin->setArgRange(args[IN_DAMPING], 0, 20000);
    args[IN_MOD] = plugin->regArg("mod", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_MOD],
                       "How far each line's read position swings; what "
                       "keeps a long tail from ringing");
    plugin->setArgUnits(args[IN_MOD], "samples");
    plugin->setArgRange(args[IN_MOD], 0, 32);
    args[IN_RATE] = plugin->regArg("rate", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RATE], "How fast the read positions swing");
    plugin->setArgUnits(args[IN_RATE], "Hz");
    plugin->setArgRange(args[IN_RATE], 0, 5);
    args[IN_DIFFUSE] = plugin->regArg("diffuse", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DIFFUSE],
                       "The input allpasses' gain: how far an attack is "
                       "smeared before the lines hear it");
    plugin->setArgRange(args[IN_DIFFUSE], 0, FDN_DIFFUSE_MAX);
    args[IN_SHIMMER] = plugin->regArg("shimmer", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SHIMMER],
                       "How much of the tail goes back in shifted by "
                       "`interval'; 0 is a plain reverb");
    plugin->setArgRange(args[IN_SHIMMER], 0, FDN_SHIMMER_MAX);
    args[IN_INTERVAL] = plugin->regArg("interval", thPlugin::ARG_IN);
    /* A ratio, as delay::pitchshift's `ratio' is. A zero is the arg left
       unwritten -- a shimmer that stops the tape is not one anybody
       means -- and is the octave. */
    plugin->setArgDesc(args[IN_INTERVAL],
                       "The ratio the shimmer shifts by each time round: 2 "
                       "is an octave up");
    plugin->setArgRange(args[IN_INTERVAL], 0.25f, FDN_INTERVAL_MAX);
    plugin->setArgDefault(args[IN_INTERVAL], 2);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    /* No range: the tail's level grows with its length -- see the head. */
    plugin->setArgDesc(args[OUT_ARG], "The tail, one side");
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_ARG2] = plugin->regArg("out2", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG2],
                       "The same tail, uncorrelated with `out'");
    plugin->setArgUnits(args[OUT_ARG2], "full scale");

    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

static bool isPrime (unsigned int n)
{
    if (n < 2)
        return false;

    for (unsigned int d = 2; d * d <= n; d++)
        if (n % d == 0)
            return false;

    return true;
}

/* The first prime at or above `n'. */
static unsigned int primeFrom (unsigned int n)
{
    while (!isPrime(n))
        n++;

    return n;
}

/* One ring's capacity, for the longest length `base' can be scaled to:
   the next prime above base * FDN_SIZE_MAX is under a hundred further
   on at these sizes, and the modulation and the interpolation need
   their own margin past that. */
static unsigned int capacity (unsigned int base, unsigned int rate,
                              float margin)
{
    return (unsigned int)ceil(base * FDN_SIZE_MAX * (rate / 44100.0)) +
           (unsigned int)margin + 128;
}

/* Every length from `size', sorted and prime, and each line's gain from
   its length and `decay'. Only when either has moved: a prime search per
   sample would be most of what the node costs. */
static void lengths (float *state, float size, unsigned int rate)
{
    unsigned int last = 0;

    for (int l = 0; l < FDN_LINES; l++)
    {
        unsigned int want =
            (unsigned int)lrint(lineBase[l] * size * (rate / 44100.0));

        if (want <= last)
            want = last + 1;

        last = primeFrom(want);
        state[S_LEN + l] = (float)last;
    }

    for (int d = 0; d < FDN_DIFFUSERS; d++)
    {
        unsigned int want =
            (unsigned int)lrint(diffBase[d] * size * (rate / 44100.0));

        state[S_DLEN + d] = (float)primeFrom(want < 2 ? 2 : want);
    }

    state[S_SIZE] = size;
}

static void gains (float *state, float decay, unsigned int rate)
{
    double squares = 0;

    for (int l = 0; l < FDN_LINES; l++)
    {
        state[S_GAIN + l] =
            (float)pow(10.0, -3.0 * state[S_LEN + l] / (decay * rate));
        squares += (double)state[S_GAIN + l] * state[S_GAIN + l];
    }

    state[S_NORM] = (float)sqrt(1.0 - squares / FDN_LINES);
    state[S_DECAY] = decay;
}

/* The 8x8 Hadamard matrix, as the fast transform: three rounds of sums
   and differences, and the 1/sqrt(8) that makes it orthogonal. */
static void hadamard (float *x)
{
    for (int h = 1; h < FDN_LINES; h *= 2)
        for (int i = 0; i < FDN_LINES; i += 2 * h)
            for (int j = i; j < i + h; j++)
            {
                const float a = x[j], b = x[j + h];

                x[j] = a + b;
                x[j + h] = a - b;
            }

    for (int i = 0; i < FDN_LINES; i++)
        x[i] *= (float)(1.0 / sqrt((double)FDN_LINES));
}

/* Row `row' of the same matrix, at column `col': the sign the transform
   above gives it. */
static float sign (int row, int col)
{
    return (__builtin_popcount((unsigned)(row & col)) & 1) ? -1.0f : 1.0f;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out, *out2;
    float *buffer, *state;
    thArg *in_arg, *in_size, *in_decay, *in_damping, *in_mod, *in_rate;
    thArg *in_diffuse, *in_shimmer, *in_interval;
    thArg *out_arg, *out_arg2;
    thArg *inout_buffer, *inout_state;
    unsigned int i;
    unsigned int head, dhead, sat;
    float phase, sphase;

    const unsigned int lineCap = capacity(lineBase[FDN_LINES - 1], samples,
                                          FDN_MOD_MAX);
    const unsigned int diffCap = capacity(diffBase[2], samples, 0);
    const float shiftWindow = FDN_SHIFT_WINDOW * samples;
    const unsigned int shiftCap = (unsigned int)shiftWindow + 4;
    const float hpPole =
        (float)(1.0 / (1.0 + 2.0 * M_PI * FDN_SHIMMER_LOW / samples));
    float *shiftRing;

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_size = mod->getArg(node, args[IN_SIZE]);
    in_decay = mod->getArg(node, args[IN_DECAY]);
    in_damping = mod->getArg(node, args[IN_DAMPING]);
    in_mod = mod->getArg(node, args[IN_MOD]);
    in_rate = mod->getArg(node, args[IN_RATE]);
    in_diffuse = mod->getArg(node, args[IN_DIFFUSE]);
    in_shimmer = mod->getArg(node, args[IN_SHIMMER]);
    in_interval = mod->getArg(node, args[IN_INTERVAL]);

    inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
    inout_state = mod->getArg(node, args[INOUT_STATE]);

    /* Every line at one capacity and every diffuser at another, so that
       each group shares a write head and a line's read is just how far
       back it is. Allocated once, at a length that depends only on the
       rate, so it is never handed back empty. */
    buffer = inout_buffer->allocate(FDN_LINES * lineCap +
                                    FDN_DIFFUSERS * diffCap + shiftCap);
    state = inout_state->allocate(S_COUNT);
    shiftRing = buffer + FDN_LINES * lineCap + FDN_DIFFUSERS * diffCap;

    head = (unsigned int)state[S_HEAD];
    dhead = (unsigned int)state[S_DHEAD];
    /* In float and stepped in float, for delay::chorus's reason: a window
       boundary is not an event. */
    phase = state[S_PHASE];
    sat = (unsigned int)state[S_SAT];
    sphase = state[S_SPHASE];

    if (head >= lineCap)
        head = 0;

    if (dhead >= diffCap)
        dhead = 0;

    if (sat >= shiftCap)
        sat = 0;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);
    out_arg2 = mod->getArg(node, args[OUT_ARG2]);
    out2 = out_arg2->allocate(windowlen);

    for (i = 0; i < windowlen; i++)
    {
        /* Neither a space of no size nor a tail of no length is a thing,
           so a zero is the arg left unwritten and means the default. */
        const float size = (*in_size)[i] == 0 ? 1 :
            thClampArg((*in_size)[i], FDN_SIZE_MIN, FDN_SIZE_MAX);
        const float decay = (*in_decay)[i] == 0 ? 2 :
            thClampArg((*in_decay)[i], FDN_DECAY_MIN, FDN_DECAY_MAX);
        /* Zero, and anything at or past Nyquist, is no low-pass at all. */
        const float damping = thClampArg((*in_damping)[i], 0,
                                         (float)samples);
        const float swing = thClampArg((*in_mod)[i], 0, FDN_MOD_MAX);
        const float rate = thClampArg((*in_rate)[i], 0, FDN_RATE_MAX);
        const float diffuse = thClampArg((*in_diffuse)[i], 0,
                                         FDN_DIFFUSE_MAX);
        const float shimmer = thClampArg((*in_shimmer)[i], 0,
                                         FDN_SHIMMER_MAX);
        const float interval = (*in_interval)[i] == 0 ? 2 :
            thClampArg((*in_interval)[i], 0, FDN_INTERVAL_MAX);
        float x = (*in_arg)[i];
        float read[FDN_LINES];
        float l, r;

        if (size != state[S_SIZE])
        {
            lengths(state, size, samples);
            state[S_DECAY] = 0;
        }

        if (decay != state[S_DECAY])
            gains(state, decay, samples);

        /* One minus the pole of a one-pole low-pass at `damping'; at or
           past Nyquist the pole is at zero and the filter is a wire. */
        if (damping != state[S_DAMPING] || state[S_DAMPA] == 0)
        {
            state[S_DAMPA] = (damping == 0 || damping * 2 >= samples)
                ? 1.0f
                : (float)(1.0 - exp(-2.0 * M_PI * damping / samples));
            state[S_DAMPING] = damping;
        }

        if (!thIsFinite(x))
            x = 0;

        /* Last sample's shifted tail, back in with the input. The
           diffusers smear it with everything else, so the shifted copy
           arrives as a wash and not as a second attack. */
        x += shimmer * state[S_NORM] * state[S_SHIFTED];

        /* ---- diffusion: four allpasses in series ---- */
        for (int d = 0; d < FDN_DIFFUSERS; d++)
        {
            float *ring = buffer + FDN_LINES * lineCap + d * diffCap;
            const unsigned int len = (unsigned int)state[S_DLEN + d];
            const float delayed = ring[(dhead + diffCap - len) % diffCap];
            const float v = x + diffuse * delayed;

            ring[dhead] = v;
            x = delayed - diffuse * v;
        }

        /* ---- the lines, read ---- */
        for (int n = 0; n < FDN_LINES; n++)
        {
            const float *ring = buffer + n * lineCap;
            const double turn = (double)phase + (double)n / FDN_LINES;
            const double back = state[S_LEN + n] +
                                swing * sin(2.0 * M_PI * turn);
            /* m + f with f in [0.5, 1.5): the lines are always longer
               than the swing, so m is at least one. */
            const unsigned int whole = (unsigned int)floor(back - 0.5);
            const float frac = (float)(back - whole);
            const float eta = (1 - frac) / (1 + frac);
            const float a = ring[(head + lineCap - whole) % lineCap];
            const float b = ring[(head + lineCap - whole - 1) % lineCap];
            float *last = &state[S_AP + n];

            read[n] = eta * a + b - eta * *last;
            *last = read[n];
        }

        /* ---- two orthogonal rows of the matrix for the two sides ---- */
        l = r = 0;

        for (int n = 0; n < FDN_LINES; n++)
        {
            l += sign(1, n) * read[n];
            r += sign(2, n) * read[n];
        }

        out[i] = l;
        out2[i] = r;

        /* ---- the shimmer's heads, on a third row ---- */
        {
            float feed = 0;

            for (int n = 0; n < FDN_LINES; n++)
                feed += sign(3, n) * read[n];

            if (!thIsFinite(feed))
                feed = 0;

            /* Filtered before the heads, not after: reading at twice the
               speed folds everything above a quarter of the rate back
               down, so the low-pass is the shifter's anti-alias filter as
               well as what stops the climb. The high-pass is a one-pole
               whose zero is at DC exactly. */
            state[S_SHPY] = hpPole * (state[S_SHPY] + feed - state[S_SHPX]);
            state[S_SHPX] = feed;
            state[S_SLP] += state[S_DAMPA] * (state[S_SHPY] - state[S_SLP]);

            if (!(fabsf(state[S_SHPY]) > 1e-30f))
                state[S_SHPY] = 0;

            if (!(fabsf(state[S_SLP]) > 1e-30f))
                state[S_SLP] = 0;

            shiftRing[sat] = state[S_SLP];
            state[S_SHIFTED] = shifterHeads(shiftRing, shiftCap, sat, sphase,
                                            shiftWindow);

            sat = (sat + 1) % shiftCap;
            sphase = shifterStep(sphase, interval, shiftWindow);
        }

        /* ---- damped, decayed, mixed and written back ---- */
        for (int n = 0; n < FDN_LINES; n++)
        {
            float *lp = &state[S_LP + n];

            *lp += state[S_DAMPA] * (read[n] - *lp);

            /* Flushed: a tail that has died away would otherwise spend
               its last minute in denormals, and the network is its own
               input. */
            if (!(fabsf(*lp) > 1e-30f))
                *lp = 0;

            read[n] = *lp * state[S_GAIN + n];
        }

        hadamard(read);

        for (int n = 0; n < FDN_LINES; n++)
        {
            float w = read[n] + x * (float)(1.0 / sqrt((double)FDN_LINES));

            buffer[n * lineCap + head] = thIsFinite(w) ? w : 0;
        }

        head = (head + 1) % lineCap;
        dhead = (dhead + 1) % diffCap;
        phase += rate / (float)samples;

        if (phase >= 1.0f || phase < 0.0f)
            phase -= floorf(phase);
    }

    state[S_HEAD] = (float)head;
    state[S_DHEAD] = (float)dhead;
    state[S_PHASE] = phase;
    state[S_SAT] = (float)sat;
    state[S_SPHASE] = sphase;

    return 0;
}
