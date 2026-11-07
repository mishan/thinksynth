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
 */

/* follow -- a line that moves with the chords.
 *
 * A written progression is a list of roots and how long each lasts. An
 * ostinato, a bass figure or a pad written once over the first of them
 * should move when the root does, the way a player's hand does: the same
 * shape, a third lower under the flat sixth. Without this a piece said
 * it by hand, a pool entry per onset -- twenty-four roots to give a bass
 * three notes a bar for eight bars -- or did not say it at all, and the
 * figure droned on one chord through all the others.
 *
 * `roots' is the progression, one root per `every', the first of them
 * home. Each note is moved as far as the root under it has moved from
 * home, counted in degrees of `scale': D to Bb in D minor is two degrees
 * down, so a D A F figure comes out Bb F D, a major chord where the
 * scale puts one. With the chromatic default the count is in semitones
 * and this is a transposition that follows a timeline.
 *
 * WHICH ROOT IS UNDER A NOTE is read off the note's own time, not off
 * when it reached this stage: a grammar that emits eight bars at once is
 * moved bar by bar, as xform::form gates it. The timeline counts from
 * the top of the transport, so `every = 4 beats' with eight roots is an
 * eight-bar progression that keeps its bar lines however the chains
 * around it start, and a section cued in at a bar line (docs/
 * GEN_FORMAT.md 5c) comes in on whichever chord that bar holds. Counted
 * in seconds at the tempo now, because a transformer is not told the
 * beat: a tempo change mid-piece moves every chord line back to the top
 * and the progression jumps. The same honest limit xform::form and the
 * sections have.
 *
 * Counted by walking semitones and stopping on the scale's tones, as
 * xform::harmonize counts, so a note the scale does not spell still
 * moves by the right number of scale steps from where it is. A note
 * moved off the keyboard is dropped, as xform::transpose drops one. A
 * release goes to the pitch its press was sent to, whatever the root
 * is by then, so a note held across a chord change ends where it began.
 *
 * Nothing random.
 */

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "thcomposer.h"

enum { P_ROOTS, P_EVERY, P_SCALE, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "roots", "the progression: one root per `every', the first home",
          THC_PARAM_NOTESET, 0, 0, 0, "60", NULL },
        { "every", "how long each root lasts", THC_PARAM_FLOAT,
          0.02, 600, 2, NULL, "s" },
        { "scale", "the steps a move is counted in; chromatic by default",
          THC_PARAM_NOTESET, 0, 0, 0,
          "60,61,62,63,64,65,66,67,68,69,70,71", NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Move each note with a written progression, by as many scale "
        "steps as the root under it has moved from the first.");

    return 0;
}

struct State {
    const thcParams *params;

    std::vector<int> roots;
    bool pc[12];

    /* Held pitches this stage passed, and where each was sent, oldest
       first: the release goes where its press went. */
    std::map<int, std::vector<int> > held;
    std::set<int> seen;

    void reparse (void);
};

static void
parseNotes (const char *s, std::vector<int> &out)
{
    out.clear();

    while (s && *s)
    {
        const int n = atoi(s);

        if (n >= 0 && n <= 127)
            out.push_back(n);

        if ((s = strchr(s, ',')))
            s++;
        else
            break;
    }
}

void
State::reparse (void)
{
    parseNotes(params->get_string(params->ctx, paramIndex[P_ROOTS]), roots);

    std::vector<int> scale;

    parseNotes(params->get_string(params->ctx, paramIndex[P_SCALE]), scale);

    for (int i = 0; i < 12; i++)
        pc[i] = scale.empty();

    for (size_t i = 0; i < scale.size(); i++)
        pc[scale[i] % 12] = true;
}

/* Scale steps from `from' to `to': positive up, negative down. */
static int
stepsBetween (const State *st, int from, int to)
{
    int n = 0;

    if (to > from)
    {
        for (int at = from + 1; at <= to; at++)
            if (st->pc[at % 12])
                n++;
    }
    else
    {
        for (int at = from - 1; at >= to; at--)
            if (st->pc[at % 12])
                n--;
    }

    return n;
}

/* `n' scale steps from `note', or -1 off the keyboard. */
static int
walk (const State *st, int note, int n)
{
    int at = note;

    while (n != 0)
    {
        at += n > 0 ? 1 : -1;

        if (at < 0 || at > 127)
            return -1;

        if (st->pc[at % 12])
            n += n > 0 ? -1 : 1;
    }

    return at;
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
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int index)
{
    if (index == paramIndex[P_ROOTS] || index == paramIndex[P_SCALE])
        static_cast<State *>(state)->reparse();
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;

    if (ev->type != THC_EV_NOTE && ev->type != THC_EV_NOTEOFF)
    {
        out->emit(out->ctx, ev);
        return;
    }

    const int pitch = ev->u.note.note;
    thcEvent copy = *ev;

    if (ev->type == THC_EV_NOTEOFF)
    {
        std::map<int, std::vector<int> >::iterator it = st->held.find(pitch);

        if (it != st->held.end() && !it->second.empty())
        {
            copy.u.note.note = it->second.front();
            it->second.erase(it->second.begin());

            if (it->second.empty())
                st->held.erase(it);

            out->emit(out->ctx, &copy);
        }
        else if (!st->seen.count(pitch))
            out->emit(out->ctx, ev);       /* an orphan; see harmonize  */

        return;
    }

    st->seen.insert(pitch);

    int to = pitch;

    if (!st->roots.empty())
    {
        const double every = p->get(p->ctx, paramIndex[P_EVERY]);
        const size_t n = st->roots.size();

        /* The nudge xform::form and the sections make, for the same
           reason: a note written on the bar line belongs to the chord
           it opens. */
        double slot = every > 0 ? std::floor(ev->at / every + 1e-6) : 0;

        if (slot < 0 || !std::isfinite(slot))
            slot = 0;

        const int root = st->roots[(size_t)std::fmod(slot, (double)n)];

        to = walk(st, pitch, stepsBetween(st, st->roots[0], root));
    }

    if (to < 0)
        return;                            /* off the keyboard: dropped */

    copy.u.note.note = to;

    if (ev->u.note.duration <= 0)
        st->held[pitch].push_back(to);

    out->emit(out->ctx, &copy);
}
