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

/* A pitch shifter: two read heads on a short line, moving at `ratio'.
 *
 * Reading a recording faster than it was written is a pitch shift, and
 * delay::chorus already does it a few cents at a time by swinging a tap.
 * Doing it by a fixed ratio means a reader that keeps moving in one
 * direction, and a reader on a line that keeps moving away from the
 * write head runs off the end of it. So there are two, half a window
 * apart, each wrapping back across the window when it reaches an edge,
 * and each faded out as it gets there:
 *
 *     delay_a = phase * window,        gain_a = (1 - cos(2 pi phase)) / 2
 *     delay_b = (phase + 1/2) * window, gain_b = 1 - gain_a
 *
 * with the phase moving (1 - ratio) / window a sample. A head is silent
 * at the instant it wraps and the other is at full level, so the jump is
 * never heard; what is heard is the crossfade, as a slight warble on a
 * sustained tone at the rate the heads wrap -- |1 - ratio| / window, which
 * is twenty times a second for an octave up through a fifty-millisecond
 * window. That warble is the sound everybody means by the effect, on
 * every pad shimmer since the eighties.
 *
 * `window' trades one fault for another. A long window wraps rarely, so
 * the warble is slow, but each head's copy is further out of step with
 * the other and a transient comes out twice; a short one keeps an attack
 * whole and warbles fast enough to be a buzz. Twenty to two hundred
 * milliseconds is the useful span.
 *
 * `ratio = 1' holds the heads still: one at half the window with a gain
 * of exactly one and the other at the edge with a gain of exactly zero,
 * so the output is the input half a window late, to the bit.
 *
 * READ BY CUBIC INTERPOLATION, four samples a head. Linear would do for
 * a chorus, which barely moves its tap; here the fraction changes every
 * sample, and a straight line drawn between samples is a low-pass whose
 * depth follows the fraction -- so the treble flutters at the rate the
 * fraction cycles. The Hermite cubic is flat enough across the band not
 * to be the thing anyone hears.
 *
 * The line is allocated once, for the longest window the node allows, so
 * moving `window' mid-note moves the heads over what is already there
 * rather than handing them silence.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"
#include "shifter.h"

enum {IN_ARG, IN_RATIO, IN_WINDOW, IN_MIX, OUT_ARG, INOUT_BUFFER,
      INOUT_STATE};
int args[INOUT_STATE + 1];

static const char desc[] = "Pitch shifter (two crossfaded read heads)";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* Three octaves up is a head crossing the window seven times faster than
   the line fills, and past that the heads wrap so often that what comes
   out is the crossfade and not the input. Zero is a head that stands
   still while the line moves under it: the pitch of a tape stopped, which
   is no pitch, and still defined. */
#define PITCHSHIFT_RATIO_MAX 8.0f

/* In seconds, as fractions of the rate: the line is allocated for the
   longest, and a window shorter than a couple of milliseconds is a head
   wrapping at an audible frequency, which is ring modulation. */
#define PITCHSHIFT_WINDOW_MIN 0.002f
#define PITCHSHIFT_WINDOW_MAX 0.25f

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
    args[IN_RATIO] = plugin->regArg("ratio", thPlugin::ARG_IN);
    /* A ratio and not semitones, so a fifth is 1.5 and not 7 with a
       conversion somewhere: a graph that wants semitones has exp2(). */
    plugin->setArgDesc(args[IN_RATIO],
                       "How much faster the heads read than the line is "
                       "written: 2 is an octave up, 0.5 one down");
    plugin->setArgRange(args[IN_RATIO], 0.25f, 4);
    args[IN_WINDOW] = plugin->regArg("window", thPlugin::ARG_IN);
    /* Samples, so a .dsp writes `50 ms'. */
    plugin->setArgDesc(args[IN_WINDOW],
                       "How far the heads travel before they wrap; the "
                       "output is half of it late");
    plugin->setArgUnits(args[IN_WINDOW], "samples");
    plugin->setArgRange(args[IN_WINDOW], 882, 8820);
    args[IN_MIX] = plugin->regArg("mix", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_MIX],
                       "0 is the dry signal, 1 is the shifted one alone");
    plugin->setArgRange(args[IN_MIX], 0, 1);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The signal, shifted");
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);
    /* [0] the write head, [1] the heads' phase, in windows. */
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    float *buffer, *state;
    thArg *in_arg, *in_ratio, *in_window, *in_mix;
    thArg *out_arg;
    thArg *inout_buffer, *inout_state;
    unsigned int i;
    unsigned int at;
    float phase;

    const float longest = PITCHSHIFT_WINDOW_MAX * samples;
    const float shortest = PITCHSHIFT_WINDOW_MIN * samples;
    /* The longest window, and the samples past it the cubic reads. */
    const unsigned int len = (unsigned int)longest + 4;

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_ratio = mod->getArg(node, args[IN_RATIO]);
    in_window = mod->getArg(node, args[IN_WINDOW]);
    in_mix = mod->getArg(node, args[IN_MIX]);

    inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
    inout_state = mod->getArg(node, args[INOUT_STATE]);

    buffer = inout_buffer->allocate(len);
    state = inout_state->allocate(2);

    at = (unsigned int)state[0];
    phase = state[1];

    if (at >= len)
        at = 0;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);

    for (i = 0; i < windowlen; i++)
    {
        const float in = (*in_arg)[i];
        const float ratio = thClampArg((*in_ratio)[i], 0,
                                       PITCHSHIFT_RATIO_MAX);
        const float window = thClampArg((*in_window)[i], shortest, longest);
        const float mix = thClampArg((*in_mix)[i], 0, 1);
        float wet;

        /* Written first, so that a head at no delay at all reads what
           has just come in rather than what came in a line ago. */
        buffer[at] = thIsFinite(in) ? in : 0;

        wet = shifterHeads(buffer, len, at, phase, window);

        /* Each end exact: at 1 the input's share is a zero added, where
           `in + (wet - in) * mix' would round the wet by a bit. */
        out[i] = wet * mix + in * (1 - mix);

        at = (at + 1) % len;
        phase = shifterStep(phase, ratio, window);
    }

    state[0] = (float)at;
    state[1] = phase;

    return 0;
}
