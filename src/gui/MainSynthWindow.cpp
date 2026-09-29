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

#include <iostream>
#include <sstream>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <system_error>
#include <signal.h>

#include <gtkmm.h>
#include <gtkmm/messagedialog.h>

#include "think.h"

#include "PatchSelPanel.h"
#include "Panes.h"
#include "Keyboard.h"
#include "KeyboardPanel.h"
#include "Composer.h"
#include "MainSynthWindow.h"

#include "../gthTheme.h"
#include "AboutBox.h"
#include "MidiMap.h"
#include "ArgPanelView.h"
#include "NodeEditor.h"
#include "../DspCatalog.h"
#include "ItemBrowser.h"
#include "Dialogs.h"
#include "SaveButton.h"


#include "../gthPrefs.h"
#include "../gthPatchfile.h"


bool chosen = false;

/* The window's three modes, spelled as the web page spells them and as
   thinkrc and panes.ini keep them. */
static const char *PATCH_MODE = "patch";
static const char *PIECE_MODE = "piece";
static const char *SEQ_MODE = "seq";

static bool isMode (const string &mode)
{
    return mode == PATCH_MODE || mode == PIECE_MODE || mode == SEQ_MODE;
}

MainSynthWindow::MainSynthWindow (gthAudio *audio)
{
    audio_ = audio;

    set_title("thinksynth");

    tearingDown_ = false;
    panes_ = NULL;
    chan_ = 0;
    selecting_ = false;
    width_ = 0;
    height_ = 0;

    /* Room for the first layout's three columns and the keys under them,
       each at its minimum and a little over. This is what a first run gets;
       applyPrefs replaces it with the size the last one was left at. */
    set_default_size(1440, 900);

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
    kbPanel_ = NULL;
    composer_ = NULL;

    /* Likewise the DSP browser: DSP_PATH is the *build* machine's install
       prefix, so on a relocatable package it names a directory the user has
       never had. findDataDir finds the one that is actually there. */
    dspDir_ = thUtil::findDataDir("dsp", "THINK_DSP_PATH", DSP_PATH);

    /* Where a file chooser opens, which the preferences may move; the
       catalog's root, above, is the shipped tree and does not move with it.
       They start out the same. */
    prevDir_ = dspDir_;

    /* "win.pane-keyboard" and the rest resolve against this. */
    actions_ = Gio::SimpleActionGroup::create();
    insert_action_group("win", actions_);

    /* Before the menu, which carries its file commands. Built, not
       started: see Composer::start. */
    composer_ = new Composer(thSynth::instance());
    insert_action_group("composer", composer_->actions());

    populateMenu();

    property_application().signal_changed().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onApplicationSet));

    
    /* The title bar carries what is about the whole window: the piece's
       transport and what it is doing, the master level and the menu. What
       is about one channel is in its panes. The piece's half waits until
       there is a piece. */
    {
        Gtk::Label *title = manage(new Gtk::Label("thinksynth"));

        title->add_css_class("title");
        composer_->status().add_css_class("subtitle");
        composer_->status().set_visible(false);

        titleBox_.set_valign(Gtk::Align::CENTER);
        titleBox_.append(*title);
        titleBox_.append(composer_->status());
    }

    composer_->transport().set_visible(false);
    header_.set_title_widget(titleBox_);
    /* The mode first, at the start of the bar: it decides what the rest
       of the window is. */
    patchModeBtn_.set_label("Patch");
    patchModeBtn_.set_tooltip_text("Patch mode: a channel's graph and "
                                   "parameters (Ctrl+1)");
    patchModeBtn_.set_action_name("win.mode");
    patchModeBtn_.set_action_target_value(
        Glib::Variant<Glib::ustring>::create(PATCH_MODE));
    pieceModeBtn_.set_label("Piece");
    pieceModeBtn_.set_tooltip_text("Piece mode: the composer, its "
                                   "settings and the roll (Ctrl+2)");
    pieceModeBtn_.set_action_name("win.mode");
    pieceModeBtn_.set_action_target_value(
        Glib::Variant<Glib::ustring>::create(PIECE_MODE));
    seqModeBtn_.set_label("Sequence");
    seqModeBtn_.set_tooltip_text("Sequence mode: grid tracks, what plays "
                                 "each one, and the roll (Ctrl+3)");
    seqModeBtn_.set_action_name("win.mode");
    seqModeBtn_.set_action_target_value(
        Glib::Variant<Glib::ustring>::create(SEQ_MODE));
    modeBox_.add_css_class("linked");
    modeBox_.append(patchModeBtn_);
    modeBox_.append(pieceModeBtn_);
    modeBox_.append(seqModeBtn_);
    header_.pack_start(modeBox_);
    header_.pack_start(composer_->transport());
    header_.pack_end(menuBtn_);
    set_titlebar(header_);

    dspEntryLbl_.set_label("Channel 1 DSP:");
    dspBrowseBtn_.set_label("Browse");
    dspEntryBox_.set_spacing(6);
    dspEntryBox_.set_margin(6);
    dspEntryBox_.append(dspEntryLbl_);
    dspEntry_.set_hexpand(true);
    dspEntryBox_.append(dspEntry_);
    dspEntryBox_.append(dspBrowseBtn_);

    /* Master level, in the title bar because it is the whole synth's rather
       than one channel's. Shown 0..127 like a channel's own amplitude, so
       the two read on one scale; 100 is unity, which is where it starts, and
       there is room above it because the engine allows gain over 1.
    
       The limiter downstream is what makes going above unity safe to offer;
       see TH_LIMIT_KNEE. */
    masterLbl_.set_text("Master");

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
    masterScale_.set_tooltip_text("Master level: 100 is unity");

    masterScale_.signal_value_changed().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onMasterGain));

    header_.pack_end(masterScale_);
    header_.pack_end(masterLbl_);

    dspEntry_.signal_activate().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onDspEntryActivate));

    dspBrowseBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onBrowseButton));

    buildPanes();

    populate();
    selectChannel(0);

    /* Focus on the channel list to begin with. Left to GTK it goes to the
       first focusable widget, the DSP entry, with its text selected -- so
       the first key typed replaced the channel's DSP. */
    set_focus(chanList_);

    gthPatchManager *patchMgr = gthPatchManager::instance();
    patchMgr->signal_patches_changed().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onPatchesChanged));
    patchMgr->signal_patch_load_error().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onPatchLoadError));

    debug("signal connections made");

}

MainSynthWindow::~MainSynthWindow (void)
{
    /* Nothing about this window is worth building now: taking the panes
       down takes the graph out of view, and a graph coming into view builds
       an editor. */
    tearingDown_ = true;

    rememberGeometry();
    writeLayout();

    /* The panes first, and everything in them with them -- the patch list,
       the MIDI map, the keys -- before the synth, which they all reach
       into. The lists and stacks that are members here are only taken off
       them, and go with the window: the node editors with them, which
       tearingDown_ keeps anything from building more of. */
    delete panes_;
    panes_ = NULL;
    patchSel_ = NULL;
    midiMap_ = NULL;
    kbPanel_ = NULL;

    /* About is kept rather than destroyed when it closes, so this is where
       it goes; and the composer after the panes, which were holding its
       widgets, and before the synth its scheduler plays into. */
    delete aboutBox_;
    delete composer_;

    aboutBox_ = NULL;
    composer_ = NULL;

    /* Not shutdown(): the loop has already ended by the time this runs, and
       asking a torn-down application to quit again is not a thing to do in a
       destructor. Just off the screen. */
    set_visible(false);
}

