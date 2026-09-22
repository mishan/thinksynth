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

/* level -- a chain, turned down.
 *
 * Every note that passes has its `level' multiplied by `gain'. The
 * level is the voice's gain at the mix and nothing else: velocity goes
 * to the graph untouched, so a hat under a level of 0.5 is half as
 * loud and exactly as open, and a bass under it keeps its accents.
 * That is what this stage is for -- an instrument's `amp' is the whole
 * channel, which two chains may share, and velocity is not loudness on
 * half the instruments in the tree.
 *
 * Bound to a knob, it is a fader on the canvas. A gain of 0 is a mute.
 */

#include <cstddef>

#include "thcomposer.h"

enum { P_GAIN, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "gain", "note level multiplier", THC_PARAM_FLOAT,
          0, 2, 1, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host, "Scale the level of every note.");

    return 0;
}

struct State {
    const thcParams *params;
};

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;

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

    if (ev->type != THC_EV_NOTE)
    {
        out->emit(out->ctx, ev);
        return;
    }

    const double gain = p->get(p->ctx, paramIndex[P_GAIN]);

    /* A gain of zero is a mute: the note is dropped rather than sent on
       at level zero, which the scheduler reads as "unset" and would lift
       back to one. The note carries its own duration, so nothing hangs. */
    if (!(gain > 0))
        return;

    thcEvent copy = *ev;
    copy.u.note.level *= (float)gain;
    out->emit(out->ctx, &copy);
}
