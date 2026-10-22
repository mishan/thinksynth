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

#include "config.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <sstream>

#include "think.h"
#include "thFFT.h"

#include "NodeBreed.h"
#include "NodeEdit.h"
#include "NodeEvolve.h"

namespace NodeBreed {

using NodeMutate::Edit;
using NodeMutate::Gene;
using NodeMutate::Shape;
using NodeMutate::Splice;

/* The centroid and the share under LOW_HZ of `played', `channels'
   interleaved at `rate', from its power summed over 4096-point frames. */
static bool toneOf (const vector<float> &played, int channels, double rate,
                    double &centroid, double &low)
{
    thv::FFTR fft(12);
    vector<float> mag(fft.bins());
    vector<double> power(fft.bins(), 0);
    const size_t frames = channels > 0 ? played.size() / channels : 0;

    for (size_t at = 0; at + fft.size() <= frames; at += fft.size())
    {
        fft.magnitude([&] (unsigned int i)
                      {
                          float sum = 0;

                          for (int c = 0; c < channels; c++)
                              sum += played[(at + i) * channels + c];

                          return sum / channels;
                      }, mag.data());

        for (size_t k = 0; k < mag.size(); k++)
            power[k] += (double)mag[k] * mag[k];
    }

    double all = 0, weighted = 0, under = 0;

    for (size_t k = 1; k < power.size(); k++)
    {
        const double hz = k * rate / fft.size();

        all += power[k];
        weighted += power[k] * hz;
        under += hz < LOW_HZ ? power[k] : 0;
    }

    if (!(all > 0))
        return false;

    centroid = weighted / all;
    low = under / all;

    return true;
}

Ear localEar (const string &pluginPath, const string &base,
              const string &targetGraph, const string &targetWav,
              const Values &values, const Values &targetValues, double rate)
{
    auto levels = std::make_shared<thSynth>(pluginPath,
                                            TH_DEFAULT_WINDOW_LENGTH,
                                            TH_DEFAULT_SAMPLES);
    Ear ear;

    /* Each render with the noise streams started over, so what one
       measures is not what the synth rendered before it: a host that has
       bred for an hour and one that has just loaded the flock agree. */
    ear.level = [levels] (const string &text, double &rms, double &peak)
    {
        levels->setSeed(1);
        return NodeMutate::level(*levels, text, rms, peak);
    };
    ear.tone = [levels] (const string &text, double &rms, double &peak,
                         double &centroid, double &low)
    {
        vector<float> played;

        levels->setSeed(1);

        return NodeMutate::level(*levels, text, rms, peak, &played) &&
               toneOf(played, levels->audioChannelCount(),
                      TH_DEFAULT_SAMPLES, centroid, low);
    };

    if (!targetGraph.empty() || !targetWav.empty())
    {
        NodeEvolve::Spec spec;

        spec.pluginPath = pluginPath;
        spec.start = base;
        spec.targetGraph = targetGraph;
        spec.targetWav = targetWav;
        spec.startArgs = values;
        spec.targetArgs = targetValues;
        spec.rate = rate;

        auto heard = std::make_shared<NodeEvolve::Listener>(spec);
        auto open = std::make_shared<int>(-1);

        ear.target = [heard, open] (const string &text)
        {
            string why;

            if (*open < 0)
            {
                heard->synth().setSeed(1);
                *open = heard->open(why);
            }

            heard->synth().setSeed(1);

            return *open ? heard->distance(text) : NodeEvolve::DEAD;
        };
    }

    /* One listener for the champion it was last asked about: a generation
       asks about one champion for every member. */
    auto last = std::make_shared<std::pair<string,
                    std::shared_ptr<NodeEvolve::Listener> > >();

    ear.apart = [pluginPath, last, values, rate] (const string &text,
                                                  const string &from)
    {
        if (!last->second || last->first != from)
        {
            NodeEvolve::Spec spec;
            string why;

            spec.pluginPath = pluginPath;
            spec.start = from;
            spec.targetGraph = from;
            spec.startArgs = values;
            spec.targetArgs = values;
            spec.rate = rate;
            last->first = from;
            last->second = std::make_shared<NodeEvolve::Listener>(spec);
            last->second->synth().setSeed(1);

            if (!last->second->open(why))
                last->second.reset();
        }

        if (!last->second)
            return NodeEvolve::DEAD;

        last->second->synth().setSeed(1);

        return last->second->distance(text);
    };

    return ear;
}

/* ---- the flock as text ----
 *
 * A line a member, and a line an op under it, fields between tabs; a
 * number as the bits of its double, so no locale and no rounding changes
 * it on the way through a host.
 */

static string bits (double v)
{
    uint64_t u;
    char hex[24];

    memcpy(&u, &v, sizeof u);
    snprintf(hex, sizeof hex, "%016llx", (unsigned long long)u);

    return hex;
}

static double unbits (const string &hex)
{
    const uint64_t u = strtoull(hex.c_str(), NULL, 16);
    double v;

    memcpy(&v, &u, sizeof v);

    return v;
}

static vector<string> fields (const string &line, char sep)
{
    vector<string> out(1);

    for (char c : line)
        if (c == sep)
            out.push_back(string());
        else
            out.back() += c;

    return out;
}

static string opLine (const Op &op)
{
    string slots, tuned;

    for (const auto &s : op.slots)
        slots += (slots.empty() ? "" : ",") + s.first + "." + s.second;

    for (const Op::Tuned &t : op.tuned)
        tuned += (tuned.empty() ? "" : ",") + std::to_string(t.node) + ":" +
                 t.arg + ":" + bits(t.value);

    const string f[] = {
        "op", op.kind, op.wire.node, op.wire.arg, op.wire.srcNode,
        op.wire.srcPort, op.splice, std::to_string(op.variant),
        op.selected ? "1" : "0", bits(op.selector), slots,
        op.pitch.freqNode, op.pitch.freqPort, op.pitch.freqControl, tuned };
    string line;

    for (size_t i = 0; i < sizeof f / sizeof f[0]; i++)
        line += (i ? "\t" : "") + f[i];

    return line;
}

static bool opOf (const vector<string> &f, Op &op)
{
    if (f.size() != 15)
        return false;

    op = Op();
    op.kind = f[1];
    op.wire.node = f[2];
    op.wire.arg = f[3];
    op.wire.srcNode = f[4];
    op.wire.srcPort = f[5];
    op.splice = f[6];
    op.variant = atoi(f[7].c_str());
    op.selected = f[8] == "1";
    op.selector = unbits(f[9]);

    for (const string &s : f[10].empty() ? vector<string>()
                                         : fields(f[10], ','))
    {
        const size_t dot = s.find('.');

        if (dot == string::npos)
            return false;

        op.slots.push_back({ s.substr(0, dot), s.substr(dot + 1) });
    }

    op.pitch.freqNode = f[11];
    op.pitch.freqPort = f[12];
    op.pitch.freqControl = f[13];

    for (const string &t : f[14].empty() ? vector<string>()
                                         : fields(f[14], ','))
    {
        const vector<string> p = fields(t, ':');

        if (p.size() != 3)
            return false;

        op.tuned.push_back({ (size_t)strtoul(p[0].c_str(), NULL, 10), p[1],
                             unbits(p[2]) });
    }

    return true;
}

string save (const Flock &flock)
{
    string out = "flock\t" + std::to_string(flock.generations) + "\t" +
                 std::to_string(flock.played.size()) + "\t" +
                 std::to_string(flock.heard.size()) + "\n" + flock.played +
                 flock.heard + "\n";

    for (const Member &m : flock.members)
    {
        out += "member\t" + bits(m.near) + "\n";

        for (const Op &op : m.ops)
            out += opLine(op) + "\n";
    }

    return out;
}

bool load (const string &text, Flock &flock)
{
    std::istringstream in(text);
    string line;
    Flock f;

    if (!std::getline(in, line))
        return false;

    const vector<string> head = fields(line, '\t');

    if (head.size() != 4 || head[0] != "flock")
        return false;

    f.generations = (unsigned int)strtoul(head[1].c_str(), NULL, 10);
    f.played.resize((size_t)strtoull(head[2].c_str(), NULL, 10));
    f.heard.resize((size_t)strtoull(head[3].c_str(), NULL, 10));

    if ((!f.played.empty() &&
         !in.read(&f.played[0], (std::streamsize)f.played.size())) ||
        (!f.heard.empty() &&
         !in.read(&f.heard[0], (std::streamsize)f.heard.size())) ||
        !std::getline(in, line) || !line.empty())
        return false;

    while (std::getline(in, line))
    {
        const vector<string> fs = fields(line, '\t');
        Op op;

        if (fs.size() == 2 && fs[0] == "member")
        {
            f.members.push_back(Member());
            f.members.back().near = unbits(fs[1]);
        }
        else if (!f.members.empty() && fs[0] == "op" && opOf(fs, op))
            f.members.back().ops.push_back(op);
        else
            return false;
    }

    flock = f;

    return true;
}

/* ---- a generation ---- */

/* The dice as bits, never through a distribution, which each standard
   library draws its own way. */
struct Dice {
    std::mt19937 rng;