/* The first layouts, for a first run and for Reset Layout, one for each
 * of the window's three modes. The web page has the same three and uses
 * the same ids, so a layout means the same on both.
 *
 * Patch: the channels down the left, the graph of the one picked in the
 * middle and its parameters on the right, and the keys along the bottom.
 * The patch list and the MIDI routing start in the drawer, a Ctrl+P and a
 * Ctrl+M away: Channels picks a channel, and the list is for loading and
 * saving whole patches.
 *
 * Piece: the canvas, with the sequencer a tab behind it and the piece's
 * settings and the selection beside it, and the roll under both, the keys a
 * tab behind the roll. The patch's panes are in the drawer rather than
 * gone: a piece is played on channels, and looking at one is a click away.
 *
 * Sequence: the tracks over the roll, and beside them what plays them --
 * the channel's parameters, the keys and Channels. No canvas: somebody
 * laying down a pattern does not need to be shown that it is a chain of
 * stages, and the mode exists to not tell them.
 */
static const char *PATCH_LAYOUT =
    "{\"dir\":\"col\",\"size\":[0.76,0.24],\"kids\":["
      "{\"dir\":\"row\",\"size\":[0.12,0.53,0.35],\"kids\":["
        "{\"tabs\":[\"channelbox\"]},"
        "{\"tabs\":[\"nodeview\"]},"
        "{\"tabs\":[\"paramview\"]}]},"
      "{\"tabs\":[\"keyboard\"]}]}";

static const char *PIECE_LAYOUT =
    "{\"dir\":\"col\",\"size\":[0.66,0.34],\"kids\":["
      "{\"dir\":\"row\",\"size\":[0.58,0.42],\"kids\":["
        "{\"tabs\":[\"composerview\",\"seqview\"]},"
        "{\"tabs\":[\"pieceedit\",\"selection\"]}]},"
      "{\"tabs\":[\"roll\",\"keyboard\"]}]}";

static const char *SEQ_LAYOUT =
    "{\"dir\":\"row\",\"size\":[0.62,0.38],\"kids\":["
      "{\"dir\":\"col\",\"size\":[0.68,0.32],\"kids\":["
        "{\"tabs\":[\"seqview\"]},"
        "{\"tabs\":[\"roll\"]}]},"
      "{\"dir\":\"col\",\"size\":[0.4,0.3,0.3],\"kids\":["
        "{\"tabs\":[\"paramview\"]},"
        "{\"tabs\":[\"keyboard\"]},"
        "{\"tabs\":[\"channelbox\"]}]}]}";

/* The key the window's one layout was kept under before there were two
   modes: read as the patch mode's, which is what it mostly was. */
static const char *OLD_LAYOUT_KEY = "desktop";

/* The composer's panes: the ones only piece mode has, and the two it
   shares with sequence mode. Everything else is in all three. */
static const char *const PIECE_PANES[] = {
    "composerview", "pieceedit", "selection",
};

static const char *const COMPOSED_PANES[] = {
    "seqview", "roll",
};

/* `content' in a scrolled window, sideways and, if `down', downward too.
 *
 * A scroller hands its child the whole width when there is room, and its
 * minimum when there is not: so a parameter panel still decides its columns
 * from the width it has, and a pane dragged narrower than the panel's one
 * column scrolls rather than cutting the column off. */
Gtk::Widget &MainSynthWindow::scrolled (Gtk::Widget &content, bool down)
{
    Gtk::ScrolledWindow *sw = manage(new Gtk::ScrolledWindow);

    sw->set_policy(Gtk::PolicyType::AUTOMATIC,
                   down ? Gtk::PolicyType::AUTOMATIC
                        : Gtk::PolicyType::NEVER);
    sw->set_child(content);

    return *sw;
}

void MainSynthWindow::buildPanes (void)
{
    panes_ = new Panes;

    chanList_.set_selection_mode(Gtk::SelectionMode::BROWSE);
    chanList_.signal_row_selected().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onChannelRow));
    chanScroll_.set_child(chanList_);
    chanScroll_.set_policy(Gtk::PolicyType::NEVER,
                           Gtk::PolicyType::AUTOMATIC);
    chanScroll_.set_vexpand(true);

    addChanBtn_.set_label("Add channel...");
    addChanBtn_.set_tooltip_text("Load a graph onto the lowest channel "
                                 "with nothing on it");
    addChanBtn_.set_margin(6);
    addChanBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onAddChannel));
    chanBox_.append(chanScroll_);
    chanBox_.append(addChanBtn_);

    paramBox_.append(dspEntryBox_);
    paramBox_.append(*manage(new Gtk::Separator(
                                 Gtk::Orientation::HORIZONTAL)));
    paramStack_.set_vexpand(true);
    paramBox_.append(paramStack_);

    nodeStack_.set_vexpand(true);

    /* The keys play the channel picked in Channels (selectChannel aims
       them). */
    kbPanel_ = manage(new KeyboardPanel(thSynth::instance()));
    patchSel_ = manage(new PatchSelPanel(thSynth::instance()));
    midiMap_ = manage(new MidiMap(thSynth::instance()));

    /* Minimum widths: what each can be read at, which is what the layout
       gives up before it gives up a pane -- and, added across a row, the
       narrowest the window can be, so they are kept low enough for a
       1280-pixel screen. A pane narrower than its content would clip it,
       so the ones that cannot wrap scroll instead. */
    panes_->add("channelbox", "Channels", chanBox_, 160);
    panes_->add("paramview", "Patch params", scrolled(paramBox_, false), 400);
    panes_->add("nodeview", "Patch graph", nodeStack_, 480);
    panes_->add("keyboard", "Keys", scrolled(*kbPanel_, false), 400);
    panes_->add("patches", "Patch Selector", scrolled(*patchSel_, true), 400);
    panes_->add("midimap", "MIDI routing", scrolled(*midiMap_, true), 400);
    panes_->add("composerview", "Piece", composer_->canvasView(), 360);
    panes_->add("seqview", "Sequencer", composer_->sequencerView(), 360);
    panes_->add("roll", "Piano roll", composer_->rollView(), 320);
    panes_->add("pieceedit", "Piece settings", composer_->settingsView(), 300);
    panes_->add("selection", "Selection", composer_->selectionView(), 300);

    mln_panes_set_window_func(panes_->gobj(), &MainSynthWindow::makePaneWindow,
                              this, NULL);

    /* A stage picked on the canvas is edited in Selection, so it comes to
       the front -- without the focus, which stays on the canvas. Unless it
       was closed: that is somebody saying they do not want it, and a click
       on the canvas is not them changing their mind. */
    composer_->signal_show_selection().connect(
        [this]
        {
            if (panes_ == NULL)
                return;

            const std::vector<string> closed = panes_->closed();

            if (std::find(closed.begin(), closed.end(), "selection") ==
                closed.end())
                panes_->present("selection", false);
        });

    /* New and Open are about the piece, whichever mode asked: piece mode
       comes up, and the piece with it, before they act. */
    composer_->signal_file_command().connect(
        [this]
        {
            if (panes_ != NULL && mode_ != PIECE_MODE)
                setDesktopMode(PIECE_MODE);
        });

    /* A track's graph, chosen from the same browser the channels use. */
    composer_->sequencer().signal_choose().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onChooseTrack));

    /* A menu command for the piece before the piece is up: the canvas
       comes into view, which starts it, and the command acts on what it
       shows. */
    composer_->signal_wanted().connect(
        [this]
        {
            if (panes_ == NULL)
                return;

            setDesktopMode(PIECE_MODE);
            panes_->present("composerview", true);
        });

    /* However it was started, the transport comes up with it. */
    composer_->signal_started().connect(
        [this]
        {
            composer_->transport().set_visible(true);
            composer_->status().set_visible(true);
        });

    panes_->setDefault(PATCH_MODE, PATCH_LAYOUT);
    panes_->setDefault(PIECE_MODE, PIECE_LAYOUT);
    panes_->setDefault(SEQ_MODE, SEQ_LAYOUT);

    panes_->signal_pane_shown().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onPaneShown));
    panes_->signal_layout_changed().connect(
        sigc::hide(sigc::mem_fun(*this, &MainSynthWindow::syncPaneActions)));
    panes_->signal_layout_kept().connect(
        sigc::mem_fun(*this, &MainSynthWindow::onLayoutKept));

    keptLayouts_[PATCH_MODE] = readLayout(PATCH_MODE);
    keptLayouts_[PIECE_MODE] = readLayout(PIECE_MODE);
    keptLayouts_[SEQ_MODE] = readLayout(SEQ_MODE);
    hadLayout_ = !keptLayouts_[PATCH_MODE].empty() ||
                 !keptLayouts_[PIECE_MODE].empty() ||
                 !keptLayouts_[SEQ_MODE].empty();

    /* Patch to begin with; applyPrefs puts the one last used back. */
    setDesktopMode(PATCH_MODE, false);

    set_child(panes_->widget());
}

