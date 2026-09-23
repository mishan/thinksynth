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
 * top. `freq' is the first partial, so f0 is freq / sqrt(1 + B). The loop
 * rings at the frequencies where its total phase lag is a whole number of
 * turns, so stretching the partials means a loop whose group delay falls
 * with frequency: an allpass cascade inside it. That is the dispersion
 * filter, and it is why this is a plugin and not a graph -- a graph may
 * not hold a loop.
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
 * UNISON. Most piano notes have two or three strings, tuned a cent or two
 * apart and struck together. `strings' loops share one dispersion design
 * and each has its own line, Thiran and loss filter, detuned `unison'
 * cents from its neighbor. They are coupled where they meet the bridge:
 *
 *     v = beta * sum(x_k)            y_k = in + x_k - v
 *
 * the bridge moving with what arrives from all of them and taking it out
 * of each. The scattering matrix I - beta 1 1^T has eigenvalue 1 - N beta
 * for the strings moving together and 1 for every way of moving against
 * each other, so it is passive for beta up to 2 / N, and only the
 * in-phase motion drives the bridge. `prompt' is that motion's T60.
 *
 * A hammer strikes the strings in phase, so the note starts all
 * in-phase motion and falls fast in `prompt'; the mistuning turns it,
 * a beat at a time, into motion against the bridge, which falls only in
 * `decay'. That is the two-stage decay of a piano note -- the prompt
 * sound and the aftersound (Weinreich, "Coupled piano strings", JASA
 * 1977) -- and the beating in it, and neither is written anywhere here:
 * both follow from the coupling. How loud the aftersound is depends on
 * the mistuning against the coupling.
 *
 * In the bass a cent is a beat every half minute, too slow to turn
 * anything, and there the aftersound comes from the unison not being
 * symmetric. The hammer's face meets the strings a little unevenly, so
 * part of the blow goes straight into motion against the bridge; and the
 * bridge rocks under strings pulling against each other, so part of that
 * motion is heard. Both are a tilt across the unison, and `imbalance' is
 * its size: string k is struck, and heard, 1 + imbalance c_k, with c_k
 * running -1 to 1 across the strings. The mean blow is unchanged, and in
 * tune the difference is a mode the bridge does not damp, heard at
 * imbalance^2 of the note and falling in `decay'.
 *
 * `out' is the strings' mean, weighted by the same tilt; at one string it
 * is the string.
 *
 * THE HAMMER. With `mass' above 0 the strings are struck by a hammer
 * rather than driven by `in' alone: a mass on a felt spring whose force
 * is
 *
 *     F = K d^p,    d the felt's compression, p about 2.5
 *
 * (Chaigne and Askenfelt, "Numerical simulations of piano strings",
 * JASA 1994; Stulov, JASA 1995). The felt stiffens as it is squeezed, so
 * a harder blow is a shorter one and puts more of its force into the
 * high partials -- a loud note is brighter because of what felt is, and
 * nothing here says so. The hammer pushes against the string's own
 * displacement at the strike point, so the waves it launches come back
 * and push it off again: in the treble the reflection from the near end
 * throws it clear in a millisecond, and in the bass it can bounce.
 *
 * For that each string is split where the hammer hits it, `position' of
 * the way along: one loop to the bridge and back, carrying every filter
 * above, and one to the near end and back, a plain delay with the sign
 * a fixed end gives it. Their lengths sum to the string's, so the tuning
 * is the tuning. The strike point is a node of every partial whose index
 * is a multiple of 1 / `position', which no longer needs saying either.
 *
 * Units: the string's impedance is 1, so a force F puts F / 2 of velocity
 * into each direction. `mass' is the hammer's over the string's, the
 * string's being its impedance times the time a wave takes to cross it,
 * 1 / (2 f0). `felt' is K in millions. `velocity' is the hammer's speed,
 * in the units the string's waves are in, so `out' scales with it. The
 * hammer is launched when `strike' rises above 0, and each sample its
 * position is solved for exactly -- the felt, the string and the hammer
 * are one delay-free loop -- by a Newton iteration, safeguarded by
 * bisection, around one for each string's force. It stops being computed
 * HAMMER_TIME after the blow.
 *
 * `out' is then the wave arriving at the bridge, after the dispersion and
 * the loss, rather than the wave leaving the strike point: the blow reaches
 * the board spread out by the string's stiffness, and heard at the strike
 * point it is a click no one at a piano hears.
 *
 * At `mass' 0 there is no hammer and nothing above changes, sample for
 * sample.
 *
 * `play' is the strings' own energy: 1 for PLAY_HOLD after the note starts
 * and while the output's peak follower is above PLAY_FLOOR. A voice ends
 * when its strings are quiet, not when its key comes up.
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

