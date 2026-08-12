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

        /* A control: one of the .dsp's top-level `@name' blocks, drawn as a
           box with a slider and one output.
           
           These are where a DSP keeps the settings meant to be played with --
           all 206 in the corpus declare .widget = 1, .min and .max, and 173
           give a .label. Showing them as text on whatever node happens to read
           them buried the interesting part of the patch; showing them as nodes
           puts the knobs on the canvas and turns `in1 = @blim' into a wire
           like any other. */
        bool isControl;

        string ctlArg;      /* the chanarg's name, without the @ */
        string ctlLabel;    /* .label, or the name again         */

        /* `.group' from the .dsp: "Envelope" for an ADSR's four sliders.
        
           An envelope's attack, decay, falloff and release are one control
           in the way anyone thinks about a patch, and four in the way the
           file stores them. Saying so lets the strips be drawn as one titled
           block rather than four unrelated rows against the same node. Empty
           for an ungrouped control, which is most of them. */
        string ctlGroup;

        float ctlValue;
        float ctlMin, ctlMax;

        /* True if this strip is the first of its group on its host, so it is
           the one that draws the group's heading. */
        bool groupHead;

        /* For a control with exactly one consumer: the box it feeds. It is
           drawn as a strip stacked above that box instead of as a node of its
           own.

           The wire is still drawn, short and dropping straight down. Leaving
           it out was the first attempt, on the reasoning that two things
           touching need no line between them -- but a node has several inputs,
           and adjacency only says which *node* a control belongs to. Which
           parameter it drives is exactly what the wire says, and nothing else
           on the canvas does.

           197 of the 206 controls in the corpus have exactly one consumer, so
           this is the ordinary case, not an optimisation. The other 9 stay
           free-standing boxes with visible wires, which turns out to be the
           useful reading: a control drawn as a box is one that several things
           share. -1 when free-standing. */
        int attachedTo;
        int attachSlot;     /* its row in the host's strip */

        /* How tall this box is when attached. The control strips are all one
           height; a probe panel is whatever its visual module asked for. The
           stack is walked by summing these rather than by multiplying, because
           a spectrogram is not the height of a meter and neither is the height
           of a slider. */
        double attachH;

        /* A probe: a live display of one of the host node's outputs, drawn as
           a panel stacked above it.
         *
         * A Box rather than a structure of its own, so that it inherits the
         * stacking, the overlap invariants and the hit-testing that dspgraph
         * already asserts over every box. It differs from a control strip in
         * which way it faces: a control feeds one of the host's *inputs*, a
         * probe reads one of its outputs.
         *
         * Deliberately NOT joined to its host by an Edge. §12 of
         * NODE_EDITOR.md argued that a control needs its wire drawn because
         * adjacency cannot say which of several inputs it drives -- but an
         * Edge here would appear in edges(), which every consumer reads as "a
         * connection in the .dsp" and which NodeEdit would try to write. A
         * probe is not in the file at all. The panel says which output it
         * reads in its own text instead, which a slider strip could not do
         * because its text is already the label and the value. */
        bool isProbe;

        string probeArg;    /* the host's output port being read */
        string probeVisual; /* "meter"; the visual module's name */

        Box (void) : x(0), y(0), w(0), h(0), layer(0), order(0),
                     isIoSource(false), isIoSink(false), isControl(false),
                     ctlValue(0), ctlMin(0), ctlMax(1), groupHead(false),
                     attachedTo(-1), attachSlot(0), attachH(0),
                     isProbe(false) { }
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

    /* ---- probes ----
     *
     * A live display of one node output, drawn as a panel above the node.
     * These are not in the .dsp and build() does not create them; the editor
     * adds them after building and re-adds them after a reload, because it is
     * the thing that knows which are armed.
     *
     * addProbe returns the new box index, or -1 if `host' is not a node box or
     * has no output port called `arg'.
     *
     * `height' is the height of the WHOLE PANEL, title row included -- not the
     * module's preferred height. A caller works it out as probeHeadHeight()
     * plus whatever the module asked for; passing the module's figure alone
     * gives a panel a dozen pixels short and a body a dozen pixels short of
     * that. Zero or negative falls back to one control strip's height.
     *
     * Arming the same point twice returns the existing panel rather than
     * stacking two, which is also what thSynth::armProbe does with slots. A
     * second arm with a different module retargets that panel.
     *
     * The caller lays out again afterwards. */
    int addProbe (int host, const string &arg, const string &visual,
                  double height);

    /* Removes a probe panel and every index above it shifts down, so any box
       index the caller was holding is stale afterwards -- the same contract
       as removing a node. Does nothing if `box' is not a probe. */
    void removeProbe (int box);

    /* The panel on `host' reading `arg', or -1. */
    int probeAt (int host, const string &arg) const;

    /* The title row at the top of a probe panel, above the area the visual
       module draws in.
     *
       Public because the two ends have to agree: the editor sizes a panel as
       the module's preferred height plus this, and the canvas splits it back
       apart to decide where the module's box starts. If they disagreed the
       plugin would draw outside the panel or leave a gap in it, and neither is
       visible in a screenshot until you go looking. */
    static double probeHeadHeight (void) { return 12.0; }

    /* Wrap the layer sequence into stacked bands once the drawing would be
       wider than this. 0 leaves it in one row, which is the default.
    
       Not an alternative layering: the layering is already optimal. Longest
       path puts every node at its critical-path depth, and that depth is the
       least number of columns any left-to-right drawing can have -- 80 of the
       81 shipped graphs are already at that minimum. Nothing reorders them
       into fewer columns without sending some wire backwards.
    
       So the only way to narrow a deep patch is to stop drawing it in one
       row. This cuts the column sequence into bands and stacks them, the way
       a long circuit is drawn on several rows of a schematic. The cost is
       real: the wire from the end of one band to the start of the next is a
       long way round, and the eye has to make the jump. */
    void setWrapWidth (double w) { wrapWidth_ = w; }
    double wrapWidth (void) const { return wrapWidth_; }

    /* How many bands the last layout used. 1 unless it wrapped. */
    int bandCount (void) const { return bands_; }

    /* True if this edge jumps from one band to the next -- the wire that has
       to be drawn as a return rather than a hop. */
    bool edgeWraps (int edge) const;

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

    /* The slider geometry of a control box, in graph coordinates: the track
       from x0 to x1 at height y, and where the handle sits along it.

       In the model rather than the canvas for the same reason edgeCurve is:
       the thing you drag and the thing you see have to be one shape. */
    bool sliderGeometry (int box, double &x0, double &x1, double &y,
                         double &handleX) const;

    /* The control box whose slider contains the point, or -1. */
    int sliderAt (double x, double y) const;

    /* The value a slider would take if its handle were dragged to `x'.
       Clamped to the control's declared range. */
    float sliderValueAt (int box, double x) const;

    /* Sets a control's value, for dragging. Does not touch the file. */
    void setControlValue (int box, float value);

    /* Moves a set of boxes together by (dx, dy), as one rigid thing.
     *
     * The clamp that keeps boxes out of negative space is applied once, to
     * the group's own left and top edge, not to each box in turn. Per-box
     * clamping looks equivalent and is not: the boxes nearest the edge stop
     * while the rest keep going, so the arrangement closes up, and which
     * boxes stick depends on which one the pointer had hold of.
     *
     * Here rather than in the canvas so it can be checked without a display,
     * the same reason boxAt and boxesIn are here. Attached strips are carried
     * along by moveBox as usual. */
    void moveSelection (const vector<int> &sel, double dx, double dy);

    /* Every box the rectangle touches, in box order.
     *
     * Touching, not enclosing. A patch is wider than the window and a box is
     * 128 pixels of it, so requiring a box to fit entirely inside the
     * rectangle would mean scrolling out to catch anything near the edge.
     * Touching is also the forgiving reading of a roughly dragged band.
     *
     * Attached controls are left out: a strip belongs to its host, moves with
     * it and is deleted with it, so gathering one on its own would offer an
     * independence it does not have.
     *
     * Here rather than in the canvas because it is geometry, and geometry is
     * the part that can be checked without a display -- the same reason
     * boxAt and portAt live here. */
    void boxesIn (double x0, double y0, double x1, double y1,
                  vector<int> &out) const;

    /* Index of a box by node name, or -1. For the io node this is the sink
       half; its args are split across both halves, so a caller after a
       particular arg should search the params of both. */
    int boxByName (const string &name) const;

    /* True if `name' is an arg the engine reads off the io node, and so an
       input of the audio-out half rather than something the io node offers.
       Public because it is the rule that decides which half an arg belongs
       to, and the corpus check asserts against it rather than restating it. */
    static bool isIoEngineInput (const string &name);

private:
    static int findPort (const Box &b, const string &name, bool wantInput);

    /* Decides which controls attach to which box. Called by layout(). */
    void assignAttachments (void);
    void collectParams (thSynthTree *tree, thNode *n, Box &b);
    void assignLayers (void);
    void orderWithinLayers (void);
    void placePorts (Box &b);

    vector<Box> boxes_;
    vector<Edge> edges_;
    map<string, int> byName_;   /* node name -> box index (the sink, for io) */

    double width_, height_;
    int layers_;
    double wrapWidth_;
    int bands_;
    int bandSize_;      /* layers per band */
};

#endif /* NODE_GRAPH_H */
