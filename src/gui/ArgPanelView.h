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

#ifndef ARG_PANEL_VIEW_H
#define ARG_PANEL_VIEW_H

#include "ArgPanel.h"
#include "PanelView.h"

/* A channel's parameters, on the desktop: ArgPanel drawn by PanelView, with
 * the three things that join them up.
 *
 * The binder is the small half and it is where the two shells differ. It
 * delivers an edit -- immediately here, because the desktop can write the arg
 * and be done; as a broadcast command in the page, because the local tab has
 * no privileged path to the scheduler. It subscribes to the args the panel
 * names, so a MIDI controller or the node editor moving one moves the control
 * too. And it records that the patch has been edited, which is a fact about a
 * gthPatchManager and so cannot live in the model at all.
 *
 * This is what src/gui/ArgTable.cpp became, less everything that turned out
 * to be a rule rather than a drawing.
 */
class ArgPanelView : public PanelView
{
public:
    ArgPanelView (void);
    ~ArgPanelView (void);

    /* Which channel these parameters belong to, so that moving one can say
       the patch has been edited. -1 for none. */
    void setChannel (int chan) { model_.setChannel(chan); }

    /* The instrument's parameters or the channel effect's. See
       ArgPanel::setPrefix. */
    void setPrefix (const string &prefix) { model_.setPrefix(prefix); }

    /* A parameter not to draw. See ArgPanel::exclude. */
    void exclude (const string &name) { model_.exclude(name); }

    /* Builds the panel from whatever is on the channel now, and subscribes.
       False when the channel has no parameters to draw, which is how the
       effect block decides whether to make room for one. */
    bool rebuild (void);

private:
    void onEdited (const string &row, const string &valueText);
    void onArgChanged (thArg *arg, string row);
    void dropArgConns (void);

    /* The args outlive the controls subscribed to them. Glib::ObjectBase is
       a sigc::trackable so the slot would be dropped when this panel dies
       anyway, but that is a property of the base class rather than something
       this file says, and it does not cover a rebuild() that runs twice.
       Held and dropped explicitly instead. */
    std::vector<sigc::connection> argConns_;

    ArgPanel model_;
};

#endif /* ARG_PANEL_VIEW_H */
