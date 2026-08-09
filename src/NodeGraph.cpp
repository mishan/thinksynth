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

/* Controls: the height of the slider row, and how far the track is inset from
   the box edges so the handle never overlaps the port. */
#define CTL_ROW       26.0
#define CTL_INSET     14.0
#define CTL_HANDLE     5.0

/* An attached control: a strip against its host's left edge. Narrower and
   much shorter than a node, because it carries a label, a track and a number
   and nothing else -- no title bar, no ports. */
#define ATTACH_W     120.0
#define ATTACH_H      22.0
#define ATTACH_GAP     3.0    /* between stacked strips           */
#define ATTACH_PAD    10.0    /* between the strip and its host   */

/* Orders box indices by the position layering gave them.
 *
 * A functor rather than a lambda. The build does now ask for C++11 -- see
 * configure.ac -- but it asks for it because think.h needs <atomic>, not
 * because anything wanted lambdas, and this file is the one piece of the node
 * editor deliberately kept independent of the engine. No reason to spend a
 * language feature on a three-line comparison. */
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

    /* Pass 3: a control box per top-level `@name' block.
     *
     * Created before edges so that pass 4 can wire chanarg references to them.
     * Every one of the 206 declarations in the corpus carries .widget = 1,
     * .min and .max, so there is no question of which ones deserve a slider --
     * declaring a chanarg *is* declaring a control. */
    map<string, int> controlOf;

    {
        const thArgMap &chan = tree->chanArgs();

        for (thArgMap::const_iterator a = chan.begin(); a != chan.end(); ++a)
        {
            thArg *arg = a->second;

            if (arg == NULL)
                continue;

            /* Only the ones declaring a widget.
             *
             * `name "TS-1"', `author' and `description' are stored as chanargs
             * too -- 110 of the 316 in the corpus -- and they are strings, not
             * knobs. Every one of the 206 real controls sets .widget; none of
             * the metadata does. The format already draws this line, so there
             * is no need to guess at it by name. */
            if (arg->widgetType() == thArg::HIDE)
                continue;

            Box b;

            b.isControl = true;
            b.ctlArg = a->first;
            b.name = "@" + a->first;
            b.ctlLabel = arg->label().empty() ? a->first : arg->label();
            b.ctlValue = (arg->len() > 0) ? (*arg)[0] : 0.0f;
            b.ctlMin = arg->min();
            b.ctlMax = arg->max();

            /* A range of nothing would make a slider that cannot move. Only
               33 of the 206 omit a label; none omit a range, but a patch
               written by hand might. */
            if (b.ctlMax <= b.ctlMin)
            {
                b.ctlMin = 0;
                b.ctlMax = (b.ctlValue > 1.0f) ? b.ctlValue * 2.0f : 1.0f;
            }

            b.plugin = "control";

            Port port;

            port.name = a->first;
            port.isInput = false;
            port.x = port.y = 0;

            b.ports.push_back(port);

            boxes_.push_back(b);
            controlOf[a->first] = (int)boxes_.size() - 1;
        }
    }

    /* Pass 4: edges. An ARG_POINTER arg is a wire from another node's arg;
       an ARG_CHANNEL arg is a wire from a control. */
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

            if (arg == NULL)
                continue;

            /* `in1 = @blim' is a connection from the @blim control, and
               drawing it as one is the whole point of having controls be
               nodes. */
            if (arg->type() == thArg::ARG_CHANNEL)
            {
                map<string, int>::iterator c =
                    controlOf.find(arg->argPtrName());

                if (c == controlOf.end())
                    continue;       /* reads a chanarg the file never declared */

                Edge ce;

                ce.fromBox = c->second;
                ce.fromPort = 0;
                ce.toBox = dst->second;

                Box &cb = boxes_[ce.toBox];

                ce.toPort = findPort(cb, a->first, true);

                if (ce.toPort < 0)
                {
                    Port port;

                    port.name = a->first;
                    port.isInput = true;
                    port.x = port.y = 0;

                    cb.ports.push_back(port);
                    ce.toPort = (int)cb.ports.size() - 1;
                }

                edges_.push_back(ce);
                continue;
            }

            if (arg->type() != thArg::ARG_POINTER)
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

    /* A control needs room under the title for its slider and readout. Its one
       port then sits beside the slider row rather than above it. */
    if (b.isControl)
        b.h = BOX_HEAD + BOX_PAD * 2 + CTL_ROW;

    /* An attached one is a strip instead: no title bar, no ports drawn, so
       it only needs the height of a slider row. layout() sets its position;
       the size has to be right before that, because the row heights it
       stacks are computed from it. */
    if (b.isControl && b.attachedTo >= 0)
    {
        b.w = ATTACH_W;
        b.h = ATTACH_H;

        /* The port keeps existing -- the edge is real and refers to it -- but
           it moves to the middle of the right edge, where the strip meets its
           host. The default placement puts it below a title bar this box does
           not have, which left it outside its own bounds. */
        for (size_t k = 0; k < b.ports.size(); k++)
        {
            b.ports[k].x = b.w;
            b.ports[k].y = b.h * 0.5;
        }

        return;
    }

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

