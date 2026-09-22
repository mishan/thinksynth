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

#ifndef DSP_BROWSER_H
#define DSP_BROWSER_H 1

#include "../DspCatalog.h"

/* One row: a group, or a graph that can be chosen.
 *
 * `file' is what a .patch's `dsp' line would say -- "ts1.dsp", "fx/echo.dsp"
 * -- and is empty for a group, which is how everything here tells the two
 * apart. The same arrangement PaletteRow has, for the same model.
 */
class BrowserRow : public Glib::Object
{
public:
    static Glib::RefPtr<BrowserRow> create (const Glib::ustring &label,
                                            const Glib::ustring &detail,
                                            const string &file)
    {
        return Glib::make_refptr_for_instance(
            new BrowserRow(label, detail, file));
    }

    Glib::ustring label (void) const { return label_; }
    Glib::ustring detail (void) const { return detail_; }
    string file (void) const { return file_; }
    bool isGroup (void) const { return file_.empty(); }

    const Glib::RefPtr<Gio::ListStore<BrowserRow> > &children (void) const {
        return children_;
    }

    void addChild (const Glib::RefPtr<BrowserRow> &child)
    {
        if (!children_)
            children_ = Gio::ListStore<BrowserRow>::create();

        children_->append(child);
    }

protected:
    BrowserRow (const Glib::ustring &label, const Glib::ustring &detail,
                const string &file)
        : Glib::ObjectBase(typeid(BrowserRow)),
          label_(label), detail_(detail), file_(file) { }

private:
    Glib::ustring label_, detail_;
    string file_;
    Glib::RefPtr<Gio::ListStore<BrowserRow> > children_;
};

/*
 * Choosing a graph, by what it is rather than by what it is called.
 *
 * What this replaces is a Gtk::FileChooserDialog over dsp/ -- sixty-one
 * filenames, no filter, no descriptions, and `bd10.dsp' sitting next to
 * `bdshaped.dsp' with nothing to say that both of them are called BD-10. The
 * titles and the descriptions have been written into every shipped file for
 * years and no chooser read them; DspCatalog is what makes reading them
 * cheap, and this is the half the user sees.
 *
 * THE EFFECT SPLIT IS ENFORCED HERE. An effect graph and an instrument are
 * the same file format and are not interchangeable, and until now that was
 * discovered after the fact: the effect Browse opened the same chooser at the
 * same directory, and picking an instrument got a dialog saying it was the
 * wrong kind of graph. A browser that knows which of the two each file is can
 * simply not offer the other one, which is the difference between a rule and
 * a complaint.
 *
 * WHAT IT HANDS BACK is the name a file names it by -- `ts1.dsp',
 * `fx/echo.dsp' -- and not a path, because gthPatchManager resolves names
 * (resolveDsp) and stores the name it was given, so a patch saved afterwards
 * carries the short name rather than this machine's absolute path. Other
 * File... is the exception and hands back exactly what the chooser said: a
 * graph outside the tree has no short name.
 */
class DspBrowser : public Gtk::Dialog
{
public:
    enum Kind {
        INSTRUMENTS,    /* the graphs a channel plays notes on */
        EFFECTS         /* the graphs that run on a channel's summed voices */
    };

    /* Scans `dir' -- thUtil::findDataDir("dsp", ...) -- and shows what is in
       it. `current' is the name already on the channel, selected when it is
       one of them, so the dialog opens where the user is rather than at the
       top of the list. */
    DspBrowser (Gtk::Window &parent, Kind kind, const string &dir,
                const string &current = "");

    /* The choice. Emitted once, after which the dialog closes itself. */
    typedef sigc::signal<void(string)> type_signal_chosen;
    type_signal_chosen signal_chosen (void) { return m_signal_chosen_; }

protected:
    void rebuild (void);
    void onFilterChanged (void);
    void onSelectionChanged (void);
    void onRowActivated (guint position);
    void onResponse (int response);
    void onOtherFile (void);
    void onOtherFileResponse (int response, Gtk::FileChooserDialog *chooser);

    /* The file under the cursor, or "" when a group row is selected. */
    string selectedFile (void);

    void choose (const string &file);

private:
    DspCatalog catalog_;
    Kind kind_;
    string dir_;
    string current_;

    Gtk::Entry filter_;
    Gtk::ScrolledWindow scroller_;
    /* A ListView and not a ColumnView, for NodePalette's reason: this is one
       unnamed column, and a ColumnView always draws a header. */
    Gtk::ListView list_;
    Gtk::Label detail_;

    Glib::RefPtr<Gio::ListStore<BrowserRow> > store_;
    Glib::RefPtr<Gtk::TreeListModel> treeModel_;
    Glib::RefPtr<Gtk::SingleSelection> selection_;

    Gtk::Button *openBtn_;

    type_signal_chosen m_signal_chosen_;
};

#endif /* DSP_BROWSER_H */
