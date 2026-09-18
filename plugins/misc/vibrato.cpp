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

/* A delayed vibrato: the wobble a player puts on a held note.
 *
 *     out = in * exp2(depth * sin(2*pi*phase) / 1200)
 *
 * so `depth' is in cents and the bend is the same interval at every
 * pitch -- which is what a vibrato in hertz can never be, since twenty
 * hertz on an A below the staff is a minor third and on the top A is a
 * comma. The same argument supersaw.dsp's detune makes, and the reason
 * this multiplies rather than adds.
 *
 * WHAT MAKES IT A PLAYER'S VIBRATO is the two times in front of it.
 * Nobody starts a note wobbling: the note is placed, it is held, and
 * only then does the hand begin to move -- so the depth stays at zero
 * for `delay' and then grows over `rise'. A .dsp is copied per note, so
 * both are measured from the start of the voice's own life, which is
 * exactly what is wanted and is not something a composer moving a
 * chanarg could do: it does not know when the note began.
 *
 * THE LFO IS HELD DURING THE DELAY rather than free-running under it.
 * Either way the deviation starts at zero -- the ramp sees to that --
 * but a free-running LFO arrives at the end of the delay somewhere
 * arbitrary in its cycle, so a piece with `rise = 0' would hear a step
 * into a half-open bend. Held, the bend always begins where a bend
 * begins: at the note, moving away from it. The cost is that `rate'
 * moved during the delay changes nothing, which is a knob nobody turns
 * in the first two hundred milliseconds of a note.
 *
 * Arithmetic cannot be this, for the two reasons a node ever exists:
 * the LFO integrates its own phase, and the ramp has to know how old
 * the voice is. Neither is a function of this window's inputs.
 *
 * DETERMINISM. Every voice starts at phase zero and age zero, so a note
 * is the same note twice and the corpus renders bit for bit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

enum {IN_ARG, IN_RATE, IN_DEPTH, IN_DELAY, IN_RISE, OUT_ARG, INOUT_STATE};
int args[INOUT_STATE + 1];

static const char desc[] = "Delayed vibrato";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* Where a vibrato lives: a few hertz, a few tens of cents. Both are what
   a slider should offer rather than what the arithmetic needs -- nothing
   here divides by either -- so neither is clamped to. A piece that writes
   a rate of forty gets a rate of forty, which is not a vibrato any more
   but is a thing somebody may have meant.

   The one bound that is arithmetic's is on the bend itself. `depth' is an
   exponent, so a number nobody meant is not a loud note but an infinity,
   and an infinity costs the whole mix rather than the one voice. Ten
   octaves either way is past every use and short of the exponent's end. */
#define VIBRATO_RATE_MAX   20.0f
#define VIBRATO_DEPTH_MAX  1200.0f
#define VIBRATO_BEND_MAX   12000.0f

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    /* No range and no unit. It is a frequency at every use in the tree,
       but the arithmetic is a ratio and a ratio is as good on anything;
       naming hertz here would be a claim about the one use. */
    plugin->setArgDesc(args[IN_ARG], "The signal to bend, a frequency");
    args[IN_RATE] = plugin->regArg("rate", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RATE], "How fast the bend goes round");
    plugin->setArgUnits(args[IN_RATE], "Hz");
    plugin->setArgRange(args[IN_RATE], 0, VIBRATO_RATE_MAX);
    args[IN_DEPTH] = plugin->regArg("depth", thPlugin::ARG_IN);
    /* Cents, either way from the note: 100 is a semitone up and a
       semitone down, which is already a wide vibrato. */
    plugin->setArgDesc(args[IN_DEPTH],
                       "How far the bend reaches, up and down");
    plugin->setArgUnits(args[IN_DEPTH], "cents");
    plugin->setArgRange(args[IN_DEPTH], 0, VIBRATO_DEPTH_MAX);
    args[IN_DELAY] = plugin->regArg("delay", thPlugin::ARG_IN);
    /* Samples, so a .dsp writes `250 ms' and the unit fold turns it into
       the rate's worth. Counted from the start of the voice. */
    plugin->setArgDesc(args[IN_DELAY],
                       "How long the note is held straight before the "
                       "bend starts");
    plugin->setArgUnits(args[IN_DELAY], "samples");
    args[IN_RISE] = plugin->regArg("rise", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RISE],
                       "How long the bend takes to reach `depth' once it "
                       "starts; 0 arrives at once");
    plugin->setArgUnits(args[IN_RISE], "samples");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "in, bent");

    /* [0] the LFO's phase in turns, [1] how many samples of this voice
       have gone by. Both mean what a zeroed buffer says they mean -- the
       start of the cycle and the start of the note -- so there is no
       primed flag here the way misc::slew needs one. */
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    float *state;
    thArg *in_arg, *in_rate, *in_depth, *in_delay, *in_rise;
    thArg *out_arg;
    thArg *inout_state;
    unsigned int i;
    float phase;
    double age;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    inout_state = mod->getArg(node, args[INOUT_STATE]);

    /* The phase is stepped in float rather than in a double the window
       boundary would round away. A window is not an event -- the same
       samples have to come out cut into windows of one as into windows
       of five hundred -- and the only way to promise that of a running
       sum is to keep it in the width it is stored in. */
    phase = (*inout_state)[0];
    age = (*inout_state)[1];
    state = inout_state->allocate(2);

    out = out_arg->allocate(windowlen);

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_rate = mod->getArg(node, args[IN_RATE]);
    in_depth = mod->getArg(node, args[IN_DEPTH]);
    in_delay = mod->getArg(node, args[IN_DELAY]);
    in_rise = mod->getArg(node, args[IN_RISE]);

    for (i = 0; i < windowlen; i++)
    {
        const float in = (*in_arg)[i];
        const float rate = thIsFinite((*in_rate)[i]) ? (*in_rate)[i] : 0;
        const float depth = thClampMag((*in_depth)[i], VIBRATO_BEND_MAX);
        const float delay = thClampArg((*in_delay)[i], 0, TH_WAVELENGTH_MAX);
        const float rise = thClampArg((*in_rise)[i], 0, TH_WAVELENGTH_MAX);
        float grown;
        const bool bending = age >= delay;

        /* Before the bend, during it, and after: no depth, a share of
           it, all of it. `rise' under a sample is the whole of it at
           once, which is what a player who is already wobbling sounds
           like and what the default is not. */
        if (age < delay)
            grown = 0;
        else if (rise >= 1.0f)
        {
            grown = (float)((age - delay) / rise);

            if (grown > 1.0f)
                grown = 1.0f;
        }
        else
            grown = 1.0f;

        out[i] = in * (float)exp2(grown * depth *
                                  sin(2.0 * M_PI * (double)phase) / 1200.0);

        /* The age stops counting once there is nothing left to count
           towards, which keeps a float's integers exact however long a
           note is held; and the LFO turns only once the delay is over,
           which is the held phase the head argues for. `bending' is read
           from the age this sample was drawn with rather than from the
           one the next will be, or the first sample of the bend would be
           a step into the LFO rather than the note itself. */
        if (age < (double)delay + rise)
            age += 1.0;

        if (bending)
            phase += rate / (float)samples;

        if (phase >= 1.0f || phase < 0.0f)
            phase -= floorf(phase);
    }

    state[0] = phase;
    state[1] = (float)age;

    return 0;
}
