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


/* An air jet blown across a flute's embouchure, into its bore.
 *
 * A digital waveguide, as Cook's STK plays it (its Flute): breath, less
 * what the bore sends back, travels the jet's length to its edge, where it
 * is split by the cubic x (x^2 - 1), held to -1..1; that drives the bore,
 * whose open ends send half of it back, darker:
 *
 *     back  = -lowpass(bore.out)
 *     jet  <- breath - 0.5 back           edge = clamp(cubic(jet.out))
 *     bore <- dcblock(edge) + 0.5 back    out  = bore.out
 *
 * The loop is the bore and the jet together, two periods long, and it
 * plays its second mode. `jet' is the jet's length against the bore's;
 * past 0.28 or 0.42 the loop leaves that mode at some notes, so it is held
 * there.
 *
 * WHERE THIS IS NOT STK'S: its bore filter is a fixed one-pole, so a low
 * note has many harmonics the loop barely damps and jumps an octave down;
 * here the pole tracks the note, its corner three times `freq', so the
 * loop is the same at every pitch. Its DC blocker's corner, near 35 Hz,
 * pulled the low notes too; this one is at 2 Hz.
 *
 * In tune by a correction that, with the filter tracking, depends on `jet'
 * alone: the loop is made 2 periods less the filter's phase delay at
 * `freq', times 2^(c/1200), c a quadratic in `jet' fitted to the
 * detuning measured at `pressure' 0.85 -- within 1.5 cents from G2 to
 * G#6, 2.5 at C2 (statecheck holds it to 5). Breath moves it a few cents,
 * as on a real one. Under C2 the loop
 * drops to its first mode, an octave down, so a lower note is held at C2.
 *
 * It speaks for a breath of about 0.88 to 1.3 of the cubic's scale: under,
 * the loop dies away; over, the cubic is held at its bound and only DC
 * gets through. Across it the tone is a V: at 1.1 the wave is symmetric
 * and has odd harmonics alone, a clarinet's; either side the even ones
 * come in. Under 1.1, more breath is brighter, as on a real flute, so
 * `pressure' 1 is 1.04, and the cube root of `pressure' gets there, so it
 * speaks from about 0.55, 0.6 at C2, near where osc::reed does.
 */

#include <math.h>
#include <stdint.h>

#include "think.h"

static const char desc[] = "Jet (an air jet blown into a flute's bore)";
thPlugin::State    mystate = thPlugin::ACTIVE;

enum { IN_FREQ, IN_PRESSURE, IN_JET, IN_NOISE, OUT_ARG, INOUT_STATE };

std::atomic<int> args[INOUT_STATE + 1];

/* The lowest note, C2, which sizes the lines: under it the loop plays its
   first mode, an octave down, at every `jet'. */
#define JET_LOWEST 65.41f

/* The breath `pressure' 1 is, on the cubic's scale. */
#define JET_BREATH 1.04f

/* The bore filter's corner, in multiples of the note. */
#define JET_CORNER 3.0f

/* The DC blocker's corner, Hz. */
#define JET_DC 2.0f

/* `jet's range, where the loop plays its second mode at every note. */
#define JET_SHORT 0.28f
#define JET_LONG 0.42f

/* The head of the state, before the bore line and the jet line. */
enum { S_WRITE, S_LOW, S_DCIN, S_DCOUT, S_NOISE, S_SIZE, S_HEAD };

/* Each line, long enough for the bore at the lowest note: two periods,
   sharpened at most 33 cents, over 1 + JET_SHORT. */
static unsigned lineSize (unsigned int samples)
{
    return (unsigned)((float)samples / JET_LOWEST * 1.65f) + 8;
}

/* A voice about to start: its lines, sized here and not in its first
   window. */
void module_prepare (thNode *node, thSynthTree *mod, unsigned int samples,
                     int voice)
{
    if (voice)
        mod->getArg(node, args[INOUT_STATE])->reserve(S_HEAD +
                                                      2 * lineSize(samples));
}

void module_cleanup (thPlugin *plugin)
{
    (void)plugin;
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc(desc);
    plugin->setState(mystate);

    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FREQ], "The note, from C2 up");
    plugin->setArgUnits(args[IN_FREQ], "Hz");
    args[IN_PRESSURE] = plugin->regArg("pressure", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_PRESSURE],
                       "Breath: under about 0.55 it does not speak (0.6 at "
                       "C2), more is brighter");
    plugin->setArgRange(args[IN_PRESSURE], 0, 1);
    args[IN_JET] = plugin->regArg("jet", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_JET],
                       "The jet's length against the bore's");
    plugin->setArgRange(args[IN_JET], JET_SHORT, JET_LONG);
    plugin->setArgDefault(args[IN_JET], 0.32f);
    args[IN_NOISE] = plugin->regArg("noise", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_NOISE], "Breath noise in the pressure");
    plugin->setArgRange(args[IN_NOISE], 0, 1);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The bore's sound");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    /* The head (S_*), then the bore line and the jet line, each S_SIZE. */
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

