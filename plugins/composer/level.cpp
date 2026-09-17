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
 * Every note that passes has its velocity multiplied by `gain'. That
 * is all, and it is here because a piece had no way to say "this
 * chain, quieter" short of editing every generator's `vel' -- and an
 * instrument's `amp' is the whole channel, which two chains may share.
 *
 * Velocity is not only loudness. A patch may read it as brightness,
 * as how open a hat is, as how hard a string was struck; hat0 fades
 * its decay on velocity squared, so a level under a ring of hats closes
 * them as well as quiets them. That is usually what was wanted, and
 * when it is not, `amp' on the instrument is the level that touches
 * nothing else.
 *
 * Bound to a knob, it is a fader on the canvas.
 */

#include <cmath>

#include "thcomposer.h"

enum { P_GAIN, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "gain", "velocity multiplier", THC_PARAM_FLOAT,
          0, 2, 1, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host, "Scale the velocity of every note.");

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

    thcEvent copy = *ev;
    const int v = (int)floor(ev->u.note.velocity *
                             p->get(p->ctx, paramIndex[P_GAIN]) + 0.5);

    copy.u.note.velocity = v < 1 ? 1 : v > 127 ? 127 : v;
    out->emit(out->ctx, &copy);
}
