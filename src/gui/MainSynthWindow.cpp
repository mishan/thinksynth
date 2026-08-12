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
#include "Dialogs.h"
#include "SaveButton.h"


#include "../gthPrefs.h"
#include "../gthPatchfile.h"


bool chosen = false;

MainSynthWindow::MainSynthWindow (gthAudio *audio)
{
    audio_ = audio;

    set_title("thinksynth");

    tearingDown_ = false;
    width_ = 0;
    height_ = 0;

    /* 520x360 until now, which predates the channel strip down the left and
       the parameter columns -- the window opened too small to show either.
       This is what a first run gets; applyPrefs replaces it with the size the
       last one was left at. */
    set_default_size(1000, 700);

    /* The size is taken while the window is still on screen, at the two
       moments it is about to stop being: the close button, and Quit.
    
       configure-event went with the rest of the GdkEvent structs. The obvious
       replacement was the window's own default-width and default-height
       properties -- they are what GTK4's own documentation binds to GSettings
       for this -- but they do not track a resize done by the window manager,
       which is every resize there is. Tested: dragged to 1111x633, saved
       1000x700, the default it was given at startup. */
    signal_close_request().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onCloseRequest), false);

    midiMap_ = NULL;
    patchSel_ = NULL;
    aboutBox_ = NULL;
    kbWin_ = NULL;

    prevDir_ = DSP_PATH;

    /* "win.keyboard" and the rest resolve against this. */
    actions_ = Gio::SimpleActionGroup::create();
    insert_action_group("win", actions_);

    populateMenu();

    property_application().signal_changed().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onApplicationSet));

    
    set_child(vbox_);

    dspEntryLbl_.set_label("DSP File: ");
    dspBrowseBtn_.set_label("Browse");
    dspEntryBox_.append(dspEntryLbl_);
    dspEntry_.set_hexpand(true);
    dspEntryBox_.append(dspEntry_);
    dspEntryBox_.append(dspBrowseBtn_);

    /* Master level, on the row that already belongs to the whole window
       rather than to one channel -- the same reason the DSP File entry is
       here. Shown 0..127 like a channel's own amplitude, so the two read on
       one scale; 100 is unity, which is where it starts, and there is room
       above it because the engine allows gain over 1.
    
       The limiter downstream is what makes going above unity safe to offer;
       see TH_LIMIT_KNEE. */
    masterLbl_.set_text("Master:");

    /* 0..127, not 0..TH_MASTER_GAIN_MAX*100. That constant is 4.0, so
       scaling it put 400 on a volume control -- four times unity, a number
       nobody wants to see there and not the scale this claims to be on. The
       engine still permits gain up to 4 for anything that needs it; what the
       master offers is the channel scale, 100 for unity and a little above
       it to lift a quiet patch. */
    masterScale_.set_range(0, MIDIVALMAX);
    masterScale_.set_increments(1, 10);
    masterScale_.set_digits(0);
    /* GTK3 turned the number on when it was told where to put it; GTK4 keeps
       the two apart, and a scale draws no value unless asked. */
    masterScale_.set_draw_value(true);
    masterScale_.set_value_pos(Gtk::PositionType::RIGHT);
    masterScale_.set_size_request(160, -1);
    masterScale_.set_value(thSynth::instance()->masterGain() * 100.0);

    masterScale_.signal_value_changed().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onMasterGain));

    dspEntryBox_.append(*manage(new Gtk::Separator(
                                    Gtk::Orientation::VERTICAL)));
    dspEntryBox_.append(masterLbl_);
    dspEntryBox_.append(masterScale_);

    dspEntry_.signal_activate().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onDspEntryActivate));

    dspBrowseBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onBrowseButton));

    vbox_.append(*menuBar_);
    vbox_.append(dspEntryBox_);
    notebook_.set_vexpand(true);
    vbox_.append(notebook_);

    /* Tabs down the left rather than across the top.
     *
     * There is one per MIDI channel, so sixteen of them, and across the top
     * they did not fit -- the notebook scrolled and most of the patches you
     * had loaded were off the end, reachable only by paging. Stacked
     * vertically they all fit in the height a patch panel needs anyway, and
     * the list reads as what it is: the channels, in order.
     *
     * Still scrollable, because nothing guarantees the window is tall. */
    notebook_.set_tab_pos(Gtk::PositionType::LEFT);
    notebook_.set_scrollable();

    notebook_.signal_switch_page().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onSwitchPage));

    populate();


    gthPatchManager *patchMgr = gthPatchManager::instance();
    patchMgr->signal_patches_changed().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onPatchesChanged));
    patchMgr->signal_patch_load_error().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onPatchLoadError));

    debug("signal connections made");

}

MainSynthWindow::~MainSynthWindow (void)
{
    /* The notebook is a member, so it is torn down after this body has run,
       and taking its pages away asks for a node editor to be built on each of
       them. Nothing about this window is worth building now. */
    tearingDown_ = true;

    rememberGeometry();

    /* These are kept rather than destroyed when they close, so this is where
       they go -- before the synth, which they all reach into. */
    delete aboutBox_;
    delete patchSel_;
    delete midiMap_;
    delete kbWin_;

    aboutBox_ = NULL;
    patchSel_ = NULL;
    midiMap_ = NULL;
    kbWin_ = NULL;

    /* Not shutdown(): the loop has already ended by the time this runs, and
       asking a torn-down application to quit again is not a thing to do in a
       destructor. Just off the screen. */
    set_visible(false);
}

