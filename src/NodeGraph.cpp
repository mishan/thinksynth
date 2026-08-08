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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "config.h"

#include <stdio.h>
#include <algorithm>

#include "think.h"
#include "NodeGraph.h"

/* Geometry. Chosen so the median graph (13 nodes, 8 layers) fits a normal
   window without scrolling; the largest (56 nodes, 17 layers) will scroll. */
#define BOX_W        128.0
#define BOX_HEAD      20.0    /* title bar */
#define PORT_PITCH    14.0
#define BOX_PAD        6.0
#define LAYER_GAP     70.0
#define ROW_GAP       24.0
#define MARGIN        20.0

/* Orders box indices by the position layering gave them.
 *
 * A functor rather than a lambda: nothing in this tree asks for a standard --
 * no -std anywhere in configure.ac, the Makefiles or scripts/Makefile -- so
 * the whole build runs on whatever the compiler defaults to, and on an older
 * toolchain that is C++98. The rest of the node editor is careful to stay
 * inside that; this one line was not. */
namespace {
    struct ByOrder {
        const vector<NodeGraph::Box> &boxes;

        ByOrder (const vector<NodeGraph::Box> &b) : boxes(b) { }

        bool operator() (int a, int b) const
        {
            return boxes[a].order < boxes[b].order;
        }
    };
}

NodeGraph::NodeGraph (void)
    : width_(0), height_(0), layers_(0)
{
}

/* A port by name.
 *
 * Direction is a preference, not a requirement. Demanding it meant that a port
 * declared the wrong way -- a plugin built against the old header, where
 * everything defaults to ARG_IN, or any other mismatch between metadata and
 * use -- both failed to match *and* got a second port created beside it with
 * the same name, which then read as a valid input and hid the mistake. The
 * .dsp is the authority on what is connected; the registration is only the
 * authority on what to call it. */
int NodeGraph::findPort (const Box &b, const string &name, bool wantInput)
{
    int fallback = -1;

    for (size_t k = 0; k < b.ports.size(); k++)
        if (b.ports[k].name == name)
        {
            if (b.ports[k].isInput == wantInput)
                return (int)k;

            if (fallback < 0)
                fallback = (int)k;
        }

    return fallback;
}

/* Does this node's plugin call `name' internal state?
 *
 * Pass 1 deliberately leaves ARG_STATE off the boxes -- a delay line's ring
 * buffer is not something to wire. But a .dsp can name one anyway, and without
 * this the edge builder would helpfully invent a port for it and undo that
 * decision. 69 args across the plugins are state; no shipped .dsp references
 * one, so this is a guard against files yet to be written rather than against
 * the corpus. */
static bool isStateArg (thNode *n, const string &name)
{
    thPlugin *p = n ? n->plugin() : NULL;

    if (p == NULL)
        return false;

    for (int k = 0; k < p->argCount(); k++)
        if (p->getArgName(k) == name)
            return p->getArgDir(k) == thPlugin::ARG_STATE;

    return false;
}

/* Snapshots a node's args into display parameters.
 *
 * Every arg is listed, including the ones bound to another node or to a
 * channel arg -- seeing that `cutoff' is driven by `env->out' rather than by a
 * number is exactly what a panel is for, and hiding wired args would make the
 * list change shape as things get connected. */
