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

/* Echo: a ring and a tap.
 *
 * WHAT GOES ROUND CAN BE CHANGED ON EACH LAP. A graph can filter what an
 * echo feeds back only by looping round it, whose nodes then run a sample
 * at a time, several times the cost -- so the loop's own processing is
 * here: `tone' a low-pass and `low' a high-pass on every
 * repeat, `drive' a saturation, and `boost' the loop's gain past 1. That
 * is a dub echo: each repeat darker and thinner than the one before it,
 * and with the gain past 1 a tail that builds on itself until the
 * saturation holds it, rather than dying away. All four at 0 are a plain
 * ring, as it always was.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

static const char desc[] = "Echo (echo echo echo)";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

enum { IN_ARG,IN_SIZE,IN_DELAY,IN_FEEDBACK,IN_DRY,INOUT_BUFFER,INOUT_BUFPOS,OUT_ARG,
       IN_TONE,IN_LOW,IN_DRIVE,IN_BOOST,INOUT_LOOP };

std::atomic<int> args[INOUT_LOOP + 1];

/* How far `boost' may take the loop: 1.5 times what it would be, which
   `drive' has to be up to hold. */
#define ECHO_BOOST_MAX 0.5f

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_SIZE] = plugin->regArg("size", thPlugin::ARG_IN);
    /* `(int)(*in_size)[i]' -- a delay line is a whole number of samples.
       Read every sample and used to size the ring, so changing it mid-note
       reallocates and loses what was in it. A size of 0 passes the input
       straight through.

       Sixty seconds is allowed and used: fx/tapeloop.dsp's ring is that long,
       2.6 million floats at 44.1 kHz, allocated on the first window. No cap
       is enforced here; the browser builds grow their heap to fit. */
    plugin->setArgStep(args[IN_SIZE], 1);
    plugin->setArgDesc(args[IN_SIZE],
                       "How long the ring is, up to sixty seconds; it has to be "
                       "at least `delay'");
    plugin->setArgUnits(args[IN_SIZE], "samples");
    args[IN_DELAY] = plugin->regArg("delay", thPlugin::ARG_IN);
    /* Taken modulo the ring, and negative values walk forward rather than
       back, so anything outside 0..size is still a defined read -- just not
       the one it reads as. */
    plugin->setArgDesc(args[IN_DELAY],
                       "How far back the tap reads, wrapped into the ring");
    plugin->setArgUnits(args[IN_DELAY], "samples");
    args[IN_FEEDBACK] = plugin->regArg("feedback", thPlugin::ARG_IN);
    /* `buffer[p] = feedback*delayed + (1 - feedback)*in', so it is a
       crossfade into the ring rather than a gain on it: at 1 nothing new
       goes in and the ring repeats for ever. */
    plugin->setArgDesc(args[IN_FEEDBACK],
                       "How much of the ring is kept; 1 stops taking input");
    plugin->setArgRange(args[IN_FEEDBACK], 0, 1);
    args[IN_DRY] = plugin->regArg("dry", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DRY],
                       "0 is all echo, 1 is all input");
    plugin->setArgRange(args[IN_DRY], 0, 1);
    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);
    args[INOUT_BUFPOS] = plugin->regArg("bufpos", thPlugin::ARG_STATE);
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The tap and the input, mixed by dry");
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    args[IN_TONE] = plugin->regArg("tone", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_TONE],
                       "A low-pass on every repeat, so each is darker than "
                       "the last; 0 is none");
    plugin->setArgUnits(args[IN_TONE], "Hz");
    args[IN_LOW] = plugin->regArg("low", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_LOW],
                       "A high-pass on every repeat, so each is thinner than "
                       "the last; 0 is none");
    plugin->setArgUnits(args[IN_LOW], "Hz");
    args[IN_DRIVE] = plugin->regArg("drive", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DRIVE],
                       "Saturation on every repeat; 0 is clean");
    plugin->setArgRange(args[IN_DRIVE], 0, 4);
    args[IN_BOOST] = plugin->regArg("boost", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_BOOST],
                       "The loop's gain past what `feedback' keeps: above 0 "
                       "a tail builds until the saturation holds it, at full "
                       "scale or under");
    plugin->setArgRange(args[IN_BOOST], 0, ECHO_BOOST_MAX);
    /* [0] the low-pass, [1] the high-pass's input, [2] its output. */
    args[INOUT_LOOP] = plugin->regArg("loop", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    float *buffer, *bufpos;
    thArg *in_arg, *in_size, *in_delay, *in_feedback, *in_dry;
    thArg *out_arg;
    thArg *inout_buffer, *inout_bufpos;
    unsigned int i;
    int index;
    float delay, feedback, dry, in;

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_size = mod->getArg(node, args[IN_SIZE]);
    in_delay = mod->getArg(node, args[IN_DELAY]);
    in_feedback = mod->getArg(node, args[IN_FEEDBACK]);
    /* How much of the origional signal is passed */
    in_dry = mod->getArg(node, args[IN_DRY]);

    inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
    inout_bufpos = mod->getArg(node, args[INOUT_BUFPOS]);
    bufpos = inout_bufpos->allocate(1);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);

    thArg *in_tone = mod->getArg(node, args[IN_TONE]);
    thArg *in_low = mod->getArg(node, args[IN_LOW]);
    thArg *in_drive = mod->getArg(node, args[IN_DRIVE]);
    thArg *in_boost = mod->getArg(node, args[IN_BOOST]);
    thArg *inout_loop = mod->getArg(node, args[INOUT_LOOP]);
    float lp = (*inout_loop)[0], hpIn = (*inout_loop)[1];
    float hpOut = (*inout_loop)[2];
    float *loop = inout_loop->allocate(3);

    for(i = 0; i < windowlen; i++) {
        unsigned int mySize = (int)(*in_size)[i];
        unsigned int myBufpos = (int)*bufpos;

        buffer = inout_buffer->allocate(mySize);
        in = (*in_arg)[i];
        feedback = (*in_feedback)[i];
        dry = (*in_dry)[i];

        unsigned int inOutLen = inout_buffer->len();

        if(inOutLen == 0) {
            out[i] = in;
            continue;
        }

        /* was `>' -- valid indices run 0..len-1, so a bufpos equal to len fell
           through and indexed one past the end. */
        if(myBufpos >= inOutLen) {
            myBufpos = 0;
        }
        index = (int)(myBufpos - (*in_delay)[i]);

        /* A negative delay walks index forward instead of back, so clamp both
           ends rather than only wrapping negatives. */
        while(index < 0) {
            index += inOutLen;
        }
        index %= (int)inOutLen;

        delay = buffer[index];

        /* What goes back in, changed on the way. Each stage only where
           its arg asks for it, so a ring with none of them is the ring it
           always was, to the bit. */
        float back = delay;
        const float tone = (*in_tone)[i];
        const float low = (*in_low)[i];
        const float drive = thClampArg((*in_drive)[i], 0, 4);
        const float boost = thClampArg((*in_boost)[i], 0, ECHO_BOOST_MAX);

        if (tone > 0 && thIsFinite(tone))
        {
            const float k = 1 - expf(-2 * (float)M_PI *
                                     fminf(tone, samples * 0.45f) / samples);

            lp += k * (back - lp);
            back = lp;
        }

        if (low > 0 && thIsFinite(low))
        {
            const float r = expf(-2 * (float)M_PI *
                                 fminf(low, samples * 0.45f) / samples);

            hpOut = r * (hpOut + back - hpIn);
            hpIn = back;
            back = hpOut;
        }

        if (boost > 0)
            back *= 1 + boost;

        /* tanh scaled so a small signal passes at unity: the drive is how
           early the curve bends, not a gain. Its ceiling is full scale
           over the drive, so a boosted loop bends at 1 at least and is
           held at full scale or under. */
        const float bend = boost > 0 ? fmaxf(drive, 1) : drive;

        if (bend > 0)
            back = TH_MAX * tanhf(bend * back / TH_MAX) / bend;

        if (!thIsFinite(back))
        {
            back = 0;
            lp = hpIn = hpOut = 0;
        }

        buffer[myBufpos] = (feedback * back) + ((1-feedback) * in);

        out[i] = ((1 - dry) * delay) + (dry * in);
        ++myBufpos;
        *bufpos = (float)myBufpos;
    }

    loop[0] = lp;
    loop[1] = hpIn;
    loop[2] = hpOut;

    return 0;
}
