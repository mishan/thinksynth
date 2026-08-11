/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

#include <iostream>
#include <sstream>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <filesystem>
#include <system_error>
#include <signal.h>

#include <gtkmm.h>
#include <gtkmm/messagedialog.h>

#include "think.h"

#include "PatchSelWindow.h"
#include "Keyboard.h"
#include "KeyboardWindow.h"
#include "MainSynthWindow.h"
#include "AboutBox.h"
#include "MidiMap.h"
#include "ArgTable.h"
#include "NodeEditor.h"


#include "../gthPrefs.h"
#include "../gthPatchfile.h"

bool chosen = false;

MainSynthWindow::MainSynthWindow (gthAudio *audio)
{
    gthPrefs *prefs = gthPrefs::instance();
    string **vals;

    audio_ = audio;

    set_title("thinksynth");
    set_default_size(520, 360);

    midiMap_ = NULL;
    patchSel_ = NULL;
    aboutBox_ = NULL;
    kbWin_ = NULL;

    vals = prefs->Get("dspdir");

    if (vals != NULL)
        prevDir_ = *(vals[0]);
    else
        prevDir_ = DSP_PATH;

    populateMenu();

    
    add(vbox_);

    dspEntryLbl_.set_label("DSP File: ");
    dspBrowseBtn_.set_label("Browse");
    dspEntryBox_.pack_start(dspEntryLbl_, Gtk::PACK_SHRINK);
    dspEntryBox_.pack_start(dspEntry_, Gtk::PACK_EXPAND_WIDGET);
    dspEntryBox_.pack_start(dspBrowseBtn_, Gtk::PACK_SHRINK);

    dspEntry_.signal_activate().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onDspEntryActivate));

    dspBrowseBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onBrowseButton));

    vbox_.pack_start(menuBar_, Gtk::PACK_SHRINK);
    vbox_.pack_start(dspEntryBox_, Gtk::PACK_SHRINK);
    vbox_.pack_start(notebook_, Gtk::PACK_EXPAND_WIDGET);

    /* Tabs down the left rather than across the top.
     *
     * There is one per MIDI channel, so sixteen of them, and across the top
     * they did not fit -- the notebook scrolled and most of the patches you
     * had loaded were off the end, reachable only by paging. Stacked
     * vertically they all fit in the height a patch panel needs anyway, and
     * the list reads as what it is: the channels, in order.
     *
     * Still scrollable, because nothing guarantees the window is tall. */
    notebook_.set_tab_pos(Gtk::POS_LEFT);
    notebook_.set_scrollable();

    notebook_.signal_switch_page().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onSwitchPage));

    populate();

    show_all_children();

    gthPatchManager *patchMgr = gthPatchManager::instance();
    patchMgr->signal_patches_changed().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onPatchesChanged));
    patchMgr->signal_patch_load_error().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onPatchLoadError));

    debug("signal connections made");

}

MainSynthWindow::~MainSynthWindow (void)
{
    menuQuit();
}

/* Builds one activatable menu item: label with mnemonic, optional accelerator,
   and its callback. Gtk::Menu_Helpers::MenuElem did all of this in a single
   expression; gtkmm-3 removed the whole helpers namespace along with
   Menu::items(), so items are constructed and appended individually. */
Gtk::MenuItem *MainSynthWindow::addMenuItem (Gtk::Menu &menu,
                                             const Glib::ustring &label,
                                             const sigc::slot<void> &handler,
                                             const char *accel)
{
    Gtk::MenuItem *item = manage(new Gtk::MenuItem(label, true));

    item->signal_activate().connect(handler);

    if (accel != NULL)
    {
        Gtk::AccelKey key(accel);

        item->add_accelerator("activate", get_accel_group(),
                              key.get_key(), key.get_mod(),
                              Gtk::ACCEL_VISIBLE);
    }

    menu.append(*item);

    return item;
}

