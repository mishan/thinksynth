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

/* form -- which bars a stage is heard in.
 *
 * Every generator in the tree plays forever at one density, which is
 * why no piece had an intro, a section, a drop or a fill: there was
 * nothing to say "not yet" or "not this time round". This is that. A
 * pattern of marks, one per bar -- `x' plays, `.' rests -- and the
 * length of a bar; a note whose time falls in a resting bar is dropped,
 * and everything else goes through untouched. `xxx.' under a solo is a
 * solo that sits out every fourth phrase; `....x' with `mode' set to
 * play-on-after is an intro of four silent bars and then the part.
 *
 * The bar is counted from transport zero in the units the file wrote:
 * written in beats it is converted through the tempo when the event
 * is looked at, like every beat-valued duration in the tree, so this
 * holds at one tempo and drifts under a tempo change the way a
 * `period' in beats does not. That is the honest limit of a transformer
 * that is handed events and not a clock. Events reach a transformer
 * when the stage upstream *emits* them, which for a grammar is a whole
 * phrase at once, and the bar is the bar the note lands in, not the
 * one the phrase was emitted in -- so a phrase eight bars long with a
 * gate under it is gated bar by bar, as it should be.
 *
 * HELD NOTES. An off must go where its on went. A note pressed in a
 * playing bar and released in a resting one still needs its release,
 * and a note dropped must have its off dropped too, or something
 * downstream gets an off for a pitch it never heard and takes another
 * stage's note down with it. So both sides are counted: the held presses
 * that went through, whose offs go through too, and the held presses
 * that did not, whose offs are swallowed. A note carrying its own
 * duration is counted on neither side -- its off is derived downstream
 * and never comes back here to be matched against anything.
 *
 * DETERMINISM. A function of the time and the pattern; nothing random.
 */

#include <cstddef>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

#include "thcomposer.h"

enum { P_PATTERN, P_BAR, P_MODE, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "pattern", "one mark per bar: x plays, . rests",
          THC_PARAM_STRING, 0, 0, 0, "x", NULL },
        { "bar",     "length of one bar", THC_PARAM_FLOAT,
          0.05, 600, 2, NULL, "s" },
        { "mode",    "after the pattern: 0 repeat it, 1 play on, 2 rest",
          THC_PARAM_INT, 0, 2, 0, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Let notes through only in the bars a pattern marks.");

    return 0;
}

struct State {
    const thcParams *params;

    std::map<int, int> down;    /* pitch -> held ons passed, not released */
    std::map<int, int> dropped; /* pitch -> held ons dropped, not released */
    std::set<int>      seen;    /* every pitch ever passed; see harmonize */
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

/* Whether the bar `at' falls in is a playing one. */
static bool
open (const State *st, double at)
{
    const thcParams *p = st->params;
    const char *pattern = p->get_string(p->ctx, paramIndex[P_PATTERN]);
    const double bar = p->get(p->ctx, paramIndex[P_BAR]);

    const int len = pattern ? (int)strlen(pattern) : 0;

    if (len == 0 || bar <= 0)
        return true;

    /* A note written on the bar line is in the bar it starts, not the
       one it ends; the nudge is smaller than anything a scheduler
       resolves. */
    long n = (long)floor(at / bar + 1e-6);

    if (n < 0)
        n = 0;

    if (n >= len)
    {
        switch ((int)p->get(p->ctx, paramIndex[P_MODE]))
        {
            case 1:  return true;
            case 2:  return false;
            default: n %= len; break;
        }
    }

    return pattern[n] == 'x' || pattern[n] == 'X';
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);

    if (ev->type == THC_EV_NOTE)
    {
        /* Held notes only, on both sides of the gate. One that carries
           its own duration has its off derived downstream and never
           routed back through here, so there is nothing to match it
           against and remembering it only leaves a tally that the next
           note at that pitch spends by mistake. harmonize and swing
           guard the same way. */
        const bool held = ev->u.note.duration <= 0;

        if (!open(st, ev->at))
        {
            /* Dropped -- and a dropped press owes its release the same
               silence. Without this the off arrives later looking like
               anybody's, gets forwarded as an orphan, and takes down
               whatever else is sounding at that pitch. */
            if (held)
                st->dropped[ev->u.note.note]++;

            return;
        }

        if (held)
        {
            st->down[ev->u.note.note]++;
            st->seen.insert(ev->u.note.note);
        }

        out->emit(out->ctx, ev);
        return;
    }

    if (ev->type == THC_EV_NOTEOFF)
    {
        const int note = ev->u.note.note;

        /* Asked first: an off downstream for a press that never went out
           is the expensive mistake, and a swallowed one is only silence
           that was already silent. */
        std::map<int, int>::iterator d = st->dropped.find(note);

        if (d != st->dropped.end() && d->second > 0)
        {
            if (--d->second == 0)
                st->dropped.erase(d);

            return;
        }

        std::map<int, int>::iterator it = st->down.find(note);

        if (it != st->down.end() && it->second > 0)
        {
            if (--it->second == 0)
                st->down.erase(it);

            out->emit(out->ctx, ev);
        }
        else if (!st->seen.count(note))
            out->emit(out->ctx, ev);       /* an orphan; forward it     */

        return;
    }

    out->emit(out->ctx, ev);
}
