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
 * THE TRIANGLE IS THE PULSE INTEGRATED, with a slow leak, so it inherits the
 * pulse's band-limiting: a triangle's corners are where a square's
 * steps were. The pulse's mean, 2 pw - 1, comes off before the integral,
 * or a width off one half would integrate to a ramp the leak holds as a
 * DC offset; and the result is divided by pw (1 - pw), the integral's
 * height, so it spans full scale at every width and pitch -- a triangle
 * at one half, leaning further into a saw as the width moves off it.
 *
 * THE STATE STEPS IN FLOAT, as osc::fmop's does, because it is kept in a
 * float between windows: a running value held wider inside a window than
 * across one comes out differently at one sample a window than at five
 * hundred.
 *
 * `phase' is where a voice's cycle starts, 0 to 1, read on its first
 * sample. Unison oscillators started at different phases do not begin
 * with all their edges together, which is the click a stack of saws
 * makes on every note when they do.
 *
 * HARD SYNC restarts the cycle whenever `reset' is above 0, and `reset'
 * is another blep's `edge': how far past the current sample the master
 * wraps. That fraction is what places the restart between two samples,
 * so the step it makes gets the same two-sample correction as a wrap; a
 * 0/1 trigger reads as a wrap at the next sample and syncs unsmoothed.
 * The triangle restarts with a step its integrator does not smooth.
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

enum {IN_FREQ, IN_WAVEFORM, IN_PW, IN_PHASE, IN_RESET, OUT_ARG, OUT_SYNC,
      OUT_EDGE, INOUT_STATE};

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

/* The two halves of a step's correction, `d' the fraction of a sample
   from the step to the sample after it, `h' the step's height: the
   sample before gets the first, the sample after the second. */
static float stepBefore (float h, float d)
{
    return h / 2 * d * d;
}

static float stepAfter (float h, float d)
{
    return h / 2 * (2 * d - d * d - 1);
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
    args[IN_RESET] = plugin->regArg("reset", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RESET],
                       "Hard sync: above 0, the cycle restarts that far "
                       "past this sample. Wire another blep's `edge' here");
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

    /* [0] the phase, [1] the triangle's integrator, [2] started, [3] the
       second half of a reset sample's corrections, owed to the next, and
       [4] whether it is. */
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
    thArg *in_reset = mod->getArg(node, args[IN_RESET]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);

    float phase = (*inout_state)[0];
    float tri = (*inout_state)[1];
    const bool started = (*inout_state)[2] > 0;
    float owed = (*inout_state)[3];
    bool owing = (*inout_state)[4] > 0;
    float *state = inout_state->allocate(5);

    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    float *sync = mod->getArg(node, args[OUT_SYNC])->allocate(windowlen);
    float *edge = mod->getArg(node, args[OUT_EDGE])->allocate(windowlen);

    if (!started)
    {
        const float p = (*in_phase)[0];
        float pw = (*in_pw)[0];

        phase = thIsFinite(p) ? p - floorf(p) : 0;

        /* The integrator starts where the wave is at that phase, rather
           than at 0 with an offset the leak takes a few cycles to bleed
           away: at the bottom where the pulse goes high, rising
           2 (1 - pw) a cycle until pw, falling 2 pw after. */
        if (!thIsFinite(pw) || pw <= 0 || pw >= 1)
            pw = 0.5f;

        tri = phase < pw ? -pw * (1 - pw) + 2 * (1 - pw) * phase
                         : pw * (1 - pw) - 2 * pw * (phase - pw);
    }

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float dt = (float)(thBoundFreq((double)(*in_freq)[i], samples) /
                                 (double)samples);
        /* A selector, read as osc::simple reads its own: the whole part
           picks, so 1.5 is the pulse. Anything under 0 or from 3 up, and
           NaN, which no cast to int may be handed, is the saw. */
        const float w = (*in_waveform)[i];
        const int wave = (thIsFinite(w) && w >= 0 && w < 3) ? (int)w : 0;
        float pw = (*in_pw)[i];
        float y;

        if (!thIsFinite(pw) || pw <= 0)
            pw = 0.5;
        /* Both edges a sample's increment apart at least, or the two
           corrections overlap and the pulse vanishes. */
        if (pw < dt)
            pw = dt;
        if (pw > 1 - dt)
            pw = 1 - dt;

        const float r = (*in_reset)[i];
        const bool reset = thIsFinite(r) && r > 0;

        if (!reset && !owing)
        {
            if (wave == 0)
            {
                y = 2 * phase - 1 - (float)polyBlep(phase, dt);
            }
            else
            {
                float t2 = phase + 1 - pw;
                t2 -= floorf(t2);
                y = (phase < pw ? 1.0f : -1.0f) +
                    (float)polyBlep(phase, dt) - (float)polyBlep(t2, dt);
            }
        }
        else
        {
            /* After a reset the phase is no longer where a wrap left it,
               so what is owed from the last sample was worked out there;
               before one, a wrap or edge counts only if it comes first. */
            const bool pulse = wave != 0;
            float t2 = phase + 1 - pw;
            float next = 0;

            t2 -= floorf(t2);
            y = pulse ? (phase < pw ? 1.0f : -1.0f) : 2 * phase - 1;

            if (owing)
                y += owed;
            else if (pulse)
                y += (float)(phase < dt ? polyBlep(phase, dt) : 0) -
                     (float)(t2 < dt ? polyBlep(t2, dt) : 0);
            else
                y -= (float)(phase < dt ? polyBlep(phase, dt) : 0);

            if (!reset)
            {
                if (pulse)
                    y += (float)(phase > 1 - dt ? polyBlep(phase, dt) : 0) -
                         (float)(t2 > 1 - dt ? polyBlep(t2, dt) : 0);
                else
                    y -= (float)(phase > 1 - dt ? polyBlep(phase, dt) : 0);
            }
            else
            {
                const float until = r < 1 ? r : 1;
                const float d = 1 - until;
                float at = phase + until * dt;

                if (pulse && phase < pw && at >= pw)
                {
                    const float dw = 1 - (pw - phase) / dt;
                    y += stepBefore(-2, dw);
                    next += stepAfter(-2, dw);
                }
                if (at >= 1)
                {
                    const float dw = 1 - (1 - phase) / dt;
                    const float h = pulse ? 2.0f : -2.0f;
                    y += stepBefore(h, dw);
                    next += stepAfter(h, dw);
                    at -= 1;
                }

                const float h = pulse ? 1 - (at < pw ? 1.0f : -1.0f)
                                      : -2 * at;
                y += stepBefore(h, d);
                next += stepAfter(h, d);
            }

            owed = next;
            owing = reset;
        }

        if (wave == 2)
        {
            /* The leak only has rounding to bleed off now, so it is
               slow: a leak of `dt' bends the slopes visibly. */
            tri = dt * (y - (2 * pw - 1)) + (1 - dt / 64) * tri;
            y = tri / (pw * (1 - pw));
        }

        out[i] = (float)(TH_MAX * y);

        if (reset)
        {
            const float until = r < 1 ? r : 1;

            phase = (1 - until) * dt;
            tri = -pw * (1 - pw) + 2 * (1 - pw) * phase;
            sync[i] = 1;
            edge[i] = until;
        }
        else
        {
            edge[i] = phase + dt >= 1 ? (1 - phase) / dt : 0;
            phase += dt;
            if (phase >= 1)
            {
                phase -= floorf(phase);
                sync[i] = 1;
            }
            else
            {
                sync[i] = 0;
            }
        }
    }

    state[0] = phase;
    state[1] = tri;
    state[2] = 1;
    state[3] = owed;
    state[4] = owing;

    return 0;
}
