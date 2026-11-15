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

/* A widener: one mono signal to two sides that differ, and that sum back
 * to the signal.
 *
 *     out0 = in + width * S,   out1 = in - width * S
 *
 * so out0 + out1 is 2 in, whatever the settings: summed to mono the side
 * cancels and leaves the input, which is the promise and the reason this is
 * a node. A Haas delay, one side late, is wider for the money and a comb
 * filter in mono; this never is.
 *
 * THE SIDE is the input highpassed at `low', `time' late, then through two
 * short allpasses. The delay moves the copy away from the input; the
 * allpasses smear it, so each side is diffuse rather than a comb with a
 * place for every frequency. Chosen by ear over either alone, and measured:
 * on strings at full width the side's correlation with the input is about
 * +0.1 (the delay alone, -0.3; the allpasses alone, -0.6), and the peak rises
 * by under half a decibel.
 *
 * THE HIGHPASS keeps the bass in the middle, where speakers want it: two
 * poles put a 60 Hz fundamental's side about 25 dB down at the default
 * 250 Hz. A bass's harmonics and a kick's click are above it and go wide,
 * which is why a bass or a drum is better left mono. The highpass is svf's,
 * which holds its input to full scale: past it the side is distorted, and
 * the mono sum is still exact.
 *
 * LEVEL: mono is exact; stereo is louder by up to sqrt(1 + width^2), 3 dB at
 * full width with all of the input above `low'. The eval's width -- the
 * side's share of the energy -- is width^2 h / (1 + width^2 h), h the
 * input's share above `low': about 0.2 at full width on a pad.
 *
 * `TIME' SLIDES. A read head sent somewhere moves there at a quarter of a
 * sample a sample, so a stepped `time' is a short glide -- the side bent by
 * up to four semitones while it lasts -- and not a click. Half a sample a
 * sample bends it by seven, and its edges are as steep as a small click's.
 * The allpasses' lengths are fixed: a length that followed a knob would need
 * its line emptied on every move.
 *
 * COST: about 9 ns a sample a voice on a desktop x86-64 (dspbench, 16
 * voices of a unison lead), against 11 for one delay::chorus and 1.5 for
 * mixer::pan -- 0.04% of a core a voice at 44.1 kHz.
 *
 * Silence in for a quarter of a second empties the lines, and a silent
 * window after that costs nothing. A non-finite input is a 0, as filt::eq
 * takes one; a NaN `width' is 0, which is mono.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"
#include "../filt/svf.h"
#include "../delay/allpass.h"

enum {IN_ARG, IN_WIDTH, IN_TIME, IN_LOW, OUT_ARG0, OUT_ARG1, INOUT_BUFFER,
      INOUT_STATE};
std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "Widener (mono-safe stereo from one signal)";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* Seconds. The longest `time', the default, the two allpasses and their
   gain, and how long a silence empties the lines. */
#define WIDEN_TIME_MAX  0.030
#define WIDEN_TIME_DEF  0.012
#define WIDEN_AP1       0.0031
#define WIDEN_AP2       0.0047
#define WIDEN_AP_GAIN   0.6f
#define WIDEN_QUIET     0.25
#define WIDEN_LOW_DEF   250.0f
#define WIDEN_SLIDE     0.25f

/* [0] the delay's write head, [1] where its read head is, [2] [3] the
   allpasses' positions, [4] [5] the highpass, [6] the rate the lines are
   cut for, [7] samples of silence in a row. */
#define WIDEN_STATE 8

struct Lines
{
    unsigned int delay, ap1, ap2;

    explicit Lines (unsigned int rate)
        : delay((unsigned int)ceil(WIDEN_TIME_MAX * rate) + 3),
          ap1((unsigned int)lround(WIDEN_AP1 * rate)),
          ap2((unsigned int)lround(WIDEN_AP2 * rate)) {}

    unsigned int total (void) const { return delay + ap1 + ap2; }
};

void module_cleanup (thPlugin *plugin)
{
}

/* A voice's lines are reserved at the rate it is prepared at, so its audio
   thread never allocates; a prototype has no reservation and cuts its own. */
