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

#include <gtkmm.h>

#include "NodeEdit.h"
#include "NodeGraph.h"
#include "NodeParamsView.h"
#include "../DspCategories.h"

/* Not words, so never a parameter's name. */
static const char *const NAME_ROW = "#name";
static const char *const EXPR_ROW = "#expr";

NodeParamsView::NodeParamsView (void)
{
    /* Wide enough for a name and a number box beside it. The editor's pane
       can be dragged from here; this is where it starts. */
    set_size_request(240, -1);

    signal_edited().connect(
        sigc::mem_fun(*this, &NodeParamsView::onEdited));
}

void NodeParamsView::setBox (const NodeGraph *graph, int box)
{
    model_.setBox(graph, box);
    showingHeader_ = false;

    if ((graph == NULL || box < 0) && haveHeader_)
    {
        showHeader();
        return;
    }

    thPanel built;

    /* Nothing selected, or a box with no parameters at all. Both are
       states worth saying rather than an empty column. */
    if (!model_.build(built))
    {
        built = thPanel();
        built.kind = thPanel::NODE_VALUE;
        built.a = box;
        built.title = (graph == NULL || box < 0) ? "No node selected"
                                                 : "No parameters";
        built.subtitle = (graph == NULL || box < 0)
            ? "Click a node to see its parameters." : "";
    }

    named_ = -1;
    exprArgs_.clear();

    /* The arithmetic an arg is written as, in the file's own spelling, typed
       over in its row; an expression's own box offers it alone. Only where
       the text can be replaced: a line an array or the io node's `[]'
       shares is the file's to edit. */
    if (graph != NULL && box >= 0 && box < (int)graph->boxes().size())
    {
        const NodeGraph::Box &b = graph->boxes()[box];
        string written;

        exprNode_ = b.isExpr ? b.exprNode : b.name;

        if (b.isExpr)
        {
            thPanelRow r;

            built.title = b.name;
            built.subtitle = "An arg written as arithmetic";
            r.id = EXPR_ROW;
            r.label = "Expression";
            r.kind = thPanelRow::READONLY;
            r.text = b.exprText;

            if (text_ != NULL &&
                NodeEdit::Text::expression(*text_, b.exprNode, b.exprArg,
                                           written))
            {
                r.kind = thPanelRow::TEXT;
                r.editable = true;
                r.text = written;
                exprArgs_[EXPR_ROW] = b.exprArg;
            }

            built.rows.insert(built.rows.begin(), r);
        }
        else
            for (thPanelRow &r : built.rows)
                for (const NodeGraph::Param &p : b.params)
                    if (p.name == r.id && p.isExpr && text_ != NULL &&
                        NodeEdit::Text::expression(*text_, b.name, p.name,
                                                   written))
                    {
                        r.kind = thPanelRow::TEXT;
                        r.editable = true;
                        r.text = written;
                        exprArgs_[r.id] = p.name;
                    }
    }

    /* A node's or a control's name, to rename it by. A probe and an
       expression have none of their own in the text. */
    if (graph != NULL && box >= 0 && box < (int)graph->boxes().size() &&
        !graph->boxes()[box].isProbe && !graph->boxes()[box].isExpr)
    {
        const NodeGraph::Box &b = graph->boxes()[box];
        thPanelRow r;

        r.id = NAME_ROW;
        r.label = "Name";
        r.kind = thPanelRow::TEXT;
        r.editable = true;
        r.text = b.isControl ? b.ctlArg : b.name;
        built.rows.insert(built.rows.begin(), r);
        named_ = box;
    }

    setPanel(built);
}

void NodeParamsView::setHeader (const Header &h)
{
    header_ = h;
    haveHeader_ = true;

    if (showingHeader_)
        showHeader();
}

/* With nothing selected, the patch itself: what the patch selector shows
   of it, and the empty column there used to be put to use. */
void NodeParamsView::showHeader (void)
{
    thPanel p;

    p.kind = thPanel::NODE_VALUE;
    p.title = "This patch";
    p.subtitle = "Click a node to see its parameters.";

    const auto field = [&] (const char *id, const char *label,
                            const string &value)
    {
        thPanelRow r;

        r.id = id;
        r.label = label;
        r.kind = thPanelRow::TEXT;
        r.editable = true;
        r.text = value;
        p.rows.push_back(r);

        return &p.rows.back();
    };

    field("name", "Name", header_.name);
    field("author", "Author", header_.author);

    thPanelRow *cat = field("category", "Category", header_.category);

    for (int i = 0; thDspCategories[i] != NULL; i++)
        cat->suggestions.push_back(thDspCategories[i]);

    field("description", "Description", header_.description);

    setPanel(p);
    showingHeader_ = true;
}

/* What was typed, turned into an intent by the provider and handed on.
 *
 * Nothing is written here, and nothing can be: a node's value lives in the
 * .dsp, and the splice is the window's -- it has the file, the undo and the
 * reload that follows. A refusal is dropped for the same reason the channel
 * panel drops one: every row this panel offers is a number or a name off a
 * list, so the ways to reach one are a stale panel and a graph rewired under
 * it, and in both the right answer is to leave the file alone. */
void NodeParamsView::onEdited (const string &row, const string &valueText)
{
    if (showingHeader_)
    {
        signal_header_edited_(row, valueText);
        return;
    }

    if (row == NAME_ROW && named_ >= 0)
    {
        signal_renamed_(named_, valueText);
        return;
    }

    if (exprArgs_.count(row))
    {
        signal_expression_edited_(exprNode_, exprArgs_[row], valueText);
        return;
    }

    thPanelEdit edit;

    const thPanelResult result = model_.propose(row, valueText, edit);

    if (!result.ok)
        return;

    /* Reported even when the value is the one the file already holds, which
     * is the one case the channel panel drops.
     *
     * Here that case means something: typing the file's number back is how
     * a person takes an edit back, and NodeEditor::onParamEdited puts the
     * file's own spelling back into the text, which clears the dirty mark.
     *
     * The guard that matters for a panel catching up is PanelView's, which
     * emits nothing at all while it is pushing a value in. */
    signal_param_edited_.emit(edit.a, edit.row, edit.value);
}
