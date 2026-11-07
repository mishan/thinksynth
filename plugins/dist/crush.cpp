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

/* Crush: fewer bits and a lower sample rate, the way an early sampler
 * had them.
 *
 * THE RATE is a sample-and-hold. A phase runs up by `rate' over the
 * synth's rate each sample; when it passes 1 the input is caught and held
 * until it passes 1 again. There is no filter in front of it, on purpose:
 * an SP-1200 at 26 kHz or an S950 turned down to 15 had none worth the
 * name, and the tones folded down from above the new Nyquist are the
 * sound being asked for.
 *
 * THE BITS round what is caught to a grid with 2^(bits - 1) steps either
 * side of zero. `bits' may be fractional, so it can be swept without
 * stepping from one grid to the next.
 *
 * 0 in either leaves that half alone, so both are written only when
 * wanted. The phase and the held sample are the state, kept as floats,
 * so what is caught does not depend on where a window ends.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

enum { IN_ARG, IN_BITS, IN_RATE, OUT_ARG, INOUT_STATE };

std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "Bit crusher (fewer bits, a lower sample rate)";
thPlugin::State    mystate = thPlugin::PASSIVE;

/* Where each piece of the state lives. */
enum { S_PHASE, S_HELD, S_LEN };

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
    args[IN_BITS] = plugin->regArg("bits", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_BITS],
                       "How many bits each sample keeps; 0 keeps them all");
    plugin->setArgRange(args[IN_BITS], 0, 24);
    plugin->setArgUnits(args[IN_BITS], "bits");
    args[IN_RATE] = plugin->regArg("rate", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RATE],
                       "How often a new sample is caught, with nothing "
                       "filtered first; 0 catches every one");
    plugin->setArgRange(args[IN_RATE], 0, 48000);
    plugin->setArgUnits(args[IN_RATE], "Hz");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The input, crushed");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg = mod->getArg(node, args[IN_ARG]);
    thArg *in_bits = mod->getArg(node, args[IN_BITS]);
    thArg *in_rate = mod->getArg(node, args[IN_RATE]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);

    float st[S_LEN];

    for (int k = 0; k < S_LEN; k++)
        st[k] = (unsigned)k < inout_state->len() ? (*inout_state)[k] : 0;

    float *state = inout_state->allocate(S_LEN);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);

    for (unsigned i = 0; i < windowlen; i++)
    {
        const float in = thIsFinite((*in_arg)[i]) ? (*in_arg)[i] : 0;
        const float rate = thClampArg((*in_rate)[i], 0, (float)samples);
        const float bits = thClampArg((*in_bits)[i], 0, 24);

        if (rate <= 0 || rate >= samples)
            st[S_HELD] = in;
        else
        {
            st[S_PHASE] += rate / samples;
            if (st[S_PHASE] >= 1)
            {
                st[S_PHASE] -= 1;
                st[S_HELD] = in;
            }
        }

        float y = st[S_HELD];

        /* Under one bit there is no grid left but zero and the two rails. */
        if (bits > 0)
        {
            const float steps = powf(2, fmaxf(bits, 1) - 1);

            y = thClampArg(roundf(y * steps) / steps, TH_MIN, TH_MAX);
        }

        out[i] = y;
    }

    memcpy(state, st, sizeof(st));

    return 0;
}