void NodeGraph::collectParams (thSynthTree *tree, thNode *n, Box &b)
{
    thPlugin *p = n->plugin();

    const thArgMap &args = n->args();

    for (thArgMap::const_iterator a = args.begin(); a != args.end(); ++a)
    {
        thArg *arg = a->second;

        if (arg == NULL)
            continue;

        /* Plugin-internal state -- a delay line's ring buffer, an envelope's
           position. Not something anyone should be shown, let alone set. */
        bool isPort = false;
        bool isOutput = false;
        bool isState = false;

        if (p)
        {
            for (int k = 0; k < p->argCount(); k++)
                if (p->getArgName(k) == a->first)
                {
                    isState = (p->getArgDir(k) == thPlugin::ARG_STATE);
                    isPort = (p->getArgDir(k) == thPlugin::ARG_IN);
                    isOutput = (p->getArgDir(k) == thPlugin::ARG_OUT);
                    break;
                }
        }

        if (isState)
            continue;

        Param prm;

        prm.name = a->first;
        prm.label = arg->label();
        prm.units = arg->units();
        prm.comment = arg->comment();
        prm.min = arg->min();
        prm.max = arg->max();
        prm.isPort = isPort;
        prm.isOutput = isOutput;

        switch (arg->type())
        {
            case thArg::ARG_POINTER:
                prm.kind = Param::POINTER;
                prm.source = arg->nodePtrName() + "->" + arg->argPtrName();
                break;

            case thArg::ARG_CHANNEL:
            {
                prm.kind = Param::CHANARG;

                /* The chanarg's name lives in argPtrName_, not nodePtrName_ --
                   a channel arg has no node half. */
                const string &chan = arg->argPtrName();

                prm.source = "@" + chan;

                /* In most DSPs every meaningful setting is a chanarg, so a
                   panel that showed only "@cutoff" would be a list of names
                   with no numbers in it. The tree knows what @cutoff is. */
                thArg *ca = tree ? tree->getChanArg(chan) : NULL;

                if (ca && ca->type() == thArg::ARG_VALUE && ca->len() > 0)
                {
                    prm.value = (*ca)[0];
                    prm.hasValue = true;

                    /* The range and label belong to the chanarg too. */
                    if (ca->min() != 0 || ca->max() != 0)
                    {
                        prm.min = ca->min();
                        prm.max = ca->max();
                    }

                    if (prm.label.empty())
                        prm.label = ca->label();
                }

                break;
            }

            case thArg::ARG_NOTE:
                prm.kind = Param::NOTE;
                prm.source = "@" + arg->argPtrName();
                break;

            case thArg::ARG_VALUE:
            default:
                prm.kind = Param::VALUE;
                /* A multi-sample arg has no single value to show; the first
                   sample is a reasonable stand-in and len() says the rest. */
                prm.value = (arg->len() > 0) ? (*arg)[0] : 0.0f;
                prm.hasValue = true;
                break;
        }

        b.params.push_back(prm);
    }
}

