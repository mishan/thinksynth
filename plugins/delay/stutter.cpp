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

/* Stutter: the last moment, again and again, while `hold' is up.
 *
 * The input is written to a ring as it comes. When `hold' rises, the
 * last `length' samples are what play, round and round, and the ring
 * stops being written so they stay there; when it falls, the input is
 * back and the ring starts again. A DJ's beat repeat, or Orbital's
 * vocal caught on one syllable: `length' a sixteenth, an eighth, a beat.
 *
 * Nothing clicks. Each repeat fades in and out over `fade' at its own
 * seam, and the move between the input and the repeats is a crossfade of
 * the same length rather than a cut.
 *
 * The length is taken when `hold' rises and kept until it falls, so a
 * length that moves does not tear a repeat in half.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

enum { IN_ARG, IN_HOLD, IN_LENGTH, IN_FADE, OUT_ARG, INOUT_BUFFER,
       INOUT_STATE };

std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "Stutter (the last moment repeated while held)";
thPlugin::State    mystate = thPlugin::ACTIVE;

#define RING_SECONDS 4
#define FADE_DEFAULT_MS 2.0f

/* Where the write is, whether held, the repeat's start and length and
   how far into it the output is, and how far the crossfade from the
   input to the repeats has gone, 0 to 1. */
enum { S_WRITE, S_HELD, S_START, S_LENGTH, S_POS, S_MIX, S_LEN };

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
    args[IN_HOLD] = plugin->regArg("hold", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_HOLD],
                       "Above 0, repeat the last `length' instead of the "
                       "input");
    plugin->setArgRange(args[IN_HOLD], 0, 1);
    args[IN_LENGTH] = plugin->regArg("length", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_LENGTH],
                       "How much is repeated, read when `hold' rises; up to "
                       "four seconds");
    plugin->setArgUnits(args[IN_LENGTH], "samples");
    args[IN_FADE] = plugin->regArg("fade", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FADE],
                       "The fade at each repeat's seam and into and out of "
                       "the hold; 0 is 2 ms");
    plugin->setArgUnits(args[IN_FADE], "samples");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The input, or the moment held");
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg = mod->getArg(node, args[IN_ARG]);
    thArg *in_hold = mod->getArg(node, args[IN_HOLD]);
    thArg *in_length = mod->getArg(node, args[IN_LENGTH]);
    thArg *in_fade = mod->getArg(node, args[IN_FADE]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);
    thArg *inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);

    float st[S_LEN];

    for (int k = 0; k < S_LEN; k++)
        st[k] = (unsigned)k < inout_state->len() ? (*inout_state)[k] : 0;

    float *state = inout_state->allocate(S_LEN);
    const unsigned len = RING_SECONDS * samples;
    float *ring = inout_buffer->allocate(len);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    unsigned w = (unsigned)st[S_WRITE];

    if (w >= len)
        w = 0;

    for (unsigned i = 0; i < windowlen; i++)
    {
        const float in = thIsFinite((*in_arg)[i]) ? (*in_arg)[i] : 0;
        const bool hold = (*in_hold)[i] > 0;
        float fade = (*in_fade)[i];

        if (!thIsFinite(fade) || fade <= 0)
            fade = FADE_DEFAULT_MS * samples / 1000;

        /* A new hold while the last is still fading out carries on with
           the repeat it was fading, rather than jumping to a new one. */
        if (hold && st[S_HELD] == 0 && st[S_MIX] == 0)
        {
            const float length = thClampArg((*in_length)[i], 2,
                                            (float)(len - 1));

            st[S_LENGTH] = floorf(length);
            st[S_START] = (float)((w + len - (unsigned)st[S_LENGTH]) % len);
            st[S_POS] = 0;
        }

        st[S_HELD] = hold ? 1 : 0;

        if (!hold)
        {
            ring[w] = in;
            w = (w + 1) % len;
        }

        /* Toward the repeats while held, back to the input after. */
        st[S_MIX] = hold ? fminf(1, st[S_MIX] + 1 / fade)
                         : fmaxf(0, st[S_MIX] - 1 / fade);

        float rep = 0;

        if (st[S_MIX] > 0 && st[S_LENGTH] >= 2)
        {
            const float n = st[S_LENGTH];
            const float pos = st[S_POS];
            const float edge = fminf(fade, n / 2);
            const float g = fminf(1, fminf((pos + 1) / edge,
                                           (n - pos) / edge));

            rep = ring[((unsigned)st[S_START] + (unsigned)pos) % len] * g;
            st[S_POS] = pos + 1 >= n ? 0 : pos + 1;
        }

        out[i] = in + (rep - in) * st[S_MIX];
    }

    st[S_WRITE] = (float)w;
    memcpy(state, st, sizeof(st));

    return 0;
}
