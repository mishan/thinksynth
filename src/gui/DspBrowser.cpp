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

#include <stdio.h>

#include <gtkmm.h>

#include "think.h"

#include "Dialogs.h"
#include "DspBrowser.h"

DspBrowser::DspBrowser (Gtk::Window &parent, Kind kind, const string &dir,
                        const string &current)
    : Gtk::Dialog(kind == EFFECTS ? "thinksynth - Channel Effect"
                                  : "thinksynth - Instrument",
                  parent, true),
      kind_(kind), dir_(dir), current_(current), openBtn_(NULL)
{
    set_default_size(460, 520);

    filter_.set_placeholder_text("Filter");
    filter_.signal_changed().connect(
        sigc::mem_fun(*this, &DspBrowser::onFilterChanged));

    store_ = Gio::ListStore<BrowserRow>::create();

    /* Two lines per row: the title the file declares, and its description
       under it in small type. That is the whole point of the dialog -- the
       descriptions have been in the files all along and the chooser this
       replaces showed filenames. A group row uses the first line only. */
    Glib::RefPtr<Gtk::SignalListItemFactory> factory =
        Gtk::SignalListItemFactory::create();

    factory->signal_setup().connect(
        [](const Glib::RefPtr<Gtk::ListItem> &item)
        {
            Gtk::TreeExpander *expander =
                Gtk::make_managed<Gtk::TreeExpander>();
            Gtk::Box *box =
                Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
            Gtk::Label *title = Gtk::make_managed<Gtk::Label>();
            Gtk::Label *detail = Gtk::make_managed<Gtk::Label>();

            title->set_halign(Gtk::Align::START);
            title->set_ellipsize(Pango::EllipsizeMode::END);

            detail->set_halign(Gtk::Align::START);
            detail->set_ellipsize(Pango::EllipsizeMode::END);
            detail->add_css_class("dim-label");
            detail->add_css_class("caption");

            box->append(*title);
            box->append(*detail);

            expander->set_child(*box);
            item->set_child(*expander);
        });

    factory->signal_bind().connect(
        [](const Glib::RefPtr<Gtk::ListItem> &item)
        {
            Gtk::TreeExpander *expander =
                dynamic_cast<Gtk::TreeExpander *>(item->get_child());

            if (expander == NULL)
                return;

            Glib::RefPtr<Gtk::TreeListRow> treeRow =
                std::dynamic_pointer_cast<Gtk::TreeListRow>(item->get_item());

            expander->set_list_row(treeRow);

            Gtk::Box *box = dynamic_cast<Gtk::Box *>(expander->get_child());

            if (box == NULL)
                return;

            Gtk::Label *title =
                dynamic_cast<Gtk::Label *>(box->get_first_child());
            Gtk::Label *detail =
                title ? dynamic_cast<Gtk::Label *>(title->get_next_sibling())
                      : NULL;

            Glib::RefPtr<BrowserRow> row =
                treeRow ? std::dynamic_pointer_cast<BrowserRow>(
                              treeRow->get_item())
                        : Glib::RefPtr<BrowserRow>();

            if (title)
                title->set_text(row ? row->label() : Glib::ustring());

            if (detail)
            {
                detail->set_text(row ? row->detail() : Glib::ustring());
                detail->set_visible(row && !row->detail().empty());
            }
        });

    list_.set_factory(factory);

    /* Double-click, or Enter, opens -- because that is what a chooser does. */
    list_.signal_activate().connect(
        sigc::mem_fun(*this, &DspBrowser::onRowActivated));

    scroller_.set_policy(Gtk::PolicyType::AUTOMATIC,
                         Gtk::PolicyType::AUTOMATIC);
    scroller_.set_child(list_);
    scroller_.set_vexpand(true);

    detail_.set_xalign(0.0);
    detail_.set_wrap(true);
    detail_.set_margin_start(6);
    detail_.set_margin_end(6);

    Gtk::Box *body = get_content_area();

    body->set_spacing(6);
    body->set_margin_start(6);
    body->set_margin_end(6);
    body->set_margin_top(6);
    body->set_margin_bottom(6);

    body->append(filter_);
    body->append(scroller_);
    body->append(detail_);

    /* The escape hatch. A .dsp of one's own does not live under dsp/ and has
       no name the catalog knows, so the old chooser stays reachable -- it is
       no longer the only way to choose a graph, which is the change. */
    add_button("_Other File...", Gtk::ResponseType::ACCEPT);
    add_button("_Cancel", Gtk::ResponseType::CANCEL);
    openBtn_ = add_button("_Open", Gtk::ResponseType::OK);

    openBtn_->set_sensitive(false);

    signal_response().connect(sigc::mem_fun(*this, &DspBrowser::onResponse));

    catalog_.scan(dir_);

    rebuild();

    if (catalog_.count() == 0)
        detail_.set_markup("<i>No graphs in</i>\n<tt>" +
                           Glib::Markup::escape_text(dir_) + "</tt>\n"
                           "<small>Set THINK_DSP_PATH, or use Other "
                           "File...</small>");
}

