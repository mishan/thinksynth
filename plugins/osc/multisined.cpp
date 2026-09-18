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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "think.h"

#define SQR(x) ((x)*(x))

/* Not parabolas: a sine plus `waves - 1' ramps, spread either side of freq.
   osc::multiwave shipped with this same description and is the one that
   stacks a series. */
static const char desc[] = "Sums a sine and detuned ramps";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
enum { IN_WAVES,OUT_ARG,OUT_SYNC,INOUT_LAST,INOUT_FREQ,IN_FREQ,IN_AMP,IN_DETUNEFREQ,IN_DETUNEAMT,IN_AMPMUL,IN_AMPADD };

int args[IN_AMPADD + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_WAVES] = plugin->regArg("waves", thPlugin::ARG_IN);
    /* `(int)(*in_waves)[0]' -- a count of oscillators, read once per window
       from sample 0 and used to size the state, so modulating it is not a
       thing this does. */
    plugin->setArgStep(args[IN_WAVES], 1);
    plugin->setArgDesc(args[IN_WAVES], "How many partials to sum");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    /* The first partial is a sine; the rest are ramps. */
    plugin->setArgDesc(args[OUT_ARG], "The sum of the partials");
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_SYNC] = plugin->regArg("sync", thPlugin::ARG_OUT);
    /* Sized every window and never written -- see the callback. Declared so
       that a .dsp wiring it gets a window of zeroes rather than one value. */
    plugin->setArgDesc(args[OUT_SYNC], "Allocated but never written");
    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);
    args[INOUT_FREQ] = plugin->regArg("freqbuffer", thPlugin::ARG_STATE);
    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FREQ], "Frequency of the first partial");
    plugin->setArgUnits(args[IN_FREQ], "Hz");
    args[IN_AMP] = plugin->regArg("amp", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_AMP], "Peak amplitude of the first partial");
    plugin->setArgRange(args[IN_AMP], 0, TH_MAX);
    plugin->setArgUnits(args[IN_AMP], "full scale");
    /* The plugin already had this default, written where nothing
       could read it: `if (amp_max == 0) amp_max = TH_MAX;' */
    plugin->setArgDefault(args[IN_AMP], TH_MAX);

    /* Partial j runs at `freq * (1 + sin(j*detunefreq/2pi) * detuneamt)' and
       at `amp * (ampmul^j + ampadd*j)'. Where osc::multiwave stacks a series,
       this spreads the partials either side of freq by a sine of their index:
       detuneamt is the spread, detunefreq how fast it alternates as j rises. */
    args[IN_DETUNEFREQ] = plugin->regArg("detunefreq", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DETUNEFREQ],
                       "How fast the detune alternates across the partials");
    args[IN_DETUNEAMT] = plugin->regArg("detuneamt", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DETUNEAMT],
                       "How far a partial may sit from freq");
    plugin->setArgUnits(args[IN_DETUNEAMT], "ratio");
    args[IN_AMPMUL] = plugin->regArg("ampmul", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_AMPMUL],
                       "Partial j is this to the power of j times amp");
    plugin->setArgUnits(args[IN_AMPMUL], "ratio");
    args[IN_AMPADD] = plugin->regArg("ampadd", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_AMPADD], "...plus this times j");
    plugin->setArgUnits(args[IN_AMPADD], "ratio");
    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    int i, j;
    float *out;
    float *out_last;
    float wfreq;
    double wavelength, freq;
    float amp_max;
    float detunefreq, detuneamt, ampmul, ampadd;
    int waves;
    thArg *in_freq, *in_amp, *in_waves, *in_detunefreq, *in_detuneamt, *in_ampmul, *in_ampadd;
    thArg *out_arg, *out_sync;
    thArg *inout_last, *inout_freq;
    
    in_waves = mod->getArg(node, args[IN_WAVES]);
    waves = (int)(*in_waves)[0];
    
    out_arg = mod->getArg(node, args[OUT_ARG]);
    out_sync = mod->getArg(node, args[OUT_SYNC]);
    /* Output a 1 when the wave begins its cycle */
    inout_last = mod->getArg(node, args[INOUT_LAST]);
    out_last = inout_last->allocate(waves);

    /* Both of these are called for the allocate, not for the pointer.
       `freqbuffer' is sized and never filled, and `sync' is sized and
       never written -- but a .dsp may still wire `sync' up, and an arg
       left at length 1 is not the same thing as a zeroed window. */
    inout_freq = mod->getArg(node, args[INOUT_FREQ]);
    inout_freq->allocate(waves);

    out_sync->allocate(windowlen);

    out = out_arg->allocate(windowlen);

    in_freq = mod->getArg(node, args[IN_FREQ]);
    in_amp = mod->getArg(node, args[IN_AMP]);
    in_detunefreq = mod->getArg(node, args[IN_DETUNEFREQ]);
    in_detuneamt = mod->getArg(node, args[IN_DETUNEAMT]);
    in_ampmul = mod->getArg(node, args[IN_AMPMUL]);
    in_ampadd = mod->getArg(node, args[IN_AMPADD]);

    for(i = 0; i < (int)windowlen; i++) {
        //wavelength = samples/(*in_freq)[i];
        freq = (*in_freq)[i];
        amp_max = (*in_amp)[i];
        if(amp_max == 0) {
          amp_max = TH_MAX;
        }

        detunefreq = (*in_detunefreq)[i];
        detuneamt = (*in_detuneamt)[i];
        ampmul = (*in_ampmul)[i];
        ampadd = (*in_ampadd)[i];

        wavelength = samples / thBoundFreq(freq, samples);
        out[i] = sin(2 * M_PI * out_last[0]/wavelength) * amp_max;
        if(++(out_last[0]) > wavelength)
            out_last[0] = 0;

        for(j = 1; j < waves; j++)
        {
            wfreq = freq + sin(j * (detunefreq / (2 * M_PI))) * detuneamt * freq;
            wavelength = samples / thBoundFreq(wfreq, samples);
            out[i] += ((out_last[j] / wavelength) - .5) * 2 * (amp_max * (pow(ampmul, j) + (ampadd * j)));
//            out[i] += sin(2 * M_PI * out_last[j] / wavelength) * (amp_max * (pow(ampmul, j) + (ampadd * j)));
            if(++(out_last[j]) > wavelength)
                out_last[j] = 0;
        }
    }
/*    node->SetArg("out", out, windowlen);
    last[0] = position;
    node->SetArg("last", (float*)last, 1);
*/    
    return 0;
}
