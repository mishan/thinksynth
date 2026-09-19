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

/* Chorus: a short delay whose tap keeps moving.
 *
 * One line, `taps' readers on it, each swinging `depth' samples either
 * way around `delay' as its LFO takes it, all at `rate' and spread
 * across half a cycle. A reader that is moving reads at the
 * wrong speed, and reading a recording at the wrong speed is a pitch
 * shift, so each tap is the input a few cents sharp or flat and a few
 * milliseconds late. Two or three of those against the dry signal is
 * more than one player playing: not a copy but a section, which is what
 * an ensemble is and what the name means.
 *
 * HALF A CYCLE, NOT A WHOLE ONE, and that is the one arithmetic detail
 * here worth arguing about. Taps spread evenly around the whole cycle
 * are a set of vectors at equal angles, and a set of vectors at equal
 * angles sums to nothing: two taps half a cycle apart are exact mirrors
 * -- one as sharp as the other is flat at every instant -- and three at
 * a third of a cycle are no better. Summed to one output their pitch
 * shifts cancel *exactly*, leaving a tremolo at a multiple of the rate,
 * which is a real effect and is not this one. (A stereo unit spreads
 * its taps around the whole cycle and gets away with it by sending the
 * mirrors to opposite speakers, where they never meet. This node has
 * one output; a graph that wants stereo instantiates two of it, which
 * is what fx/chorus.dsp does.)
 *
 * Spread across half a cycle instead, the same vectors span at most a
 * hundred and eighty degrees and cannot cancel, at any depth and at any
 * frequency. The gate in statecheck is exactly that: a second tap has to
 * leave the sidebands the first one makes standing rather than take them
 * away.
 *
 * IT IS NOT A VIBRATO, and the difference is the mix. `misc::vibrato'
 * bends a frequency before an oscillator reads it, so what comes out is
 * one voice, in tune with itself, wobbling. This shifts an *output* by
 * moving where it is read from, and then puts the unshifted signal back
 * beside it -- so what comes out is two voices that disagree, which is
 * the beating everybody means by chorus. Neither can do the other's job:
 * a vibrato has no second voice and a chorus cannot bend a note it has
 * not heard yet.
 *
 * INTERPOLATED, because the tap lands between samples and the whole
 * effect is in that fraction. Reading the nearest sample instead would
 * quantize the pitch shift into a staircase of exact-sample delays, and
 * the steps between them are the clicks a cheap chorus makes. Linear
 * here: a cubic would cost four reads a tap to fix a treble loss that a
 * quarter-percent-detuned copy of the signal buries anyway.
 *
 * FEEDBACK MAKES IT A FLANGER, and nothing else has to change. The taps'
 * average goes back into the line's write, so one reflection becomes a
 * resonance: the comb's peaks sharpen, and with `delay' under a couple of
 * hundred samples and `depth' most of it the sweep is the jet-engine sound
 * rather than an ensemble. Negative feedback inverts what is written and
 * moves the peaks to where the notches were -- odd harmonics only, which is
 * the hollow half of the sound and the reason the arg is signed.
 *
 * The loop's gain at a peak is 1/(1 - |feedback|), so the ceiling is short
 * of 1 (see CHORUS_FEEDBACK_MAX) and the output is louder than the input by
 * that much wherever the comb peaks. That is what `mix' is for.
 *
 * SIZED FROM ITS ARGS, so `delay' and `depth' together size the line
 * and moving either one mid-note hands the reader a fresh, empty buffer.
 * `delay::echo' makes the same trade with `size' for the same reason:
 * a ring that was reallocated is a ring with nothing in it, and there is
 * no answer to that which is not a second buffer and a crossfade.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

enum {IN_ARG, IN_RATE, IN_DEPTH, IN_DELAY, IN_MIX, IN_TAPS, IN_PHASE,
      IN_FEEDBACK, OUT_ARG, INOUT_BUFFER, INOUT_STATE};
int args[INOUT_STATE + 1];

static const char desc[] = "Chorus (moving taps on a short delay)";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* A chorus is a slow wobble on a short line: the rate is a fraction of a
   hertz to a few, the delay is a handful of milliseconds, and the depth
   is a fraction of the delay. Past those it is a vibrato, a doubler or a
   flanger -- all worth having and none of them this -- so the numbers
   are declared and not enforced. The line's own ceiling is, because
   `delay' and `depth' size an allocation. */
