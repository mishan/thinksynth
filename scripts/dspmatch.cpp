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

/*
 * dspmatch -- can a search hear its way back to a patch?
 *
 * A search judged on its audio is two things that fail differently: a
 * distance between sounds, and something that walks downhill on it. When
 * the pair fails to match a recording there is no telling which was at
 * fault, or whether the synth could have made the sound at all. So the
 * targets here are renders of patches that ship: reachable by construction,
 * with the answer known. Three questions, in the order they have to be
 * answered, and then the thing itself:
 *
 *   near     Does the distance agree with an ear about which sounds are
 *            alike? Renders every file given and lists each one's nearest
 *            neighbors. A kick should land next to the other kicks.
 *
 *   probe    Is the distance something a search can walk on? Moves each
 *            constant of one patch a step at a time either side of where
 *            it is and measures how far the sound went. A constant is
 *            `smooth' if further is always further, `rugged' if not, and
 *            `deaf' if nothing moved -- which is a gene a search wastes
 *            draws on.
 *
 *   recover  Does it close? Scrambles a patch's constants, then runs a
 *            (1+lambda) evolution strategy against the render of the
 *            original. The distance should come back towards zero. The
 *            constants need not come back to where they were: two
 *            settings that sound alike are one answer, not an error.
 *
 *   match    The same search with the target read from a WAV file and the
 *            constants starting where the patch has them: how near can this
 *            graph get to that sound. `-o' names where the answer goes.
 *
 *   grow     match, and then the graph itself: a node from the pool spliced
 *            into a wire, wherever one can go, kept if the sound is nearer
 *            for it. `pool' lists what may be spliced in.
 *
 * A gene is a constant the author wrote down -- `node { arg = 0.3; }' or a
 * top-level `@control = 0.3;' -- and an edit is NodeEdit::Text splicing the
 * patch's own text, so every candidate is a .dsp that loads, diffs and
 * opens in the editor.
 *
 * A gene moves in a coordinate where a step is about as audible anywhere.
 * A time or a frequency moves in octaves: from 2 ms, 50 ms away is a
 * different instrument and from 400 ms it is a nudge, and a search stepping
 * by a fraction of a 0-500 ms range cannot land on a 2 ms attack at all.
 * Anything written with a unit is taken for one of those, and so is an
 * unranged constant, and so is a control sitting in the bottom tenth of a
 * range that starts at zero -- an LFO rate of 0.5 in 0 to 10 is the usual
 * one, which a step of a twentieth of the range doubles. The rest move across their declared range.
 * A constant of zero with no range has neither and is left alone.
 *
 * Some constants are not quantities. A waveform is one of six, a tap count
 * is a whole number, and an FM operator's ratio is a quantity only on
 * paper: 2 is an octave, 2.1 is a bell, and between them the distance does
 * not slope towards either. These are choice genes -- one of a short list.
 * Nothing in a plugin says "this ratio tunes an oscillator", so which args
 * those are is a table here; it wants to be something a plugin declares.
 *
 * A choice cannot be judged by making it. By the time a search has been
 * running a while its quantities are tuned around the choices it has, and
 * the right waveform dropped into a patch tuned for the wrong one sounds
 * worse than the wrong one does. Both of the obvious schemes were tried --
 * a probability per choice learned from each generation's winners, and
 * rendering the search's mean with one choice flipped -- and both keep
 * whatever a gene started as. So a choice is judged after it has been tuned
 * for: each alternative gets a short search of its own from where the best
 * patch stands, the incumbent gets the same so that it is not beaten by the
 * extra tuning alone, and the winner is where the next gene starts from. A
 * graph edit is a choice in this sense too, and would be judged the same
 * way.
 *
 * A graph edit is judged as a choice is, and costs what a choice costs, so
 * it is raced in two heats: every candidate gets a few generations, which
 * is enough to tell a filter on the note number from a filter on the
 * signal, and the few that come through get a trial of the full length
 * beside the graph as it stands. An edit has to win by a margin, not by a
 * hair. A bigger graph always fits a little better, and a node that buys a
 * hundredth of a dB is a node someone has to read.
 *
 * The edits are text edits -- NodeEdit::Text adds the node and moves the
 * wire -- so a grown patch is a .dsp with one more block in it, and what it
 * grew is a diff.
 *
 * The search is a separable CMA-ES: a step size per gene, learned from
 * which steps paid. One step size for every gene was tried first and
 * stalls a dB or two out, because a gene the sound hangs on and a gene it
 * barely hears want steps a hundred times apart. Separable, so without the
 * covariances that would let it walk along a trade between two controls;
 * that is the next thing to add if runs stall on one.
 *
 * Candidates are scored in forked children, one each. A thSynth is not
 * something two threads can each have one of, and a process is.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "think.h"
#include "thcRandom.h"

#include "NodeCatalog.h"
#include "NodeEdit.h"
#include "NodeGraph.h"
#include "soundfeat.h"

using thsound::Distance;
using thsound::Extractor;
using thsound::Features;

/* A second held and half a second of release, in windows. The release is
   half of what an envelope is, and a render that never lets go cannot hear
   it. */
static int HOLD_WINDOWS = 43;
static const int TAIL_WINDOWS = 22;

/* Which term probe prints: a sum cannot say which of its parts misbehaved. */
static string probeTerm = "total";

static const double RANGED_STEP = 0.05;     /* of the declared range */
static const double OCTAVE_STEP = 0.25;     /* octaves               */
static const int PROBE_STEPS = 4;
static const double SCRAMBLE_STEPS = 6;

static string pluginPath;
static string scratchPath;
static int note = 60;

struct Gene {
    string node;        /* empty for a top-level control */
    string arg;
    double value;
    double lo, hi;
    bool ranged;
    bool octaves;

    /* Not empty: the gene is one of these and nothing between. */
    vector<double> choices;
};

/* Args that tune an oscillator against the note. Whole and half multiples
   are the ones with names; anything else a patch already says is kept as
   one more choice. */
static const char *const HARMONIC[][2] = {
    { "osc/fmop", "ratio" },
    { "osc/multiwave", "pitchmul" },
    { "osc/simple", "mul" },
};

static bool harmonic (thPlugin *plugin, const string &arg)
{
    for (size_t i = 0; plugin && i < sizeof HARMONIC / sizeof HARMONIC[0]; i++)
    {
        const string path = plugin->path();
        const string want = HARMONIC[i][0];

        if (arg == HARMONIC[i][1] &&
            path.find(want) != string::npos)
            return true;
    }

    return false;
}

