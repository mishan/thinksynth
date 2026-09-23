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

/* A stiff string: a digital waveguide whose partials are stretched the way
 * a piano's are, and whose decay is fitted per note.
 *
 *     y[n] = in[n] + damper * loss(disp(frac(y[n - N])))
 *
 * filt::comb is the same loop with a one-pole in it, and it rings on the
 * harmonic series. A piano string is stiff, and stiffness puts partial n
 * at
 *
 *     f_n = n f0 sqrt(1 + B n^2)          (Fletcher, Blackham, Stratton 1962)
 *
 * with the inharmonicity B from about 1e-4 in the tenor to 1e-2 at the
 * top. The loop rings at the frequencies where its total phase lag is a
 * whole number of turns, so stretching the partials means a loop whose
 * group delay falls with frequency: an allpass cascade inside it. That is
 * the dispersion filter, and it is why this is a plugin and not a graph --
 * a graph may not hold a loop.
 *
 * THE DISPERSION FILTER is designed once per note, from `freq' and `b',
 * in one of two shapes:
 *
 * - In the bass, where the stretch across the band adds up to several
 *   turns of phase, a cascade of second-order allpasses placed along it:
 *   the target's excess phase over a linear one is cut into 2 pi steps and
 *   one pole pair sits at the middle of each, its radius from the spacing
 *   to its neighbors (Abel, Välimäki and Smith, "Robust, efficient design
 *   of allpass filters for dispersive string sound synthesis", IEEE SPL
 *   2010). Up to DISP_MAX sections, which covers A0 to about 2 kHz.
 *
 * - Above, where the excess is under a few turns, DISP_FO identical
 *   first-order allpasses (Van Duyne and Smith, ICMC 1994), their one
 *   coefficient fitted by least squares over the partials below
 *   DISP_FIT_HZ, weighted to the low ones.
 *
 * Measured against the formula above with a realistic B curve, the first
 * eight partials land within about 1.5 cents from A0 to C7, and the next
 * eight within about 5.
 *
 * THE LOSS FILTER is a one-pole, g (1 + a) / (1 + a z^-1), fitted to two
 * decay times: `decay' at the fundamental and `hidecay' at LOSS_HI_HZ
 * (Välimäki et al., JAES 1996). A mode loses |H| once per trip round the
 * loop, and the trip at frequency w is the loop's group delay there, so
 * the per-trip gain for a T60 of t seconds is 10^(-3 tau / (rate t)).
 *
 * TUNING. Every filter in the loop delays the signal, so the delay line
 * is whatever is left of one period at the fundamental once the loss and
 * dispersion filters have taken theirs. The fractional part goes to a
 * first-order Thiran allpass rather than a linear interpolation: the
 * interpolation is a low-pass that changes with the fraction, so the
 * decay would change from key to key. The fraction is solved against the
 * Thiran's own phase, so the fundamental is exact to the float.
 *
 * THE DAMPER. `gate' above 0 is a free string. At 0 the loop takes an
 * extra per-trip loss that brings it down in `damper' seconds, faded in
 * over DAMPER_FADE so it does not click. The engine keeps `trigger' at 2
 * while the sustain pedal holds a released note, so `gate =
 * ionode->trigger' is the pedal too; a key with no damper is a graph that
 * writes `gate = 1'.
 *
 * `play' is the string's own energy: 1 for PLAY_HOLD after the note starts
 * and while the output's peak follower is above PLAY_FLOOR. A voice ends
 * when its string is quiet, not when its key comes up.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

static const char desc[] = "Stiff String (a piano string)";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* Most second-order sections the bass may use. Sixteen reach about 2 kHz
   at A0; eight stop near 1.3 kHz. */
#define DISP_MAX 16

/* The first-order cascade's length. Shortened in the top octaves when the
   sections' delay would not leave the line its minimum. Sixteen rather
   than eight halves the worst error on the first eight partials; it costs
   what eight second-order sections do. */
#define DISP_FO 16

/* The first-order cascade replaces the second-order one below this many
   second-order sections: under four turns of excess phase the staircase
   is too coarse to follow the target. */
#define DISP_BQ_MIN 4

/* The band the dispersion is fitted over. Partials above it keep the
   stretch the band's top ends with. */
#define DISP_FIT_HZ 6000.0

/* Most partials the first-order fit weighs. More than sixteen buys the
   partials above them a cent or two and costs the low ones several. */
#define DISP_FIT_PARTIALS 16

