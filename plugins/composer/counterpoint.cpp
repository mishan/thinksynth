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

/* counterpoint -- a second voice that argues with the first.
 *
 * First species: one note against each note that arrives, consonant
 * with it, in the scale, and moving the other way when it can. That is
 * three rules, and three rules are most of what a sixteenth-century
 * teacher had to say about two voices:
 *
 *   consonance   the added voice sits a third, fifth, sixth or octave
 *                (or a tenth) from the melody, and never anything else;
 *                thirds and sixths are preferred, fifths and octaves
 *                reserved -- except for the first note, which wants a
 *                perfect interval, as the rule has always been
 *   motion       contrary motion is best, oblique next, similar last;
 *                and two voices moving into a fifth or an octave
 *                together is the parallel-fifths rule, the one
 *                everybody remembers, and costs more than anything
 *                else -- so it happens only when the scale leaves no
 *                other consonance, which a pentatonic sometimes does
 *   voice        it steps rather than leaps, other things being equal
 *
 * Each candidate is scored on those and the best is taken; ties break
 * toward the closer interval. There is no randomness anywhere, so the
 * same melody gets the same counterpoint twice, which is what a
 * transformer owes a replay.
 *
 * xform::harmonize is the other way to put a voice under a line and the
 * difference is the point. A harmonizer adds a fixed number of degrees
 * and so moves in parallel with the melody by construction; this one
 * chooses, and chooses against. The melody is unchanged and is heard
 * unless `pass' says otherwise; the added voice is `taper' as loud.
 *
 * HELD NOTES. As in harmonize: the pitch chosen for each sounding root
 * is remembered, so the root's release takes down exactly the note it
 * put up, and a root pressed twice before its release takes the first
 * down as the second goes up. A root the scale had no consonance for is
 * remembered too, as having put up nothing -- harmonize's empty chord --
 * and only held roots are remembered at all.
 */

#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

#include "thcomposer.h"

enum { P_SCALE, P_BELOW, P_TAPER, P_PASS, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "scale", "the pitch set the added voice keeps to",
          THC_PARAM_NOTESET, 0, 0, 0, "48,50,52,53,55,57,59", NULL },
        { "below", "1: the added voice is under the melody; 0: over it",
          THC_PARAM_INT, 0, 1, 1, NULL, NULL },
        { "taper", "velocity multiplier for the added voice",
          THC_PARAM_FLOAT, 0.1, 1, 0.8, NULL, NULL },
        { "pass",  "1: the melody is heard too", THC_PARAM_INT,
          0, 1, 1, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Add a consonant second voice that prefers contrary motion: "
        "first-species counterpoint.");

    return 0;
}

struct State {
    const thcParams *params;

    bool pc[12];

    /* The last pair, for the motion rules; -1 before the first. */
    int lastMelody, lastCounter;

    std::map<int, int> sounding;   /* root -> the note put under it     */
    std::set<int>      seen;

    void reparse (void);
};

void
State::reparse (void)
{
    const char *s = params->get_string(params->ctx, paramIndex[P_SCALE]);
    int count = 0;

    for (int i = 0; i < 12; i++)
        pc[i] = false;

    while (s && *s)
    {
        const int n = atoi(s);

        if (n >= 0 && n <= 127 && !pc[n % 12])
        {
            pc[n % 12] = true;
            count++;
        }

        if ((s = strchr(s, ',')))
            s++;
        else
            break;
    }

    if (count == 0)
        for (int i = 0; i < 12; i++)
            pc[i] = true;
}

static bool
perfect (int interval)
{
    const int i = abs(interval) % 12;

    return i == 0 || i == 7;
}

/* The note to put against `melody', or -1 if the scale offers none. */
static int
choose (const State *st, int melody, bool below)
{
    /* Closest first, which is what breaks ties. */
    static const int intervals[] = { 3, 4, 7, 8, 9, 12, 15, 16 };

    int best = -1, bestScore = -1000;

    for (size_t i = 0; i < sizeof(intervals) / sizeof(intervals[0]); i++)
    {
        const int iv = intervals[i];
        const int c = below ? melody - iv : melody + iv;

        if (c < 0 || c > 127 || !st->pc[c % 12])
            continue;

        int score = perfect(iv) ? 1 : 2;

        if (st->lastCounter < 0)
            score += perfect(iv) ? 3 : 0;   /* begin on a perfect one   */
        else
        {
            const int dm = melody - st->lastMelody;
            const int dc = c - st->lastCounter;

            if (dm == 0 || dc == 0)
                score += 1;                             /* oblique      */
            else if ((dm > 0) != (dc > 0))
                score += 3;                             /* contrary     */
            else if (perfect(iv) &&
                     perfect(st->lastMelody - st->lastCounter))
                score -= 6;               /* parallel or direct perfects */

            const int leap = abs(dc);

            if (leap > 4)
                score -= (leap - 4 + 1) / 2;
        }

        if (score > bestScore)
        {
            bestScore = score;
            best = c;
        }
    }

    return best;
}

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->lastMelody = st->lastCounter = -1;
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
    if (index == paramIndex[P_SCALE])
        static_cast<State *>(state)->reparse();
}

static void
release (State *st, int root, const thcEvent &like, thcEventSink *out)
{
    std::map<int, int>::iterator it = st->sounding.find(root);

    if (it == st->sounding.end())
        return;

    /* Recorded with nothing under it: the scale offered this root no
       consonance, so there is nothing to take down and forgetting it is
       the whole release. Emitting the marker as a note number was how
       a THC_EV_NOTEOFF for note -1 used to reach delNote. */
    if (it->second >= 0)
    {
        thcEvent off = like;

        off.type = THC_EV_NOTEOFF;
        off.u.note.note = it->second;
        out->emit(out->ctx, &off);
    }

    st->sounding.erase(it);
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    if (ev->type != THC_EV_NOTE && ev->type != THC_EV_NOTEOFF)
    {
        out->emit(out->ctx, ev);
        return;
    }

    const int  root = ev->u.note.note;
    const bool pass = (int)get(P_PASS) != 0;

    if (ev->type == THC_EV_NOTEOFF)
    {
        if (st->sounding.count(root))
        {
            if (pass)
                out->emit(out->ctx, ev);

            release(st, root, *ev, out);
        }
        else if (!st->seen.count(root))
            out->emit(out->ctx, ev);           /* an orphan; see harmonize */

        return;
    }

    release(st, root, *ev, out);

    if (pass)
        out->emit(out->ctx, ev);

    const int c = choose(st, root, (int)get(P_BELOW) != 0);

    if (c >= 0)
    {
        thcEvent copy = *ev;
        const int v = (int)(ev->u.note.velocity * get(P_TAPER) + 0.5);

        copy.u.note.note = c;
        copy.u.note.velocity = v < 1 ? 1 : v > 127 ? 127 : v;

        out->emit(out->ctx, &copy);

        st->lastMelody = root;
        st->lastCounter = c;
    }

    /* Held notes only, and recorded even when the scale had no consonance
       to offer -- as harmonize records an empty chord -- so the release
       knows there is nothing to take down instead of guessing. A note
       carrying its own duration has its off derived downstream and never
       routed back here, so remembering it would strand both maps: a
       `sounding' entry nothing spends, and a `seen' entry that turns off
       the orphan branch below for a pitch this stage is not holding. */
    if (ev->u.note.duration <= 0)
    {
        st->sounding[root] = c;
        st->seen.insert(root);
    }
}
