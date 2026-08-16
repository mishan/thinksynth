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
 */

/* quantize -- snap incoming notes to a pitch set.
 *
 * The transformer that makes a scale edit safe: a generator can wander
 * where it likes and this pulls every note to the nearest member of the
 * set, so retuning the scale mid-piece keeps everything consonant.
 * Chanarg events pass through untouched -- pitch is the only thing this
 * has an opinion about.
 */

#include <cstdlib>
#include <cstring>

#include "thcomposer.h"

enum { P_SCALE, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "scale", "pitch set to snap to", THC_PARAM_NOTESET,
          0, 0, 0, "48,50,52,53,55,57,59", NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Snap every note to the nearest member of a pitch set.");

    return 0;
}

struct State {
    const thcParams *params;

    /* The scale across every octave: pitch classes matter, octaves are
       the note's own business. pool holds the set as given; snapping
       works modulo 12 against it. */
    int  pool[128];
    int  poolLen;

    void reparse (void);
    int  snap (int note) const;
};

void
State::reparse (void)
{
    const char *s = params->get_string(params->ctx, paramIndex[P_SCALE]);

    poolLen = 0;
    while (s && *s && poolLen < 128)
    {
        int n = atoi(s);

        if (n >= 0 && n <= 127)
            pool[poolLen++] = n;

        if ((s = strchr(s, ',')))
            s++;
    }
}

/* Nearest member of the set in absolute pitch, searching every octave
 * transposition of the set's pitch classes; ties go down, which on an
 * equal-tempered ambient pad is the less startling direction. */
int
State::snap (int note) const
{
    if (poolLen == 0)
        return note;

    int best = note, bestDist = 128;

    for (int i = 0; i < poolLen; i++)
    {
        int pc = pool[i] % 12;

        for (int oct = -1; oct <= 10; oct++)
        {
            int candidate = oct * 12 + pc;

            if (candidate < 0 || candidate > 127)
                continue;

            int dist = candidate > note ? candidate - note
                                        : note - candidate;

            if (dist < bestDist ||
                (dist == bestDist && candidate < best))
            {
                best = candidate;
                bestDist = dist;
            }
        }
    }

    return best;
}

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->reparse();

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int index)
{
    if (index == paramIndex[P_SCALE])
        static_cast<State *>(state)->reparse();
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);

    thcEvent copy = *ev;

    /* Offs snap with the same snap as ons, or a held note's release
       would name a pitch its press never had and the note would hang. */
    if (copy.type == THC_EV_NOTE || copy.type == THC_EV_NOTEOFF)
        copy.u.note.note = st->snap(copy.u.note.note);

    out->emit(out->ctx, &copy);
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}
