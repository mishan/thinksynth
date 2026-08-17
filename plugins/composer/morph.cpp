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

/* morph -- the timbre between two presets.
 *
 * A patch's declared chanargs are a vector of floats, a preset is a named
 * one, and the space between two presets is a line you can travel along.
 * This walks that line and emits where it is as THC_EV_CHANARG events, so
 * a piece can open an instrument out over four minutes and close it again
 * -- the same machinery that drives a melody, pointed at a filter.
 *
 * Every component moves at once, which is why the sink it wants is
 * `chanarg = "*"': an ordinary chanarg sink names one knob and overwrites
 * whatever the event carried, and a vector delivered that way would
 * arrive as one knob taking every component's value in turn. See §9 of
 * COMPOSITION_HANDOFF.md for why a preset is a noun at all.
 *
 * Two roles, one plugin, and they are genuinely different pieces of
 * music:
 *
 *   gen::morph    ticks along the line on its own clock -- a slow sweep
 *                 under whatever else is playing.
 *   xform::morph  a note passing through schedules a whole sweep from
 *                 that note's time, so the instrument opens as it is
 *                 played. Nothing ticks; the material is the clock.
 *
 * The second is why `retrigger' exists rather than being implied by the
 * role: a gen:: stage in the middle of a chain still receives, and a
 * sweep that silently restarted every time a note went past would be a
 * surprise. Off by default; a transformer placement turns it on.
 *
 * No randomness anywhere, so a replay is exact by construction rather
 * than by seeding.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <cairo.h>

#include "thcomposer.h"

enum { P_FROM, P_TO, P_TIME, P_STEPS, P_CURVE, P_MODE, P_RETRIGGER,
       P_COUNT };

/* mode */
enum { M_ONCE = 0, M_LOOP, M_PINGPONG };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "from",  "preset the sweep starts at", THC_PARAM_PRESET,
          0, 0, 0, "", NULL },
        { "to",    "preset the sweep arrives at", THC_PARAM_PRESET,
          0, 0, 0, "", NULL },
        { "time",  "how long the sweep takes", THC_PARAM_FLOAT,
          0.01, 3600, 30, NULL, "s" },
        { "steps", "values emitted across the sweep", THC_PARAM_INT,
          2, 512, 48, NULL, NULL },
        { "curve", "1 is linear; below 1 hurries, above 1 lingers",
          THC_PARAM_FLOAT, 0.1, 8, 1, NULL, NULL },
        { "mode",  "0 once, 1 loop, 2 back and forth", THC_PARAM_INT,
          0, 2, 0, NULL, NULL },
        { "retrigger", "a note restarts the sweep (transformer placement)",
          THC_PARAM_INT, 0, 1, 0, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR | THC_TRANSFORMER);
    info->set_desc(info->host,
        "Interpolates between two presets and plays the timbre between.");

    return 0;
}

/* ---- presets ----------------------------------------------------------- */

struct Component { std::string name; double from, to; };

/* "res=0.8,fmin=0.06" -- what the loader resolved a preset name into.
 * The plugin does not know what a preset is called or where it came from,
 * exactly as a NOTESET plugin never sees a note name. */
static void
parsePreset (const char *text, std::vector<std::pair<std::string, double> > &out)
{
    out.clear();

    if (text == NULL)
        return;

    const char *p = text;

    while (*p)
    {
        const char *eq = strchr(p, '=');

        if (eq == NULL)
            break;

        std::string name(p, (size_t)(eq - p));

        char       *end = NULL;
        const double v = strtod(eq + 1, &end);

        if (end == eq + 1)          /* nothing numeric after the '='     */
            break;

        out.push_back(std::make_pair(name, v));

        p = (*end == ',') ? end + 1 : end;

        while (*p == ',' || *p == ' ')
            p++;
    }
}

/* The two vectors, paired by name.
 *
 * Paired rather than zipped by position, because the two presets are
 * written by hand and a piece is entitled to list them in whatever order
 * reads best -- and to give one of them a component the other lacks. A
 * component only one side names holds still at the value it was given,
 * which is the reading that lets `to' be a small correction to `from'
 * rather than a full restatement of it. */
