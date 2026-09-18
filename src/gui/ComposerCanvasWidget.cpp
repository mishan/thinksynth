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

#include "ComposerCanvasWidget.h"

ComposerCanvasWidget::ComposerCanvasWidget (void)
    : ComposerCanvas(), GraphCanvas(*static_cast<ComposerCanvas *>(this))
{
    auto click = Gtk::GestureClick::create();

    /* Through a lambda rather than straight to the handler, because
       which button was pressed is the *controller's* to answer and the
       signal does not carry it. A plugin that could not tell a primary
       click from a secondary one would have half an input API -- a Life
       board wants left to draw and right to erase. */
    click->signal_pressed().connect(
        [this, click](int n, double x, double y)
        { onPressed(n, x, y, (int)click->get_current_button()); });
    click->signal_released().connect(
        [this, click](int n, double x, double y)
        { onReleased(n, x, y, (int)click->get_current_button()); });
    add_controller(click);

    /* Motion, for painting a plugin's picture by dragging across it.
       Separate from the drag gesture below because that one exists to
       move stage boxes around and reports offsets; a plugin wants
       positions. */
    auto motion = Gtk::EventControllerMotion::create();

    motion->signal_motion().connect(
        sigc::mem_fun(*this, &ComposerCanvas::onMotion));
    add_controller(motion);

    auto keys = Gtk::EventControllerKey::create();

    keys->signal_key_pressed().connect(
        sigc::mem_fun(*this, &ComposerCanvasWidget::onKey), false);
    add_controller(keys);

    set_focusable(true);

    auto drag = Gtk::GestureDrag::create();

    drag->signal_drag_begin().connect(
        sigc::mem_fun(*this, &ComposerCanvas::onDragBegin));
    drag->signal_drag_update().connect(
        sigc::mem_fun(*this, &ComposerCanvas::onDragUpdate));
    drag->signal_drag_end().connect(
        sigc::mem_fun(*this, &ComposerCanvas::onDragEnd));
    add_controller(drag);
}

/* The toolkit's keysym to the content's name for it. The content sees
   KEY_ESCAPE and never a GDK_KEY_; a browser shell does the same with
   a KeyboardEvent. */
bool
ComposerCanvasWidget::onKey (guint keyval, guint, Gdk::ModifierType)
{
    if (keyval == GDK_KEY_Escape)
        return keyPressed(KEY_ESCAPE);

    return false;
}
