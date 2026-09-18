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

/* bassline -- a root becomes a bar of bass.
 *
 * The other half of gen::progression: given one note a chord, play a
 * pattern under it. The pattern is a string of steps -- `r' the root,
 * `f' the fifth, `t' the third, `o' the root an octave up, `d' the
 * root an octave down, `.' a rest and `_' a tie -- and each step is
 * `step' long, so "r.r.f.r_" under a chord every two beats is the
 * boogie everybody knows. The third and the fifth are counted in
 * degrees of `scale', the way xform::harmonize counts, so the third is
 * minor where the key puts a minor chord and nobody has to say so.
 *
 * The root arrives in the octave the progression was written in and
 * `octave' moves the bass down from there; a chord on C3 with octave
 * at -1 has its bass on C2. `pass' is whether the root itself carries
 * on downstream, which it usually should not: this stage is normally
 * the last thing before the bass's own sink.
 *
 * A root with a duration is a bar with a length, and the pattern is
 * played once per root regardless -- a pattern longer than the chord
 * runs into the next one, which is a choice the file made. Held roots
 * (live keys) play the pattern once on the press, and their release
 * is passed on only with `pass'.
 *
 * DETERMINISM. Nothing random; the bass is a function of the root.
 */

#include <cstdlib>
#include <cstring>

#include "thcomposer.h"

enum { P_SCALE, P_PATTERN, P_STEP, P_HOLD, P_VEL, P_OCTAVE, P_PASS,
       P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "scale",   "the key the third and fifth are counted in",
          THC_PARAM_NOTESET, 0, 0, 0, "48,50,52,53,55,57,59", NULL },
        { "pattern", "steps: r root, t third, f fifth, o up an octave, "
          "d down one, . rest, _ tie", THC_PARAM_STRING, 0, 0, 0,
          "r.r.f.r.", NULL },
        { "step",    "length of one step", THC_PARAM_FLOAT,
          0.02, 60, 0.25, NULL, "s" },
        { "hold",    "time before each note-off", THC_PARAM_FLOAT,
          0.01, 60, 0.2, NULL, "s" },
        { "vel",     "velocity", THC_PARAM_INT, 1, 127, 96, NULL, NULL },
        { "octave",  "octaves to move the root by", THC_PARAM_INT,
          -3, 3, -1, NULL, NULL },
        { "pass",    "1: the root is heard too", THC_PARAM_INT,
          0, 1, 0, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Play a bass pattern under every root that arrives.");

    return 0;
}

struct State {
    const thcParams *params;

    bool pc[12];
    int  pcCount;

    void reparse (void);
};

void
State::reparse (void)
{
    const char *s = params->get_string(params->ctx, paramIndex[P_SCALE]);

    for (int i = 0; i < 12; i++)
        pc[i] = false;

    pcCount = 0;

    while (s && *s)
    {
        const int n = atoi(s);

        if (n >= 0 && n <= 127 && !pc[n % 12])
        {
            pc[n % 12] = true;
            pcCount++;
        }

        if ((s = strchr(s, ',')))
            s++;
        else
            break;
    }

    if (pcCount == 0)                    /* see harmonize: chromatic    */
    {
        for (int i = 0; i < 12; i++)
            pc[i] = true;

        pcCount = 12;
    }
}

/* The nth scale tone above `note'; harmonize's walk. */
static int
walkUp (const State *st, int note, int n)
{
    int at = note;

    while (n > 0)
    {
        if (++at > 127)
            return -1;

        if (st->pc[at % 12])
            n--;
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
    if (index == paramIndex[P_SCALE])
        static_cast<State *>(state)->reparse();
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    if (ev->type != THC_EV_NOTE)
    {
        if (ev->type != THC_EV_NOTEOFF || (int)get(P_PASS))
            out->emit(out->ctx, ev);

        return;
    }

    if ((int)get(P_PASS))
        out->emit(out->ctx, ev);

    const char *pattern = p->get_string(p->ctx, paramIndex[P_PATTERN]);
    const double step = get(P_STEP);
    const double hold = get(P_HOLD);
    const int root = ev->u.note.note + 12 * (int)get(P_OCTAVE);

    if (pattern == NULL || step <= 0)
        return;

    /* The note a `_' lengthens: emitted only once its length is known,
       so a tie is one longer note and not a note re-struck. */
    int    pending = -1;
    double pendingAt = 0;
    int    pendingLen = 0;

    for (int i = 0; ; i++)
    {
        const char c = pattern[i];
        int note = -2;                    /* -2: not a note step         */

        switch (c)
        {
            case 'r': note = root; break;
            case 't': note = walkUp(st, root, 2); break;
            case 'f': note = walkUp(st, root, 4); break;
            case 'o': note = root + 12; break;
            case 'd': note = root - 12; break;
            case '_':
                if (pending >= 0)
                    pendingLen++;

                continue;
            case '.': note = -1; break;
            case 0:   break;
            default:  continue;           /* anything else is cosmetic   */
        }

        if (pending >= 0)
        {
            thcEvent copy = *ev;

            copy.at = pendingAt;
            copy.u.note.note = pending;
            copy.u.note.velocity = (int)get(P_VEL);
            copy.u.note.duration = hold + (pendingLen - 1) * step;

            out->emit(out->ctx, &copy);
            pending = -1;
        }

        if (c == 0)
            break;

        if (note >= 0 && note <= 127)
        {
            pending = note;
            pendingAt = ev->at + i * step;
            pendingLen = 1;
        }
    }
}