void MainSynthWindow::populateMenu (void)
{
    /* The accelerators used to come along with each MenuElem; they need an
       explicit group now. Gtk::Window::get_accel_group() hands back the
       window's own, already attached -- calling add_accel_group() on it
       attaches it a second time and trips an assertion in GTK. */

    /* File */
    addMenuItem(menuFile_, "_Keyboard",
                sigc::mem_fun(*this, &MainSynthWindow::menuKeyboard),
                "<ctrl>k");
    addMenuItem(menuFile_, "_Patch Selector",
                sigc::mem_fun(*this, &MainSynthWindow::menuPatchSel),
                "<ctrl>p");
    addMenuItem(menuFile_, "_MIDI Controllers",
                sigc::mem_fun(*this, &MainSynthWindow::menuMidiMap),
                "<ctrl>m");
    addMenuItem(menuFile_, "_Node View",
                sigc::mem_fun(*this, &MainSynthWindow::menuNodes),
                "<ctrl>n");

    menuFile_.append(*manage(new Gtk::SeparatorMenuItem()));

    addMenuItem(menuFile_, "_Quit",
                sigc::mem_fun(*this, &MainSynthWindow::menuQuit),
                "<ctrl>q");


    /* Help */
    addMenuItem(menuHelp_, "_About",
                sigc::mem_fun(*this, &MainSynthWindow::menuAbout));

    /* add the menus to the menubar */
    {
        Gtk::MenuItem *fileMenu = manage(new Gtk::MenuItem("_File", true));

        fileMenu->set_submenu(menuFile_);
        menuBar_.append(*fileMenu);


        Gtk::MenuItem *helpMenu = manage(new Gtk::MenuItem("_Help", true));

        helpMenu->set_submenu(menuHelp_);
        helpMenu->set_right_justified();
        menuBar_.append(*helpMenu);
    }
}


void MainSynthWindow::menuKeyboard (void)
{
    if (kbWin_ == NULL)
    {
        kbWin_ = new KeyboardWindow (thSynth::instance());
        menuBar_.accelerate(*kbWin_);
        kbWin_->signal_hide().connect(
            sigc::mem_fun(*this, &MainSynthWindow::onKeyboardHide));
    }

    kbWin_->show_all_children();
    kbWin_->show();
}

/* Opens the node view on whatever .dsp the current tab names. The window
   parses its own copy of the file, so it neither sees nor disturbs the tree
   the channel is playing -- which also means it works before a patch has been
   loaded, as long as the entry names a readable file. */
/* Show the node graph for the patch page you are on.
 *
 * It is a tab on that page now, so this is a switch rather than a window to
 * open: the editor is built and the .dsp read the first time the tab is
 * shown, by onSubTab. All the resolving that used to happen here went with
 * it -- the page knows its own dspFile, so there is nothing to guess from the
 * entry box and no way for the menu to show a different file from the tab. */
void MainSynthWindow::menuNodes (void)
{
    const int chan = notebook_.get_current_page();

    if (chan < 0 || chan >= (int)subTabs_.size() || subTabs_[chan] == NULL)
    {
        Gtk::MessageDialog dlg(*this, "No patch on this tab.", false,
                               Gtk::MESSAGE_INFO, Gtk::BUTTONS_OK, true);
        dlg.set_secondary_text("Choose a DSP for this channel first; the node "
                               "view shows the file that channel is playing.");
        dlg.run();
        return;
    }

    subTabs_[chan]->set_current_page(1);
}

void MainSynthWindow::menuPatchSel (void)
{
    if (patchSel_ == NULL)
    {
        patchSel_ = new PatchSelWindow(thSynth::instance());
        menuBar_.accelerate(*patchSel_);
        patchSel_->signal_hide().connect(
            sigc::mem_fun(*this, &MainSynthWindow::onPatchSelHide));
    }
    
    patchSel_->show_all_children();
    patchSel_->show();
}

