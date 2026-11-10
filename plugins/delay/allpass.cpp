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

/* Schroeder's allpass: a delay line fed back and fed forward at once.
 *
 *     v[n] = x[n] + gain * v[n - delay]
 *     y[n] = v[n - delay] - gain * v[n]
 *
 * Every frequency comes out at the amplitude it went in at; what changes
 * is when. One impulse becomes a run of echoes `delay' apart, decaying
 * by `gain' each time, and the energy in that run is exactly the energy
 * of the impulse -- which is the property the whole thing is for.
 *
 * WHY A REVERB WANTS ONE. A bank of combs gives a tail of the right
 * length and the wrong texture: four delay lines make four echoes a
 * round, so the first tenth of a second is a handful of distinct slaps
 * anybody can count, and a room does not do that. An allpass takes each
 * slap and spreads it into a run of its own a few milliseconds apart,
 * and does it without touching the level of a single frequency -- so the
 * tail keeps the shape and the decay the combs gave it and stops being
 * countable. Two in series after the bank is Schroeder's arrangement and
 * is what fx/hall.dsp has at the end of it: on that graph's own impulse
 * response the first quarter of a second went from twenty-two echoes to
 * three hundred, and the peak halved, because the same energy stopped
 * arriving all at once.
 *
 * What it cannot do is move a resonance. The magnitude response is flat,
 * which is the whole definition, so every peak the comb bank puts in the
 * spectrum is still exactly where it was. Diffusion is a time-domain fix
 * for a time-domain problem, and a reverb that rings at a pitch needs its
 * comb lengths changed instead.
 *
 * NOT filt::allpass, which is a one-pole phase shifter: a coefficient
 * worked out from a frequency, no delay line, and a phase that passes
 * ninety degrees somewhere. Both are allpass in the sense that matters
 * to a textbook -- flat magnitude -- and they are used for opposite
 * things. This one is in delay:: because it is a delay line, and it
 * counts in samples for the same reason every other member does.
 *
 * `gain' at 1 is a line that never decays and `gain' above it is one
 * that grows, so the arithmetic is bounded just inside either. At 0 the
 * feedback and the feedforward both vanish and what is left is a plain
 * delay of `delay' samples, which is a useful thing to be able to ask
 * for and is what the tests use as their control.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"
#include "allpass.h"

enum {IN_ARG, IN_DELAY, IN_GAIN, OUT_ARG, INOUT_BUFFER, INOUT_BUFPOS};
std::atomic<int> args[INOUT_BUFPOS + 1];

static const char desc[] = "Allpass delay (Schroeder)";
thPlugin::State    mystate = thPlugin::ACTIVE;

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
    args[IN_DELAY] = plugin->regArg("delay", thPlugin::ARG_IN);
    /* It sizes the line rather than reading a position in a longer one,
       so changing it mid-note starts the line empty -- the same trade
       `delay::echo' makes with `size', and for the same reason: an
       allpass whose delay is a knob is a knob that clicks. */
    plugin->setArgStep(args[IN_DELAY], 1);
    plugin->setArgDesc(args[IN_DELAY],
                       "How far apart the echoes are; it is also how long "
                       "the line is");
    plugin->setArgUnits(args[IN_DELAY], "samples");
    args[IN_GAIN] = plugin->regArg("gain", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_GAIN],
                       "How much of each echo is fed back and forward; 0 "
                       "is a plain delay");
    plugin->setArgRange(args[IN_GAIN], -ALLPASS_GAIN_MAX, ALLPASS_GAIN_MAX);
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    /* No range, for filt::allpass's reason: the magnitude response is
       flat in the steady state and a transient overshoots what came in,
       so -1 to 1 would be a promise the arithmetic does not make. */
    plugin->setArgDesc(args[OUT_ARG],
                       "The input, spread into echoes of the same total "
                       "energy");
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);
    args[INOUT_BUFPOS] = plugin->regArg("bufpos", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg = mod->getArg(node, args[IN_ARG]);
    thArg *in_delay = mod->getArg(node, args[IN_DELAY]);
    thArg *in_gain = mod->getArg(node, args[IN_GAIN]);
    thArg *inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
    thArg *inout_bufpos = mod->getArg(node, args[INOUT_BUFPOS]);
    float *bufpos = inout_bufpos->allocate(1);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    float *buffer = inout_buffer->values();
    thkAllpass k;

    k.pos = *bufpos;
    for (unsigned int i = 0; i < windowlen; i++)
    {
        const unsigned int len = thkAllpassLen((*in_delay)[i], (float)samples);

        /* A changed length starts a fresh, zeroed line. */
        if (len != 0 && len != inout_buffer->len())
        {
            buffer = inout_buffer->resize(len, samples);
            memset(buffer, 0, len * sizeof(float));
        }
        out[i] = k.tick(buffer, len, (*in_arg)[i], (*in_gain)[i]);
    }
    *bufpos = k.pos;

    return 0;
}