bool MainSynthWindow::isPiecePane (const string &id)
{
    for (size_t i = 0; i < G_N_ELEMENTS(PIECE_PANES); i++)
        if (id == PIECE_PANES[i])
            return true;

    return isComposedPane(id);
}

bool MainSynthWindow::isComposedPane (const string &id)
{
    for (size_t i = 0; i < G_N_ELEMENTS(COMPOSED_PANES); i++)
        if (id == COMPOSED_PANES[i])
            return true;

    return false;
}

/* Where a pane asked for in a mode that has not got it is: the tracks in
   the sequence, and the rest of the composer's in the piece. */
string MainSynthWindow::modeFor (const string &id)
{
    if (id == "seqview")
        return SEQ_MODE;

    return isPiecePane(id) ? PIECE_MODE : mode_;
}

bool MainSynthWindow::inMode (const string &id, const string &mode)
{
    if (isComposedPane(id))
        return mode != PATCH_MODE;

    if (isPiecePane(id))
        return mode == PIECE_MODE;

    return true;
}

/* A pane the mode does not have is unavailable rather than closed: it
   leaves the layout without the layout forgetting where it was, so coming
   back to the mode puts it back there. */
void MainSynthWindow::setDesktopMode (const string &mode, bool keep)
{
    if (panes_ == NULL || !isMode(mode))
        return;

    if (mode == mode_)
        return;

    mode_ = mode;

    for (size_t i = 0; i < G_N_ELEMENTS(PIECE_PANES); i++)
        panes_->setAvailable(PIECE_PANES[i], inMode(PIECE_PANES[i], mode));

    for (size_t i = 0; i < G_N_ELEMENTS(COMPOSED_PANES); i++)
        panes_->setAvailable(COMPOSED_PANES[i],
                             inMode(COMPOSED_PANES[i], mode));

    /* The piece and the sequence are two documents the composer keeps
       apart; patch mode leaves whichever was up playing. The tracks offer
       to change what plays them in the sequence only. Before the panes
       are told, since a pane coming into view is what starts the
       composer, and it starts on the document this says. */
    if (mode == SEQ_MODE)
        composer_->useDocument(Composer::SEQUENCE);
    else if (mode == PIECE_MODE)
        composer_->useDocument(Composer::PIECE);

    /* Read again on the way in, so a graph added since the last time has
       its title on the buttons. */
    if (mode == SEQ_MODE)
        catalog_.reset();

    composer_->sequencer().setChoosing(
        mode == SEQ_MODE,
        [this] (const string &dsp) { return dspTitle(dsp); });

    panes_->setMode(mode);
    panes_->load(keptLayouts_[mode]);

    if (modeAction_)
        modeAction_->set_state(Glib::Variant<Glib::ustring>::create(mode));

    if (keep)
    {
        string **vals = new string *[2];

        vals[0] = new string(mode);
        vals[1] = NULL;
        gthPrefs::instance()->Set("mode", vals);
    }

    syncPaneActions();
    syncComposer();
}

/* The keys to the last track, which in the sequence is the one with a
 * keyboard instrument on it -- the others are a kit and a bass, and the
 * channel picked before was likely a patch no track plays.
 *
 * Once, the first time the sequence has tracks: after that the channel is
 * whatever somebody picked, and a trip through another mode is not a
 * reason to take it from them. From syncComposer as well as the mode
 * switch, because a window that starts in sequence mode has no tracks
 * until its pane is in view and the composer starts.
 */
void MainSynthWindow::aimKeysAtSequence (void)
{
    if (keysAimed_ || mode_ != SEQ_MODE || tearingDown_ || composer_ == NULL)
        return;

    const SeqView &seq = composer_->sequencer();
    const size_t n = seq.trackCount();

    if (n == 0 || seq.trackChannel(n - 1) < 0)
        return;

    keysAimed_ = true;
    selectChannel(seq.trackChannel(n - 1));
}

void MainSynthWindow::onPaneShown (const string &id, bool visible)
{
    if (paneActs_.count(id))
        paneActs_[id]->set_state(Glib::Variant<bool>::create(visible));

    if (id == "nodeview" && visible)
        ensureEditor(chan_);

    if (isPiecePane(id))
        syncComposer();
}

void MainSynthWindow::syncComposer (void)
{
    if (panes_ == NULL || composer_ == NULL || tearingDown_)
        return;

    const bool canvas = panes_->isVisible("composerview");
    const bool tracks = panes_->isVisible("seqview");
    const bool editing = panes_->isVisible("pieceedit") ||
                         panes_->isVisible("selection");

    /* The first time any of them is looked at, and not before: starting
       loads the piece, and the piece's instruments go onto channels. */
    if (!composer_->started() &&
        (canvas || tracks || editing || panes_->isVisible("roll")))
        composer_->start();

    composer_->setCanvasShown(canvas);
    composer_->setSequencerShown(tracks);
    composer_->setEditing(editing && composer_->started());

    aimKeysAtSequence();
}

void MainSynthWindow::syncPaneActions (void)
{
    if (panes_ == NULL)
        return;

    for (std::map<string, Glib::RefPtr<Gio::SimpleAction> >::iterator i =
             paneActs_.begin(); i != paneActs_.end(); ++i)
        i->second->set_state(
            Glib::Variant<bool>::create(panes_->isVisible(i->first)));
}

/* A tick in the menu, or its key: a pane in view closes to the drawer, and
   one that is not -- closed, or behind another tab -- comes to the front,
   where it was. So Ctrl+M shows the MIDI routing, as it did when that was
   a window, and a second Ctrl+M puts it away. */
void MainSynthWindow::togglePane (const string &id)
{
    if (panes_ == NULL)
        return;

    if (!inMode(id, mode_))
    {
        setDesktopMode(modeFor(id));
        panes_->present(id, true);
    }
    else if (panes_->isVisible(id))
        panes_->close(id);
    else
        panes_->present(id, true);

    syncPaneActions();
}

/* Beside thinkrc rather than in it: thinkrc reads 255 bytes a line and
   splits every value at its commas, and a layout is longer than that and
   made of commas. */
string MainSynthWindow::layoutPath (void)
{
    return (std::filesystem::path(Glib::get_user_config_dir()) /
            PACKAGE_NAME / "panes.ini").string();
}

