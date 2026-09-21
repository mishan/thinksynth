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

#include <gtkmm.h>

#include "StageParamsView.h"

StageParamsView::StageParamsView (void)
{
    signal_edited().connect(
        sigc::mem_fun(*this, &StageParamsView::onEdited));
}

void StageParamsView::setStage (const thcGenEdit::Doc *doc,
                                thcScheduler *sched, int chain, int stage)
{
    why_.clear();

    model_.setPiece(doc, sched);
    model_.setStage(chain, stage);

    thPanel built;

    if (model_.build(built))
    {
        setPanel(built);
        return;
    }

    /* A stage with no params at all, or none this panel can describe -- a
       dsp node run at control rate has parameters and they are the other
       world's. Both are states worth saying rather than an empty column. */
    thPanel empty;

    empty.kind = thPanel::GEN_PARAM;
    empty.a = chain;
    empty.b = stage;
    empty.title = chain < 0 ? "No stage selected" : "No parameters";

    setPanel(empty);
}

/* What was typed, turned into an intent by the provider and handed on.
 *
 * Nothing is written here. The lasting half of an edit is a splice into the
 * .gen and the audible half is the running stage's param store, and the
 * window does both in that order -- it has the work copy and it is what
 * marks the piece dirty.
 *
 * A refusal is kept rather than dropped, which is where this differs from
 * the channel's panel and the node's. Those offer numbers and lists, so the
 * only way to a refusal is a panel gone stale under the person; this offers
 * a box to type note names and preset names into, and "'H4' is not a note
 * name" is the answer to something somebody just did. */
void StageParamsView::onEdited (const string &row, const string &valueText)
{
    thPanelEdit edit;

    const thPanelResult result = model_.propose(row, valueText, edit);

    why_ = result.why;

    if (!result.ok || !result.changed)
        return;

    signal_param_edited_.emit(edit);
}
