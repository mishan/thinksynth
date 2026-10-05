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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * windowlencheck -- every node plugin, cut into windows of one sample and of
 * five hundred, must give the same output bit for bit.
 *
 * A plugin that keeps something in a local, reads buf[i - 1] without
 * remembering the last sample of the window before, or works out a
 * coefficient once a window from a control that moves, sounds different
 * at a different buffer size, which is the one thing the engine promises
 * it will not. statecheck asks this of the plugins someone wrote a graph
 * for; this asks it of all of them, from what each declares about itself,
 * so a new plugin is covered the day it is built.
 *
 * Each arg in gets something to do:
 *
 *   - a signal arg (`in', `in0', ..., or one in full-scale units that
 *     goes below zero) gets a chord of three sines;
 *   - a trigger, gate, reset or sync gets a square, so envelopes fire and
 *     release several times -- and in the second pass below is held, a
 *     trigger at 1 as a free-running envelope is given it and a reset at
 *     0;
 *   - any other arg with a declared range -- or in Hz or samples, which
 *     get 20 to 4000 -- sweeps it slowly, low to high, so a control that
 *     moves inside a window is a control that moves -- and then, in a
 *     second pass, holds still a third of the way up, since a plugin may
 *     take a faster path for a control that is one value;
 *   - an arg with named values is held at each value in turn, one render
 *     each, so every waveform and mode is visited;
 *   - the rest are held at their default, or left alone.
 *
 * What moves is wired from a node of its own, as in a graph, so a plugin
 * that picks its path by whether an arg is wired takes the one a voice
 * takes. And every arg out is compared, a signed zero equal to a zero. A
 * plugin that cannot be held to this -- one that outputs a table, or
 * plays a file -- is in a table of exceptions below with its reason, and
 * nowhere else.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmath>
#include <string>
#include <vector>

#include "think.h"

#include "NodeCatalog.h"

using std::string;
using std::vector;

static int failures = 0;

/* Long enough for several cycles of every driver below, and for a slow
   sweep to cross a range. */
static const unsigned RENDER = 6000;

/* Plugins this harness does not run, and why. Each is a decision, so each
   names its reason. */
static const struct { const char *spelling, *why; } skipped[] = {
    { "misc::print",       "writes to stdout" },
    { "impulse::blackman", "outputs a table, not a stream" },
    { "impulse::parabola", "outputs a table, not a stream" },
    { "impulse::sine",     "outputs a table, not a stream" },
    { "impulse::square",   "outputs a table, not a stream" },
    { "osc::sample",       "plays a file, and is silent with none named" },
    { "osc::stretch",      "plays a file, and is silent with none named" },
    { "osc::grain",        "plays a file, and is silent with none named" },
};

/* Plugins that design something from their controls once a window, on
   purpose, and so are held to the promise only with their controls still. */
static const struct { const char *spelling, *why; } designed[] = {
    { "filt::pianostring", "designs its string from the first sample of "
                           "each window, which is costly; a note's controls "
                           "hold still" },
    { "filt::sympathetic", "lays out its strings from the first sample of "
                           "each window; the controls pick keys" },
    { "filt::comb",        "reads its size once a window" },
};

#define FIND(table)                                                    \
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)    \
        if (spelling == table[i].spelling)                             \
            return table[i].why;                                       \
    return NULL

static const char *
skipReason (const string &spelling)
{
    FIND(skipped);
}

static const char *
designReason (const string &spelling)
{
    FIND(designed);
}

enum Drive { HOLD, SIGNAL, GATE, SWEEP };

/* A gate held still: up for a trigger, down for a reset, which held up
   would start the envelope again every sample. */
static float
held (const string &name)
{
    return (name == "reset" || name == "sync") ? 0.0f : 1.0f;
}

/* An arg's travel: the one it declares, or for a duration or a frequency
   that declares none, a span that makes oscillators sound and envelopes
   move -- even where a default of 0 would have them sit still. False for
   an arg with neither. */
static bool
travel (thPlugin *p, int a, float &lo, float &hi)
{
    if (p->argHasRange(a) && p->getArgMax(a) > p->getArgMin(a))
    {
        lo = p->getArgMin(a);
        hi = p->getArgMax(a);
        return true;
    }

    const string &units = p->getArgUnits(a);

    if (units == "Hz" || units == "samples")
    {
        lo = 20;
        hi = 4000;
        return true;
    }

    return false;
}