string MainSynthWindow::readLayout (const string &mode)
{
    Glib::RefPtr<Glib::KeyFile> file = Glib::KeyFile::create();

    try
    {
        file->load_from_file(layoutPath());
    }
    catch (const Glib::Error &)
    {
        /* No file: the first layout. */
        return string();
    }

    try
    {
        return file->get_string("layouts", mode);
    }
    catch (const Glib::Error &)
    {
    }

    /* Nothing kept for the mode. The patch mode takes the one layout
       there was before there were modes, if that is what there is. */
    if (mode == PATCH_MODE)
    {
        try
        {
            return file->get_string("layouts", OLD_LAYOUT_KEY);
        }
        catch (const Glib::Error &)
        {
        }
    }

    return string();
}

void MainSynthWindow::onLayoutKept (const string &mode, const string &text)
{
    if (!isMode(mode))
        return;

    keptLayouts_[mode] = text;

    /* After the last change of a burst -- a divider's drag is a change on
       every step of it. */
    layoutWrite_.disconnect();
    layoutWrite_ = Glib::signal_timeout().connect(
        [this] { writeLayout(); return false; }, 500);
}

void MainSynthWindow::writeLayout (void)
{
    if (!layoutWrite_.connected())
        return;

    layoutWrite_.disconnect();

    const string path = layoutPath();
    Glib::RefPtr<Glib::KeyFile> file = Glib::KeyFile::create();

    try
    {
        file->load_from_file(path, Glib::KeyFile::Flags::KEEP_COMMENTS);
    }
    catch (const Glib::Error &)
    {
        /* A first write: nothing to keep. */
    }

    for (std::map<string, string>::const_iterator i = keptLayouts_.begin();
         i != keptLayouts_.end(); ++i)
    {
        if (i->second.empty())
        {
            try
            {
                file->remove_key("layouts", i->first);
            }
            catch (const Glib::Error &)
            {
            }
        }
        else
            file->set_string("layouts", i->first, i->second);
    }

    /* Kept under the modes' own keys now. */
    try
    {
        file->remove_key("layouts", OLD_LAYOUT_KEY);
    }
    catch (const Glib::Error &)
    {
    }

    std::error_code ec;

    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);

    try
    {
        file->save_to_file(path);
    }
    catch (const Glib::Error &e)
    {
        fprintf(stderr, "thinksynth: could not keep the layout in %s: %s\n",
                path.c_str(), e.what());
    }
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
    writeLayout();

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


/* The desktop changed its light/dark setting underneath us. */
void MainSynthWindow::onSystemThemeChanged (void)
{
    /* Re-apply rather than assume: apply() ignores the system scheme unless
       the choice is Auto, so this is a no-op for someone who picked Light or
       Dark explicitly. */
    gthTheme::apply(gthTheme::current());
}

/* A choice from the Appearance menu. */
void MainSynthWindow::menuTheme (const Glib::ustring &target)
{
    setTheme(gthTheme::fromString(target));
}

/* One way in for the menu, the preferences file and the startup path, so the
   action state, the stored value and what the toolkit is actually doing
   cannot drift apart. */
void MainSynthWindow::setTheme (gthThemeChoice choice)
{
    gthTheme::apply(choice);

    if (themeAction_)
        themeAction_->set_state(
            Glib::Variant<Glib::ustring>::create(gthTheme::toString(choice)));

    string **vals = new string *[2];

    vals[0] = new string(gthTheme::toString(choice));
    vals[1] = NULL;

    gthPrefs::instance()->Set("theme", vals);
}