    explicit Dice (unsigned int seed) : rng(seed) {}

    unsigned int below (size_t n) { return n ? rng() % (unsigned int)n : 0; }
    bool chance (double p) { return (rng() >> 8) < p * 16777216.0; }
};

static bool parsedOn (thSynth *synth, const string &text, vector<Gene> &genes,
                      Shape &shape)
{
    static std::atomic<unsigned int> serial { 0 };
    const string name = "breed#" + std::to_string(++serial) + ".dsp";
    thSynthTree *tree;

    {
        thUtil::GraphHold hold = thUtil::addGraphText(name, text);

        tree = synth->parseTree(name);
    }

    if (tree == NULL)
        return false;

    NodeMutate::read(tree, text, &genes, &shape);
    delete tree;

    return true;
}

namespace {

/* How far a member with no target may move the declared graph's
   brightness before it is marked down, in octaves. */
const double BRIGHT_OCTAVES = 1.5;

struct Breeding {
    thSynth *synth;
    const string &base;
    const Params &params;
    const Ear &ear;
    Dice &dice;
    vector<Splice> pool;
    double rms, peak;

    /* The declared graph's brightness and low share, 0 where the ear
       cannot say; and the champion novelty is measured from. */
    double centroid = 0, low = 0;
    string played;

    bool text (const vector<Op> &ops, string &out)
    {
        return NodeMutate::replay(synth, base, ops, out);
    }