void module_prepare (thNode *node, thSynthTree *mod, unsigned int samples,
                     int voice)
{
    if (voice)
    {
        mod->getArg(node, args[INOUT_BUFFER])->reserve(Lines(samples).total());
        mod->getArg(node, args[INOUT_STATE])->reserve(WIDEN_STATE);
    }
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_WIDTH] = plugin->regArg("width", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_WIDTH], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_WIDTH],
                       "How loud the side is against the middle: 0 is mono, "
                       "1 the widest");
    plugin->setArgRange(args[IN_WIDTH], 0, 1);
    args[IN_TIME] = plugin->regArg("time", thPlugin::ARG_IN);
    /* Samples, so a .dsp writes `12 ms'. No range: a sample count is right
       at one rate only, so it is held to 30 ms at the rate it runs at. */
    plugin->setArgDesc(args[IN_TIME],
                       "How late the side is, up to 30 ms; 0 is 12 ms");
    plugin->setArgUnits(args[IN_TIME], "samples");
    args[IN_LOW] = plugin->regArg("low", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_LOW],
                       "Under this the sound stays in the middle; 0 is "
                       "250 Hz");
    plugin->setArgUnits(args[IN_LOW], "Hz");
    plugin->setArgDefault(args[IN_LOW], WIDEN_LOW_DEF);
    args[OUT_ARG0] = plugin->regArg("out0", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG0], "The left side");
    plugin->setArgUnits(args[OUT_ARG0], "full scale");
    args[OUT_ARG1] = plugin->regArg("out1", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG1], "The right side");
    plugin->setArgUnits(args[OUT_ARG1], "full scale");

    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg = mod->getArg(node, args[IN_ARG]);
    thArg *in_width = mod->getArg(node, args[IN_WIDTH]);
    thArg *in_time = mod->getArg(node, args[IN_TIME]);
    thArg *in_low = mod->getArg(node, args[IN_LOW]);
    thArg *inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);
    float *out0 = mod->getArg(node, args[OUT_ARG0])->allocate(windowlen);
    float *out1 = mod->getArg(node, args[OUT_ARG1])->allocate(windowlen);
    const Lines lines(samples);
    const float rate = (float)samples;
    const float most = (float)(WIDEN_TIME_MAX * samples);
    const unsigned int quiet = (unsigned int)(WIDEN_QUIET * samples);
    const bool fresh = inout_state->len() < WIDEN_STATE ||
                       (*inout_state)[6] != rate ||
                       inout_buffer->len() != lines.total();
    float *state, *buffer;
    unsigned int i;

    if (fresh)
    {
        state = inout_state->allocate(WIDEN_STATE);
        memset(state, 0, WIDEN_STATE * sizeof(float));
        state[1] = -1;                  /* the read head starts where sent */
        state[6] = rate;
        buffer = inout_buffer->resize(lines.total(), lines.total());
        memset(buffer, 0, lines.total() * sizeof(float));
    }
    else
    {
        state = inout_state->values();
        buffer = inout_buffer->values();
    }

    /* Non-finite state is a fault upstream of here; start over from it. */
    for (int s = 0; s < WIDEN_STATE; s++)
        if (!thIsFinite(state[s]))
            state[s] = 0;

    /* A silence long enough to have emptied the lines, and silence still. */
    if (state[7] >= quiet)
    {
        bool silent = true;

        for (i = 0; i < windowlen && silent; i++)
            silent = (*in_arg)[i] == 0 || !thIsFinite((*in_arg)[i]);

        if (silent)
        {
            memset(out0, 0, windowlen * sizeof(float));
            memset(out1, 0, windowlen * sizeof(float));
            return 0;
        }
    }

    float *line = buffer, *ap1 = buffer + lines.delay, *ap2 = ap1 + lines.ap1;
    unsigned int at = (unsigned int)state[0];
    float head = state[1];
    thkAllpass a1, a2;
    thkSvf hp;
    float low = NAN;
    thkSvf::Ctl ctl = {};

    a1.pos = state[2];
    a2.pos = state[3];
    hp.ic1 = state[4];
    hp.ic2 = state[5];

    if (at >= lines.delay)
        at = 0;

    for (i = 0; i < windowlen; i++)
    {
        const float raw = (*in_arg)[i];
        const float in = thIsFinite(raw) ? raw : 0;
        const float width = thClampArg((*in_width)[i], 0, 1);
        const float t = (*in_time)[i];
        const float want = thIsFinite(t) && t > 0 ? thClampArg(t, 1, most)
                                                  : (float)(WIDEN_TIME_DEF * samples);
        const float l = (*in_low)[i];
        const float cut = thIsFinite(l) && l > 0 ? thClampArg(l, 20, 2000) : WIDEN_LOW_DEF;
        float band, high, side;

        /* A silence that has emptied the lines, as the window above that
           skips it all: nothing to read, and nowhere for the head to be. */
        if (in == 0 && state[7] >= quiet)
        {
            out0[i] = out1[i] = 0;
            continue;
        }

        if (cut != low)
        {
            ctl = thkSvf::ctl(cut, 0, rate);
            low = cut;
        }

        hp.tick(ctl, in, band, high);

        /* The read head slides to where it is sent, and starts there:
           at a note's start and again after a silence. */
        if (head < 0)
            head = want;
        else if (want > head + WIDEN_SLIDE)
            head += WIDEN_SLIDE;
        else if (want < head - WIDEN_SLIDE)
            head -= WIDEN_SLIDE;
        else
            head = want;

        line[at] = high;
        {
            const unsigned int whole = (unsigned int)head;
            const float frac = head - (float)whole;
            const unsigned int ia = at >= whole ? at - whole : at + lines.delay - whole;
            const unsigned int ib = ia == 0 ? lines.delay - 1 : ia - 1;

            side = line[ia] + (line[ib] - line[ia]) * frac;
        }
        at = at + 1 == lines.delay ? 0 : at + 1;

        side = a1.tick(ap1, lines.ap1, side, WIDEN_AP_GAIN);
        side = a2.tick(ap2, lines.ap2, side, WIDEN_AP_GAIN);

        out0[i] = in + width * side;
        out1[i] = in - width * side;

        if (in != 0)
            state[7] = 0;
        else if (state[7] < quiet && ++state[7] >= quiet)
        {
            memset(buffer, 0, lines.total() * sizeof(float));
            hp.ic1 = hp.ic2 = 0;
            head = -1;
        }
    }

    state[0] = (float)at;
    state[1] = head;
    state[2] = a1.pos;
    state[3] = a2.pos;
    state[4] = hp.ic1;
    state[5] = hp.ic2;

    return 0;
}
