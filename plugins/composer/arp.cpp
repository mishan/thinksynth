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

/* arp -- the plugin the handoff kept promising: it has to know what is
 * held NOW, which is the one thing duration-carrying events cannot say
 * and the reason THC_EV_NOTEOFF exists.
 *
 * receive() maintains the held set from either kind of press: a live
 * key (duration <= 0) is held until its NOTEOFF; a composed note
 * (duration > 0) holds itself for exactly its duration, so an upstream
 * generator can lay chords on the arp and get them broken. tick() walks
 * the held set -- up, down, up-and-down, or seeded random -- extended
 * across `octaves', at `period' a step; the same stage arpeggiates a
 * hardware keyboard through `input midi' and an eno_line through the
 * chain, with nothing configured differently.
 *
 * `vel = 0' means "as played": each emitted note carries the velocity
 * of the held note it came from, which is what makes an arpeggio of an
 * expressive performance still expressive.
 */

#include <algorithm>
#include <random>
#include <vector>

#include "thcomposer.h"

enum { P_PATTERN, P_OCTAVES, P_PERIOD, P_HOLD, P_VEL, P_PASS, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "pattern", "0 up, 1 down, 2 up-and-down, 3 random",
          THC_PARAM_INT, 0, 3, 0, NULL, NULL },
        { "octaves", "how many octaves the held set spans",
          THC_PARAM_INT, 1, 3, 1, NULL, NULL },
        { "period",  "time between steps", THC_PARAM_FLOAT,
          0.02, 10, 0.2, NULL, "s" },
        { "hold",    "time before each step's note-off", THC_PARAM_FLOAT,
          0.01, 10, 0.15, NULL, "s" },
        { "vel",     "step velocity; 0 means as played", THC_PARAM_INT,
          0, 127, 0, NULL, NULL },
        { "pass",    "1: what is held also flows downstream",
          THC_PARAM_INT, 0, 1, 0, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR | THC_TRANSFORMER);
    info->set_desc(info->host,
        "Breaks whatever is held into a pattern of steps.");

    return 0;
}

struct Held
{
    int    note, velocity;
    double releaseAt;        /* <= 0: held until its NOTEOFF            */
};

struct State {
    const thcParams *params;
    std::mt19937     rng;

    std::vector<Held> held;
    int  pos;
    int  dir;                /* up-and-down's current direction         */

    void release (int note)
    {
        for (size_t i = 0; i < held.size(); i++)
            if (held[i].note == note)
            {
                held.erase(held.begin() + i);
                return;
            }
    }
};

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->rng.seed(params->seed);
    st->pos = 0;
    st->dir = 1;

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;

    if (ev->type == THC_EV_NOTE)
    {
        /* Re-pressing a held note replaces it: one key, one entry. */
        st->release(ev->u.note.note);

        Held h;

        h.note = ev->u.note.note;
        h.velocity = ev->u.note.velocity;
        h.releaseAt = ev->u.note.duration > 0
            ? ev->at + ev->u.note.duration : 0;

        st->held.push_back(h);
    }
    else if (ev->type == THC_EV_NOTEOFF)
        st->release(ev->u.note.note);

    if ((int)p->get(p->ctx, paramIndex[P_PASS]) != 0)
        out->emit(out->ctx, ev);
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    /* Composed notes let go of themselves. */
    for (size_t i = st->held.size(); i-- > 0; )
        if (st->held[i].releaseAt > 0 && st->held[i].releaseAt <= t->now)
            st->held.erase(st->held.begin() + i);

    if (t->running && !st->held.empty())
    {
        /* The sequence: held notes sorted ascending, repeated up the
           octaves. Rebuilt per step because the held set is live. */
        std::vector<Held> seq = st->held;

        std::sort(seq.begin(), seq.end(),
                  [](const Held &a, const Held &b)
                  { return a.note < b.note; });

        int octaves = (int)get(P_OCTAVES);
        size_t base = seq.size();

        for (int o = 1; o < octaves; o++)
            for (size_t i = 0; i < base; i++)
            {
                Held h = seq[i];

                h.note += 12 * o;

                if (h.note <= 127)
                    seq.push_back(h);
            }

        int len = (int)seq.size();
        int pick;

        switch ((int)get(P_PATTERN))
        {
            default:
            case 0:  pick = st->pos % len;                    break;
            case 1:  pick = len - 1 - st->pos % len;          break;
            case 2:                    /* up-and-down, no repeats at the
                                          turnarounds when len > 1      */
            {
                if (len == 1)
                    pick = 0;
                else
                {
                    int cycle = 2 * len - 2;
                    int k = st->pos % cycle;

                    pick = k < len ? k : cycle - k;
                }
                break;
            }
            case 3:  pick = (int)(st->rng() % len);           break;
        }

        thcEvent ev = {};

        ev.type = THC_EV_NOTE;
        ev.at = t->now;
        ev.channel = 0;                  /* the sink routes             */
        ev.u.note.note = seq[pick].note;

        int vel = (int)get(P_VEL);

        ev.u.note.velocity = vel > 0 ? vel : seq[pick].velocity;
        ev.u.note.duration = get(P_HOLD);

        out->emit(out->ctx, &ev);
        st->pos++;
    }

    return t->now + get(P_PERIOD);
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}