    bool keeps (const string &text, double *c = NULL, double *lo = NULL)
    {
        double r, p, cc, ll;

        if (ear.tone ? !ear.tone(text, r, p, cc, ll) : !ear.level(text, r, p))
            return false;

        if (c != NULL)
            *c = ear.tone ? cc : 0;

        if (lo != NULL)
            *lo = ear.tone ? ll : 0;

        /* Half the declared level at most, not growth's quarter: a
           flock's champion replaces the instrument every generation, and
           one that drops away reads as the piece stopping. */
        return NodeMutate::keepsLevel(r, p, rms, peak) && r >= rms * 0.5;
    }

    /* In dB, as novelty is, what a member with no target loses for
       sounding unlike the instrument it came from: brightness past
       BRIGHT_OCTAVES either way, and the low end, where the declared
       graph has one, more than half gone. DEAD for one that does not keep
       the level. */
    double flaw (const string &text)
    {
        double c, lo;

        if (!keeps(text, &c, &lo))
            return NodeEvolve::DEAD;

        if (!(centroid > 0) || !(c > 0))
            return 0;

        const double octaves = std::fabs(std::log2(c / centroid));
        double f = 4 * std::max(0.0, octaves - BRIGHT_OCTAVES);

        if (low > 0.2 && lo < low * 0.5)
            f += 6 * (1 - lo / (low * 0.5));

        return f;
    }

    /* Lower better: how far from the champion just played, up to
       NOVELTY_DB, less its flaw. */
    double novelty (const string &text)
    {
        const double f = flaw(text);
        const double apart = f >= NodeEvolve::DEAD ? f : ear.apart(text, played);

        return apart >= NodeEvolve::DEAD ? NodeEvolve::DEAD
                                         : f - std::min(apart, NOVELTY_DB);
    }

