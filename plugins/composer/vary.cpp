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

/* vary -- a written line, never quite the same twice.
 *
 * A grammar or a pool plays the same bar for ever, and `chance' and
 * `ratchet' only ever nick the drums: one drops a note and the other
 * rolls it. What a *line* wants is the handful of things a player does
 * to one without changing what it is -- leave a note out, take one an
 * octave up, land a little early, say one twice, lean into one from the
 * note beside it, flick a mordent on it.
 *
 * Each note gets exactly one of them, chosen by one draw against the
 * probabilities, in the order below; everything left over is the note as
 * written. Every probability defaults to 0, so a stage nobody has asked
 * anything of is a pass-through -- which is what makes this safe to put
 * under a line before deciding what it should do.
 *
 *   rest      the note is not played
 *   leap      the note, an octave away
 *   push      the note, a `grid' step early or late
 *   double    the note twice, each half as long
 *   approach  a neighbor from `scale' leaned on first
 *   ornament  a mordent from `scale': the note, its neighbor, the note
 *
 * NO HELD NOTES. A note that carries its own duration is a note this can
 * divide, shorten and place; a held one (duration <= 0, live input's
 * spelling of "who knows") has no length yet and no end to move, and a
 * dropped press would owe its release the bookkeeping xform::form spells
 * out. Held notes and their offs go through as they came, which is also
 * why there is no state here beyond the last pitch.
 *
 * TWO DRAWS PER NOTE, always -- which one, and which way -- whatever the
 * probabilities say. Drawing only when a knob is up would mean that
 * turning one reseeded every note after it, and `chance' says why that
 * is the wrong kind of surprise.
 *
 * DETERMINISM is the instance seed, so the same piece varies the same
 * way twice. Everything else is a function of the note.
 */

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <random>

#include "thcomposer.h"

enum { P_SCALE, P_GRID, P_REST, P_LEAP, P_PUSH, P_DOUBLE, P_APPROACH,
       P_ORNAMENT, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "scale",    "pitch set the neighbors come from",
          THC_PARAM_NOTESET, 0, 0, 0, "48,50,52,53,55,57,59", NULL },
        { "grid",     "how far a pushed note moves", THC_PARAM_FLOAT,
          0.01, 60, 0.25, NULL, "s" },
        { "rest",     "chance a note is left out", THC_PARAM_FLOAT,
          0, 1, 0, NULL, NULL },
        { "leap",     "chance a note moves an octave", THC_PARAM_FLOAT,
          0, 1, 0, NULL, NULL },
        { "push",     "chance a note lands a grid step early or late",
          THC_PARAM_FLOAT, 0, 1, 0, NULL, NULL },
        { "double",   "chance a note is played twice, half as long",
          THC_PARAM_FLOAT, 0, 1, 0, NULL, NULL },
        { "approach", "chance a note is led into from its neighbor",
          THC_PARAM_FLOAT, 0, 1, 0, NULL, NULL },
        { "ornament", "chance a note carries a mordent",
          THC_PARAM_FLOAT, 0, 1, 0, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Vary a written line: rests, leaps, pushes, doubles and ornaments.");

    return 0;
}

struct State {
    const thcParams *params;
    std::mt19937     rng;

    /* The scale, as given. Neighbors are found modulo 12 against it,
       the way xform::quantize snaps: pitch classes are the scale, and
       which octave a neighbor lands in is the note's business. */
    int pool[128];
    int poolLen;

    /* The last pitch that went out, for the direction an approach comes
       from -- which is the only thing here that looks past one note, and
       it looks backwards, so nothing waits for anything. */
    int  last;
    bool haveLast;

    void reparse (void);
    int  neighbor (int note, bool above) const;
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

/* The nearest member of the scale strictly above or below `note',
 * searching every octave of the set's pitch classes.
 *
 * A semitone when there is no scale to ask. That is the leading tone,
 * which is the approach anybody plays by ear, and it means a stage with
 * no `scale' written on it still does something musical rather than
 * nothing. */
int
State::neighbor (int note, bool above) const
{
    int best = -1;

    for (int i = 0; i < poolLen; i++)
    {
        const int pc = pool[i] % 12;

        for (int oct = 0; oct <= 10; oct++)
        {
            const int cand = oct * 12 + pc;

            if (cand < 0 || cand > 127)
                continue;

            if (above ? (cand > note && (best < 0 || cand < best))
                      : (cand < note && (best < 0 || cand > best)))
                best = cand;
        }
    }

    if (best < 0)
        best = above ? note + 1 : note - 1;

    return best < 0 ? 0 : best > 127 ? 127 : best;
}

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->rng.seed(params->seed);
    st->last = 0;
    st->haveLast = false;
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
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}