bool NodeGraph::build (thSynthTree *tree)
{
    boxes_.clear();
    edges_.clear();
    byName_.clear();

    if (tree == NULL)
        return false;

    thNode *ionode = tree->IONode();

    const thSynthTree::NodeMap &nodes = tree->nodes();

    /* Pass 1: a box per node, with ports from its plugin.
     *
     * Ports come from the plugin's registration rather than from the args the
     * .dsp happens to bind, so an unconnected input still shows up and can be
     * wired. Nodes with no plugin (the io node) have no registration, so their
     * ports are recovered from their args below. */
    map<string, int> sourceOfIo;

    for (thSynthTree::NodeMap::const_iterator i = nodes.begin();
         i != nodes.end(); ++i)
    {
        thNode *n = i->second;

        if (n == NULL)
            continue;

        Box b;

        b.name = n->name();

        thPlugin *p = n->plugin();

        if (p)
        {
            /* thPluginManager stores a filesystem path
               ("plugins/misc/midi2freq.so"); show it the way the .dsp spells
               it, "misc::midi2freq", so the box matches the source. */
            string path = p->path();

            string::size_type dot = path.rfind(".");
            if (dot != string::npos)
                path = path.substr(0, dot);

            string::size_type slash = path.rfind('/');
            string::size_type prev = (slash == string::npos)
                                     ? string::npos
                                     : path.rfind('/', slash - 1);

            if (slash != string::npos && prev != string::npos)
                b.plugin = path.substr(prev + 1, slash - prev - 1) + "::" +
                           path.substr(slash + 1);
            else
                b.plugin = path;

            for (int k = 0; k < p->argCount(); k++)
            {
                if (!p->argIsPort(k))
                    continue;       /* plugin-internal state, not a port */

                Port port;

                port.name = p->getArgName(k);
                port.isInput = (p->getArgDir(k) == thPlugin::ARG_IN);
                port.x = port.y = 0;

                b.ports.push_back(port);
            }
        }

        collectParams(tree, n, b);

        boxes_.push_back(b);
        byName_[b.name] = (int)boxes_.size() - 1;
    }

    /* Pass 2: the io node has no plugin, so its ports come from its args.
     *
     * It is also split in two. Anything other nodes read from it (note,
     * velocity, trigger) belongs to a source box; anything it reads itself
     * (out0, out1, play, channels) belongs to a sink box. */
    if (ionode)
    {
        map<string, int>::iterator found = byName_.find(ionode->name());

        if (found != byName_.end())
        {
            Box &sink = boxes_[found->second];

            sink.isIoSink = true;
            sink.plugin = "audio out";

            const thArgMap &args = ionode->args();

            for (thArgMap::const_iterator a = args.begin(); a != args.end(); ++a)
            {
                if (a->second == NULL)
                    continue;

                Port port;

                port.name = a->first;
                port.isInput = true;
                port.x = port.y = 0;

                sink.ports.push_back(port);
            }

            /* The source half. Its ports are filled in during edge building,
               as we discover which of the io node's args others read. */
            Box src;

            src.name = ionode->name();
            src.plugin = "midi in";
            src.isIoSource = true;

            boxes_.push_back(src);
            sourceOfIo[ionode->name()] = (int)boxes_.size() - 1;
        }
    }

    /* Pass 3: edges. An ARG_POINTER arg is a wire from another node's arg. */
    for (thSynthTree::NodeMap::const_iterator i = nodes.begin();
         i != nodes.end(); ++i)
    {
        thNode *n = i->second;

        if (n == NULL)
            continue;

        map<string, int>::iterator dst = byName_.find(n->name());

        if (dst == byName_.end())
            continue;

        const thArgMap &args = n->args();

        for (thArgMap::const_iterator a = args.begin(); a != args.end(); ++a)
        {
            thArg *arg = a->second;

            if (arg == NULL || arg->type() != thArg::ARG_POINTER)
                continue;

            map<string, int>::iterator srcIt = byName_.find(arg->nodePtrName());

            if (srcIt == byName_.end())
                continue;       /* dangling reference; the parser warns */

            int fromBox = srcIt->second;

            /* Reading from the io node means reading its *source* half. */
            map<string, int>::iterator ios = sourceOfIo.find(arg->nodePtrName());

            if (ios != sourceOfIo.end())
                fromBox = ios->second;

            Edge e;

            e.fromBox = fromBox;
            e.toBox = dst->second;

            /* Find, or failing that create, the ports at each end. */
            thSynthTree::NodeMap::const_iterator srcNode =
                nodes.find(arg->nodePtrName());

            /* A reference to something the plugin calls internal state is a
               mistake in the .dsp, not an undeclared port. Inventing a port
               for it would put a delay line's ring buffer on the canvas as
               something wireable. */
            if (isStateArg(n, a->first) ||
                (srcNode != nodes.end() &&
                 isStateArg(srcNode->second, arg->argPtrName())))
            {
                fprintf(stderr, "%s.%s: refers to plugin-internal state; "
                        "not drawing it\n", n->name().c_str(),
                        a->first.c_str());
                continue;
            }

            Box &fb = boxes_[e.fromBox];

            e.fromPort = findPort(fb, arg->argPtrName(), false);

            /* An output the plugin never registered still exists as far as the
               .dsp is concerned, and dropping the wire would be worse than
               showing a port that no registration backs. Three plugins create
               their outputs by string lookup in the callback -- moog's out_low
               among them -- and this is what keeps those wires drawn. The io
               source half discovers every one of its outputs this way too. */
            if (e.fromPort < 0)
            {
                Port port;

                port.name = arg->argPtrName();
                port.isInput = false;
                port.x = port.y = 0;

                fb.ports.push_back(port);
                e.fromPort = (int)fb.ports.size() - 1;
            }

            Box &tb = boxes_[e.toBox];

            e.toPort = findPort(tb, a->first, true);

            /* An arg bound in the .dsp that the plugin never registered still
               deserves a port -- better to show the wiring than hide it. */
            if (e.toPort < 0)
            {
                Port port;

                port.name = a->first;
                port.isInput = true;
                port.x = port.y = 0;

                tb.ports.push_back(port);
                e.toPort = (int)tb.ports.size() - 1;
            }

            /* An io node arg that something reads -- note, velocity, trigger
               -- is an output of the MIDI source, whatever the io node's own
               (nonexistent) plugin would say. Without this the panel would
               cheerfully offer to set `ionode.note' to a number, which is not
               a thing that can be done to incoming MIDI. */
            if (ios != sourceOfIo.end())
            {
                Box &io = boxes_[srcIt->second];

                for (size_t q = 0; q < io.params.size(); q++)
                    if (io.params[q].name == arg->argPtrName())
                        io.params[q].isOutput = true;
            }

            if (e.fromPort >= 0 && e.toPort >= 0)
                edges_.push_back(e);
        }
    }

    return true;
}

/* Longest-path layering, with a depth-first pass first to spot back edges.
 *
 * 87 of the 92 shipped DSPs are acyclic once the io node is split. The other
 * five are real feedback loops; their back edges are marked and then ignored
 * for layering, which is the standard way to keep a layered drawing readable
 * without pretending the cycle is not there. */