    /* A frequency `next' brought starts where the declared graph sounds,
       an octave under to two over by the dice, rather than at its
       plugin's default: a filter at a kilohertz on a bass is the bass
       gone. Only frequencies said in Hz or as a fraction of the rate;
       tuning finds the rest. */
    void seat (const string &now, string &next)
    {
        vector<Gene> genes, had;
        Shape was, is;

        if (!(centroid > 0) || !parsedOn(synth, now, had, was))
            return;

        static std::atomic<unsigned int> serial { 0 };
        const string name = "seat#" + std::to_string(++serial) + ".dsp";
        thSynthTree *tree;

        {
            thUtil::GraphHold hold = thUtil::addGraphText(name, next);

            tree = synth->parseTree(name);
        }

        if (tree == NULL)
            return;

        NodeMutate::read(tree, next, &genes, &is);

        const double hz = centroid * std::pow(2.0, (double)dice.below(4) - 1);
        string why;

        for (const Gene &g : genes)
        {
            thNode *node = g.node.empty() ? NULL : tree->findNode(g.node);
            thPlugin *plugin = node ? node->plugin() : NULL;

            if (plugin == NULL || !g.choices.empty() ||
                std::find(was.names.begin(), was.names.end(), g.node) !=
                    was.names.end())
                continue;

            for (int i = 0; i < plugin->argCount(); i++)
            {
                if (plugin->getArgName(i) != g.arg)
                    continue;

                const string units = plugin->getArgUnits(i);
                double v = units == "Hz" ? hz :
                           units == "fraction of the rate"
                               ? hz / TH_DEFAULT_SAMPLES : -1;

                if (v < 0)
                    continue;

                if (g.ranged)
                    v = std::max(g.lo, std::min(g.hi, v));

                NodeEdit::Text::setValue(next, g.node, g.arg, v, why);
            }
        }

        delete tree;
    }

