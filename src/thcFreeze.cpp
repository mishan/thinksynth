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

#include "config.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "thcFreeze.h"
#include "thcGenEdit.h"
#include "thcGenFile.h"
#include "thcScheduler.h"

static const int MAX_STEPS = 64;
static const int MAX_ROWS = 32;

/* A number with its unit, as a duration is written. */
static std::string
duration (double v, const char *unit)
{
    std::string num;

    if (!thcGenEdit::format(v, num))
        num = "0.25";

    return num + " " + unit;
}

bool
thcFreeze::fromChain (const thcScheduler &sched, size_t chain, int bars,
                      Params &out, std::string &why)
{
    const thcChain *c = const_cast<thcScheduler &>(sched).chain(chain);

    if (c == NULL)
    {
        why = "no such chain";
        return false;
    }

    /* The window: where it starts, how long a step is, how many. */
    double t0, t1, step, stepText;
    int steps;
    const char *unit;
    const double now = sched.now();

    if (sched.usesBeats() && sched.tempo() > 0)
    {
        const double spb = 60.0 / sched.tempo();
        const double meter = sched.meter();
        const double barLine = std::floor(sched.beat() / meter + 1e-6) *
                               meter;

        if (bars < 1)
            bars = 1;

        while (bars > 1 && bars * meter * 4 > MAX_STEPS)
            bars--;

        stepText = 0.25;
        step = stepText * spb;
        steps = std::min(MAX_STEPS, (int)std::lround(bars * meter * 4));
        t1 = now - (sched.beat() - barLine) * spb;
        t0 = t1 - steps * step;
        unit = "beats";
    }
    else
    {
        stepText = step = 0.125;
        steps = 32;
        t1 = now;
        t0 = t1 - steps * step;
        unit = "s";
    }

    if (t0 < -step / 2)
    {
        why = "it has not played for long enough yet";
        return false;
    }

    /* What falls in it, on its nearest step. */
    struct Hit { int step, len, note, vel; };
    std::vector<Hit> hits;

    for (const thcPlayed &p : c->played)
    {
        const int s = (int)std::lround((p.at - t0) / step);

        if (p.at < t0 - step / 2 || s < 0 || s >= steps)
            continue;

        const int len = std::max(1, (int)std::lround(p.duration / step));

        hits.push_back({ s, len, p.note, p.velocity });
    }

    if (hits.empty())
    {
        why = "it played nothing in the last " +
              (sched.usesBeats() ? std::to_string(bars) + " bars"
                                 : std::string("four seconds"));
        return false;
    }

    /* The ladder: every pitch it used, or the most used where there are
       more than a grid has rows. */
    std::map<int, int> uses;

    for (const Hit &h : hits)
        uses[h.note]++;

    std::vector<int> ladder;

    for (const auto &u : uses)
        ladder.push_back(u.first);

    if (ladder.size() > (size_t)MAX_ROWS)
    {
        std::stable_sort(ladder.begin(), ladder.end(),
                         [&](int a, int b) { return uses[a] > uses[b]; });
        ladder.resize(MAX_ROWS);
        std::sort(ladder.begin(), ladder.end());
    }

    /* Loud, and the rest: a hit well over the middle velocity is an
       accent, at the grid's `accent' over its `vel'. */
    std::vector<int> vels;

    for (const Hit &h : hits)
        vels.push_back(h.vel);

    std::sort(vels.begin(), vels.end());

    const int vel = vels[vels.size() / 2];
    int loud = 0, louder = 0;

    for (const Hit &h : hits)
        if (h.vel >= vel + 12)
        {
            loud += h.vel - vel;
            louder++;
        }

    const int accent = louder > 0 ? std::min(127, loud / louder) : 24;

    /* The cells, top row first, which is how the grid reads them. */
    const int rows = (int)ladder.size();
    std::vector<std::string> cells(rows, std::string(steps, '.'));

    for (const Hit &h : hits)
    {
        const auto at = std::find(ladder.begin(), ladder.end(), h.note);

        if (at == ladder.end())
            continue;

        std::string &row = cells[rows - 1 - (at - ladder.begin())];

        row[h.step] = h.vel >= vel + 12 ? 'X' : 'x';

        int k = 1;

        for (; k < h.len && h.step + k < steps; k++)
            if (row[h.step + k] == '.' || row[h.step + k] == '-')
                row[h.step + k] = '-';
            else
                break;

        /* An earlier note's tie that ran on past this one is over: the
           cells after this note's own are not this note's. */
        for (int j = h.step + k; j < steps && row[j] == '-'; j++)
            row[j] = '.';
    }

    std::string text, notes;

    for (int r = 0; r < rows; r++)
        text += (r ? "/" : "") + cells[r];

    for (int n : ladder)
        notes += (notes.empty() ? "" : " ") + thcGenLoader::noteName(n);

    out.clear();
    out.push_back({ "cells", "\"" + text + "\"" });
    out.push_back({ "steps", std::to_string(steps) });
    out.push_back({ "rows", std::to_string(rows) });
    out.push_back({ "notes", "\"" + notes + "\"" });
    out.push_back({ "period", duration(stepText, unit) });
    out.push_back({ "hold", duration(stepText * 0.9, unit) });
    out.push_back({ "vel", std::to_string(vel) });
    out.push_back({ "accent", std::to_string(accent) });
    out.push_back({ "listen", "0" });
    out.push_back({ "pass", "1" });

    return true;
}

thcGenEdit::Result
thcFreeze::write (const std::string &path, const thcGenEdit::Chain &from,
                  const thcScheduler &sched, const std::string &name,
                  const Params &params, std::string &why)
{
    const thcGenEdit::Sink *sink = NULL;

    for (const thcGenEdit::Sink &s : from.sinks)
        if (s.chanarg.empty())
        {
            sink = &s;
            break;
        }

    if (sink == NULL)
    {
        why = from.name + " plays no notes to freeze";
        return thcGenEdit::REFUSED;
    }

    thcGenEdit::Result r =
        thcGenEdit::addChain(path, name, sink->channel, sink->instrument,
                             "frozen", "gen", "grid", params, why);

    if (r == thcGenEdit::OK && !from.startText.empty())
        r = thcGenEdit::setChainStart(path, name, from.startText, why);

    const std::vector<thcSection> &secs = sched.sections();

    for (size_t si = 0; r == thcGenEdit::OK && si < secs.size(); si++)
    {
        const double level = sched.sectionLevelOf(si, from.name);

        if (level != 1)
            r = thcGenEdit::setSectionLevel(path, secs[si].name, name, level,
                                            why);
    }

    return r;
}

std::string
thcFreeze::frozenName (const std::string &name,
                       const std::vector<std::string> &taken)
{
    for (int n = 1; ; n++)
    {
        const std::string candidate =
            name + "_frozen" + (n > 1 ? std::to_string(n) : "");

        if (std::find(taken.begin(), taken.end(), candidate) == taken.end())
            return candidate;
    }
}
