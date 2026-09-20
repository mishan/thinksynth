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

/* lsystem -- a Lindenmayer system, played.
 *
 * An axiom is rewritten `depth' times by the rules, and the result is
 * read as turtle instructions over a pitch ladder:
 *
 *   F        sound the current degree, then step forward in time
 *   r        step forward in time silently (a rest)
 *   + / -    move a degree up / down the pitch ladder; past its ends
 *            the ladder wraps an octave, so contour is unbounded
 *   [ / ]    push / pop (time, degree) -- a bracket is a BRANCH: what
 *            follows `]' resumes where `[' was, so bracketed material
 *            runs in parallel with what comes after. Polyphony falls
 *            out of the grammar.
 *   _        a tie: step forward in time with the last note still
 *            sounding, so `F__' is one note three steps long. After a
 *            rest, or a bracket, there is no last note and it is a
 *            rest.
 *   > / <    accent the next note: `accent' more, or less, velocity
 *            for each mark, so `>>F' is a note hit hard.
 *   others   structure only (the classic X), never heard
 *
 * WRITING A TUNE. At depth 0 nothing is rewritten and the axiom is the
 * phrase, so a melody is these marks over a ladder, one step at a
 * time, with rests -- which is what the game pieces do (overworld.gen
 * and the others). The tie is what makes that a fair way to write: a
 * plugin whose notes were all one length made every tune three stages,
 * one per note length, the others' notes rubbed out into rests.
 *
 * The whole derived phrase is emitted as one scheduled block, arbitrary
 * seconds into the future, and the tick sleeps until the phrase ends --
 * this is the first composer that *plans*, which is exactly what the
 * piano roll's ghosted right half was built to show. There is no
 * randomness anywhere in this plugin: the same file replays the same
 * piece without touching its seed.
 *
 * The derivation is capped (MAX_DERIVED / MAX_EVENTS) because rules
 * like F=FF are exponential and an editor slider goes to 8; hitting the
 * cap truncates the tail rather than failing, which for music is the
 * right kind of wrong.
 */

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef THC_NO_DRAW
#include <cairo.h>
#endif

#include "thcomposer.h"

/* M_PI is not in C++ and UCRT hides it; thMath.h is the one place that
 * knows that. See its header for why there are two answers and not one. */
#include "thMath.h"

enum { P_AXIOM, P_RULES, P_DEPTH, P_NOTES, P_STEP, P_HOLD, P_VEL,
       P_ACCENT, P_COUNT };

static int paramIndex[P_COUNT];

#define MAX_DERIVED 4096
#define MAX_EVENTS  512

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "axiom", "the starting string", THC_PARAM_STRING,
          0, 0, 0, "X", NULL },
        { "rules", "rewrites, comma-separated: X=F[+X]F[-X]",
          THC_PARAM_STRING, 0, 0, 0, "X=F[+X]F[-X]", NULL },
        { "depth", "how many times the rules rewrite", THC_PARAM_INT,
          0, 8, 4, NULL, NULL },
        { "notes", "the pitch ladder +/- climbs", THC_PARAM_NOTESET,
          0, 0, 0, "48,50,52,55,57", NULL },
        { "step",  "length of one turtle step", THC_PARAM_FLOAT,
          0.02, 60, 0.25, NULL, "s" },
        { "hold",  "time before note-off", THC_PARAM_FLOAT,
          0.01, 60, 0.3, NULL, "s" },
        { "vel",   "velocity", THC_PARAM_INT, 1, 127, 80, NULL, NULL },
        { "accent", "velocity each > adds and each < takes away",
          THC_PARAM_INT, 0, 64, 12, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR | THC_EMITS_AHEAD);
    info->set_desc(info->host,
        "An L-system: rewrite an axiom, walk the result as melody.");

    return 0;
}

struct State {
    const thcParams *params;

    int  pool[128];
    int  poolLen;

    std::string derived;

    /* The interpreted phrase: (step offset, MIDI note, steps long, net
       accent marks). */
    struct Ev { int at; int midi; int len; int accent; };
    std::vector<Ev> phrase;
    int phraseSteps;

    void reparseNotes (void);
    void derive (void);
    void interpret (void);
    int  degreeToMidi (int degree) const;
};