/* The highest `b' accepted: past it a string is a bar. */
#define B_MAX 0.05f

/* Where `hidecay' is measured: here, or at twice the fundamental if that
   is higher. */
#define LOSS_HI_HZ 3000.0

/* The loss filter's pole, at its steepest. */
#define LOSS_POLE_MIN -0.95

/* Decay times, in seconds. */
#define DECAY_MIN 0.01f
#define DECAY_MAX 200.0f
#define DECAY_DEFAULT 10.0f
#define DAMPER_DEFAULT 0.15f

/* How long the damper takes to land, in seconds. */
#define DAMPER_FADE 0.01

/* The lowest note the line is sized for, in hertz, and the shortest line
   left after the filters, in samples: the Thiran wants its fraction in
   [0.5, 1.5) and the integer part at least one. */
#define FREQ_MIN 16.0
#define LINE_MIN 2.0

/* `play': the hold after the note starts, the follower's release, and the
   level below which the string is over (-80 dBFS). */
#define PLAY_HOLD 0.05
#define PLAY_RELEASE 0.05
#define PLAY_FLOOR 1e-4

/* Below this a value is flushed to zero, so a long decay does not end in
   denormals. */
#define FLUSH 1e-30

enum {IN_ARG, IN_FREQ, IN_B, IN_DECAY, IN_HIDECAY, IN_DAMPER, IN_GATE,
      OUT_ARG, OUT_PLAY, INOUT_BUFFER, INOUT_STATE};

int args[INOUT_STATE + 1];

/* Where each piece of the state lives, in INOUT_STATE. */
enum {
    S_POS,                  /* the line's write position */
    S_AGE,                  /* samples since the note began, saturating */
    S_PEAK,                 /* the output's peak follower */
    S_DAMP,                 /* the damper, 0 off to 1 down */
    S_THIRAN,               /* the Thiran's state */
    S_LOSS,                 /* the loss filter's last output */
    S_FREQ,                 /* the inputs the design below is for */
    S_B,
    S_DECAY,
    S_HIDECAY,
    S_DAMPER,
    S_KIND,                 /* 0 no dispersion, 1 first-order, 2 second */
    S_SECTIONS,             /* how many sections */
    S_LINE,                 /* the line's integer delay */
    S_ETA,                  /* the Thiran's coefficient */
    S_G,                    /* the loss filter's gain */
    S_POLE,                 /* and pole */
    S_DGAIN,                /* the damper's per-trip gain */
    S_C1,                   /* DISP_MAX first coefficients */
    S_C2 = S_C1 + DISP_MAX, /* DISP_MAX second coefficients */
    S_Z1 = S_C2 + DISP_MAX, /* DISP_MAX first states */
    S_Z2 = S_Z1 + DISP_MAX, /* DISP_MAX second states */
    S_COUNT = S_Z2 + DISP_MAX
};

/* Everything the design produces, in double until it is stored. */
struct Design {
    int kind, sections, line;
    double eta, g, pole, dgain;
    double c1[DISP_MAX], c2[DISP_MAX];
};

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    /* Added into the loop, so it is the force on the string: a hammer, a
       noise burst, a wav. */
    plugin->setArgDesc(args[IN_ARG], "Excitation");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");

    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    /* Read once a window: a change redesigns the loop, which is a note's
       work and not a sample's. */
    plugin->setArgDesc(args[IN_FREQ],
                       "The fundamental; read once per window");
    plugin->setArgUnits(args[IN_FREQ], "Hz");

    args[IN_B] = plugin->regArg("b", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_B],
                       "Inharmonicity: partial n at n f0 sqrt(1 + b n^2); "
                       "0 is a harmonic string");
    plugin->setArgRange(args[IN_B], 0, B_MAX);

    args[IN_DECAY] = plugin->regArg("decay", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DECAY],
                       "How long the fundamental takes to fall sixty "
                       "decibels");
    plugin->setArgUnits(args[IN_DECAY], "seconds");
    plugin->setArgRange(args[IN_DECAY], DECAY_MIN, DECAY_MAX);
    plugin->setArgDefault(args[IN_DECAY], DECAY_DEFAULT);

    args[IN_HIDECAY] = plugin->regArg("hidecay", thPlugin::ARG_IN);
    /* 0 is the same as `decay': every partial dies together, the tube
       filt::comb is at `damp = 0'. */
    plugin->setArgDesc(args[IN_HIDECAY],
                       "The same for the partials near 3 kHz; 0 is "
                       "`decay'");
    plugin->setArgUnits(args[IN_HIDECAY], "seconds");
    plugin->setArgRange(args[IN_HIDECAY], DECAY_MIN, DECAY_MAX);

    args[IN_DAMPER] = plugin->regArg("damper", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DAMPER],
                       "How long the string takes to fall sixty decibels "
                       "with the damper down");
    plugin->setArgUnits(args[IN_DAMPER], "seconds");
    plugin->setArgRange(args[IN_DAMPER], DECAY_MIN, DECAY_MAX);
    plugin->setArgDefault(args[IN_DAMPER], DAMPER_DEFAULT);

    args[IN_GATE] = plugin->regArg("gate", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_GATE],
                       "Above 0 the string is free; at 0 the damper is "
                       "down");
    plugin->setArgRange(args[IN_GATE], 0, 2);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    /* No range: a string driven at its own pitch builds up past its
       input. */
    plugin->setArgDesc(args[OUT_ARG], "The string");
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    args[OUT_PLAY] = plugin->regArg("play", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_PLAY],
                       "1 while the string is still sounding");
    plugin->setArgRange(args[OUT_PLAY], 0, 1);

    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

