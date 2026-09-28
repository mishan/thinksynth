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

#ifndef PANES_H
#define PANES_H

#include <string>
#include <vector>

#include <gtkmm.h>

#include <mullion-gtk.h>

/* mullion-gtk's MlnPanes, in gtkmm's terms: the window's panes, tiled as
 * the web page's are, and laid out from the same layout text.
 *
 * Not a Gtk::Widget subclass. MlnPanes is a C type gtkmm has no wrapper
 * for, and deriving one would mean registering a C++ type over it for the
 * sake of a constructor; what the window needs is a widget to pack and a
 * dozen calls, so this holds the widget and widget() is what gets packed.
 *
 * The content of a pane is parented by MlnPanes for as long as it is a
 * pane, and a moved pane is never unparented. A managed widget goes when
 * the panes do; one that is a member of something has to outlive them, so
 * whatever holds both destroys this first.
 */
class Panes : public sigc::trackable
{
public:
    Panes (void);
    ~Panes (void);

    Panes (const Panes &) = delete;
    Panes &operator= (const Panes &) = delete;

    Gtk::Widget &widget (void) { return *widget_; }
    MlnPanes *gobj (void) { return panes_; }

    /* A pane there from the start: before load(). */
    bool add (const std::string &id, const std::string &title,
              Gtk::Widget &content, int minWidth);

    void setTitle (const std::string &id, const std::string &title);
    void setAvailable (const std::string &id, bool available);
    void setAttention (const std::string &id, bool attention);

    /* The app's own items for a pane's tab menu, looked up from the tab
       up, so the window's actions are there. */
    void setPaneMenu (const std::string &id,
                      const Glib::RefPtr<Gio::MenuModel> &menu);

    void present (const std::string &id, bool focus = true);
    void close (const std::string &id);
    bool isVisible (const std::string &id);

    /* The panes in the drawer, in the order it lists them. */
    std::vector<std::string> closed (void);

    bool setDefault (const std::string &mode, const std::string &json);
    void setMode (const std::string &mode);

    /* A kept layout for the mode, or empty for the default. True if the
       kept one was used. */
    bool load (const std::string &kept);
    std::string layout (void);
    bool setLayout (const std::string &json);
    void reset (void);

    /* Where a pane's tab and its leaf are drawn, in the widget's
       coordinates. False for a pane that is not drawn. */
    bool tabBounds (const std::string &id, graphene_rect_t &bounds);
    bool leafBounds (const std::string &id, graphene_rect_t &bounds);

    /* A pane came into view or went out of it: behind another tab, in the
       drawer, or its window hidden. */
    sigc::signal<void (const std::string &, bool)> &signal_pane_shown (void)
    {
        return paneShown_;
    }

    /* The layout changed, by a person or by the app. */
    sigc::signal<void (const std::string &)> &signal_layout_changed (void)
    {
        return layoutChanged_;
    }

    /* Keep `text' for `mode'; empty text: forget what was kept. */
    sigc::signal<void (const std::string &, const std::string &)> &
    signal_layout_kept (void)
    {
        return layoutKept_;
    }

private:
    static void onPaneShown (MlnPanes *, const char *id, gboolean visible,
                             gpointer self);
    static void onLayoutChanged (MlnPanes *, const char *mode, gpointer self);
    static void onLayoutKept (MlnPanes *, const char *mode, const char *text,
                              gpointer self);

    MlnPanes *panes_;
    Gtk::Widget *widget_;

    sigc::signal<void (const std::string &, bool)> paneShown_;
    sigc::signal<void (const std::string &)> layoutChanged_;
    sigc::signal<void (const std::string &, const std::string &)> layoutKept_;
};

#endif /* PANES_H */