void MainSynthWindow::applyPrefs (void)
{
    gthPrefs *prefs = gthPrefs::instance();

    /* Appearance first: it costs nothing and it means the window is never
       briefly the wrong colour on the way up. */
    {
        string **tvals = prefs->Get("theme");

        setTheme(tvals != NULL && tvals[0] != NULL
                 ? gthTheme::fromString(*(tvals[0]))
                 : gthThemeChoice::Auto);

        /* Auto has to keep following after startup, and the only way to know
           the desktop has changed its mind is to be told. Connected
           unconditionally: which choice is in force can change while the
           program runs, and apply() is what decides whether the system's
           opinion matters. */
        gthTheme::signalSystemSchemeChanged().connect(
            sigc::mem_fun(*this, &MainSynthWindow::onSystemThemeChanged));

        gthTheme::startWatching();
    }

    /* The mode last used. */
    {
        string **mvals = prefs->Get("mode");

        if (mvals != NULL && mvals[0] != NULL)
            setDesktopMode(*(mvals[0]), false);
    }

    /* The directory the DSP browser opens in. This was read in the
       constructor, where the preferences have not been loaded yet, so it
       always fell through to the install path however many times you had
       browsed somewhere else. */
    string **vals = prefs->Get("dspdir");

    if (vals != NULL && vals[0] != NULL)
        prevDir_ = *(vals[0]);

    /* Whether the keys were left hidden, from when that was a preference
       of its own, for a first run with panes: after that the layout says
       where they are, and the preference is not asked again. */
    if (!hadLayout_)
    {
        string **kvals = prefs->Get("keyboard");

        if (kvals != NULL && kvals[0] != NULL && *(kvals[0]) == "0")
            panes_->close("keyboard");
    }

    /* A saved size is only worth honoring if it can be seen. A window
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

    /* The first run's size, cut down to a screen smaller than it. */
    {
        int dw = 0, dh = 0;

        get_default_size(dw, dh);

        if (maxw > 0 && dw > maxw)
            dw = maxw;

        if (maxh > 0 && dh > maxh)
            dh = maxh;

        set_default_size(dw, dh);
    }

    vals = prefs->Get("window");

    if (vals == NULL || vals[0] == NULL || vals[1] == NULL)
        return;

    const int w = atoi(vals[0]->c_str());
    const int h = atoi(vals[1]->c_str());

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
       -- it is what makes "win.pane-keyboard" answer to Ctrl+K from any
       window the application owns -- and the window is built before it has
       been given one, so this waits for onApplicationSet. */
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
 * by the title bar's menu button, with the behaviour attached separately as
 * named actions on the window.
 *
 * The indirection earns its keep: an accelerator binds to an action rather
 * than to a widget, so it works before the menu has ever been opened and
 * keeps working if the item moves. */
void MainSynthWindow::populateMenu (void)
{
    /* The panes, each a tick that says whether it is in view: see
       togglePane. The keys keep the Ctrl+K they were toggled with, the
       patch list and the MIDI map the Ctrl+P and Ctrl+M that opened their
       windows, and the piece the Ctrl+G that opened the Composer's. */
    static const struct
    {
        const char *id;
        const char *label;
        const char *accel;
    } panes[] = {
        { "channelbox", "C_hannels",       NULL },
        { "paramview",  "Patch _Params",   NULL },
        { "nodeview",   "Patch _Graph",    NULL },
        { "keyboard",   "_Keys",           "<Control>k" },
        { "patches",    "Patch _Selector", "<Control>p" },
        { "midimap",    "_MIDI Routing",   "<Control>m" },
        { "composerview", "P_iece",        "<Control>g" },
        { "seqview",    "Seq_uencer",      NULL },
        { "roll",       "Piano _Roll",     NULL },
        { "pieceedit",  "Piece Se_ttings", NULL },
        { "selection",  "S_election",      NULL },
    };

    Glib::RefPtr<Gio::Menu> view = Gio::Menu::create();

    for (size_t i = 0; i < G_N_ELEMENTS(panes); i++)
    {
        const string id = panes[i].id;

        paneActs_[id] = actions_->add_action_bool(
            "pane-" + id,
            sigc::bind(sigc::mem_fun(*this, &MainSynthWindow::togglePane), id),
            false);

        if (panes[i].accel != NULL)
            accels_.push_back(std::make_pair("win.pane-" + id,
                                             Glib::ustring(panes[i].accel)));

        view->append(panes[i].label, "win.pane-" + id);
    }

    /* The panes answer to "panes.reset" themselves, but only from inside:
       this menu hangs off the title bar, which is not. */
    addAction("reset-layout",
              [this] { if (panes_ != NULL) panes_->reset(); });
    addAction("quit",
              sigc::mem_fun(*this, &MainSynthWindow::menuQuit), "<Control>q");
    addAction("about", sigc::mem_fun(*this, &MainSynthWindow::menuAbout));

    /* Appearance. A radio action rather than three separate items, so the
       menu shows which one is in force -- and stateful, so the state is the
       preference rather than something shadowing it. */
    themeAction_ = actions_->add_action_radio_string(
        "theme",
        sigc::mem_fun(*this, &MainSynthWindow::menuTheme),
        gthTheme::toString(gthTheme::current()));

    /* The mode, the same way: the title bar's three toggles and the menu's
       three items are views of one stateful action. */
    modeAction_ = actions_->add_action_radio_string(
        "mode",
        [this] (const Glib::ustring &target)
        {
            setDesktopMode(target.raw());
        },
        PATCH_MODE);

    accels_.push_back(std::make_pair(Glib::ustring("win.mode::patch"),
                                     Glib::ustring("<Control>1")));
    accels_.push_back(std::make_pair(Glib::ustring("win.mode::piece"),
                                     Glib::ustring("<Control>2")));
    accels_.push_back(std::make_pair(Glib::ustring("win.mode::seq"),
                                     Glib::ustring("<Control>3")));

    /* No mnemonics: every letter these names have is taken by an item
       below, and a shared one makes the key cycle between them rather
       than choose. Ctrl+1, Ctrl+2 and Ctrl+3 are the keys for these. */
    Glib::RefPtr<Gio::Menu> modes = Gio::Menu::create();

    modes->append("Patch Mode", "win.mode::patch");
    modes->append("Piece Mode", "win.mode::piece");
    modes->append("Sequence Mode", "win.mode::seq");

    Glib::RefPtr<Gio::Menu> layout = Gio::Menu::create();

    layout->append("Reset La_yout", "win.reset-layout");

    Glib::RefPtr<Gio::Menu> appearance = Gio::Menu::create();

    /* The target is the string the preference file stores, so the action, the
       menu and thinkrc all spell it the same way and there is no third
       mapping to keep in step. */
    appearance->append("_Auto (follow the system)", "win.theme::auto");
    appearance->append("_Light", "win.theme::light");
    appearance->append("_Dark",  "win.theme::dark");

    Glib::RefPtr<Gio::Menu> look = Gio::Menu::create();

    look->append_submenu("Appeara_nce", appearance);

    Glib::RefPtr<Gio::Menu> end = Gio::Menu::create();

    end->append("A_bout", "win.about");
    end->append("_Quit", "win.quit");

    /* One menu behind the button, in sections: a separator is a section
       boundary in a menu model rather than an item of its own. */
    Glib::RefPtr<Gio::Menu> menu = Gio::Menu::create();

    menu->append_section(modes);
    menu->append_section(view);
    menu->append_section(layout);
    menu->append_section(composer_->menu());
    menu->append_section(look);
    menu->append_section(end);

    menuBtn_.set_icon_name("open-menu-symbolic");
    menuBtn_.set_tooltip_text("Menu");
    menuBtn_.set_menu_model(menu);

    /* F10 opens it, as it opened the menu bar this replaces. */
    menuBtn_.set_primary(true);
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

/* Channels is a narrow pane, so its width is the window's to spare.
 *
 * Left-aligned because a column of centered labels of different lengths has
 * no edge to read down. Ellipsized at the end so one long name cannot widen
 * the pane and take that space from the patch; the full path is on the
 * tooltip either way. */
Gtk::Widget *MainSynthWindow::makeChannelLabel (const string &text,
                                                const string &tip)
{
    Gtk::Label *lbl = manage(new Gtk::Label(text));

    lbl->set_xalign(0.0);
    lbl->set_ellipsize(Pango::EllipsizeMode::END);
    lbl->set_margin(4);

    /* Both widths, and the minimum is the one that matters: an ellipsizing
       label reports "..." as its minimum size, so with only a maximum set
       the list shrank every row to three dots. width_chars reserves the
       room; max_width_chars stops one long name widening the pane. */
    lbl->set_width_chars(16);
    lbl->set_max_width_chars(22);

    if (!tip.empty())
        lbl->set_tooltip_text(tip);

    return lbl;
}

void MainSynthWindow::appendChannel (const string &tabName, const string &tip,
                                     int num, bool is_real)
{
    const string name = "ch" + std::to_string(num);

    if (is_real)
    {
        chanList_.append(*makeChannelLabel(tabName, tip));
        rowChans_.push_back(num);
    }

    /* The graph's holder, filled when it is first looked at. */
    Gtk::Box *holder = manage(new Gtk::Box(Gtk::Orientation::VERTICAL));

    holders_.push_back(holder);
    nodeStack_.add(*holder, name);

    if (is_real == false)
    {
        Gtk::Label *lbl = manage(new Gtk::Label("Please select a DSP file to associate with this patch."));
        lbl->set_justify(Gtk::Justification::CENTER);
        lbl->set_wrap(true);
        paramStack_.add(*lbl, name);

        Gtk::Label *none = manage(new Gtk::Label("No patch on this channel."));

        none->add_css_class("dim-label");
        none->set_vexpand(true);
        holder->append(*none);
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
        sorry->set_wrap(true);
        paramStack_.add(*sorry, name);
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
        lname_lbl->set_yalign(0.0);
        rname_lbl->set_xalign(0.0);

        /* Wrapped: a pane is as narrow as somebody drags it, and one line
           of description was the widest thing on the page. */
        rname_lbl->set_wrap(true);
        rname_lbl->set_hexpand(true);
        
        info_table->attach(*lname_lbl, 0, 2, 1, 1);
        info_table->attach(*rname_lbl, 1, 2, 1, 1);
    }

    Gtk::Frame *dsp_frame = manage(new Gtk::Frame);
    ArgPanelView *dsp_table = manage(new ArgPanelView);

    dsp_frame->set_label("DSP Parameters");
    dsp_table->set_margin_start(6);
    dsp_table->set_margin_end(6);
    dsp_table->set_margin_top(6);
    dsp_table->set_margin_bottom(6);
    dsp_frame->set_child(*dsp_table);
        
    tab_vbox->append(*info_frame);
    dsp_frame->set_vexpand(true);
    tab_vbox->append(*dsp_frame);
    tab_vbox->append(*makeEffectFrame(num));

    /* Which parameters there are, what each is worth in the unit it was
       written in and which node drives it -- all of that is ArgPanel's now,
       so this says only which channel and which of its parameters not to
       draw. The rest of it used to be forty lines here and a second forty in
       the browser; src/PanelModel.h says why there are none. */
    dsp_table->setChannel(num);

    /* The channel amplitude is a slider like the rest, and it is drawn once
       already -- pinned to the patch bar above, where it is in the same
       place on every page. Twice would be two controls for one value. */
    dsp_table->exclude("amp");

    dsp_table->rebuild();

    /* The patch bar sits above the parameters. Which patch this is, how
       loud it is and whether it has been saved are facts about the patch;
       they should not scroll off the top of the parameter panel. The graph
       they came from is a pane of its own, beside this one. */
    Gtk::Box *page = manage(new Gtk::Box(Gtk::Orientation::VERTICAL));

    page->append(*makePatchBar(num));
    page->append(*manage(new Gtk::Separator(Gtk::Orientation::HORIZONTAL)));
    tab_view->set_vexpand(true);
    page->append(*tab_view);

    paramStack_.add(*page, name);
}

/* The channel effect's block. See the header.
 *
 * Drawn on every page whether or not the channel has one, and empty when it
 * does not, because the way to *get* one is the button in it. A block that
 * appeared only once an effect was loaded would be a control you could not
 * reach until you had used it.
 */
Gtk::Widget *MainSynthWindow::makeEffectFrame (int chan)
{
    gthPatchManager *patchMgr = gthPatchManager::instance();
    gthPatchManager::PatchFile *patch = patchMgr->getPatch(chan);

    Gtk::Frame *frame = manage(new Gtk::Frame);
    Gtk::Box *body = manage(new Gtk::Box(Gtk::Orientation::VERTICAL));

    frame->set_label("Channel Effect");
    frame->set_child(*body);

    body->set_spacing(6);
    body->set_margin_start(6);
    body->set_margin_end(6);
    body->set_margin_top(6);
    body->set_margin_bottom(6);

    const string name = patch ? patch->doc.effect : string();

    Gtk::Box *bar = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL));

    bar->set_spacing(6);

    Gtk::Label *which = manage(new Gtk::Label(
        name.empty() ? "None \xe2\x80\x94 the channel's voices go out as they "
                       "are" : name));

    which->set_xalign(0.0);
    which->set_hexpand(true);

    /* Dimmed rather than absent when there is none: "None" is a state the
       channel is in, and a label that disappeared would leave the two buttons
       floating with nothing saying what they act on. */
    if (name.empty())
        which->set_sensitive(false);

    bar->append(*which);

    Gtk::Button *choose = manage(new Gtk::Button(name.empty() ? "_Choose\xe2\x80\xa6"
                                                              : "_Replace\xe2\x80\xa6"));

    choose->set_use_underline(true);
    choose->signal_clicked().connect(
        sigc::bind(sigc::mem_fun(*this, &MainSynthWindow::onEffectBrowse),
                   chan));
    bar->append(*choose);

    Gtk::Button *none = manage(new Gtk::Button("_None"));

    none->set_use_underline(true);
    none->set_sensitive(!name.empty());
    /* Out of the click, as a save is: taking the effect off rebuilds every
       page, this button's with it, while its handler would still be on the
       stack. */
    none->signal_clicked().connect(
        [this, chan]
        {
            Glib::signal_idle().connect_once(
                sigc::bind(sigc::mem_fun(*this,
                                         &MainSynthWindow::onEffectRemove),
                           chan));
        });
    bar->append(*none);

    /* Nothing to load one onto. An effect belongs to a channel and the
       channel is the patch, so the buttons say so rather than failing when
       pressed. */
    if (patch == NULL)
    {
        choose->set_sensitive(false);
        none->set_sensitive(false);
        which->set_text("Load a DSP on this channel first");
    }

    body->append(*bar);

    /* The second parameter panel: the effect's own chanargs, which are a
       second map -- see thSynth::getChanArg and TH_EFFECT_PREFIX. Same
       widget as the one above it, differing by the prefix its lookups
       carry. */
    ArgPanelView *table = new ArgPanelView;

    table->setChannel(chan);
    table->setPrefix(TH_EFFECT_PREFIX);

    /* Built before it is managed, because an effect with no parameters gets
       no panel and no separator above one -- and a managed widget nothing
       ever takes is a widget nothing ever frees. */
    if (table->rebuild())
    {
        body->append(*manage(new Gtk::Separator(
                                 Gtk::Orientation::HORIZONTAL)));
        body->append(*manage(table));
    }
    else
        delete table;

    return frame;
}