/* Phase lag, in radians at w, of the first-order allpass
   (a + z^-1) / (1 + a z^-1). Its delay at DC is (1 - a) / (1 + a). */
static double lagFirst (double a, double w)
{
    return w - 2.0 * atan2(a * sin(w), 1.0 + a * cos(w));
}

/* The same for the second-order allpass with poles at r e^(+-j t). Each
   factor's angle stays inside a half turn because r < 1, so the sum needs
   no unwrapping. */
static double lagSecond (double r, double t, double w)
{
    return 2.0 * w +
           2.0 * (atan2(-r * sin(t - w), 1.0 - r * cos(t - w)) +
                  atan2(r * sin(t + w), 1.0 - r * cos(t + w)));
}

/* And of the loss filter g (1 + a) / (1 + a z^-1). */
static double lagLoss (double a, double w)
{
    return atan2(-a * sin(w), 1.0 + a * cos(w));
}

/* The stiff string's partial index at frequency f, continuous: the
   inverse of f = n f0 sqrt(1 + B n^2). */
static double partialAt (double f, double f0, double b)
{
    const double x = (f / f0) * (f / f0);

    if (b <= 0)
        return f / f0;

    return sqrt((sqrt(1.0 + 4.0 * b * x) - 1.0) / (2.0 * b));
}

static double partialHz (double n, double f0, double b)
{
    return n * f0 * sqrt(1.0 + b * n * n);
}

/* The loop phase the string wants at w: n turns at partial n. */
static double target (double w, double f0, double b, double rate)
{
    return 2.0 * M_PI * partialAt(w * rate / (2.0 * M_PI), f0, b);
}

/* Its group delay, in samples. */
static double targetDelay (double w, double f0, double b, double rate)
{
    const double h = 1e-6;

    return (target(w + h, f0, b, rate) - target(w - h, f0, b, rate)) /
           (2.0 * h);
}

/* The phase target's excess over the line with its slope at wm, in turns:
   how many second-order sections it takes to follow up to wm. */
static double excessTurns (double wm, double f0, double b, double rate)
{
    return (target(wm, f0, b, rate) -
            targetDelay(wm, f0, b, rate) * wm) / (2.0 * M_PI);
}

/* The loss filter from the two decays. */
static void designLoss (Design *d, double f0, double b, double decay,
                        double hidecay, double rate)
{
    const double w1 = 2.0 * M_PI * partialHz(1, f0, b) / rate;
    double fh = LOSS_HI_HZ, wh, g1, gh, ratio, cs, disc, a;

    if (fh < 2.0 * partialHz(1, f0, b))
        fh = 2.0 * partialHz(1, f0, b);

    g1 = pow(10.0, -3.0 * targetDelay(w1, f0, b, rate) / (rate * decay));
    d->g = g1;
    d->pole = 0;

    if (fh >= 0.45 * rate || hidecay >= decay)
        return;

    wh = 2.0 * M_PI * fh / rate;
    gh = pow(10.0, -3.0 * targetDelay(wh, f0, b, rate) / (rate * hidecay));

    /* |H(w1)| / |H(wh)| = g1 / gh is a quadratic in the pole whose roots
       are each other's reciprocals; the one inside the circle is it. */
    ratio = (g1 / gh) * (g1 / gh);
    cs = cos(wh) - ratio * cos(w1);
    disc = cs * cs - (1.0 - ratio) * (1.0 - ratio);
    a = LOSS_POLE_MIN;

    if (disc >= 0)
    {
        const double r1 = (-cs + sqrt(disc)) / (1.0 - ratio);
        const double r2 = (-cs - sqrt(disc)) / (1.0 - ratio);

        a = fabs(r1) < 1.0 ? r1 : r2;
    }

    if (!(a >= LOSS_POLE_MIN))
        a = LOSS_POLE_MIN;

    if (a > 0)
        a = 0;

    d->pole = a;
    d->g = g1 * sqrt(1.0 + 2.0 * a * cos(w1) + a * a) / (1.0 + a);

    if (d->g > 0.999999)
        d->g = 0.999999;
}