/* What a plugin's arg may be chosen from, if it is a choice at all. */
static vector<double> choicesOf (thPlugin *plugin, int idx, const string &arg,
                                 double value)
{
    vector<double> c;

    if (plugin == NULL || idx < 0 || idx >= plugin->argCount())
        return c;

    const size_t names = plugin->getArgValues(idx).size();
    const double step = plugin->getArgStep(idx);
    const double lo = plugin->getArgMin(idx), hi = plugin->getArgMax(idx);

    if (names)
        for (size_t i = 0; i < names; i++)
            c.push_back((double)i);
    else if (step >= 1 && hi > lo && (hi - lo) / step <= 16)
        for (double v = lo; v <= hi; v += step)
            c.push_back(v);
    else if (harmonic(plugin, arg))
    {
        for (int k = 1; k <= 16; k++)
            c.push_back(k * 0.5);

        if (std::find(c.begin(), c.end(), value) == c.end())
            c.push_back(value);
    }

    return c;
}

/* `node.arg = srcNode->srcPort', spelled just so: an arg fed by arithmetic
   is a graph of its own and has no one wire to splice into. */
struct Wire {
    string node, arg;
    string srcNode, srcPort;
};

/* What of a patch's structure an edit needs to know. */
struct Shape {
    vector<Wire> wires;
    vector<string> names;
};

/* Something that can go in a wire: one way in, one way out, and what to
   set the rest to so that it starts as something rather than as zeros. */
struct Splice {
    string spelling, name;
    string in, out;
    vector<pair<string, double> > initial;
};

static int argIndex (thPlugin *plugin, const string &arg)
{
    for (int i = 0; plugin && i < plugin->argCount(); i++)
        if (plugin->getArgName(i) == arg)
            return i;

    return -1;
}


static bool readFile (const char *path, string &out)
{
    std::ifstream in(path);

    if (!in)
        return false;

    std::ostringstream ss;

    ss << in.rdbuf();
    out = ss.str();

    return true;
}

/* One note of a patch's text, mixed to mono. Through a scratch file because
   loadTree onto a channel takes a filename; a search that did this in
   earnest would want the FILE* overload taught about channels. */
static bool render (const string &source, vector<float> &mono,
                    vector<Gene> *genes = NULL, Shape *shape = NULL)
{
    {
        std::ofstream out(scratchPath.c_str(), std::ios::trunc);

        out << source;

        if (!out)
            return false;
    }

    srand(1);

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    thSynthTree *tree = synth.loadTree(scratchPath, 0, 100);

    if (tree == NULL)
        return false;

    if (genes)
    {
        const thSynthTree::NodeMap &nodes = tree->nodes();
        std::map<string, vector<double> > driven;

        for (thSynthTree::NodeMap::const_iterator n = nodes.begin();
             n != nodes.end(); ++n)
        {
            /* The io node's constants -- channels, poly, mono -- are what
               the instrument is, not how it sounds. */
            if (n->second == NULL || n->second == tree->IONode())
                continue;

            const thArgMap &args = n->second->args();

            for (thArgMap::const_iterator a = args.begin(); a != args.end(); ++a)
            {
                thArg *arg = a->second;

                thPlugin *plugin = n->second->plugin();

                /* A control takes after what it drives, which is how the
                   engine types one too: `@ratio' is a list of ratios
                   because an operator's `ratio' reads it. */
                if (arg && arg->type() == thArg::ARG_CHANNEL)
                {
                    const vector<double> c =
                        choicesOf(plugin, argIndex(plugin, a->first), a->first, 0);

                    if (!c.empty() && !driven.count(arg->argPtrName()))
                        driven[arg->argPtrName()] = c;
                }

                if (arg == NULL || arg->type() != thArg::ARG_VALUE ||
                    arg->len() != 1 ||
                    NodeEdit::Text::find(source, n->first, a->first) != NodeEdit::OK)
                    continue;

                const int idx = argIndex(plugin, a->first);

                /* The plugin's range is where it stays a filter: past it
                   a resonance is an oscillator and then a NaN. It bounds
                   the gene and does not scale it -- a signal input's range
                   is all of full scale, and a twentieth of that is no
                   step. */
                const bool bounded = idx >= 0 && plugin->argHasRange(idx);

                Gene g = { n->first, a->first, (*arg)[0],
                           bounded ? plugin->getArgMin(idx) : 0,
                           bounded ? plugin->getArgMax(idx) : 0, bounded, true,
                           choicesOf(plugin, idx, a->first, (*arg)[0]) };

                /* A count too long to list is not searched at all: it is
                   still not a quantity. */

                if (g.choices.empty() &&
                    ((*arg)[0] == 0 || (idx >= 0 && plugin->getArgStep(idx) >= 1)))
                    continue;

                genes->push_back(g);
            }
        }

        const thArgMap &controls = tree->chanArgs();

        for (thArgMap::const_iterator c_ = controls.begin();
             c_ != controls.end(); ++c_)
        {
            thArg *arg = c_->second;

            if (arg == NULL || arg->len() != 1)
                continue;

            const double v = (*arg)[0];
            const bool ranged = arg->max() > arg->min();

            vector<double> c;

            if (!arg->valueNames().empty())
                for (size_t i = 0; i < arg->valueNames().size(); i++)
                    c.push_back((double)i);
            else if (arg->step() >= 1)
            {
                if (ranged && (arg->max() - arg->min()) / arg->step() <= 16)
                    for (double x = arg->min(); x <= arg->max(); x += arg->step())
                        c.push_back(x);
                else
                    continue;
            }
            else if (driven.count(c_->first))
            {
                const vector<double> &all = driven[c_->first];

                for (size_t i = 0; i < all.size(); i++)
                    if (!ranged || (all[i] >= arg->min() && all[i] <= arg->max()))
                        c.push_back(all[i]);

                if (std::find(c.begin(), c.end(), v) == c.end())
                    c.push_back(v);
            }

            if (c.empty() && !ranged && v == 0)
                continue;

            const bool octaves = v > 0 &&
                (!ranged || !arg->units().empty() ||
                 (arg->min() >= 0 && arg->max() >= 10 * v));

            const Gene g = { "", c_->first, v, arg->min(), arg->max(),
                             ranged, octaves, c };

            genes->push_back(g);
        }
    }

    if (shape)
    {
        NodeGraph graph;
        std::map<string, bool> seen;

        graph.build(tree);

        for (size_t b = 0; b < graph.boxes().size(); b++)
        {
            const NodeGraph::Box &box = graph.boxes()[b];

            if (!seen[box.name])
                shape->names.push_back(box.name);

            seen[box.name] = true;

            for (size_t a = 0; a < box.params.size(); a++)
            {
                const NodeGraph::Param &param = box.params[a];
                const size_t arrow = param.source.find("->");

                if (param.kind != NodeGraph::Param::POINTER || param.isExpr ||
                    arrow == string::npos)
                    continue;

                const Wire w = { box.name, param.name,
                                 param.source.substr(0, arrow),
                                 param.source.substr(arrow + 2) };

                /* What the io node sends is the note, not the sound of
                   it: a number, a gate. Nothing in the pool is for those,
                   nor for `play', which is a gate going the other way. */
                if (tree->IONode() &&
                    (w.srcNode == tree->IONode()->name() ||
                     (w.node == tree->IONode()->name() && w.arg == "play")))
                    continue;

                bool have = false;

                for (size_t i = 0; i < shape->wires.size(); i++)
                    have |= shape->wires[i].node == w.node &&
                            shape->wires[i].arg == w.arg;

                if (!have)
                    shape->wires.push_back(w);
            }
        }
    }

    synth.addNote(0, note, 100);

    const int channels = synth.audioChannelCount();
    const int len = synth.getWindowlen();

    mono.clear();
    mono.reserve((size_t)(HOLD_WINDOWS + TAIL_WINDOWS) * len);

    for (int w = 0; w < HOLD_WINDOWS + TAIL_WINDOWS; w++)
    {
        if (w == HOLD_WINDOWS)
            synth.delNote(0, note);

        synth.process();

        const float *buf = synth.getOutput();

        for (int i = 0; i < len; i++)
        {
            double acc = 0;

            for (int c = 0; c < channels; c++)
                acc += buf[(size_t)c * len + i];

            mono.push_back((float)(acc / channels));
        }
    }

    return true;
}

