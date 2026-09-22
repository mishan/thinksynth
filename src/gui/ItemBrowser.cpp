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
#include "ItemBrowser.h"

ItemBrowser::ItemBrowser (Gtk::Window &parent, const Glib::ustring &title,
                          const Provider &provider, const std::string &otherDir,
                          const std::string &current)
    : Gtk::Dialog(title, parent, true),
      provider_(provider), otherDir_(otherDir), selected_(current),
      openBtn_(NULL)
{
    set_default_size(460, 520);

    filter_.set_placeholder_text("Filter");
    filter_.signal_changed().connect(
        sigc::mem_fun(*this, &ItemBrowser::onFilterChanged));

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
        sigc::mem_fun(*this, &ItemBrowser::onRowActivated));

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

    /* The escape hatch. A file of one's own does not live in the shipped tree
       and has no name the catalog knows, so the old chooser stays reachable
       -- it is no longer the only way to choose, which is the change. */
    if (!otherDir_.empty())
        add_button("_Other File...", Gtk::ResponseType::ACCEPT);

    add_button("_Cancel", Gtk::ResponseType::CANCEL);
    openBtn_ = add_button("_Open", Gtk::ResponseType::OK);

    openBtn_->set_sensitive(false);

    signal_response().connect(sigc::mem_fun(*this, &ItemBrowser::onResponse));

    rebuild();
}

void ItemBrowser::setEmptyNote (const Glib::ustring &markup)
{
    if (store_->get_n_items() == 0 && filter_.get_text().empty())
        detail_.set_markup(markup);
}

void ItemBrowser::rebuild (void)
{
    const std::vector<BrowserGroup> groups = provider_(filter_.get_text());

    store_->remove_all();

    /* The group holding what is in use, so it can be opened below. */
    Glib::ustring openGroup;

    for (size_t g = 0; g < groups.size(); g++)
    {
        if (groups[g].items.empty())
            continue;

        Glib::RefPtr<BrowserRow> groupRow =
            BrowserRow::create(groups[g].name, "", "", "");

        for (size_t i = 0; i < groups[g].items.size(); i++)
        {
            const BrowserItem &item = groups[g].items[i];

            groupRow->addChild(BrowserRow::create(item.name, item.desc,
                                                  item.file, item.note));

            if (!selected_.empty() && item.file == selected_)
                openGroup = groups[g].name;
        }

        /* Appended once it has its children, the way NodePalette fills a
           category row before the model sees it: a group handed over empty is
           a group Gtk::TreeListModel is told has nothing to expand. */
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
        sigc::mem_fun(*this, &ItemBrowser::onSelectionChanged));

    list_.set_model(selection_);

    /* Nothing is selected yet, so say so. Emptying `store_' above does
       reach the outgoing selection and clear the two of them on the way
       past, but that is the teardown of a model being replaced doing it,
       and a filter that leaves `selected_' out returns from here without
       ever selecting anything. Resetting them outright is one line and
       does not rest on the order two models are dismantled in. */
    onSelectionChanged();

    /* The remembered row, selected: a dialog that opens on the current file
     * is a dialog that answers "what is this?" as well as "what else is
     * there?", and a filter that still contains that row should leave the
     * cursor on it. Expanding its group is what makes the row exist -- a
     * collapsed group's children are not rows in the flattened model at
     * all.
     *
     * No scroll to it: Gtk::ListView::scroll_to arrived in 4.12 and this
     * builds against 4.6. The row is selected, so Open acts on it either
     * way. */
    if (openGroup.empty())
        return;

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

        if (row && row->file() == selected_)
        {
            selection_->set_selected(i);
            break;
        }
    }
}

void ItemBrowser::onFilterChanged (void)
{
    rebuild();
}

std::string ItemBrowser::selectedFile (void)
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

void ItemBrowser::onSelectionChanged (void)
{
    Glib::RefPtr<Gtk::TreeListRow> treeRow =
        selection_
        ? std::dynamic_pointer_cast<Gtk::TreeListRow>(
              selection_->get_selected_item())
        : Glib::RefPtr<Gtk::TreeListRow>();

    Glib::RefPtr<BrowserRow> row =
        treeRow ? std::dynamic_pointer_cast<BrowserRow>(treeRow->get_item())
                : Glib::RefPtr<BrowserRow>();

    if (openBtn_)
        openBtn_->set_sensitive(row && !row->isGroup());

    detail_.set_markup(row ? row->note() : std::string());

    /* Only a leaf is remembered, and an empty selection does not forget: a
       rebuild passes through "nothing selected" on its way to selecting
       again, and clearing here would make it lose the row every time. */
    if (row && !row->isGroup())
        selected_ = row->file();
}

void ItemBrowser::onRowActivated (guint position)
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

void ItemBrowser::choose (const std::string &file)
{
    if (file.empty())
        return;

    m_signal_chosen_(file);

    closeDialog(this);
}

void ItemBrowser::onResponse (int response)
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

void ItemBrowser::onOtherFile (void)
{
    Gtk::FileChooserDialog *chooser =
        new Gtk::FileChooserDialog(*this, get_title(),
                                   Gtk::FileChooser::Action::OPEN);

    chooser->set_modal(true);
    chooser->add_button("_Cancel", Gtk::ResponseType::CANCEL);
    chooser->add_button("_Open", Gtk::ResponseType::OK);

    if (!otherDir_.empty())
        chooser->set_current_folder(Gio::File::create_for_path(otherDir_));

    chooser->signal_response().connect(
        sigc::bind(sigc::mem_fun(*this, &ItemBrowser::onOtherFileResponse),
                   chooser));

    chooser->present();
}

void ItemBrowser::onOtherFileResponse (int response,
                                       Gtk::FileChooserDialog *chooser)
{
    const std::string picked = response == Gtk::ResponseType::OK
                               ? chosenPath(*chooser) : std::string();

    closeDialog(chooser);

    /* A path and not a name: a file outside the tree has no short name, and
       both callers take a path here -- resolveDsp hands an absolute one
       straight back, and a piece is opened by path anyway. */
    choose(picked);
}