/* The size the window has now, while it still has one.
 *
 * get_width() and get_height() answer for a realised window and answer zero
 * for a hidden one, which is why this cannot wait until the destructor: by
 * then it has been hidden and there is nothing left to ask. */
void MainSynthWindow::captureSize (void)
{
    const int w = get_width(), h = get_height();

    if (w > 0 && h > 0)
    {
        width_ = w;
        height_ = h;
    }
}

/* The one way out of the program, reached from the close button and from
 * Quit.
 *
 * Neither of them lets GTK do the obvious thing. Letting the close proceed
 * destroys the window, and main deletes it a moment later, after the audio
 * device has been stopped -- so the ordering that branch was careful about
 * would depend on a destroyed widget still being safe to remove from the
 * application and free. Hiding it instead keeps both the widget and the C++
 * object intact until main is ready.
 *
 * And hiding alone is not enough either. The application holds the window
 * because add_window() gave it to it, and hiding does not give it back --
 * only destroying does. So a hidden window leaves the loop running with
 * nothing on screen, which is what Quit would have done. The application is
 * asked to stop explicitly.
 */
void MainSynthWindow::shutdown (void)
{
    captureSize();

    set_visible(false);

    Glib::RefPtr<Gtk::Application> app = get_application();

    if (app)
        app->quit();
}

/* True: handled, so the window is hidden rather than destroyed. */
bool MainSynthWindow::onCloseRequest (void)
{
    shutdown();

    return true;
}


void MainSynthWindow::applyPrefs (void)
{
    gthPrefs *prefs = gthPrefs::instance();

    /* The directory the DSP browser opens in. This was read in the
       constructor, where the preferences have not been loaded yet, so it
       always fell through to the install path however many times you had
       browsed somewhere else. */
    string **vals = prefs->Get("dspdir");

    if (vals != NULL && vals[0] != NULL)
        prevDir_ = *(vals[0]);

    vals = prefs->Get("window");

    if (vals == NULL || vals[0] == NULL || vals[1] == NULL)
        return;

    const int w = atoi(vals[0]->c_str());
    const int h = atoi(vals[1]->c_str());

    /* A saved size is only worth honouring if it can be seen. A window
       restored to 12x4 -- or to something larger than the screen it is now
       being opened on, which is what moving between a desktop and a laptop
       does -- is worse than one that ignores the file. */
    /* Gdk::Screen is gone; a monitor's geometry is the thing to ask now, and
       the monitor this window is on is not known before it has been shown --
       so the first one the display lists is what this settles for. It is a
       sanity check, not a placement decision. */
    int maxw = 0, maxh = 0;

    {
        Glib::RefPtr<Gdk::Display> display = Gdk::Display::get_default();

        if (display)
        {
            Glib::RefPtr<Gio::ListModel> monitors = display->get_monitors();

            if (monitors && monitors->get_n_items() > 0)
            {
                Glib::RefPtr<Gdk::Monitor> mon =
                    std::dynamic_pointer_cast<Gdk::Monitor>(
                        monitors->get_object(0));

                if (mon)
                {
                    Gdk::Rectangle area;

                    mon->get_geometry(area);
                    maxw = area.get_width();
                    maxh = area.get_height();
                }
            }
        }
    }

    if (w < 320 || h < 240)
        return;

    if ((maxw > 0 && w > maxw) || (maxh > 0 && h > maxh))
        return;

    set_default_size(w, h);
}

void MainSynthWindow::rememberGeometry (void)
{
    if (width_ <= 0 || height_ <= 0)
        return;

    /* std::to_string, not a stringstream: a stream formats through the global
       C++ locale, which is free to group thousands. main() pins LC_NUMERIC
       for the C library and that does not reach iostreams, so a saved 1000
       came back as "1,000" -- three preference fields where there should be
       two, since the file separates values with commas. */
    string **vals = new string *[3];

    vals[0] = new string(std::to_string(width_));
    vals[1] = new string(std::to_string(height_));
    vals[2] = NULL;

    gthPrefs::instance()->Set("window", vals);
}

/* Builds one activatable menu item: label with mnemonic, optional accelerator,
   and its callback. Gtk::Menu_Helpers::MenuElem did all of this in a single
   expression; gtkmm-3 removed the whole helpers namespace along with
   Menu::items(), so items are constructed and appended individually. */
void MainSynthWindow::addAction (const Glib::ustring &name,
                                 const sigc::slot<void ()> &handler,
                                 const char *accel)
{
    /* Gtk::Window is a Gio::ActionMap through Gtk::ApplicationWindow only;
       a plain window carries its actions in a group of its own, inserted
       under the "win" prefix that the menu items name. */
    actions_->add_action(name, handler);

    /* Recorded rather than bound. An accelerator belongs to the application
       -- it is what makes "win.keyboard" answer to Ctrl+K from any window the
       application owns -- and the window is built before it has been given
       one, so this waits for onApplicationSet. */
    if (accel != NULL)
        accels_.push_back(std::make_pair("win." + name, Glib::ustring(accel)));
}

void MainSynthWindow::onApplicationSet (void)
{
    Glib::RefPtr<Gtk::Application> app = get_application();

    if (!app)
        return;

    for (size_t i = 0; i < accels_.size(); i++)
        app->set_accel_for_action(accels_[i].first, accels_[i].second);
}

