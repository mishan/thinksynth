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
        [this] (int n, double x, double y)
        {
            const unsigned mods = modsOf(click_->get_current_event_state()) |
                                  (spaceHeld_ ? MOD_PAN : 0);

            if (mods & MOD_PAN)
                set_cursor("grabbing");

            onPressed(n, x, y, mods);
        });
    click_->signal_released().connect(
        [this] (int n, double x, double y)
        {
            onReleased(n, x, y);
            set_cursor(spaceHeld_ ? "grab" : "");
        });
    add_controller(click_);

    /* The middle button pans, as it does in most things with a canvas. */
    pan_ = Gtk::GestureClick::create();
    pan_->set_button(2);
    pan_->signal_pressed().connect(
        [this] (int n, double x, double y)
        {
            set_cursor("grabbing");
            onPressed(n, x, y, MOD_PAN);
        });
    pan_->signal_released().connect(
        [this] (int n, double x, double y)
        {
            onReleased(n, x, y);
            set_cursor(spaceHeld_ ? "grab" : "");
        });
    add_controller(pan_);

    /* So the editor's keys -- Ctrl+Z -- go to the graph clicked on, not to
       whatever had the focus before. */
    set_focusable(true);
    click_->signal_pressed().connect(
        [this] (int, double, double) { grab_focus(); });

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

    keys_ = Gtk::EventControllerKey::create();
    keys_->signal_key_pressed().connect(
        [this] (guint keyval, guint, Gdk::ModifierType state)
        {
            const unsigned mods = modsOf(state);
            Key key = KEY_NONE;

            /* Held, a left drag pans; repeats while held say nothing new. */
            if (keyval == GDK_KEY_space)
            {
                if (!spaceHeld_)
                    set_cursor("grab");

                spaceHeld_ = true;
                return true;
            }

            switch (keyval)
            {
                case GDK_KEY_Escape:    key = KEY_ESCAPE; break;
                case GDK_KEY_Left:
                case GDK_KEY_KP_Left:   key = KEY_LEFT; break;
                case GDK_KEY_Right:
                case GDK_KEY_KP_Right:  key = KEY_RIGHT; break;
                case GDK_KEY_Up:
                case GDK_KEY_KP_Up:     key = KEY_UP; break;
                case GDK_KEY_Down:
                case GDK_KEY_KP_Down:   key = KEY_DOWN; break;
                case GDK_KEY_Delete:
                case GDK_KEY_KP_Delete:
                case GDK_KEY_BackSpace: key = KEY_DELETE; break;
            }

            return key != KEY_NONE && keyPressed(key, mods);
        }, false);
    keys_->signal_key_released().connect(
        [this] (guint keyval, guint, Gdk::ModifierType)
        {
            if (keyval == GDK_KEY_space)
            {
                spaceHeld_ = false;
                set_cursor("");
            }
        });
    add_controller(keys_);

    /* Space let go of elsewhere is never heard here. */
    Glib::RefPtr<Gtk::EventControllerFocus> focus =
        Gtk::EventControllerFocus::create();

    focus->signal_leave().connect([this]
        {
            spaceHeld_ = false;
            set_cursor("");
        });
    add_controller(focus);

    /* A shortcut rather than a keysym in the controller above: a trigger
       matches the key that is A on the layout the keyboard has, a Russian
       one included. The canvas's own, so a text field keeps its Ctrl+A. */
    Glib::RefPtr<Gtk::ShortcutController> all =
        Gtk::ShortcutController::create();

    all->add_shortcut(Gtk::Shortcut::create(
        Gtk::ShortcutTrigger::parse_string("<Primary>a"),
        Gtk::CallbackAction::create(
            [this] (Gtk::Widget &, const Glib::VariantBase &)
            {
                return keyPressed(KEY_SELECT_ALL, MOD_CTRL);
            })));
    add_controller(all);
}

/* Cmd on a Mac is what Ctrl is elsewhere. Only there: an X keymap can
   put Meta on the same modifier as Alt. */
unsigned NodeCanvasWidget::modsOf (Gdk::ModifierType state)
{
    using M = Gdk::ModifierType;

#ifdef __APPLE__
    const M primary = M::META_MASK;
#else
    const M primary = M::CONTROL_MASK;
#endif

    return ((state & M::SHIFT_MASK) == M::SHIFT_MASK ? MOD_SHIFT : 0) |
           ((state & primary) == primary ? MOD_CTRL : 0);
}
