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

/*
 * thinknode -- the node editor's model, in the page's own instance of the
 * module.
 *
 * NodeGraph, NodeEdit, NodeLayout and NodeCatalog are 4.8k lines that took
 * a corpus to get right -- the io node as two boxes, feedback arcs,
 * attached controls, splices that keep every comment -- and they include
 * only libthink, so they compile here unchanged (JAM_M6.md, section 7).
 * What is in this file is the way in: build a graph from a patch's text,
 * read what is in it, edit it, and hand the new text back.
 *
 * Everything here takes and returns TEXT. In a room the document is the
 * patch: the text comes out of a CRDT and the new text goes back in as a
 * splice, and there is no file anywhere in it. That is what
 * NodeEdit::Text and NodeLayout::Text are for, and this is their caller.
 *
 * Its own translation unit, and its own synth, because it shares nothing
 * with the scheduler in thinkweb.cpp: the page's instance of the module
 * never plays anything. The synth here exists to parse a .dsp -- the
 * parser is a method on it -- and to hold the plugin manager the catalogue
 * asks for ports, and it is silent, so nothing it is handed is ever built
 * into a note.
 */

#include "config.h"

#include <stdio.h>

#include <string>
#include <vector>

#include <emscripten.h>

#include "think.h"

#include "thDynLib.h"
#include "thPluginManager.h"
#include "thSynth.h"

#include "cairo2d.h"
#include "cairomm/context.h"

#include "twdraw.h"

#include "NodeCanvas.h"
#include "NodeCatalog.h"
#include "NodeEdit.h"
#include "NodeGraph.h"
#include "NodeLayout.h"

/* Where a patch handed over as text is written, so the parser can open it
   like any other file. The module's own MEMFS; nothing leaves the page. */
#define TN_FILE "/node.dsp"

namespace {

thSynth        *synth_ = NULL;
NodeGraph       graph_;
NodeCatalog     catalog_;
NodeCatalog::Entry described_;

/* What the last edit produced: the new text, the sentence a refusal came
   with, and the count of references an edit rewrote on the way. They live
   until the next edit, which is what the page reads them between. */
std::string     text_;
std::string     why_;
int             removed_ = 0;

/* The synth exists to parse and to load plugins. Silent, since nothing
   here plays: a note handed to it would be a note nobody asked for
   (thSynth::setSilent). */
thSynth *synth (void)
{
    if (synth_ == NULL)
    {
        synth_ = new thSynth("", TH_DEFAULT_WINDOW_LENGTH,
                             TH_DEFAULT_SAMPLES);
        synth_->setSilent(true);
    }

    return synth_;
}

bool writeFile (const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL)
        return false;

    fputs(text, f);

    return fclose(f) == 0;
}

const NodeGraph::Box *boxAt (int b)
{
    if (b < 0 || (size_t)b >= graph_.boxes().size())
        return NULL;

    return &graph_.boxes()[(size_t)b];
}

/* Every edit has the same shape from the page's side: it is handed the
   patch and answers with a Result, a new text and a sentence. */
int answer (NodeEdit::Result r)
{
    return (int)r;
}

/* ---- the canvas ----
 *
 * The desktop's NodeCanvas, with the same four answers a shell owes a
 * CanvasContent (JAM_M6.md, section 6.1) -- the composer canvas's shell in
 * thinkweb.cpp is the same class of thing, and the page's half of both is
 * one file, canvasview.js.
 */
class WebNodeCanvas : public NodeCanvas
{
public:
    WebNodeCanvas (void)
        : dirty_(true), width_(0), height_(0),
          viewX_(0), viewY_(0), viewW_(0), viewH_(0) {}

    bool takeDirty (void)
    {
        const bool was = dirty_;

        dirty_ = false;
        return was;
    }

    int width (void) const { return width_; }
    int height (void) const { return height_; }

