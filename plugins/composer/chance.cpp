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

/* chance -- a note gets through, probably.
 *
 * The probability gate thcomposer.h listed among the transformers on
 * its first day. A ring of sixteen closed hats is a machine; the same
 * ring through a gate at nine in ten is a drummer, because the hat you
 * expected and did not get is the whole difference. Every note is kept
 * with probability `prob', drawn from the instance seed, so the same
 * piece drops the same hats twice.
 *
 * A dropped note's release must be dropped too -- see xform::form for
 * why an off with no on downstream is worse than nothing -- and that is
 * the only state here.
 */

#include <cstddef>
#include <map>
#include <random>
#include <set>

#include "thcomposer.h"

enum { P_PROB, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "prob", "chance a note is kept", THC_PARAM_FLOAT,
          0, 1, 0.75, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Keep each note with a probability; drop the rest.");

    return 0;
}

struct State {
    const thcParams *params;
    std::mt19937     rng;

    std::map<int, int> down;    /* pitch -> ons kept, not yet released   */
    std::set<int>      seen;
};

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->rng.seed(params->seed);

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;

    if (ev->type == THC_EV_NOTE)
    {
        /* uniform_real_distribution agrees to the bit across libraries;
           see thcRandom.h. Drawn even when prob is 1, so turning the
           knob does not reseed everything after it. */
        std::uniform_real_distribution<double> uni(0.0, 1.0);
        const double roll = uni(st->rng);

        if (roll >= p->get(p->ctx, paramIndex[P_PROB]))
            return;

        st->down[ev->u.note.note]++;
        st->seen.insert(ev->u.note.note);
        out->emit(out->ctx, ev);
        return;
    }

    if (ev->type == THC_EV_NOTEOFF)
    {
        std::map<int, int>::iterator it = st->down.find(ev->u.note.note);

        if (it != st->down.end() && it->second > 0)
        {
            if (--it->second == 0)
                st->down.erase(it);

            out->emit(out->ctx, ev);
        }
        else if (!st->seen.count(ev->u.note.note))
            out->emit(out->ctx, ev);

        return;
    }

    out->emit(out->ctx, ev);
}
