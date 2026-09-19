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

/* One sine operator, the unit a DX patch is written in.
 *
 *     out   = sin(2*pi*phase + index*mod + feedback*own recent output)
 *     phase = phase + freq*ratio/rate
 *
 * and that is the whole node. What it is for is what a graph builds out
 * of several of them: one operator's `out' on another's `mod' is a
 * two-operator patch, four of them wired up is any of the thirty-two
 * algorithms a piece would want. There is no algorithm node and there
 * will not be one -- the graph *is* the algorithm, and an expression on
 * `mod' (`mod = a->out * 0.6 + b->out') is the mixing an algorithm's
 * branches do.
 *
 * PHASE MODULATION, NOT FREQUENCY MODULATION, which is the reason this
 * node exists beside `osc::simple'. simple's `fm' input adds to the
 * phase *increment* -- `position += 1 + fm*fmamt' -- so it is through-zero
 * FM, and two things follow that make a DX patch impossible to write on
 * it. A modulator with any DC in it moves the increment permanently and
 * the note goes out of tune; and the deviation a given `fmamt' produces
 * is a fixed number of samples, so the same setting is a different
 * timbre at every pitch. Adding to the phase itself has neither problem:
 * a DC modulator shifts the wave along and leaves its frequency alone,
 * and `index' is radians, which is a ratio of the cycle and therefore
 * the same sound at every pitch. That last property is what lets a patch
 * be a patch rather than a note.
 *
 * NO `amp'. An operator's level is not its own business: a modulator's
 * level is the `index' of the operator it feeds, and a carrier's level
 * is whatever the graph multiplies its output by -- an envelope through
 * `mixer::mul', the same as every other voice in the tree. A DX patch
 * spells both of those "output level", which is why the sound gets
 * brighter when it is hit harder on a real one: velocity is on the
 * modulator's index, not on the carrier's gain. A graph writes that
 * as `index = env->out * @index * ionode->velocity'.
 *
 * FEEDBACK is the operator's own output back into its own phase, the
 * DX7's op-6 trick and the only way one operator makes anything but a
 * sine. The recurrence y = sin(x + b*y) has a closed form -- its nth
 * harmonic is 2*J_n(n*b)/(n*b) -- which is a sine at b = 0 and, at
 * b = 1, a wave whose slope goes vertical once a cycle: a sawtooth, near
 * enough, and the reason the knob's top is one radian rather than
 * something rounder. Past that the implicit equation is multivalued and
 * the recursion is noise, so `feedback' is clamped to 0..1 and means
 * radians like `index' does.
 *
 * THE LAST TWO OUTPUTS, AVERAGED, are what goes round that loop rather
 * than the last one alone. A one-sample loop at high feedback rings at
 * Nyquist -- the cheapest oscillator there is, and inaudible until it
 * intermodulates with everything else -- and a two-sample average is a
 * one-zero low-pass with a null exactly there. It is what the hardware
 * did, for the same reason.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thSynthTree.h"
#include "thSynth.h"

enum {IN_FREQ, IN_RATIO, IN_MOD, IN_INDEX, IN_FEEDBACK, IN_RESET,
      OUT_ARG, INOUT_STATE};

int args[INOUT_STATE + 1];

static const char desc[] = "FM Operator (a sine whose phase is modulated)";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* Radians. The index a DX patch asks for is a handful -- a bell is two
   or three, a bass is one falling to nothing -- and a two-operator pair
   is already noise by twenty, where the sidebands reach past Nyquist in
   both directions. Declared rather than enforced, like every other
   range in the tree: the number is a slider's travel and not a law, and
   the callback below clamps the bottom of it and not the top.
 *
   Nothing is bought by clamping the top anyway. `mod' is not bounded --
   it is whatever the graph put there, and an expression mixing an
   algorithm's branches will hand over more than full scale as a matter
   of course -- so the phase deviation this node actually applies is
   `index * mod', and a ceiling on one factor of a product is not a
   ceiling on the product. What a big index does is alias, which is a
   sound and not a fault, and which every other oscillator here is
   likewise free to make. */
#define FMOP_INDEX_MAX      20.0f

/* Radians again, and this one *is* enforced, because 1 is where the
   closed form above stops having a solution. See the head. */