static float clamp (float x, float lo, float hi)
{
    return x > lo ? (x < hi ? x : hi) : lo;     /* NaN to lo */
}

/* `line' read `delay' samples before `write', linearly between two. */
static float readLine (const float *line, unsigned size, unsigned write,
                       float delay)
{
    const float at = (float)write - delay;
    const float base = floorf(at);
    const float frac = at - base;
    const long i = (long)base;
    const unsigned a = (unsigned)(((i % (long)size) + (long)size) % (long)size);
    const unsigned b = a + 1 < size ? a + 1 : 0;

    return line[a] + (line[b] - line[a]) * frac;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_freq = mod->getArg(node, args[IN_FREQ]);
    thArg *in_pressure = mod->getArg(node, args[IN_PRESSURE]);
    thArg *in_jet = mod->getArg(node, args[IN_JET]);
    thArg *in_noise = mod->getArg(node, args[IN_NOISE]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);

    const float rate = (float)samples;
    const unsigned size = lineSize(samples);
    const bool kept = inout_state->len() == S_HEAD + 2 * size &&
                      (unsigned)(*inout_state)[S_SIZE] == size;

    unsigned write = kept ? (unsigned)(*inout_state)[S_WRITE] : 0;
    float low = kept ? (*inout_state)[S_LOW] : 0;
    float dcIn = kept ? (*inout_state)[S_DCIN] : 0;
    float dcOut = kept ? (*inout_state)[S_DCOUT] : 0;
    uint32_t dice = kept ? (uint32_t)(*inout_state)[S_NOISE] : 0x2545F4u;

    float *state = inout_state->allocate(S_HEAD + 2 * size);

    float *bore = state + S_HEAD, *jetLine = bore + size;
    const float dcPole = expf(-2 * (float)M_PI * JET_DC / rate);

    /* The loop's lengths, worked out again only when the note or the jet
       moves. */
    float lastF = -1, lastJ = -1, pole = 0, boreLen = 1, jetLen = 1;

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float hz = (*in_freq)[i];
        const float p = (*in_pressure)[i];
        const float jt = (*in_jet)[i];
        const float grain = (*in_noise)[i];

        if (!thIsFinite(hz) || !thIsFinite(p) || !thIsFinite(jt) ||
            !thIsFinite(grain))
        {
            out[i] = 0;
            continue;
        }

        const float f = clamp(hz, JET_LOWEST, rate / 8);
        const float j = clamp(jt, JET_SHORT, JET_LONG);

        if (f != lastF || j != lastJ)
        {
            const float w = 2 * (float)M_PI * f / rate;
            const float cents = (-848.30f * j + 976.39f) * j - 230.05f;

            pole = expf(-JET_CORNER * w);

            const float lag = atan2f(pole * sinf(w), 1 - pole * cosf(w)) / w;

            boreLen = (2 * rate / f * exp2f(cents / 1200) - lag) / (1 + j);
            jetLen = boreLen * j;
            lastF = f;
            lastJ = j;
        }

        dice = (dice * 1103515245u + 12345u) & 0xFFFFFFu;

        const float white = (float)dice / (float)0x800000 - 1;
        const float breath = JET_BREATH * cbrtf(clamp(p, 0, 1)) *
                             (1 + clamp(grain, 0, 1) * white);

        const float sound = readLine(bore, size, write, boreLen);

        low = (1 - pole) * sound + pole * low;

        const float back = -low;

        jetLine[write] = breath - 0.5f * back;

        const float x = readLine(jetLine, size, write, jetLen);
        const float edge = clamp(x * (x * x - 1), -1, 1);
        const float blocked = edge - dcIn + dcPole * dcOut;

        dcIn = edge;
        dcOut = blocked;
        bore[write] = blocked + 0.5f * back;
        write = write + 1 < size ? write + 1 : 0;

        out[i] = TH_MAX * 0.3f * sound;
    }

    state[S_WRITE] = (float)write;
    state[S_LOW] = low;
    state[S_DCIN] = dcIn;
    state[S_DCOUT] = dcOut;
    state[S_NOISE] = (float)dice;
    state[S_SIZE] = (float)size;

    return 0;
}