/* The most strings a note may have, and the widest unison in cents. */
#define STRINGS_MAX 3
#define UNISON_MAX 100.0f

/* The hammer: how long after the blow it is followed, the felt exponent
   when `exponent' is 0, where it strikes when `position' is 0, and the
   solver's iterations. */
#define HAMMER_TIME 0.05
#define HAMMER_P_DEFAULT 2.5
#define HAMMER_POSITION_DEFAULT 0.125
#define HAMMER_ITERATIONS 30

/* The heaviest hammer and the hardest felt, K in millions. A treble
   hammer is light against a light string, so its mass over the string's
   climbs to tens up the keyboard, and its felt doubles in hardness every
   few keys: these leave room for both up to MIDI's top note. */
#define MASS_MAX 100.0f
#define FELT_MAX 1e8f

/* How long the damper takes to land, in seconds. */
#define DAMPER_FADE 0.01

/* The lowest note the line is sized for, in hertz, and the shortest line
   left after the filters, in samples: the Thiran wants its fraction near
   [0.5, 1.5) and the integer part at least one. */
#define FREQ_MIN 16.0
#define LINE_MIN 2.0

/* The Thiran's fraction, at its extremes. Above a fraction of 0 it is
   stable; these keep its pole off the unit circle. Between them the
   Thiran reaches the lag a period needs at any fundamental up to a third
   of the rate, with the line a sample longer where it must be. */
#define THIRAN_MIN 0.1
#define THIRAN_MAX 4.0

/* The corner of the high-pass on `in', in hertz. The loss filter's gain
   is highest at DC, at worst its clamp, so a pulse that is all one sign
   would leave a DC mode ringing long after the partials. Two one-poles:
   one leaves the string an input with no net DC but a tail that the
   nearly lossless DC mode still sums to a residue; the second takes the
   residue's first order away too. Far enough below FREQ_MIN that the
   lowest string's excitation loses under a decibel. */
#define DC_HZ 5.0

/* `play': the hold after the note starts, the follower's release, and the
   level below which the string is over (-80 dBFS). */
#define PLAY_HOLD 0.05
#define PLAY_RELEASE 0.05
#define PLAY_FLOOR 1e-4

/* Below this a value is flushed to zero, so a long decay does not end in
   denormals. */
#define FLUSH 1e-30

enum {IN_ARG, IN_FREQ, IN_B, IN_DECAY, IN_HIDECAY, IN_DAMPER, IN_GATE,
      IN_STRINGS, IN_UNISON, IN_PROMPT, IN_IMBALANCE, IN_STRIKE, IN_VELOCITY,
      IN_MASS, IN_FELT, IN_EXPONENT, IN_POSITION, OUT_ARG, OUT_PLAY,
      OUT_FORCE, INOUT_BUFFER, INOUT_STATE};

int args[INOUT_STATE + 1];

/* Where each string's own state lives, from the start of its block. */
enum {
    P_THIRAN,               /* the Thiran's state */
    P_LOSS,                 /* the loss filter's last output */
    P_LINE,                 /* the line's integer delay */
    P_ETA,                  /* the Thiran's coefficient */
    P_G,                    /* the loss filter's gain */
    P_POLE,                 /* and pole */
    P_Z1,                   /* DISP_MAX first states */
    P_Z2 = P_Z1 + DISP_MAX, /* DISP_MAX second states */
    P_COUNT = P_Z2 + DISP_MAX
};

/* Where each piece of the state lives, in INOUT_STATE. */
enum {
    S_POS,                  /* the lines' write position */
    S_AGE,                  /* samples since the note began, saturating */
    S_PEAK,                 /* the output's peak follower */
    S_DAMP,                 /* the damper, 0 off to 1 down */
    S_DCX,                  /* the high-pass's last input */
    S_DCY1,                 /* its first stage's last output */
    S_DCY2,                 /* and its second's */
    S_FREQ,                 /* the inputs the design below is for */
    S_B,
    S_DECAY,
    S_HIDECAY,
    S_DAMPER,
    S_STRINGS,
    S_UNISON,
    S_PROMPT,
    S_KIND,                 /* 0 no dispersion, 1 first-order, 2 second */
    S_SECTIONS,             /* how many sections */
    S_BETA,                 /* the bridge coupling */
    S_DGAIN,                /* the damper's per-trip gain */
    S_C1,                   /* DISP_MAX first coefficients */
    S_C2 = S_C1 + DISP_MAX, /* DISP_MAX second coefficients */
    S_STRING = S_C2 + DISP_MAX, /* STRINGS_MAX blocks of P_COUNT */
    S_TOTAL = S_STRING + STRINGS_MAX * P_COUNT, /* the loop's delay */
    S_HY,                   /* the hammer's position */
    S_HV,                   /* and velocity */
    S_HTIME,                /* samples since the blow; past HAMMER_TIME, done */
    S_HPREV,                /* `strike' last sample, for its rising edge */
    S_APOS,                 /* the near ends' write position */
    S_YS,                   /* STRINGS_MAX displacements at the strike point */
    S_COUNT = S_YS + STRINGS_MAX
};

