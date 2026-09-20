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

/* A delay line fed back into itself: a tube, and with `damp' up a string.
 *
 *     y[n] = x[n] + feedback * lp(y[n - rate/freq])
 *
 * The loop is `rate/freq' samples long, so what goes round it comes back
 * a period later `feedback' quieter, and the response peaks on the
 * harmonic series of `freq'. Four in parallel at unrelated periods is a
 * reverb's tail, which is what fx/hall.dsp is built from.
 *
 * DAMP, AND WHY A STRING NEEDS IT. With nothing in the loop every
 * partial decays at the same rate, so a noise burst comes out ringing
 * every harmonic for the same length: a pipe. A plucked string loses its
 * high partials first -- the bridge and the air take the fast motion
 * first -- and one lowpass in the feedback path is the whole of that
 * difference. That is the Karplus-Strong string, and with a burst at the
 * front of a note it is the fingered bass, the muted rhythm guitar and
 * the strummed acoustic.
 *
 * `damp' is the pole of a one-pole lowpass on the delayed signal:
 *
 *     lp[n] = (1 - damp) * y[n - D] + damp * lp[n - 1]
 *
 * so 0 is a wire and the node is what it was, sample for sample, and
 * towards 1 the harmonics above the fundamental fall away faster than it
 * does. The lowpass runs before `feedback' rather than after it, so how
 * bright the string is and how long it rings stay two knobs rather than
 * one.
 *
 * THE FRACTIONAL READ. `rate/freq' is not a whole number of samples: at
 * 44.1 kHz a 440 Hz string wants 100.2 of them. Reading the nearest slot
 * makes the loop 100 samples long on one turn and 101 on the next as the
 * position drifts, which is a pitch that wobbles a semitone's tenth
 * around the one asked for and grows as the period shortens -- by 2 kHz
 * a sample is most of a semitone. So the line is read between its two
 * neighbouring samples, weighted by where the read falls, and a scale
 * played on it is in tune.
 *
 * TUNING WITH THE DAMPER IN THE LOOP. A filter in a feedback path is
 * also a delay: the one-pole holds the signal back by its own phase
 * delay, atan2(damp sin w, 1 - damp cos w) / w samples at the frequency
 * the loop rings at, which is a whole sample at `damp = 0.5' and more
 * above it. Left alone that flattens the string, and worse the higher it
 * plays -- at 2 kHz a sample is 78 cents. The damper's delay is
 * therefore taken off the line's own, so the loop is `rate/freq' samples
 * long whatever `damp' says and the pitch is the pitch.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

static const char desc[] = "Comb Filter";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* A pole of 1 is not a lowpass but an integrator, and an integrator in a
   feedback loop is a ramp to the rails. This is just inside it, and at
   this much damping a string is already a thud. */
#define COMB_DAMP_MAX 0.95f

/* `size' sizes an allocation, so a number nobody meant is a gigabyte
   rather than a long line. Ten seconds is a hundred times the longest
   comb a reverb wants; the floor is the shortest line the read can sit
   inside. */
#define COMB_SIZE_MIN 4

enum {IN_ARG, IN_FREQ, IN_FEEDBACK, IN_DAMP, IN_SIZE, OUT_ARG, INOUT_BUFFER,
      INOUT_BUFPOS, INOUT_DAMPED};
int args[INOUT_DAMPED + 1];

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
    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    /* The line is `rate/freq' samples long and is read between samples,
       so this is the comb's fundamental to the cent -- and moving it
       during a note is a bend, which is what a fretless plays. */
    plugin->setArgDesc(args[IN_FREQ], "Delay length spacing");
    plugin->setArgUnits(args[IN_FREQ], "Hz");
    args[IN_FEEDBACK] = plugin->regArg("feedback", thPlugin::ARG_IN);
    /* `buffer[p] = feedback * lp(buffer[p]) + in', so 1 sustains for ever
       and anything above it grows every pass. */
    plugin->setArgDesc(args[IN_FEEDBACK],
                       "How much of each pass is kept; 1 never decays");
    plugin->setArgRange(args[IN_FEEDBACK], 0, 1);
    args[IN_DAMP] = plugin->regArg("damp", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DAMP],
                       "How much darker each pass is; 0 is a tube and up "
                       "is a string");
    plugin->setArgRange(args[IN_DAMP], 0, COMB_DAMP_MAX);
    args[IN_SIZE] = plugin->regArg("size", thPlugin::ARG_IN);
    /* It sizes the line, read once a window: a line whose length moves
       is a line that empties. */
    plugin->setArgStep(args[IN_SIZE], 1);
    plugin->setArgDesc(args[IN_SIZE],
                       "Buffer size, 4 samples to 10 seconds; read once per "
                       "window; changing clears the line");
    plugin->setArgUnits(args[IN_SIZE], "samples");

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "Filtered signal");
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);
    args[INOUT_BUFPOS] = plugin->regArg("bufpos", thPlugin::ARG_STATE);
    /* The one-pole's last output. In an arg rather than a local because a
       window boundary is not an event: a filter that forgets where it was
       at the end of a window is a different sound at every buffer size. */
    args[INOUT_DAMPED] = plugin->regArg("damped", thPlugin::ARG_STATE);

    return 0;
}

