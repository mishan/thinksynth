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

#ifndef ITEM_BROWSER_H
#define ITEM_BROWSER_H 1

#include <string>
#include <vector>

/*
 * Choosing a file by what it is rather than by what it is called.
 *
 * What this replaces, twice over, is a Gtk::FileChooserDialog: sixty-one
 * filenames over dsp/ with no filter and no descriptions, and thirty-one over
 * gen/ the same way. Every one of those files declares a title and a
 * description and no chooser read either -- and a filename is often not
 * enough on its own: `rpiano0.dsp' and `rpiano1.dsp' differ by one character
 * and by which filter they run, which is a thing the description says and
 * nothing else does.
 *
 * WHAT IT KNOWS is a list of groups of rows, and nothing about where they
 * came from. The rows are asked for again on every keystroke in the filter
 * box, through `Provider', because deciding which rows a filter leaves is a
 * rule about a corpus and not about a widget: DspCatalog::matches and
 * GenCatalog::matches are those rules, and they are checked by harnesses with
 * no display anywhere near them. A widget that filtered a list it was handed
 * once would be a third copy of that rule, reachable only by pressing keys.
 *
 * WHAT IT HANDS BACK is whatever the row's `file' said. The two callers mean
 * different things by that on purpose: a graph is named (`ts1.dsp',
 * `fx/echo.dsp') because gthPatchManager resolves names and stores the name it
 * was given, so a patch saved afterwards carries the short one; a piece is a
 * path, because opening one copies a file rather than resolving a name.
 * Other File... hands back exactly what the chooser said either way.
 */

/* One row. `note' is the line under the list when the row is selected -- the
   filename, and whatever else is worth knowing but not worth a column. */
struct BrowserItem {
    std::string file;
    std::string name;
    std::string desc;
    std::string note;
};

struct BrowserGroup {
    std::string name;
    std::vector<BrowserItem> items;
};

/* One row in the view: a group, or something that can be chosen.
 *
 * `file' is empty for a group, which is how everything here tells the two
 * apart. The same arrangement PaletteRow has, for the same model. */
class BrowserRow : public Glib::Object
{
public:
    static Glib::RefPtr<BrowserRow> create (const Glib::ustring &label,
                                            const Glib::ustring &detail,
                                            const std::string &file,
                                            const std::string &note)
    {
        return Glib::make_refptr_for_instance(
            new BrowserRow(label, detail, file, note));
    }

    Glib::ustring label (void) const { return label_; }
    Glib::ustring detail (void) const { return detail_; }
    std::string file (void) const { return file_; }
    std::string note (void) const { return note_; }
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
                const std::string &file, const std::string &note)
        : Glib::ObjectBase(typeid(BrowserRow)),
          label_(label), detail_(detail), file_(file), note_(note) { }

private:
    Glib::ustring label_, detail_;
    std::string file_, note_;
    Glib::RefPtr<Gio::ListStore<BrowserRow> > children_;
};

class ItemBrowser : public Gtk::Dialog
{
public:
    /* What the list holds, given what is in the filter box. Called on every
       keystroke; see the header comment for why it is a callback. */
    typedef sigc::slot<std::vector<BrowserGroup>(const std::string &)>
        Provider;

    /* `otherDir' is where Other File... opens, and "" leaves that button off.
       `current' is what is already in use, selected when it is one of the
       rows, so the dialog opens where the user is rather than at the top of
       the list. */
    ItemBrowser (Gtk::Window &parent, const Glib::ustring &title,
                 const Provider &provider, const std::string &otherDir,
                 const std::string &current = "");

    /* The choice. Emitted once, after which the dialog closes itself. */
    typedef sigc::signal<void(std::string)> type_signal_chosen;
    type_signal_chosen signal_chosen (void) { return m_signal_chosen_; }

    /* Shown when the provider comes back with nothing at all -- not a filter
       that matched nothing, but a corpus that is not there. Naming the
       directory is the whole message. */
    void setEmptyNote (const Glib::ustring &markup);

protected:
    void rebuild (void);
    void onFilterChanged (void);
    void onSelectionChanged (void);
    void onRowActivated (guint position);
    void onResponse (int response);
    void onOtherFile (void);
    void onOtherFileResponse (int response, Gtk::FileChooserDialog *chooser);

    /* The file under the cursor, or "" when a group row is selected. */
    std::string selectedFile (void);

    void choose (const std::string &file);

    /* protected rather than private for editorcheck's reason: which
       widgets this keeps is its own business, and a harness that presses
       them is a subclass rather than a wider header. */
    Provider provider_;
    std::string otherDir_;
    /* The row a rebuild puts the cursor back on: the file already in use to
       begin with, and after that whatever the user last looked at. Typing
       into the filter builds a new model on every keystroke, and going back
       to the file in use there would drag the cursor off the row being read,
       once per character. */
    std::string selected_;

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

#endif /* ITEM_BROWSER_H */