static unsigned int le (const unsigned char *p, int bytes)
{
    unsigned int v = 0;

    for (int i = 0; i < bytes; i++)
        v |= (unsigned int)p[i] << (8 * i);

    return v;
}

/* PCM of 16, 24 or 32 bits or 32-bit float, mixed to mono. At the synth's
   own rate or not at all: a resampler is a filter, and a target that has
   been through one is a different target. */
static bool readWav (const char *path, vector<float> &mono, string &why)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;

    ss << in.rdbuf();

    const string data = ss.str();
    const unsigned char *d = (const unsigned char *)data.data();

    if (!in || data.size() < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4))
    {
        why = "not a WAV file";
        return false;
    }

    unsigned int format = 0, channels = 0, rate = 0, bits = 0;

    for (size_t at = 12; at + 8 <= data.size(); )
    {
        const size_t size = le(d + at + 4, 4);
        const unsigned char *body = d + at + 8;

        if (at + 8 + size > data.size())
            break;

        if (!memcmp(d + at, "fmt ", 4) && size >= 16)
        {
            format = le(body, 2);
            channels = le(body + 2, 2);
            rate = le(body + 4, 4);
            bits = le(body + 14, 2);

            /* WAVE_FORMAT_EXTENSIBLE keeps the real format in the first
               two bytes of its GUID. */
            if (format == 0xFFFE && size >= 26)
                format = le(body + 24, 2);
        }
        else if (!memcmp(d + at, "data", 4))
        {
            const unsigned int bytes = bits / 8;

            if (!channels || !bytes ||
                !((format == 1 && (bits == 16 || bits == 24 || bits == 32)) ||
                  (format == 3 && bits == 32)))
            {
                why = "only 16, 24 or 32 bit PCM and 32 bit float are read";
                return false;
            }

            if (rate != TH_DEFAULT_SAMPLES)
            {
                why = "the target has to be at 44100 Hz";
                return false;
            }

            for (size_t f = 0; (f + 1) * channels * bytes <= size; f++)
            {
                double acc = 0;

                for (unsigned int c = 0; c < channels; c++)
                {
                    const unsigned char *p = body + (f * channels + c) * bytes;

                    if (format == 3)
                    {
                        float x;

                        memcpy(&x, p, 4);
                        acc += x;
                    }
                    else
                    {
                        /* Sign-extended by shifting the sample to the top
                           of the word first. */
                        const int v = (int)(le(p, (int)bytes) << (32 - bits));

                        acc += (double)v / 2147483648.0;
                    }
                }

                mono.push_back((float)(acc / channels));
            }

            return true;
        }

        at += 8 + size + (size & 1);
    }

    why = "no sample data";
    return false;
}

/* 16-bit mono, peaking 3 dB under full scale whatever the render's own
   level was: the distance does not hear gain, so what it found has none
   worth keeping, and three files at three levels cannot be compared by
   ear. */
static bool writeWav (const string &path, const vector<float> &mono)
{
    double peak = 0;

    for (size_t i = 0; i < mono.size(); i++)
        if (fabs((double)mono[i]) > peak)
            peak = fabs((double)mono[i]);

    const double gain = peak > 0 ? 0.708 * 32767.0 / peak : 0;
    const unsigned int bytes = (unsigned int)mono.size() * 2;

    std::ofstream out(path.c_str(), std::ios::binary);

    unsigned char h[44] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E',
                            'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
                            0,0,0,0, 0,0,0,0, 2,0, 16,0,
                            'd','a','t','a', 0,0,0,0 };
    const unsigned int fields[4][2] = { { 4, 36 + bytes },
                                        { 24, TH_DEFAULT_SAMPLES },
                                        { 28, TH_DEFAULT_SAMPLES * 2 },
                                        { 40, bytes } };

    for (int f = 0; f < 4; f++)
        for (int i = 0; i < 4; i++)
            h[fields[f][0] + i] = (unsigned char)(fields[f][1] >> (8 * i));

    out.write((const char *)h, sizeof h);

    for (size_t i = 0; i < mono.size(); i++)
    {
        const int v = (int)lrint((double)mono[i] * gain);
        const unsigned char b[2] = { (unsigned char)(v & 0xff),
                                     (unsigned char)((v >> 8) & 0xff) };

        out.write((const char *)b, 2);
    }

    return (bool)out;
}

/* `source' and what it sounds like, beside each other. */
static void save (const string &prefix, const string &source)
{
    vector<float> mono;

    std::ofstream((prefix + ".dsp").c_str()) << source;

    if (render(source, mono))
        writeWav(prefix + ".wav", mono);
}

/* A value in the gene's own coordinate, and back. Coming back clamps to
   the declared range, so going there again says where the wall was. */
static double toU (const Gene &g, double v)
{
    /* A choice gene's coordinate is which choice, and nothing moves in it:
       it is only so one vector can say where every gene is. */
    if (!g.choices.empty())
    {
        size_t nearest = 0;

        for (size_t i = 1; i < g.choices.size(); i++)
            if (fabs(g.choices[i] - v) < fabs(g.choices[nearest] - v))
                nearest = i;

        return (double)nearest;
    }

    if (g.octaves)
        return log2(fabs(v)) / OCTAVE_STEP;

    return (v - g.lo) / (RANGED_STEP * (g.hi - g.lo));
}

