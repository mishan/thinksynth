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

/* A low-pass gate: the West Coast's filter and amplifier in one, opened and
 * closed together through a vactrol -- a lamp and a photocell -- so a sound
 * gets darker as it gets quieter, the way a struck thing does.
 *
 * THE VACTROL is v, 0 shut to 1 open, following `control' or a strike: up
 * in about `open', down in about `close', and slower still the nearer it is
 * to shut (a photocell's tail), its fall three times as long at the bottom
 * as at the top. A rising `strike' holds it at `level' until it gets there
 * and then lets go, so a strike is a full ping whatever `open' is: the
 * plucked, woody note the gate is known for.
 *
 * From v, the low pass's cutoff, 20 Hz * 1000^v (20 Hz to 20 kHz), and the
 * gain, v^2. `mode' 1 is the filter alone, at unity gain; 2 the gain alone,
 * no filter at all. The filter is filt::svf's (filt/svf.h's zero-delay
 * state-variable form), which a cutoff moved every sample does not upset.
 *
 * The fall's rate and the filter's coefficient are worked out from v
 * rounded to 1/256, again only when that changes: a function of v alone, so
 * the output does not depend on where a window starts, and no tangent a
 * sample through every decay. The gain is from v itself.
 *
 * `play' is 1 while v is above 1e-4 or anything holds it open: a voice
 * whose note is a strike ends when it has rung out.
 */

#include <math.h>

#include "think.h"

#include "svf.h"

static const char desc[] = "Low-pass gate (a vactrol's filter and gain)";
thPlugin::State    mystate = thPlugin::ACTIVE;

enum { IN_ARG, IN_CONTROL, IN_STRIKE, IN_LEVEL, IN_OPEN, IN_CLOSE, IN_MODE,
       IN_RES, OUT_ARG, OUT_PLAY, INOUT_STATE };

std::atomic<int> args[INOUT_STATE + 1];

/* v's steps, for the fall's rate and the cutoff. */
#define LPG_STEPS 256

/* Where `play' says it has rung out. */
#define LPG_SILENT 1e-4f

void module_cleanup (thPlugin *plugin)
{
    (void)plugin;
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc(desc);
    plugin->setState(mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_CONTROL] = plugin->regArg("control", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_CONTROL], "How open, held: 0 shut, 1 open");
    plugin->setArgRange(args[IN_CONTROL], 0, 1);
    args[IN_STRIKE] = plugin->regArg("strike", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_STRIKE],
                       "Rising above 0, pings it open to `level', and lets "
                       "it fall");
    plugin->setArgRange(args[IN_STRIKE], 0, 1);
    args[IN_LEVEL] = plugin->regArg("level", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_LEVEL], "How far a strike opens it");
    plugin->setArgRange(args[IN_LEVEL], 0, 1);
    plugin->setArgDefault(args[IN_LEVEL], 1);
    args[IN_OPEN] = plugin->regArg("open", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_OPEN], "How long it takes to open");
    plugin->setArgUnits(args[IN_OPEN], "ms");
    plugin->setArgRange(args[IN_OPEN], 0.1f, 1000);
    plugin->setArgDefault(args[IN_OPEN], 5);
    args[IN_CLOSE] = plugin->regArg("close", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_CLOSE],
                       "How long it takes to close from open; slower near "
                       "shut");
    plugin->setArgUnits(args[IN_CLOSE], "ms");
    plugin->setArgRange(args[IN_CLOSE], 0.1f, 10000);
    plugin->setArgDefault(args[IN_CLOSE], 150);
    args[IN_MODE] = plugin->regArg("mode", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_MODE],
                       "0 filter and gain together, 1 the filter alone, 2 "
                       "the gain alone");
    plugin->setArgRange(args[IN_MODE], 0, 2);
    plugin->setArgStep(args[IN_MODE], 1);
    args[IN_RES] = plugin->regArg("res", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RES], "The low pass's resonance");
    plugin->setArgRange(args[IN_RES], 0, 0.95f);
    plugin->setArgDefault(args[IN_RES], 0.1f);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The signal, gated");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_PLAY] = plugin->regArg("play", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_PLAY],
                       "1 until it has rung out: a struck voice's length");
    plugin->setArgRange(args[OUT_PLAY], 0, 1);

    /* [0] v, [1] the last strike, [2] the level a strike holds it at, or
       -1, [3] [4] the filter, [5] the step of v the next two were worked
       out for, or -1, [6] the fall's rate there, [7] the filter's g there,
       [8] and [9] the `close' and the rate that was for, [10] and [11] the
       open's rate and the `open' it was for. */
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