static void
pair (const std::vector<std::pair<std::string, double> > &from,
      const std::vector<std::pair<std::string, double> > &to,
      std::vector<Component> &out)
{
    out.clear();

    for (size_t i = 0; i < from.size(); i++)
    {
        Component c;

        c.name = from[i].first;
        c.from = c.to = from[i].second;

        for (size_t j = 0; j < to.size(); j++)
            if (to[j].first == c.name)
                c.to = to[j].second;

        out.push_back(c);
    }

    for (size_t j = 0; j < to.size(); j++)
    {
        bool seen = false;

        for (size_t i = 0; i < from.size(); i++)
            if (from[i].first == to[j].first)
                seen = true;

        if (seen)
            continue;

        Component c;

        c.name = to[j].first;
        c.from = c.to = to[j].second;

        out.push_back(c);
    }
}

/* ---- instance ---------------------------------------------------------- */

struct State {
    const thcParams *params;

    std::vector<Component> comps;
    std::string            fromText, toText;   /* what comps was built from */

    double phase;      /* 0..1 along the line                             */
    int    direction;  /* +1 or -1, for ping-pong                         */
    bool   started;

    /* For the draw only. */
    double lastPhase;
};

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->phase = 0;
    st->direction = 1;
    st->started = false;
    st->lastPhase = 0;

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}

static double
getp (State *st, int i)
{
    return st->params->get(st->params->ctx, paramIndex[i]);
}

/* Rebuilt only when the text changed, because a param read costs nothing
 * and a re-parse per tick would not. The comparison is on the resolved
 * strings rather than on a dirty flag, so this is right whether the value
 * changed through composer_param_changed, through a panel edit, or not at
 * all. */
static void
refresh (State *st)
{
    const char *f = st->params->get_string(st->params->ctx, paramIndex[P_FROM]);
    const char *t = st->params->get_string(st->params->ctx, paramIndex[P_TO]);

    const std::string fs = f ? f : "";
    const std::string ts = t ? t : "";

    if (fs == st->fromText && ts == st->toText && !st->comps.empty())
        return;

    st->fromText = fs;
    st->toText = ts;

    std::vector<std::pair<std::string, double> > from, to;

    parsePreset(fs.c_str(), from);
    parsePreset(ts.c_str(), to);

    pair(from, to, st->comps);
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int)
{
    refresh(static_cast<State *>(state));
}

/* The shape of the travel. `curve' is an exponent: 1 is a straight line,
 * below 1 leaves quickly and arrives slowly, above 1 the reverse. One
 * knob rather than a menu of named easings, because it is continuous and
 * therefore something a @knob can drive mid-piece. */
static double
shaped (double phase, double curve)
{
    if (phase <= 0)
        return 0;
    if (phase >= 1)
        return 1;

    return pow(phase, curve);
}

static void
emitAt (State *st, thcEventSink *out, double when, double phase,
        double curve, int channel)
{
    const double k = shaped(phase, curve);

    for (size_t i = 0; i < st->comps.size(); i++)
    {
        const Component &c = st->comps[i];

        thcEvent ev = {};

        ev.type = THC_EV_CHANARG;
        ev.at = when;
        ev.channel = channel;            /* the sink routes             */
        ev.u.chanarg.name = c.name.c_str();
        ev.u.chanarg.value = (float)(c.from + (c.to - c.from) * k);

        out->emit(out->ctx, &ev);
    }
}

/* ---- generator: the sweep on its own clock ----------------------------- */

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);

    refresh(st);

    const double time = getp(st, P_TIME);
    const int    steps = (int)getp(st, P_STEPS);
    const double curve = getp(st, P_CURVE);
    const int    mode = (int)getp(st, P_MODE);

    const double dt = time / (steps > 1 ? (steps - 1) : 1);

    if (!st->started)
    {
        /* The first tick puts the instrument where the sweep starts,
           before anything has moved. Without it the patch keeps whatever
           its own .dsp declared until the second tick, and the piece
           opens on a timbre it never asked for. */
        st->started = true;
        st->phase = 0;
        st->direction = 1;

        emitAt(st, out, t->now, 0, curve, 0);
        st->lastPhase = 0;

        return t->now + dt;
    }

    if (!t->running)
        return t->now + dt;         /* paused: hold still, keep the draw */

    const double advance = 1.0 / (steps > 1 ? (steps - 1) : 1);

    st->phase += advance * st->direction;

    if (st->phase >= 1.0)
    {
        if (mode == M_PINGPONG)
        {
            st->phase = 1.0;
            st->direction = -1;
        }
        else if (mode == M_LOOP)
            st->phase = 0.0;
        else
        {
            /* Arrived. Emit the endpoint exactly -- a sweep that stopped
               at 0.98 of the way there would leave the instrument almost
               at the preset the file named, forever. */
            st->phase = 1.0;
            emitAt(st, out, t->now, 1.0, curve, 0);
            st->lastPhase = 1.0;

            return THC_NEVER;
        }
    }
    else if (st->phase <= 0.0 && st->direction < 0)
    {
        st->phase = 0.0;
        st->direction = 1;
    }

    emitAt(st, out, t->now, st->phase, curve, 0);
    st->lastPhase = st->phase;

    return t->now + dt;
}