#define CHORUS_RATE_MAX   10.0f
#define CHORUS_TAPS_MAX   3

/* Where the feedback stops.
 *
 * The loop's gain at a comb peak is 1/(1 - |feedback|), so 1 is a comb that
 * does not converge: the ringing at the peaks grows until the window guard
 * catches it. 0.95 is a peak twenty times the input, which is already more
 * resonance than a flanger is usually asked for, and it is a number the
 * arithmetic can hold. */
#define CHORUS_FEEDBACK_MAX 0.95f

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
    args[IN_RATE] = plugin->regArg("rate", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RATE], "How fast the taps move");
    plugin->setArgUnits(args[IN_RATE], "Hz");
    plugin->setArgRange(args[IN_RATE], 0, CHORUS_RATE_MAX);
    args[IN_DEPTH] = plugin->regArg("depth", thPlugin::ARG_IN);
    /* Samples, so a .dsp writes `2 ms'. How far the tap swings either
       way from `delay', which is what sets how far out of tune the
       copies go: the shift is the rate of change of this. */
    plugin->setArgDesc(args[IN_DEPTH],
                       "How far each tap swings either side of `delay'");
    plugin->setArgUnits(args[IN_DEPTH], "samples");
    args[IN_DELAY] = plugin->regArg("delay", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DELAY],
                       "Where the taps sit when their LFO is at zero");
    plugin->setArgUnits(args[IN_DELAY], "samples");
    args[IN_MIX] = plugin->regArg("mix", thPlugin::ARG_IN);
    /* The dry signal is half of the effect and not a courtesy: the
       beating is between the copies and the original. At 1 there is
       nothing to beat against and what is left is a detuned copy. */
    plugin->setArgDesc(args[IN_MIX],
                       "0 is the dry signal, 1 is the moving taps alone");
    plugin->setArgRange(args[IN_MIX], 0, 1);
    args[IN_TAPS] = plugin->regArg("taps", thPlugin::ARG_IN);
    plugin->setArgStep(args[IN_TAPS], 1);
    plugin->setArgDesc(args[IN_TAPS],
                       "How many readers, spread across half the LFO's "
                       "cycle");
    plugin->setArgRange(args[IN_TAPS], 1, CHORUS_TAPS_MAX);
    plugin->setArgDefault(args[IN_TAPS], 2);
    args[IN_PHASE] = plugin->regArg("phase", thPlugin::ARG_IN);
    /* Turns, so half is the far side of the cycle. It is how two of
       these become a stereo pair: one graph, two nodes, one of them
       reading where the other is not. */
    plugin->setArgDesc(args[IN_PHASE],
                       "Where this node's LFO starts, as a fraction of "
                       "its cycle");
    plugin->setArgRange(args[IN_PHASE], 0, 1);
    args[IN_FEEDBACK] = plugin->regArg("feedback", thPlugin::ARG_IN);
    /* The taps' average back into the line's write, which turns the comb
       from one reflection into a resonance -- and turns this node into a
       flanger. Negative inverts what is written, so the comb's peaks land
       where its notches were: the odd-harmonic series, which is the hollow
       half of the sound.

       It raises the level, and that is arithmetic rather than a fault: the
       peaks are 1/(1 - |feedback|) times the input, so 0.9 is ten of them.
       `mix' and the channel's own level are what that is for. */
    plugin->setArgDesc(args[IN_FEEDBACK],
                       "How much of the taps goes back into the line: 0 is a "
                       "chorus, and either end is a flanger");
    plugin->setArgRange(args[IN_FEEDBACK], -CHORUS_FEEDBACK_MAX,
                        CHORUS_FEEDBACK_MAX);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The signal and its moving copies");
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);
    /* [0] the write head, [1] the LFO's phase in turns. */
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    float *buffer, *state;
    thArg *in_arg, *in_rate, *in_depth, *in_delay, *in_mix, *in_taps;
    thArg *in_phase, *in_feedback;
    thArg *out_arg;
    thArg *inout_buffer, *inout_state;
    unsigned int i;
    unsigned int at;
    float phase;

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_rate = mod->getArg(node, args[IN_RATE]);
    in_depth = mod->getArg(node, args[IN_DEPTH]);
    in_delay = mod->getArg(node, args[IN_DELAY]);
    in_mix = mod->getArg(node, args[IN_MIX]);
    in_taps = mod->getArg(node, args[IN_TAPS]);
    in_phase = mod->getArg(node, args[IN_PHASE]);
    in_feedback = mod->getArg(node, args[IN_FEEDBACK]);

    inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
    inout_state = mod->getArg(node, args[INOUT_STATE]);

    at = (unsigned int)(*inout_state)[0];
    /* In float, and stepped in float, so that a window boundary is not an
       event: a running sum kept wider than it is stored comes out
       different at one sample a window than at five hundred. */
    phase = (*inout_state)[1];
    state = inout_state->allocate(2);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);

    for (i = 0; i < windowlen; i++)
    {
        const float in = (*in_arg)[i];
        const float rate = thIsFinite((*in_rate)[i]) ? (*in_rate)[i] : 0;
        const float mix = thClampArg((*in_mix)[i], 0, 1);
        float depth = thClampArg((*in_depth)[i], 0, (float)samples / 4);
        float delay = thClampArg((*in_delay)[i], 0, (float)samples / 4);
        int taps = (int)thClampArg((*in_taps)[i], 1, CHORUS_TAPS_MAX);
        const float offset = thClampArg((*in_phase)[i], 0, 1);
        const float feedback = thClampArg((*in_feedback)[i],
                                          -CHORUS_FEEDBACK_MAX,
                                          CHORUS_FEEDBACK_MAX);
        unsigned int len;
        float wet = 0;
        int t;

        /* The line has to hold the furthest any tap can reach, plus the
           sample the interpolation reads beyond it and the one being
           written. */
        len = (unsigned int)(delay + depth) + 3;
        buffer = inout_buffer->allocate(len);

        if (at >= len)
            at = 0;

        /* A knob at zero is a graph paying for nothing. The line is
           still written and the LFO still turns -- so the knob can come
           up mid-note onto a line with something in it, rather than
           onto a fresh silence -- and only the reading is skipped.
           `feedback' needs the taps whatever `mix' says: what it writes
           into the line is what makes the next window's comb. */
        for (t = 0; t < taps && (mix > 0 || feedback != 0); t++)
        {
            /* Across half the cycle -- see the head for why not all of
               it. One tap is a doubler, two is the sound the name is
               for, three is a section. */
            const double turn = (double)phase + (double)offset +
                                (double)t / (2 * taps);
            const double swing = depth * sin(2.0 * M_PI * turn);
            double back = delay + swing;
            unsigned int whole;
            float frac, a, b;

            /* A tap at the write head reads what is being written and a
               tap past the end of the line reads what has not happened
               yet; both land on the nearest sample that has. */
            if (!(back >= 1.0))
                back = 1.0;

            if (back > (double)(len - 2))
                back = len - 2;

            whole = (unsigned int)back;
            frac = (float)(back - whole);

            a = buffer[(at + len - whole) % len];
            b = buffer[(at + len - whole - 1) % len];

            /* Between the two, which is the whole effect: the fraction
               is where the pitch shift lives. */
            wet += a + (b - a) * frac;
        }

        wet /= taps;

        /* Read first, then written: every tap is at least one sample back,
           so the order does not move a sample at `feedback = 0' -- and at
           anything else this is the loop, the taps' average added to what
           is coming in.

           Guarded, because the line is its own input from here: one sample
           that is not a number would otherwise circulate for as long as the
           note lasts. */
        {
            const float back = in + feedback * wet;

            buffer[at] = thIsFinite(back) ? back : 0;
        }

        out[i] = in + (wet - in) * mix;

        at = (at + 1) % len;
        phase += rate / (float)samples;

        if (phase >= 1.0f || phase < 0.0f)
            phase -= floorf(phase);
    }

    state[0] = (float)at;
    state[1] = phase;

    return 0;
}