static Drive
driveFor (thPlugin *p, int a, bool still)
{
    const string &name = p->getArgName(a);

    /* A level from 0 up -- a sustain, a peak -- is a control, not audio. */
    if (name == "in" || (name.size() == 3 && name.compare(0, 2, "in") == 0 &&
                         isdigit((unsigned char)name[2])) ||
        (p->getArgUnits(a) == "full scale" && p->getArgMin(a) < 0))
        return SIGNAL;

    if (name == "trigger" || name == "gate" || name == "reset" ||
        name == "sync" || name == "play")
        return GATE;

    if (!p->getArgValues(a).empty())
        return HOLD;

    float lo, hi;

    if (travel(p, a, lo, hi))
        return still ? HOLD : SWEEP;

    return HOLD;
}

/* The drivers: three osc::simple and their arithmetic would be a graph,
   and a graph is what this is meant to test, so the stimulus is written
   into the args directly, a sample at a time, from a formula. */
static float
stimulus (Drive d, thPlugin *p, int a, unsigned n)
{
    const double t = (double)n / TH_DEFAULT_SAMPLES;

    switch (d)
    {
    case SIGNAL:
        return (float)(0.3 * sin(2 * M_PI * 220 * t) +
                       0.25 * sin(2 * M_PI * 331 * t + 1) +
                       0.2 * sin(2 * M_PI * 1503 * t + 2));
    case GATE:
        return fmod(t * 23, 1) < 0.6 ? 1 : 0;
    case SWEEP:
    {
        float lo = 0, hi = 0;

        travel(p, a, lo, hi);

        const double u = 0.5 - 0.5 * cos(2 * M_PI * 3 * t);

        return (float)(lo + (hi - lo) * (0.1 + 0.8 * u));
    }
    case HOLD:
        break;
    }

    return 0;
}

/* One render of `spelling' at `windowlen', every arg out appended to
   `out' in turn. `held' gives the value of a named-values arg, or -1. */
static bool
render (const string &pluginPath, const string &path, bool still,
        int heldArg, float heldValue, unsigned windowlen, vector< vector<float> > &out,
        vector<string> &outNames, string &why)
{
    thSynth synth(pluginPath, (int)windowlen, TH_DEFAULT_SAMPLES);
    thSynthTree tree("windowlencheck", &synth);
    thPlugin *p = synth.getPluginManager()->getOrLoadPlugin(path);

    if (p == NULL || p->state() == thPlugin::NOTLOADED)
    {
        why = "does not load";
        return false;
    }

    thNode *node = new thNode("n", p);
    thNode *io = new thNode("ionode", NULL);

    tree.newNode(node, true);
    tree.newNode(io, true);

    thPlugin *adder = synth.getPluginManager()->getOrLoadPlugin("math/add");
    vector<int> ins, outs;
    vector<Drive> drives;
    vector<thNode *> sources;
    float lo, hi;

    if (adder == NULL)
    {
        why = "math::add does not load, and drives everything";
        return false;
    }

    for (int a = 0; a < p->argCount(); a++)
    {
        if (p->getArgDir(a) == thPlugin::ARG_OUT)
            outs.push_back(a);
        else if (p->getArgDir(a) == thPlugin::ARG_IN)
        {
            const Drive d = driveFor(p, a, still);

            if (a == heldArg)
                node->setArg(p->getArgName(a), heldValue);
            else if (d == GATE && still)
                node->setArg(p->getArgName(a), held(p->getArgName(a)));
            else if (d == HOLD && still && p->getArgValues(a).empty() &&
                     travel(p, a, lo, hi))
                node->setArg(p->getArgName(a), lo + (hi - lo) / 3);
            else if (d == HOLD && p->argHasDefault(a))
                node->setArg(p->getArgName(a), p->getArgDefault(a));
            else if (d != HOLD)
            {
                /* From a math::add whose own input this writes, so the arg
                   is a wire, the way a graph drives it. */
                const string src = "src" + std::to_string(sources.size());
                thNode *from = new thNode(src, adder);

                tree.newNode(from, true);
                from->setArg("in1", 0.0f);
                from->setArg("in0", 0.0f);
                node->setArg(p->getArgName(a), src, "out");
                sources.push_back(from);
                ins.push_back(a);
                drives.push_back(d);
            }
        }
    }

    if (outs.empty())
    {
        why = "has no arg out";
        return false;
    }

    io->setArg("n", "n", p->getArgName(outs[0]));
    tree.setIONode("ionode");
    tree.buildArgMap();
    tree.setPointers();
    tree.buildSynthTree();

    out.assign(outs.size(), vector<float>());
    outNames.clear();
    for (size_t o = 0; o < outs.size(); o++)
        outNames.push_back(p->getArgName(outs[o]));

    for (unsigned done = 0; done < RENDER; done += windowlen)
    {
        for (size_t k = 0; k < ins.size(); k++)
        {
            float *buf = sources[k]->getArg("in0")->allocate(windowlen);

            for (unsigned i = 0; i < windowlen; i++)
                buf[i] = stimulus(drives[k], p, ins[k], done + i);
        }

        tree.markAllNodes();
        tree.process(windowlen);

        for (size_t o = 0; o < outs.size(); o++)
        {
            thArg *a = node->getArg(outNames[o]);

            for (unsigned i = 0; i < windowlen && done + i < RENDER; i++)
                out[o].push_back(i < a->len() ? (*a)[i]
                                               : (a->len() ? (*a)[0] : 0));
        }
    }

    return true;
}