/* The menu, as a model and a set of actions.
 *
 * Gtk::MenuBar, Gtk::Menu and Gtk::MenuItem are all gone. What replaces them
 * is a Gio::Menu -- a description of the menu with no widgets in it -- shown
 * by a Gtk::PopoverMenuBar, with the behaviour attached separately as named
 * actions on the window.
 *
 * The indirection earns its keep: an accelerator now binds to "win.keyboard"
 * rather than to a widget, so it works before the menu has ever been opened
 * and keeps working if the item moves. */
void MainSynthWindow::populateMenu (void)
{
    Glib::RefPtr<Gio::Menu> file = Gio::Menu::create();
    Glib::RefPtr<Gio::Menu> fileItems = Gio::Menu::create();
    Glib::RefPtr<Gio::Menu> quitItem = Gio::Menu::create();
    Glib::RefPtr<Gio::Menu> help = Gio::Menu::create();

    addAction("keyboard",
              sigc::mem_fun(*this, &MainSynthWindow::menuKeyboard), "<Control>k");
    addAction("patchsel",
              sigc::mem_fun(*this, &MainSynthWindow::menuPatchSel), "<Control>p");
    addAction("midimap",
              sigc::mem_fun(*this, &MainSynthWindow::menuMidiMap), "<Control>m");
    addAction("quit",
              sigc::mem_fun(*this, &MainSynthWindow::menuQuit), "<Control>q");
    addAction("about", sigc::mem_fun(*this, &MainSynthWindow::menuAbout));

    /* No Node View item. It dates from when the editor was a window of its
       own; now that it is a tab on the patch page, the menu could only do
       what clicking the tab does -- and it had to explain itself with a
       dialog when the current channel had no patch on it. */
    fileItems->append("_Keyboard", "win.keyboard");
    fileItems->append("_Patch Selector", "win.patchsel");
    fileItems->append("_MIDI Controllers", "win.midimap");

    /* A separator is a section boundary in a menu model rather than an item
       of its own, so Quit goes in a section by itself. */
    quitItem->append("_Quit", "win.quit");

    file->append_section(fileItems);
    file->append_section(quitItem);

    help->append("_About", "win.about");

    Glib::RefPtr<Gio::Menu> bar = Gio::Menu::create();

    bar->append_submenu("_File", file);
    bar->append_submenu("_Help", help);

    /* Help no longer sits over on the right. set_right_justified went with
       Gtk::MenuItem, and a menu model has no place to say it. */
    menuBar_ = Gtk::manage(new Gtk::PopoverMenuBar(bar));
}


void MainSynthWindow::menuKeyboard (void)
{
    if (kbWin_ == NULL)
    {
        kbWin_ = new KeyboardWindow (thSynth::instance());
        kbWin_->signal_close_request().connect(
            sigc::bind(sigc::mem_fun(*this, &MainSynthWindow::onSubWindowClose),
                       (Gtk::Window *)kbWin_), false);

        addCloseAccel(kbWin_);
    }

    kbWin_->present();
}

void MainSynthWindow::menuPatchSel (void)
{
    if (patchSel_ == NULL)
    {
        patchSel_ = new PatchSelWindow(thSynth::instance());
        patchSel_->signal_close_request().connect(
            sigc::bind(sigc::mem_fun(*this, &MainSynthWindow::onSubWindowClose),
                       (Gtk::Window *)patchSel_), false);

        addCloseAccel(patchSel_);
    }
    
    patchSel_->present();
}

void MainSynthWindow::menuMidiMap (void)
{
    if (midiMap_ == NULL)
    {
        midiMap_ = new MidiMap(thSynth::instance());
        midiMap_->signal_close_request().connect(
            sigc::bind(sigc::mem_fun(*this, &MainSynthWindow::onSubWindowClose),
                       (Gtk::Window *)midiMap_), false);

        addCloseAccel(midiMap_);
    }

    midiMap_->present();
}

void MainSynthWindow::menuQuit (void)
{
    shutdown();
}

