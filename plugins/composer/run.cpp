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

/* run -- a scale pickup that arrives exactly on the written note.
 *
 * Positive `steps' climb from below; negative steps descend from above.
 * The target keeps its time, length and velocity. Each pickup note lasts
 * one step, ending where the next begins. The scale is a set of pitch
 * classes, repeated across octaves; a target outside the scale is still
 * the target, with scale tones leading into it.
 *
 * This only works when the upstream generator has written the target
 * ahead of time. A target earlier than `time' cannot have a full pickup
 * without notes before transport zero, so it goes through unchanged.
 * Held notes and their releases also pass through: a release has no
 * duration with which to place the pickup.
 *
 * The instance seed decides which targets get a run. One draw per
 * duration-bearing note keeps the sequence reproducible across replays.
 */

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "thcomposer.h"

enum { P_SCALE, P_STEPS, P_TIME, P_PROB, P_VEL, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "scale", "pitch set the pickup walks through", THC_PARAM_NOTESET,
          0, 0, 0, "48,50,52,53,55,57,59", NULL },
        { "steps", "scale degrees before the target; negative descends",
          THC_PARAM_INT, -24, 24, 7, NULL, NULL },
        { "time", "length of the pickup before the target",
          THC_PARAM_FLOAT, 0.01, 60, 0.5, NULL, "s" },
        { "prob", "chance that a target gets a pickup",
          THC_PARAM_FLOAT, 0, 1, 0, NULL, NULL },
        { "vel", "pickup velocity; 0 uses the target's velocity",
          THC_PARAM_INT, 0, 127, 0, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Lead into a written note with an ascending or descending scale run.");

    return 0;
}

struct State {
    const thcParams *params;
    std::mt19937 rng;
    bool pitchClass[12];

    void reparse (void)
    {
        memset(pitchClass, 0, sizeof(pitchClass));

        const char *s = params->get_string(params->ctx, paramIndex[P_SCALE]);

        while (s && *s)
        {
            char *end;
            const long n = strtol(s, &end, 10);

            if (end != s && n >= 0 && n <= 127)
                pitchClass[n % 12] = true;

            s = strchr(end, ',');

            if (s)
                s++;
        }
    }

    int nextDegree (int note, int direction) const
    {
        for (int n = note + direction; n >= 0 && n <= 127;
             n += direction)
            if (pitchClass[n % 12])
                return n;

        return -1;
    }
};

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

    if (ev->type != THC_EV_NOTE || ev->u.note.duration <= 0)
    {
        out->emit(out->ctx, ev);
        return;
    }

    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    const double roll = uni(st->rng);
    const double probability = get(P_PROB);
    const double rawSteps = get(P_STEPS);
    const double time = get(P_TIME);

    if (!std::isfinite(probability) || roll >= probability ||
        !std::isfinite(rawSteps) || !std::isfinite(time) ||
        time <= 0 || ev->at < time ||
        fabs(rawSteps) < 1 || ev->u.note.note < 0 ||
        ev->u.note.note > 127)
    {
        out->emit(out->ctx, ev);
        return;
    }

    /* Ranges draw controls, but a file may write past them. No run can
       contain more than 127 distinct MIDI pitches in one direction. */
    const int count = (int)fmin(fabs(rawSteps), 127.0);
    const int direction = rawSteps > 0 ? -1 : 1;
    const double each = time / count;
    std::vector<int> notes;
    int pitch = ev->u.note.note;

    for (int i = 0; i < count; i++)
    {
        pitch = st->nextDegree(pitch, direction);

        if (pitch < 0)
            break;

        notes.push_back(pitch);
    }

    const double configuredVelocity = get(P_VEL);
    int velocity = std::isfinite(configuredVelocity)
        ? (int)fmin(fmax(configuredVelocity, 0.0), 127.0) : 0;

    if (velocity <= 0)
        velocity = ev->u.note.velocity;

    if (velocity < 1)
        velocity = 1;
    else if (velocity > 127)
        velocity = 127;

    /* Reverse the walk away from the target to play it toward the
       target. At a MIDI edge, the available notes take the final slots
       and still land on the target at its written time. */
    for (size_t i = 0; i < notes.size(); i++)
    {
        thcEvent copy = *ev;

        copy.at = ev->at - (notes.size() - i) * each;
        copy.u.note.note = notes[notes.size() - 1 - i];
        copy.u.note.duration = each;
        copy.u.note.velocity = velocity;
        out->emit(out->ctx, &copy);
    }

    out->emit(out->ctx, ev);
}
