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

/* A saw, a pulse and a triangle that do not alias.
 *
 * `osc::simple' draws its saw as a straight line that drops to the
 * bottom in one sample. That drop is a step, and a step has harmonics
 * all the way up -- past Nyquist, where they fold back down as tones
 * that have nothing to do with the note. On a high note they are louder
 * than the note's own upper partials, and they move the wrong way when
 * the pitch bends: the "digital" in a cheap synth's sound.
 *
 * POLYBLEP rounds the step off. A band-limited step is a sinc's
 * integral; two samples of a polynomial either side of the jump are a
 * close enough fit to it to push the folded harmonics 40 dB or more
 * down, for the price of a couple of multiplies when a cycle wraps.
 * The pulse is two such steps, one at the wrap and one at `pw'.
 *
 * THE TRIANGLE IS THE PULSE INTEGRATED, with a leak, so it inherits the
 * pulse's band-limiting: a triangle's corners are where a square's
 * steps were. The leak keeps a pulse width off one half from walking it
 * away from zero, and the scale keeps it at full scale at every pitch.
 *
 * `phase' is where a voice's cycle starts, 0 to 1, read on its first
 * sample. Unison oscillators started at different phases do not begin
 * with all their edges together, which is the click a stack of saws
 * makes on every note when they do.
 *
 * This is a new node rather than a fix to `osc::simple', because every
 * patch in the tree was voiced against that one's aliasing.
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

enum {IN_FREQ, IN_WAVEFORM, IN_PW, IN_PHASE, OUT_ARG, OUT_SYNC, INOUT_STATE};

std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "Band-limited oscillator (PolyBLEP saw, pulse, triangle)";
thPlugin::State    mystate = thPlugin::ACTIVE;

static const char *const blepWaveforms[] = { "Sawtooth", "Pulse", "Triangle" };

/* The correction for a unit step at phase 0, `t' the phase and `dt' the
   increment: nonzero only within a sample either side of the edge. */
static double polyBlep (double t, double dt)
{
    if (t < dt)
    {
        const double x = t / dt;
        return x + x - x * x - 1;
    }
    if (t > 1 - dt)
    {
        const double x = (t - 1) / dt;
        return x * x + x + x + 1;
    }
    return 0;
}

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FREQ], "Frequency");
    plugin->setArgUnits(args[IN_FREQ], "Hz");
    args[IN_WAVEFORM] = plugin->regArg("waveform", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_WAVEFORM], "Which wave");
    plugin->setArgValues(args[IN_WAVEFORM], blepWaveforms,
                         (int)(sizeof(blepWaveforms) /
                               sizeof(blepWaveforms[0])));
    args[IN_PW] = plugin->regArg("pw", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_PW],
                       "Pulse width: how much of the cycle is high");
    plugin->setArgRange(args[IN_PW], 0, 1);
    plugin->setArgDefault(args[IN_PW], 0.5);
    args[IN_PHASE] = plugin->regArg("phase", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_PHASE],
                       "Where the cycle starts, read on the voice's first "
                       "sample");
    plugin->setArgRange(args[IN_PHASE], 0, 1);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The wave");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_SYNC] = plugin->regArg("sync", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_SYNC],
                       "1 on the sample the cycle wraps, 0 otherwise");
    plugin->setArgRange(args[OUT_SYNC], 0, 1);

    /* [0] the phase, [1] the triangle's integrator, [2] started. */
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_freq = mod->getArg(node, args[IN_FREQ]);
    thArg *in_waveform = mod->getArg(node, args[IN_WAVEFORM]);
    thArg *in_pw = mod->getArg(node, args[IN_PW]);
    thArg *in_phase = mod->getArg(node, args[IN_PHASE]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);

    double phase = (*inout_state)[0];
    double tri = (*inout_state)[1];
    const bool started = (*inout_state)[2] > 0;
    float *state = inout_state->allocate(3);

    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    float *sync = mod->getArg(node, args[OUT_SYNC])->allocate(windowlen);

    if (!started)
    {
        const float p = (*in_phase)[0];
        phase = thIsFinite(p) ? p - floor(p) : 0;
    }

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const double dt = thBoundFreq((double)(*in_freq)[i], samples) /
                          (double)samples;
        const int wave = (int)(*in_waveform)[i];
        double pw = (*in_pw)[i];
        double y;

        if (!thIsFinite(pw) || pw <= 0)
            pw = 0.5;
        /* Both edges a sample's increment apart at least, or the two
           corrections overlap and the pulse vanishes. */
        if (pw < dt)
            pw = dt;
        if (pw > 1 - dt)
            pw = 1 - dt;

        if (wave == 0)
        {
            y = 2 * phase - 1 - polyBlep(phase, dt);
        }
        else
        {
            double t2 = phase + 1 - pw;
            t2 -= floor(t2);
            y = (phase < pw ? 1.0 : -1.0) + polyBlep(phase, dt) -
                polyBlep(t2, dt);
            if (wave == 2)
            {
                tri = dt * y + (1 - dt) * tri;
                y = tri * 4;
            }
        }

        out[i] = (float)(TH_MAX * y);

        phase += dt;
        if (phase >= 1)
        {
            phase -= floor(phase);
            sync[i] = 1;
        }
        else
        {
            sync[i] = 0;
        }
    }

    state[0] = (float)phase;
    state[1] = (float)tri;
    state[2] = 1;

    return 0;
}
