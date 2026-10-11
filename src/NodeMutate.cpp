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

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <map>

#include "think.h"
#include "thSoundFile.h"

#include "NodeCatalog.h"
#include "NodeEdit.h"
#include "NodeGraph.h"
#include "NodeMutate.h"

namespace NodeMutate {

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

int argIndex (thPlugin *plugin, const string &arg)
{
    for (int i = 0; plugin && i < plugin->argCount(); i++)
        if (plugin->getArgName(i) == arg)
            return i;

    return -1;
}

/* What `source', parsed as `tree', offers a search. See the header. */
void read (thSynthTree *tree, const string &source, vector<Gene> *genes,
           Shape *shape)
{
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

            if (box.plugin.compare(0, 5, "osc::") == 0)
                for (size_t a = 0; a < box.params.size(); a++)
                {
                    const NodeGraph::Param &param = box.params[a];
                    const size_t arrow = param.source.find("->");

                    if (param.name != "freq" || param.isExpr)
                        continue;

                    Voice v;

                    v.node = box.name;

                    if (param.kind == NodeGraph::Param::POINTER && arrow != string::npos)
                    {
                        v.freqNode = param.source.substr(0, arrow);
                        v.freqPort = param.source.substr(arrow + 2);
                    }
                    else if (param.kind == NodeGraph::Param::CHANARG &&
                             param.source.size() > 1)
                        v.freqControl = param.source.substr(1);
                    else
                        continue;

                    shape->voices.push_back(v);
                }

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

                /* Only a wire carrying a signal. A frequency, a gate or
                   an envelope goes through the same `->' and a filter on
                   it is a number filtered, which is not what the pool is
                   for. What a port carries is its units. */
                thNode *src = tree->findNode(w.srcNode);
                thPlugin *srcPlugin = src ? src->plugin() : NULL;
                const int srcIdx = argIndex(srcPlugin, w.srcPort);

                if (srcIdx < 0 || srcPlugin->getArgUnits(srcIdx) != "full scale")
                    continue;

                bool have = false;

                for (size_t i = 0; i < shape->wires.size(); i++)
                    have |= shape->wires[i].node == w.node &&
                            shape->wires[i].arg == w.arg;

                if (!have)
                    shape->wires.push_back(w);
            }
        }

        const thSynthTree::NodeMap &nodes = tree->nodes();

        for (thSynthTree::NodeMap::const_iterator n = nodes.begin();
             n != nodes.end(); ++n)
        {
            thNode *nd = n->second;

            if (nd == NULL || nd == tree->IONode())
                continue;

            thPlugin *plugin = nd->plugin();
            const thArgMap &args = nd->args();

            for (thArgMap::const_iterator a = args.begin(); a != args.end(); ++a)
            {
                thArg *arg = a->second;
                const int idx = argIndex(plugin, a->first);

                if (arg == NULL || idx < 0 || arg->len() != 1)
                    continue;

                /* An arg the file never wrote is at the plugin's default,
                   and a slot all the same: a saw's pulse width is where
                   PWM goes, and a patch with no PWM has no `pw' line. The
                   editor adds one. */
                const bool written =
                    NodeEdit::Text::find(source, n->first, a->first) == NodeEdit::OK;

                /* A choice, a count or a time is nothing to modulate; nor
                   is a signal input, which is what the pool is for. */
                if (!plugin->getArgValues(idx).empty() || plugin->getArgStep(idx) >= 1 ||
                    plugin->getArgUnits(idx) == "samples" ||
                    plugin->getArgUnits(idx) == "full scale")
                    continue;

                Slot sl;

                sl.node = n->first;
                sl.arg = a->first;
                sl.plugin = plugin->path();
                sl.lo = plugin->getArgMin(idx);
                sl.hi = plugin->getArgMax(idx);

                if (arg->type() == thArg::ARG_VALUE)
                {
                    sl.value = written ? (*arg)[0] : plugin->getArgDefault(idx);

                    if (sl.value == 0)
                        continue;
                }
                else if (!written)
                    continue;
                else if (arg->type() == thArg::ARG_CHANNEL)
                {
                    thArg *ctl = tree->getChanArg(arg->argPtrName());

                    if (ctl == NULL || ctl->len() != 1 || !ctl->valueNames().empty())
                        continue;

                    sl.control = arg->argPtrName();
                    sl.value = (*ctl)[0];
                }
                else
                    continue;

                sl.octaves = !plugin->argHasRange(idx) || sl.hi <= sl.lo ||
                             !plugin->getArgUnits(idx).empty();
                shape->slots.push_back(sl);
            }
        }
    }
}

