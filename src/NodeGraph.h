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

#ifndef NODE_GRAPH_H
#define NODE_GRAPH_H 1

/*
 * A display graph built from a parsed thSynthTree: boxes, ports and wires,
 * with positions.
 *
 * Deliberately free of any GTK dependency so it can be exercised headlessly
 * over the whole .dsp corpus -- see scripts/dspgraph.cpp. Layout quality is a
 * matter of taste and cannot be unit-tested, but "every node placed, nothing
 * overlapping, every edge attached at both ends" can be, and that is most of
 * what goes wrong.
 */

#include <string>
#include <vector>
#include <map>
#include <utility>      /* std::pair; <map> is not required to provide it */

/* Named explicitly rather than relying on think.h's `using namespace std'
   having been pulled in first. This header is meant to be includable from a
   translation unit that knows nothing about libthink -- that is the whole
   point of keeping it free of engine types -- and it was not. */
using std::string;
using std::vector;
using std::map;
using std::pair;

class thSynthTree;
class thNode;

class NodeGraph {
public:
    NodeGraph (void);

    struct Port {
        string name;
        bool isInput;
        /* Offset from the box's top-left, filled in by layout(). */
        double x, y;
    };

    /* One settable thing on a node: what the .dsp calls an arg.
     *
     * A snapshot, not a live pointer. That keeps this header free of libthink
     * as well as of GTK, and means the graph stays valid and checkable after
     * the tree it was built from is gone. Anything that wants to *change* a
     * value goes through the .dsp text, not through here. */
    struct Param {
        /* Where the value comes from. Mirrors thArg::ArgType; kept separate so
           this header needs nothing from libthink. */
        enum Kind { VALUE = 0, POINTER, CHANARG, NOTE };

        string name;
        string label;       /* human name from the .dsp, or empty */
        string units;       /* "ms", "%" -- display only          */
        string comment;

        Kind kind;
        float value;        /* meaningful when kind == VALUE      */
        float min, max;     /* both 0 if the .dsp gave no range   */

        /* For POINTER, "env->out"; for CHANARG, "@cutoff". Empty otherwise. */
        string source;

        /* True if the plugin registers this as an input port -- one of the
           things a wire can land on. An arg the .dsp binds that the plugin
           never registered is still listed, just not a port. */
        bool isPort;

        /* True if the plugin declares this an output.

           An output is an arg like any other: it is in the node's arg map and
           it has a value. But the plugin writes it, so offering a spin button
           for it would invite an edit that gets overwritten on the next
           window. Shown, not offered -- hiding it outright would be worse,
           since seeing what a node produces is half of reading a patch. */
        bool isOutput;

        /* True if `value' means anything. A wired parameter has no number of
           its own; a chanarg has the channel's, which is worth showing. */
        bool hasValue;

        Param (void) : kind(VALUE), value(0), min(0), max(0), isPort(false),
                       isOutput(false), hasValue(false) { }
    };

    struct Box {
        string name;        /* the node's name in the .dsp   */
        string plugin;      /* "osc::simple"; the io halves get
                               "midi in" and "audio out"     */
        vector<Port> ports;
        vector<Param> params;

        double x, y, w, h;

        int layer;          /* left-to-right rank                       */
        int order;          /* position within the layer                */

        /* The io node appears twice: once as the MIDI source that supplies
           note/velocity/trigger, once as the audio sink that consumes
           out0/out1/play. Drawing it as one box puts it in a cycle in 89 of
           the 92 shipped DSPs, which is an artefact of collapsing two
           unrelated port groups into one vertex. */
        bool isIoSource;
        bool isIoSink;

        Box (void) : x(0), y(0), w(0), h(0), layer(0), order(0),
                     isIoSource(false), isIoSink(false) { }
    };

    struct Edge {
        int fromBox, toBox;     /* indices into boxes_          */
        int fromPort, toPort;   /* indices into that box's ports */