void DspBrowser::rebuild (void)
{
    const string needle = filter_.get_text();

    store_->remove_all();

    /* The group holding what is already on the channel, so it can be opened
       below; empty while filtering, when the selection is not the point. */
    Glib::ustring openGroup;

    for (size_t g = 0; g < catalog_.groups().size(); g++)
    {
        const string &group = catalog_.groups()[g];
        const vector<DspCatalog::Entry> &list = catalog_.inGroup(group);

        Glib::RefPtr<BrowserRow> groupRow;

        for (size_t e = 0; e < list.size(); e++)
        {
            /* Which rows a chooser has is DspCatalog::matches, and not a
               rule of this widget's: an effect is not offered in the
               instrument dialog or the other way round, and the filter reads
               the title, the description and the filename. Deciding it there
               is what lets scripts/dspcatalog hold it still without a
               display. */
            if (!DspCatalog::matches(list[e], kind_ == EFFECTS, needle))
                continue;

            if (!groupRow)
                groupRow = BrowserRow::create(group, "", "");

            groupRow->addChild(BrowserRow::create(list[e].name,
                                                  list[e].desc,
                                                  list[e].file));

            if (!current_.empty() && list[e].file == current_)
                openGroup = group;
        }

        /* Appended once it has its children, the way NodePalette fills a
           category row before the model sees it: a group handed over empty is
           a group Gtk::TreeListModel is told has nothing to expand. */
        if (groupRow)
            store_->append(groupRow);
    }

    /* A fresh model each time, so expanded state starts from nothing rather
       than from whatever the last filter left behind. autoexpand while
       filtering, because filtering with everything collapsed hides the
       results. */
    treeModel_ = Gtk::TreeListModel::create(
        store_,
        [](const Glib::RefPtr<Glib::ObjectBase> &item)
            -> Glib::RefPtr<Gio::ListModel>
        {
            Glib::RefPtr<BrowserRow> row =
                std::dynamic_pointer_cast<BrowserRow>(item);

            /* Null means "a leaf": no arrow, nothing to expand. */
            if (!row || !row->children())
                return Glib::RefPtr<Gio::ListModel>();

            return row->children();
        },
        false,                              /* passthrough */
        !filter_.get_text().empty());       /* autoexpand  */

    selection_ = Gtk::SingleSelection::create(treeModel_);

    selection_->set_autoselect(false);
    selection_->set_can_unselect(true);
    selection_->set_selected(GTK_INVALID_LIST_POSITION);

    selection_->property_selected().signal_changed().connect(
        sigc::mem_fun(*this, &DspBrowser::onSelectionChanged));

    list_.set_model(selection_);

    /* What is on the channel now, selected: a dialog that opens on the
       current graph is a dialog that answers "what is this?" as well as
       "what else is there?". Expanding its group is what makes the row
       exist -- a collapsed group's children are not rows in the flattened
       model at all.
     *
     * No scroll to it: Gtk::ListView::scroll_to arrived in 4.12 and this
     * builds against 4.6. The row is selected, so Open acts on it either
     * way. */
    if (!openGroup.empty())
    {
        for (guint i = 0; i < treeModel_->get_n_items(); i++)
        {
            Glib::RefPtr<Gtk::TreeListRow> treeRow = treeModel_->get_row(i);
            Glib::RefPtr<BrowserRow> row =
                treeRow ? std::dynamic_pointer_cast<BrowserRow>(
                              treeRow->get_item())
                        : Glib::RefPtr<BrowserRow>();

            if (row && row->isGroup() && row->label() == openGroup)
            {
                treeRow->set_expanded(true);
                break;
            }
        }

        for (guint i = 0; i < treeModel_->get_n_items(); i++)
        {
            Glib::RefPtr<Gtk::TreeListRow> treeRow = treeModel_->get_row(i);
            Glib::RefPtr<BrowserRow> row =
                treeRow ? std::dynamic_pointer_cast<BrowserRow>(
                              treeRow->get_item())
                        : Glib::RefPtr<BrowserRow>();

            if (row && row->file() == current_)
            {
                selection_->set_selected(i);
                break;
            }
        }
    }
}

