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

/* hostcheck -- the two hosts have to agree.
 *
 * UNIFICATION.md names this hazard before phase 3 is written: "the second
 * interpreter drifts. Control-rate and audio-rate hosts running the same
 * plugin must agree on semantics per sample. A `hostcheck'-style gate --
 * same plugin, same input, both hosts, diff -- belongs in ctest from
 * phase 3 day one." This is that, on day one.
 *
 * What is actually being compared, since both hosts are the same
 * thSynthTree walking the same .so: *the window length*. The control-rate
 * host runs one sample at a time, fifty times a second; the audio thread
 * runs a thousand at a time, forty-three times a second. Everything the
 * composer world can now put on a chain depends on those two producing
 * the same numbers, and nothing before this asked whether they do.
 *
 * They are not obliged to by anything except each plugin's own care. A
 * plugin that kept its phase in a local, or wrote its output buffer
 * before reading its input one, or looked at buf[i - 1] without
 * remembering buf[len - 1] between windows, would work perfectly at
 * a thousand samples and be wrong at one -- and the failure would show
 * up as a composer's LFO drifting slowly out of tune with itself, which
 * is about the least debuggable thing in this tree.
 *
 * So: run a graph N windows of 1, run the same graph 1 window of N, and
 * diff. Same tree shape, same args, same rate, same plugin binaries; the
 * only difference is how the samples are cut up. Anything that is not
 * identical is a plugin that cannot be trusted at control rate, and the
 * failure names it.
 *
 * Headless: no display, no audio device, no main loop.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include <cmath>
#include <string>
#include <vector>

#include "think.h"

static int failures = 0;

static void
fail (const std::string &what)
{
    fprintf(stderr, "hostcheck: FAIL: %s\n", what.c_str());
    failures++;
}

/* One node, its args, and the wires into it. Enough to describe the
 * graphs below without a .dsp file, which is the point: these are the
 * shapes a *chain* can now hold, and they never go near the parser. */
struct Wire   { const char *arg, *fromNode, *fromArg; };
struct Value  { const char *arg; float value; };

struct NodeSpec
{
    const char *name;
    const char *spelling;              /* "osc/simple"                   */
    std::vector<Value> values;
    std::vector<Wire>  wires;
};

/* Builds the graph and returns the io node's view of `watch'->`watchArg'
 * over `samples' samples, cut into windows of `windowlen'.
 *
 * A fresh synth and a fresh tree per call, so the two runs cannot share
 * a plugin's state through anything but the plugin's own globals -- and
 * a plugin with per-process state instead of per-node state is exactly
 * one of the things this is looking for. */
static bool
buildGraph (thSynth &synth, thSynthTree &tree,
            const std::vector<NodeSpec> &spec, std::string &why)
{
    for (size_t i = 0; i < spec.size(); i++)
    {
        thPlugin *p =
            synth.getPluginManager()->getOrLoadPlugin(spec[i].spelling);

        if (p == NULL || p->state() == thPlugin::NOTLOADED)
        {
            why = std::string("could not load ") + spec[i].spelling;
            return false;
        }

        tree.newNode(new thNode(spec[i].name, p), true);
    }

    for (size_t i = 0; i < spec.size(); i++)
    {
        thNode *n = tree.findNode(spec[i].name);

        for (size_t v = 0; v < spec[i].values.size(); v++)
            n->setArg(spec[i].values[v].arg, spec[i].values[v].value);

        for (size_t w = 0; w < spec[i].wires.size(); w++)
            n->setArg(spec[i].wires[w].arg, spec[i].wires[w].fromNode,
                      spec[i].wires[w].fromArg);
    }

    /* The same synthetic io node thcNodeHost invents, for the same
       reason: process() fires what it can reach from there. */
    thNode *io = new thNode("ionode", NULL);

    tree.newNode(io, true);

    for (size_t i = 0; i < spec.size(); i++)
    {
        thPlugin *p =
            synth.getPluginManager()->getOrLoadPlugin(spec[i].spelling);
        std::string outArg;

        for (int a = 0; p != NULL && a < p->argCount(); a++)
            if (p->getArgDir(a) == thPlugin::ARG_OUT)
            {
                outArg = p->getArgName(a);
                break;
            }

        if (!outArg.empty())
            io->setArg(spec[i].name, spec[i].name, outArg);
    }

    tree.setIONode("ionode");
    tree.buildArgMap();
    tree.setPointers();
    tree.buildSynthTree();

    return true;
}