#define FMOP_FEEDBACK_MAX   1.0f

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FREQ], "The voice's frequency");
    /* No range: the top is Nyquist, which is not a number this call
       knows. Every hertz arg in the tree is the same. */
    plugin->setArgUnits(args[IN_FREQ], "Hz");
    args[IN_RATIO] = plugin->regArg("ratio", thPlugin::ARG_IN);
    /* The whole of a DX patch's tuning. Whole numbers are harmonic and
       everything else is not: 1:3.5 is a bell because 3.5 has no
       common measure with 1, and no envelope or filter can fake that. */
    plugin->setArgDesc(args[IN_RATIO],
                       "This operator runs at `freq' times this");
    plugin->setArgUnits(args[IN_RATIO], "ratio");
    plugin->setArgDefault(args[IN_RATIO], 1);
    args[IN_MOD] = plugin->regArg("mod", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_MOD],
                       "Modulator in, usually another operator's `out'");
    plugin->setArgRange(args[IN_MOD], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_MOD], "full scale");
    args[IN_INDEX] = plugin->regArg("index", thPlugin::ARG_IN);
    /* The brightness knob, and the one a patch puts an envelope on:
       every DX sound that starts bright and settles is an index
       decaying over a carrier that does not. */
    plugin->setArgDesc(args[IN_INDEX],
                       "How far `mod' at full scale pushes the phase");
    plugin->setArgRange(args[IN_INDEX], 0, FMOP_INDEX_MAX);
    plugin->setArgUnits(args[IN_INDEX], "radians");
    args[IN_FEEDBACK] = plugin->regArg("feedback", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FEEDBACK],
                       "The operator's own output back into its phase: "
                       "0 is a sine, 1 is nearly a sawtooth");
    plugin->setArgRange(args[IN_FEEDBACK], 0, FMOP_FEEDBACK_MAX);
    plugin->setArgUnits(args[IN_FEEDBACK], "radians");
    args[IN_RESET] = plugin->regArg("reset", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RESET],
                       "Start the cycle again while this is above zero");
    plugin->setArgRange(args[IN_RESET], 0, 1);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The operator");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    /* [0] the phase in turns, [1] and [2] the last two outputs. */
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    float *state;
    thArg *in_freq, *in_ratio, *in_mod, *in_index, *in_feedback, *in_reset;
    thArg *out_arg;
    thArg *inout_state;
    unsigned int i;
    float phase, y1, y2;

    in_freq = mod->getArg(node, args[IN_FREQ]);
    in_ratio = mod->getArg(node, args[IN_RATIO]);
    in_mod = mod->getArg(node, args[IN_MOD]);
    in_index = mod->getArg(node, args[IN_INDEX]);
    in_feedback = mod->getArg(node, args[IN_FEEDBACK]);
    in_reset = mod->getArg(node, args[IN_RESET]);

    inout_state = mod->getArg(node, args[INOUT_STATE]);

    /* In float, and stepped in float, so that a window boundary is not
       an event: a running sum kept wider than it is stored comes out
       different at one sample a window than at five hundred. */
    phase = (*inout_state)[0];
    y1 = (*inout_state)[1];
    y2 = (*inout_state)[2];
    state = inout_state->allocate(3);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);

    for (i = 0; i < windowlen; i++)
    {
        const float ratio = thIsFinite((*in_ratio)[i]) ? (*in_ratio)[i] : 1;
        /* The floor and the NaN, which is all of thClampArg that is
           wanted here: a negative index is the same timbre with the
           modulator inverted and is best read as none at all, and a
           non-finite one has to land somewhere. The ceiling is the
           slider's -- see FMOP_INDEX_MAX. */
        const float index = thIsFinite((*in_index)[i])
                          ? ((*in_index)[i] > 0 ? (*in_index)[i] : 0)
                          : 0;
        const float fb = thClampArg((*in_feedback)[i], 0,
                                    FMOP_FEEDBACK_MAX);
        const float in = thIsFinite((*in_mod)[i]) ? (*in_mod)[i] : 0;
        double angle;
        float y;

        /* `> 0' rather than `== 1', which is env::adsr's reading of the
           same word and is false for a NaN either way. */
        if ((*in_reset)[i] > 0)
            phase = 0;

        /* The bound is the tree's: below it a wavelength does not fit in
           a float's mantissa, above it the operator is past Nyquist. A
           modulator at a high ratio reaches the ceiling and stays a
           tone there rather than aliasing down the keyboard. */
        angle = 2.0 * M_PI * (double)phase +
                (double)index * (double)in / TH_MAX +
                (double)fb * ((double)y1 + (double)y2) * 0.5;

        y = (float)(TH_MAX * sin(angle));

        out[i] = y;

        y2 = y1;
        y1 = y;

        phase += (float)(thBoundFreq((double)(*in_freq)[i] * ratio,
                                     samples) / (double)samples);

        /* One end only: thBoundFreq's floor is positive, so the
           increment is too, and the phase can leave the unit interval
           in one direction. floorf rather than a subtraction anyway,
           because a frequency near Nyquist steps most of a turn and
           the two cost the same. */
        if (phase >= 1.0f)
            phase -= floorf(phase);
    }

    state[0] = phase;
    state[1] = y1;
    state[2] = y2;

    return 0;
}
