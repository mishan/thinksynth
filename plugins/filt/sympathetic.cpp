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

/* Sympathetic strings: every string on a piano, listening.
 *
 * A struck string moves the bridge, and the bridge moves every other
 * string on it. Those whose dampers are off ring in sympathy -- at their
 * own pitches, wherever a partial of what was played lands near one of
 * theirs. With the sustain pedal down that is all of them, and it is the
 * halo a piano played with the pedal has and a piano played without it
 * does not (Lehtonen, Penttinen, Rauhala and Välimäki, "Analysis and
 * modeling of piano sustain-pedal effects", JASA 2007).
 *
 * A voice cannot do this: it cannot hear the others. This runs on the
 * channel, as an effect, on the sum of every voice. One loop per key from
 * `low' to `high', tuned to equal temperament at A = 440, each
 *
 *     y[n] = (1 - g_free) in[n] + g lp(y[n - rate/f])
 *
 * -- filt::comb's string, with its input scaled by 1 - g_free so that a
 * free string passes a sine at its own pitch at unity whatever its decay
 * (g here is the whole trip's gain at that pitch, the low-pass's included),
 * and
 * `out' is the bank's sum. What the channel hears is what the bank gives
 * back on top of the dry signal, and how much is the graph's to say.
 *
 * THE DAMPERS. Each loop's per-trip gain g moves between two: the free
 * string's, from `decay', and the damped one's, from `damper'. `pedal' is
 * where between them, 0 up to 1 down, followed over PEDAL_TIME so that
 * the felt lifts and lands rather than switching -- and a half pedal is a
 * half-damped string, which is what half-pedaling is. Keys from
 * `undamped' up have no damper on a grand and are always free.
 *
 * `decay' is the T60 at middle C; the strings below ring longer and those
 * above shorter, doubling every DECAY_KEYS keys down as dsp/grand.dsp's
 * do. `damp' is how much darker each trip is, with its phase delay taken
 * off the line so the loop stays in tune. The line's fraction is a
 * first-order Thiran allpass, as in filt::pianostring, and not a linear
 * read: a linear read is a low-pass too, and in a loop it would take a
 * free string's gain below unity and shorten its decay, more the higher
 * the key.
 *
 * What is not here: the keys that are held down with the pedal up are
 * free too, and this cannot know which they are.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

static const char desc[] = "Sympathetic strings (a piano's, with a pedal)";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* The keyboard, as MIDI notes, and the most strings the bank holds. */
#define KEY_LOWEST 21
#define KEY_HIGHEST 108
#define KEYS (KEY_HIGHEST - KEY_LOWEST + 1)

/* How long the dampers take to lift or land, in seconds. */
#define PEDAL_TIME 0.05

/* The decay curve: `decay' at middle C, doubling this many keys down. */
#define DECAY_KEYS 17.0

#define DECAY_MIN 0.01f
#define DECAY_MAX 200.0f
#define DECAY_DEFAULT 8.0f
#define DAMPER_DEFAULT 0.1f
#define DAMP_MAX 0.95f

/* A trip's gain at DC, where the low-pass passes everything, may not
   reach 1. Dividing by the low-pass's gain at the string's pitch would
   take it past that on a high key with `damp' up, and there a string
   rings shorter than `decay' rather than for ever. */
#define LOOP_GAIN_MAX 0.9999

#define FLUSH 1e-30

enum {IN_ARG, IN_PEDAL, IN_LOW, IN_HIGH, IN_UNDAMPED, IN_DECAY, IN_DAMPER,
      IN_DAMP, OUT_ARG, INOUT_BUFFER, INOUT_STATE};

int args[INOUT_STATE + 1];

/* The state: a few scalars, then per key its line's start in the buffer,
   its length, its write position, its two gains, its read delay and its
   low-pass memory. */