void MainSynthWindow::menuAbout (void)
{
    if (aboutBox_)
        return;

    aboutBox_ = new AboutBox;
    aboutBox_->signal_close_request().connect(
        sigc::bind(sigc::mem_fun(*this, &MainSynthWindow::onSubWindowClose),
                   (Gtk::Window *)aboutBox_), false);

    addCloseAccel(aboutBox_);
    aboutBox_->present();
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

    lbl->set_xalign(0.0);
    lbl->set_ellipsize(Pango::EllipsizeMode::END);

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
        lbl->set_justify(Gtk::Justification::CENTER);
        notebook_.append_page(*lbl, *makeTabLabel(tabName, tip));
        return;
    }

    gthPatchManager *patchMgr = gthPatchManager::instance();
    thArgMap args = patchMgr->getChannelArgs(num);

    /* XXX: this no longer applies */
    /* only 'amp' */
    if (args.size() == 1)
    {
        Gtk::Label *sorry = manage(new Gtk::Label("Sorry, this DSP does not have modifiable settings."));
        sorry->set_justify(Gtk::Justification::CENTER);
        notebook_.append_page(*sorry, *makeTabLabel(tabName, tip));
        return;
    }
        
    Gtk::ScrolledWindow *tab_view = manage(new Gtk::ScrolledWindow);
    Gtk::Box *tab_vbox = manage(new Gtk::Box(Gtk::Orientation::VERTICAL));

    /* Room around and between the two frames. Everything sat flush against
       the panel edge and against each other: the frame labels touched the
       left border, and "Description:" ran into the line under it. */
    tab_vbox->set_margin_start(8);
    tab_vbox->set_margin_end(8);
    tab_vbox->set_margin_top(8);
    tab_vbox->set_margin_bottom(8);
    tab_vbox->set_spacing(8);
    Gtk::Frame *info_frame = manage(new Gtk::Frame);
    Gtk::Grid *info_table = manage(new Gtk::Grid);

    tab_view->set_child(*tab_vbox);

    /* No horizontal scrolling, and the parameter panel needs it that way.
     *
     * A scrolled window that may scroll gives its child the width the child
     * asks for and scrolls to the rest; one that may not gives the child the
     * viewport. The panel now decides its own column count from the width it
     * is handed, so the first of those means it is asked how wide it would
     * like to be, answers with the widest thing it can draw, and never wraps
     * -- the sideways scrollbar appears instead.
     *
     * It was NEVER before the columns existed, then AUTOMATIC so a narrow
     * window could scroll to sliders squeezed to a nub rather than crushing
     * them. Wrapping is the better answer to that and this is what it needs.
     * The panel's minimum is one column, so the window still goes narrow. */
    tab_view->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

    info_frame->set_label("DSP Information");
    info_frame->set_child(*info_table);

    info_table->set_column_spacing(5);
    info_table->set_row_spacing(5);

    /* Inside the frame as well as outside it, so the text is not against the
       frame's own line. */
    info_table->set_margin_start(6);
    info_table->set_margin_end(6);
    info_table->set_margin_top(6);
    info_table->set_margin_bottom(6);

    thArg *dspName = args["name"];

    if (dspName)
    {
        Gtk::Label *lname_lbl = manage(new Gtk::Label("Name: "));
        Gtk::Label *rname_lbl = manage(new Gtk::Label(dspName->comment()));

        lname_lbl->set_xalign(1.0);
        rname_lbl->set_xalign(0.0);

        info_table->attach(*lname_lbl, 0, 0, 1, 1);
        info_table->attach(*rname_lbl, 1, 0, 1, 1);
    }

    thArg *dspAuthor = args["author"];

    if (dspAuthor)
    {
        Gtk::Label *lname_lbl = manage(new Gtk::Label("Author: "));
        Gtk::Label *rname_lbl = manage(new Gtk::Label(dspAuthor->comment()));

        lname_lbl->set_xalign(1.0);
        rname_lbl->set_xalign(0.0);

        
        info_table->attach(*lname_lbl, 0, 1, 1, 1);
        info_table->attach(*rname_lbl, 1, 1, 1, 1);
    }

    thArg *dspDesc = args["desc"];

    if (dspDesc)
    {
        Gtk::Label *lname_lbl = manage(new Gtk::Label("Description: "));
        Gtk::Label *rname_lbl = manage(new Gtk::Label(dspDesc->comment()));

        lname_lbl->set_xalign(1.0);
        rname_lbl->set_xalign(0.0);
        
        info_table->attach(*lname_lbl, 0, 2, 1, 1);
        info_table->attach(*rname_lbl, 1, 2, 1, 1);
    }

    Gtk::Frame *dsp_frame = manage(new Gtk::Frame);
    ArgTable *dsp_table = manage(new ArgTable);

    dsp_frame->set_label("DSP Parameters");
    dsp_table->set_margin_start(6);
    dsp_table->set_margin_end(6);
    dsp_table->set_margin_top(6);
    dsp_table->set_margin_bottom(6);
    dsp_frame->set_child(*dsp_table);
        
    tab_vbox->append(*info_frame);
    dsp_frame->set_vexpand(true);
    tab_vbox->append(*dsp_frame);

    /* Which node drives each control, so the panel can gather them the way
       the node editor does. */
    std::map<string, string> groups = inferGroups(num);

    /* populate each tab */
    for (thArgMap::iterator j = args.begin();
         j != args.end(); j++)
    {
        string argName = j->first;
        thArg *arg = j->second;

        if (arg == NULL)
            continue;

        /* The channel amplitude is a slider like the rest, and it is drawn
           once already -- pinned to the patch bar above, where it is in the
           same place on every page. Twice would be two controls for one
           value. */
        if (argName == "amp")
            continue;

        switch (arg->widgetType())
        {
            case thArg::HIDE:
                break;
            case thArg::SLIDER:
            {
                dsp_table->insertArg(arg, groups.count(argName)
                                          ? groups[argName] : string());
                break;                
            }
            default:
                break;
        }
    }

    dsp_table->setChannel(num);

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
    Gtk::Box *holder = manage(new Gtk::Box(Gtk::Orientation::VERTICAL));

    sub->append_page(*tab_view, "Overview");
    sub->append_page(*holder, "Nodes");

    sub->signal_switch_page().connect(
        sigc::bind(
            sigc::mem_fun(*this, &MainSynthWindow::onSubTab),
            holder, patchMgr->getPatch(num) ? patchMgr->getPatch(num)->dspFile
                                            : string(),
            num));

    /* The patch bar sits above the two views rather than in either of them.
       Which patch this is, how loud it is and whether it has been saved are
       facts about the patch; they should not go away because you switched to
       the graph, and they should not scroll off the top of the parameter
       panel. */
    Gtk::Box *page = manage(new Gtk::Box(Gtk::Orientation::VERTICAL));

    page->append(*makePatchBar(num));
    page->append(*manage(new Gtk::Separator(Gtk::Orientation::HORIZONTAL)));
    sub->set_vexpand(true);
    page->append(*sub);

    notebook_.append_page(*page, *makeTabLabel(tabName, tip));

}

