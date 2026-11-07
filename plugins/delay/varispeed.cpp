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

/* Varispeed: what comes in, played back at another speed.
 *
 * The signal is recorded as it arrives and read back by a head that
 * moves `speed' samples a sample. At 1 the head is at the write and this
 * is a wire. Below 1 it falls behind and everything slows and drops in
 * pitch together, which is a tape machine's motor being switched off --
 * at 0 the head stands still on one moment. Above 1, while it is behind,
 * it catches up, which is the motor spinning back up.
 *
 * A SLOW TAPE IS A QUIET ONE. A playback head's level falls with the
 * tape's speed, so below a third of the speed the output is turned down
 * with it, and a stopped head is silence rather than whatever sample it
 * stopped on held as a DC level.
 *
 * BACK AT 1, IT RETURNS TO NOW. A head left behind would play the piece
 * late for ever, so once `speed' has been at 1 for ten milliseconds the
 * output crossfades from the late head to the input over ten more and
 * the head jumps to the write; once begun, the crossfade finishes. A
 * speed only passing through 1 on its way up to catch up is not at 1
 * long enough to start one. A stop is therefore `speed' ridden down to 0
 * and back up to 1, and nothing else needs remembering.
 *
 * The ring is ten seconds, which is how far behind the head can fall;
 * further behind than that is silence. The lag is a whole sample count
 * and a fraction, so a head ten seconds behind still moves by exactly
 * the step it is given. Ten seconds is a few megabytes a node, zeroed on
 * its first window: right for a channel's effect, heavy for every voice.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

enum { IN_ARG, IN_SPEED, OUT_ARG, INOUT_BUFFER, INOUT_STATE };

std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "Varispeed (a tape motor slowing and starting)";
thPlugin::State    mystate = thPlugin::ACTIVE;

#define RING_SECONDS 10
#define RETURN_MS 10.0f

/* Below this speed the output is turned down in proportion. */
#define QUIET_BELOW (1.0f / 3)

/* The write position, how far behind it the head is (whole samples and a
   fraction), how long the speed has been at 1, and how far into a return
   to now the output is (0 for not returning). */
enum { S_WRITE, S_LAG, S_LAGF, S_AT1, S_RETURN, S_LEN };

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
    args[IN_SPEED] = plugin->regArg("speed", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SPEED],
                       "How fast the playback runs: 1 is a wire, 0 stopped, "
                       "above 1 catching up");
    plugin->setArgRange(args[IN_SPEED], 0, 2);
    plugin->setArgDefault(args[IN_SPEED], 1);
    plugin->setArgUnits(args[IN_SPEED], "ratio");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The input at that speed");
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg = mod->getArg(node, args[IN_ARG]);
    thArg *in_speed = mod->getArg(node, args[IN_SPEED]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);
    thArg *inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);

    float st[S_LEN];

    for (int k = 0; k < S_LEN; k++)
        st[k] = (unsigned)k < inout_state->len() ? (*inout_state)[k] : 0;

    float *state = inout_state->allocate(S_LEN);
    const unsigned len = RING_SECONDS * samples;
    float *ring = inout_buffer->allocate(len);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    const float returnLen = RETURN_MS * samples / 1000;

    unsigned w = (unsigned)st[S_WRITE];

    if (w >= len)
        w = 0;

    for (unsigned i = 0; i < windowlen; i++)
    {
        const float in = (*in_arg)[i];
        float speed = (*in_speed)[i];

        if (!thIsFinite(speed))
            speed = 1;
        speed = thClampArg(speed, 0, 2);

        ring[w] = thIsFinite(in) ? in : 0;

        /* The head moves `speed'; the write moves 1. */
        double lag = (double)st[S_LAG] + st[S_LAGF] + 1 - speed;
        bool lost = false;

        if (lag < 0)
            lag = 0;
        if (lag > (double)(len - 2))
        {
            lag = len - 2;
            lost = true;
        }

        double at = (double)w - lag;

        if (at < 0)
            at += len;

        const unsigned a = (unsigned)at % len;
        const unsigned b = (a + 1) % len;
        const float frac = (float)(at - floor(at));
        float y = ring[a] + (ring[b] - ring[a]) * frac;

        if (lost)
            y = 0;
        else if (speed < QUIET_BELOW)
            y *= speed / QUIET_BELOW;

        const bool at1 = speed >= 0.999f && speed <= 1.001f;

        st[S_AT1] = at1 ? st[S_AT1] + 1 : 0;

        /* Back at speed for long enough, with the head behind: crossfade
           to now, and finish what was begun. */
        if (lag > 0 && (st[S_RETURN] > 0 || st[S_AT1] > returnLen))
        {
            st[S_RETURN] += 1;

            const float t = st[S_RETURN] / returnLen;

            if (t >= 1)
            {
                lag = 0;
                st[S_RETURN] = 0;
                y = ring[w];
            }
            else
                y = y + (ring[w] - y) * t;
        }
        else
            st[S_RETURN] = 0;

        st[S_LAG] = (float)floor(lag);
        st[S_LAGF] = (float)(lag - floor(lag));
        out[i] = y;
        w = (w + 1) % len;
    }

    st[S_WRITE] = (float)w;
    memcpy(state, st, sizeof(st));

    return 0;
}