/* A velocity that stays a velocity. */
static int
vel (double v)
{
    const int i = (int)floor(v + 0.5);

    return i < 1 ? 1 : i > 127 ? 127 : i;
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    /* See the header: only a note that knows how long it is. */
    if (ev->type != THC_EV_NOTE || ev->u.note.duration <= 0)
    {
        out->emit(out->ctx, ev);
        return;
    }

    std::uniform_real_distribution<double> uni(0.0, 1.0);
    const double roll = uni(st->rng);
    const bool   up = uni(st->rng) < 0.5;

    const int    note = ev->u.note.note;
    const double dur = ev->u.note.duration;
    const int    was = st->last;
    const bool   haveWas = st->haveLast;

    /* What the line did, for the approach that comes after: the pitch
       that went out, where one went out. A note left out leaves the
       melody where it was -- there is nothing to have moved towards. */

    /* One draw against the probabilities in turn. A file whose numbers
       add to more than one starves the ones at the end, which is the
       honest reading of "a note is one thing or another" and is what a
       piece hears the moment it tries it. */
    double edge = get(P_REST);

    if (roll < edge)
        return;                                 /* rest                 */

    if (roll < (edge += get(P_LEAP)))
    {
        thcEvent copy = *ev;

        /* Away from the keyboard's edge rather than off it: an octave
           that would leave MIDI goes the other way instead, because a
           leap nobody can hear is a note deleted by accident. */
        const int hi = note + 12, lo = note - 12;

        copy.u.note.note = up ? (hi <= 127 ? hi : lo)
                              : (lo >= 0 ? lo : hi);

        st->last = copy.u.note.note;
        st->haveLast = true;

        out->emit(out->ctx, &copy);
        return;
    }

    if (roll < (edge += get(P_PUSH)))
    {
        thcEvent copy = *ev;
        const double grid = get(P_GRID);

        /* Early, unless early is before the piece began. A note pushed
           earlier than the moment its generator emitted it cannot be
           un-emitted -- the scheduler delivers what is already due as
           soon as it can -- so an anticipation is only ever as early as
           the phrase it belongs to was written ahead. That is exactly
           the case a grammar gives it, and the one a step sequencer
           does not. */
        if (up || ev->at - grid < 0)
            copy.at = ev->at + grid;
        else
            copy.at = ev->at - grid;

        st->last = note;
        st->haveLast = true;

        out->emit(out->ctx, &copy);
        return;
    }

    if (roll < (edge += get(P_DOUBLE)))
    {
        thcEvent copy = *ev;

        copy.u.note.duration = dur / 2;
        out->emit(out->ctx, &copy);

        copy.at = ev->at + dur / 2;
        out->emit(out->ctx, &copy);

        st->last = note;
        st->haveLast = true;
        return;
    }

    if (roll < (edge += get(P_APPROACH)))
    {
        /* Leaned into from the side the line is already moving in --
           rising, and the approach comes from below. With nothing to
           compare against, the draw decides. The tone takes the front
           of the note rather than the silence before it: the beat is
           where the phrase put it, and stealing from the note ahead
           cannot collide with the note behind. */
        const bool fromBelow = haveWas && was != note ? was < note : up;
        const double grid = get(P_GRID);
        double lead = dur / 4;

        if (lead > grid / 2)
            lead = grid / 2;

        thcEvent a = *ev;

        a.u.note.note = st->neighbor(note, !fromBelow);
        a.u.note.duration = lead;
        a.u.note.velocity = vel(ev->u.note.velocity * 0.85);
        out->emit(out->ctx, &a);

        thcEvent copy = *ev;

        copy.at = ev->at + lead;
        copy.u.note.duration = dur - lead;
        out->emit(out->ctx, &copy);

        st->last = note;
        st->haveLast = true;
        return;
    }

    if (roll < (edge += get(P_ORNAMENT)))
    {
        /* The mordent: the note, its neighbor, the note again, inside
           the note's own length, so the phrase is undisturbed. */
        const double third = dur / 4;
        thcEvent copy = *ev;

        copy.u.note.duration = third;
        out->emit(out->ctx, &copy);

        copy.at = ev->at + third;
        copy.u.note.note = st->neighbor(note, up);
        out->emit(out->ctx, &copy);

        copy.at = ev->at + 2 * third;
        copy.u.note.note = note;
        copy.u.note.duration = dur - 2 * third;
        out->emit(out->ctx, &copy);

        st->last = note;
        st->haveLast = true;
        return;
    }

    st->last = note;
    st->haveLast = true;

    out->emit(out->ctx, ev);
}