static float clamp (float x, float lo, float hi)
{
    return x > lo ? (x < hi ? x : hi) : lo;     /* NaN to lo */
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg = mod->getArg(node, args[IN_ARG]);
    thArg *in_control = mod->getArg(node, args[IN_CONTROL]);
    thArg *in_strike = mod->getArg(node, args[IN_STRIKE]);
    thArg *in_level = mod->getArg(node, args[IN_LEVEL]);
    thArg *in_open = mod->getArg(node, args[IN_OPEN]);
    thArg *in_close = mod->getArg(node, args[IN_CLOSE]);
    thArg *in_mode = mod->getArg(node, args[IN_MODE]);
    thArg *in_res = mod->getArg(node, args[IN_RES]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);

    const bool fresh = inout_state->len() < 12;
    float v = (*inout_state)[0];
    float lastStrike = (*inout_state)[1];
    float holding = fresh ? -1 : (*inout_state)[2];
    thkSvf svf;

    svf.ic1 = (*inout_state)[3];
    svf.ic2 = (*inout_state)[4];

    int step = fresh ? -1 : (int)(*inout_state)[5];
    float fall = (*inout_state)[6], g = (*inout_state)[7];
    float fallFor = (*inout_state)[8], rateFor = (*inout_state)[9];
    float rise = (*inout_state)[10];
    float riseFor = fresh ? -1 : (*inout_state)[11];

    float *state = inout_state->allocate(12);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    float *play = mod->getArg(node, args[OUT_PLAY])->allocate(windowlen);
    const float rate = (float)samples;

    if (!(v >= 0 && v <= 1))
        v = 0;

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float strike = (*in_strike)[i];

        if (strike > 0 && !(lastStrike > 0))
            holding = clamp((*in_level)[i], 0, 1);

        lastStrike = strike;

        const float control = clamp((*in_control)[i], 0, 1);
        const float target = holding > control ? holding : control;
        const float open = clamp((*in_open)[i], 0.1f, 1000);
        const float close = clamp((*in_close)[i], 0.1f, 10000);

        if (open != riseFor)
        {
            rise = expf(-1000 / (open * rate));
            riseFor = open;
        }

        const int at = (int)lrintf(v * LPG_STEPS);

        if (at != step || close != fallFor || rate != rateFor)
        {
            const float q = (float)at / LPG_STEPS;
            const float hz = 20 * powf(1000, q);

            fall = expf(-1000 / (close * (1 + 2 * (1 - q)) * rate));
            g = tanf(3.14159265358979323846f *
                     clamp(hz, 0, thkSvf::CUTOFF_CEIL * rate) / rate);
            step = at;
            fallFor = close;
            rateFor = rate;
        }

        v += (target - v) * (1 - (target > v ? rise : fall));

        /* A strike holds it until it is there, then lets it fall. */
        if (holding >= 0 && v >= holding * 0.99f)
            holding = -1;

        const float x = (*in_arg)[i] / TH_MAX;
        const int mode = (int)clamp((*in_mode)[i], 0, 2);
        float y = x;

        /* A sample that is not a number never reaches the filter. */
        if (!thIsFinite(x))
            y = 0;
        else if (mode != 2)
        {
            const float k = 2 - 2 * clamp((*in_res)[i], 0, 0.95f);
            const float d = 1 / (1 + g * (g + k));
            const thkSvf::Ctl c = { k, d, g * d, g * g * d };
            float band, high;

            y = svf.tick(c, x, band, high);
        }

        out[i] = TH_MAX * (mode == 1 ? y : y * v * v);
        play[i] = v > LPG_SILENT || target > 0 ? 1 : 0;
    }

    state[0] = v;
    state[1] = lastStrike;
    state[2] = holding;
    state[3] = svf.ic1;
    state[4] = svf.ic2;
    state[5] = (float)step;
    state[6] = fall;
    state[7] = g;
    state[8] = fallFor;
    state[9] = rateFor;
    state[10] = rise;
    state[11] = riseFor;

    return 0;
}