    /* `ops' one edit on, or at the depth allowed one edit back; false if
       no edit of the kinds allowed was found that keeps the level. */
    bool mutate (vector<Op> &ops)
    {
        if (ops.size() >= params.depth)
        {
            if (ops.empty())
                return false;

            ops.erase(ops.begin() + dice.below(ops.size()));
            return true;
        }

        string now;
        vector<Gene> genes;
        Shape shape;

        if (!text(ops, now) || !parsedOn(synth, now, genes, shape))
            return false;

        const vector<Edit> all = NodeMutate::edits(now, shape, genes, pool);
        vector<const Edit *> allowed;
        string words = " " + params.kinds + " ";

        std::replace(words.begin(), words.end(), ',', ' ');

        for (const Edit &e : all)
            if (params.kinds.empty() ||
                words.find(" " + e.kind + " ") != string::npos)
                allowed.push_back(&e);

        if (allowed.empty())
            return false;

        /* Eight tries at most from where the dice land, as grow's sixteen:
           each is a render. */
        const unsigned int from = dice.below(allowed.size());

        for (size_t k = 0; k < allowed.size() && k < 8; k++)
        {
            Op op = allowed[(from + k) % allowed.size()]->op;
            string next = now, what;

            if (!NodeMutate::applyTagged(synth, next, op, what))
                continue;

            seat(now, next);

            /* Tuned before it is judged, target or not: an edit with its
               plugin's defaults is often an edit that fails the level and
               would not with its constants anywhere near right. */
            if (params.tune > 0)
                next = NodeEvolve::tuneAdded(
                    *synth, now, next, params.tune,
                    ear.target ? ear.target
                               : [this] (const string &t)
                                 { return novelty(t); });

            NodeMutate::tunedOf(synth, now, next, op);

            if (keeps(next))
            {
                ops.push_back(op);
                return true;
            }
        }

        return false;
    }
};

}

bool generation (thSynth *synth, const string &base, Flock &flock_,
                 const Params &params, unsigned int dice_, const Ear &ear,
                 string &champion, size_t &depth, string &why,
                 vector<Op> *championOps)
{
    /* Bred on a copy, so a generation stopped half way leaves the flock
       as it was. */
    Flock flock = flock_;
    const auto stopped = [&params, &why] ()
    {
        if (!params.stop || !params.stop())
            return false;

        why = "stopped";
        return true;
    };

    vector<Gene> genes;
    Shape shape;
    Dice dice(dice_);
    Breeding b = { synth, base, params, ear, dice, {}, 0, 0 };

    b.played = flock.played.empty() ? base : flock.played;

    if (synth == NULL || !parsedOn(synth, base, genes, shape))
    {
        why = "the declared graph does not parse";
        return false;
    }

    if (!ear.level || !ear.level(base, b.rms, b.peak))
    {
        why = "the declared graph makes no sound to measure against";
        return false;
    }

    if (ear.tone)
    {
        double r, p;

        if (!ear.tone(base, r, p, b.centroid, b.low))
            b.centroid = b.low = 0;
    }

    NodeMutate::buildPool(synth->getPluginManager(), b.pool);

    const size_t population = std::max<size_t>(params.population, 2);

    /* The declared graph, and the rest each one edit from it. */
    if (flock.members.empty())
    {
        flock.members.push_back(Member());

        while (flock.members.size() < population)
        {
            Member m;

            b.mutate(m.ops);
            flock.members.push_back(m);
        }
    }

    /* Heard against something else now -- another target, the values
       moved -- so no distance kept is one; and a member past a depth that
       has come down is held to it. */
    if (flock.heard != params.heard)
    {
        for (Member &m : flock.members)
            m.near = -1;

        flock.heard = params.heard;
    }

    for (Member &m : flock.members)
        while (m.ops.size() > params.depth)
        {
            m.ops.erase(m.ops.begin() + dice.below(m.ops.size()));
            m.near = -1;
        }

    /* Scored, lower nearer: from the target, or from the champion just
       played -- novelty, up to a point, less what it lost. */
    vector<string> texts(flock.members.size());
    vector<double> score(flock.members.size());

    for (size_t i = 0; i < flock.members.size(); i++)
    {
        Member &m = flock.members[i];

        if (stopped())
            return false;

        if (!b.text(m.ops, texts[i]))
            score[i] = NodeEvolve::DEAD;
        else if (ear.target)
        {
            if (m.near < 0)
                m.near = ear.target(texts[i]);

            score[i] = m.near;
        }
        else
            score[i] = b.novelty(texts[i]);
    }

    vector<size_t> order(flock.members.size());

    for (size_t i = 0; i < order.size(); i++)
        order[i] = i;

    std::stable_sort(order.begin(), order.end(),
                     [&score] (size_t x, size_t y)
                     { return score[x] < score[y]; });

    champion = texts[order[0]];
    depth = flock.members[order[0]].ops.size();
    flock.played = champion;
    b.played = champion;

    if (championOps != NULL)
        *championOps = flock.members[order[0]].ops;

    /* The next: the best carried, the rest bred from tournaments of two. */
    auto pick = [&] ()
    {
        const size_t x = dice.below(order.size()),
                     y = dice.below(order.size());

        return score[x] <= score[y] ? x : y;
    };
    vector<Member> next;

    for (size_t e = 0; e < std::min(params.elites, order.size()); e++)
        next.push_back(flock.members[order[e]]);

    while (next.size() < population)
    {
        if (stopped())
            return false;

        const size_t a = pick();
        Member child = flock.members[a];

        /* A few tries for a child that keeps the level; the parent as it
           is if none does. */
        for (int tries = 0; tries < 4; tries++)
        {
            vector<Op> ops = flock.members[a].ops;
            bool changed = false;

            if (dice.chance(params.crossover))
            {
                const size_t o = pick();
                const bool first = score[a] <= score[o];

                ops = NodeMutate::cross(flock.members[first ? a : o].ops,
                                        flock.members[first ? o : a].ops,
                                        dice.rng());
                changed = o != a;
            }

            if (dice.chance(params.mutation))
                changed |= b.mutate(ops);

            /* Two parents can hold more between them than one may. */
            while (ops.size() > params.depth)
                ops.erase(ops.begin() + dice.below(ops.size()));

            string text;
            vector<Op> kept;

            if (!changed)
                break;

            if (NodeMutate::replay(synth, base, ops, text, &kept) &&
                b.keeps(text))
            {
                child.ops = kept;
                child.near = -1;
                break;
            }
        }

        next.push_back(child);
    }

    flock.members.swap(next);
    flock.generations++;
    flock_ = flock;

    return true;
}

bool step (thSynth *synth, const string &base, const string &flock,
           const Params &params, unsigned int dice,
           const string &targetGraph, const string &targetWav,
           const Values &values, const Values &targetValues, double rate,
           string &flockOut, string &champion, size_t &depth, string &why)
{
    Flock f;

    if (!flock.empty() && !load(flock, f))
    {
        why = "the flock does not read back";
        return false;
    }

    if (synth == NULL)
    {
        why = "no synth to breed with";
        return false;
    }

    const Ear ear = localEar(synth->getPluginManager()->pluginPath(), base,
                             targetGraph, targetWav, values, targetValues,
                             rate);
    Params p = params;

    p.heard = targetGraph + '\n' + targetWav + '\n' + bits(rate);

    for (const Values *v : { &values, &targetValues })
    {
        p.heard += '\n';

        for (const auto &one : *v)
            p.heard += one.first + "=" + bits(one.second) + ";";
    }

    if (!generation(synth, base, f, p, dice, ear, champion, depth, why))
        return false;

    flockOut = save(f);

    return true;
}

}
