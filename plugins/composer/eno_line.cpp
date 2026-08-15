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

/* eno_line -- the simplest useful composer: one free-running line.
 *
 * One note drawn from a pool, fired on its own period with optional
 * jitter and a probability gate -- a tape loop from Music for Airports,
 * as a plugin. Several instances with incommensurate periods are the
 * piece. Exists mostly to prove the interface is comfortable to write
 * against; the .gen semantics of the python prototype (scripts/genplay.py)
 * map onto its params 1:1.
 */

#include <cstdlib>
#include <cstring>
#include <random>

#include "thcomposer.h"

enum { P_NOTES, P_PERIOD, P_JITTER, P_PROB, P_HOLD, P_VEL,
       P_VELJIT, P_COUNT };

static int paramIndex[P_COUNT];

/* No channel param: where events land is routing, routing belongs to
 * the piece, and the piece says it in a sink block. */
extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "notes",   "pitch pool to draw from", THC_PARAM_NOTESET,
          0, 0, 0, "53,56,60", NULL },
        { "period",  "time between firings", THC_PARAM_FLOAT,
          0.1, 600, 20, NULL, "s" },
        { "jitter",  "+/- time on the period", THC_PARAM_FLOAT,
          0, 60, 0, NULL, "s" },
        { "prob",    "chance a firing sounds", THC_PARAM_FLOAT,
          0, 1, 1, NULL, NULL },
        { "hold",    "time before note-off", THC_PARAM_FLOAT,
          0.05, 60, 4, NULL, "s" },
        { "vel",     "base velocity", THC_PARAM_INT, 1, 127, 80,
          NULL, NULL },
        { "vel_jitter", "+/- velocity spread", THC_PARAM_INT,
          0, 64, 0, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR);
    info->set_desc(info->host,
        "One tape loop: a note from a pool, on its own period.");

    return 0;
}

struct State {
    const thcParams *params;
    std::mt19937     rng;
    int              pool[128];
    int              poolLen;

    void reparseNotes (void);
};

/* A NOTESET arrives resolved: "53,56,60". The host parsed the note
 * names at the file boundary (thcGenLoader::parseNoteList, the one
 * shared pitch parser); no plugin ever sees pitch text. */
void
State::reparseNotes (void)
{
    const char *s = params->get_string(params->ctx, paramIndex[P_NOTES]);

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

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->rng.seed(params->seed);
    st->reparseNotes();

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int index)
{
    if (index == paramIndex[P_NOTES])
        static_cast<State *>(state)->reparseNotes();
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    std::uniform_real_distribution<double> uni(0.0, 1.0);

    if (t->running && st->poolLen && uni(st->rng) < get(P_PROB))
    {
        thcEvent ev = {};

        ev.type    = THC_EV_NOTE;
        ev.at      = t->now;
        ev.channel = 0;                    /* the sink routes            */
        ev.u.note.note = st->pool[st->rng() % st->poolLen];

        int vj = (int)get(P_VELJIT);
        int v  = (int)get(P_VEL) + (vj ? (int)(st->rng() % (2 * vj + 1)) - vj
                                       : 0);

        ev.u.note.velocity = v < 1 ? 1 : v > 127 ? 127 : v;
        ev.u.note.duration = get(P_HOLD);

        out->emit(out->ctx, &ev);
    }

    double jit = get(P_JITTER);

    return t->now + get(P_PERIOD) + (jit ? uni(st->rng) * 2 * jit - jit : 0);
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}