/* Everything the design produces, in double until it is stored. */
struct Design {
    int kind, sections, strings;
    double g, pole, beta, dgain, total;
    double c1[DISP_MAX], c2[DISP_MAX];
    int line[STRINGS_MAX];
    double eta[STRINGS_MAX], sg[STRINGS_MAX], spole[STRINGS_MAX];
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
                       "Inharmonicity: partial n at n f0 sqrt(1 + b n^2), "
                       "f0 sqrt(1 + b) the fundamental; 0 is a harmonic "
                       "string");
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

    args[IN_STRINGS] = plugin->regArg("strings", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_STRINGS],
                       "How many unison strings; read once per window");
    plugin->setArgRange(args[IN_STRINGS], 1, STRINGS_MAX);
    plugin->setArgStep(args[IN_STRINGS], 1);
    plugin->setArgDefault(args[IN_STRINGS], 1);

    args[IN_UNISON] = plugin->regArg("unison", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_UNISON],
                       "How far apart neighboring strings are tuned");
    plugin->setArgUnits(args[IN_UNISON], "cents");
    plugin->setArgRange(args[IN_UNISON], 0, UNISON_MAX);

    args[IN_PROMPT] = plugin->regArg("prompt", thPlugin::ARG_IN);
    /* The in-phase motion's own loss, on top of `decay'. */
    plugin->setArgDesc(args[IN_PROMPT],
                       "How long the strings moving together take to fall "
                       "sixty decibels into the bridge; 0 is uncoupled");
    plugin->setArgUnits(args[IN_PROMPT], "seconds");
    plugin->setArgRange(args[IN_PROMPT], DECAY_MIN, DECAY_MAX);

    args[IN_IMBALANCE] = plugin->regArg("imbalance", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_IMBALANCE],
                       "The tilt across the unison: the outer strings are "
                       "struck, and heard, 1 plus and minus this");
    plugin->setArgRange(args[IN_IMBALANCE], 0, 1);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    /* No range: a string driven at its own pitch builds up past its
       input. */
    plugin->setArgDesc(args[OUT_ARG],
                       "The strings' mean, tilted by `imbalance'");
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    args[IN_STRIKE] = plugin->regArg("strike", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_STRIKE],
                       "Launches the hammer when it rises above 0");
    plugin->setArgRange(args[IN_STRIKE], 0, 2);

    args[IN_VELOCITY] = plugin->regArg("velocity", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_VELOCITY],
                       "The hammer's speed at the string; read at the blow");
    plugin->setArgRange(args[IN_VELOCITY], 0, 1);

    args[IN_MASS] = plugin->regArg("mass", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_MASS],
                       "The hammer's mass over the string's; 0 is no hammer");
    plugin->setArgRange(args[IN_MASS], 0, MASS_MAX);

    args[IN_FELT] = plugin->regArg("felt", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FELT],
                       "The felt's stiffness K, in millions: F = K d^p");
    plugin->setArgRange(args[IN_FELT], 0, FELT_MAX);

    args[IN_EXPONENT] = plugin->regArg("exponent", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_EXPONENT],
                       "The felt's exponent p: how much harder it gets the "
                       "more it is squeezed");
    plugin->setArgRange(args[IN_EXPONENT], 1, 5);
    plugin->setArgDefault(args[IN_EXPONENT], HAMMER_P_DEFAULT);

    args[IN_POSITION] = plugin->regArg("position", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_POSITION],
                       "Where the hammer strikes, as a fraction of the "
                       "string from the near end");
    plugin->setArgRange(args[IN_POSITION], 0.02f, 0.5f);
    plugin->setArgDefault(args[IN_POSITION], HAMMER_POSITION_DEFAULT);

    args[OUT_PLAY] = plugin->regArg("play", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_PLAY],
                       "1 while the strings are still sounding");
    plugin->setArgRange(args[OUT_PLAY], 0, 1);

    args[OUT_FORCE] = plugin->regArg("force", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_FORCE],
                       "The hammer's force on the strings; 0 when it is "
                       "clear of them");

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
static void designLoss (double *g, double *pole, double f0, double b,
                        double decay, double hidecay, double rate)
{
    const double w1 = 2.0 * M_PI * partialHz(1, f0, b) / rate;
    double fh = LOSS_HI_HZ, wh, g1, gh, ratio, cs, disc, a;

    if (fh < 2.0 * partialHz(1, f0, b))
        fh = 2.0 * partialHz(1, f0, b);

    g1 = pow(10.0, -3.0 * targetDelay(w1, f0, b, rate) / (rate * decay));
    *g = g1;
    *pole = 0;

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

    *pole = a;
    *g = g1 * sqrt(1.0 + 2.0 * a * cos(w1) + a * a) / (1.0 + a);

    if (*g > 0.999999)
        *g = 0.999999;
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

/* The loop's lag at w from everything but the line and the Thiran, with
   a loss filter whose pole is `pole'. */
static double filterLag (const Design *d, double pole, double w)
{
    double lag = lagLoss(pole, w);
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

/* The delay line and the Thiran that tune one string to f0, given the
   dispersion already designed and a loss filter with pole `pole'. */
static void designLine (const Design *d, double pole, double f0, double b,
                        double rate, int *line, double *eta, double *total)
{
    const double w1 = 2.0 * M_PI * partialHz(1, f0, b) / rate;
    double lo = THIRAN_MIN, hi = THIRAN_MAX, want, frac;
    int i;

    *total = (2.0 * M_PI - filterLag(d, pole, w1)) / w1;

    if (*total < LINE_MIN)
        *total = LINE_MIN;

    *line = (int)floor(*total - 0.5);

    /* The fraction against the Thiran's own phase at the fundamental. Near
       the top of the range that phase is far from the fraction's, and the
       lag wanted can be more than THIRAN_MAX gives; a sample more line
       takes a turn of the fundamental off it. */
    want = (*total - *line) * w1;

    if (want > lagFirst((1.0 - THIRAN_MAX) / (1.0 + THIRAN_MAX), w1))
    {
        (*line)++;
        want -= w1;
    }

    for (i = 0; i < 50; i++)
    {
        const double mid = 0.5 * (lo + hi);

        if (lagFirst((1.0 - mid) / (1.0 + mid), w1) < want)
            lo = mid;
        else
            hi = mid;
    }

    frac = 0.5 * (lo + hi);
    *eta = (1.0 - frac) / (1.0 + frac);
}

/* The whole loop for one note. The dispersion is designed once, at the
   note's own pitch, and shared: a cent's detune moves the target by less
   than the fit's own error. Each string gets its own loss filter and its
   own line. `freq' is the first partial, so the f0 in the formula is a
   little under it. */
static void design (Design *d, double freq, double b, double decay,
                    double hidecay, double damper, int strings,
                    double unison, double prompt, double rate)
{
    const double f0 = freq / sqrt(1.0 + b);
    const double w1 = 2.0 * M_PI * freq / rate;
    double total;
    int k;

    designLoss(&d->g, &d->pole, f0, b, decay, hidecay, rate);

    d->kind = 0;
    d->sections = 0;
    d->strings = strings;

    if (b > 0)
    {
        d->sections = designSecond(d, f0, b, rate);

        if (d->sections > 0)
            d->kind = 2;
        else
        {
            /* Fewer sections until the line has room. The widest unison
               makes the outer strings 6% shorter, which the half sample
               over LINE_MIN covers. */
            int m;

            for (m = DISP_FO; m > 0; m--)
            {
                d->kind = 1;
                d->sections = m;
                d->c1[0] = designFirst(d, m, f0, b, rate);

                if ((2.0 * M_PI - filterLag(d, d->pole, w1)) / w1 >=
                    LINE_MIN + 0.5)
                    break;
            }

            if (m == 0)
            {
                d->kind = 0;
                d->sections = 0;
            }
        }
    }

    /* Neighbors `unison' cents apart, centered on the note. */
    for (k = 0; k < strings; k++)
    {
        const double cents = (k - 0.5 * (strings - 1)) * unison;
        const double fk = f0 * pow(2.0, cents / 1200.0);
        double tk;

        designLoss(&d->sg[k], &d->spole[k], fk, b, decay, hidecay, rate);
        designLine(d, d->spole[k], fk, b, rate, &d->line[k], &d->eta[k],
                   &tk);
    }

    total = (2.0 * M_PI - filterLag(d, d->pole, w1)) / w1;

    /* Per trip, and a trip at the fundamental is the loop's group delay
       there: the period and what the dispersion filter adds to it. */
    d->dgain = pow(10.0, -3.0 * targetDelay(w1, f0, b, rate) /
                         (rate * damper));
    d->total = total;

    /* The in-phase motion's per-trip gain is 1 - N beta. One string has
       no motion against the bridge to keep, so it is not coupled: its
       loss to the bridge is in `decay'. */
    d->beta = 0;

    if (strings > 1 && prompt > 0)
        d->beta = (1.0 - pow(10.0, -3.0 * total / (rate * prompt))) /
                  strings;
}

static void store (float *state, const Design *d)
{
    int k;

    state[S_KIND] = (float)d->kind;
    state[S_SECTIONS] = (float)d->sections;
    state[S_BETA] = (float)d->beta;
    state[S_DGAIN] = (float)d->dgain;
    state[S_TOTAL] = (float)d->total;

    for (k = 0; k < DISP_MAX; k++)
    {
        state[S_C1 + k] = (float)(d->kind == 1 ? d->c1[0] : d->c1[k]);
        state[S_C2 + k] = (float)d->c2[k];
    }

    for (k = 0; k < d->strings; k++)
    {
        float *str = state + S_STRING + k * P_COUNT;

        str[P_LINE] = (float)d->line[k];
        str[P_ETA] = (float)d->eta[k];
        str[P_G] = (float)d->sg[k];
        str[P_POLE] = (float)d->spole[k];
    }
}

static inline double flush (double x)
{
    return fabs(x) < FLUSH ? 0 : x;
}

/* One string's force for a felt squeezed `a' before the string gives:
   F = K (a - F h)^p, h the string's give per unit force this sample. The
   right side falls as F rises and is concave, so Newton from 0 climbs to
   the root from below and never past it. */
static double feltForce (double a, double k, double p, double h)
{
    double f = 0;
    int i;

    if (!(a > 0) || k <= 0)
        return 0;

    for (i = 0; i < HAMMER_ITERATIONS; i++)
    {
        const double u = a - f * h;
        double up, phi, slope, next;

        if (!(u > 0))
            break;

        up = pow(u, p - 1);
        phi = f - k * up * u;
        slope = 1 + k * p * up * h;
        next = f - phi / slope;

        if (next > a / h)
            next = a / h;

        if (!(next > f) || next - f <= 1e-12 * next)
        {
            if (next > f)
                f = next;

            break;
        }

        f = next;
    }

    return f;
}

/* The hammer's position after this sample, and each string's force in
   `f': the root of
       Y = hy + (hv - sum F(Y) dt / m) dt
   which rises through zero once, so Newton inside a bracket that bisects
   whenever a step would leave it. `base' is where each string would be
   with no hammer on it, and `weight' the tilt across the unison. */
static double hammerSolve (double hy, double hv, const double *base,
                           const double *weight, int strings, double k,
                           double p, double m, double dt, double *f)
{
    double hi = hy + hv * dt, lo = hi, y = hi;
    int i, s;

    for (s = 0; s < strings; s++)
        if (base[s] < lo)
            lo = base[s];

    for (i = 0; i < HAMMER_ITERATIONS; i++)
    {
        double sum = 0, dsum = 0, g, dg, next;

        for (s = 0; s < strings; s++)
        {
            const double ks = k * weight[s];
            const double fs = feltForce(y - base[s], ks, p, dt / 2);

            f[s] = fs;
            sum += fs;

            if (fs > 0)
            {
                const double u = y - base[s] - fs * dt / 2;
                const double c = ks * p * pow(u, p - 1);

                dsum += c / (1 + c * dt / 2);
            }
        }

        g = y - (hy + hv * dt) + sum * dt * dt / m;

        if (g == 0 || hi - lo <= 1e-15 * (fabs(hi) + 1e-30))
            break;

        if (g > 0)
            hi = y;
        else
            lo = y;

        dg = 1 + dsum * dt * dt / m;
        next = y - g / dg;

        y = (next > lo && next < hi) ? next : 0.5 * (lo + hi);
    }

    return y;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg, *in_freq, *in_b, *in_decay, *in_hidecay, *in_damper;
    thArg *in_gate, *in_strings, *in_unison, *in_prompt, *in_imbalance;
    thArg *in_strike, *in_velocity, *in_mass, *in_felt, *in_exponent;
    thArg *in_position;
    thArg *out_arg, *out_play, *out_force, *inout_buffer, *inout_state;
    float *out, *play, *force, *buffer, *state;
    /* Room for the flattest string: FREQ_MIN's period, and the widest
       unison's outer string a whole UNISON_MAX below it. */
    const unsigned int len =
        (unsigned int)(samples / FREQ_MIN * pow(2.0, UNISON_MAX / 1200.0)) +
        8;
    /* The near end's loop is at most half the string's. */
    const unsigned int lenA = len / 2 + 8;
    const double rate = samples;
    const double fade = 1.0 - exp(-1.0 / (DAMPER_FADE * rate));
    const double release = exp(-1.0 / (PLAY_RELEASE * rate));
    const double hold = PLAY_HOLD * rate;
    const double dcpole = exp(-2.0 * M_PI * DC_HZ / rate);
    double freq, b, decay, hidecay, damper, unison, prompt;
    /* The state is float between samples as it is between windows, so a
       window boundary rounds nothing a sample boundary does not. */
    float z1[STRINGS_MAX][DISP_MAX], z2[STRINGS_MAX][DISP_MAX];
    float thz[STRINGS_MAX], lossz[STRINGS_MAX], damp, peak, age;
    float dcx, dcy1, dcy2;
    double c1[DISP_MAX], c2[DISP_MAX], beta, dgain;
    double eta[STRINGS_MAX], g[STRINGS_MAX], pole[STRINGS_MAX];
    double strike[STRINGS_MAX], imbalance;
    float *line[STRINGS_MAX], *near[STRINGS_MAX];
    int lines[STRINGS_MAX];
    float ys[STRINGS_MAX], hy, hv, htime, hprev;
    double mass, hmass = 1, felt, exponent, position;
    unsigned int apos = 0;
    int da = 0;
    unsigned int at, i;
    int kind, sections, strings, s, k;

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_freq = mod->getArg(node, args[IN_FREQ]);
    in_b = mod->getArg(node, args[IN_B]);
    in_decay = mod->getArg(node, args[IN_DECAY]);
    in_hidecay = mod->getArg(node, args[IN_HIDECAY]);
    in_damper = mod->getArg(node, args[IN_DAMPER]);
    in_gate = mod->getArg(node, args[IN_GATE]);
    in_strings = mod->getArg(node, args[IN_STRINGS]);
    in_unison = mod->getArg(node, args[IN_UNISON]);
    in_prompt = mod->getArg(node, args[IN_PROMPT]);
    in_imbalance = mod->getArg(node, args[IN_IMBALANCE]);
    in_strike = mod->getArg(node, args[IN_STRIKE]);
    in_velocity = mod->getArg(node, args[IN_VELOCITY]);
    in_mass = mod->getArg(node, args[IN_MASS]);
    in_felt = mod->getArg(node, args[IN_FELT]);
    in_exponent = mod->getArg(node, args[IN_EXPONENT]);
    in_position = mod->getArg(node, args[IN_POSITION]);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);
    out_play = mod->getArg(node, args[OUT_PLAY]);
    play = out_play->allocate(windowlen);
    out_force = mod->getArg(node, args[OUT_FORCE]);
    force = out_force->allocate(windowlen);

    inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
    inout_state = mod->getArg(node, args[INOUT_STATE]);
    buffer = inout_buffer->allocate(STRINGS_MAX * (len + lenA));
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
    strings = (int)floor(thClampArg((*in_strings)[0], 1, STRINGS_MAX) + 0.5);
    unison = thClampArg((*in_unison)[0], 0, UNISON_MAX);
    prompt = (*in_prompt)[0] == 0 ? 0
                                  : thClampArg((*in_prompt)[0], DECAY_MIN,
                                               DECAY_MAX);

    /* A fresh hammer is at rest, done with a blow it never struck, until
       `strike' first rises. */
    if (state[S_FREQ] == 0)
        state[S_HTIME] = (float)ceil(HAMMER_TIME * rate);

    /* A fresh state has freq 0, which no bounded freq equals. */
    if ((float)freq != state[S_FREQ] || (float)b != state[S_B] ||
        (float)decay != state[S_DECAY] || (float)hidecay != state[S_HIDECAY] ||
        (float)damper != state[S_DAMPER] ||
        (float)strings != state[S_STRINGS] ||
        (float)unison != state[S_UNISON] || (float)prompt != state[S_PROMPT])
    {
        Design d;

        /* Strings joining the unison start at rest: what they held when
           they last sounded is stale. */
        for (s = (int)state[S_STRINGS]; s < strings; s++)
        {
            memset(state + S_STRING + s * P_COUNT, 0,
                   P_COUNT * sizeof(float));
            memset(buffer + s * len, 0, len * sizeof(float));
        }

        memset(&d, 0, sizeof(d));
        design(&d, freq, b, decay, hidecay, damper, strings, unison, prompt,
               rate);
        store(state, &d);

        state[S_FREQ] = (float)freq;
        state[S_B] = (float)b;
        state[S_DECAY] = (float)decay;
        state[S_HIDECAY] = (float)hidecay;
        state[S_DAMPER] = (float)damper;
        state[S_STRINGS] = (float)strings;
        state[S_UNISON] = (float)unison;
        state[S_PROMPT] = (float)prompt;
    }

    kind = (int)state[S_KIND];
    sections = (int)state[S_SECTIONS];
    beta = state[S_BETA];
    dgain = state[S_DGAIN];

    for (k = 0; k < DISP_MAX; k++)
    {
        c1[k] = state[S_C1 + k];
        c2[k] = state[S_C2 + k];
    }

    imbalance = thClampArg((*in_imbalance)[0], 0, 1);

    mass = thClampArg((*in_mass)[0], 0, MASS_MAX);
    felt = thClampArg((*in_felt)[0], 0, FELT_MAX) * 1e6;
    exponent = (*in_exponent)[0] == 0 ? HAMMER_P_DEFAULT
                                      : thClampArg((*in_exponent)[0], 1, 5);
    position = (*in_position)[0] == 0
               ? HAMMER_POSITION_DEFAULT
               : thClampArg((*in_position)[0], 0.02f, 0.5f);

    for (s = 0; s < strings; s++)
    {
        const float *str = state + S_STRING + s * P_COUNT;

        strike[s] = strings > 1
                    ? 1.0 + imbalance * (2.0 * s / (strings - 1) - 1.0)
                    : 1.0;

        line[s] = buffer + s * len;
        near[s] = buffer + STRINGS_MAX * len + s * lenA;
        ys[s] = state[S_YS + s];
        lines[s] = (int)str[P_LINE];
        eta[s] = str[P_ETA];
        g[s] = str[P_G];
        pole[s] = str[P_POLE];
        thz[s] = str[P_THIRAN];
        lossz[s] = str[P_LOSS];

        for (k = 0; k < DISP_MAX; k++)
        {
            z1[s][k] = str[P_Z1 + k];
            z2[s][k] = str[P_Z2 + k];
        }
    }

    at = (unsigned int)state[S_POS];
    age = state[S_AGE];
    peak = state[S_PEAK];
    damp = state[S_DAMP];
    dcx = state[S_DCX];
    dcy1 = state[S_DCY1];
    dcy2 = state[S_DCY2];

    if (at >= len)
        at = 0;

    hy = state[S_HY];
    hv = state[S_HV];
    htime = state[S_HTIME];
    hprev = state[S_HPREV];
    apos = (unsigned int)state[S_APOS];

    if (apos >= lenA)
        apos = 0;

    if (mass > 0)
    {
        int shortest = lines[0];

        for (s = 1; s < strings; s++)
            if (lines[s] < shortest)
                shortest = lines[s];

        /* The near end's share of the loop, whole samples: the fraction
           stays with the Thiran on the bridge side. */
        da = (int)floor(position * state[S_TOTAL] + 0.5);

        if (da > (int)lenA - 2)
            da = (int)lenA - 2;

        if (da > shortest - 1)
            da = shortest - 1;

        if (da < 1)
            da = 1;

        /* The string's mass is its impedance, 1, times the time a wave
           takes to cross it. */
        hmass = mass / (2.0 * freq);
    }

    for (i = 0; i < windowlen; i++)
    {
        const float raw = (*in_arg)[i];
        const double in = thIsFinite(raw) ? raw : 0;
        const double down = (*in_gate)[i] > 0 ? 0 : 1;
        const float last = dcy1;
        double x, y, sum = 0, bridge, kept, mean = 0;

        dcy1 = (float)flush(in - dcx + dcpole * dcy1);
        dcy2 = (float)flush(dcy1 - last + dcpole * dcy2);
        dcx = (float)in;

        for (s = 0; s < strings; s++)
        {
            /* The line's output, and the Thiran's fraction on it. */
            x = line[s][(at + len - (lines[s] - da)) % len];
            y = eta[s] * x + thz[s];
            thz[s] = (float)flush(x - eta[s] * y);
            x = y;

            if (kind == 1)
                for (k = 0; k < sections; k++)
                {
                    y = c1[k] * x + z1[s][k];
                    z1[s][k] = (float)flush(x - c1[k] * y);
                    x = y;
                }
            else if (kind == 2)
                for (k = 0; k < sections; k++)
                {
                    y = c2[k] * x + z1[s][k];
                    z1[s][k] = (float)flush(c1[k] * x - c1[k] * y +
                                            z2[s][k]);
                    z2[s][k] = (float)flush(x - c2[k] * y);
                    x = y;
                }

            lossz[s] = (float)flush(g[s] * (1.0 + pole[s]) * x -
                                    pole[s] * lossz[s]);
            sum += lossz[s];
        }

        /* What the bridge takes, the same from every string. */
        bridge = beta * sum;

        damp = (float)(damp + (down - damp) * fade);
        kept = 1.0 - damp * (1.0 - dgain);

        if (mass <= 0)
        {
            for (s = 0; s < strings; s++)
            {
                y = dcy2 * strike[s] + (lossz[s] - bridge) * kept;
                y = flush(y);

                line[s][at] = (float)y;
                mean += y * strike[s];
            }

            force[i] = 0;
        }
        else
        {
            /* What arrives at the strike point from each end: from the
               bridge through every filter, from the near end a plain
               delay, each turned over by the end it came from. */
            double fromBridge[STRINGS_MAX], fromNear[STRINGS_MAX];
            double base[STRINGS_MAX], f[STRINGS_MAX], total = 0;
            const float struck = (*in_strike)[i];

            if (struck > 0 && hprev <= 0)
            {
                /* Launched from where the strings are, touching. */
                hy = ys[0];

                for (s = 1; s < strings; s++)
                    if (ys[s] > hy)
                        hy = ys[s];

                hv = (float)thClampArg((*in_velocity)[i], 0, 1);
                htime = 0;
            }

            hprev = struck;

            for (s = 0; s < strings; s++)
            {
                fromBridge[s] = -(lossz[s] - bridge) * kept;
                fromNear[s] = -near[s][(apos + lenA - da) % lenA];

                /* Where the string would be with no hammer on it. */
                base[s] = ys[s] + (fromBridge[s] + fromNear[s] +
                                   dcy2 * strike[s]) / rate;
                f[s] = 0;
            }

            if (htime < HAMMER_TIME * rate)
            {
                const double yh = hammerSolve(hy, hv, base, strike, strings,
                                              felt, exponent, hmass,
                                              1.0 / rate, f);

                for (s = 0; s < strings; s++)
                    total += f[s];

                hv = (float)(hv - total / rate / hmass);
                hy = (float)yh;
                htime++;
            }

            for (s = 0; s < strings; s++)
            {
                const double inject = dcy2 * strike[s] + f[s] / 2;

                y = flush(fromNear[s] + inject);
                line[s][at] = (float)y;
                near[s][apos] = (float)flush(fromBridge[s] + inject);
                ys[s] = (float)(base[s] + f[s] / 2 / rate);

                /* Heard at the bridge, not at the hammer: the wave as it
                   arrives there, through the dispersion and the loss. The
                   blow itself is never heard raw -- by the time it reaches
                   the board the string's stiffness has spread it out. */
                mean += -fromBridge[s] * strike[s];
            }

            force[i] = (float)total;
            apos = (apos + 1) % lenA;
        }

        mean /= strings;
        out[i] = (float)mean;

        peak = (float)(fabs(mean) > peak * release ? fabs(mean)
                                                   : peak * release);

        if (age < hold)
            age++;

        play[i] = (age < hold || peak > PLAY_FLOOR) ? 1 : 0;

        at = (at + 1) % len;
    }

    for (s = 0; s < strings; s++)
    {
        float *str = state + S_STRING + s * P_COUNT;

        str[P_THIRAN] = thz[s];
        str[P_LOSS] = lossz[s];

        for (k = 0; k < DISP_MAX; k++)
        {
            str[P_Z1 + k] = z1[s][k];
            str[P_Z2 + k] = z2[s][k];
        }
    }

    for (s = 0; s < strings; s++)
        state[S_YS + s] = ys[s];

    state[S_HY] = hy;
    state[S_HV] = hv;
    state[S_HTIME] = htime;
    state[S_HPREV] = hprev;
    state[S_APOS] = (float)apos;
    state[S_POS] = (float)at;
    state[S_AGE] = age;
    state[S_PEAK] = peak;
    state[S_DAMP] = damp;
    state[S_DCX] = dcx;
    state[S_DCY1] = dcy1;
    state[S_DCY2] = dcy2;

    return 0;
}