void NodeGraph::assignLayers (void)
{
    const int n = (int)boxes_.size();

    vector< vector<int> > out(n);       /* box -> outgoing edge indices */

    for (size_t e = 0; e < edges_.size(); e++)
        out[edges_[e].fromBox].push_back((int)e);

    /* colour: 0 unvisited, 1 on the stack, 2 done */
    vector<int> colour(n, 0);
    vector<int> stack;

    for (int start = 0; start < n; start++)
    {
        if (colour[start] != 0)
            continue;

        /* iterative DFS: the largest graph is 56 nodes, but recursion here
           would still be a needless stack risk */
        vector< pair<int, size_t> > work;

        work.push_back(make_pair(start, (size_t)0));
        colour[start] = 1;

        while (!work.empty())
        {
            int u = work.back().first;
            size_t &k = work.back().second;

            if (k < out[u].size())
            {
                Edge &e = edges_[out[u][k]];

                k++;

                if (colour[e.toBox] == 1)
                    e.feedback = true;          /* back edge */
                else if (colour[e.toBox] == 0)
                {
                    colour[e.toBox] = 1;
                    work.push_back(make_pair(e.toBox, (size_t)0));
                }
            }
            else
            {
                colour[u] = 2;
                work.pop_back();
            }
        }
    }

    /* longest path over the acyclic remainder */
    vector<int> indeg(n, 0);

    for (size_t e = 0; e < edges_.size(); e++)
        if (!edges_[e].feedback)
            indeg[edges_[e].toBox]++;

    vector<int> queue;

    for (int i = 0; i < n; i++)
    {
        boxes_[i].layer = 0;

        if (indeg[i] == 0)
            queue.push_back(i);
    }

    for (size_t q = 0; q < queue.size(); q++)
    {
        int u = queue[q];

        for (size_t k = 0; k < out[u].size(); k++)
        {
            Edge &e = edges_[out[u][k]];

            if (e.feedback)
                continue;

            if (boxes_[e.toBox].layer < boxes_[u].layer + 1)
                boxes_[e.toBox].layer = boxes_[u].layer + 1;

            if (--indeg[e.toBox] == 0)
                queue.push_back(e.toBox);
        }
    }

    layers_ = 0;

    for (int i = 0; i < n; i++)
        layers_ = max(layers_, boxes_[i].layer + 1);
}

/* Orders boxes within each layer to reduce crossings.
 *
 * A couple of sweeps of the barycentre heuristic -- put each box at the
 * average position of what it connects to. Not optimal, nowhere near, but it
 * is the difference between "readable" and "spaghetti" and costs almost
 * nothing at these sizes. */
void NodeGraph::orderWithinLayers (void)
{
    const int n = (int)boxes_.size();

    vector< vector<int> > inLayer(layers_);

    for (int i = 0; i < n; i++)
        inLayer[boxes_[i].layer].push_back(i);

    for (int l = 0; l < layers_; l++)
        for (size_t k = 0; k < inLayer[l].size(); k++)
            boxes_[inLayer[l][k]].order = (int)k;

    for (int sweep = 0; sweep < 4; sweep++)
    {
        bool forward = (sweep % 2) == 0;

        for (int li = 0; li < layers_; li++)
        {
            int l = forward ? li : layers_ - 1 - li;

            vector< pair<double, int> > key;

            for (size_t k = 0; k < inLayer[l].size(); k++)
            {
                int b = inLayer[l][k];
                double sum = 0;
                int cnt = 0;

                for (size_t e = 0; e < edges_.size(); e++)
                {
                    if (edges_[e].feedback)
                        continue;

                    if (forward && edges_[e].toBox == b)
                    {
                        sum += boxes_[edges_[e].fromBox].order;
                        cnt++;
                    }
                    else if (!forward && edges_[e].fromBox == b)
                    {
                        sum += boxes_[edges_[e].toBox].order;
                        cnt++;
                    }
                }

                key.push_back(make_pair(cnt ? sum / cnt : (double)boxes_[b].order,
                                        b));
            }

            sort(key.begin(), key.end());

            for (size_t k = 0; k < key.size(); k++)
            {
                inLayer[l][k] = key[k].second;
                boxes_[key[k].second].order = (int)k;
            }
        }
    }
}

void NodeGraph::placePorts (Box &b)
{
    int ins = 0, outs = 0;

    for (size_t k = 0; k < b.ports.size(); k++)
        if (b.ports[k].isInput) ins++; else outs++;

    b.w = BOX_W;
    b.h = BOX_HEAD + BOX_PAD * 2 + PORT_PITCH * max(max(ins, outs), 1);

    int i = 0, o = 0;

    for (size_t k = 0; k < b.ports.size(); k++)
    {
        Port &p = b.ports[k];

        if (p.isInput)
        {
            p.x = 0;
            p.y = BOX_HEAD + BOX_PAD + PORT_PITCH * i + PORT_PITCH / 2;
            i++;
        }
        else
        {
            p.x = b.w;
            p.y = BOX_HEAD + BOX_PAD + PORT_PITCH * o + PORT_PITCH / 2;
            o++;
        }
    }
}

