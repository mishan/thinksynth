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

    struct Box {
        string name;        /* the node's name in the .dsp   */
        string plugin;      /* "osc::simple", or "" for io   */
        vector<Port> ports;

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

        /* True if layering had to reverse this edge to break a cycle. Five of
           the shipped DSPs are genuine feedback loops (reverb01, dfb,
           smoothie and friends); these want drawing differently rather than
           being treated as an error. */
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

    /* Moves a box, for dragging. Does not re-layer. */
    void moveBox (int index, double x, double y);

private:
    static int findPort (const Box &b, const string &name, bool wantInput);
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
