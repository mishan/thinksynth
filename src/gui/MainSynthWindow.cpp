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
#include <sys/types.h>
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

#ifdef HAVE_JACK
# include "../gthJackAudio.h"
#endif

#include "../gthPrefs.h"
#include "../gthPatchfile.h"

bool chosen = false;

void MainSynthWindow::toggleConnects (void)
{
    /* These used to be fished out of menuJack_.items() by index. gtkmm-3 has
       no items() list, and indexing a menu by position was fragile anyway --
       the two items are held directly now. */
    if (jackConnect_ == NULL || jackDisconnect_ == NULL)
        return;

    bool c = jackConnect_->is_sensitive();

    jackConnect_->set_sensitive(!c);
    jackDisconnect_->set_sensitive(c);
}

#ifdef HAVE_JACK

static void connectDialog (int error)
{
    string msg;
    
    switch (error)
    {
        case gthJackAudio::ERR_NO_PLAYBACK:
            msg = "Could not find a playback target for JACK\n"
                    "(alsa_pcm or oss.)";
            break;
        case gthJackAudio::ERR_HANDLE_NULL:
            msg = "Can't connect to the JACK server because it no\n"
                      "longer seems to be running.";
            break;
        default:
            msg = "Could not (dis)connect JACK, errno = " + error;
            break;
    }

    Gtk::MessageDialog errorDialog (msg.c_str(), false, Gtk::MESSAGE_ERROR);
    errorDialog.run();
}

#endif /* HAVE_JACK */

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

#ifdef HAVE_JACK
    signal_realize().connect(
        sigc::mem_fun(*this, &MainSynthWindow::jackCheck));
#endif
    
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

    menuFile_.append(*manage(new Gtk::SeparatorMenuItem()));

    addMenuItem(menuFile_, "_Quit",
                sigc::mem_fun(*this, &MainSynthWindow::menuQuit),
                "<ctrl>q");

#ifdef HAVE_JACK
    /* JACK */
    if (dynamic_cast<gthJackAudio*>(audio_) != NULL)
    {
        gthPrefs *prefs = gthPrefs::instance();
        string** vals;
        bool sel;

        jackConnect_ = addMenuItem(menuJack_, "_Connect to JACK now",
                        sigc::mem_fun(*this, &MainSynthWindow::menuJackTry));

        jackDisconnect_ = addMenuItem(menuJack_, "_Disconnect from JACK",
                        sigc::mem_fun(*this, &MainSynthWindow::menuJackDis));

        jackDisconnect_->set_sensitive(false);

        menuJack_.append(*manage(new Gtk::SeparatorMenuItem()));

        Gtk::CheckMenuItem *autoItem =
            manage(new Gtk::CheckMenuItem("_Auto-connect to JACK", true));

        vals = prefs->Get("autoconnect");
        sel = !!(vals && *vals[0] == "true");

        /* Set the state before connecting, or this would fire the handler and
           write the preference straight back. */
        autoItem->set_active(sel);

        autoItem->signal_toggled().connect(
            sigc::mem_fun(*this, &MainSynthWindow::menuJackAuto));

        menuJack_.append(*autoItem);
    }
#endif /* HAVE_JACK */

    /* Help */
    addMenuItem(menuHelp_, "_About",
                sigc::mem_fun(*this, &MainSynthWindow::menuAbout));

    /* add the menus to the menubar */
    {
        Gtk::MenuItem *fileMenu = manage(new Gtk::MenuItem("_File", true));

        fileMenu->set_submenu(menuFile_);
        menuBar_.append(*fileMenu);

#ifdef HAVE_JACK
        if (dynamic_cast<gthJackAudio*>(audio_) != NULL)
        {
            Gtk::MenuItem *jackMenu = manage(new Gtk::MenuItem("_JACK", true));

            jackMenu->set_submenu(menuJack_);
            menuBar_.append(*jackMenu);
        }
#endif

        Gtk::MenuItem *helpMenu = manage(new Gtk::MenuItem("_Help", true));

        helpMenu->set_submenu(menuHelp_);
        helpMenu->set_right_justified();
        menuBar_.append(*helpMenu);
    }
}

#ifdef HAVE_JACK

void MainSynthWindow::menuJackDis (void)
{
    gthJackAudio *jaudio = (gthJackAudio*)audio_;
    
    if (jaudio)
    {
        jaudio->tryConnect(false);
        toggleConnects();
    }
}

void MainSynthWindow::menuJackTry (void)
{
    gthJackAudio *jaudio = (gthJackAudio*)audio_;

    if (jaudio)
    {
        int error;
        if ((error = jaudio->tryConnect()) == 0)
            toggleConnects();
        else
            connectDialog(error);
    }
}

void MainSynthWindow::menuJackAuto (void)
{
    gthPrefs *prefs = gthPrefs::instance();
    string *val = new string;
    string **vals = new string*[2];
    
    string **res = prefs->Get("autoconnect");

    vals[0] = val;
    vals[1] = NULL;

    if (res && *res[0] == "true")
    {
        if (chosen == false) { chosen = true; return; }
        *val = "false";
    }
    else
    {
        chosen = true;
        *val = "true";
    }
    
    prefs->Set("autoconnect", vals);
}

#endif /* HAVE_JACK */

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

void MainSynthWindow::append_tab (const string &tabName, int num, bool is_real)
{
    if (is_real == false)
    {
        Gtk::Label *lbl = manage(new Gtk::Label("Please select a DSP file to associate with this patch."));
        lbl->set_justify(Gtk::JUSTIFY_CENTER);
        notebook_.append_page(*lbl, tabName);
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
        notebook_.append_page(*sorry, tabName);
        return;
    }
        
    Gtk::ScrolledWindow *tab_view = manage(new Gtk::ScrolledWindow);
    Gtk::VBox *tab_vbox = manage(new Gtk::VBox);
    Gtk::Frame *info_frame = manage(new Gtk::Frame);
    Gtk::Table *info_table = manage(new Gtk::Table(3, 2));

    tab_view->add(*tab_vbox);
    tab_view->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);

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

    notebook_.append_page(*tab_view, tabName);

}

void MainSynthWindow::populate (void)
{
    /* populate notebook */
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
            append_tab (tabName, i, false);
            continue;
        }

        if (patch->filename.length() > 0)
        {
            /* display channel # */
            tabName = chanStr.str() + patch->filename;
        }
        else
        {
            tabName = chanStr.str() + "(Untitled)";
        }

        append_tab (tabName, i, true);
    }

}

#ifdef HAVE_JACK
void MainSynthWindow::jackCheck (void)
{
    gthPrefs *prefs = gthPrefs::instance();
    string ** vals;

    /* Not the best place to do it but we need to call toggleConnects */
    if (dynamic_cast<gthJackAudio*>(audio_) != NULL)
    {
        vals = prefs->Get("autoconnect");
        if (vals && *vals[0] == "true")
        {
            int error;
            if ((error = ((gthJackAudio*)audio_)->tryConnect()) == 0)
                toggleConnects();
            else
                connectDialog (error);
        }
    }
}
#endif

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
