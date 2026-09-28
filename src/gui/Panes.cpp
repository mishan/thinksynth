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

#include "Panes.h"

/* Held with a reference of its own for as long as this is, sunk rather than
 * taken: a new widget's reference is floating, and a parent would otherwise
 * be the one to sink it and the only one holding it. So the panes outlive
 * being taken off a window, and go when this does.
 *
 * Glib::wrap of a type gtkmm does not know gives the nearest one it does,
 * which is Gtk::Widget -- enough to pack. The wrapper belongs to the C
 * object and goes with it.
 */
Panes::Panes (void)
{
    panes_ = MLN_PANES(g_object_ref_sink(mln_panes_new()));
    widget_ = Glib::wrap(GTK_WIDGET(panes_));

    g_signal_connect(panes_, "pane-shown", G_CALLBACK(onPaneShown), this);
    g_signal_connect(panes_, "layout-changed", G_CALLBACK(onLayoutChanged),
                     this);
    g_signal_connect(panes_, "layout-kept", G_CALLBACK(onLayoutKept), this);
}

Panes::~Panes (void)
{
    g_signal_handlers_disconnect_by_data(panes_, this);

    /* Off the box or window it was packed into, so that the last
       reference is this one and the panes' content goes now rather than
       whenever the parent does. Those are the two it is packed into; a
       parent of another kind keeps it until that parent goes. */
    if (Gtk::Widget *parent = widget_->get_parent())
    {
        if (Gtk::Box *box = dynamic_cast<Gtk::Box *>(parent))
            box->remove(*widget_);
        else if (Gtk::Window *win = dynamic_cast<Gtk::Window *>(parent))
            win->unset_child();
    }

    g_object_unref(panes_);
}

void Panes::onPaneShown (MlnPanes *, const char *id, gboolean visible,
                         gpointer self)
{
    static_cast<Panes *>(self)->paneShown_.emit(id, visible != FALSE);
}

void Panes::onLayoutChanged (MlnPanes *, const char *mode, gpointer self)
{
    static_cast<Panes *>(self)->layoutChanged_.emit(mode ? mode : "");
}

void Panes::onLayoutKept (MlnPanes *, const char *mode, const char *text,
                          gpointer self)
{
    static_cast<Panes *>(self)->layoutKept_.emit(mode ? mode : "",
                                                 text ? text : "");
}

bool Panes::add (const std::string &id, const std::string &title,
                 Gtk::Widget &content, int minWidth)
{
    return mln_panes_register(panes_, id.c_str(), title.c_str(),
                              content.gobj(), minWidth);
}

void Panes::setTitle (const std::string &id, const std::string &title)
{
    mln_panes_set_title(panes_, id.c_str(), title.c_str());
}

void Panes::setAvailable (const std::string &id, bool available)
{
    mln_panes_set_available(panes_, id.c_str(), available);
}

void Panes::setAttention (const std::string &id, bool attention)
{
    mln_panes_set_attention(panes_, id.c_str(), attention);
}

void Panes::setPaneMenu (const std::string &id,
                         const Glib::RefPtr<Gio::MenuModel> &menu)
{
    mln_panes_set_pane_menu(panes_, id.c_str(), menu ? menu->gobj() : NULL);
}

void Panes::present (const std::string &id, bool focus)
{
    mln_panes_present(panes_, id.c_str(), focus);
}

void Panes::close (const std::string &id)
{
    mln_panes_close(panes_, id.c_str());
}

bool Panes::isVisible (const std::string &id)
{
    return mln_panes_is_visible(panes_, id.c_str());
}

std::vector<std::string> Panes::closed (void)
{
    std::vector<std::string> out;
    char **ids = mln_panes_get_closed(panes_);

    for (char **i = ids; i != NULL && *i != NULL; i++)
        out.push_back(*i);

    g_strfreev(ids);

    return out;
}

bool Panes::setDefault (const std::string &mode, const std::string &json)
{
    return mln_panes_set_default(panes_, mode.c_str(), json.c_str());
}

void Panes::setMode (const std::string &mode)
{
    mln_panes_set_mode(panes_, mode.c_str());
}

bool Panes::load (const std::string &kept)
{
    return mln_panes_load(panes_, kept.empty() ? NULL : kept.c_str());
}

std::string Panes::layout (void)
{
    char *text = mln_panes_get_layout(panes_);
    std::string out = text ? text : "";

    g_free(text);

    return out;
}

bool Panes::setLayout (const std::string &json)
{
    return mln_panes_set_layout(panes_, json.c_str());
}

void Panes::reset (void)
{
    mln_panes_reset(panes_);
}

bool Panes::tabBounds (const std::string &id, graphene_rect_t &bounds)
{
    return mln_panes_get_tab_bounds(panes_, id.c_str(), &bounds);
}

bool Panes::leafBounds (const std::string &id, graphene_rect_t &bounds)
{
    return mln_panes_get_leaf_bounds(panes_, id.c_str(), &bounds);
}