/* `samples' samples of watch->watchArg, cut into windows of
 * `windowlen'. setActiveNodes here rather than marking everything,
 * because this half is imitating the *audio* thread and that is what it
 * does; the graphs it is used on all have an ACTIVE leaf. */
static bool
render (const std::string &pluginPath, long rate,
        const std::vector<NodeSpec> &spec,
        const std::string &watch, const std::string &watchArg,
        unsigned windowlen, unsigned samples, std::vector<float> &out,
        std::string &why)
{
    thSynth synth(pluginPath, (int)windowlen, (int)rate);
    thSynthTree tree("hostcheck", &synth);

    if (!buildGraph(synth, tree, spec, why))
        return false;

    thNode *w = tree.findNode(watch);

    if (w == NULL)
    {
        why = "no node called " + watch;
        return false;
    }

    thArg *arg = w->getArg(watchArg);

    if (arg == NULL)
    {
        why = watch + " has no arg called " + watchArg;
        return false;
    }

    out.clear();
    out.reserve(samples);

    for (unsigned done = 0; done < samples; done += windowlen)
    {
        tree.setActiveNodes();
        tree.process(windowlen);

        for (unsigned i = 0; i < windowlen && done + i < samples; i++)
            out.push_back((*arg)[i]);
    }

    return true;
}

/* render(), with one input stepped by hand between windows.
 *
 * A graph of nothing but arithmetic has no ACTIVE plugin to drive it, so
 * it produces a constant unless its input changes -- which is precisely
 * the situation a knob binding on a node arg creates, and precisely
 * where the walk that skips inactive nodes goes wrong. */
static bool
renderDriven (const std::string &pluginPath, long rate,
              const std::vector<NodeSpec> &spec,
              const std::string &watch, const std::string &watchArg,
              const std::string &driveNode, const std::string &driveArg,
              unsigned samples, std::vector<float> &out, std::string &why)
{
    thSynth synth(pluginPath, 1, (int)rate);
    thSynthTree tree("hostcheck", &synth);

    if (!buildGraph(synth, tree, spec, why))
        return false;

    thNode *w = tree.findNode(watch);
    thNode *d = tree.findNode(driveNode);

    if (w == NULL || d == NULL)
    {
        why = "no such node in the driven graph";
        return false;
    }

    thArg *arg = w->getArg(watchArg);
    thArg *in  = d->getArg(driveArg);

    if (arg == NULL || in == NULL)
    {
        why = "no such arg in the driven graph";
        return false;
    }

    out.clear();

    for (unsigned i = 0; i < samples; i++)
    {
        in->setValue((float)i);

        /* What thcNodeHost does: everything runs every window. */
        const thSynthTree::NodeMap &nodes = tree.nodes();

        for (thSynthTree::NodeMap::const_iterator n = nodes.begin();
             n != nodes.end(); ++n)
            if (n->second != NULL)
                n->second->setRecalc(true);

        tree.process(1);
        out.push_back((*arg)[0]);
    }

    return true;
}

/* The same, for a graph with nothing ACTIVE in it.
 *
 * A passive graph produces nothing unless something changes underneath
 * it, so `driveNode.driveArg' is stepped between windows -- which is
 * also exactly what a knob binding does to a node arg. Only the
 * one-sample cut is meaningful here (the coarse cut would see a single
 * value for the whole window), so what is checked is that the graph
 * *keeps running* and follows its input, which is the failure this
 * exists for: it fired once and then froze forever. */