/* The strip across the top of a patch page.
 *
 * Amplitude is here rather than down among the DSP Parameters because it is
 * the one control every patch has. In the parameter grid it sorts in with
 * whatever the .dsp happens to declare -- first for one patch, third for
 * another, in a different column for a third -- so the control you reach for
 * most often is the one you have to look for. Pinned here it is in the same
 * place on every page, on the same 0..127 scale as the Master slider at the
 * top of the window, which is the thing it is multiplied against.
 *
 * Save and Save As are here for the same reason. Saving a patch meant opening
 * the Patch Selector, finding this channel's row in it and saving from there
 * -- a second window to reach an operation that belongs to the page you are
 * already looking at. */
Gtk::Widget *MainSynthWindow::makePatchBar (int chan)
{
    gthPatchManager *patchMgr = gthPatchManager::instance();
    gthPatchManager::PatchFile *patch = patchMgr->getPatch(chan);

    Gtk::Box *bar = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL));

    bar->set_spacing(6);
    bar->set_margin_start(6);
    bar->set_margin_end(6);
    bar->set_margin_top(6);
    bar->set_margin_bottom(6);

    const bool saved = (patch != NULL) && (patch->filename.length() > 0);

    Gtk::Label *nameLbl = manage(new Gtk::Label);

    /* The tab carries this too, but ellipsised to sixteen characters in a
       narrow strip -- so it is often the end of the name that is missing, and
       the end is what tells two versions of a patch apart. */
    nameLbl->set_markup("<b>" + Glib::Markup::escape_text(
                            saved
                            ? thUtil::basename(patch->filename.c_str())
                            : string("(unsaved)")) + "</b>");
    nameLbl->set_ellipsize(Pango::EllipsizeMode::MIDDLE);

    if (saved)
        nameLbl->set_tooltip_text(patch->filename);

    bar->append(*nameLbl);

    thArg *amp = thSynth::instance()->getChanArg(chan, "amp");

    if (amp != NULL)
    {
        Gtk::Label *ampLbl = manage(new Gtk::Label("Amplitude:"));
        Gtk::Scale *ampScale = manage(new Gtk::Scale(Gtk::Orientation::HORIZONTAL));

        /* Deliberately the same shape as masterScale_: same range, same
           steps, value on the right. The two multiply together, so they
           should not look like different kinds of control. */
        ampScale->set_range(0, MIDIVALMAX);
        ampScale->set_increments(1, 10);
        ampScale->set_digits(0);
        ampScale->set_draw_value(true);
        ampScale->set_value_pos(Gtk::PositionType::RIGHT);
        ampScale->set_size_request(160, -1);
        ampScale->set_value((*amp)[0]);

        ampScale->signal_value_changed().connect(
            sigc::bind(
                sigc::mem_fun(*this, &MainSynthWindow::onAmpSlider),
                ampScale, chan));

        /* MIDI volume, the Patch Selector and a patch load all write this arg
           behind the slider's back. */
        ampConns_.push_back(amp->signal_arg_changed().connect(
            sigc::bind(
                sigc::mem_fun(*this, &MainSynthWindow::onAmpArgChanged),
                chan)));

        ampScales_[chan] = ampScale;

        bar->append(*manage(new Gtk::Separator(
                                Gtk::Orientation::VERTICAL)));
        bar->append(*ampLbl);
        bar->append(*ampScale);
    }

    /* Over on the right, which a GTK4 box reaches by appending an expanding
       nothing first: it packs one way now, and pack_end is gone. */
    SaveButton *saveBtn = manage(new SaveButton);

    saveBtn->signal_save().connect(
        sigc::bind(sigc::mem_fun(*this, &MainSynthWindow::onSavePatch), chan));
    saveBtn->signal_save_as().connect(
        sigc::bind(sigc::mem_fun(*this, &MainSynthWindow::onSavePatchAs),
                   chan));

    /* Nothing to overwrite until the patch has a file of its own -- which
       makes Save mean Save As rather than making it dead. */
    saveBtn->setHasFile(saved);
    saveBtn->setModified(patchMgr->isDirty(chan));

    /* The button outlives this call and the patch's state changes under it,
       so it listens rather than being told once. */
    saveConns_.push_back(patchMgr->signal_patch_dirty().connect(
        sigc::bind(sigc::mem_fun(*this, &MainSynthWindow::onPatchDirty),
                   saveBtn, chan)));

    if (saved)
        saveBtn->setFileName(patch->filename);

    {
        Gtk::Box *gap = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL));

        gap->set_hexpand(true);
        bar->append(*gap);
    }

    bar->append(*saveBtn);

    return bar;
}

/* Looked up rather than captured: the arg belongs to the thMidiChan, and
   loading a patch onto this channel replaces the channel. */
/* One of the sixteen patches changed; if it is this button's, say so. */
void MainSynthWindow::onPatchDirty (int chan, SaveButton *button, int mine)
{
    if (chan == mine)
        button->setModified(gthPatchManager::instance()->isDirty(mine));
}

