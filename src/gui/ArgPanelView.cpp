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

#include "think.h"

#include "ArgPanelView.h"
#include "../gthPatchfile.h"

ArgPanelView::ArgPanelView (void)
{
    signal_edited().connect(
        sigc::mem_fun(*this, &ArgPanelView::onEdited));
}

ArgPanelView::~ArgPanelView (void)
{
    dropArgConns();
}

/* The controls are about to go; the args they subscribed to are not.
 *
 * Called from the destructor and from the top of rebuild(), so that a panel
 * built twice does not leave the first set of slots behind pointing at
 * widgets the second set replaced. */
void ArgPanelView::dropArgConns (void)
{
    for (size_t i = 0; i < argConns_.size(); i++)
        argConns_[i].disconnect();

    argConns_.clear();
}

bool ArgPanelView::rebuild (void)
{
    dropArgConns();

    thPanel built;

    const bool any = model_.build(built);

    setPanel(built);

    /* The other direction: MIDI controllers and the node editor both write
       these args behind the panel's back. The arg is handed to the slot by
       the signal, so only the row name is bound. */
    for (size_t i = 0; i < built.rows.size(); i++)
    {
        thArg *arg = model_.argFor(built.rows[i].id);

        if (arg == NULL)
            continue;

        argConns_.push_back(arg->signal_arg_changed().connect(
            sigc::bind(sigc::mem_fun(*this, &ArgPanelView::onArgChanged),
                       built.rows[i].id)));
    }

    return any;
}

/* What the person typed, turned into an intent by the provider and delivered
 * here.
 *
 * Nothing is done about a refusal yet. Every row a channel panel draws is a
 * number or a name off a list, so the only ways to reach one are a stale
 * panel and a command carrying a name the plugin does not implement -- and in
 * both of those the right answer is to leave the arg alone, which is what
 * this does. The first panel with a typed value in it is the one that needs
 * somewhere to put `why'. */
void ArgPanelView::onEdited (const string &row, const string &valueText)
{
    thPanelEdit edit;

    const thPanelResult result = model_.propose(row, valueText, edit);

    if (!result.ok || !result.changed)
        return;

    if (!model_.deliver(edit))
        return;

    /* Moving a control is editing the patch. Nothing writes it to disk, so
       this is the whole of the record that it happened. */
    if (model_.channel() >= 0)
        gthPatchManager::instance()->markDirty(model_.channel());
}

/* The arg is handed over by the signal and looked up again all the same.
 *
 * What a row shows is not simply the number in the arg -- a duration is
 * unfolded and a selector is not -- and the provider is the one that knows
 * which. Reading it from there rather than converting here is what stops the
 * two coming to disagree. */
void ArgPanelView::onArgChanged (thArg *arg, string row)
{
    double display = 0;

    (void)arg;

    if (model_.valueFor(row, display))
        setValue(row, display);
}
