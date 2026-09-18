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

#include "NodeCanvasWidget.h"

NodeCanvasWidget::NodeCanvasWidget (void)
    : NodeCanvas(), GraphCanvas(*static_cast<NodeCanvas *>(this))
{
    /* Input, through controllers. There are no on_*_event vfuncs in GTK4
       and no event mask to widen -- a controller receives the kind of
       thing it is for, and is handed the coordinates rather than being
       asked to fetch them. */

    /* Only the first button is wanted: everything here is a left-drag. */
    click_ = Gtk::GestureClick::create();
    click_->set_button(1);
    click_->signal_pressed().connect(
        sigc::mem_fun(*this, &NodeCanvas::onPressed));
    click_->signal_released().connect(
        sigc::mem_fun(*this, &NodeCanvas::onReleased));
    add_controller(click_);

    /* A second gesture rather than widening the first: GestureClick
       filters by button, and asking one controller for both would mean
       every left-drag path testing which button it was. */
    rightClick_ = Gtk::GestureClick::create();
    rightClick_->set_button(3);
    rightClick_->signal_pressed().connect(
        sigc::mem_fun(*this, &NodeCanvas::onRightPressed));
    add_controller(rightClick_);

    motion_ = Gtk::EventControllerMotion::create();
    motion_->signal_motion().connect(
        sigc::mem_fun(*this, &NodeCanvas::onMotion));
    motion_->signal_leave().connect(
        sigc::mem_fun(*this, &NodeCanvas::onLeave));
    add_controller(motion_);
}
