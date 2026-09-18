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

/* A one-pole lag: the output chases the input instead of jumping to it.
 *
 *     y += (x - y) * k,   k = 1 - exp(-1 / time)
 *
 * which is the step response y(n) = 1 - exp(-n / time). At n = time the
 * output has covered 1 - 1/e of the distance -- 63% -- and that is what
 * `time' means here, the time constant rather than the time to arrive. Four
 * of them is within two percent.
 *
 * The answer to a composer stepping a chanarg once a period: a gen::walk
 * writing a new value every beat is a staircase, and the lag that turns it
 * into a line belongs inside the instrument, where it runs at the sample
 * rate, rather than in the composer, which does not run between steps.
 *
 * It starts where its input is, not at zero. A .dsp is copied per note --
 * every voice gets its own graph and its own state -- so a lag that began at
 * zero would begin at zero *again on every note*, and a lag on a frequency
 * would start each note at 0 Hz and slide up into it. What a lag is for is
 * the changes, so the first sample of a voice's life is taken as where the
 * lag already is. A consequence worth knowing: an arg that does not move
 * during a note cannot be lagged, because there is nothing to lag. The thing
 * to put a lag on is a chanarg, which is what moves under a held note.
 *
 * Arithmetic cannot be this. `out = in * 0.05 + out * 0.95' is the same
 * equation and would need to read its own output, which an expression
 * desugars into a cycle rather than a delay -- so a lag is a node, and one
 * with state.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

enum {IN_ARG, IN_TIME, OUT_ARG, INOUT_LAST};
int args[INOUT_LAST + 1];

static const char desc[] = "One-pole lag";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    /* No range and no unit: a lag does not care what it is lagging. It is
       written on a frequency as often as on a level, and saying "full scale"
       here would be a claim about the one use. */
    plugin->setArgDesc(args[IN_ARG], "The signal to chase");
    args[IN_TIME] = plugin->regArg("time", thPlugin::ARG_IN);
    /* Samples, so a .dsp writes `20 ms' and the unit fold turns it into the
       rate's worth. No range: there is a floor of 0 and no ceiling short of
       a lag slower than any note is long. */
    plugin->setArgDesc(args[IN_TIME],
                       "Time constant: how long to cover 63% of a step. "
                       "Under one sample passes the input straight through");
    plugin->setArgUnits(args[IN_TIME], "samples");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "in, lagged");

    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    float *out_last;
    thArg *in_arg, *in_time;
    thArg *out_arg;
    thArg *inout_last;
    unsigned int i;
    float y;
    bool primed;

    /* The coefficient, and the `time' it was worked out from. `time' is a
       buffer like every other arg and may move every sample, but it usually
       does not -- so exp() is called when the number changes rather than once
       a sample. A NaN never compares equal to itself, which sends it down the
       recompute path, where thClampArg answers the floor. */
    float lastTime = 0, k = 1;
    bool haveK = false;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    inout_last = mod->getArg(node, args[INOUT_LAST]);

    y = (*inout_last)[0];
    /* [1] is whether [0] means anything yet. A state buffer arrives zeroed
       and 0 is a perfectly good lagged value, so there is no sentinel
       available in [0] itself. */
    primed = (*inout_last)[1] != 0;
    out_last = inout_last->allocate(2);

    out = out_arg->allocate(windowlen);

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_time = mod->getArg(node, args[IN_TIME]);

    for (i = 0; i < windowlen; i++)
    {
        const float x = (*in_arg)[i];
        const float t = (*in_time)[i];

        if (!haveK || t != lastTime)
        {
            /* Not at least 1 -- so a zero, a negative and a NaN all land
               here -- is a lag of less than a sample, which is no lag: k of
               1 copies the input. Above that, exp(-1/t) is in (0, 1) and so
               is k, which is the stability condition for the whole plugin:
               the pole sits at 1 - k. */
            k = (t >= 1.0f) ? (float)(1.0 - exp(-1.0 / (double)t)) : 1.0f;
            lastTime = t;
            haveK = true;
        }

        /* The first sample of this voice, and the restart after a bad one:
           both are a lag with no history, and a lag with no history has
           caught up. Otherwise one non-finite sample would be read back for
           ever, since here the state is the output. */
        if (!primed || !thIsFinite(y))
        {
            y = thIsFinite(x) ? x : 0.0f;
            primed = true;
        }
        else
            y += (x - y) * k;

        out[i] = y;
    }

    out_last[0] = y;
    out_last[1] = 1;

    return 0;
}