static double fromU (const Gene &g, double u)
{
    if (!g.choices.empty())
    {
        const long i = lrint(u);

        return g.choices[i < 0 ? 0 : (i >= (long)g.choices.size()
                                      ? g.choices.size() - 1 : (size_t)i)];
    }

    double v = g.octaves
             ? (g.value < 0 ? -1.0 : 1.0) * pow(2.0, u * OCTAVE_STEP)
             : g.lo + u * RANGED_STEP * (g.hi - g.lo);

    if (g.ranged)
        v = v < g.lo ? g.lo : (v > g.hi ? g.hi : v);

    return v;
}

static double moved (const Gene &g, double from, double du)
{
    return fromU(g, toU(g, from) + du);
}

static bool write (string &source, const Gene &g, double value)
{
    string why;

    const NodeEdit::Result r = g.node.empty()
        ? NodeEdit::Text::setChanArg(source, g.arg, value, why)
        : NodeEdit::Text::setValue(source, g.node, g.arg, value, why);

    return r == NodeEdit::OK;
}

static string geneName (const Gene &g)
{
    return g.node.empty() ? "@" + g.arg : g.node + "." + g.arg;
}

/* A candidate that will not load, or is not a sound, is as far away as
   anything can be. Finite, so a search can still rank two of them. */
static const double DEAD = 1e6;

static double score (const Extractor &ex, const Features &target,
                     const string &source, Distance *parts = NULL)
{
    vector<float> mono;
    Features f;

    if (!render(source, mono))
        return DEAD;

    ex.extract(mono, f);

    if (!f.usable())
        return DEAD;

    const Distance d = Extractor::distance(target, f);

    if (parts)
        *parts = d;

    return d.total();
}

/* score() for each of `sources', each in a child of its own with a
   scratch file of its own. A child that dies scores as a patch that would
   not load, which for a search is what it was. */
static void scoreAll (const Extractor &ex, const Features &target,
                      const vector<string> &sources, vector<double> &out)
{
    vector<pid_t> pids(sources.size(), -1);
    vector<int> fds(sources.size(), -1);

    out.assign(sources.size(), DEAD);

    for (size_t k = 0; k < sources.size(); k++)
    {
        int fd[2];

        if (pipe(fd) != 0)
            continue;

        pids[k] = fork();

        if (pids[k] == 0)
        {
            char suffix[32];

            close(fd[0]);
            snprintf(suffix, sizeof suffix, ".%d.dsp", (int)k);
            scratchPath += suffix;

            const double d = score(ex, target, sources[k]);
            const ssize_t n = ::write(fd[1], &d, sizeof d);

            unlink(scratchPath.c_str());
            _exit(n == (ssize_t)sizeof d ? 0 : 1);
        }

        close(fd[1]);

        if (pids[k] < 0)
            close(fd[0]);
        else
            fds[k] = fd[0];
    }

    for (size_t k = 0; k < sources.size(); k++)
    {
        if (fds[k] < 0)
            continue;

        double d;

        if (read(fds[k], &d, sizeof d) == (ssize_t)sizeof d && std::isfinite(d))
            out[k] = d;

        close(fds[k]);
        waitpid(pids[k], NULL, 0);
    }
}

/* ------------------------------------------------------------------ near */

static int near (const Extractor &ex, int argc, char **argv)
{
    vector<string> names;
    vector<Features> feats;

    for (int i = 0; i < argc; i++)
    {
        string source;
        vector<float> mono;
        Features f;

        if (!readFile(argv[i], source) || !render(source, mono))
        {
            printf("skip  %s: would not load\n", argv[i]);
            continue;
        }

        ex.extract(mono, f);

        if (!f.usable())
        {
            printf("skip  %s: %s\n", argv[i], thsound::verdictName(f.verdict));
            continue;
        }

        const char *slash = strrchr(argv[i], '/');

        names.push_back(slash ? slash + 1 : argv[i]);
        feats.push_back(f);
    }

    printf("\n%-22s  %s\n", "patch", "nearest (spectral + envelope/2, dB)");

    for (size_t i = 0; i < feats.size(); i++)
    {
        vector<std::pair<double, size_t> > order;

        for (size_t j = 0; j < feats.size(); j++)
            if (j != i)
                order.push_back(std::make_pair(
                    Extractor::distance(feats[i], feats[j]).total(), j));

        std::stable_sort(order.begin(), order.end());

        printf("%-22s", names[i].c_str());

        for (size_t k = 0; k < 3 && k < order.size(); k++)
            printf("  %s %.1f", names[order[k].second].c_str(), order[k].first);

        printf("\n");
    }

    return 0;
}

/* ----------------------------------------------------------------- probe */

static int probe (const Extractor &ex, const char *file)
{
    string source;
    vector<float> mono;
    vector<Gene> genes;
    Features target;

    if (!readFile(file, source) || !render(source, mono, &genes))
    {
        printf("%s: would not load\n", file);
        return 2;
    }

    ex.extract(mono, target);

    if (!target.usable())
    {
        printf("%s: %s\n", file, thsound::verdictName(target.verdict));
        return 2;
    }

    printf("%s: %d genes, self-distance %.4f\n\n", file, (int)genes.size(),
           score(ex, target, source));

    printf("%-28s %9s ", "gene", "value");

    for (int s = -PROBE_STEPS; s <= PROBE_STEPS; s++)
        if (s)
            printf(" %+5d", s);

    printf("\n");

    int smooth = 0, rugged = 0, deaf = 0;

    for (size_t g = 0; g < genes.size(); g++)
    {
        if (!genes[g].choices.empty())
            continue;

        double d[2 * PROBE_STEPS + 1];
        bool refused[2 * PROBE_STEPS + 1];
        double furthest = 0;
        bool monotone = true;

        for (int s = -PROBE_STEPS; s <= PROBE_STEPS; s++)
        {
            string candidate = source;
            const int k = s + PROBE_STEPS;

            d[k] = 0;

            /* An edit the writer refuses is no measurement, which is not
               the same as a measurement of zero. */
            refused[k] = s && !write(candidate, genes[g],
                                     moved(genes[g], genes[g].value, s));

            if (!s || refused[k])
                continue;

            Distance parts;

            d[k] = score(ex, target, candidate, &parts);

            if (d[k] < DEAD)
                d[k] = probeTerm == "spectral" ? parts.spectral
                     : probeTerm == "envelope" ? parts.envelope
                     : d[k];

            if (d[k] > furthest)
                furthest = d[k];
        }

        /* Further should be no nearer. A tenth of a dB of slack: a range
           wall makes two steps the same step, and that is not ruggedness. */
        for (int s = 1; s < PROBE_STEPS; s++)
            for (int side = -1; side <= 1; side += 2)
            {
                const int in = PROBE_STEPS + side * s;
                const int out = in + side;

                if (!refused[in] && !refused[out] && d[out] < d[in] - 0.1)
                    monotone = false;
            }

        const char *kind = furthest < 0.05 ? "deaf"
                         : (monotone ? "smooth" : "RUGGED");

        if (furthest < 0.05) deaf++;
        else if (monotone) smooth++;
        else rugged++;

        printf("%-28s %9.4g ", geneName(genes[g]).c_str(), genes[g].value);

        for (int s = -PROBE_STEPS; s <= PROBE_STEPS; s++)
            if (s)
            {
                if (refused[s + PROBE_STEPS])
                    printf("   n/a");
                else if (d[s + PROBE_STEPS] >= DEAD)
                    printf("  dead");
                else
                    printf(" %5.1f", d[s + PROBE_STEPS]);
            }

        printf("  %s\n", kind);
    }

    printf("\n%d smooth, %d rugged, %d deaf\n", smooth, rugged, deaf);

    /* A choice has no further and nearer, only elsewhere: how far each of
       the others is from the one the patch made. */
    for (size_t g = 0; g < genes.size(); g++)
    {
        if (genes[g].choices.empty())
            continue;

        printf("\n%s is %g; at", geneName(genes[g]).c_str(), genes[g].value);

        for (size_t c = 0; c < genes[g].choices.size(); c++)
        {
            string candidate = source;

            if (genes[g].choices[c] == genes[g].value)
                continue;

            if (!write(candidate, genes[g], genes[g].choices[c]))
                printf("  %g n/a", genes[g].choices[c]);
            else
            {
                const double d = score(ex, target, candidate);

                if (d >= DEAD)
                    printf("  %g dead", genes[g].choices[c]);
                else
                    printf("  %g %.1f", genes[g].choices[c], d);
            }
        }

        printf("\n");
    }

    return 0;
}