void MainSynthWindow::onAmpSlider (Gtk::Scale *scale, int chan)
{
    thArg *amp = thSynth::instance()->getChanArg(chan, "amp");

    if (amp == NULL)
        return;

    const double want = scale->get_value();

    /* Nothing to do, and nothing to report, when the slider is only catching
       up with the arg.
     *
     * The two follow each other -- onAmpArgChanged moves the slider when
       anything else moves the arg -- so without this, restoring a saved
       amplitude at startup arrived here as though someone had dragged it, and
       every patch came up already modified. */
    if ((double)(*amp)[0] == want)
        return;

    amp->setValue(want);

    gthPatchManager::instance()->markDirty(chan);
}

void MainSynthWindow::onAmpArgChanged (thArg *arg, int chan)
{
    std::map<int, Gtk::Scale *>::iterator i = ampScales_.find(chan);

    if (i == ampScales_.end() || i->second == NULL)
        return;

    i->second->set_value((*arg)[0]);
}

void MainSynthWindow::onSavePatch (int chan)
{
    gthPatchManager::PatchFile *patch =
        gthPatchManager::instance()->getPatch(chan);

    if (patch == NULL || patch->filename.empty())
        return;

    Glib::signal_idle().connect_once(
        sigc::bind(
            sigc::mem_fun(*this, &MainSynthWindow::doSavePatch),
            patch->filename, chan));
}

void MainSynthWindow::onSavePatchAs (int chan)
{
    gthPatchManager *patchMgr = gthPatchManager::instance();
    gthPatchManager::PatchFile *patch = patchMgr->getPatch(chan);

    if (patch == NULL)
        return;

    /* On the heap and answered later. run() is gone, so a chooser cannot be a
       question asked in the middle of a function any more -- everything below
       the point this used to block has moved into onSavePatchAsResponse. */
    Gtk::FileChooserDialog *fileSel =
        new Gtk::FileChooserDialog(*this, "thinksynth - Save Patch",
                                   Gtk::FileChooser::Action::SAVE);

    fileSel->set_modal(true);
    fileSel->add_button("_Cancel", Gtk::ResponseType::CANCEL);
    fileSel->add_button("_Save", Gtk::ResponseType::OK);

    /* The same preference the Patch Selector keeps, so the two agree about
       where patches live rather than each remembering separately. */
    string **vals = gthPrefs::instance()->Get("patchdir");

    if (vals != NULL && vals[0] != NULL)
        fileSel->set_current_folder(Gio::File::create_for_path(*(vals[0])));

    if (patch->filename.length() > 0)
    {
        fileSel->set_file(Gio::File::create_for_path(patch->filename));
    }
    else if (patch->dspFile.length() > 0)
    {
        /* A starting point rather than a guess at what it should be called:
           the DSP's own name with the patch extension, which is at least in
           the right family. */
        string suggest = thUtil::basename(patch->dspFile.c_str());
        const string::size_type dot = suggest.rfind('.');

        if (dot != string::npos)
            suggest.erase(dot);

        fileSel->set_current_name(suggest + ".patch");
    }

    fileSel->signal_response().connect(
        sigc::bind(sigc::mem_fun(*this,
                                 &MainSynthWindow::onSavePatchAsResponse),
                   fileSel, chan));

    fileSel->present();
}

void MainSynthWindow::onSavePatchAsResponse (int response,
                                             Gtk::FileChooserDialog *fileSel,
                                             int chan)
{
    const string file = response == Gtk::ResponseType::OK
                        ? chosenPath(*fileSel) : string();

    closeDialog(fileSel);

    if (file.empty())
        return;

    {
        string **dir = new string *[2];

        dir[0] = new string(thUtil::dirname(file.c_str()));
        dir[1] = NULL;

        gthPrefs::instance()->Set("patchdir", dir);
    }

    /* Asked here rather than by the chooser. GTK3's did it itself; GTK4
       dropped the property and does not confirm in its place.
    
       The write is still deferred, and the reason has changed twice over:
       there is no click on the stack any more, but savePatch rebuilds every
       page, and doing that from inside a dialog's own response handler
       destroys widgets the emission is walking. */
    confirmOverwrite(this, file,
        sigc::bind(sigc::mem_fun(*this, &MainSynthWindow::queueSavePatch),
                   file, chan));
}

/* Deferred out of the click that asked for it.
 *
 * savePatch emits signal_patches_changed, and this window answers that by
 * removing every notebook page and building them again -- so the button whose
 * handler is on the stack, and the page holding it, would be destroyed
 * underneath an emission that is still running. Writing from an idle callback
 * means the click has returned first and nothing is left pointing into the
 * page. */
void MainSynthWindow::doSavePatch (string file, int chan)
{
    if (!gthPatchManager::instance()->savePatch(file, chan))
        showError(this, "Could not write " + file);
}

/* Which node each control drives, for grouping the panel by.
 *
 * Almost no patch declares `.group', but almost every patch groups its
 * controls all the same -- by what they are wired to. `@a', `@d', `@s' and
 * `@r' all feed the same env node, and that is the envelope, whether or not
 * anyone wrote the word down. The node editor already draws them this way,
 * stacked on the node they drive; this is the same rule, so the two views
 * agree about what belongs together.
 *
 * Only controls with exactly one consumer are grouped. One read by several
 * nodes belongs to no single one of them -- it is a patch-wide control, and
 * the node editor leaves those as free-standing boxes for the same reason. */