static void
comparePassive (const std::string &pluginPath, const char *what,
                const std::vector<NodeSpec> &spec,
                const std::string &watch, const std::string &watchArg,
                const std::string &driveNode, const std::string &driveArg)
{
    std::vector<float> out;
    std::string why;

    if (!renderDriven(pluginPath, 50, spec, watch, watchArg,
                      driveNode, driveArg, 8, out, why))
    {
        fail(std::string(what) + ": " + why);
        return;
    }

    bool moved = false;

    for (size_t i = 1; i < out.size(); i++)
        if (out[i] != out[0])
            moved = true;

    if (!moved)
    {
        fail(std::string(what) + ": the graph stopped following its input "
             "-- a passive graph that fires once and freezes");
        return;
    }

    printf("ok    %s\n", what);
}

/* Both cuts of the same graph, diffed.
 *
 * Exact equality, not a tolerance. The two runs execute the same
 * arithmetic in the same order on the same values; a difference is a
 * plugin doing something structurally different at one window length,
 * not a rounding artefact, and a tolerance here would hide precisely
 * what this exists to catch. */
static void
compare (const std::string &pluginPath, const char *what,
         const std::vector<NodeSpec> &spec,
         const std::string &watch, const std::string &watchArg)
{
    const long     rate = 50;
    const unsigned n = 200;

    std::vector<float> fine, coarse;
    std::string why;

    if (!render(pluginPath, rate, spec, watch, watchArg, 1, n, fine, why))
    {
        fail(std::string(what) + ": " + why);
        return;
    }

    if (!render(pluginPath, rate, spec, watch, watchArg, n, n, coarse, why))
    {
        fail(std::string(what) + ": " + why);
        return;
    }

    if (fine.size() != coarse.size())
    {
        fail(std::string(what) + ": the two hosts produced different "
             "numbers of samples");
        return;
    }

    /* Two silences agree perfectly, and prove nothing.
     *
     * A graph that was never wired up, a plugin that refused to load,
     * an arg name that is a typo -- every one of those produces a flat
     * line out of both hosts and a passing comparison. So the signal has
     * to have gone somewhere before its agreement is worth anything. */
    bool moved = false;

    for (size_t i = 1; i < fine.size() && !moved; i++)
        if (fine[i] != fine[0])
            moved = true;

    if (!moved)
    {
        fail(std::string(what) + ": the watched signal never moved, so "
             "the hosts agreeing about it means nothing");
        return;
    }

    for (size_t i = 0; i < fine.size(); i++)
        if (fine[i] != coarse[i])
        {
            char buf[192];

            snprintf(buf, sizeof(buf),
                     "%s: the hosts disagree at sample %zu -- "
                     "one-at-a-time %.9g, all-at-once %.9g",
                     what, i, (double)fine[i], (double)coarse[i]);
            fail(buf);
            return;
        }

    printf("ok    %s\n", what);
}