/* Works out which controls hang off which box.
 *
 * A control with exactly one consumer belongs to that consumer and is drawn
 * against it. One with several is shared and stays a node; one with none is
 * not yet wired to anything and has nowhere to hang. */
void NodeGraph::assignAttachments (void)
{
    for (size_t b = 0; b < boxes_.size(); b++)
    {
        boxes_[b].attachedTo = -1;
        boxes_[b].attachSlot = 0;
    }

    for (size_t b = 0; b < boxes_.size(); b++)
    {
        if (!boxes_[b].isControl)
            continue;

        int consumers = 0;
        int host = -1;

        for (size_t e = 0; e < edges_.size(); e++)
            if (edges_[e].fromBox == (int)b)
            {
                consumers++;
                host = edges_[e].toBox;
            }

        /* Not itself, and not another control -- neither would give it
           anywhere sensible to sit. */
        if (consumers == 1 && host >= 0 && host != (int)b &&
            !boxes_[host].isControl)
            boxes_[b].attachedTo = host;
    }

    /* Slots, in box order, so the strip does not reshuffle between runs. */
    vector<int> used(boxes_.size(), 0);

    for (size_t b = 0; b < boxes_.size(); b++)
        if (boxes_[b].attachedTo >= 0)
            boxes_[b].attachSlot = used[boxes_[b].attachedTo]++;
}

bool NodeGraph::edgeIsImplied (int edge) const
{
    if (edge < 0 || edge >= (int)edges_.size())
        return false;

    const Edge &e = edges_[edge];

    if (e.fromBox < 0 || e.fromBox >= (int)boxes_.size())
        return false;

    return boxes_[e.fromBox].attachedTo == e.toBox;
}

/* The strip height for a host: its attached controls stacked. */
static double stripHeight (const vector<NodeGraph::Box> &boxes, int host)
{
    int n = 0;

    for (size_t b = 0; b < boxes.size(); b++)
        if (boxes[b].attachedTo == host)
            n++;

    if (n == 0)
        return 0;

    return n * ATTACH_H + (n - 1) * ATTACH_GAP;
}