void MainSynthWindow::menuMidiMap (void)
{
    if (midiMap_ == NULL)
    {
        midiMap_ = new MidiMap(thSynth::instance());
        menuBar_.accelerate(*midiMap_);
        midiMap_->signal_hide().connect(
            sigc::mem_fun(*this, &MainSynthWindow::onMidiMapHide));
    }

    midiMap_->show_all_children();
    midiMap_->show();
}

void MainSynthWindow::menuQuit (void)
{
    hide();
}

void MainSynthWindow::menuAbout (void)
{
    if (aboutBox_)
        return;

    aboutBox_ = new AboutBox;
    aboutBox_->show();
    aboutBox_->signal_hide().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onAboutBoxHide));
}

/* The tab strip runs down the side, so its width is the window's to spare.
 *
 * Left-aligned because a column of centred labels of different lengths has no
 * edge to read down. Ellipsised at the end so one long name cannot widen the
 * strip and take that space from the patch panel; the full path is on the
 * tooltip either way. */
Gtk::Widget *MainSynthWindow::makeTabLabel (const string &text,
                                            const string &tip)
{
    Gtk::Label *lbl = manage(new Gtk::Label(text));

    lbl->set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    lbl->set_ellipsize(Pango::ELLIPSIZE_END);

    /* Both widths, and the minimum is the one that matters: an ellipsising
       label reports "..." as its minimum size, so with only a maximum set the
       notebook shrank every tab to three dots. width_chars reserves the room;
       max_width_chars stops one long name widening the strip. */
    lbl->set_width_chars(16);
    lbl->set_max_width_chars(22);

    if (!tip.empty())
        lbl->set_tooltip_text(tip);

    lbl->show();

    return lbl;
}

