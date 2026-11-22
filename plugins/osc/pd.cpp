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

/* Phase distortion: a cosine read through a bent phase.
 *
 * The phase g runs fast over part of the cycle and slowly over the rest,
 * and the output is cos(2 pi g). Unbent, g is the cycle's own phase and
 * the wave a cosine; `amount' bends it toward the named wave, and the
 * spectrum opens as a filter's would, with no filter. The waves are after
 * the ones Casio's CZ synths named; the maps are drawn here.
 *
 * EVERY BENT MAP IS A WHOLE NUMBER OF TURNS A CYCLE, so the wave is
 * continuous at the wrap, and its knees but the double's sit at a crest
 * or a trough, where the cosine is flat: no corner there either. The saw
 * turns half a turn fast and half slowly; the square half a turn, a hold,
 * half a turn, a hold; the pulse a whole turn and a hold; the double the
 * saw's bend on each half, the second mirrored in time so the halves
 * differ and the even harmonics stay; the sawpulse the saw's bend on the
 * first half and the pulse's on the second.
 *
 * A HOLD IS DC: the pulse sits on its crest for all but its dip. Each
 * wave's mean is closed form, taken off, and the result divided by 1 plus
 * it, so a sweep of `amount' neither thumps nor passes full scale.
 *
 * THE RESONANT WAVES are a cosine at r times the pitch under a window that
 * falls to nothing by the end of the cycle -- a saw, a triangle or a
 * trapezoid -- as w (1 - cos), which is 0 at both ends, so the restart is
 * continuous for a ratio that is not whole. `amount' sweeps r from 1 to
 * `reso'; the sweep is the resonance. The level moves with r, by about
 * 5 dB, and that is left: it is what a resonance does.
 *
 * ALIASING. A short stretch of cosine spreads to about twice its own
 * frequency, so the sharpest knee a pitch allows keeps the steep stretch
 * under 0.225 of the rate, and r f under 0.2: a high note's sweep stops
 * short of the extreme rather than folding over. 1 / knee is what
 * `amount' moves, as the harmonics go as 1 / knee: an even sweep.
 *
 * HARD SYNC is osc::blep's: `reset' is a master's `edge', and the step a
 * restart makes gets the same two-sample correction. The step in slope it
 * makes is left. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "pd.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thSynthTree.h"
#include "thSynth.h"

enum { IN_FREQ, IN_WAVEFORM, IN_AMOUNT, IN_RESO, IN_PHASE, IN_RESET,
       OUT_ARG, OUT_SYNC, OUT_EDGE, INOUT_STATE, INOUT_LAST };
std::atomic<int> args[INOUT_LAST + 1];

static const char desc[] = "Phase distortion: a cosine read through a bent "
                           "phase, a sine at amount 0";
thPlugin::State mystate = thPlugin::ACTIVE;

static const char *const pdWaveforms[] = { "saw", "square", "pulse", "double",
                                           "sawpulse", "resosaw", "resotri",
                                           "resotrap" };

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc(desc);
    plugin->setState(mystate);

    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FREQ], "Frequency");
    plugin->setArgUnits(args[IN_FREQ], "Hz");
    args[IN_WAVEFORM] = plugin->regArg("waveform", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_WAVEFORM], "The wave `amount' bends toward");
    plugin->setArgValues(args[IN_WAVEFORM], pdWaveforms,
                         (int)(sizeof(pdWaveforms) / sizeof(pdWaveforms[0])));
    args[IN_AMOUNT] = plugin->regArg("amount", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_AMOUNT],
                       "How far the phase is bent: 0 a sine, 1 the wave; "
                       "the resonant waves' ratio, 1 to `reso'");
    plugin->setArgRange(args[IN_AMOUNT], 0, 1);
    args[IN_RESO] = plugin->regArg("reso", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RESO],
                       "The resonant waves' ratio at amount 1, to the pitch");
    plugin->setArgRange(args[IN_RESO], 1, 16);
    plugin->setArgDefault(args[IN_RESO], 8);
    args[IN_PHASE] = plugin->regArg("phase", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_PHASE],
                       "Where the cycle starts, read on the voice's first "
                       "sample");
    plugin->setArgRange(args[IN_PHASE], 0, 1);
    args[IN_RESET] = plugin->regArg("reset", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RESET],
                       "Hard sync: above 0, the cycle restarts that far "
                       "past this sample. Wire a master's `edge' here");
    plugin->setArgRange(args[IN_RESET], 0, 1);
    plugin->setArgUnits(args[IN_RESET], "samples");

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The wave");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_SYNC] = plugin->regArg("sync", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_SYNC],
                       "1 on the sample the cycle wraps, 0 otherwise");
    plugin->setArgRange(args[OUT_SYNC], 0, 1);
    args[OUT_EDGE] = plugin->regArg("edge", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_EDGE],
                       "On the sample the cycle wraps, how far past it the "
                       "wrap falls, 0 to 1; 0 elsewhere. A slave's `reset'");
    plugin->setArgRange(args[OUT_EDGE], 0, 1);
    plugin->setArgUnits(args[OUT_EDGE], "samples");

    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_freq = mod->getArg(node, args[IN_FREQ]);
    thArg *in_waveform = mod->getArg(node, args[IN_WAVEFORM]);
    thArg *in_amount = mod->getArg(node, args[IN_AMOUNT]);
    thArg *in_reso = mod->getArg(node, args[IN_RESO]);
    thArg *in_phase = mod->getArg(node, args[IN_PHASE]);
    thArg *in_reset = mod->getArg(node, args[IN_RESET]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);
    thkPd osc;

    /* [0] the phase, [1] whether it has started, [2] the second half of a
       restart's correction, owed to the next sample. */
    osc.phase = (*inout_state)[0];
    osc.started = (*inout_state)[1] > 0;
    osc.owed = (*inout_state)[2];

    float *state = inout_state->allocate(3);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    float *sync = mod->getArg(node, args[OUT_SYNC])->allocate(windowlen);
    float *edge = mod->getArg(node, args[OUT_EDGE])->allocate(windowlen);
    /* The control once a window while what makes it holds still. */
    const bool still = in_freq->len() <= 1 && in_waveform->len() <= 1 &&
                       in_amount->len() <= 1 && in_reso->len() <= 1;
    thkPd::Ctl c = thkPd::ctl((*in_freq)[0], (*in_waveform)[0],
                              (*in_amount)[0], (*in_reso)[0], (float)samples);

    for (unsigned int i = 0; i < windowlen; i++)
    {
        if (!still)
            c = thkPd::ctl((*in_freq)[i], (*in_waveform)[i], (*in_amount)[i],
                           (*in_reso)[i], (float)samples);
        out[i] = TH_MAX * osc.tick(c, (*in_phase)[0], (*in_reset)[i], sync[i],
                                   edge[i]);
    }

    state[0] = osc.phase;
    state[1] = 1;
    state[2] = osc.owed;

    return 0;
}