/* --------------------------------------------------------------- recover */

/* A separable CMA-ES over the quantities among `genes', from `mean' in
   their coordinates, towards `target'. Choice genes stay where `mean' has
   them. `base' is only the text the writes go into: every gene is rewritten
   every time. Leaves the best patch heard in `bestSource' and where its
   genes were in `bestAt'. `label' heads a printed trace, and no label is no
   trace. */
static double search (const Extractor &ex, const Features &target,
                      const string &base, const vector<Gene> &genes,
                      vector<double> mean, double sigma, std::mt19937 &rng,
                      int generations, const char *label,
                      string &bestSource, vector<double> &bestAt)
{
    const size_t n = genes.size();

    thcNormal normal;

    /* Hansen's defaults, with the learning rates of the diagonal raised by
       (n + 2) / 3 as Ros and Hansen's separable variant does: n variances
       are learned from what would otherwise teach n^2 / 2 covariances. */
    const int lambda = 16;
    const int mu = lambda / 2;

    vector<double> weight(mu);
    double weightSum = 0, weightSq = 0;

    for (int i = 0; i < mu; i++)
    {
        weight[i] = log(mu + 0.5) - log(i + 1.0);
        weightSum += weight[i];
    }

    for (int i = 0; i < mu; i++)
    {
        weight[i] /= weightSum;
        weightSq += weight[i] * weight[i];
    }

    /* N is the dimension CMA-ES's constants are functions of, and a patch
       of nothing but choices still has to divide by it. */
    vector<size_t> quantity;

    for (size_t g = 0; g < n; g++)
        if (genes[g].choices.empty())
            quantity.push_back(g);

    const double N = (double)std::max<size_t>(quantity.size(), 1);
    const double muEff = 1.0 / weightSq;
    const double cSigma = (muEff + 2) / (N + muEff + 5);
    const double dSigma = 1 + cSigma +
        2 * std::max(0.0, sqrt((muEff - 1) / (N + 1)) - 1);
    const double cC = (4 + muEff / N) / (N + 4 + 2 * muEff / N);
    const double boost = (N + 2) / 3;
    const double c1 = std::min(1.0, boost * 2 / ((N + 1.3) * (N + 1.3) + muEff));
    const double cMu = std::min(1 - c1,
        boost * 2 * (muEff - 2 + 1 / muEff) / ((N + 2) * (N + 2) + muEff));
    const double chiN = sqrt(N) * (1 - 1 / (4 * N) + 1 / (21 * N * N));

    vector<double> var(n, 1.0), pathSigma(n, 0.0), pathC(n, 0.0);

    bestSource = base;
    bestAt = mean;

    for (size_t g = 0; g < n; g++)
        write(bestSource, genes[g], fromU(genes[g], mean[g]));

    Distance parts;
    double best = score(ex, target, bestSource, &parts);

    if (label)
    {
        printf("%s\n%5s %9s %9s %9s %6s\n", label, "gen", "best", "spectral",
               "envelope", "sigma");
        printf("%5d %9.3f %9.3f %9.3f\n", 0, best, parts.spectral, parts.envelope);
    }

    if (quantity.empty())
        return best;

    for (int gen = 1; gen <= generations && best > 0.01; gen++)
    {
        vector<vector<double> > at(lambda, mean);
        vector<string> sources(lambda, base);
        vector<double> scores;

        for (int k = 0; k < lambda; k++)
            for (size_t g = 0; g < n; g++)
            {
                if (!genes[g].choices.empty())
                {
                    write(sources[k], genes[g], fromU(genes[g], mean[g]));
                    continue;
                }

                const double u = mean[g] + sigma * sqrt(var[g]) * normal(rng);

                /* Where it landed, not where it was thrown: a sample past
                   a range wall is the wall, and the update should learn
                   from the point that was heard. */
                at[k][g] = toU(genes[g], fromU(genes[g], u));

                if (!write(sources[k], genes[g], fromU(genes[g], u)))
                    at[k][g] = mean[g];
            }

        scoreAll(ex, target, sources, scores);

        vector<std::pair<double, int> > order;

        for (int k = 0; k < lambda; k++)
            order.push_back(std::make_pair(scores[k], k));

        std::stable_sort(order.begin(), order.end());

        if (order[0].first < best)
        {
            best = order[0].first;
            bestSource = sources[order[0].second];
            bestAt = at[order[0].second];
        }

        double pathNorm = 0;
        vector<double> step(n, 0.0);

        for (size_t q = 0; q < quantity.size(); q++)
        {
            const size_t g = quantity[q];

            for (int i = 0; i < mu; i++)
                step[g] += weight[i] * (at[order[i].second][g] - mean[g]) / sigma;

            pathSigma[g] = (1 - cSigma) * pathSigma[g] +
                sqrt(cSigma * (2 - cSigma) * muEff) * step[g] / sqrt(var[g]);
            pathNorm += pathSigma[g] * pathSigma[g];
        }

        pathNorm = sqrt(pathNorm);

        const bool moving = pathNorm /
            sqrt(1 - pow(1 - cSigma, 2.0 * gen)) < (1.4 + 2 / (N + 1)) * chiN;

        for (size_t q = 0; q < quantity.size(); q++)
        {
            const size_t g = quantity[q];

            pathC[g] = (1 - cC) * pathC[g] +
                (moving ? sqrt(cC * (2 - cC) * muEff) * step[g] : 0.0);

            double rankMu = 0;

            for (int i = 0; i < mu; i++)
            {
                const double y = (at[order[i].second][g] - mean[g]) / sigma;

                rankMu += weight[i] * y * y;
            }

            var[g] = (1 - c1 - cMu) * var[g] +
                c1 * (pathC[g] * pathC[g] +
                      (moving ? 0.0 : cC * (2 - cC) * var[g])) +
                cMu * rankMu;

            mean[g] += sigma * step[g];
        }

        sigma *= exp(cSigma / dSigma * (pathNorm / chiN - 1));

        if (label && (gen % 10 == 0 || gen == generations))
        {
            score(ex, target, bestSource, &parts);
            printf("%5d %9.3f %9.3f %9.3f %6.2f\n", gen, best,
                   parts.spectral, parts.envelope, sigma);
        }
    }

    return best;
}