std::map<string, string> MainSynthWindow::inferGroups (int chan)
{
    std::map<string, string> host;
    std::map<string, int> uses;

    thMidiChan *channel = thSynth::instance()->getChannel(chan);

    if (channel == NULL)
        return host;

    thSynthTree *tree = channel->modnode();

    if (tree == NULL)
        return host;

    const thSynthTree::NodeMap &nodes = tree->nodes();

    for (thSynthTree::NodeMap::const_iterator n = nodes.begin();
         n != nodes.end(); ++n)
    {
        if (n->second == NULL)
            continue;

        const thArgMap &args = n->second->args();

        for (thArgMap::const_iterator a = args.begin(); a != args.end(); ++a)
        {
            if (a->second == NULL ||
                a->second->type() != thArg::ARG_CHANNEL)
                continue;

            const string ctl = a->second->argPtrName();

            uses[ctl]++;
            host[ctl] = n->second->name();
        }
    }

    for (std::map<string, int>::iterator u = uses.begin();
         u != uses.end(); ++u)
        if (u->second != 1)
            host.erase(u->first);

    return host;
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

    /* Not while the pages are being taken away: this fires as a side effect
       of a sub-notebook losing its Overview page, and the Nodes page it is
       switching to is on its way out with it. Building an editor there is a
       plugin directory scanned and a .dsp parsed for a widget nobody will
       see -- and at shutdown, when the window is destroyed after the synth,
       it was a NULL thSynth::instance() handed to a node editor that then
       parsed with it. */
    if (tearingDown_ || num != 1 || editors_.count(holder))
        return;

    Gtk::Box *box = dynamic_cast<Gtk::Box *>(holder);

    if (box == NULL)
        return;

    NodeEditor *ed = manage(new NodeEditor(thSynth::instance()));

    editors_[holder] = ed;

    ed->set_vexpand(true);
    box->append(*ed);

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
    editors_.clear();

    /* The sliders about to be discarded are subscribed to args that outlive
       them. Dropping the subscriptions here rather than leaving them to find
       a replaced widget keeps a session's worth of patch loads from
       accumulating one per page per reload. */
    for (size_t i = 0; i < ampConns_.size(); i++)
        ampConns_[i].disconnect();

    for (size_t i = 0; i < saveConns_.size(); i++)
        saveConns_[i].disconnect();

    ampConns_.clear();
    saveConns_.clear();
    ampScales_.clear();

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
            /* Nothing on this channel at all. Distinct from a channel with
               a DSP loaded but no patch file saved for it yet, which keeps
               "(Untitled)" below -- the two used to read the same, so an
               empty channel and unsaved work looked alike. */
            tabName = chanStr.str() + "(empty)";
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
                      thUtil::basename(patch->filename.c_str());
        }
        else
        {
            /* A DSP is loaded; no patch file has been saved for it. */
            tabName = chanStr.str() + "(Untitled)";
        }

        append_tab (tabName, patch->filename, i, true);
    }

    /* The first page is current without a switch ever happening, so it would
       otherwise be the one tab never marked. */
    const int cur = notebook_.get_current_page();

    highlightTab(cur < 0 ? 0 : cur);
}


/* Empties the notebook.
 *
 * gtkmm-3 dropped Notebook::pages() and Widget::hide_all(), so pages are
 * removed one at a time and hide() covers the children.
 *
 * Removing a page destroys the Overview/Nodes notebook sitting on it, and a
 * notebook that is losing pages switches to whichever one is left. That
 * arrives at onSubTab as a request to build a node editor -- on a page that
 * is being destroyed, which is at best a plugin directory scanned and a .dsp
 * parsed for nothing. Hence the flag. */
void MainSynthWindow::clearPages (void)
{
    tearingDown_ = true;

    /* Not hidden first any more. That was to spare the flicker of sixteen
       pages going one at a time, and it depended on a show_all() afterwards
       to undo it -- which GTK4 does not have, so the notebook stayed hidden
       and the window came up empty below the toolbar. */
    while (notebook_.get_n_pages() > 0)
        notebook_.remove_page(-1);

    tearingDown_ = false;
}

void MainSynthWindow::onPatchesChanged (void)
{
    int pagenum = notebook_.get_current_page();

    clearPages();

    populate();

    if (pagenum != -1)
        notebook_.set_current_page(pagenum);
}

void MainSynthWindow::onPatchLoadError (const char* failure)
{
    showError(this, "Could not load the patch file",
              Glib::ustring(failure) +
              "\n\nA syntax error, or the DSP it names does not exist.");
}

/* Closing one of the secondary windows.
 *
 * They used to be deleted when they hid, and rebuilt on the next open. That
 * cannot work in GTK4: closing a window destroys it rather than hiding it, so
 * the hide never arrived, the pointer was still set, and the next open called
 * present() on a destroyed window -- "A window is shown after it has been
 * destroyed. This will leave the window in an inconsistent state."
 *
 * Returning true keeps the window: GTK asks whether it may close, and this
 * says no and hides it instead. So it survives to be presented again, which
 * is both simpler and cheaper than rebuilding it -- the patch selector and
 * the MIDI map are subscribed to the patch manager and keep themselves
 * current whether they are on screen or not.
 *
 * They are deleted in the destructor now, which is also where they have to be:
 * every one of them points into the synth. */
/* Ctrl+W closes a secondary window.
 *
 * A key controller rather than an application accelerator, which is how
 * Ctrl+K and the rest are done. Those work because the main window is added
 * to the application and "win.keyboard" resolves against it; these windows
 * are deliberately not added -- the application quits when the last of its
 * windows goes, and the shutdown ordering wants exactly one window deciding
 * that. So an accelerator registered on the application would never reach
 * them.
 *
 * In the capture phase, so the window hears the key before whatever has
 * focus. Nothing in these windows binds Ctrl+W, but a text field one day
 * might, and a window's own close key should not be the thing that loses.
 */
