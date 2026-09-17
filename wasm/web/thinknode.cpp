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