    void setViewport (double x, double y, double w, double h)
    {
        viewX_ = x;
        viewY_ = y;
        viewW_ = w;
        viewH_ = h;

        shellResized();
        dirty_ = true;
    }

protected:
    void requestRedraw (void) override { dirty_ = true; }

    void resizeShell (int w, int h) override
    {
        width_ = w;
        height_ = h;
        dirty_ = true;
    }

    bool shellViewport (double &x, double &y, double &w,
                        double &h) const override
    {
        if (viewW_ <= 0.0 || viewH_ <= 0.0)
            return false;

        x = viewX_;
        y = viewY_;
        w = viewW_;
        h = viewH_;

        return true;
    }

private:
    bool dirty_;
    int width_, height_;
    double viewX_, viewY_, viewW_, viewH_;
};

WebNodeCanvas *canvas_ = NULL;
Cairo::RefPtr<Cairo::Context> canvasContext_;

/* What the canvas has decided since the page last asked. The desktop's
   NodeEditor answers these signals with an edit, a rebuild or a line in
   the status bar; the page does the same, and this is how they reach it
   (JAM_M6.md, section 7.2).
 *
 * A queue rather than a callback per signal because a message to a page is
 * not a function call: what the shell does with each is its own business,
 * and it does it after the gesture that produced them has been fully
 * applied here. */
struct Signal
{
    int         kind;
    int         a, b, c, d;
    double      x, y, value;
    std::string text;

    Signal (int k) : kind(k), a(-1), b(-1), c(-1), d(-1),
                     x(0), y(0), value(0) {}
};

std::vector<Signal> signals_;

/* The kinds, which the page switches on. */
enum {
    SIG_BOX_MOVED = 0,
    SIG_SELECTED,
    SIG_SELECTION,
    SIG_CONNECT,
    SIG_DISCONNECT,
    SIG_REFUSED,
    SIG_CONTROL,
    SIG_CONTEXT,
    SIG_PROBE
};

const Signal *signalAt (int i)
{
    if (i < 0 || (size_t)i >= signals_.size())
        return NULL;

    return &signals_[(size_t)i];
}

WebNodeCanvas *canvas (void)
{
    if (canvas_ != NULL)
        return canvas_;

    canvas_ = new WebNodeCanvas();

    canvas_->signal_box_moved().connect([](int box)
        {
            Signal s(SIG_BOX_MOVED);

            s.a = box;
            signals_.push_back(s);
        });

    canvas_->signal_selected().connect([](int box)
        {
            Signal s(SIG_SELECTED);

            s.a = box;
            signals_.push_back(s);
        });

    canvas_->signal_selection().connect([](int many)
        {
            Signal s(SIG_SELECTION);

            s.a = many;
            signals_.push_back(s);
        });

    canvas_->signal_connect_requested().connect(
        [](int fromBox, int fromPort, int toBox, int toPort)
        {
            Signal s(SIG_CONNECT);

            s.a = fromBox;
            s.b = fromPort;
            s.c = toBox;
            s.d = toPort;
            signals_.push_back(s);
        });

    canvas_->signal_disconnect_requested().connect([](int edge)
        {
            Signal s(SIG_DISCONNECT);

            s.a = edge;
            signals_.push_back(s);
        });

    canvas_->signal_refused().connect([](std::string why)
        {
            Signal s(SIG_REFUSED);

            s.text = why;
            signals_.push_back(s);
        });

    canvas_->signal_control_changed().connect(
        [](int box, double value, bool committing)
        {
            Signal s(SIG_CONTROL);

            s.a = box;
            s.b = committing ? 1 : 0;
            s.value = value;
            signals_.push_back(s);
        });

    canvas_->signal_context_requested().connect(
        [](int box, int port, double x, double y)
        {
            Signal s(SIG_CONTEXT);

            s.a = box;
            s.b = port;
            s.x = x;
            s.y = y;
            signals_.push_back(s);
        });

    canvas_->signal_probe_activated().connect([](int box)
        {
            Signal s(SIG_PROBE);

            s.a = box;
            signals_.push_back(s);
        });

    canvas_->setGraph(&graph_);

    return canvas_;
}

} /* namespace */