/* The two renders, compared; true if they agree. */
static bool
agree (const string &pluginPath, const string &path, bool still,
       int heldArg, float heldValue, const string &label)
{
    vector< vector<float> > one, many;
    vector<string> names;
    string why;

    if (!render(pluginPath, path, still, heldArg, heldValue, 1, one, names,
                why) ||
        !render(pluginPath, path, still, heldArg, heldValue, 500, many,
                names, why))
    {
        printf("FAIL  %s: %s\n", label.c_str(), why.c_str());
        failures++;
        return false;
    }

    for (size_t o = 0; o < one.size(); o++)
        for (size_t i = 0; i < RENDER; i++)
            if (memcmp(&one[o][i], &many[o][i], sizeof(float)) != 0 &&
                !(one[o][i] == 0 && many[o][i] == 0))
            {
                printf("FAIL  %s: `%s' at sample %zu is %.9g one at a time, "
                       "%.9g five hundred\n", label.c_str(),
                       names[o].c_str(), i, one[o][i], many[o][i]);
                failures++;
                return false;
            }

    return true;
}

int
main (int argc, char **argv)
{
    string pluginPath;

    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            pluginPath = argv[++i];

    if (pluginPath.empty())
    {
        fprintf(stderr, "usage: %s -p <plugindir>\n", argv[0]);
        return 2;
    }

    if (pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    NodeCatalog cat;

    if (cat.scan(pluginPath) == 0)
    {
        fprintf(stderr, "windowlencheck: no plugins under %s\n",
                pluginPath.c_str());
        return 2;
    }

    int checked = 0;

    for (size_t c = 0; c < cat.categories().size(); c++)
    {
        const string &category = cat.categories()[c];

        if (!NodeCatalog::isNodeCategory(category))
            continue;

        const vector<NodeCatalog::Entry> &list = cat.inCategory(category);

        for (size_t e = 0; e < list.size(); e++)
        {
            const string &spelling = list[e].spelling;
            const string path = category + "/" + list[e].name;

            if (const char *why = skipReason(spelling))
            {
                printf("skip  %s: %s\n", spelling.c_str(), why);
                continue;
            }

            thSynth probe(pluginPath, 1, TH_DEFAULT_SAMPLES);
            thPlugin *p = probe.getPluginManager()->getOrLoadPlugin(path);
            const char *designs = designReason(spelling);

            if (designs)
                printf("held  %s: %s\n", spelling.c_str(), designs);

            bool good = (designs ||
                         agree(pluginPath, path, false, -1, 0, spelling)) &&
                        agree(pluginPath, path, true, -1, 0,
                              spelling + ", controls held");

            for (int a = 0; good && p != NULL && a < p->argCount(); a++)
            {
                const vector<string> &values = p->getArgValues(a);

                for (size_t v = 1; good && v < values.size(); v++)
                    good = agree(pluginPath, path, designs != NULL, a,
                                 (float)v,
                                 spelling + " " + p->getArgName(a) + " = " +
                                 values[v]);
            }

            if (good)
                printf("ok    %s\n", spelling.c_str());

            checked++;
        }
    }

    printf("\n%d plugins, %d failed\n", checked, failures);

    return failures ? 1 : 0;
}
