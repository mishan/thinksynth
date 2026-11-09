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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "think.h"

#include "simple.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thSynthTree.h"
#include "thSynth.h"


enum {IN_FREQ, IN_AMP, IN_PW, IN_WAVEFORM, IN_FM, IN_FMAMT, IN_RESET, IN_MUL,
    OUT_ARG, OUT_SYNC, INOUT_LAST};

std::atomic<int> args[INOUT_LAST + 1];

static const char desc[] = "Complex Oscillator";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* The six the switch in module_callback() actually implements, in its order.
   Kept beside the plugin rather than in the GUI because this is the only place
   that knows what a 3 means, and the only place that would notice a seventh
   being added. */
static const char *const waveforms[] = {
    "Sine", "Sawtooth", "Square", "Triangle", "Half-circle", "Parabola"
};

void module_cleanup (thPlugin *plugin)
{
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FREQ], "Frequency");
    /* No range: the wavelength is samples/freq, so the top is Nyquist and
       that is not a number this call knows. Every hertz arg is the same. */
    plugin->setArgUnits(args[IN_FREQ], "Hz");
    args[IN_AMP] = plugin->regArg("amp", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_AMP], "Peak amplitude");
    plugin->setArgRange(args[IN_AMP], 0, TH_MAX);
    plugin->setArgUnits(args[IN_AMP], "full scale");
    /* The plugin already had this default, written where nothing
       could read it: `if (amp_max == 0) amp_max = TH_MAX;' */
    plugin->setArgDefault(args[IN_AMP], TH_MAX);
    args[IN_PW] = plugin->regArg("pw", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_PW],
                       "Pulse width: how much of the cycle is the first half");
    plugin->setArgRange(args[IN_PW], 0, 1);
    /* The plugin already had this default, written where nothing
       could read it: `if (pw == 0) pw = 0.5;' */
    plugin->setArgDefault(args[IN_PW], 0.5);
    args[IN_WAVEFORM] = plugin->regArg("waveform", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_WAVEFORM], "Which wave");

    /* Read `switch ((int)buf_waveform[i])', so it is a selector and not a
       parameter: 3.4 is a triangle and so is 3.9. Saying so is what lets a
       control driving it be a list of six names rather than a slider whose
       travel is five sixths decoration. */
    plugin->setArgValues(args[IN_WAVEFORM], waveforms,
                         (int)(sizeof(waveforms) / sizeof(waveforms[0])));

    args[IN_FM] = plugin->regArg("fm", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FM], "FM Input");
    plugin->setArgRange(args[IN_FM], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_FM], "full scale");
    args[IN_FMAMT] = plugin->regArg("fmamt", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FMAMT], "Modulation amount");
    /* `position += 1 + (fm/TH_MAX) * fmamt', so this is how far the phase
       jumps ahead per sample at full modulation. */
    plugin->setArgUnits(args[IN_FMAMT], "samples");
    args[IN_RESET] = plugin->regArg("reset", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RESET], "Reset position to 0 when this goes to 1");
    plugin->setArgRange(args[IN_RESET], 0, 1);
    args[IN_MUL] = plugin->regArg("mul", thPlugin::ARG_IN);
    /* The plugin's own comment on this arg said "Multiply the wavelength by
       this", which is the wrong way round -- `freq *= mul' multiplies the
       frequency, and the line below it calls mul a frequency multiplier.
       dsp/organ0.dsp drives it with drawbar ratios, which only make sense as
       one. Harvesting an author's words is the rule; carrying an inverted one
       into a reference is not. */
    plugin->setArgDesc(args[IN_MUL], "Multiply the frequency by this");
    plugin->setArgUnits(args[IN_MUL], "ratio");
    /* The plugin already had this default, written where nothing
       could read it: `if (mul) freq *= mul;' -- a 0 multiplies by nothing, which is a 1 */
    plugin->setArgDefault(args[IN_MUL], 1);


    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The wave");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_SYNC] = plugin->regArg("sync", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_SYNC], "Output a 1 when the wave begins its cycle");
    plugin->setArgRange(args[OUT_SYNC], 0, 1);

    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_freq = mod->getArg(node, args[IN_FREQ]);
    thArg *in_amp = mod->getArg(node, args[IN_AMP]);
    thArg *in_pw = mod->getArg(node, args[IN_PW]);
    thArg *in_waveform = mod->getArg(node, args[IN_WAVEFORM]);
    thArg *in_fm = mod->getArg(node, args[IN_FM]);
    thArg *in_fmamt = mod->getArg(node, args[IN_FMAMT]);
    thArg *in_reset = mod->getArg(node, args[IN_RESET]);
    thArg *in_mul = mod->getArg(node, args[IN_MUL]);
    thArg *inout_last = mod->getArg(node, args[INOUT_LAST]);
    thkSimple osc;

    osc.position = (*inout_last)[0];

    float *last = inout_last->allocate(1);
    float *sync = mod->getArg(node, args[OUT_SYNC])->allocate(windowlen);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    const auto ctl = [&](unsigned int i) {
        return thkSimple::ctl((*in_freq)[i], (*in_mul)[i], (*in_amp)[i] / TH_MAX,
                              (*in_pw)[i], (*in_waveform)[i], (float)samples);
    };
    /* The cycle once a window while what makes it holds still. */
    const bool still = in_freq->len() <= 1 && in_mul->len() <= 1 &&
                       in_amp->len() <= 1 && in_pw->len() <= 1 &&
                       in_waveform->len() <= 1;
    thkSimple::Ctl c = ctl(0);

    for (unsigned int i = 0; i < windowlen; i++)
    {
        if (!still)
            c = ctl(i);
        out[i] = TH_MAX * osc.tick(c, (*in_fm)[i] / TH_MAX, (*in_fmamt)[i],
                                   (*in_reset)[i], sync[i]);
    }
    last[0] = osc.position;

    return 0;
}