extern "C" {

/* ---- the graph ---------------------------------------------------------
 *
 * Built from the text, laid out, and then read box by box. The canvas
 * draws it from the same graph in the same heap; these exports are for the
 * page's forms -- the params panel and the palette -- which are HTML for
 * the same reason they are gtkmm on the desktop.
 */

/* Parses the patch and lays it out. The number of boxes, or -1 if it did
   not parse. */
EMSCRIPTEN_KEEPALIVE int tw_graph_build (const char *text)
{
    if (text == NULL || !writeFile(TN_FILE, text))
        return -1;

    thSynthTree *tree = synth()->parseTree(TN_FILE);

    if (tree == NULL)
        return -1;

    const bool ok = graph_.build(tree);

    delete tree;

    if (!ok)
        return -1;

    /* Built is not laid out: build() makes the boxes and the wires,
       layout() gives them their columns and their positions. The saved
       ones in the file go over the top of that (tw_graph_apply_layout),
       which is why this order and not the other. */
    graph_.layout();

    return (int)graph_.boxes().size();
}

/* The saved positions in the text, applied over the computed layout. How
   many boxes were moved. */
EMSCRIPTEN_KEEPALIVE int tw_graph_apply_layout (const char *text)
{
    NodeLayout::PosMap pos;

    if (text == NULL || !NodeLayout::Text::read(text, pos))
        return 0;

    return NodeLayout::apply(graph_, pos, graph_);
}

/* The text with its layout block rewritten from the graph as it stands --
   what a drag has to be followed by. Read back with tw_edit_text(). */
EMSCRIPTEN_KEEPALIVE int tw_layout_write (const char *text)
{
    if (text == NULL)
        return 0;

    text_ = text;

    return NodeLayout::Text::write(text_, graph_) ? 1 : 0;
}

/* The probes saved in the text: how many, and then each in turn. */
EMSCRIPTEN_KEEPALIVE int tw_layout_probe_count (const char *text)
{
    static std::vector<NodeLayout::ProbeRef> probes;

    if (text != NULL)
        NodeLayout::Text::readProbes(text, probes);

    return (int)probes.size();
}

EMSCRIPTEN_KEEPALIVE int tw_graph_box_count (void)
{
    return (int)graph_.boxes().size();
}

EMSCRIPTEN_KEEPALIVE const char *tw_graph_box_name (int b)
{
    const NodeGraph::Box *box = boxAt(b);

    return box != NULL ? box->name.c_str() : "";
}

EMSCRIPTEN_KEEPALIVE const char *tw_graph_box_plugin (int b)
{
    const NodeGraph::Box *box = boxAt(b);

    return box != NULL ? box->plugin.c_str() : "";
}

/* What kind of box it is, which is what the page's panel switches on:
   0 a node, 1 a control, 2 the io node's source half, 3 its sink half,
   4 a probe panel. */
EMSCRIPTEN_KEEPALIVE int tw_graph_box_kind (int b)
{
    const NodeGraph::Box *box = boxAt(b);

    if (box == NULL)
        return -1;

    if (box->isProbe)
        return 4;

    if (box->isIoSource)
        return 2;

    if (box->isIoSink)
        return 3;

    if (box->isControl)
        return 1;

    return 0;
}

EMSCRIPTEN_KEEPALIVE double tw_graph_box_x (int b)
{
    const NodeGraph::Box *box = boxAt(b);

    return box != NULL ? box->x : 0.0;
}

EMSCRIPTEN_KEEPALIVE double tw_graph_box_y (int b)
{
    const NodeGraph::Box *box = boxAt(b);

    return box != NULL ? box->y : 0.0;
}

/* A control's own numbers, for the strip the page draws it as. */
EMSCRIPTEN_KEEPALIVE const char *tw_graph_box_control (int b)
{
    const NodeGraph::Box *box = boxAt(b);

    return box != NULL ? box->ctlArg.c_str() : "";
}

EMSCRIPTEN_KEEPALIVE double tw_graph_box_control_value (int b)
{
    const NodeGraph::Box *box = boxAt(b);

    return box != NULL ? box->ctlValue : 0.0;
}

/* ---- a box's parameters, for the panel ---- */

EMSCRIPTEN_KEEPALIVE int tw_graph_param_count (int b)
{
    const NodeGraph::Box *box = boxAt(b);

    return box != NULL ? (int)box->params.size() : 0;
}

EMSCRIPTEN_KEEPALIVE const char *tw_graph_param_name (int b, int p)
{
    const NodeGraph::Box *box = boxAt(b);

    if (box == NULL || p < 0 || (size_t)p >= box->params.size())
        return "";

    return box->params[(size_t)p].name.c_str();
}

EMSCRIPTEN_KEEPALIVE double tw_graph_param_value (int b, int p)
{
    const NodeGraph::Box *box = boxAt(b);

    if (box == NULL || p < 0 || (size_t)p >= box->params.size())
        return 0.0;

    return box->params[(size_t)p].value;
}

/* NodeGraph::Param::Kind: what drives this parameter -- a value, a wire,
   a control -- which is what decides whether the panel offers a box to
   type in or a sentence saying where the number comes from. */
EMSCRIPTEN_KEEPALIVE int tw_graph_param_kind (int b, int p)
{
    const NodeGraph::Box *box = boxAt(b);

    if (box == NULL || p < 0 || (size_t)p >= box->params.size())
        return -1;

    return (int)box->params[(size_t)p].kind;
}

EMSCRIPTEN_KEEPALIVE int tw_graph_param_is_port (int b, int p)
{
    const NodeGraph::Box *box = boxAt(b);

    if (box == NULL || p < 0 || (size_t)p >= box->params.size())
        return 0;

    return box->params[(size_t)p].isPort ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int tw_graph_param_is_output (int b, int p)
{
    const NodeGraph::Box *box = boxAt(b);

    if (box == NULL || p < 0 || (size_t)p >= box->params.size())
        return 0;

    return box->params[(size_t)p].isOutput ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int tw_graph_param_has_value (int b, int p)
{
    const NodeGraph::Box *box = boxAt(b);

    if (box == NULL || p < 0 || (size_t)p >= box->params.size())
        return 0;

    return box->params[(size_t)p].hasValue ? 1 : 0;
}

/* ---- the edits ---------------------------------------------------------
 *
 * Each takes the patch as it stands and answers with a NodeEdit::Result;
 * the new text and the sentence a refusal came with are read back with
 * tw_edit_text() and tw_edit_why(). The page then splices the difference
 * into the document, which is what makes it everybody's edit.
 *
 * The text is handed in every time rather than kept here, because the
 * document moves under this: between one edit and the next, somebody else
 * may have typed in the same file. The new text is computed against what
 * the page had at the moment it asked, and the splice is computed inside
 * the transaction (JAM_M6.md, section 12.5).
 */

EMSCRIPTEN_KEEPALIVE const char *tw_edit_text (void)
{
    return text_.c_str();
}

EMSCRIPTEN_KEEPALIVE const char *tw_edit_why (void)
{
    return why_.c_str();
}

/* How many references an edit rewrote on the way -- the part of a delete
   nobody asked for, which is worth telling them about. */
EMSCRIPTEN_KEEPALIVE int tw_edit_removed (void)
{
    return removed_;
}

EMSCRIPTEN_KEEPALIVE int tw_edit_set_value (const char *text,
                                            const char *node,
                                            const char *arg, double value)
{
    text_ = text != NULL ? text : "";

    return answer(NodeEdit::Text::setValue(text_, node, arg, value, why_));
}

EMSCRIPTEN_KEEPALIVE int tw_edit_connect (const char *text, const char *node,
                                          const char *arg,
                                          const char *srcNode,
                                          const char *srcPort)
{
    text_ = text != NULL ? text : "";

    return answer(NodeEdit::Text::connect(text_, node, arg, srcNode, srcPort,
                                          why_));
}

EMSCRIPTEN_KEEPALIVE int tw_edit_connect_control (const char *text,
                                                  const char *node,
                                                  const char *arg,
                                                  const char *control)
{
    text_ = text != NULL ? text : "";

    return answer(NodeEdit::Text::connectControl(text_, node, arg, control,
                                                 why_));
}

EMSCRIPTEN_KEEPALIVE int tw_edit_disconnect (const char *text,
                                             const char *node,
                                             const char *arg, double value)
{
    text_ = text != NULL ? text : "";

    return answer(NodeEdit::Text::disconnect(text_, node, arg, value, why_));
}

EMSCRIPTEN_KEEPALIVE int tw_edit_set_chanarg (const char *text,
                                              const char *name, double value)
{
    text_ = text != NULL ? text : "";

    return answer(NodeEdit::Text::setChanArg(text_, name, value, why_));
}

EMSCRIPTEN_KEEPALIVE int tw_edit_add_node (const char *text,
                                           const char *node,
                                           const char *plugin)
{
    text_ = text != NULL ? text : "";

    /* The defaults the plugin declares for its own args, so a node the
       editor adds says what it does rather than leaving `amp = 0' for the
       reader to interpret. The catalogue is where they come from. */
    NodeCatalog::Entry e;
    std::vector<std::pair<std::string, double> > initial;

    if (catalog_.describe(plugin, synth()->getPluginManager(), e))
        for (size_t i = 0; i < e.defaults.size(); i++)
            initial.push_back(std::make_pair(e.defaults[i].name,
                                             e.defaults[i].value));

    return answer(NodeEdit::Text::addNode(text_, node, plugin, initial,
                                          why_));
}

EMSCRIPTEN_KEEPALIVE int tw_edit_remove_node (const char *text,
                                              const char *node)
{
    text_ = text != NULL ? text : "";
    removed_ = 0;

    return answer(NodeEdit::Text::removeNode(text_, node, removed_, why_));
}

EMSCRIPTEN_KEEPALIVE int tw_edit_add_control (const char *text,
                                              const char *name, double value,
                                              double min, double max,
                                              const char *label,
                                              const char *group)
{
    text_ = text != NULL ? text : "";

    return answer(NodeEdit::Text::addControl(text_, name, value, min, max,
                                             label, group, why_));
}

EMSCRIPTEN_KEEPALIVE int tw_edit_set_control_meta (const char *text,
                                                   const char *name,
                                                   double min, double max,
                                                   const char *label,
                                                   const char *group)
{
    text_ = text != NULL ? text : "";

    return answer(NodeEdit::Text::setControlMeta(text_, name, min, max, label,
                                                 group, why_));
}

EMSCRIPTEN_KEEPALIVE int tw_edit_remove_control (const char *text,
                                                 const char *name)
{
    text_ = text != NULL ? text : "";
    removed_ = 0;

    return answer(NodeEdit::Text::removeControl(text_, name, removed_, why_));
}

/* A new patch: the smallest .dsp that loads. */
EMSCRIPTEN_KEEPALIVE int tw_edit_create (const char *name, const char *author)
{
    text_.clear();

    return answer(NodeEdit::Text::createFile(text_, name, author, why_));
}

/* What a Result means, in a sentence a person can read. */
EMSCRIPTEN_KEEPALIVE const char *tw_edit_result_text (int r)
{
    return NodeEdit::resultText((NodeEdit::Result)r);
}

/* ---- the canvas --------------------------------------------------------
 *
 * The same shape as the composer canvas's exports in thinkweb.cpp, and the
 * page drives both through the same shell (canvasview.js). They are
 * separate sets rather than one with a selector because the two canvases
 * have nothing else in common: one is enlarged and painted on, the other
 * is wired up.
 */

/* Draw at w x h. The list is read back with tw_draw_ops() and friends, as
   every drawing in this module is. */
EMSCRIPTEN_KEEPALIVE int tw_node_canvas_draw (int w, int h)
{
    cairo_t *cr = twDrawingBegin();

    if (!canvasContext_)
        canvasContext_ = Cairo::Context::create(cr);

    canvas()->draw(canvasContext_, w, h);

    return cairo2d_op_words(cr);
}

/* The right button is its own entry point here, as it is on the desktop:
   it asks what can be done rather than starting a wire. */
EMSCRIPTEN_KEEPALIVE void tw_node_canvas_press (double x, double y,
                                                int button, int nPress)
{
    if (button == 3)
        canvas()->onRightPressed(nPress, x, y);
    else
        canvas()->onPressed(nPress, x, y);
}

EMSCRIPTEN_KEEPALIVE void tw_node_canvas_motion (double x, double y)
{
    canvas()->onMotion(x, y);
}

EMSCRIPTEN_KEEPALIVE void tw_node_canvas_release (double x, double y,
                                                  int button)
{
    (void)button;
    canvas()->onReleased(1, x, y);
}

EMSCRIPTEN_KEEPALIVE int tw_node_canvas_key (int key)
{
    return canvas()->keyPressed((CanvasContent::Key)key) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void tw_node_canvas_viewport (double x, double y,
                                                   double w, double h)
{
    canvas()->setViewport(x, y, w, h);
}

EMSCRIPTEN_KEEPALIVE double tw_node_canvas_zoom (void)
{
    return canvas()->zoom();
}

EMSCRIPTEN_KEEPALIVE void tw_node_canvas_set_zoom (double z)
{
    canvas()->setZoom(z);
}

EMSCRIPTEN_KEEPALIVE void tw_node_canvas_zoom_to_fit (void)
{
    canvas()->zoomToFit();
}

EMSCRIPTEN_KEEPALIVE int tw_node_canvas_width (void)
{
    return canvas()->width();
}

EMSCRIPTEN_KEEPALIVE int tw_node_canvas_height (void)
{
    return canvas()->height();
}

EMSCRIPTEN_KEEPALIVE int tw_node_canvas_dirty (void)
{
    return canvas()->takeDirty() ? 1 : 0;
}

/* The box the params panel is showing, or -1. */
EMSCRIPTEN_KEEPALIVE int tw_node_canvas_selected (void)
{
    return canvas()->selected();
}

EMSCRIPTEN_KEEPALIVE void tw_node_canvas_select (int box)
{
    canvas()->setSelected(box);
}

/* ---- what the canvas decided ---- */

EMSCRIPTEN_KEEPALIVE int tw_node_signal_count (void)
{
    return (int)signals_.size();
}

EMSCRIPTEN_KEEPALIVE void tw_node_signals_clear (void)
{
    signals_.clear();
}

#define TW_SIGNAL_FIELD(name, type, member, empty)                         \
    EMSCRIPTEN_KEEPALIVE type tw_node_signal_##name (int i)                \
    {                                                                      \
        const Signal *s = signalAt(i);                                     \
                                                                           \
        return s != NULL ? s->member : empty;                              \
    }

TW_SIGNAL_FIELD(kind,  int,    kind,  -1)
TW_SIGNAL_FIELD(a,     int,    a,     -1)
TW_SIGNAL_FIELD(b,     int,    b,     -1)
TW_SIGNAL_FIELD(c,     int,    c,     -1)
TW_SIGNAL_FIELD(d,     int,    d,     -1)
TW_SIGNAL_FIELD(x,     double, x,    0.0)
TW_SIGNAL_FIELD(y,     double, y,    0.0)
TW_SIGNAL_FIELD(value, double, value, 0.0)

#undef TW_SIGNAL_FIELD

EMSCRIPTEN_KEEPALIVE const char *tw_node_signal_text (int i)
{
    const Signal *s = signalAt(i);

    return s != NULL ? s->text.c_str() : "";
}

/* ---- the palette -------------------------------------------------------
 *
 * Every plugin this module was built with, by category. There is no
 * directory to walk -- they are compiled in and found through a table
 * (wasm/web/CMakeLists.txt) -- so the catalogue is handed the table's
 * names instead (NodeCatalog::take).
 */

EMSCRIPTEN_KEEPALIVE int tw_catalog_take (void)
{
    std::vector<std::string> spellings;

#ifdef THINK_STATIC_PLUGINS
    for (size_t i = 0; i < thStaticPluginCount; i++)
    {
        std::string name = thStaticPlugins[i].name;
        const std::string::size_type slash = name.find('/');

        if (slash == std::string::npos)
            continue;

        /* The composers are in the same table and are not nodes: a .dsp
           has nowhere to put one. */
        if (name.compare(0, slash, "composer") == 0)
            continue;

        spellings.push_back(name.substr(0, slash) + "::" +
                            name.substr(slash + 1));
    }
#endif

    return catalog_.take(spellings);
}

EMSCRIPTEN_KEEPALIVE int tw_catalog_count (void)
{
    return catalog_.count();
}

EMSCRIPTEN_KEEPALIVE int tw_catalog_category_count (void)
{
    return (int)catalog_.categories().size();
}

EMSCRIPTEN_KEEPALIVE const char *tw_catalog_category (int c)
{
    if (c < 0 || (size_t)c >= catalog_.categories().size())
        return "";

    return catalog_.categories()[(size_t)c].c_str();
}

EMSCRIPTEN_KEEPALIVE int tw_catalog_in_category (const char *category)
{
    return category != NULL
        ? (int)catalog_.inCategory(category).size() : 0;
}

EMSCRIPTEN_KEEPALIVE const char *tw_catalog_spelling (const char *category,
                                                      int i)
{
    if (category == NULL)
        return "";

    const std::vector<NodeCatalog::Entry> &list =
        catalog_.inCategory(category);

    if (i < 0 || (size_t)i >= list.size())
        return "";

    return list[(size_t)i].spelling.c_str();
}

EMSCRIPTEN_KEEPALIVE const char *tw_catalog_name (const char *category, int i)
{
    if (category == NULL)
        return "";

    const std::vector<NodeCatalog::Entry> &list =
        catalog_.inCategory(category);

    if (i < 0 || (size_t)i >= list.size())
        return "";

    return list[(size_t)i].name.c_str();
}

/* Loads the plugin and reads what it says about itself. Nonzero if it
   loaded; what it said is the four calls below, until the next describe. */
EMSCRIPTEN_KEEPALIVE int tw_catalog_describe (const char *spelling)
{
    described_ = NodeCatalog::Entry();

    if (spelling == NULL)
        return 0;

    return catalog_.describe(spelling, synth()->getPluginManager(),
                             described_) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char *tw_catalog_desc (void)
{
    return described_.desc.c_str();
}

EMSCRIPTEN_KEEPALIVE int tw_catalog_port_count (void)
{
    return (int)described_.ports.size();
}

EMSCRIPTEN_KEEPALIVE const char *tw_catalog_port_name (int k)
{
    if (k < 0 || (size_t)k >= described_.ports.size())
        return "";

    return described_.ports[(size_t)k].name.c_str();
}

EMSCRIPTEN_KEEPALIVE int tw_catalog_port_is_input (int k)
{
    if (k < 0 || (size_t)k >= described_.ports.size())
        return 0;

    return described_.ports[(size_t)k].isInput ? 1 : 0;
}

/* A name for a new node that nothing in the graph collides with. */
EMSCRIPTEN_KEEPALIVE const char *tw_catalog_suggest (const char *plugin)
{
    static std::string suggested;
    std::vector<std::string> taken;

    for (size_t i = 0; i < graph_.boxes().size(); i++)
        taken.push_back(graph_.boxes()[i].name);

    suggested = NodeCatalog::suggestName(plugin != NULL ? plugin : "", taken);

    return suggested.c_str();
}

} /* extern "C" */
