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

/* The DX7's envelope: four rates, four levels, all 0 to 99.
 *
 * Not an ADSR with different knob names. A DX envelope is four segments,
 * each heading for its own level at its own rate -- L1 at R1, then L2 at
 * R2, then L3 at R3, where it holds while the key is down, then L4 at R4
 * once it is up -- and nothing says a level must be lower than the one
 * before. A patch that dips and swells, or that releases *upward*, is
 * written in the same eight numbers as one that does not, which is half
 * of why DX patches sound like DX patches.
 *
 * THE LEVEL IS IN DECIBELS. Each step of 0..99 is 0.75 dB, so 99 is full
 * scale, 91 is 6 dB down and 0 is silence, and a segment moves at a
 * steady number of dB a second. A straight line in dB is an exponential
 * in amplitude, which is what a decaying string or bell does; a linear
 * ramp in amplitude, which is what env::adsr draws, sounds like a fader.
 * The output is the amplitude, 0 to 1, so it multiplies like any other
 * envelope -- and a modulator's index through it is in dB too, which is
 * why a DX timbre closes the way a real one does.
 *
 * THE RATE IS EXPONENTIAL: the speed doubles every 6.2 steps, from about
 * a third of a dB a second at 0 (a sound that takes minutes to fade) to
 * an instant at 99. That is the hardware's `rate * 41 / 64' quarter-
 * octave scale, calibrated so a full-range fall takes about a second at
 * 50. Rising is faster than falling and faster still from far below:
 * the hardware jumps a rising segment to about -44 dB before it starts
 * and then climbs on a curve that slows as it nears the top, and this
 * does both, because a DX attack at any rate is snappier than its decay
 * at the same number.
 *
 * `ratescale' is the hardware's keyboard rate scaling, 0 to 7: higher
 * notes run every segment faster, as a high string dies sooner than a
 * low one, adding up to `ratescale * 31 / 8' to the quarter-octave
 * count at the top of the keyboard.
 *
 * `play' goes to 0 once the release has reached L4 *and* L4 is 0. A
 * patch whose L4 is above silence holds there for ever, as the hardware
 * did; that is a carrier nobody writes, and a modulator's play is read
 * by nothing.
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

enum {IN_R1, IN_R2, IN_R3, IN_R4, IN_L1, IN_L2, IN_L3, IN_L4,
      IN_TRIGGER, IN_NOTE, IN_RATESCALE, OUT_ARG, OUT_PLAY, INOUT_STATE};

std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "DX7 Envelope (four rates, four levels, in dB)";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* Where a rising segment that starts under it jumps to first: about
   -44 dB, the hardware's. */
#define DX_JUMP_LEVEL  40.0f

/* The segments. HELD is L3 with the key down; DONE is L4 reached. */
enum { SEG_1, SEG_2, SEG_3, SEG_HELD, SEG_RELEASE, SEG_DONE };

static float dxClamp99 (float x)
{
    if (!thIsFinite(x) || x < 0)
        return 0;
    return x > 99 ? 99 : x;
}