void NodeGraph::layout (void)
{
    if (boxes_.empty())
    {
        width_ = height_ = 0;
        layers_ = 0;
        return;
    }

    assignLayers();

    for (size_t i = 0; i < boxes_.size(); i++)
        placePorts(boxes_[i]);

    orderWithinLayers();

    /* Columns are as wide as their widest box; rows stack by height. */
    vector< vector<int> > inLayer(layers_);

    for (size_t i = 0; i < boxes_.size(); i++)
        inLayer[boxes_[i].layer].push_back((int)i);

    for (int l = 0; l < layers_; l++)
        sort(inLayer[l].begin(), inLayer[l].end(), ByOrder(boxes_));

    /* tallest column first, so the rest can be centred against it */
    double tallest = 0;

    for (int l = 0; l < layers_; l++)
    {
        double h = 0;

        for (size_t k = 0; k < inLayer[l].size(); k++)
            h += boxes_[inLayer[l][k]].h + ROW_GAP;

        tallest = max(tallest, h - ROW_GAP);
    }

    double x = MARGIN;

    for (int l = 0; l < layers_; l++)
    {
        double h = 0;

        for (size_t k = 0; k < inLayer[l].size(); k++)
            h += boxes_[inLayer[l][k]].h + ROW_GAP;

        h -= ROW_GAP;

        /* Centring keeps a one-box column beside the middle of a six-box one,
           which shortens the wires and stops the drawing looking top-heavy. */
        double y = MARGIN + (tallest - h) / 2.0;
        double widest = 0;

        for (size_t k = 0; k < inLayer[l].size(); k++)
        {
            Box &b = boxes_[inLayer[l][k]];

            b.x = x;
            b.y = y;

            y += b.h + ROW_GAP;
            widest = max(widest, b.w);
        }

        x += widest + LAYER_GAP;
    }

    width_ = x - LAYER_GAP + MARGIN;
    height_ = tallest + MARGIN * 2;
}

int NodeGraph::feedbackCount (void) const
{
    int n = 0;

    for (size_t e = 0; e < edges_.size(); e++)
        if (edges_[e].feedback)
            n++;

    return n;
}

void NodeGraph::moveBox (int index, double x, double y)
{
    if (index < 0 || index >= (int)boxes_.size())
        return;

    boxes_[index].x = x;
    boxes_[index].y = y;
}

void NodeGraph::refreshExtent (void)
{
    double w = 0, h = 0;

    for (size_t i = 0; i < boxes_.size(); i++)
    {
        w = max(w, boxes_[i].x + boxes_[i].w);
        h = max(h, boxes_[i].y + boxes_[i].h);
    }

    width_ = w + MARGIN;
    height_ = h + MARGIN;
}

int NodeGraph::boxAt (double x, double y) const
{
    /* Backwards: the canvas draws in order, so the last box drawn is the one
       on top and should be the one picked. */
    for (int i = (int)boxes_.size() - 1; i >= 0; i--)
    {
        const Box &b = boxes_[i];

        if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h)
            return i;
    }

    return -1;
}

void NodeGraph::portPos (int box, int port, double &x, double &y) const
{
    x = y = 0;

    if (box < 0 || box >= (int)boxes_.size())
        return;

    const Box &b = boxes_[box];

    if (port < 0 || port >= (int)b.ports.size())
        return;

    x = b.x + b.ports[port].x;
    y = b.y + b.ports[port].y;
}

bool NodeGraph::portAt (double x, double y, int &box, int &port,
                        double slack) const
{
    double best = slack * slack;
    bool found = false;

    for (int i = (int)boxes_.size() - 1; i >= 0; i--)
    {
        const Box &b = boxes_[i];

        /* A port handle sits on the box edge, so the search area has to
           straddle it rather than being clipped to the box. */
        if (x < b.x - slack || x > b.x + b.w + slack ||
            y < b.y - slack || y > b.y + b.h + slack)
            continue;

        for (size_t k = 0; k < b.ports.size(); k++)
        {
            const double dx = x - (b.x + b.ports[k].x);
            const double dy = y - (b.y + b.ports[k].y);
            const double d2 = dx * dx + dy * dy;

            if (d2 <= best)
            {
                best = d2;
                box = i;
                port = (int)k;
                found = true;
            }
        }
    }

    return found;
}

int NodeGraph::boxByName (const string &name) const
{
    map<string, int>::const_iterator f = byName_.find(name);

    return (f == byName_.end()) ? -1 : f->second;
}