enum {
    S_PEDAL,                /* the dampers, followed */
    S_DECAY,                /* the inputs the layout below is for */
    S_DAMPER,
    S_DAMP,
    S_RATE,
    S_START,                /* KEYS line starts */
    S_LEN = S_START + KEYS, /* KEYS line lengths */
    S_POS = S_LEN + KEYS,   /* KEYS write positions */
    S_FREE = S_POS + KEYS,  /* KEYS free gains */
    S_HELD = S_FREE + KEYS, /* KEYS damped gains */
    S_BACK = S_HELD + KEYS, /* KEYS integer read delays */
    S_ETA = S_BACK + KEYS,  /* KEYS Thiran coefficients */
    S_THIRAN = S_ETA + KEYS, /* KEYS Thiran states */
    S_LP = S_THIRAN + KEYS, /* KEYS low-pass memories */
    S_IN = S_LP + KEYS,     /* KEYS input gains */
    S_COUNT = S_IN + KEYS
};

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "What the bridge carries: the channel");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");

    args[IN_PEDAL] = plugin->regArg("pedal", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_PEDAL],
                       "The sustain pedal: 0 up, 1 down, between is a half "
                       "pedal");
    plugin->setArgRange(args[IN_PEDAL], 0, 1);

    args[IN_LOW] = plugin->regArg("low", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_LOW], "The lowest string; read once per window");
    plugin->setArgUnits(args[IN_LOW], "MIDI note");
    plugin->setArgRange(args[IN_LOW], KEY_LOWEST, KEY_HIGHEST);
    plugin->setArgDefault(args[IN_LOW], KEY_LOWEST);

    args[IN_HIGH] = plugin->regArg("high", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_HIGH],
                       "The highest string; read once per window");
    plugin->setArgUnits(args[IN_HIGH], "MIDI note");
    plugin->setArgRange(args[IN_HIGH], KEY_LOWEST, KEY_HIGHEST);
    plugin->setArgDefault(args[IN_HIGH], KEY_HIGHEST);

    args[IN_UNDAMPED] = plugin->regArg("undamped", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_UNDAMPED],
                       "The lowest string with no damper; read once per "
                       "window");
    plugin->setArgUnits(args[IN_UNDAMPED], "MIDI note");
    plugin->setArgRange(args[IN_UNDAMPED], KEY_LOWEST, KEY_HIGHEST + 1);
    plugin->setArgDefault(args[IN_UNDAMPED], 90);

    args[IN_DECAY] = plugin->regArg("decay", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DECAY],
                       "A free string's T60 at middle C, doubling every "
                       "seventeen keys down");
    plugin->setArgUnits(args[IN_DECAY], "seconds");
    plugin->setArgRange(args[IN_DECAY], DECAY_MIN, DECAY_MAX);
    plugin->setArgDefault(args[IN_DECAY], DECAY_DEFAULT);

    args[IN_DAMPER] = plugin->regArg("damper", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DAMPER], "A damped string's T60");
    plugin->setArgUnits(args[IN_DAMPER], "seconds");
    plugin->setArgRange(args[IN_DAMPER], DECAY_MIN, DECAY_MAX);
    plugin->setArgDefault(args[IN_DAMPER], DAMPER_DEFAULT);

    args[IN_DAMP] = plugin->regArg("damp", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DAMP],
                       "How much darker each trip is; 0 rings every "
                       "partial as long as the fundamental");
    plugin->setArgRange(args[IN_DAMP], 0, DAMP_MAX);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "Every string, summed");
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

static double keyHz (int key)
{
    return 440.0 * pow(2.0, (key - 69) / 12.0);
}

/* How far the one-pole holds the signal back at the loop's own pitch, as
   filt::comb takes it off. */
static double dampDelay (double damp, double period)
{
    const double w = 2.0 * M_PI / period;

    if (damp <= 0)
        return 0;

    return atan2(damp * sin(w), 1.0 - damp * cos(w)) / w;
}

/* Phase lag at w of the first-order allpass (a + z^-1) / (1 + a z^-1). */
static double lagFirst (double a, double w)
{
    return w - 2.0 * atan2(a * sin(w), 1.0 + a * cos(w));
}

/* The longest the buffer can need: every key's line at this rate. */
static unsigned int capacity (unsigned int rate)
{
    unsigned int total = 0;

    for (int key = KEY_LOWEST; key <= KEY_HIGHEST; key++)
        total += (unsigned int)ceil(rate / keyHz(key)) + 4;

    return total;
}

/* Every key's line, gains and read delay. The lines are packed end to end,
   each a few samples longer than its period so the read sits inside it. */
static void layout (float *state, double decay, double damper, double damp,
                    unsigned int rate)
{
    unsigned int start = 0;

    for (int k = 0; k < KEYS; k++)
    {
        const double period = rate / keyHz(KEY_LOWEST + k);
        const double t60 = decay * pow(2.0, (60 - (KEY_LOWEST + k)) /
                                            DECAY_KEYS);
        const double w = 2.0 * M_PI / period;
        double total = period - dampDelay(damp, period);
        double lo = 0.4, hi = 1.6, frac;
        int line, i;

        if (total < 2)
            total = 2;

        /* The integer part, and the fraction solved against the Thiran's
           own phase at the string's pitch. */
        line = (int)floor(total - 0.5);

        for (i = 0; i < 40; i++)
        {
            const double mid = 0.5 * (lo + hi);

            if (line * w + lagFirst((1.0 - mid) / (1.0 + mid), w) < total * w)
                lo = mid;
            else
                hi = mid;
        }

        frac = 0.5 * (lo + hi);

        state[S_START + k] = (float)start;
        state[S_LEN + k] = (float)(ceil(period) + 4);
        /* The per-trip gains the two T60s want, divided by what the
           low-pass already takes at the string's pitch, so `damp' darkens
           the partials without shortening the fundamental. The input is
           scaled by one less the free gain: at its own pitch a free string
           then gives back what it was given. */
        {
            const double lpgain = (1 - damp) /
                                  sqrt(1 - 2 * damp * cos(w) + damp * damp);
            const double free = pow(10.0, -3.0 * period / (rate * t60));
            const double held = pow(10.0, -3.0 * period / (rate * damper));

            state[S_FREE + k] = (float)fmin(free / lpgain, LOOP_GAIN_MAX);
            state[S_HELD + k] = (float)fmin(held / lpgain, LOOP_GAIN_MAX);
            state[S_IN + k] = (float)(1 - free);
        }

        state[S_BACK + k] = (float)line;
        state[S_ETA + k] = (float)((1.0 - frac) / (1.0 + frac));

        start += (unsigned int)state[S_LEN + k];
    }
}