/* How far back in samples the one-pole holds the signal, at the frequency
   the loop rings at. Zero when there is no filter, which is the case every
   reverb in the tree runs. */
static double dampDelay (double damp, double period)
{
    const double w = 2.0 * M_PI / period;

    if (damp <= 0)
        return 0;

    return atan2(damp * sin(w), 1.0 - damp * cos(w)) / w;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    float *buffer, *bufpos, *lastdamped;
    thArg *in_arg, *in_size, *in_freq, *in_feedback, *in_damp;
    thArg *out_arg;
    thArg *inout_buffer, *inout_bufpos, *inout_damped;
    unsigned int i;
    unsigned int at, len, oldlen;
    float damped;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_size = mod->getArg(node, args[IN_SIZE]); /* Buffer size */
    in_freq = mod->getArg(node, args[IN_FREQ]); /* Delay length spacing */
    in_feedback = mod->getArg(node, args[IN_FEEDBACK]);
    in_damp = mod->getArg(node, args[IN_DAMP]);

    inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
    inout_bufpos = mod->getArg(node, args[INOUT_BUFPOS]);
    inout_damped = mod->getArg(node, args[INOUT_DAMPED]);

    /* The first callback has not allocated either state buffer yet. */
    bufpos = inout_bufpos->allocate(1);
    lastdamped = inout_damped->allocate(1);
    at = (unsigned int)*bufpos;
    damped = *lastdamped;

    /* Size is sampled once per window: reallocating per sample would put
       repeated heap operations on the audio thread. A changed length starts
       a new line, including its write head and the lowpass in its loop. */
    len = (unsigned int)thClampArg((*in_size)[0], COMB_SIZE_MIN,
                                   (float)samples * 10);
    oldlen = inout_buffer->len();
    buffer = inout_buffer->allocate(len);

    if (oldlen != len)
    {
        at = 0;
        damped = 0;
    }

    if (at >= len)
        at = 0;

    for (i = 0; i < windowlen; i++)
    {
        const float in = (*in_arg)[i];
        const float feedback = (*in_feedback)[i];
        const float damp = thClampArg((*in_damp)[i], 0, COMB_DAMP_MAX);
        const double period = (double)samples /
                              thBoundFreq((*in_freq)[i], samples);
        double back = period - dampDelay(damp, period);
        unsigned int whole;
        float frac, a, b, held, lp, write;

        /* A read at the write head is the sample about to be overwritten
           and one past the end of the line has not been written yet; both
           land on the nearest read that is a delay. A period longer than
           the line is the graph asking for a note the line cannot hold. */
        if (!(back >= 1.0))
            back = 1.0;

        if (back > (double)(len - 2))
            back = len - 2;

        whole = (unsigned int)back;
        frac = (float)(back - whole);

        a = buffer[(at + len - whole) % len];
        b = buffer[(at + len - whole - 1) % len];

        /* Between the two neighbours, which is where the tuning lives. */
        held = a + (b - a) * frac;

        lp = (1 - damp) * held + damp * damped;
        damped = lp;

        write = feedback * lp + in;

        /* The line is its own input, so one sample that is not a number
           would circulate for as long as the note lasts. */
        buffer[at] = thIsFinite(write) ? write : 0;
        out[i] = buffer[at];

        at = (at + 1) % len;
    }

    *bufpos = (float)at;
    *lastdamped = damped;

    return 0;
}