/* The second-order staircase. Returns how many sections it placed. */
static int designSecond (Design *d, double f0, double b, double rate)
{
    const double top = 2.0 * M_PI * DISP_FIT_HZ / rate;
    double wm = top < 0.9 * M_PI ? top : 0.9 * M_PI;
    double slope, t[DISP_MAX + 2];
    int n, k;

    /* Fewer sections than the band needs: fit a narrower band. */
    if (excessTurns(wm, f0, b, rate) > DISP_MAX)
    {
        double lo = 2.0 * M_PI * f0 / rate, hi = wm;

        for (k = 0; k < 50; k++)
        {
            const double mid = 0.5 * (lo + hi);

            if (excessTurns(mid, f0, b, rate) > DISP_MAX)
                hi = mid;
            else
                lo = mid;
        }

        wm = lo;
    }

    n = (int)floor(excessTurns(wm, f0, b, rate) + 0.5);

    if (n > DISP_MAX)
        n = DISP_MAX;

    if (n < DISP_BQ_MIN)
        return 0;

    slope = targetDelay(wm, f0, b, rate);

    /* A pole pair at the middle of each 2 pi step of the excess. */
    for (k = 1; k <= n; k++)
    {
        const double want = 2.0 * M_PI * (k - 0.5);
        double lo = 0, hi = wm;
        int i;

        for (i = 0; i < 50; i++)
        {
            const double mid = 0.5 * (lo + hi);

            if (target(mid, f0, b, rate) - slope * mid < want)
                lo = mid;
            else
                hi = mid;
        }

        t[k] = lo;
    }

    /* Each pole's width is the spacing to its neighbors; the ends mirror
       their one neighbor. */
    t[0] = -t[1];
    t[n + 1] = 2.0 * t[n] - t[n - 1];

    for (k = 1; k <= n; k++)
    {
        const double r = exp(-0.5 * (t[k + 1] - t[k - 1]));

        d->c1[k - 1] = -2.0 * r * cos(t[k]);
        d->c2[k - 1] = r * r;
    }

    return n;
}

/* The loop's lag at w from everything but the line and the Thiran. */
static double filterLag (const Design *d, double w)
{
    double lag = lagLoss(d->pole, w);
    int k;

    if (d->kind == 1)
        lag += d->sections * lagFirst(d->c1[0], w);
    else if (d->kind == 2)
        for (k = 0; k < d->sections; k++)
        {
            const double r = sqrt(d->c2[k]);
            const double t = acos(-d->c1[k] / (2.0 * r));

            lag += lagSecond(r, t, w);
        }

    return lag;
}

/* The first-order cascade: m sections of one coefficient, least squares
   over the partials in the band, weighted by 1/n, with the fundamental
   held exact. Returns the coefficient. */
static double designFirst (const Design *d, int m, double f0, double b,
                           double rate)
{
    double w[DISP_FIT_PARTIALS + 1];
    const double golden = 0.6180339887498949;
    double lo = -0.95, hi = 0;
    int count = 0, n, i;

    for (n = 1; n <= DISP_FIT_PARTIALS; n++)
    {
        const double f = partialHz(n, f0, b);

        if (f >= DISP_FIT_HZ || f >= 0.45 * rate)
            break;

        w[n] = 2.0 * M_PI * f / rate;
        count = n;
    }

    if (count < 2)
        return 0;

    for (i = 0; i < 40; i++)
    {
        double x[2], cost[2];
        int j;

        x[0] = hi - golden * (hi - lo);
        x[1] = lo + golden * (hi - lo);

        for (j = 0; j < 2; j++)
        {
            const double line = (2.0 * M_PI - m * lagFirst(x[j], w[1]) -
                                 lagLoss(d->pole, w[1])) / w[1];

            cost[j] = 0;

            for (n = 2; n <= count; n++)
            {
                const double err = (line * w[n] + m * lagFirst(x[j], w[n]) +
                                    lagLoss(d->pole, w[n])) /
                                   (2.0 * M_PI * n) - 1.0;

                cost[j] += err * err / n;
            }
        }

        if (cost[0] < cost[1])
            hi = x[1];
        else
            lo = x[0];
    }

    return 0.5 * (lo + hi);
}

