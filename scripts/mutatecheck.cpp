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
 * mutatecheck -- the edits a search makes to a graph (src/NodeMutate.h).
 *
 * dspmatch is where the vocabulary is judged, by ear, over minutes. This is
 * what can be said about it without rendering a note: that a graph's genes
 * and the places an edit can go are found, that every edit it proposes is a
 * graph that parses, and -- the ablation in miniature -- that a graph with
 * a node taken out is offered exactly that node back in exactly that place.
 * And the search over it (src/NodeEvolve.h), briefly: a tune that comes
 * nearer, and a cancel that stops.
 *
 *   mutatecheck -p build/plugins/ -d dsp/
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "think.h"
#include "NodeEvolve.h"
#include "NodeMutate.h"

using std::string;
using std::vector;
using namespace NodeMutate;

static int failed = 0;

static void okOrFail (bool good, const string &what, const string &detail)
{
    if (good)
        printf("ok    %s\n", what.c_str());
    else
    {
        printf("FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : ": ",
               detail.c_str());
        failed++;
    }
}

/* `text' parsed on `synth', through the registry an inline graph uses, so
   no candidate touches the disk. NULL if it does not parse. */
static thSynthTree *parse (thSynth &synth, const string &text)
{
    static int serial = 0;
    const string name = "mutatecheck#" + std::to_string(++serial) + ".dsp";
    thUtil::GraphHold hold = thUtil::addGraphText(name, text);

    return synth.parseTree(name);
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH, dspDir = "dsp";

    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            pluginPath = argv[++i];
        else if (!strcmp(argv[i], "-d") && i + 1 < argc)
            dspDir = argv[++i];

    std::ifstream in((dspDir + "/bass.dsp").c_str(), std::ios::binary);
    std::ostringstream text;

    text << in.rdbuf();

    /* With the line endings it was written with, which a Windows checkout
       may have changed: the drive is taken out below by its text. */
    string bass = text.str();

    bass.erase(std::remove(bass.begin(), bass.end(), '\r'), bass.end());
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    thSynthTree *tree = bass.empty() ? NULL : parse(synth, bass);

    if (tree == NULL)
    {
        okOrFail(false, "bass.dsp parses", dspDir);
        return 1;
    }

    vector<Gene> genes;
    Shape shape;

    read(tree, bass, &genes, &shape);

    bool drive = false;

    for (size_t g = 0; g < genes.size(); g++)
        drive |= genes[g].node.empty() && genes[g].arg == "drive" &&
                 genes[g].ranged && genes[g].lo == 1 && genes[g].hi == 6;

    okOrFail(drive, "a control is a gene, with its declared range",
             std::to_string(genes.size()) + " genes");

    bool wire = false;

    for (size_t w = 0; w < shape.wires.size(); w++)
        wire |= shape.wires[w].node == "vca" && shape.wires[w].arg == "in0" &&
                shape.wires[w].srcNode == "drive";

    okOrFail(wire, "a wire carrying the sound is a place a splice can go",
             std::to_string(shape.wires.size()) + " wires");

    /* A ranged gene comes back from past its wall at the wall. */
    for (size_t g = 0; g < genes.size(); g++)
        if (genes[g].choices.empty() && genes[g].ranged && !genes[g].octaves)
        {
            okOrFail(fromU(genes[g], toU(genes[g], genes[g].hi) + 100) ==
                         genes[g].hi,
                     "a step past a gene's range stops at its edge",
                     geneName(genes[g]));
            break;
        }

    vector<Splice> pool;

    buildPool(synth.getPluginManager(), pool);

    int svf = 0;

    for (size_t p = 0; p < pool.size(); p++)
        svf += pool[p].spelling == "filt::svf";

    okOrFail(svf == 3, "the pool has a state-variable filter once per way "
             "out", std::to_string(svf));

    /* Taken out by hand, and offered back. */
    string ablated = bass;
    const string block = "node drive dist::saturate {\n"
                         "    in = filt->out_low;\n"
                         "    factor = @drive;\n"
                         "};\n\n";
    const size_t at = ablated.find(block);
    const size_t vca = ablated.find("    in0 = drive->out;");

    if (at == string::npos || vca == string::npos)
    {
        okOrFail(false, "bass.dsp still has the drive this check takes out",
                 "");
        return 1;
    }

    ablated.replace(vca, strlen("    in0 = drive->out;"),
                    "    in0 = filt->out_low;");
    ablated.erase(at, block.size());

    thSynthTree *cut = parse(synth, ablated);
    vector<Gene> cutGenes;
    Shape cutShape;

    if (cut != NULL)
        read(cut, ablated, &cutGenes, &cutShape);

    const vector<Edit> all = edits(ablated, cutShape, cutGenes, pool);
    int parsed = 0;
    bool back = false;

    for (size_t e = 0; e < all.size(); e++)
    {
        thSynthTree *t = all[e].source != ablated
                       ? parse(synth, all[e].source) : NULL;

        parsed += t != NULL;
        delete t;

        back |= all[e].what ==
                "dist::saturate->out into vca.in0 = filt->out_low";
    }

    okOrFail(!all.empty() && parsed == (int)all.size(),
             "every edit proposed is a different graph that parses",
             std::to_string(parsed) + " of " + std::to_string(all.size()));
    okOrFail(back, "a graph with its saturator taken out is offered it back, "
             "between the filter and the vca", "");

    /* Each edit made again from where it goes rather than from its text. */
    int again = 0;

    for (size_t e = 0; e < all.size(); e++)
    {
        string text = ablated, what;

        again += apply(&synth, text, all[e].op, what) &&
                 text == all[e].source && what == all[e].what;
    }

    okOrFail(again == (int)all.size(), "every edit made again from its op "
             "is the same graph", std::to_string(again) + " of " +
             std::to_string(all.size()));

    /* Lineages from bass.dsp: each step an edit the graph as it stands
       offers, made tagged as a lineage makes it, the last of each with a
       constant it added moved as a tune would. */
    auto lineage = [&](const vector<size_t> &picks, vector<Op> &ops,
                       string &text)
    {
        text = bass;
        ops.clear();

        for (size_t k = 0; k < picks.size(); k++)
        {
            thSynthTree *t = parse(synth, text);
            vector<Gene> g;
            Shape s;

            if (t == NULL)
                return false;

            read(t, text, &g, &s);
            delete t;

            const vector<Edit> can = edits(text, s, g, pool);

            if (can.empty())
                return false;

            Op op = can[picks[k] % can.size()].op;
            string next = text, what;

            if (!applyTagged(&synth, next, op, what))
                return false;

            if (k + 1 == picks.size())
            {
                thSynthTree *u = parse(synth, next);
                vector<Gene> ng;
                Shape ns;

                if (u == NULL)
                    return false;

                read(u, next, &ng, &ns);
                delete u;

                for (const Gene &one : ng)
                    if (std::find(s.names.begin(), s.names.end(), one.node) ==
                        s.names.end())
                    {
                        write(next, one, moved(one, one.value, 3));
                        break;
                    }
            }

            tunedOf(&synth, text, next, op);
            ops.push_back(op);
            text = next;
        }

        return true;
    };
    vector<Op> a, b;
    string aText, bText, made;

    const bool built = lineage({ 5, 40, 77 }, a, aText) &&
                       lineage({ 5, 13, 21 }, b, bText);

    okOrFail(built && replay(&synth, bass, a, made) && made == aText &&
                 !a.back().tuned.empty(),
             "a lineage made again on the declared graph is the graph it "
             "made, tuning and all", "");

    vector<Op> kept;
    Op nowhere = a[0];

    nowhere.wire.node = "nowhere";
    nowhere.slots.assign(1, { "nowhere", "in" });
    a.insert(a.begin() + 1, nowhere);

    okOrFail(replay(&synth, bass, a, made, &kept) && made == aText &&
                 kept.size() == a.size() - 1,
             "an op whose place the graph does not have is passed over",
             std::to_string(kept.size()) + " of " + std::to_string(a.size()));
    a.erase(a.begin() + 1);

    /* Crossed: each key once and only the parents', each parent's ops in
       that parent's order, all of the fitter's, and the same child --
       tuning and all -- from the same dice. */
    auto inOrder = [](const vector<Op> &child, const vector<Op> &parent)
    {
        size_t at = 0;

        for (const Op &op : child)
            for (size_t i = at; i < parent.size(); i++)
                if (key(parent[i]) == key(op))
                {
                    at = i + 1;
                    break;
                }
                else if (i + 1 == parent.size())
                {
                    bool later = false;

                    for (size_t j = 0; j < at; j++)
                        later |= key(parent[j]) == key(op);

                    if (later)
                        return false;
                }

        return true;
    };
    std::set<string> parents, fromB;

    for (const Op &op : a)
        parents.insert(key(op));

    for (const Op &op : b)
        parents.insert(key(op));

    bool crossed = built;

    for (unsigned int seed = 1; crossed && seed <= 16; seed++)
    {
        const vector<Op> child = cross(a, b, seed), twin = cross(a, b, seed);
        std::set<string> seen;
        size_t fromA = 0;

        for (const Op &op : child)
        {
            crossed &= parents.count(key(op)) && seen.insert(key(op)).second;

            bool mine = false;

            for (const Op &x : a)
                mine |= key(x) == key(op);

            fromA += mine;

            if (!mine)
                fromB.insert(key(op));
        }

        crossed &= fromA == a.size() && inOrder(child, a) &&
                   inOrder(child, b) && twin.size() == child.size() &&
                   std::equal(twin.begin(), twin.end(), child.begin(),
                              [](const Op &x, const Op &y)
                              { return key(x) == key(y) &&
                                       x.tuned.size() == y.tuned.size() &&
                                       std::equal(x.tuned.begin(),
                                                  x.tuned.end(),
                                                  y.tuned.begin(),
                                                  [](const Op::Tuned &p,
                                                     const Op::Tuned &q)
                                                  { return p.value ==
                                                           q.value; }); });
    }

    okOrFail(crossed && !fromB.empty(),
             "a child has each parent's ops in that parent's order, all the "
             "fitter's and the other's at even odds, each once, and the same "
             "child from the same dice",
             std::to_string(fromB.size()) + " of the other's taken");

    /* The other made Y after B, though it made A after both and the
       fitter made A first: Y still follows B in every child that has it.
       Ops told apart by key alone; nothing is made. */
    {
        auto named = [](const string &n) { Op op; op.wire.node = n; return op; };
        auto where = [](const vector<Op> &child, const Op &op)
        {
            for (size_t i = 0; i < child.size(); i++)
                if (key(child[i]) == key(op))
                    return (long)i;

            return -1L;
        };
        const Op A = named("A"), B = named("B"), X = named("X"),
                 Y = named("Y");
        bool after = true, taken = false;

        for (unsigned int seed = 1; seed <= 16; seed++)
        {
            const vector<Op> child = cross({ A, B }, { B, X, A, Y }, seed);
            const long y = where(child, Y);

            taken |= y >= 0;
            after &= y < 0 || y > where(child, B);
        }

        okOrFail(after && taken, "an op of the other parent's is not put "
                 "before one it was made after", "");
    }

    /* The fitter splices a filter and then modulates its cutoff; the other
       splices the same filter into another wire. Whichever comes first in
       a child, the modulation is on the fitter's filter, not the other's. */
    {
        thSynthTree *t = parse(synth, bass);
        vector<Gene> g;
        Shape s;

        read(t, bass, &g, &s);
        delete t;

        const vector<Edit> can = edits(bass, s, g, pool);
        const Edit *first = NULL, *second = NULL;

        for (const Edit &e : can)
            if (e.op.splice != "filt::svf->out_low")
                continue;
            else if (first == NULL)
                first = &e;
            else if (second == NULL &&
                     (e.op.wire.node != first->op.wire.node ||
                      e.op.wire.arg != first->op.wire.arg))
                second = &e;

        bool aimed = first != NULL && second != NULL;
        string mine, filter;

        if (aimed)
        {
            string what;

            mine = bass;
            aimed = applyTagged(&synth, mine, first->op, what);

            thSynthTree *u = aimed ? parse(synth, mine) : NULL;
            vector<Gene> ng;
            Shape ns;

            if (u != NULL)
            {
                read(u, mine, &ng, &ns);
                delete u;
            }

            for (const string &n : ns.names)
                if (std::find(s.names.begin(), s.names.end(), n) ==
                    s.names.end())
                    filter = n;

            const vector<Edit> next = edits(mine, ns, ng, pool);
            const Edit *mod = NULL;

            for (const Edit &e : next)
                if (mod == NULL && e.kind == "modulate" &&
                    e.op.slots.size() == 1 &&
                    e.op.slots[0].first == filter &&
                    e.op.slots[0].second == "cutoff")
                    mod = &e;

            aimed = mod != NULL;

            for (unsigned int seed = 1; aimed && seed <= 16; seed++)
            {
                const vector<Op> child = cross({ first->op, mod->op },
                                               { second->op }, seed);

                aimed = replay(&synth, bass, child, made, &kept) &&
                        kept.size() == child.size();

                const size_t at = made.find("node " + filter + " ");
                const size_t end = made.find("}", at);
                const size_t cut = made.find("cutoff = base_", at);

                aimed &= at != string::npos && cut != string::npos &&
                         cut < end;
            }
        }

        okOrFail(aimed, "an op on a node its lineage added finds that node "
                 "whatever the other parent added first",
                 filter.empty() ? "no filter spliced" : filter);
    }

    /* And the search that uses it, on a thread of its own: an enveloped
       sine with its sustain moved, searched toward itself as written,
       comes nearer; and a search cancelled stops. No choice genes -- no
       waveform line -- so there is nothing to race and the tune is seconds. */
    {
        const string target =
            "node ionode { channels = 1; out0 = vca->out; play = env->play; };\n"
            "io ionode;\n"
            "node freq misc::midi2freq { note = ionode->note; };\n"
            "node osc osc::simple { freq = freq->out; amp = ionode->velocity; };\n"
            "node filt filt::svf { in = osc->out; cutoff = 900; res = 0.4; };\n"
            "node env env::adsr { a = 5 ms; d = 200 ms; s = 0.7; r = 100 ms;\n"
            "    trigger = ionode->trigger; };\n"
            "node vca mixer::mul { in0 = filt->out_low; in1 = env->out; };\n";
        string scrambled = target;
        const size_t d = scrambled.find("s = 0.7;");

        if (d != string::npos)
            scrambled.replace(d, strlen("s = 0.7;"), "s = 0.05;");

        NodeEvolve::Job job;
        NodeEvolve::Job::Spec spec;
        string why, found;
        double distance = 0;

        spec.pluginPath = pluginPath;
        spec.start = scrambled;
        spec.targetGraph = target;
        spec.generations = 20;

        okOrFail(d != string::npos && job.start(spec, why),
                 "a search starts", why);

        for (int i = 0; i < 6000 && !job.done(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        const bool ok = job.result(found, distance, why);

        okOrFail(ok && distance < job.started() * 0.5 && found != scrambled,
                 "a tune moves the graph toward its target",
                 std::to_string(job.started()) + " -> " +
                 std::to_string(distance) + (ok ? "" : " (no result) ") +
                 (job.done() ? "" : " (not done) ") + why);

        NodeEvolve::Job grow;

        spec.grow = true;
        grow.start(spec, why);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        grow.cancel();

        /* Promptly: a window waits on this when it closes. */
        const auto asked = std::chrono::steady_clock::now();

        while (!grow.done() &&
               std::chrono::steady_clock::now() - asked <
                   std::chrono::seconds(30))
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        const double took = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - asked).count();

        okOrFail(grow.done() && took < 3, "a search that is cancelled stops "
                 "within a few seconds", std::to_string(took) + " s");
    }

    delete cut;
    delete tree;

    printf("\n%d failure(s)\n", failed);

    return failed;
}