void MainSynthWindow::addCloseAccel (Gtk::Window *window)
{
    if (window == NULL)
        return;

    Glib::RefPtr<Gtk::EventControllerKey> keys =
        Gtk::EventControllerKey::create();

    keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    keys->signal_key_pressed().connect(
        sigc::bind(sigc::mem_fun(*this, &MainSynthWindow::onSubWindowKey),
                   window),
        false);

    window->add_controller(keys);
}

/* True to say the key has been dealt with; false for everything that is not
   Ctrl+W, so the window carries on as before. */
bool MainSynthWindow::onSubWindowKey (guint keyval, guint keycode,
                                      Gdk::ModifierType state,
                                      Gtk::Window *window)
{
    (void)keycode;

    if ((keyval != GDK_KEY_w && keyval != GDK_KEY_W)
        || (state & Gdk::ModifierType::CONTROL_MASK)
           != Gdk::ModifierType::CONTROL_MASK)
        return false;

    /* close(), not hide(): it goes through the close-request handler, so
       there is one place that decides what closing one of these means. */
    window->close();

    return true;
}

bool MainSynthWindow::onSubWindowClose (Gtk::Window *window)
{
    if (window != NULL)
        window->set_visible(false);

    return true;
}

/* gtkmm-3 passes the page widget itself rather than the opaque GtkNotebookPage
   struct, which no longer exists. */
/* Make it obvious which tab is the current one.
 *
 * Across the top a selected tab is unmistakable -- it joins the page below
 * it. Down the side that join is a one-pixel edge, and with the labels
 * left-aligned in a tall strip the only other mark was the dotted focus
 * rectangle, which is about having keyboard focus and not about which patch
 * you are looking at.
 *
 * Bold rather than a colour or a background: it reads on every theme, light
 * or dark, without this having an opinion about the palette. get_text()
 * returns the label's plain text whether or not markup was applied to it, so
 * the name survives being marked up and unmarked repeatedly. */
void MainSynthWindow::highlightTab (int pagenum)
{
    for (int i = 0; i < notebook_.get_n_pages(); i++)
    {
        Gtk::Widget *w = notebook_.get_nth_page(i);

        if (w == NULL)
            continue;

        Gtk::Label *lbl =
            dynamic_cast<Gtk::Label *>(notebook_.get_tab_label(*w));

        if (lbl == NULL)
            continue;

        const string text = lbl->get_text();

        if (i == pagenum)
            lbl->set_markup("<b>" + Glib::Markup::escape_text(text) + "</b>");
        else
            lbl->set_text(text);
    }
}

/* 100 on the slider is unity gain, so the number reads like a channel
   amplitude rather than a multiplier. setMasterGain clamps and stores
   atomically; the audio thread reads it every block. */
void MainSynthWindow::onMasterGain (void)
{
    thSynth::instance()->setMasterGain(masterScale_.get_value() / 100.0);
}

void MainSynthWindow::onSwitchPage (Gtk::Widget *page, guint pagenum)
{
    highlightTab((int)pagenum);

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
        showError(this, "Could not load the DSP",
                  dspfile + "\n\nA syntax error, or it does not exist.");

        return;
    }

    clearPages();

    populate();

    notebook_.set_current_page(pagenum);
}

void MainSynthWindow::onBrowseButton (void)
{
    Gtk::FileChooserDialog *fileSel =
        new Gtk::FileChooserDialog(*this, "thinksynth - Load DSP",
                                   Gtk::FileChooser::Action::OPEN);

    fileSel->set_modal(true);
    fileSel->add_button("_Cancel", Gtk::ResponseType::CANCEL);
    fileSel->add_button("_Open", Gtk::ResponseType::OK);

    if (prevDir_ != "")
        fileSel->set_current_folder(Gio::File::create_for_path(prevDir_));

    /* The page is captured now rather than read in the handler: the chooser
       is not modal to the notebook, and the tab that was current when Browse
       was clicked is the one this is loading onto. */
    fileSel->signal_response().connect(
        sigc::bind(sigc::mem_fun(*this, &MainSynthWindow::onBrowseResponse),
                   fileSel, notebook_.get_current_page()));

    fileSel->present();
}

void MainSynthWindow::queueSavePatch (string file, int chan)
{
    Glib::signal_idle().connect_once(
        sigc::bind(
            sigc::mem_fun(*this, &MainSynthWindow::doSavePatch), file, chan));
}

void MainSynthWindow::onBrowseResponse (int response,
                                        Gtk::FileChooserDialog *fileSel,
                                        int pagenum)
{
    const string picked = response == Gtk::ResponseType::OK
                          ? chosenPath(*fileSel) : string();

    closeDialog(fileSel);

    if (picked.empty())
        return;

    dspEntry_.set_text(picked);

    if (!gthPatchManager::instance()->newPatch(picked, pagenum))
    {
        showError(this, "Could not load the DSP",
                  picked + "\n\nA syntax error, or it does not exist.");
        return;
    }

    prevDir_ = thUtil::dirname(picked.c_str());
    prevDir_ += "/";

    {
        string **vals = new string *[2];

        vals[0] = new string(prevDir_);
        vals[1] = NULL;

        gthPrefs::instance()->Set("dspdir", vals);
    }

    /* load up the patch file */
    clearPages();

    populate();
    notebook_.set_current_page(pagenum);
}
