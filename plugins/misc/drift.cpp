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

/* Smoothed random: a value that wanders.
 *
 *     out = center + depth * (a + (b - a) * (1 - cos(pi * t)) / 2)
 *
 * with a and b drawn uniformly from -1 to 1 and t running from 0 to 1
 * over 1 / `rate' seconds; at the end of the run b becomes a and a new b
 * is drawn. So every 1 / rate seconds the output arrives at a new target,
 * by a half cosine, which starts and ends with no slope -- the derivative
 * is continuous everywhere, including at the joins, and a filter or a pan
 * this drives never lurches.
 *
 * It is what every ambient patch puts on four things at once -- the
 * cutoff, the detune, the pan, the level -- and it used to be three nodes
 * each: a noise source, a latch to hold one value of it and a lag to take
 * the edge off, which is a staircase with its corners rounded, not a
 * curve. Here it is one node and the curve is the point.
 *
 * DETERMINISTIC, from `seed'. The stream belongs to the voice and starts
 * where `seed' says on the voice's first sample (plugins/dice.h), so two
 * voices started with the same seed wander the same way and a piece
 * renders the same twice. A graph that wants each note to wander on its
 * own writes `seed = ionode->note', or anything else that differs between
 * notes. `seed' is read once, then never again.
 *
 * `rate' may move: the phase is how far through the current run the
 * output is, and a new rate only changes how fast the rest of it goes.
 * At 0 the output holds wherever it is.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"
#include "plugins/dice.h"

enum {IN_RATE, IN_DEPTH, IN_CENTER, IN_SEED, OUT_ARG, INOUT_STATE};
int args[INOUT_STATE + 1];

static const char desc[] = "Drift (smoothed random)";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* A new target every millisecond is already a noise, and past that the
   runs are shorter than the cosine's own shape can be heard in. */
#define DRIFT_RATE_MAX 1000.0f

/* Where each piece of the state lives. */
enum { S_DICE0, S_DICE1, S_SEEDED, S_PHASE, S_FROM, S_TO, S_COUNT };

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_RATE] = plugin->regArg("rate", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RATE],
                       "How often the output reaches a new target");
    plugin->setArgUnits(args[IN_RATE], "Hz");
    plugin->setArgRange(args[IN_RATE], 0, 20);
    args[IN_DEPTH] = plugin->regArg("depth", thPlugin::ARG_IN);
    /* No range and no unit, for misc::slew's reason: it is a cutoff in
       one graph and a pan in the next. */
    plugin->setArgDesc(args[IN_DEPTH],
                       "How far either side of `center' the targets fall");
    args[IN_CENTER] = plugin->regArg("center", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_CENTER], "What the output wanders about");
    args[IN_SEED] = plugin->regArg("seed", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SEED],
                       "Which way it wanders; read on the voice's first "
                       "sample");

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG],
                       "center + depth times a smooth path through random "
                       "points in -1..1");

    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_rate = mod->getArg(node, args[IN_RATE]);
    thArg *in_depth = mod->getArg(node, args[IN_DEPTH]);
    thArg *in_center = mod->getArg(node, args[IN_CENTER]);
    thArg *in_seed = mod->getArg(node, args[IN_SEED]);
    thArg *out_arg = mod->getArg(node, args[OUT_ARG]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);
    float *out = out_arg->allocate(windowlen);
    float *state = inout_state->allocate(S_COUNT);

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float rate = thClampArg((*in_rate)[i], 0, DRIFT_RATE_MAX);
        const float depth = thIsFinite((*in_depth)[i]) ? (*in_depth)[i] : 0;
        const float center = thIsFinite((*in_center)[i])
                                 ? (*in_center)[i] : 0;

        if (state[S_SEEDED] == 0)
        {
            thDiceSeed(&state[S_DICE0], (*in_seed)[i]);
            state[S_FROM] = (float)(2 * thDiceNext(&state[S_DICE0]) - 1);
            state[S_TO] = (float)(2 * thDiceNext(&state[S_DICE0]) - 1);
            state[S_PHASE] = 0;
            state[S_SEEDED] = 1;
        }

        const float t = state[S_PHASE];
        const float bend = (float)(0.5 - 0.5 * cos(M_PI * t));
        const float path = state[S_FROM] +
                           (state[S_TO] - state[S_FROM]) * bend;

        out[i] = center + depth * path;

        /* In float and stepped in float, for delay::chorus's reason: a
           window boundary is not an event. */
        state[S_PHASE] += rate / (float)samples;

        if (state[S_PHASE] >= 1.0f)
        {
            state[S_PHASE] -= floorf(state[S_PHASE]);
            state[S_FROM] = state[S_TO];
            state[S_TO] = (float)(2 * thDiceNext(&state[S_DICE0]) - 1);
        }
    }

    return 0;
}