void MainSynthWindow::append_tab (const string &tabName, const string &tip,
                                  int num, bool is_real)
{
    if (is_real == false)
    {
        Gtk::Label *lbl = manage(new Gtk::Label("Please select a DSP file to associate with this patch."));
        lbl->set_justify(Gtk::JUSTIFY_CENTER);
        notebook_.append_page(*lbl, *makeTabLabel(tabName, tip));
        subTabs_.push_back(NULL);
        return;
    }

    gthPatchManager *patchMgr = gthPatchManager::instance();
    thArgMap args = patchMgr->getChannelArgs(num);

    /* XXX: this no longer applies */
    /* only 'amp' */
    if (args.size() == 1)
    {
        Gtk::Label *sorry = manage(new Gtk::Label("Sorry, this DSP does not have modifiable settings."));
        sorry->set_justify(Gtk::JUSTIFY_CENTER);
        notebook_.append_page(*sorry, *makeTabLabel(tabName, tip));
        subTabs_.push_back(NULL);
        return;
    }
        
    Gtk::ScrolledWindow *tab_view = manage(new Gtk::ScrolledWindow);
    Gtk::VBox *tab_vbox = manage(new Gtk::VBox);
    Gtk::Frame *info_frame = manage(new Gtk::Frame);
    Gtk::Table *info_table = manage(new Gtk::Table(3, 2));

    tab_view->add(*tab_vbox);
    /* Horizontal scrolling allowed now that the parameters sit in columns.
       It was NEVER, which meant a window narrower than the layout crushed the
       sliders instead of letting you scroll to them. */
    tab_view->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);

    info_frame->set_label("DSP Information");
    info_frame->add(*info_table);

    info_table->set_col_spacings(5);
    info_table->set_row_spacings(5);

    thArg *dspName = args["name"];

    if (dspName)
    {
        Gtk::Label *lname_lbl = manage(new Gtk::Label("Name: "));
        Gtk::Label *rname_lbl = manage(new Gtk::Label(dspName->comment()));

        lname_lbl->set_alignment(Gtk::ALIGN_END);
        rname_lbl->set_alignment(Gtk::ALIGN_START);

        info_table->attach(*lname_lbl, 0, 1, 0, 1, Gtk::FILL, Gtk::FILL);
        info_table->attach(*rname_lbl, 1, 2, 0, 1, Gtk::FILL, Gtk::FILL);
    }

    thArg *dspAuthor = args["author"];

    if (dspAuthor)
    {
        Gtk::Label *lname_lbl = manage(new Gtk::Label("Author: "));
        Gtk::Label *rname_lbl = manage(new Gtk::Label(dspAuthor->comment()));

        lname_lbl->set_alignment(Gtk::ALIGN_END);
        rname_lbl->set_alignment(Gtk::ALIGN_START);

        
        info_table->attach(*lname_lbl, 0, 1, 1, 2, Gtk::FILL, Gtk::FILL);
        info_table->attach(*rname_lbl, 1, 2, 1, 2, Gtk::FILL, Gtk::FILL);
    }

    thArg *dspDesc = args["desc"];

    if (dspDesc)
    {
        Gtk::Label *lname_lbl = manage(new Gtk::Label("Description: "));
        Gtk::Label *rname_lbl = manage(new Gtk::Label(dspDesc->comment()));

        lname_lbl->set_alignment(Gtk::ALIGN_END);
        rname_lbl->set_alignment(Gtk::ALIGN_START);
        
        info_table->attach(*lname_lbl, 0, 1, 2, 3, Gtk::FILL, Gtk::FILL);
        info_table->attach(*rname_lbl, 1, 2, 2, 3, Gtk::FILL, Gtk::FILL);
    }

    Gtk::Frame *dsp_frame = manage(new Gtk::Frame);
    ArgTable *dsp_table = manage(new ArgTable);

    dsp_frame->set_label("DSP Parameters");
    dsp_frame->add(*dsp_table);
        
    tab_vbox->pack_start(*info_frame, Gtk::PACK_SHRINK);
    tab_vbox->pack_start(*dsp_frame);

    /* populate each tab */
    for (thArgMap::iterator j = args.begin();
         j != args.end(); j++)
    {
        string argName = j->first;
        thArg *arg = j->second;

        if (arg == NULL)
            continue;

        switch (arg->widgetType())
        {
            case thArg::HIDE:
                break;
            case thArg::SLIDER:
            {
                dsp_table->insertArg(arg);
                break;                
            }
            default:
                break;
        }
    }

    /* Now that the count is known, the table can pick its column count. */
    dsp_table->reflow();

    /* Two views of the same patch, side by side in the same window: the
       sliders, and the graph they came from. The node editor used to be a
       separate window opened from the menu, so the two could never be seen
       together and each had its own idea of what was unsaved.
    
       The editor itself is not built yet. Building one scans the plugin
       directory to fill its palette, and doing that sixteen times on startup
       to reach the one page anyone opens would be sixteen scans wasted. The
       Nodes page holds an empty box until its tab is first looked at. */
    Gtk::Notebook *sub = manage(new Gtk::Notebook);
    Gtk::Box *holder = manage(new Gtk::VBox);

    sub->append_page(*tab_view, "Overview");
    sub->append_page(*holder, "Nodes");

    sub->signal_switch_page().connect(
        sigc::bind<Gtk::Widget *, string, int>(
            sigc::mem_fun(*this, &MainSynthWindow::onSubTab),
            holder, patchMgr->getPatch(num) ? patchMgr->getPatch(num)->dspFile
                                            : string(),
            num));

    notebook_.append_page(*sub, *makeTabLabel(tabName, tip));
    subTabs_.push_back(sub);

}

/* A .dsp name as a patch stores it, turned into a path that can be opened.
 *
 * A patch keeps the name it was given, normally bare -- `ts1.dsp'. Resolving
 * that against the install path is the patch manager's rule, and resolveDsp
 * knows only about the install path, so a file the user browsed to from
 * somewhere else needs the directory they browsed from as well.
 *
 * This lived inside the menu handler that used to open the node window. When
 * the editor moved onto a tab the handler became a one-line switch and this
 * went with it -- so the tab opened the bare name, and every patch pointing at
 * an installed .dsp failed with "Could not read ts1.dsp". It is a function
 * now, because two callers need it and a third will. */
