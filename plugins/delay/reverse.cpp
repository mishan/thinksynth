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

/* Reverse: the input in chunks, each played backwards.
 *
 * Everything that comes in is written to a ring. Two read heads walk back
 * through it, each starting where the write was at the top of its chunk
 * and moving the other way, so a chunk `size' long comes out back to
 * front, beginning at once and ending `size' later -- a swell where the
 * input had a decay. The heads are half a chunk apart and each is shaped
 * by sin^2 over its chunk, so the two always add up to one and neither
 * chunk's edge is heard.
 *
 * Put a reverb in front and the tail of every note arrives before it,
 * rising: the reverse reverb on a hundred records. On its own it is the
 * backwards echo.
 *
 * The phase is a float, kept as one so the state holds it exactly, and
 * the write position a whole sample count, so what comes out is the same
 * at any window. `size' may move; the chunk length in use changes only
 * where a chunk ends, so a turned knob never throws a head mid-chunk.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

enum { IN_ARG, IN_SIZE, OUT_ARG, INOUT_BUFFER, INOUT_STATE };

std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "Reverse (the input in chunks, each backwards)";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* A chunk of up to two seconds reads back to four seconds behind the
   write. */
#define CHUNK_SECONDS_MAX 2

enum { S_WRITE, S_PHASE, S_SIZE, S_LEN };

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
    args[IN_SIZE] = plugin->regArg("size", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SIZE],
                       "How long each backwards chunk is; up to two seconds");
    plugin->setArgUnits(args[IN_SIZE], "samples");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The input, a chunk at a time, "
                                      "backwards");
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg = mod->getArg(node, args[IN_ARG]);
    thArg *in_size = mod->getArg(node, args[IN_SIZE]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);
    thArg *inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);

    float st[S_LEN];

    for (int k = 0; k < S_LEN; k++)
        st[k] = (unsigned)k < inout_state->len() ? (*inout_state)[k] : 0;

    float *state = inout_state->allocate(S_LEN);
    const unsigned len = 2 * CHUNK_SECONDS_MAX * samples + 2;
    float *ring = inout_buffer->allocate(len);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    unsigned w = (unsigned)st[S_WRITE];
    float phase = st[S_PHASE];
    float size = st[S_SIZE];

    if (w >= len)
        w = 0;

    for (unsigned i = 0; i < windowlen; i++)
    {
        const float in = (*in_arg)[i];
        ring[w] = thIsFinite(in) ? in : 0;

        if (size < 2 || phase >= size)
        {
            phase = 0;
            size = thClampArg((*in_size)[i], 2,
                              (float)(CHUNK_SECONDS_MAX * samples));
        }

        /* Half a chunk apart, so one window is the other's complement. */
        const float g = sinf((float)M_PI * phase / size);
        float y = 0;

        for (int h = 0; h < 2; h++)
        {
            const float p = fmodf(phase + h * size / 2, size);

            /* Back from where the write was when this head's chunk began,
               as far as the write has since come forward. */
            const double back = 2.0 * p + 1;
            double at = (double)w - back;

            while (at < 0)
                at += len;

            const unsigned a = (unsigned)at % len;
            const unsigned b = (a + 1) % len;
            const float frac = (float)(at - floor(at));
            const float v = ring[a] + (ring[b] - ring[a]) * frac;
            y += v * (h == 0 ? g * g : 1 - g * g);
        }

        out[i] = y;
        phase += 1;
        w = (w + 1) % len;
    }

    st[S_WRITE] = (float)w;
    st[S_PHASE] = phase;
    st[S_SIZE] = size;
    memcpy(state, st, sizeof(st));

    return 0;
}