void NodeGraph::layout (void)
{
    if (boxes_.empty())
    {
        width_ = height_ = 0;
        layers_ = 0;
        return;
    }

    assignAttachments();
    assignLayers();

    /* A control that several nodes share cannot attach to any one of them, so
     * it stays a box -- but longest-path layering puts it in layer 0, because
     * it has no inputs, which is as far from its consumers as the canvas
     * goes. @waveform in organ0.dsp drives four nodes in the middle of the
     * patch and was being drawn at the far left with four long wires crossing
     * everything.
     *
     * Moved to just before its earliest consumer. Nothing points at it, so
     * nothing can be made to point backwards by moving it right. */
    for (size_t b = 0; b < boxes_.size(); b++)
    {
        if (!boxes_[b].isControl || boxes_[b].attachedTo >= 0)
            continue;

        int earliest = -1;

        for (size_t e = 0; e < edges_.size(); e++)
            if (edges_[e].fromBox == (int)b)
            {
                const int l = boxes_[edges_[e].toBox].layer;

                if (earliest < 0 || l < earliest)
                    earliest = l;
            }

        if (earliest > 0)
            boxes_[b].layer = earliest - 1;
    }

    for (size_t i = 0; i < boxes_.size(); i++)
        placePorts(boxes_[i]);

    orderWithinLayers();

    /* Columns are as wide as their widest box; rows stack by height.
     *
     * An attached control is not a column member -- it rides along with its
     * host and is positioned afterwards. Leaving it in was what put thirteen
     * of them in a column down the left edge of ts1.dsp, taller than the
     * signal path they belonged to. */
    vector< vector<int> > inLayer(layers_);

    for (size_t i = 0; i < boxes_.size(); i++)
        if (boxes_[i].attachedTo < 0)
            inLayer[boxes_[i].layer].push_back((int)i);

    for (int l = 0; l < layers_; l++)
        sort(inLayer[l].begin(), inLayer[l].end(), ByOrder(boxes_));

    /* tallest column first, so the rest can be centred against it */
    double tallest = 0;

    for (int l = 0; l < layers_; l++)
    {
        double h = 0;

        for (size_t k = 0; k < inLayer[l].size(); k++)
            h += max(boxes_[inLayer[l][k]].h,
                     stripHeight(boxes_, inLayer[l][k])) + ROW_GAP;

        tallest = max(tallest, h - ROW_GAP);
    }

    double x = MARGIN;

    for (int l = 0; l < layers_; l++)
    {
        double h = 0;

        for (size_t k = 0; k < inLayer[l].size(); k++)
            h += max(boxes_[inLayer[l][k]].h,
                     stripHeight(boxes_, inLayer[l][k])) + ROW_GAP;

        h -= ROW_GAP;

        /* Centring keeps a one-box column beside the middle of a six-box one,
           which shortens the wires and stops the drawing looking top-heavy. */
        double y = MARGIN + (tallest - h) / 2.0;
        double widest = 0;

        /* The strip sits to the left of the host, inside the same column, so
           the column has to be wide enough for both. Computed before placing
           anything, or the first host would be flush against the previous
           column and its strip would overlap it. */
        double indent = 0;

        for (size_t k = 0; k < inLayer[l].size(); k++)
            if (stripHeight(boxes_, inLayer[l][k]) > 0)
                indent = ATTACH_W + ATTACH_PAD;

        for (size_t k = 0; k < inLayer[l].size(); k++)
        {
            Box &b = boxes_[inLayer[l][k]];

            const double rowH = max(b.h, stripHeight(boxes_, (int)(&b - &boxes_[0])));

            b.x = x + indent;

            /* Centre the box against its strip when the strip is taller, so
               a node with five controls does not sit at the top of them. */
            b.y = y + (rowH - b.h) / 2.0;

            y += rowH + ROW_GAP;
            widest = max(widest, indent + b.w);
        }

        x += widest + LAYER_GAP;
    }

    /* Now the strips, against the left edge of the host they belong to and
       centred on it vertically. */
    for (size_t b = 0; b < boxes_.size(); b++)
    {
        Box &c = boxes_[b];

        if (c.attachedTo < 0)
            continue;

        const Box &host = boxes_[c.attachedTo];
        const double sh = stripHeight(boxes_, c.attachedTo);

        c.w = ATTACH_W;
        c.h = ATTACH_H;
        c.x = host.x - ATTACH_PAD - ATTACH_W;
        c.y = host.y + (host.h - sh) / 2.0 +
              c.attachSlot * (ATTACH_H + ATTACH_GAP);
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

        /* An attached control draws no port: it touches its host, and the
           one edge it has is implied by that. Returning a port here would
           offer a wire that cannot be made and would sit under the strip's
           slider, which is what a click there actually means. */
        if (b.attachedTo >= 0)
            continue;

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

bool NodeGraph::canConnect (int fromBox, int fromPort, int toBox, int toPort,
                            string &why) const
{
    why.clear();

    if (fromBox < 0 || fromBox >= (int)boxes_.size() ||
        toBox < 0 || toBox >= (int)boxes_.size())
    { why = "no such node"; return false; }

    const Box &fb = boxes_[fromBox];
    const Box &tb = boxes_[toBox];

    if (fromPort < 0 || fromPort >= (int)fb.ports.size() ||
        toPort < 0 || toPort >= (int)tb.ports.size())
    { why = "no such port"; return false; }

    if (fb.ports[fromPort].isInput)
    { why = "wires start at an output"; return false; }

    if (!tb.ports[toPort].isInput)
    { why = "wires end at an input"; return false; }

    /* Box, not node: the io node's two halves share a name, and connecting
       the MIDI source into the audio sink is exactly what a DSP does. The
       message says "box" for the same reason -- phrased as a rule about nodes
       it would read as forbidding something three shipped DSPs do. */
    if (fromBox == toBox)
    { why = "a box cannot feed itself"; return false; }

    return true;
}

/* Horizontal-tangent cubic: wires leave an output rightwards and enter an
   input from the left, which reads as flow even where one doubles back. The
   control offset grows with distance so long wires bow more. */
void NodeGraph::edgeCurve (int edge, double *xs, double *ys) const
{
    for (int i = 0; i < 4; i++)
        xs[i] = ys[i] = 0;

    if (edge < 0 || edge >= (int)edges_.size())
        return;

    const Edge &e = edges_[edge];

    double x1, y1, x2, y2;

    portPos(e.fromBox, e.fromPort, x1, y1);
    portPos(e.toBox, e.toPort, x2, y2);

    double reach = (x2 - x1) * 0.5;

    if (reach < 30.0)
        reach = 30.0 + (x1 - x2) * 0.25;    /* a back edge needs a wider bow */

    xs[0] = x1;          ys[0] = y1;
    xs[1] = x1 + reach;  ys[1] = y1;
    xs[2] = x2 - reach;  ys[2] = y2;
    xs[3] = x2;          ys[3] = y2;
}

int NodeGraph::edgeAt (double x, double y, double slack) const
{
    int best = -1;
    double bestD = slack * slack;

    for (size_t e = 0; e < edges_.size(); e++)
    {
        /* Not drawn, so not clickable: an implied edge is the gap between a
           strip and the box it touches, and there is nothing there to hit. */
        if (edgeIsImplied((int)e))
            continue;

        double xs[4], ys[4];

        edgeCurve((int)e, xs, ys);

        /* A cubic stays inside the convex hull of its control points, so the
           bounding box of those four -- grown by the slack -- is a sound
           rejection test and costs four comparisons instead of 33 distance
           calculations. This runs on every pointer motion over a graph with
           up to 3094 wires, which is where it matters. */
        double bx0 = xs[0], bx1 = xs[0], by0 = ys[0], by1 = ys[0];

        for (int i = 1; i < 4; i++)
        {
            if (xs[i] < bx0) bx0 = xs[i];
            if (xs[i] > bx1) bx1 = xs[i];
            if (ys[i] < by0) by0 = ys[i];
            if (ys[i] > by1) by1 = ys[i];
        }

        if (x < bx0 - slack || x > bx1 + slack ||
            y < by0 - slack || y > by1 + slack)
            continue;

        /* Sampled rather than solved. Thirty-two points on a wire that is at
           most a few hundred pixels long puts them within a few pixels of each
           other, which is finer than the slack being tested against. */
        for (int i = 0; i <= 32; i++)
        {
            const double t = i / 32.0;
            const double u = 1.0 - t;

            const double bx = u*u*u*xs[0] + 3*u*u*t*xs[1] +
                              3*u*t*t*xs[2] + t*t*t*xs[3];
            const double by = u*u*u*ys[0] + 3*u*u*t*ys[1] +
                              3*u*t*t*ys[2] + t*t*t*ys[3];

            const double dx = bx - x, dy = by - y;
            const double d = dx * dx + dy * dy;

            if (d < bestD)
            {
                bestD = d;
                best = (int)e;
            }
        }
    }

    return best;
}

bool NodeGraph::connect (int fromBox, int fromPort, int toBox, int toPort,
                         string &why)
{
    if (!canConnect(fromBox, fromPort, toBox, toPort, why))
        return false;

    /* One right-hand side per arg: a second wire into the same input is not
       something the grammar can express, so this replaces rather than adds. */
    for (size_t e = 0; e < edges_.size(); e++)
        if (edges_[e].toBox == toBox && edges_[e].toPort == toPort)
        {
            edges_.erase(edges_.begin() + e);
            break;
        }

    Edge e;

    e.fromBox = fromBox;
    e.fromPort = fromPort;
    e.toBox = toBox;
    e.toPort = toPort;

    edges_.push_back(e);

    /* Keep the parameter snapshot honest, so the panel agrees with the canvas
       without waiting for a save and a reparse. */
    Box &tb = boxes_[toBox];

    const string src = boxes_[fromBox].name + "->" +
                       boxes_[fromBox].ports[fromPort].name;

    for (size_t k = 0; k < tb.params.size(); k++)
        if (tb.params[k].name == tb.ports[toPort].name)
        {
            tb.params[k].kind = Param::POINTER;
            tb.params[k].source = src;
            tb.params[k].hasValue = false;
            return true;
        }

    Param p;

    p.name = tb.ports[toPort].name;
    p.kind = Param::POINTER;
    p.source = src;
    p.isPort = true;

    tb.params.push_back(p);

    return true;
}

void NodeGraph::removeEdge (int edge)
{
    if (edge < 0 || edge >= (int)edges_.size())
        return;

    const Edge e = edges_[edge];

    edges_.erase(edges_.begin() + edge);

    Box &tb = boxes_[e.toBox];

    for (size_t k = 0; k < tb.params.size(); k++)
        if (tb.params[k].name == tb.ports[e.toPort].name)
        {
            tb.params[k].kind = Param::VALUE;
            tb.params[k].source.clear();
            tb.params[k].value = 0;
            tb.params[k].hasValue = true;
            break;
        }
}

bool NodeGraph::sliderGeometry (int box, double &x0, double &x1, double &y,
                                double &handleX) const
{
    x0 = x1 = y = handleX = 0;

    if (box < 0 || box >= (int)boxes_.size())
        return false;

    const Box &b = boxes_[box];

    if (!b.isControl)
        return false;

    if (b.attachedTo >= 0)
    {
        /* The strip is label, track, number on one line. The track takes the
           middle, leaving room either side for the two pieces of text. */
        x0 = b.x + ATTACH_W * 0.42;
        x1 = b.x + ATTACH_W - 34.0;
        y = b.y + ATTACH_H * 0.5;
    }
    else
    {
        x0 = b.x + CTL_INSET;
        x1 = b.x + b.w - CTL_INSET;
        y = b.y + BOX_HEAD + BOX_PAD + CTL_ROW * 0.5;
    }

    const double span = (b.ctlMax > b.ctlMin)
                        ? (double)(b.ctlMax - b.ctlMin) : 1.0;

    double t = ((double)b.ctlValue - (double)b.ctlMin) / span;

    if (t < 0) t = 0;
    if (t > 1) t = 1;

    handleX = x0 + t * (x1 - x0);

    return true;
}

int NodeGraph::sliderAt (double x, double y) const
{
    for (int i = (int)boxes_.size() - 1; i >= 0; i--)
    {
        if (!boxes_[i].isControl)
            continue;

        double x0, x1, ty, hx;

        if (!sliderGeometry(i, x0, x1, ty, hx))
            continue;

        /* The whole slider row is the target, not just the handle. Clicking
           anywhere on the track should jump the value there -- hunting for a
           five-pixel handle is unpleasant at any zoom. */
        const double reach = (boxes_[i].attachedTo >= 0)
                             ? ATTACH_H * 0.5 : CTL_ROW * 0.5;

        if (x >= x0 - CTL_HANDLE && x <= x1 + CTL_HANDLE &&
            y >= ty - reach && y <= ty + reach)
            return i;
    }

    return -1;
}

float NodeGraph::sliderValueAt (int box, double x) const
{
    double x0, x1, y, hx;

    if (!sliderGeometry(box, x0, x1, y, hx))
        return 0;

    const Box &b = boxes_[box];

    if (x1 <= x0)
        return b.ctlMin;

    double t = (x - x0) / (x1 - x0);

    if (t < 0) t = 0;
    if (t > 1) t = 1;

    return (float)((double)b.ctlMin + t * ((double)b.ctlMax - (double)b.ctlMin));
}

void NodeGraph::setControlValue (int box, float value)
{
    if (box < 0 || box >= (int)boxes_.size() || !boxes_[box].isControl)
        return;

    Box &b = boxes_[box];

    if (value < b.ctlMin) value = b.ctlMin;
    if (value > b.ctlMax) value = b.ctlMax;

    b.ctlValue = value;

    /* Everything reading this control shows the new number straight away.
     *
     * The name is built once. Inside the loop it was a fresh allocation per
     * parameter inspected, on every motion event of a slider drag. */
    const string source = "@" + b.ctlArg;

    for (size_t i = 0; i < boxes_.size(); i++)
        for (size_t k = 0; k < boxes_[i].params.size(); k++)
            if (boxes_[i].params[k].kind == Param::CHANARG &&
                boxes_[i].params[k].source == source)
            {
                boxes_[i].params[k].value = value;
                boxes_[i].params[k].hasValue = true;
            }
}