/* The whole search: each choice gene's alternatives raced against the
   incumbent, every one of them tuned for before it is judged, and then the
   quantities at length around the choices that won.

   The race comes first, from wherever the patch starts. Tuning the
   quantities beforehand was tried and is what loses: thirty generations
   fit them to the wrong waveform well enough that the right one cannot
   catch up in a trial, and the search keeps what it began with. A trial
   has to be long enough to tell, too -- at twelve generations the right
   choice and the wrong one finish within noise of each other, and at
   thirty they do not. */
static double searchAll (const Extractor &ex, const Features &target,
                         const string &base, const vector<Gene> &genes,
                         const vector<double> &start, unsigned int seed,
                         int generations, string &bestSource,
                         vector<double> &bestAt)
{
    const int TRIAL = 30, ROUNDS = 2;

    std::mt19937 rng(seed);

    bestAt = start;
    bestSource = base;

    for (size_t g = 0; g < genes.size(); g++)
        write(bestSource, genes[g], fromU(genes[g], start[g]));

    double best = score(ex, target, bestSource);

    printf("start %.3f\n", best);

    for (int round = 0; round < ROUNDS; round++)
    {
        bool changed = false;

        for (size_t g = 0; g < genes.size(); g++)
        {
            if (genes[g].choices.size() < 2)
                continue;

            const vector<double> from = bestAt;

            printf("\n%s:", geneName(genes[g]).c_str());

            for (size_t c = 0; c < genes[g].choices.size(); c++)
            {
                vector<double> trial = from, trialAt;
                string trialSource;

                trial[g] = (double)c;

                const double d = search(ex, target, base, genes, trial,
                                        round ? 1.0 : 2.0, rng, TRIAL, NULL,
                                        trialSource, trialAt);

                printf("  %g%s %.2f", genes[g].choices[c],
                       c == (size_t)lrint(from[g]) ? "*" : "", d);
                fflush(stdout);

                if (d < best)
                {
                    changed |= c != (size_t)lrint(from[g]);
                    best = d;
                    bestSource = trialSource;
                    bestAt = trialAt;
                }
            }

            printf("\n");
        }

        if (!changed)
            break;
    }

    string polished;
    vector<double> polishedAt;

    printf("\n");

    const double d = search(ex, target, base, genes, bestAt, 1.0, rng,
                            generations, "closing", polished, polishedAt);

    if (d < best)
    {
        best = d;
        bestSource = polished;
        bestAt = polishedAt;
    }

    return best;
}

static void report (const vector<Gene> &genes, const char *was,
                    const vector<double> &start, const vector<double> &found)
{
    printf("\n%-28s %10s %10s %10s\n", "gene", "truth", was, "found");

    for (size_t g = 0; g < genes.size(); g++)
        printf("%-28s %10.4g %10.4g %10.4g%s\n", geneName(genes[g]).c_str(),
               genes[g].value, fromU(genes[g], start[g]),
               fromU(genes[g], found[g]),
               !genes[g].choices.empty() ? "  choice"
                                         : (genes[g].octaves ? "  oct" : ""));
}

static int recover (const Extractor &ex, const char *file, unsigned int seed,
                    int generations, const string &prefix)
{
    string truth;
    vector<float> mono;
    vector<Gene> genes;
    Features target;

    if (!readFile(file, truth) || !render(truth, mono, &genes))
    {
        printf("%s: would not load\n", file);
        return 2;
    }

    ex.extract(mono, target);

    if (!target.usable() || genes.empty())
    {
        printf("%s: nothing to search\n", file);
        return 2;
    }

    /* A generator of its own for the scramble, so the same seed scrambles
       the same way whatever the search then draws. */
    std::mt19937 rng(seed ^ 0x5eed);
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);

    vector<double> start(genes.size());
    string scrambled = truth;

    for (size_t g = 0; g < genes.size(); g++)
    {
        start[g] = genes[g].choices.empty()
            ? toU(genes[g], moved(genes[g], genes[g].value,
                                  uniform(rng) * SCRAMBLE_STEPS))
            : (double)thcUniformIndex(rng, 0, genes[g].choices.size() - 1);

        write(scrambled, genes[g], fromU(genes[g], start[g]));
    }

    printf("%s: %d genes scrambled, seed %u\n\n", file, (int)genes.size(), seed);

    string found;
    vector<double> foundAt;

    const double d = searchAll(ex, target, truth, genes, start, seed,
                               generations, found, foundAt);

    report(genes, "scrambled", start, foundAt);
    printf("\nfound %.3f\n", d);

    if (!prefix.empty())
    {
        save(prefix + "-target", truth);
        save(prefix + "-start", scrambled);
        save(prefix + "-found", found);
    }

    return 0;
}

/* ----------------------------------------------------------------- match */