/* Everything a change of effect has to do to this window, beyond what the
 * patch manager's patches_changed has already done: every page is rebuilt
 * there, because a page holds widgets bound to args on a channel and the
 * channel's second arg map has just been replaced. What is left is which
 * channel is looked at. */
void MainSynthWindow::reloadPages (int chan)
{
    if (chan >= 0)
        selectChannel(chan);
}

/* The effect chooser, which is the instrument browser with the other half of
 * the corpus in it.
 *
 * This used to open the same file chooser at the same directory, so nothing
 * stopped an instrument being picked and the only thing that said so was the
 * dialog below, afterwards. The browser offers effect graphs and no others,
 * which is the distinction being made where the choice is made. */
void MainSynthWindow::onEffectBrowse (int chan)
{
    openDspBrowser(true, chan);
}

void MainSynthWindow::onEffectChosen (string picked, int chan)
{
    if (picked.empty())
        return;

    if (!gthPatchManager::instance()->setEffect(chan, picked))
    {
        /* The two ways it can go, and the message says both rather than
           guessing which: a graph that will not parse and a graph that parses
           and is an instrument are different mistakes with the same symptom
           here. */
        showError(this, "Could not load the effect",
                  picked + "\n\nA syntax error, or it is not an effect graph "
                  "-- an effect's io node declares in0, which is where the "
                  "engine puts the channel's audio.");
        return;
    }

    reloadPages(chan);
}