void
State::reparseNotes (void)
{
    const char *s = params->get_string(params->ctx, paramIndex[P_NOTES]);

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

/* Degree 0 is pool[0]; past either end the ladder wraps an octave up or
 * down, so `+++++' on a five-note scale lands an octave and a bit out
 * rather than pinning to the top. */
int
State::degreeToMidi (int degree) const
{
    if (poolLen == 0)
        return -1;

    int idx = degree % poolLen;
    int oct = degree / poolLen;

    if (idx < 0)
    {
        idx += poolLen;
        oct -= 1;
    }

    int midi = pool[idx] + 12 * oct;

    return midi < 0 || midi > 127 ? -1 : midi;
}

void
State::derive (void)
{
    /* "X=F[+X]F[-X], F=FF" -> a table keyed by the single character
       left of each '='. Spaces are cosmetic; commas separate rules. */
    const char *rulesText =
        params->get_string(params->ctx, paramIndex[P_RULES]);

    std::string rhs[256];
    bool has[256] = { false };

    const char *s = rulesText;

    while (s && *s)
    {
        while (*s == ' ' || *s == ',')
            s++;

        if (*s == 0)
            break;

        char key = *s++;

        while (*s == ' ')
            s++;

        if (*s != '=')
        {
            /* Not a rule; skip to the next comma rather than guessing. */
            while (*s && *s != ',')
                s++;
            continue;
        }

        s++;

        std::string body;

        while (*s && *s != ',')
        {
            if (*s != ' ')
                body += *s;
            s++;
        }

        rhs[(unsigned char)key] = body;
        has[(unsigned char)key] = true;
    }

    /* get_string is nullable everywhere else it is read; an unset
       axiom is an empty derivation, not undefined behavior. */
    const char *axiom =
        params->get_string(params->ctx, paramIndex[P_AXIOM]);

    derived = axiom != NULL ? axiom : "";

    int depth = (int)params->get(params->ctx, paramIndex[P_DEPTH]);

    for (int d = 0; d < depth; d++)
    {
        std::string next;

        next.reserve(derived.size() * 2);

        for (size_t i = 0; i < derived.size(); i++)
        {
            unsigned char c = derived[i];

            if (has[c])
                next += rhs[c];
            else
                next += (char)c;

            if (next.size() >= MAX_DERIVED)
                break;                   /* truncated, not failed        */
        }

        derived.swap(next);

        if (derived.size() >= MAX_DERIVED)
            break;
    }
}

void
State::interpret (void)
{
    phrase.clear();
    phraseSteps = 0;

    struct Turtle { int at, degree; };
    std::vector<Turtle> stack;
    Turtle t = { 0, 0 };

    /* The note a `_' would lengthen: the last F, until a rest or a
       bracket comes between. And the accent marks waiting for the next
       F. */
    int last = -1;
    int accent = 0;

    for (size_t i = 0; i < derived.size(); i++)
    {
        switch (derived[i])
        {
            case 'F':
            {
                int midi = degreeToMidi(t.degree);

                last = -1;

                if (midi >= 0 && phrase.size() < MAX_EVENTS)
                {
                    phrase.push_back({ t.at, midi, 1, accent });
                    last = (int)phrase.size() - 1;
                }

                accent = 0;
                t.at++;
                break;
            }
            case 'r': t.at++; last = -1; break;
            case '_':
                if (last >= 0)
                    phrase[last].len++;

                t.at++;
                break;
            case '>': accent++; break;
            case '<': accent--; break;
            case '+': t.degree++; break;
            case '-': t.degree--; break;
            case '[': stack.push_back(t); last = -1; break;
            case ']':
                if (!stack.empty())
                {
                    t = stack.back();
                    stack.pop_back();
                }

                last = -1;
                break;
            default:  break;             /* structure, never heard      */
        }

        /* `at' has already stepped past the note it sounded, so it is
           the phrase's length, not its last index: a phrase of four
           steps is four steps long and repeats on the fifth, which is
           what lets one written in beats stay in the bar with a ring
           beside it. */
        if (t.at > phraseSteps)
            phraseSteps = t.at;
    }
}

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->reparseNotes();
    st->derive();
    st->interpret();

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int index)
{
    State *st = static_cast<State *>(state);

    if (index == paramIndex[P_NOTES])
        st->reparseNotes();

    /* The derivation depends on all three; the interpretation also on
       the pool. Re-deriving on any of them is cheap and always right. */
    if (index == paramIndex[P_AXIOM] || index == paramIndex[P_RULES] ||
        index == paramIndex[P_DEPTH] || index == paramIndex[P_NOTES])
    {
        st->derive();
        st->interpret();
    }
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    double step = get(P_STEP);

    if (step <= 0)
        step = 0.25;

    if (t->running && !st->phrase.empty())
    {
        for (size_t i = 0; i < st->phrase.size(); i++)
        {
            thcEvent ev = {};

            /* A tied note holds for its extra steps on top of `hold', so
               the gap `hold' leaves before the next step is the same gap
               whatever the length. */
            int vel = (int)get(P_VEL) +
                      st->phrase[i].accent * (int)get(P_ACCENT);

            ev.type = THC_EV_NOTE;
            ev.u.note.level = 1;
            ev.at = t->now + st->phrase[i].at * step;
            ev.channel = 0;              /* the sink routes             */
            ev.u.note.note = st->phrase[i].midi;
            ev.u.note.velocity = vel < 1 ? 1 : vel > 127 ? 127 : vel;
            ev.u.note.duration = get(P_HOLD) +
                                 (st->phrase[i].len - 1) * step;

            out->emit(out->ctx, &ev);
        }

    }

    double span = st->phraseSteps > 0 ? st->phraseSteps * step : step;

    return t->now + span;
}

/* The phrase as contour: one dot per note, branches visible as several
 * dots in a column. (No playhead sweep: the phrase is emitted whole,
 * and progress through it is the piano roll's picture, not this
 * box's.) */
#ifndef THC_NO_DRAW
extern "C" THINK_PLUGIN_API void
composer_draw (void *state, cairo_t *cr, double w, double h)
{
    State *st = static_cast<State *>(state);

    if (st->phrase.empty() || st->phraseSteps < 1)
        return;

    int lo = 128, hi = -1;

    for (size_t i = 0; i < st->phrase.size(); i++)
    {
        if (st->phrase[i].midi < lo) lo = st->phrase[i].midi;
        if (st->phrase[i].midi > hi) hi = st->phrase[i].midi;
    }

    if (hi < lo)
        return;

    if (hi - lo < 4)
        hi = lo + 4;

    double mx = 6, my = 6;
    double iw = w - 2 * mx, ih = h - 2 * my;

    cairo_set_source_rgba(cr, 1.0, 0.85, 0.3, 0.85);

    for (size_t i = 0; i < st->phrase.size(); i++)
    {
        double x = mx + iw * st->phrase[i].at / st->phraseSteps;
        double y = my + ih * (1.0 - (double)(st->phrase[i].midi - lo) /
                                    (hi - lo));

        cairo_arc(cr, x, y, 1.8, 0, 2 * M_PI);
        cairo_fill(cr);
    }
}
#endif

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}
