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

/* A single reed blown into a bore: a clarinet, or toward a saxophone.
 *
 * A digital waveguide, as Smith drew it and Cook's STK plays it (its
 * Saxofony): the bore is two delay lines meeting at the reed, `position'
 * of the way along, a period between them. At the bell, the open end, the
 * wave comes back inverted, a little quieter and a little darker (0.95 of
 * a two-point average). At the reed, breath meets what the bore sends back,
 * and the reed opens or closes by how much the breath is the stronger:
 *
 *     bell  = -0.95 avg2(upper)        sound = bell - lower
 *     diff  = breath - sound            reed  = offset + slope * diff, 0..1
 *     upper <- breath - diff * reed - bell,   lower <- bell
 *
 * Under a threshold breath the loop dies away and the reed does not speak;
 * over it, it settles into a tone. `position' 0.5, the middle, is the
 * clarinet, its odd harmonics strong; toward 0.2, a saxophone, every
 * harmonic, and a top a fourth or so above where a real one stops.
 * `stiffness' is the reed's: stiffer is brighter and needs more breath.
 *
 * THE REED IS HELD TO 0..1, where STK lets it reach -1: written out, what
 * goes into the bore is (1 - r) breath - (1 - r) bell - r lower, and its
 * gains add to 1 only for r in 0..1 -- with the bell's 0.95 that keeps the
 * loop from gaining, whatever is asked of it. A bound by construction: no
 * setting statecheck's sweep reaches drives r under 0 to show it.
 *
 * In tune by one constant: the loop is a period long less half a sample,
 * the bell filter's delay; measured to within 2 cents from C3 to C7 at the
 * middle position (statecheck holds it to 5). Breath moves it a little, as
 * on a real one. Lines are read with linear interpolation, their length
 * worked out every sample, so a glide is smooth.
 */

#include <math.h>
#include <stdint.h>

#include "think.h"

static const char desc[] = "Reed (a single reed blown into a bore)";
thPlugin::State    mystate = thPlugin::ACTIVE;

enum { IN_FREQ, IN_PRESSURE, IN_STIFFNESS, IN_NOISE, IN_POSITION, OUT_ARG,
       INOUT_STATE };

std::atomic<int> args[INOUT_STATE + 1];

/* The lowest note, which sizes the lines. */
#define REED_LOWEST 20.0f

/* What the loop's own delay takes off a period: the bell's two-point
   average, half a sample. */
#define REED_LOOP_DELAY 0.5f

/* The head of the state, before the two lines. */
enum { S_WRITE, S_PREV, S_NOISE, S_SIZE, S_HEAD };

/* Each line, long enough for the lowest note. */
static unsigned lineSize (unsigned int samples)
{
    return (unsigned)((float)samples / REED_LOWEST) + 8;
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
    plugin->setArgDesc(args[IN_FREQ], "The note");
    plugin->setArgUnits(args[IN_FREQ], "Hz");
    args[IN_PRESSURE] = plugin->regArg("pressure", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_PRESSURE],
                       "Breath: under about 0.5 it does not speak, more is "
                       "louder and brighter");
    plugin->setArgRange(args[IN_PRESSURE], 0, 1);
    args[IN_STIFFNESS] = plugin->regArg("stiffness", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_STIFFNESS],
                       "The reed's: stiffer is brighter and needs more "
                       "breath");
    plugin->setArgRange(args[IN_STIFFNESS], 0, 1);
    plugin->setArgDefault(args[IN_STIFFNESS], 0.5f);
    args[IN_NOISE] = plugin->regArg("noise", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_NOISE], "Breath noise in the pressure");
    plugin->setArgRange(args[IN_NOISE], 0, 1);
    args[IN_POSITION] = plugin->regArg("position", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_POSITION],
                       "Where along the bore the reed sits: 0.5 a clarinet, "
                       "0.2 toward a saxophone");
    plugin->setArgRange(args[IN_POSITION], 0.2f, 0.5f);
    plugin->setArgDefault(args[IN_POSITION], 0.5f);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The bore's sound at the reed");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    /* The head (S_*), then the upper line and the lower, each S_SIZE. */
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
    thArg *in_stiffness = mod->getArg(node, args[IN_STIFFNESS]);
    thArg *in_noise = mod->getArg(node, args[IN_NOISE]);
    thArg *in_position = mod->getArg(node, args[IN_POSITION]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);

    const float rate = (float)samples;
    const unsigned size = lineSize(samples);
    const bool kept = inout_state->len() == S_HEAD + 2 * size &&
                      (unsigned)(*inout_state)[S_SIZE] == size;

    /* Everything a window rounds into a float is kept in one between
       windows, so a window boundary is not an event. */
    unsigned write = kept ? (unsigned)(*inout_state)[S_WRITE] : 0;
    float prev = kept ? (*inout_state)[S_PREV] : 0;
    uint32_t dice = kept ? (uint32_t)(*inout_state)[S_NOISE] : 0x2545F4u;

    /* allocate() keeps what a buffer of this size held, and zeroes one of
       another: a voice's first window, or a rate changed. */
    float *state = inout_state->allocate(S_HEAD + 2 * size);

    float *upper = state + S_HEAD, *lower = upper + size;

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float hz = (*in_freq)[i];
        const float p = (*in_pressure)[i];
        const float stiff = (*in_stiffness)[i];
        const float grain = (*in_noise)[i];
        const float pos = (*in_position)[i];

        if (!thIsFinite(hz) || !thIsFinite(p) || !thIsFinite(stiff) ||
            !thIsFinite(grain) || !thIsFinite(pos))
        {
            out[i] = 0;
            continue;
        }

        const float f = clamp(hz, REED_LOWEST, rate / 8);
        const float where = clamp(pos, 0.2f, 0.5f);
        const float loop = rate / f - REED_LOOP_DELAY;
        const float up = (1 - where) * loop, low = where * loop;

        /* A 24-bit LCG, which a float holds exactly between windows. */
        dice = (dice * 1103515245u + 12345u) & 0xFFFFFFu;

        const float white = (float)dice / (float)0x800000 - 1;
        const float breath = clamp(p, 0, 1) *
                             (1 + clamp(grain, 0, 1) * white);
        const float s = clamp(stiff, 0, 1);
        const float slope = 0.1f + 0.4f * s, offset = 1.0f - 0.6f * s;

        const float u = readLine(upper, size, write, up);
        const float l = readLine(lower, size, write, low > 1 ? low : 1);
        const float bell = -0.95f * 0.5f * (u + prev);
        const float sound = bell - l;
        const float diff = breath - sound;
        const float r = clamp(offset + slope * diff, 0, 1);

        prev = u;
        upper[write] = breath - diff * r - bell;
        lower[write] = bell;
        write = write + 1 < size ? write + 1 : 0;

        /* No DC to take out: the bell's wave less itself, later. */
        out[i] = TH_MAX * 0.6f * sound;
    }

    state[S_WRITE] = (float)write;
    state[S_PREV] = prev;
    state[S_NOISE] = (float)dice;
    state[S_SIZE] = (float)size;

    return 0;
}