void MainSynthWindow::onEffectRemove (int chan)
{
    if (!gthPatchManager::instance()->setEffect(chan, ""))
        return;

    reloadPages(chan);
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

    /* Channels carries this too, but ellipsized to sixteen characters in a
       narrow pane -- so it is often the end of the name that is missing, and
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
    else if (patch->doc.dsp.length() > 0)
    {
        /* A starting point rather than a guess at what it should be called:
           the DSP's own name with the patch extension, which is at least in
           the right family. */
        string suggest = thUtil::basename(patch->doc.dsp.c_str());
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
 * removing every channel's pages and building them again -- so the button whose
 * handler is on the stack, and the page holding it, would be destroyed
 * underneath an emission that is still running. Writing from an idle callback
 * means the click has returned first and nothing is left pointing into the
 * page. */
void MainSynthWindow::doSavePatch (string file, int chan)
{
    if (!gthPatchManager::instance()->savePatch(file, chan))
        showError(this, "Could not write " + file);
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

/* The channel's graph is in view for the first time: build its editor.
 *
 * Anything already built is left alone -- this is asked on every change of
 * channel and every time the graph comes into view, not only the first. */
void MainSynthWindow::ensureEditor (int chan)
{
    /* Not while the pages are being taken away, or the window is: at
       shutdown, when the window is destroyed after the synth, it was a NULL
       thSynth::instance() handed to a node editor that then parsed with
       it. */
    if (tearingDown_ || chan < 0 || chan >= (int)holders_.size() ||
        editors_.count(chan))
        return;

    gthPatchManager::PatchFile *patch =
        gthPatchManager::instance()->getPatch(chan);

    /* An empty channel's holder says so already. */
    if (patch == NULL)
        return;

    const string dspFile = patch->doc.dsp;

    NodeEditor *ed = manage(new NodeEditor(thSynth::instance()));

    editors_[chan] = ed;

    ed->set_vexpand(true);
    holders_[chan]->append(*ed);

    if (dspFile.empty())
    {
        ed->setStatusPublic("This patch has no DSP file yet. Choose one in "
                            "Patch params, or use New to start one.");
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
            /* Nothing on this channel at all: its pages, and no row. A
               list of sixteen with twelve of them "(empty)" said less than
               a list of the four there are, and Add channel... is where
               another comes from. */
            appendChannel("", "", i, false);
            continue;
        }

        if (patch->filename.length() > 0)
        {
            /* The basename, not the path.
            
               A row read `3: /usr/local/share/thinksynth/dsp/old/analog03.dsp'
               -- almost all of it identical to every other row, and the part
               that identifies it last, which is the part a narrow pane cuts
               off. The full path is still worth having, so it moves to the
               tooltip. */
            tabName = chanStr.str() +
                      thUtil::basename(patch->filename.c_str());

            /* And without ".patch": every row has it, so it says nothing,
               and it is what the pane's width cut to "FunkMachine.p...". */
            const string ext = ".patch";

            if (tabName.size() > ext.size() &&
                tabName.compare(tabName.size() - ext.size(), ext.size(),
                                ext) == 0)
                tabName.erase(tabName.size() - ext.size());
        }
        else if (!patch->doc.dsp.empty())
        {
            /* A DSP is loaded; no patch file has been saved for it -- which
               is every instrument a piece carries. The graph's name, with
               its ".dsp" left on, which is what tells it from a saved
               patch: a piece's channels all read "(Untitled)" before, so
               nothing said which was the kick and which the pad. */
            tabName = chanStr.str() +
                      thUtil::basename(patch->doc.dsp.c_str());
            appendChannel(tabName, patch->doc.dsp, i, true);
            continue;
        }
        else
        {
            /* Neither a patch file nor a graph's name to show. */
            tabName = chanStr.str() + "(Untitled)";
        }

        appendChannel(tabName, patch->filename, i, true);
    }
}


/* Empties Channels and both stacks, to be filled again. */
void MainSynthWindow::clearPages (void)
{
    tearingDown_ = true;

    /* Not while a row is being taken away: a list losing its selected row
       selects nothing, and nothing is not a channel. */
    selecting_ = true;

    while (Gtk::Widget *w = chanList_.get_first_child())
        chanList_.remove(*w);

    rowChans_.clear();
    selecting_ = false;

    while (Gtk::Widget *w = paramStack_.get_first_child())
        paramStack_.remove(*w);

    while (Gtk::Widget *w = nodeStack_.get_first_child())
        nodeStack_.remove(*w);

    holders_.clear();
    editors_.clear();

    tearingDown_ = false;
}

void MainSynthWindow::onPatchesChanged (void)
{
    clearPages();

    populate();

    selectChannel(chan_);
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
 * is both simpler and cheaper than rebuilding it.
 *
 * They are deleted in the destructor now, which is also where they have to be:
 * every one of them points into the synth. */
/* Ctrl+W closes a secondary window.
 *
 * A key controller rather than an application accelerator, which is how
 * Ctrl+K and the rest are done. Those work because the main window is added
 * to the application and "win.pane-keyboard" resolves against it; these
 * windows are deliberately not added -- the application quits when the last of its
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

/* A window for panes moved out of the main one, by a tab's menu or a tab
 * dragged out of it.
 *
 * What is in a pane asks the window for "win." and "composer." -- the
 * piece's Save, the pane toggles -- so this one has the same two groups,
 * and the main window's keys as shortcuts of its own: an accelerator is
 * the application's, and reaches a window only through its action groups.
 * Not added to the application, as no secondary window is (see
 * addCloseAccel), and gone with the main window. Closing it puts its
 * panes back.
 */
GtkWindow *MainSynthWindow::makePaneWindow (MlnPanes *panes, gpointer data)
{
    MainSynthWindow *self = static_cast<MainSynthWindow *>(data);
    GtkWidget *win = gtk_window_new();
    GtkEventController *keys = gtk_shortcut_controller_new();

    (void)panes;

    gtk_window_set_transient_for(GTK_WINDOW(win), self->gobj());
    gtk_window_set_destroy_with_parent(GTK_WINDOW(win), TRUE);
    gtk_widget_insert_action_group(win, "win",
                                   G_ACTION_GROUP(self->actions_->gobj()));
    gtk_widget_insert_action_group(win, "composer",
                                   G_ACTION_GROUP(self->composer_->actions()->gobj()));

    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(keys),
                                      GTK_SHORTCUT_SCOPE_GLOBAL);

    for (size_t i = 0; i < self->accels_.size(); i++)
    {
        GtkShortcutTrigger *trigger =
            gtk_shortcut_trigger_parse_string(self->accels_[i].second.c_str());

        if (trigger == NULL)
            continue;

        gtk_shortcut_controller_add_shortcut(
            GTK_SHORTCUT_CONTROLLER(keys),
            gtk_shortcut_new(trigger,
                             gtk_named_action_new(self->accels_[i].first.c_str())));
    }

    gtk_widget_add_controller(win, keys);

    return GTK_WINDOW(win);
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

/* 100 on the slider is unity gain, so the number reads like a channel
   amplitude rather than a multiplier. setMasterGain clamps and stores
   atomically; the audio thread reads it every block. */
void MainSynthWindow::onMasterGain (void)
{
    thSynth::instance()->setMasterGain(masterScale_.get_value() / 100.0);
}

/* The channel the panes are about.
 *
 * Channels picks it, and a reload, a load or an effect keeps it; Patch
 * params and Patch graph show its pages, and the keys play it. */
void MainSynthWindow::selectChannel (int chan)
{
    if (chan < 0 || chan >= (int)holders_.size())
        chan = 0;

    chan_ = chan;

    const string name = "ch" + std::to_string(chan);

    if (paramStack_.get_child_by_name(name) != NULL)
        paramStack_.set_visible_child(name);

    if (nodeStack_.get_child_by_name(name) != NULL)
        nodeStack_.set_visible_child(name);

    /* Its row, if it has one: a channel with nothing on it has none, and
       the list then has nothing selected rather than the wrong one. */
    {
        Gtk::ListBoxRow *row = NULL;

        for (size_t i = 0; i < rowChans_.size(); i++)
            if (rowChans_[i] == chan)
                row = chanList_.get_row_at_index((int)i);

        selecting_ = true;

        if (row != NULL)
            chanList_.select_row(*row);
        else
            chanList_.unselect_all();

        selecting_ = false;
    }

    addChanBtn_.set_sensitive(firstFreeChannel() >= 0);

    /* And the keys play the channel being looked at. */
    if (kbPanel_ != NULL)
        kbPanel_->setChannel(chan);

    if (panes_ != NULL && panes_->isVisible("nodeview"))
        ensureEditor(chan);

    gthPatchManager *patchMgr = gthPatchManager::instance();
    gthPatchManager::PatchFile *patch = patchMgr->getPatch(chan);

    /* The entry is the current channel's DSP, and sits over all sixteen
       pages; the label says which one it is. */
    dspEntryLbl_.set_label("Channel " + std::to_string(chan + 1) + " DSP:");

    if (patch == NULL)
    {
        dspEntry_.set_text("");
        return;
    }

    dspEntry_.set_text(patch->doc.dsp);
}

void MainSynthWindow::onChannelRow (Gtk::ListBoxRow *row)
{
    if (selecting_ || row == NULL)
        return;

    const int idx = row->get_index();

    if (idx >= 0 && idx < (int)rowChans_.size())
        selectChannel(rowChans_[idx]);
}

int MainSynthWindow::firstFreeChannel (void)
{
    gthPatchManager *patchMgr = gthPatchManager::instance();

    for (int i = 0; i < patchMgr->numPatches(); i++)
        if (patchMgr->getPatch(i) == NULL)
            return i;

    return -1;
}

void MainSynthWindow::onAddChannel (void)
{
    const int chan = firstFreeChannel();

    if (chan >= 0)
        openDspBrowser(false, chan);
}

void MainSynthWindow::onDspEntryActivate (void)
{
    gthPatchManager *patchMgr = gthPatchManager::instance();
    string dspfile = dspEntry_.get_text();
    int pagenum = chan_;

    /* noop caused by a spurious Enter */
    if (dspfile == "")
        return;
    
    if (patchMgr->newPatch(dspfile, pagenum) == false)
    {
        showError(this, "Could not load the DSP",
                  dspfile + "\n\nA syntax error, or it does not exist.");

        return;
    }

    /* The pages are rebuilt already: loading says the patches changed. */
    selectChannel(pagenum);
}

/* The instrument chooser: a browser over the catalog rather than a file
 * chooser over a directory.
 *
 * What the user picked from before was sixty-one filenames with no filter and
 * no descriptions -- and the titles and descriptions have been written into
 * every one of those files for years. See src/gui/DspBrowser.h.
 */
/* The rows a graph chooser shows, given what is in its filter box.
 *
 * The rule is DspCatalog's -- an effect is not offered in the instrument
 * dialog or the other way round, and the filter reads the title, the
 * description and the filename -- so that what a chooser offers can be held
 * still by a harness with no display (scripts/dspcatalog). This turns that
 * answer into rows. */
static vector<BrowserGroup> dspRows (DspCatalog *catalog, bool effects,
                                     const std::string &needle)
{
    vector<BrowserGroup> out;

    for (size_t g = 0; g < catalog->groups().size(); g++)
    {
        const string &group = catalog->groups()[g];
        const vector<DspCatalog::Entry> &list = catalog->inGroup(group);

        BrowserGroup rows;

        rows.name = group;

        for (size_t i = 0; i < list.size(); i++)
        {
            if (!DspCatalog::matches(list[i], effects, needle))
                continue;

            BrowserItem item;

            item.file = list[i].file;
            item.name = list[i].name;
            item.desc = list[i].desc;

            /* The filename, because that is what a .patch's `dsp' line will
               say and what a piece names in its `dsp' clause -- the one thing
               the row does not show and the one worth knowing about a file
               you are about to put in a document. */
            item.note = "<tt>" + Glib::Markup::escape_text(list[i].file) +
                        "</tt>";

            if (!list[i].author.empty())
                item.note += "  <small>" +
                             Glib::Markup::escape_text(list[i].author) +
                             "</small>";

            rows.items.push_back(item);
        }

        if (!rows.items.empty())
            out.push_back(rows);
    }

    return out;
}

/* The instrument chooser and the effect chooser, which are one browser over
 * the two halves of one corpus.
 *
 * The catalog outlives the dialog by being owned by it -- a Glib::RefPtr
 * would be the toolkit's way and this is a plain object, so it is held in a
 * shared_ptr the provider slot captures and released with the slot. */
void MainSynthWindow::openDspBrowser (bool effects, int chan)
{
    gthPatchManager::PatchFile *patch =
        gthPatchManager::instance()->getPatch(chan);

    std::shared_ptr<DspCatalog> catalog = std::make_shared<DspCatalog>();

    catalog->scan(dspDir_);

    const string current = patch == NULL ? string()
                         : effects ? patch->doc.effect : patch->doc.dsp;

    ItemBrowser *browser = new ItemBrowser(
        *this,
        effects ? "thinksynth - Channel Effect" : "thinksynth - Instrument",
        [catalog, effects](const std::string &needle)
        {
            return dspRows(catalog.get(), effects, needle);
        },
        prevDir_.empty() ? dspDir_ : prevDir_, current);

    browser->setEmptyNote("<i>No graphs in</i>\n<tt>" +
                          Glib::Markup::escape_text(dspDir_) + "</tt>\n"
                          "<small>Set THINK_DSP_PATH, or use Other "
                          "File...</small>");

    if (effects)
        browser->signal_chosen().connect(
            sigc::bind(sigc::mem_fun(*this,
                                     &MainSynthWindow::onEffectChosen), chan));
    else
        browser->signal_chosen().connect(
            sigc::bind(sigc::mem_fun(*this,
                                     &MainSynthWindow::onBrowseChosen), chan));

    browser->present();
}

DspCatalog *MainSynthWindow::dspCatalog (void)
{
    if (!catalog_)
    {
        catalog_ = std::make_shared<DspCatalog>();
        catalog_->scan(dspDir_);
    }

    return catalog_.get();
}

string MainSynthWindow::dspTitle (const string &dsp)
{
    const DspCatalog::Entry *e = dspCatalog()->find(dsp);

    if (e != NULL && !e->name.empty())
        return e->name;

    return thUtil::basename(dsp.c_str());
}

/* The instrument browser, as Patch params' Browse opens it, over the graph
 * the track plays now -- and the answer is an edit to the sequence rather
 * than a patch on a channel: the instrument is the piece's, so the file is
 * where it changes, and a channel loaded behind the piece's back would be
 * taken to be somebody else's and moved off at the next reload.
 */
void MainSynthWindow::onChooseTrack (size_t chain, size_t stage, string dsp)
{
    /* Read afresh, as Browse's is, and kept for the titles after. */
    catalog_.reset();
    dspCatalog();

    std::shared_ptr<DspCatalog> catalog = catalog_;

    ItemBrowser *browser = new ItemBrowser(
        *this, "thinksynth - Instrument",
        [catalog](const std::string &needle)
        {
            return dspRows(catalog.get(), false, needle);
        },
        prevDir_.empty() ? dspDir_ : prevDir_, dsp);

    browser->setEmptyNote("<i>No graphs in</i>\n<tt>" +
                          Glib::Markup::escape_text(dspDir_) + "</tt>\n"
                          "<small>Set THINK_DSP_PATH, or use Other "
                          "File...</small>");

    browser->signal_chosen().connect(
        [this, catalog, chain, stage] (string picked)
        {
            if (picked.empty() || composer_ == NULL)
                return;

            /* A graph the catalog has no row for -- one from Other
               File... -- counts as pitched: a ladder that turns out to
               do nothing is a smaller surprise than a pattern silently
               cut to one row. */
            const DspCatalog::Entry *e = catalog->find(picked);

            composer_->setTrackInstrument(chain, stage, picked,
                                          e == NULL || e->readsNote);
        });

    browser->present();
}

void MainSynthWindow::onBrowseButton (void)
{
    /* The channel is captured now rather than read in the handler: the
       browser is not modal to the window, and the channel that was current
       when Browse was clicked is the one this is loading onto. */
    openDspBrowser(false, chan_);
}

void MainSynthWindow::queueSavePatch (string file, int chan)
{
    Glib::signal_idle().connect_once(
        sigc::bind(
            sigc::mem_fun(*this, &MainSynthWindow::doSavePatch), file, chan));
}

/* `picked' is the name the file is named by -- `ts1.dsp' -- or, from the
 * browser's Other File..., an absolute path. newPatch resolves either and
 * keeps the name as given, so a patch saved afterwards carries the short one.
 */
void MainSynthWindow::onBrowseChosen (string picked, int pagenum)
{
    if (picked.empty())
        return;

    dspEntry_.set_text(picked);

    if (!gthPatchManager::instance()->newPatch(picked, pagenum))
    {
        showError(this, "Could not load the DSP",
                  picked + "\n\nA syntax error, or it does not exist.");
        return;
    }

    /* Only a path moves the chooser's folder. A name out of the catalog is
       not one: it is resolved against the shipped tree rather than read from
       a directory the user picked, so there is nothing there to remember. */
    if (std::filesystem::path(picked).is_absolute())
    {
        prevDir_ = thUtil::dirname(picked.c_str());
        prevDir_ += "/";

        string **vals = new string *[2];

        vals[0] = new string(prevDir_);
        vals[1] = NULL;

        gthPrefs::instance()->Set("dspdir", vals);
    }

    /* The pages are rebuilt already: loading says the patches changed. */
    selectChannel(pagenum);
}