/* ---- transformer: the material is the clock ---------------------------- */

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);

    /* Everything continues downstream, always. A morph is about timbre;
       swallowing the notes that triggered it would make it a gate as
       well, and a stage that does two things is two stages. */
    out->emit(out->ctx, ev);

    if (ev->type != THC_EV_NOTE || getp(st, P_RETRIGGER) < 0.5)
        return;

    refresh(st);

    const double time = getp(st, P_TIME);
    const int    steps = (int)getp(st, P_STEPS);
    const double curve = getp(st, P_CURVE);
    const double dt = time / (steps > 1 ? (steps - 1) : 1);

    /* The whole sweep, scheduled from the note's own time, the way
       lsystem emits a derived phrase as one block. The scheduler queues
       the future and the piano roll draws it, so the sweep is visible
       coming before it is audible arriving. */
    for (int i = 0; i < steps; i++)
    {
        const double phase = (steps > 1) ? (double)i / (steps - 1) : 1.0;

        emitAt(st, out, ev->at + i * dt, phase, curve, ev->channel);
    }

    st->lastPhase = 1.0;
}

/* ---- draw -------------------------------------------------------------- */

/* One lane per component, drawn from its `from' to its `to' with a marker
 * where the sweep has got to. The plugin's entire state made visible,
 * which is the rule the other draws follow: the point is to be able to
 * see that a morph is moving, and which way, without reading numbers. */
extern "C" THINK_PLUGIN_API void
composer_draw (void *state, cairo_t *cr, double w, double h)
{
    State *st = static_cast<State *>(state);

    refresh(st);

    const size_t n = st->comps.size();

    if (n == 0 || w <= 0 || h <= 0)
        return;

    const double pad = 4;
    const double lane = (h - 2 * pad) / (double)n;
    const double x0 = pad + 44;                 /* room for the label   */
    const double x1 = w - pad;

    if (x1 <= x0 || lane <= 2)
        return;

    cairo_set_line_width(cr, 1.0);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, lane > 14 ? 10 : 8);

    const double k = shaped(st->lastPhase, getp(st, P_CURVE));

    for (size_t i = 0; i < n; i++)
    {
        const Component &c = st->comps[i];
        const double y = pad + lane * (i + 0.5);

        cairo_set_source_rgba(cr, 1, 1, 1, 0.45);
        cairo_move_to(cr, pad, y + 3);
        cairo_show_text(cr, c.name.c_str());

        /* The track. */
        cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
        cairo_move_to(cr, x0, y);
        cairo_line_to(cr, x1, y);
        cairo_stroke(cr);

        /* Travelled so far, so the lane fills as the sweep proceeds. */
        const double x = x0 + (x1 - x0) * k;

        cairo_set_source_rgba(cr, 0.45, 0.75, 1.0, 0.75);
        cairo_move_to(cr, x0, y);
        cairo_line_to(cr, x, y);
        cairo_stroke(cr);

        /* Where the value is now, and -- faintly -- where it is headed. */
        cairo_set_source_rgba(cr, 1, 1, 1, 0.20);
        cairo_arc(cr, x1, y, 2.0, 0, 2 * M_PI);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 0.6, 0.85, 1.0, 0.95);
        cairo_arc(cr, x, y, 3.0, 0, 2 * M_PI);
        cairo_fill(cr);
    }
}