static int match (const Extractor &ex, const char *wav, const char *file,
                  unsigned int seed, int generations, const string &prefix)
{
    string source, why;
    vector<float> heard, mono;
    vector<Gene> genes;
    Features target;

    if (!readWav(wav, heard, why))
    {
        printf("%s: %s\n", wav, why.c_str());
        return 2;
    }

    if (!readFile(file, source) || !render(source, mono, &genes) || genes.empty())
    {
        printf("%s: would not load, or has nothing to search\n", file);
        return 2;
    }

    /* As long as a render, so both have the same frames: a longer target
       is cut at the end of the release and a shorter one is followed by
       silence, which is what followed it. */
    heard.resize(mono.size(), 0.0f);
    ex.extract(heard, target);

    if (!target.usable())
    {
        printf("%s: %s\n", wav, thsound::verdictName(target.verdict));
        return 2;
    }

    vector<double> start(genes.size());

    for (size_t g = 0; g < genes.size(); g++)
        start[g] = toU(genes[g], genes[g].value);

    printf("%s towards %s: %d genes, note %d, seed %u\n\n", file, wav,
           (int)genes.size(), note, seed);

    string found;
    vector<double> foundAt;

    const double d = searchAll(ex, target, source, genes, start, seed,
                               generations, found, foundAt);

    report(genes, "start", start, foundAt);
    printf("\nfound %.3f\n", d);

    if (!prefix.empty())
    {
        writeWav(prefix + "-target.wav", heard);
        save(prefix + "-start", source);
        save(prefix + "-found", found);
    }

    return 0;
}

/* ------------------------------------------------------------------ grow */

/* What may be spliced into a wire: everything in the categories that take a
   signal and give one back. The way in is `in' where a plugin has one and
   its first input where it does not. */
static void buildPool (vector<Splice> &pool)
{
    static const char *const CATEGORIES[] = { "filt", "dist", "delay", "dyn" };

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    thPluginManager *pm = synth.getPluginManager();
    NodeCatalog catalog;

    if (pm == NULL || catalog.scan(pm->pluginPath()) == 0)
        return;

    for (size_t c = 0; c < sizeof CATEGORIES / sizeof CATEGORIES[0]; c++)
    {
        const vector<NodeCatalog::Entry> &list = catalog.inCategory(CATEGORIES[c]);

        for (size_t e = 0; e < list.size(); e++)
        {
            thPlugin *plugin = pm->getOrLoadPlugin(string(CATEGORIES[c]) + "/" +
                                                   list[e].name);
            NodeCatalog::Entry entry;

            if (plugin == NULL || !catalog.describe(list[e].spelling, pm, entry))
                continue;

            Splice sp;
            vector<string> outs;

            sp.spelling = entry.spelling;
            sp.name = entry.name;

            for (size_t i = 0; i < entry.ports.size(); i++)
            {
                const NodeCatalog::Port &port = entry.ports[i];

                if (!port.isInput)
                    outs.push_back(port.name);
                else if (sp.in.empty() || port.name == "in")
                    sp.in = port.name;
            }

            if (sp.in.empty() || outs.empty())
                continue;

            /* Every other input starts at the plugin's default where it
               has one and in the middle of its range where it has only
               that -- the geometric middle of a range that starts above
               zero, since that is a range of frequencies. */
            for (int i = 0; i < plugin->argCount(); i++)
            {
                const string name = plugin->getArgName(i);
                bool isInput = false;

                for (size_t k = 0; k < entry.ports.size(); k++)
                    isInput |= entry.ports[k].isInput && entry.ports[k].name == name;

                if (!isInput || name == sp.in)
                    continue;

                const double lo = plugin->getArgMin(i), hi = plugin->getArgMax(i);
                double v = plugin->getArgDefault(i);

                if (v == 0 && plugin->argHasRange(i) && hi > lo &&
                    plugin->getArgValues(i).empty())
                    v = lo > 0 ? sqrt(lo * hi) : (lo + hi) * 0.5;

                /* And one with neither, by what it is measured in: a
                   frequency in the middle of what is heard, a ratio that
                   changes nothing. A saturator whose drive is not written
                   down has no drive to search. */
                if (v == 0 && !plugin->argHasRange(i) &&
                    plugin->getArgValues(i).empty())
                {
                    const string units = plugin->getArgUnits(i);

                    v = units == "Hz" ? 1000 : units == "ratio" ? 1 : 0;
                }

                if (v != 0 && std::isfinite(v))
                    sp.initial.push_back(std::make_pair(name, v));
            }

            /* One splice per way out. A state-variable filter is three
               filters, and which of them a patch wanted is not something
               tuning can find afterwards. */
            for (size_t o = 0; o < outs.size(); o++)
            {
                sp.out = outs[o];
                pool.push_back(sp);
            }
        }
    }
}

static int showPool (void)
{
    vector<Splice> pool;

    buildPool(pool);

    for (size_t i = 0; i < pool.size(); i++)
    {
        printf("%-18s %s -> %s ", pool[i].spelling.c_str(),
               pool[i].in.c_str(), pool[i].out.c_str());

        for (size_t k = 0; k < pool[i].initial.size(); k++)
            printf("  %s=%g", pool[i].initial[k].first.c_str(),
                   pool[i].initial[k].second);

        printf("\n");
    }

    return pool.empty() ? 2 : 0;
}

/* `sp' spliced into `wire'. */
static bool splice (string &source, const Shape &shape, const Wire &wire,
                    const Splice &sp, string &name)
{
    string why;

    name = NodeCatalog::suggestName(sp.name, shape.names);

    return NodeEdit::Text::addNode(source, name, sp.spelling, sp.initial, why) == NodeEdit::OK &&
           NodeEdit::Text::connect(source, name, sp.in, wire.srcNode, wire.srcPort, why) == NodeEdit::OK &&
           NodeEdit::Text::connect(source, wire.node, wire.arg, name, sp.out, why) == NodeEdit::OK;
}

struct Candidate {
    string what;
    string source;
    vector<Gene> genes;
    vector<double> at;
    double score;

    bool operator< (const Candidate &o) const { return score < o.score; }
};

/* `generations' more of tuning for a candidate, from where it stands. */
static void tune (const Extractor &ex, const Features &target, Candidate &c,
                  std::mt19937 &rng, int generations)
{
    string found;
    vector<double> at;

    const double d = search(ex, target, c.source, c.genes, c.at, 1.0, rng,
                            generations, NULL, found, at);

    if (d < c.score)
    {
        c.score = d;
        c.source = found;
        c.at = at;
    }
}

static bool candidate (const string &source, const string &what, Candidate &c,
                       Shape *shape = NULL)
{
    vector<float> mono;

    c.what = what;
    c.source = source;
    c.genes.clear();
    c.score = DEAD;

    if (!render(source, mono, &c.genes, shape) || c.genes.empty())
        return false;

    c.at.resize(c.genes.size());

    for (size_t g = 0; g < c.genes.size(); g++)
        c.at[g] = toU(c.genes[g], c.genes[g].value);

    return true;
}