void DspBrowser::onFilterChanged (void)
{
    rebuild();
}

string DspBrowser::selectedFile (void)
{
    if (!selection_)
        return "";

    Glib::RefPtr<Gtk::TreeListRow> treeRow =
        std::dynamic_pointer_cast<Gtk::TreeListRow>(
            selection_->get_selected_item());

    if (!treeRow)
        return "";

    Glib::RefPtr<BrowserRow> row =
        std::dynamic_pointer_cast<BrowserRow>(treeRow->get_item());

    return row ? row->file() : "";
}

void DspBrowser::onSelectionChanged (void)
{
    const string file = selectedFile();

    if (openBtn_)
        openBtn_->set_sensitive(!file.empty());

    const DspCatalog::Entry *e = file.empty() ? NULL : catalog_.find(file);

    if (e == NULL)
    {
        detail_.set_text("");
        return;
    }

    /* The filename, because that is what a .patch's `dsp' line will say and
       what a piece names in its `dsp' clause -- the one thing the row does
       not show and the one thing worth knowing about a file you are about to
       put in a document. */
    string text = "<tt>" + Glib::Markup::escape_text(e->file) + "</tt>";

    if (!e->author.empty())
        text += "  <small>" + Glib::Markup::escape_text(e->author) +
                "</small>";

    detail_.set_markup(text);
}

void DspBrowser::onRowActivated (guint position)
{
    if (!treeModel_)
        return;

    Glib::RefPtr<Gtk::TreeListRow> treeRow = treeModel_->get_row(position);

    if (!treeRow)
        return;

    Glib::RefPtr<BrowserRow> row =
        std::dynamic_pointer_cast<BrowserRow>(treeRow->get_item());

    if (!row)
        return;

    /* A group row toggles rather than opening anything. Expanded state
       belongs to the Gtk::TreeListRow, not to ours. */
    if (row->isGroup())
    {
        treeRow->set_expanded(!treeRow->get_expanded());
        return;
    }

    choose(row->file());
}

void DspBrowser::choose (const string &file)
{
    if (file.empty())
        return;

    m_signal_chosen_(file);

    closeDialog(this);
}

void DspBrowser::onResponse (int response)
{
    if (response == Gtk::ResponseType::OK)
    {
        choose(selectedFile());
        return;
    }

    if (response == Gtk::ResponseType::ACCEPT)
    {
        onOtherFile();
        return;
    }

    closeDialog(this);
}

void DspBrowser::onOtherFile (void)
{
    Gtk::FileChooserDialog *chooser =
        new Gtk::FileChooserDialog(*this, "thinksynth - Load DSP",
                                   Gtk::FileChooser::Action::OPEN);

    chooser->set_modal(true);
    chooser->add_button("_Cancel", Gtk::ResponseType::CANCEL);
    chooser->add_button("_Open", Gtk::ResponseType::OK);

    if (!dir_.empty())
        chooser->set_current_folder(Gio::File::create_for_path(dir_));

    chooser->signal_response().connect(
        sigc::bind(sigc::mem_fun(*this, &DspBrowser::onOtherFileResponse),
                   chooser));

    chooser->present();
}

void DspBrowser::onOtherFileResponse (int response,
                                      Gtk::FileChooserDialog *chooser)
{
    const string picked = response == Gtk::ResponseType::OK
                          ? chosenPath(*chooser) : string();

    closeDialog(chooser);

    /* A path and not a name: a graph outside the tree has no short name, and
       resolveDsp hands an absolute path straight back. */
    choose(picked);
}
