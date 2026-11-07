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

/* throw -- a knob up for the notes a pattern picks, and down again.
 *
 * The dub engineer's move: one snare out of eight, the last word of a
 * line, sent into the echo while everything else stays dry, so the echo
 * is a thing that happens rather than a wash. Notes pass through
 * untouched; for each one `pattern' marks with an `x', this also emits
 * the knob at `up' a moment before it -- `lead', so the knob is open when
 * the note arrives -- and back at `down' `hold' after it.
 *
 * The knob is `knob', and then the instrument's own sink plays the notes
 * and rides the echo's input with them:
 *
 *     stage t xform::throw { pattern = ".......x"; hold = 0.2 s;
 *                            knob = "fx.input"; };
 *     sink { instrument = snare; };
 *
 * With `knob' empty it is whatever a chanarg sink on the chain names,
 * beside the note sink.
 *
 * `pattern' is read a character a note and goes round: `.' leaves the
 * note dry, `x' throws it. Notes at one instant are a chord and count as
 * one. Nothing random; a rewind is a fresh stage.
 *
 * A DRY NOTE PUTS THE KNOB DOWN, at its own instant, unless a throw is
 * still open then. So the knob starts at `down' on the first note
 * whatever the instrument's default, and a `down' a section mute dropped
 * is put back by the next note heard. A thrown note while a throw is
 * still open keeps it open: the earlier one's `down' would land inside
 * the later one's, so the knob is put back up there.
 */

#include <cstring>

#include "thcomposer.h"

enum { P_PATTERN, P_UP, P_DOWN, P_HOLD, P_LEAD, P_KNOB, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "pattern", "a character a note: x throws it, . leaves it",
          THC_PARAM_STRING, 0, 0, 0, "x", NULL },
        { "up",   "the knob while a note is thrown", THC_PARAM_FLOAT,
          -100000, 100000, 1, NULL, NULL },
        { "down", "the knob otherwise", THC_PARAM_FLOAT,
          -100000, 100000, 0, NULL, NULL },
        { "hold", "how long after the note the knob stays up",
          THC_PARAM_FLOAT, 0.01, 60, 0.2, NULL, "s" },
        { "lead", "how long before the note the knob goes up",
          THC_PARAM_FLOAT, 0, 1, 0.01, NULL, "s" },
        { "knob", "the chanarg it rides; empty for the one a chanarg sink "
          "names", THC_PARAM_STRING, 0, 0, 0, "", NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Ride a knob up for the notes a pattern picks: a dub throw.");

    return 0;
}

struct State {
    const thcParams *params;
    unsigned long    count;     /* notes, chords counted once */
    double           lastAt;
    bool             any;
    double           openUntil; /* when the last throw's `down' lands */
};

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->count = 0;
    st->lastAt = 0;
    st->any = false;
    st->openUntil = -1e300;

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

    out->emit(out->ctx, ev);

    if (ev->type != THC_EV_NOTE)
        return;

    const bool chord = st->any && ev->at - st->lastAt < 1e-6 &&
                       st->lastAt - ev->at < 1e-6;

    st->lastAt = ev->at;
    st->any = true;

    if (chord)
        return;

    const char *pattern = p->get_string(p->ctx, paramIndex[P_PATTERN]);
    const size_t len = pattern ? strlen(pattern) : 0;
    const unsigned long n = st->count++;
    const float up = (float)p->get(p->ctx, paramIndex[P_UP]);
    const float down = (float)p->get(p->ctx, paramIndex[P_DOWN]);

    thcEvent knob = {};

    knob.type = THC_EV_CHANARG;
    knob.channel = ev->channel;
    const char *name = p->get_string(p->ctx, paramIndex[P_KNOB]);

    /* NULL: the sink names the target. */
    knob.u.chanarg.name = name && *name ? name : NULL;

    if (len == 0 || pattern[n % len] != 'x')
    {
        if (ev->at >= st->openUntil)
        {
            knob.at = ev->at;
            knob.u.chanarg.value = down;
            out->emit(out->ctx, &knob);
        }
        return;
    }

    knob.at = ev->at - p->get(p->ctx, paramIndex[P_LEAD]);
    knob.u.chanarg.value = up;
    out->emit(out->ctx, &knob);

    if (knob.at < st->openUntil)
    {
        knob.at = st->openUntil;
        out->emit(out->ctx, &knob);
    }

    st->openUntil = ev->at + p->get(p->ctx, paramIndex[P_HOLD]);
    knob.at = st->openUntil;
    knob.u.chanarg.value = down;
    out->emit(out->ctx, &knob);
}