static int grow (const Extractor &ex, const Features &target,
                 const string &start, unsigned int seed, int generations,
                 int rounds, string &found)
{
    const int HEAT = 8, FINAL = 30, FINALISTS = 6;

    /* An edit has to be a tenth better and a quarter of a dB better, and
       under half a dB there is nothing left for a node to explain. The
       ratio alone is not enough: at 0.2 dB a tenth is 0.02, which is what
       two runs of the same tuning differ by, and the first version of this
       kept a resonant filter on the `play' gate for it. */
    const double RATIO = 0.9, ABSOLUTE = 0.25, CLOSE = 0.5;

    vector<Splice> pool;
    std::mt19937 rng(seed);

    buildPool(pool);

    Candidate best;

    if (!candidate(start, "as it stands", best))
        return 2;

    best.score = score(ex, target, best.source);
    printf("start %.3f\n", best.score);

    tune(ex, target, best, rng, FINAL);
    printf("tuned %.3f\n", best.score);

    for (int round = 1; round <= rounds && best.score > CLOSE; round++)
    {
        Shape shape;
        Candidate stands;

        candidate(best.source, "as it stands", stands, &shape);
        stands.score = best.score;

        vector<Candidate> heat;

        for (size_t w = 0; w < shape.wires.size(); w++)
            for (size_t p = 0; p < pool.size(); p++)
            {
                string source = best.source, name;
                Candidate c;

                if (!splice(source, shape, shape.wires[w], pool[p], name))
                    continue;

                const string what = pool[p].spelling + "->" + pool[p].out + " into " +
                    shape.wires[w].node + "." + shape.wires[w].arg + " = " +
                    shape.wires[w].srcNode + "->" + shape.wires[w].srcPort;

                if (candidate(source, what, c))
                    heat.push_back(c);
            }

        printf("\nround %d: %d wires, %d splices, %d candidates\n", round,
               (int)shape.wires.size(), (int)pool.size(), (int)heat.size());

        for (size_t i = 0; i < heat.size(); i++)
        {
            heat[i].score = score(ex, target, heat[i].source);
            tune(ex, target, heat[i], rng, HEAT);
        }

        std::stable_sort(heat.begin(), heat.end());

        if (heat.size() > (size_t)FINALISTS)
            heat.resize(FINALISTS);

        /* The graph as it stands runs the final too. Without that an edit
           that does nothing wins on the extra tuning. */
        tune(ex, target, stands, rng, HEAT + FINAL);

        for (size_t i = 0; i < heat.size(); i++)
        {
            const double after = heat[i].score;

            tune(ex, target, heat[i], rng, FINAL);
            printf("  %6.3f -> %6.3f  %s\n", after, heat[i].score,
                   heat[i].what.c_str());
        }

        printf("  %6s    %6.3f  as it stands\n", "", stands.score);

        std::stable_sort(heat.begin(), heat.end());

        if (heat.empty() || heat[0].score > stands.score * RATIO ||
            heat[0].score > stands.score - ABSOLUTE)
        {
            best = stands;
            printf("nothing wins by enough\n");
            break;
        }

        best = heat[0];
        printf("kept: %s\n", best.what.c_str());
    }

    printf("\n");

    vector<double> at;

    const double d = searchAll(ex, target, best.source, best.genes, best.at,
                               seed, generations, found, at);

    printf("\nfound %.3f\n", d);

    return 0;
}

static int growTowards (const Extractor &ex, const char *wav, const char *file,
                        unsigned int seed, int generations, int rounds,
                        const string &prefix)
{
    string source, why, found;
    vector<float> heard, mono;
    Features target;

    if (!readWav(wav, heard, why))
    {
        printf("%s: %s\n", wav, why.c_str());
        return 2;
    }

    if (!readFile(file, source) || !render(source, mono))
    {
        printf("%s: would not load\n", file);
        return 2;
    }

    heard.resize(mono.size(), 0.0f);
    ex.extract(heard, target);

    if (!target.usable())
    {
        printf("%s: %s\n", wav, thsound::verdictName(target.verdict));
        return 2;
    }

    printf("%s towards %s: note %d, seed %u\n\n", file, wav, note, seed);

    const int rc = grow(ex, target, source, seed, generations, rounds, found);

    if (rc == 0 && !prefix.empty())
    {
        writeWav(prefix + "-target.wav", heard);
        save(prefix + "-start", source);
        save(prefix + "-found", found);
    }

    return rc;
}

int main (int argc, char **argv)
{
    unsigned int seed = 1;
    int generations = 150;
    int rounds = 3;
    string prefix;
    int i = 1;

    for (; i < argc && argv[i][0] == '-'; i++)
    {
        if (!strcmp(argv[i], "-p") && i + 1 < argc) pluginPath = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) seed = (unsigned int)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-g") && i + 1 < argc) generations = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) rounds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) note = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "-w") && i + 1 < argc) HOLD_WINDOWS = std::max(1, atoi(argv[++i]));
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) probeTerm = argv[++i];
        else break;
    }

    if (pluginPath.empty() || i >= argc ||
        (i + 1 >= argc && strcmp(argv[i], "pool")))
    {
        printf("usage: %s -p PLUGINS [options] near file.dsp ...\n"
               "       %s -p PLUGINS [options] [-t spectral|envelope] probe file.dsp\n"
               "       %s -p PLUGINS [options] [-o PREFIX] recover file.dsp\n"
               "       %s -p PLUGINS [options] [-o PREFIX] match target.wav file.dsp\n"
               "       %s -p PLUGINS [options] [-o PREFIX] [-r ROUNDS] grow target.wav file.dsp\n"
               "       %s -p PLUGINS pool\n"
               "  -n NOTE         the note played, 60 unless given\n"
               "  -w HOLD         how long it is held, in windows of 1024 frames\n"
               "  -s SEED  -g GENERATIONS\n"
               "  -o PREFIX       write PREFIX-target, -start and -found, as .wav\n"
               "                  and, where there is a patch, as .dsp\n",
               argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
        return 2;
    }

    if (pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    char scratch[] = "/tmp/dspmatch-XXXXXX.dsp";
    const int fd = mkstemps(scratch, 4);

    if (fd < 0)
    {
        perror("mkstemps");
        return 2;
    }

    close(fd);
    scratchPath = scratch;

    const Extractor ex(TH_DEFAULT_SAMPLES);
    const string mode = argv[i++];
    int rc = 2;

    if (mode == "near")
        rc = near(ex, argc - i, argv + i);
    else if (mode == "probe")
        rc = probe(ex, argv[i]);
    else if (mode == "recover")
        rc = recover(ex, argv[i], seed, generations, prefix);
    else if (mode == "pool")
        rc = showPool();
    else if (mode == "grow" && i + 1 < argc)
        rc = growTowards(ex, argv[i], argv[i + 1], seed, generations, rounds, prefix);
    else if (mode == "match" && i + 1 < argc)
        rc = match(ex, argv[i], argv[i + 1], seed, generations, prefix);

    unlink(scratch);

    return rc;
}