/* The whole loop for one note. */
static void design (Design *d, double f0, double b, double decay,
                    double hidecay, double damper, double rate)
{
    const double w1 = 2.0 * M_PI * partialHz(1, f0, b) / rate;
    double total, frac, lo, hi;
    int i;

    designLoss(d, f0, b, decay, hidecay, rate);

    d->kind = 0;
    d->sections = 0;

    if (b > 0)
    {
        d->sections = designSecond(d, f0, b, rate);

        if (d->sections > 0)
            d->kind = 2;
        else
        {
            /* Fewer sections until the line has room. */
            int m;

            for (m = DISP_FO; m > 0; m--)
            {
                d->kind = 1;
                d->sections = m;
                d->c1[0] = designFirst(d, m, f0, b, rate);

                if ((2.0 * M_PI - filterLag(d, w1)) / w1 >= LINE_MIN + 0.5)
                    break;
            }

            if (m == 0)
            {
                d->kind = 0;
                d->sections = 0;
            }
        }
    }

    total = (2.0 * M_PI - filterLag(d, w1)) / w1;

    if (total < LINE_MIN)
        total = LINE_MIN;

    d->line = (int)floor(total - 0.5);

    /* The fraction against the Thiran's own phase at the fundamental. */
    lo = 0.4;
    hi = 1.6;

    for (i = 0; i < 40; i++)
    {
        const double mid = 0.5 * (lo + hi);

        if (d->line * w1 + lagFirst((1.0 - mid) / (1.0 + mid), w1) <
            total * w1)
            lo = mid;
        else
            hi = mid;
    }

    frac = 0.5 * (lo + hi);
    d->eta = (1.0 - frac) / (1.0 + frac);

    d->dgain = pow(10.0, -3.0 * total / (rate * damper));
}

static void store (float *state, const Design *d)
{
    int k;

    state[S_KIND] = (float)d->kind;
    state[S_SECTIONS] = (float)d->sections;
    state[S_LINE] = (float)d->line;
    state[S_ETA] = (float)d->eta;
    state[S_G] = (float)d->g;
    state[S_POLE] = (float)d->pole;
    state[S_DGAIN] = (float)d->dgain;

    for (k = 0; k < DISP_MAX; k++)
    {
        state[S_C1 + k] = (float)(d->kind == 1 ? d->c1[0] : d->c1[k]);
        state[S_C2 + k] = (float)d->c2[k];
    }
}