string MainSynthWindow::resolveDspPath (const string &named)
{
    if (named.empty())
        return named;

    string path = gthPatchManager::resolveDsp(named);

    if (!std::filesystem::path(named).is_absolute() && !prevDir_.empty())
    {
        std::error_code ec;

        const std::filesystem::path browsed =
            std::filesystem::path(prevDir_) / named;

        if (!std::filesystem::exists(path, ec) &&
            std::filesystem::exists(browsed, ec))
            path = browsed.string();
    }

    return path;
}

/* The Nodes tab has been shown for the first time: build its editor.
 *
 * `page' and `num' are which sub-tab was switched to; `holder' is the empty
 * box the Nodes page was given. Anything already built is left alone -- this
 * fires on every switch, not only the first. */
void MainSynthWindow::onSubTab (Gtk::Widget *page, guint num,
                                Gtk::Widget *holder, string dspFile, int chan)
{
    (void)page;

    if (num != 1 || editors_.count(holder))
        return;

    Gtk::Box *box = dynamic_cast<Gtk::Box *>(holder);

    if (box == NULL)
        return;

    NodeEditor *ed = manage(new NodeEditor(thSynth::instance()));

    editors_[holder] = ed;

    box->pack_start(*ed);
    box->show_all_children();

    if (dspFile.empty())
    {
        ed->setStatusPublic("This patch has no DSP file yet. Choose one above,"
                            " or use New to start one.");
        return;
    }

    const string path = resolveDspPath(dspFile);

    /* The channel is passed so slider moves in the graph reach the running
       synth, the same as they did when this was a window. */
    if (!ed->open(path, chan))
        ed->setStatusPublic("Could not read " + dspFile +
                            (path == dspFile ? "" : "  (" + path + ")"));
}

void MainSynthWindow::populate (void)
{
    /* populate notebook */
    subTabs_.clear();
    editors_.clear();
    gthPatchManager *patchMgr = gthPatchManager::instance();
    int numPatches = patchMgr->numPatches();

    for (int i = 0; i < numPatches; i++)
    {
        gthPatchManager::PatchFile *patch = patchMgr->getPatch(i);
        std::ostringstream chanStr;
        string tabName;
        
        chanStr << i + 1 << ": ";
        
        if (patch == NULL)
        {
            tabName = chanStr.str() + "(Untitled)";
            append_tab (tabName, "", i, false);
            continue;
        }

        if (patch->filename.length() > 0)
        {
            /* The basename, not the path.
            
               A tab read `3: /usr/local/share/thinksynth/dsp/old/analog03.dsp'
               -- almost all of it identical to every other tab, and the part
               that identifies it last, which is the part a narrow tab cuts
               off. The full path is still worth having, so it moves to the
               tooltip. */
            tabName = chanStr.str() +
                      thUtil::basename((char *)patch->filename.c_str());
        }
        else
        {
            tabName = chanStr.str() + "(Untitled)";
        }

        append_tab (tabName, patch->filename, i, true);
    }

}


void MainSynthWindow::onPatchesChanged (void)
{
    int pagenum = notebook_.get_current_page();

    /* gtkmm-3 dropped Notebook::pages() and Widget::hide_all(); pages are
       removed one at a time now, and hide() covers the children. */
    notebook_.hide();

    while (notebook_.get_n_pages() > 0)
        notebook_.remove_page(-1);

    populate();
    notebook_.show_all();

    if (pagenum != -1)
        notebook_.set_current_page(pagenum);
}