static inline double flush (double x)
{
    return fabs(x) < FLUSH ? 0 : x;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg, *in_pedal, *in_low, *in_high, *in_undamped, *in_decay;
    thArg *in_damper, *in_damp, *out_arg, *inout_buffer, *inout_state;
    float *out, *buffer, *state;
    const double follow = 1.0 - exp(-1.0 / (PEDAL_TIME * samples));
    double decay, damper, damp;
    float pedal;
    int low, high, undamped, k;
    unsigned int i, cap;

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_pedal = mod->getArg(node, args[IN_PEDAL]);
    in_low = mod->getArg(node, args[IN_LOW]);
    in_high = mod->getArg(node, args[IN_HIGH]);
    in_undamped = mod->getArg(node, args[IN_UNDAMPED]);
    in_decay = mod->getArg(node, args[IN_DECAY]);
    in_damper = mod->getArg(node, args[IN_DAMPER]);
    in_damp = mod->getArg(node, args[IN_DAMP]);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);

    inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
    inout_state = mod->getArg(node, args[INOUT_STATE]);
    state = inout_state->allocate(S_COUNT);

    /* Sized for every key once, at the first window: the lines are laid
       out for all of them whatever `low' and `high' say, so moving those
       costs nothing and allocates nothing. */
    cap = capacity(samples);
    buffer = inout_buffer->allocate(cap);

    low = (*in_low)[0] == 0 ? KEY_LOWEST
                            : (int)thClampArg((*in_low)[0], KEY_LOWEST,
                                              KEY_HIGHEST);
    high = (*in_high)[0] == 0 ? KEY_HIGHEST
                              : (int)thClampArg((*in_high)[0], KEY_LOWEST,
                                                KEY_HIGHEST);
    undamped = (*in_undamped)[0] == 0
               ? 90
               : (int)thClampArg((*in_undamped)[0], KEY_LOWEST,
                                 KEY_HIGHEST + 1);
    decay = (*in_decay)[0] == 0 ? DECAY_DEFAULT
                                : thClampArg((*in_decay)[0], DECAY_MIN,
                                             DECAY_MAX);
    damper = (*in_damper)[0] == 0 ? DAMPER_DEFAULT
                                  : thClampArg((*in_damper)[0], DECAY_MIN,
                                               DECAY_MAX);
    damp = thClampArg((*in_damp)[0], 0, DAMP_MAX);

    if ((float)decay != state[S_DECAY] || (float)damper != state[S_DAMPER] ||
        (float)damp != state[S_DAMP] || (float)samples != state[S_RATE])
    {
        layout(state, decay, damper, damp, samples);

        state[S_DECAY] = (float)decay;
        state[S_DAMPER] = (float)damper;
        state[S_DAMP] = (float)damp;
        state[S_RATE] = (float)samples;
    }

    pedal = state[S_PEDAL];

    for (i = 0; i < windowlen; i++)
    {
        const float raw = (*in_arg)[i];
        const double in = thIsFinite(raw) ? raw : 0;
        double sum = 0;

        pedal = (float)(pedal +
                        (thClampArg((*in_pedal)[i], 0, 1) - pedal) * follow);

        for (k = low - KEY_LOWEST; k <= high - KEY_LOWEST; k++)
        {
            float *line = buffer + (unsigned int)state[S_START + k];
            const unsigned int len = (unsigned int)state[S_LEN + k];
            const unsigned int w = (unsigned int)state[S_POS + k] % len;
            const unsigned int back = (unsigned int)state[S_BACK + k];
            const double eta = state[S_ETA + k];
            const double free = state[S_FREE + k];
            const double g = (KEY_LOWEST + k >= undamped)
                             ? free
                             : state[S_HELD + k] +
                               (free - state[S_HELD + k]) * pedal;
            const double x = line[(w + len - back) % len];
            const double t = eta * x + state[S_THIRAN + k];
            const double lp = (1 - damp) * t + damp * state[S_LP + k];
            const double y = flush(state[S_IN + k] * in + g * lp);

            state[S_THIRAN + k] = (float)flush(x - eta * t);
            state[S_LP + k] = (float)flush(lp);
            line[w] = (float)y;
            state[S_POS + k] = (float)((w + 1) % len);
            sum += y;
        }

        out[i] = (float)sum;
    }

    state[S_PEDAL] = pedal;

    return 0;
}