        /* True if layering found this to be a back edge and left it out of
           the ranking. Nothing is reversed -- the edge is drawn from and to
           the same ports as any other, just dashed. Five of the shipped DSPs
           are genuine feedback loops (reverb01, dfb, smoothie and friends);
           these want drawing differently rather than being treated as an
           error. */
        bool feedback;

        Edge (void) : fromBox(-1), toBox(-1), fromPort(-1), toPort(-1),
                      feedback(false) { }
    };

    /* Builds boxes and edges from a parsed tree. Ports come from the node's
       plugin, so plugin-internal state (thPlugin::ARG_STATE) is left out --
       a delay line's ring buffer is not something anyone should be wiring. */
    bool build (thSynthTree *tree);

    /* Assigns layers, orders within layers, and pixel positions. */
    void layout (void);

    const vector<Box> &boxes (void) const { return boxes_; }
    const vector<Edge> &edges (void) const { return edges_; }

    /* Overall extent after layout(), for sizing a canvas. */
    double width (void) const { return width_; }
    double height (void) const { return height_; }

    int layerCount (void) const { return layers_; }
    int feedbackCount (void) const;

    /* Moves a box, for dragging. Does not re-layer, and does not extend the
       canvas extent -- call refreshExtent() if the box may have gone past the
       previous bounds. */
    void moveBox (int index, double x, double y);

    /* Recomputes width()/height() from where the boxes actually are, after
       dragging has moved them off the laid-out grid. */
    void refreshExtent (void);

    /* Hit-testing, in graph coordinates. Deliberately here rather than in the
       canvas so it can be exercised headlessly -- "clicking a port's centre
       finds that port" is exactly the sort of thing that silently goes wrong
       and is tedious to notice by hand.

       boxAt returns the topmost box containing the point, or -1. Later boxes
       win, matching the draw order. */
    int boxAt (double x, double y) const;

    /* portAt finds a port whose handle contains the point, within `slack'
       pixels. Returns true and fills box/port on a hit. */
    bool portAt (double x, double y, int &box, int &port,
                 double slack = 6.0) const;

    /* Absolute position of a port's handle. */
    void portPos (int box, int port, double &x, double &y) const;

    /* Whether a wire from one port to another is allowed, with a sentence
       saying why not.

       Here rather than in NodeEdit because it needs the graph: the io node is
       one node in the .dsp but two boxes on screen, and three shipped DSPs
       genuinely read ionode->velocity into an ionode arg. A rule phrased over
       node names would have to forbid that. */
    bool canConnect (int fromBox, int fromPort, int toBox, int toPort,
                     string &why) const;

    /* The cubic a wire is drawn along: its two endpoints and two control
       points, in graph coordinates.

       Here rather than in the canvas so that drawing and hit-testing cannot
       drift apart -- "clicking a wire selects a different wire" is precisely
       the bug that happens when the same curve is described twice. */
    void edgeCurve (int edge, double *xs, double *ys) const;

    /* The wire nearest the point, within `slack', or -1. */
    int edgeAt (double x, double y, double slack = 5.0) const;

    /* Adds a wire, replacing whatever fed that input -- the grammar has one
       right-hand side per arg, so two wires into one input cannot be spelled.
       Updates the target's parameter snapshot to match. False, with a reason,
       if canConnect says no. */
    bool connect (int fromBox, int fromPort, int toBox, int toPort,
                  string &why);

    /* Removes a wire and puts the target parameter back to a plain value. */
    void removeEdge (int edge);

    /* Index of a box by node name, or -1. For the io node this is the sink
       half, which is the one that owns the args. */
    int boxByName (const string &name) const;

private:
    static int findPort (const Box &b, const string &name, bool wantInput);
    void collectParams (thSynthTree *tree, thNode *n, Box &b);
    void assignLayers (void);
    void orderWithinLayers (void);
    void placePorts (Box &b);

    vector<Box> boxes_;
    vector<Edge> edges_;
    map<string, int> byName_;   /* node name -> box index (the sink, for io) */

    double width_, height_;
    int layers_;
};

#endif /* NODE_GRAPH_H */