/* A value in the gene's own coordinate, and back. Coming back clamps to
   the declared range, so going there again says where the wall was. */
double toU (const Gene &g, double v)
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

double fromU (const Gene &g, double u)
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

double moved (const Gene &g, double from, double du)
{
    return fromU(g, toU(g, from) + du);
}

bool write (string &source, const Gene &g, double value)
{
    string why;

    const NodeEdit::Result r = g.node.empty()
        ? NodeEdit::Text::setChanArg(source, g.arg, value, why)
        : NodeEdit::Text::setValue(source, g.node, g.arg, value, why);

    return r == NodeEdit::OK;
}

string geneName (const Gene &g)
{
    return g.node.empty() ? "@" + g.arg : g.node + "." + g.arg;
}

/* What may be spliced into a wire: everything in the categories that take a
   signal and give one back. The way in is `in' where a plugin has one and
   its first input where it does not. */
void buildPool (thPluginManager *pm, vector<Splice> &pool)
{
    static const char *const CATEGORIES[] = { "filt", "dist", "delay", "dyn" };

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

/* `sp' spliced into `wire'. */
bool splice (string &source, const Shape &shape, const Wire &wire,
                    const Splice &sp, string &name)
{
    string why;

    name = NodeCatalog::suggestName(sp.name, shape.names);

    return NodeEdit::Text::addNode(source, name, sp.spelling, sp.initial, why) == NodeEdit::OK &&
           NodeEdit::Text::connect(source, name, sp.in, wire.srcNode, wire.srcPort, why) == NodeEdit::OK &&
           NodeEdit::Text::connect(source, wire.node, wire.arg, name, sp.out, why) == NodeEdit::OK;
}

/* A second oscillator, at the pitch `voice' reads, summed into `wire':
   `kind' 0 is a square an octave down, 1 a saw in unison. Its waveform
   and its ratio are choice genes from then on, so what it starts as is
   only where the race begins. */
bool addVoice (string &source, Shape &shape, const Wire &wire,
                      const Voice &voice, int kind, string &what)
{
    string why;
    vector<pair<string, double> > init;
    const string osc = NodeCatalog::suggestName("voice", shape.names);

    shape.names.push_back(osc);

    const string sum = NodeCatalog::suggestName("sum", shape.names);

    shape.names.push_back(sum);

    init.push_back(std::make_pair(string("waveform"), kind ? 1.0 : 2.0));
    init.push_back(std::make_pair(string("mul"), kind ? 1.0 : 0.5));
    init.push_back(std::make_pair(string("amp"), 0.5));

    if (NodeEdit::Text::addNode(source, osc, "osc::simple", init, why) != NodeEdit::OK)
        return false;

    if (voice.freqControl.empty()
        ? NodeEdit::Text::connect(source, osc, "freq", voice.freqNode, voice.freqPort, why) != NodeEdit::OK
        : NodeEdit::Text::connectControl(source, osc, "freq", voice.freqControl, why) != NodeEdit::OK)
        return false;

    init.clear();

    return NodeEdit::Text::addNode(source, sum, "math::add", init, why) == NodeEdit::OK &&
           NodeEdit::Text::connect(source, sum, "in0", wire.srcNode, wire.srcPort, why) == NodeEdit::OK &&
           NodeEdit::Text::connect(source, sum, "in1", osc, "out", why) == NodeEdit::OK &&
           NodeEdit::Text::connect(source, wire.node, wire.arg, sum, "out", why) == NodeEdit::OK &&
           (what = string(kind ? "unison saw" : "sub square") + " like " + voice.node +
                   " into " + wire.node + "." + wire.arg, true);
}

/* An envelope or an LFO on `slot': three nodes, and the arg reads the
   last of them. `kind' 0 is env::ad, 1 is a sine osc::simple. The depth
   starts at the value itself for a quantity in octaves and at a quarter
   of the range otherwise, and is a constant the search then tunes, as
   are the base and the modulator's own args. */
bool modulate (string &source, Shape &shape, const vector<Slot> &slots,
                      int kind, string &what)
{
    const Slot &slot = slots[0];
    string why;
    vector<pair<string, double> > init;
    const string mod = NodeCatalog::suggestName(kind ? "lfo" : "env", shape.names);

    shape.names.push_back(mod);

    const string mul = NodeCatalog::suggestName("depth", shape.names);

    shape.names.push_back(mul);

    const string add = NodeCatalog::suggestName("base", shape.names);

    shape.names.push_back(add);

    if (kind == 0)
    {
        init.push_back(std::make_pair(string("a"), 0.0));
        init.push_back(std::make_pair(string("d"), TH_DEFAULT_SAMPLES * 0.2));
        init.push_back(std::make_pair(string("p"), 1.0));

        if (NodeEdit::Text::addNode(source, mod, "env::ad", init, why) != NodeEdit::OK)
            return false;
    }
    else
    {
        init.push_back(std::make_pair(string("freq"), 4.0));
        init.push_back(std::make_pair(string("waveform"), 0.0));

        if (NodeEdit::Text::addNode(source, mod, "osc::simple", init, why) != NodeEdit::OK)
            return false;
    }

    const double depth = slot.octaves ? fabs(slot.value) * (kind ? 0.25 : 1.0)
                                      : (slot.hi - slot.lo) * 0.25;

    init.clear();
    init.push_back(std::make_pair(string("in1"), depth));

    if (NodeEdit::Text::addNode(source, mul, "math::mul", init, why) != NodeEdit::OK ||
        NodeEdit::Text::connect(source, mul, "in0", mod, "out", why) != NodeEdit::OK)
        return false;

    init.clear();

    if (slot.control.empty())
        init.push_back(std::make_pair(string("in1"), slot.value));

    if (NodeEdit::Text::addNode(source, add, "math::add", init, why) != NodeEdit::OK ||
        NodeEdit::Text::connect(source, add, "in0", mul, "out", why) != NodeEdit::OK)
        return false;

    if (!slot.control.empty() &&
        NodeEdit::Text::connectControl(source, add, "in1", slot.control, why) != NodeEdit::OK)
        return false;

    /* Every slot in the group reads the one sum: seven pulse widths,
       one LFO. They have the same base by construction. */
    for (size_t i = 0; i < slots.size(); i++)
        if (NodeEdit::Text::connect(source, slots[i].node, slots[i].arg, add, "out", why) != NodeEdit::OK)
            return false;

    char count[32] = "";

    if (slots.size() > 1)
        snprintf(count, sizeof count, "all %d ", (int)slots.size());

    what = string(kind ? "lfo" : "env") + " on " + count +
           (slots.size() > 1 ? slot.arg : slot.node + "." + slot.arg) +
           (slot.control.empty() ? "" : " = @" + slot.control);

    return true;
}

/* A group of slots that one modulator can serve: the same arg at the
   same value on every node of one plugin. Given `one', its group. */
vector<Slot> siblings (const Shape &shape, const Slot &one)
{
    vector<Slot> group;

    for (size_t i = 0; i < shape.slots.size(); i++)
        if (shape.slots[i].plugin == one.plugin && shape.slots[i].arg == one.arg &&
            shape.slots[i].control == one.control &&
            shape.slots[i].value == one.value)
            group.push_back(shape.slots[i]);

    return group;
}

/* The selector genes of `nodes' that share an arg name: seven
   oscillators' waveforms. Empty if the nodes have none in common. */
vector<const Gene *> selectorsOf (const vector<Gene> &genes,
                                         const vector<Slot> &slots)
{
    vector<const Gene *> out;

    for (size_t g = 0; g < genes.size(); g++)
        if (genes[g].node == slots[0].node && !genes[g].choices.empty())
        {
            for (size_t i = 0; i < slots.size(); i++)
                for (size_t h = 0; h < genes.size(); h++)
                    if (genes[h].node == slots[i].node && genes[h].arg == genes[g].arg &&
                        genes[h].choices == genes[g].choices)
                        out.push_back(&genes[h]);

            if (out.size() == slots.size())
                return out;

            out.clear();
        }

    return out;
}

vector<Edit> edits (const string &source, const Shape &shape,
                    const vector<Gene> &genes, const vector<Splice> &pool,
                    int *voices)
{
    vector<Edit> out;

    for (size_t w = 0; w < shape.wires.size(); w++)
        for (size_t p = 0; p < pool.size(); p++)
        {
            Edit e = { "splice", "", source };
            string name;

            if (!splice(e.source, shape, shape.wires[w], pool[p], name))
                continue;

            e.what = pool[p].spelling + "->" + pool[p].out + " into " +
                shape.wires[w].node + "." + shape.wires[w].arg + " = " +
                shape.wires[w].srcNode + "->" + shape.wires[w].srcPort;

            out.push_back(e);
        }

    /* Each slot alone, and each group of siblings as one -- a group of one
       is the slot alone -- with and without the nodes' selector at each of
       its other values: a modulation on a pulse width is nothing on a saw. */
    for (size_t sl = 0; sl < shape.slots.size(); sl++)
    {
        const vector<Slot> group = siblings(shape, shape.slots[sl]);

        if (group[0].node != shape.slots[sl].node)
            continue;   /* the group runs from its first member */

        vector<vector<Slot> > forms;

        forms.push_back(vector<Slot>(1, shape.slots[sl]));

        if (group.size() > 1)
            forms.push_back(group);

        for (size_t f = 0; f < forms.size(); f++)
        {
            const vector<const Gene *> sel = selectorsOf(genes, forms[f]);
            const size_t values = sel.empty() ? 0 : sel[0]->choices.size();

            for (int kind = 0; kind < 2; kind++)
                for (size_t v = 0; v <= values; v++)
                {
                    Edit e = { "modulate", "", source };
                    Shape scratch = shape;
                    bool ok = true;

                    /* v == values is the selector as it stands. */
                    if (v < values && sel[0]->choices[v] == sel[0]->value)
                        continue;

                    for (size_t i = 0; v < values && i < sel.size(); i++)
                        ok &= write(e.source, *sel[i], sel[i]->choices[v]);

                    if (!ok || !modulate(e.source, scratch, forms[f], kind,
                                         e.what))
                        continue;

                    if (v < values)
                    {
                        char with[64];

                        snprintf(with, sizeof with, " with %s = %g",
                                 sel[0]->arg.c_str(), sel[0]->choices[v]);
                        e.what += with;
                    }

                    out.push_back(e);
                }
        }
    }

    /* One pitch source is one voice: seven detuned saws reading the same
       vibrato are one place to add a voice, not seven. */
    vector<Voice> pitches;

    for (size_t v = 0; v < shape.voices.size(); v++)
    {
        bool have = false;

        for (size_t k = 0; k < pitches.size(); k++)
            have |= pitches[k].freqNode == shape.voices[v].freqNode &&
                    pitches[k].freqPort == shape.voices[v].freqPort &&
                    pitches[k].freqControl == shape.voices[v].freqControl;

        if (!have)
            pitches.push_back(shape.voices[v]);
    }

    for (size_t w = 0; w < shape.wires.size(); w++)
        for (size_t v = 0; v < pitches.size(); v++)
            for (int kind = 0; kind < 2; kind++)
            {
                Edit e = { "voice", "", source };
                Shape scratch = shape;

                if (addVoice(e.source, scratch, shape.wires[w], pitches[v],
                             kind, e.what))
                    out.push_back(e);
            }

    if (voices != NULL)
        *voices = (int)pitches.size();

    return out;
}

/* How loud `text' is on `synth': the RMS and the peak of a phrase that
   plays it the way a piece does -- a note in each of four octaves, C2 to
   C5, each pressed before the last is let go, so a monophonic graph slides
   and a polyphonic one stacks, and then a release. A filter that rings at
   the bottom of the range, or a slide that kicks one, is in the measure.
   False for a graph that does not load or goes non-finite. */
static bool level (thSynth &synth, const string &text, double &rms,
                   double &peak)
{
    static unsigned int serial = 0;
    const string name = "grow-level#" + std::to_string(++serial) + ".dsp";
    thUtil::GraphHold hold = thUtil::addGraphText(name, text);

    /* A clean slate: the last candidate's tail is not this one's. */
    synth.removeChan(0);

    if (synth.loadTree(name, 0, 100) == NULL)
        return false;

    const int step = thsound::HOLD_WINDOWS / 2;
    const int windows = 4 * step + thsound::HOLD_WINDOWS +
                        thsound::TAIL_WINDOWS;
    const int len = synth.getWindowlen();
    double sum = 0;
    long count = 0;

    peak = 0;

    for (int w = 0; w < windows; w++)
    {
        if (w % step == 0 && w / step < 4)
        {
            synth.addNote(0, 36 + 12 * (w / step), 100);

            if (w > 0)
                synth.delNote(0, 36 + 12 * (w / step - 1));
        }

        if (w == 4 * step + thsound::HOLD_WINDOWS / 2)
            synth.delNote(0, 72);

        synth.process();

        const float *out = synth.getOutput();
        const int samples = len * synth.audioChannelCount();

        for (int i = 0; i < samples; i++)
        {
            if (!std::isfinite(out[i]))
                return false;

            sum += (double)out[i] * out[i];
            peak = std::max(peak, (double)std::fabs(out[i]));
        }

        count += samples;
    }

    synth.removeChan(0);

    rms = count ? sqrt(sum / count) : 0;

    return rms > 0;
}

bool grow (thSynth *synth, const string &text, const string &base,
           unsigned int choice, const string &kinds, string &grown,
           string &what, string &why)
{
    if (synth == NULL)
    {
        why = "no synth to parse the graph with";
        return false;
    }

    /* Parsed through the registry an inline graph uses, so nothing is
       written anywhere to be read back. */
    static unsigned int serial = 0;
    const string name = "grow#" + std::to_string(++serial) + ".dsp";
    thSynthTree *tree;

    {
        thUtil::GraphHold hold = thUtil::addGraphText(name, text);

        tree = synth->parseTree(name);
    }

    if (tree == NULL)
    {
        why = "the graph does not parse";
        return false;
    }

    vector<Gene> genes;
    Shape shape;
    vector<Splice> pool;

    read(tree, text, &genes, &shape);
    delete tree;

    buildPool(synth->getPluginManager(), pool);

    const vector<Edit> all = edits(text, shape, genes, pool);
    vector<const Edit *> allowed;

    /* A kind as a word of `kinds', not as a substring of it. */
    string words = " " + kinds + " ";

    std::replace(words.begin(), words.end(), ',', ' ');

    for (size_t e = 0; e < all.size(); e++)
        if (kinds.empty() || words.find(" " + all[e].kind + " ") != string::npos)
            allowed.push_back(&all[e]);

    if (allowed.empty())
    {
        why = "no edit of the kinds allowed fits this graph";
        return false;
    }

    /* A synth of its own, so the one playing is not touched. */
    thSynth ear(synth->getPluginManager()->pluginPath(),
                TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    double wantRms, wantPeak;

    if (!level(ear, base, wantRms, wantPeak))
    {
        why = "the declared graph makes no sound to measure against";
        return false;
    }

    /* Sixteen tries at most: each is a render, and a host may be asking
       from the thread that draws its window. */
    for (size_t k = 0; k < allowed.size() && k < 16; k++)
    {
        const Edit &pick = *allowed[(choice + k) % allowed.size()];
        double rms, peak;

        /* +3 dB and -12 dB on the RMS, and +3 dB on the peak. */
        if (level(ear, pick.source, rms, peak) && rms <= wantRms * 1.4125 &&
            rms >= wantRms * 0.2512 && peak <= wantPeak * 1.4125)
        {
            grown = pick.source;
            what = pick.what;
            return true;
        }
    }

    why = "the edits tried all move the level too far";
    return false;
}

}