/* Levels per second at a rate, after rate scaling. */
static double dxSpeed (float rate, double scale)
{
    double q = dxClamp99(rate) * 41.0 / 64.0 + scale;
    if (q > 63)
        q = 63;
    /* 0.348 dB/s at q = 0, doubling every four; a level is 0.75 dB. */
    return 0.348 * pow(2.0, q / 4.0) / 0.75;
}

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    static const char *const rname[4] = {"r1", "r2", "r3", "r4"};
    static const char *const lname[4] = {"l1", "l2", "l3", "l4"};
    static const char *const rdesc[4] = {
        "Rate of the first segment, toward l1",
        "Rate of the second segment, toward l2",
        "Rate of the third segment, toward l3, where it holds",
        "Rate of the release, toward l4"};
    static const char *const ldesc[4] = {
        "Level the first segment heads for",
        "Level the second segment heads for",
        "Level the third heads for and holds while the key is down",
        "Level the release heads for"};

    plugin->setDesc (desc);
    plugin->setState (mystate);

    for (int k = 0; k < 4; k++)
    {
        args[IN_R1 + k] = plugin->regArg(rname[k], thPlugin::ARG_IN);
        plugin->setArgDesc(args[IN_R1 + k], rdesc[k]);
        plugin->setArgRange(args[IN_R1 + k], 0, 99);
    }
    for (int k = 0; k < 4; k++)
    {
        args[IN_L1 + k] = plugin->regArg(lname[k], thPlugin::ARG_IN);
        plugin->setArgDesc(args[IN_L1 + k], ldesc[k]);
        plugin->setArgRange(args[IN_L1 + k], 0, 99);
    }

    args[IN_TRIGGER] = plugin->regArg("trigger", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_TRIGGER],
                       "Note Trigger: 0 released, 1 held, 2 held by the pedal");
    plugin->setArgRange(args[IN_TRIGGER], 0, 2);
    args[IN_NOTE] = plugin->regArg("note", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_NOTE],
                       "The MIDI note, for rate scaling");
    plugin->setArgRange(args[IN_NOTE], 0, 127);
    args[IN_RATESCALE] = plugin->regArg("ratescale", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RATESCALE],
                       "How much faster every segment runs up the keyboard");
    plugin->setArgRange(args[IN_RATESCALE], 0, 7);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The envelope, as an amplitude");
    plugin->setArgRange(args[OUT_ARG], 0, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_PLAY] = plugin->regArg("play", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_PLAY], "1 while the note is sounding");
    plugin->setArgRange(args[OUT_PLAY], 0, 1);

    /* [0] the level, 0..99; [1] the segment. */
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_r[4], *in_l[4], *in_trigger, *in_note, *in_scale;
    thArg *out_arg, *play_arg, *inout_state;
    float *out, *play, *state;
    float level;
    int seg;

    for (int k = 0; k < 4; k++)
    {
        in_r[k] = mod->getArg(node, args[IN_R1 + k]);
        in_l[k] = mod->getArg(node, args[IN_L1 + k]);
    }
    in_trigger = mod->getArg(node, args[IN_TRIGGER]);
    in_note = mod->getArg(node, args[IN_NOTE]);
    in_scale = mod->getArg(node, args[IN_RATESCALE]);

    inout_state = mod->getArg(node, args[INOUT_STATE]);
    level = (*inout_state)[0];
    seg = (int)(*inout_state)[1];
    state = inout_state->allocate(2);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);
    play_arg = mod->getArg(node, args[OUT_PLAY]);
    play = play_arg->allocate(windowlen);

    for (unsigned int i = 0; i < windowlen; i++)
    {
        /* `> 0', so a choked voice's -1 is a release like a 0. */
        const bool held = (*in_trigger)[i] > 0;
        double scale, target, speed;
        int k;

        if (!held && seg < SEG_RELEASE)
            seg = SEG_RELEASE;

        k = seg < SEG_HELD ? seg : 3;
        target = dxClamp99((*in_l[k])[i]);

        if (seg == SEG_HELD)
        {
            level = (float)dxClamp99((*in_l[2])[i]);
        }
        else if (seg != SEG_DONE)
        {
            const float note = thIsFinite((*in_note)[i]) ? (*in_note)[i] : 60;
            double x = note / 3.0 - 7;
            if (x < 0)
                x = 0;
            if (x > 31)
                x = 31;
            scale = thClampArg((*in_scale)[i], 0, 7) * x / 8.0;
            speed = dxSpeed((*in_r[k])[i], scale) / samples;

            if (target > level)
            {
                if (level < DX_JUMP_LEVEL && target > DX_JUMP_LEVEL)
                    level = DX_JUMP_LEVEL;
                /* Seventeen times as fast from silence as at the top,
                   slowing as it climbs. */
                level += (float)(speed * (17.0 - level * 16.0 / 99.0));
                if (level >= target)
                    level = (float)target;
            }
            else
            {
                level -= (float)speed;
                if (level <= target)
                    level = (float)target;
            }

            if (level == (float)target)
            {
                if (seg == SEG_RELEASE)
                    seg = SEG_DONE;
                else if (seg < SEG_HELD)
                    seg++;
            }
        }

        out[i] = level > 0 ? (float)(TH_MAX * pow(2.0, (level - 99) / 8.0))
                           : 0;
        play[i] = (seg == SEG_DONE && level <= 0) ? 0 : 1;
    }

    state[0] = level;
    state[1] = seg;

    return 0;
}
