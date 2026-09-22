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

/* cloud -- one note becomes a shimmer of entries.
 *
 * Every note that passes is followed by `count' copies of itself spread
 * over the next `time' seconds, each a pitch drawn from `scale' within
 * `pitch' semitones of the note, each a little quieter than the last,
 * each somewhere else in the stereo field. One held chord in, and what
 * comes out is the chord arriving in pieces, the upper voices trickling
 * in above it: the note-level counterpart of a grain cloud, and the
 * figure a harp or a celesta plays over a pad.
 *
 *   count    how many copies
 *   time     how long after the note they are spread over
 *   pitch    how far from the note, in semitones, a copy may land
 *   scale    the pitch classes a copy may land on; with none, the note's
 *            own, so the copies are the note in other octaves
 *   taper    how much quieter the last copy is than the first: 0 all at
 *            the note's level, 1 the last at nothing
 *   spread   how far either way a copy's aux0 -- the pan, in the tree's
 *            graphs -- may be moved from the note's
 *   pass     1: the note itself goes on as well; 0: only the copies
 *
 * THE ONSETS ARE EVEN, jittered: copy i starts somewhere in the i-th of
 * `count' equal slots of `time', so the entries neither bunch nor tick
 * like a clock.
 *
 * QUIETER BY LEVEL, NOT VELOCITY. A copy is the same note played the same
 * way further off, so what changes is its `level' -- the voice's gain at
 * the mix -- and a graph reading velocity as brightness or attack plays
 * every copy with the note's.
 *
 * NO HELD NOTES. A note with no duration yet (live input's "who knows")
 * has no length for its copies to take, and a copy of it would owe a
 * release this stage cannot know the time of; xform::vary says the same.
 * Held notes and their offs go through as they came.
 *
 * DETERMINISM is the instance seed. Three draws a copy -- when, what
 * pitch, where -- always, whatever the params, so turning a knob does not
 * reseed the copies after it.
 */

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <random>
#include <vector>

#include "thcomposer.h"

enum { P_COUNT_, P_TIME, P_PITCH, P_SCALE, P_TAPER, P_SPREAD, P_PASS,
       P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "count",  "how many copies of each note", THC_PARAM_INT,
          1, 32, 4, NULL, NULL },
        { "time",   "how long after the note the copies are spread over",
          THC_PARAM_FLOAT, 0.01, 60, 2, NULL, "s" },
        { "pitch",  "how far from the note a copy may land", THC_PARAM_INT,
          0, 36, 12, NULL, NULL },
        { "scale",  "pitch classes a copy may land on; none is the note's",
          THC_PARAM_NOTESET, 0, 0, 0, "", NULL },
        { "taper",  "how much quieter the last copy is than the first",
          THC_PARAM_FLOAT, 0, 1, 0.5, NULL, NULL },
        { "spread", "how far either way a copy's aux0 (pan) moves",
          THC_PARAM_FLOAT, 0, 1, 0.7, NULL, NULL },
        { "pass",   "1: the note itself is heard too; 0: only the copies",
          THC_PARAM_INT, 0, 1, 1, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Spread each note into copies across time, pitch and the stereo "
        "field.");

    return 0;
}

struct State {
    const thcParams *params;
    std::mt19937     rng;

    /* The scale's pitch classes, as a set of twelve. Empty is "the note's
       own", which is decided per note. */
    bool classes[12];
    bool haveScale;

    void reparse (void);
};

void
State::reparse (void)
{
    const char *s = params->get_string(params->ctx, paramIndex[P_SCALE]);

    memset(classes, 0, sizeof(classes));
    haveScale = false;

    while (s && *s)
    {
        const int n = atoi(s);

        if (n >= 0 && n <= 127)
        {
            classes[n % 12] = true;
            haveScale = true;
        }

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

    if ((int)get(P_PASS))
        out->emit(out->ctx, ev);

    const int    count = (int)get(P_COUNT_);
    const double time = get(P_TIME);
    const int    reach = (int)get(P_PITCH);
    const double taper = get(P_TAPER);
    const double spread = get(P_SPREAD);
    const int    note = ev->u.note.note;
    /* A level of 0 is an unset one, which the scheduler reads as 1. */
    const float  level = ev->u.note.level > 0 ? ev->u.note.level : 1;

    /* Every pitch a copy may land on, low to high. */
    std::vector<int> choices;

    for (int n = note - reach; n <= note + reach; n++)
        if (n >= 0 && n <= 127 &&
            (st->haveScale ? st->classes[n % 12] : n % 12 == note % 12))
            choices.push_back(n);

    std::uniform_real_distribution<double> uni(0.0, 1.0);

    for (int i = 0; i < count; i++)
    {
        const double uWhen = uni(st->rng);
        const double uPitch = uni(st->rng);
        const double uPlace = uni(st->rng);
        thcEvent copy = *ev;

        copy.at = ev->at + time * (i + uWhen) / count;

        if (!choices.empty())
        {
            size_t k = (size_t)(uPitch * (double)choices.size());

            if (k >= choices.size())
                k = choices.size() - 1;

            copy.u.note.note = choices[k];
        }

        /* From the note's level at the first copy down to (1 - taper) of
           it at the last. */
        {
            const double along = count > 1 ? (double)i / (count - 1) : 0;
            const double gain = 1 - taper * along;

            /* A copy tapered to nothing is not sent: at level 0 the
               scheduler would lift it back to full. */
            if (!(gain > 0))
                continue;

            copy.u.note.level = (float)(level * gain);
        }

        {
            const double pan = ev->u.note.aux[0] + spread * (2 * uPlace - 1);

            copy.u.note.aux[0] = (float)(pan < -1 ? -1 : pan > 1 ? 1 : pan);
        }

        out->emit(out->ctx, &copy);
    }
}