int
main (int argc, char *argv[])
{
    std::string pluginPath;

    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            pluginPath = argv[++i];

    if (pluginPath.empty())
    {
        fprintf(stderr, "usage: hostcheck -p <plugindir>\n");
        return 2;
    }

    /* An oscillator on its own: the case every LFO in a chain is, and
       the one where a phase kept in the wrong place would show. */
    {
        std::vector<NodeSpec> spec(1);

        spec[0].name = "lfo";
        spec[0].spelling = "osc/simple";
        spec[0].values = { { "freq", 0.5f }, { "waveform", 0 },
                           { "amp", 1.0f } };

        compare(pluginPath, "osc::simple alone", spec, "lfo", "out");
    }

    /* Every waveform it offers, since they are six different loops
       sharing one phase and only one of them was exercised above. */
    for (int wave = 0; wave <= 5; wave++)
    {
        std::vector<NodeSpec> spec(1);
        char label[64];

        spec[0].name = "lfo";
        spec[0].spelling = "osc/simple";
        spec[0].values = { { "freq", 0.37f }, { "waveform", (float)wave },
                           { "amp", 1.0f } };

        snprintf(label, sizeof(label), "osc::simple waveform %d", wave);
        compare(pluginPath, label, spec, "lfo", "out");
    }

    /* A wired graph: the arithmetic nodes reading an oscillator, which
       is the shape a piece uses to move a signal into a param's range.
       Here the question is whether a node reads its input *this* window
       or the last one -- an off-by-one that a thousand-sample window
       hides and a one-sample window does not. */
    {
        std::vector<NodeSpec> spec(3);

        spec[0].name = "lfo";
        spec[0].spelling = "osc/simple";
        spec[0].values = { { "freq", 0.25f }, { "waveform", 0 },
                           { "amp", 1.0f } };

        spec[1].name = "half";
        spec[1].spelling = "math/mul";
        spec[1].values = { { "in1", 0.4f } };
        spec[1].wires  = { { "in0", "lfo", "out" } };

        spec[2].name = "mid";
        spec[2].spelling = "math/add";
        spec[2].values = { { "in1", 0.5f } };
        spec[2].wires  = { { "in0", "half", "out" } };

        compare(pluginPath, "osc -> mul -> add", spec, "mid", "out");
    }

    /* An envelope, which is the plan's other named use ("an envelope
       shaping a piece's dynamics over minutes") and the plugin family
       most likely to count windows rather than samples. */
    {
        std::vector<NodeSpec> spec(1);

        spec[0].name = "env";
        spec[0].spelling = "env/adsr";
        spec[0].values = { { "a", 20.0f }, { "d", 30.0f }, { "s", 0.5f },
                           { "r", 40.0f }, { "trigger", 1.0f } };

        compare(pluginPath, "env::adsr", spec, "env", "out");
    }

    /* A filter, for its history: the one kind of plugin that genuinely
       has to remember the sample before this one, and therefore the one
       most exposed to being handed a window of length one. */
    {
        std::vector<NodeSpec> spec(2);

        spec[0].name = "lfo";
        spec[0].spelling = "osc/simple";
        spec[0].values = { { "freq", 2.0f }, { "waveform", 1 },
                           { "amp", 1.0f } };

        spec[1].name = "lp";
        spec[1].spelling = "filt/res1pole";
        spec[1].values = { { "cutoff", 0.2f } };
        spec[1].wires  = { { "in", "lfo", "out" } };

        compare(pluginPath, "filt::res1pole over an osc", spec, "lp", "out");
    }

    /* Arithmetic on its own, with nothing ACTIVE in the graph.
     *
     * The shape GEN_FORMAT invites for putting a knob through a range,
     * and the one where the audio thread's own scheduling assumption --
     * that a graph is driven by something that produces without being
     * asked -- is simply false. It fired once and froze, and no other
     * graph here would have shown it, because every one of them has an
     * oscillator or an envelope pulling the passive nodes along behind
     * it. `in0' is stepped by hand between windows so there is
     * something to see. */
    {
        std::vector<NodeSpec> spec(2);

        spec[0].name = "gain";
        spec[0].spelling = "math/mul";
        spec[0].values = { { "in0", 0.5f }, { "in1", 2.0f } };

        spec[1].name = "lift";
        spec[1].spelling = "math/add";
        spec[1].values = { { "in1", 0.25f } };
        spec[1].wires  = { { "in0", "gain", "out" } };

        comparePassive(pluginPath, "math only, nothing active", spec,
                       "lift", "out", "gain", "in0");
    }

    /* And a logic family member, which is passive for the same reason
       and untested for the same one. */
    {
        std::vector<NodeSpec> spec(1);

        spec[0].name = "gate";
        spec[0].spelling = "logic/and";
        spec[0].values = { { "in0", 0.0f }, { "in1", 1.0f } };

        comparePassive(pluginPath, "logic::and, nothing active", spec,
                       "gate", "out", "gate", "in0");
    }

    if (failures == 0)
        printf("hostcheck: OK\n");
    else
        printf("hostcheck: %d failure%s\n", failures,
               failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