static inline double flush (double x)
{
    return fabs(x) < FLUSH ? 0 : x;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg, *in_freq, *in_b, *in_decay, *in_hidecay, *in_damper;
    thArg *in_gate, *out_arg, *out_play, *inout_buffer, *inout_state;
    float *out, *play, *buffer, *state;
    const unsigned int len = (unsigned int)(samples / FREQ_MIN) + 8;
    const double rate = samples;
    const double fade = 1.0 - exp(-1.0 / (DAMPER_FADE * rate));
    const double release = exp(-1.0 / (PLAY_RELEASE * rate));
    const double hold = PLAY_HOLD * rate;
    double freq, b, decay, hidecay, damper;
    /* The state is float between samples as it is between windows, so a
       window boundary rounds nothing a sample boundary does not. */
    float z1[DISP_MAX], z2[DISP_MAX], thz, lossz, damp, peak, age;
    double c1[DISP_MAX], c2[DISP_MAX], eta, g, pole, dgain;
    unsigned int at, i;
    int kind, sections, line, k;

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_freq = mod->getArg(node, args[IN_FREQ]);
    in_b = mod->getArg(node, args[IN_B]);
    in_decay = mod->getArg(node, args[IN_DECAY]);
    in_hidecay = mod->getArg(node, args[IN_HIDECAY]);
    in_damper = mod->getArg(node, args[IN_DAMPER]);
    in_gate = mod->getArg(node, args[IN_GATE]);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);
    out_play = mod->getArg(node, args[OUT_PLAY]);
    play = out_play->allocate(windowlen);

    inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
    inout_state = mod->getArg(node, args[INOUT_STATE]);
    buffer = inout_buffer->allocate(len);
    state = inout_state->allocate(S_COUNT);

    /* The line needs FREQ_MIN's period and the filters at least a sample
       each of the top one; a third of the rate leaves both. */
    freq = thBoundFreq((*in_freq)[0], samples);

    if (freq < FREQ_MIN)
        freq = FREQ_MIN;

    if (freq > rate / 3.0)
        freq = rate / 3.0;

    b = thClampArg((*in_b)[0], 0, B_MAX);
    decay = (*in_decay)[0] == 0 ? DECAY_DEFAULT
                                : thClampArg((*in_decay)[0], DECAY_MIN,
                                             DECAY_MAX);
    hidecay = (*in_hidecay)[0] == 0 ? decay
                                    : thClampArg((*in_hidecay)[0], DECAY_MIN,
                                                 DECAY_MAX);
    damper = (*in_damper)[0] == 0 ? DAMPER_DEFAULT
                                  : thClampArg((*in_damper)[0], DECAY_MIN,
                                               DECAY_MAX);

    /* A fresh state has freq 0, which no bounded freq equals. */
    if ((float)freq != state[S_FREQ] || (float)b != state[S_B] ||
        (float)decay != state[S_DECAY] || (float)hidecay != state[S_HIDECAY] ||
        (float)damper != state[S_DAMPER])
    {
        Design d;

        memset(&d, 0, sizeof(d));
        design(&d, freq, b, decay, hidecay, damper, rate);
        store(state, &d);

        state[S_FREQ] = (float)freq;
        state[S_B] = (float)b;
        state[S_DECAY] = (float)decay;
        state[S_HIDECAY] = (float)hidecay;
        state[S_DAMPER] = (float)damper;
    }

    kind = (int)state[S_KIND];
    sections = (int)state[S_SECTIONS];
    line = (int)state[S_LINE];
    eta = state[S_ETA];
    g = state[S_G];
    pole = state[S_POLE];
    dgain = state[S_DGAIN];

    for (k = 0; k < DISP_MAX; k++)
    {
        c1[k] = state[S_C1 + k];
        c2[k] = state[S_C2 + k];
        z1[k] = state[S_Z1 + k];
        z2[k] = state[S_Z2 + k];
    }

    at = (unsigned int)state[S_POS];
    age = state[S_AGE];
    peak = state[S_PEAK];
    damp = state[S_DAMP];
    thz = state[S_THIRAN];
    lossz = state[S_LOSS];

    if (at >= len)
        at = 0;

    for (i = 0; i < windowlen; i++)
    {
        const float raw = (*in_arg)[i];
        const double in = thIsFinite(raw) ? raw : 0;
        const double down = (*in_gate)[i] > 0 ? 0 : 1;
        double x, y;

        /* The line's output, and the Thiran's fraction on it. */
        x = buffer[(at + len - line) % len];
        y = eta * x + thz;
        thz = (float)flush(x - eta * y);
        x = y;

        if (kind == 1)
            for (k = 0; k < sections; k++)
            {
                y = c1[k] * x + z1[k];
                z1[k] = (float)flush(x - c1[k] * y);
                x = y;
            }
        else if (kind == 2)
            for (k = 0; k < sections; k++)
            {
                y = c2[k] * x + z1[k];
                z1[k] = (float)flush(c1[k] * x - c1[k] * y + z2[k]);
                z2[k] = (float)flush(x - c2[k] * y);
                x = y;
            }

        lossz = (float)flush(g * (1.0 + pole) * x - pole * lossz);

        damp = (float)(damp + (down - damp) * fade);

        y = in + lossz * (1.0 - damp * (1.0 - dgain));
        y = flush(y);

        buffer[at] = (float)y;
        out[i] = (float)y;

        peak = (float)(fabs(y) > peak * release ? fabs(y) : peak * release);

        if (age < hold)
            age++;

        play[i] = (age < hold || peak > PLAY_FLOOR) ? 1 : 0;

        at = (at + 1) % len;
    }

    for (k = 0; k < DISP_MAX; k++)
    {
        state[S_Z1 + k] = z1[k];
        state[S_Z2 + k] = z2[k];
    }

    state[S_POS] = (float)at;
    state[S_AGE] = age;
    state[S_PEAK] = peak;
    state[S_DAMP] = damp;
    state[S_THIRAN] = thz;
    state[S_LOSS] = lossz;

    return 0;
}