void MainSynthWindow::onPatchLoadError (const char* failure)
{
    char *error = g_strdup_printf("Couldn't load patchfile %s; syntax error, or DSP does not exist",
        failure);
        
    Gtk::MessageDialog errorDialog (error, false, Gtk::MESSAGE_ERROR);
    
    errorDialog.run();
    free(error);
}

void MainSynthWindow::onAboutBoxHide (void)
{
    delete aboutBox_;
    aboutBox_ = NULL;
}

void MainSynthWindow::onPatchSelHide (void)
{
    delete patchSel_;
    patchSel_ = NULL;
}

void MainSynthWindow::onMidiMapHide (void)
{
    delete midiMap_;
    midiMap_ = NULL;
}


void MainSynthWindow::onKeyboardHide (void)
{
    delete kbWin_;
    kbWin_ = NULL;
}

/* gtkmm-3 passes the page widget itself rather than the opaque GtkNotebookPage
   struct, which no longer exists. */
void MainSynthWindow::onSwitchPage (Gtk::Widget *page, guint pagenum)
{
    gthPatchManager *patchMgr = gthPatchManager::instance();
    gthPatchManager::PatchFile *patch = patchMgr->getPatch(pagenum);

    if (patch == NULL)
    {
        dspEntry_.set_text("");
        return;
    }

    dspEntry_.set_text(patch->dspFile);
}

void MainSynthWindow::onDspEntryActivate (void)
{
    gthPatchManager *patchMgr = gthPatchManager::instance();
    string dspfile = dspEntry_.get_text();
    int pagenum = notebook_.get_current_page();

    /* noop caused by a spurious Enter */
    if (dspfile == "")
        return;
    
    if (patchMgr->newPatch(dspfile, pagenum) == false)
    {
        char *error = g_strdup_printf("Couldn't load DSP %s; syntax error, or does not exist",
            dspfile.c_str());
        
        Gtk::MessageDialog errorDialog (error, false, Gtk::MESSAGE_ERROR);
        
        errorDialog.run();

        free(error);

        return;
    }

    /* gtkmm-3 dropped Notebook::pages() and Widget::hide_all(); pages are
       removed one at a time now, and hide() covers the children. */
    notebook_.hide();

    while (notebook_.get_n_pages() > 0)
        notebook_.remove_page(-1);

    populate();
    notebook_.show_all();

    notebook_.set_current_page(pagenum);
}

void MainSynthWindow::onBrowseButton (void)
{
    gthPatchManager *patchMgr = gthPatchManager::instance();
    int pagenum = notebook_.get_current_page();
    Gtk::FileChooserDialog fileSel(*this, "thinksynth - Load DSP",
                                   Gtk::FILE_CHOOSER_ACTION_OPEN);

    fileSel.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
    fileSel.add_button(Gtk::Stock::OPEN, Gtk::RESPONSE_OK);

    if (prevDir_ != "")
        fileSel.set_current_folder(prevDir_);

    if (fileSel.run() == Gtk::RESPONSE_OK)
    {
        dspEntry_.set_text(fileSel.get_filename());

        if (patchMgr->newPatch(fileSel.get_filename(), pagenum))
        {
            string dn = thUtil::dirname((char*)fileSel.get_filename().c_str());

            prevDir_ = dn + "/";

            string **vals = new string *[2];
            vals[0] = new string(prevDir_);
            vals[1] = NULL;

            gthPrefs *prefs = gthPrefs::instance();
            prefs->Set("dspdir", vals);

            /* load up the patch file */
            notebook_.hide();

            while (notebook_.get_n_pages() > 0)
                notebook_.remove_page(-1);

            populate();
            notebook_.show_all();
            notebook_.set_current_page(pagenum);
        }
        else
        {
            char *error = g_strdup_printf("Couldn't load DSP %s; syntax error, or does not exist",
                fileSel.get_filename().c_str());
        
            Gtk::MessageDialog errorDialog (error, false, Gtk::MESSAGE_ERROR);
        
            errorDialog.run();

            free(error);
        }
    }
}
