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

/*
 * windowcheck -- the main window, tiled.
 *
 *   windowcheck -p PLUGIN_DIR DSP_FILE
 *
 * The window is the channels, a patch's parameters and its graph, the keys,
 * the patch list, the MIDI routing and the piece's four, as panes
 * (src/gui/Panes.h), in two modes: patch and piece. This builds it the way
 * main() does, with a patch on a channel, and asks what a person would
 * see: which panes are up the first time, which channel they are about,
 * that each mode has its own panes and its own layout, that the View
 * menu's ticks put panes up and take them down, that the layouts are kept
 * in panes.ini and read back, and that the window comes down cleanly with
 * an editor built in it.
 *
 * Needs a display, and skips itself without one, as editorcheck does.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtkmm.h>

#include "think.h"

#include "thUtil.h"
#include "gthPatchfile.h"
#include "gthPrefs.h"
#include "gthSignal.h"
#include "gui/Composer.h"
#include "gui/MainSynthWindow.h"
#include "gui/Panes.h"

/* The application-wide signals gthSignal.h declares, defined here because
 * main.cpp defines them there and this harness is not main.cpp. */
sigNoteOn    m_sigNoteOn;
sigNoteOff   m_sigNoteOff;
sigNoteClear m_sigNoteClear;
sigNoteOn    m_sigKbdNoteOn;
sigNoteOff   m_sigKbdNoteOff;

/* A subclass, for the reason composercheck has one: what the window keeps
 * is its own business, and there is no call to widen it for a test. */
class TestWindow : public MainSynthWindow {
public:
    TestWindow (void) : MainSynthWindow(NULL) { }

    using MainSynthWindow::panes_;
    using MainSynthWindow::chanList_;
    using MainSynthWindow::editors_;
    using MainSynthWindow::chan_;
    using MainSynthWindow::actions_;
    using MainSynthWindow::dspEntryLbl_;
    using MainSynthWindow::composer_;
    using MainSynthWindow::dspEntry_;
    using MainSynthWindow::onDspEntryActivate;
    using MainSynthWindow::selectChannel;
    using MainSynthWindow::writeLayout;
    using MainSynthWindow::layoutPath;
    using MainSynthWindow::setDesktopMode;
    using MainSynthWindow::mode_;
    using MainSynthWindow::rowChans_;
    using MainSynthWindow::addChanBtn_;
    using MainSynthWindow::firstFreeChannel;
    using MainSynthWindow::onBrowseChosen;
};

static int checks = 0;
static int failures = 0;

static void
ok (const char *what)
{
    printf("ok    %s\n", what);
    checks++;
}

static void
fail (const char *what)
{
    printf("FAIL  %s\n", what);
    checks++;
    failures++;
}

static void
check (bool cond, const char *what)
{
    if (cond)
        ok(what);
    else
        fail(what);
}

/* Bounded rather than "until idle", as in composercheck: a window with a
   frame clock running is never idle. */
static void
pump (int rounds)
{
    Glib::RefPtr<Glib::MainContext> ctx = Glib::MainContext::get_default();

    for (int i = 0; i < rounds; i++)
        while (ctx->pending())
            ctx->iteration(false);
}

/* How many widgets are under `w', to tell a pane that has been built from
   one that has not. */
static int
descendants (Gtk::Widget *w)
{
    int n = 0;

    for (Gtk::Widget *c = w->get_first_child(); c != NULL;
         c = c->get_next_sibling())
        n += 1 + descendants(c);

    return n;
}

static bool
isClosed (TestWindow *win, const std::string &id)
{
    const std::vector<std::string> closed = win->panes_->closed();

    return std::find(closed.begin(), closed.end(), id) != closed.end();
}

static bool
ticked (TestWindow *win, const std::string &id)
{
    Glib::RefPtr<Gio::Action> act = win->actions_->lookup_action("pane-" + id);
    bool on = false;

    if (!act)
        return false;

    act->get_state(on);

    return on;
}

static void
activate (TestWindow *win, const std::string &name)
{
    win->actions_->activate_action(name);
    pump(4);
}

/* The title bar's toggles and the menu's two items, which are one
   action with the mode as its target. */
static void
pickMode (TestWindow *win, const char *mode)
{
    win->actions_->activate_action("mode",
                                   Glib::Variant<Glib::ustring>::create(mode));
    pump(4);
}

/* The row Channels has for `chan', or NULL: only a channel with something
   on it has one. */
static Gtk::ListBoxRow *
rowFor (TestWindow *win, int chan)
{
    for (size_t i = 0; i < win->rowChans_.size(); i++)
        if (win->rowChans_[i] == chan)
            return win->chanList_.get_row_at_index((int)i);

    return NULL;
}

static std::string
readAll (const std::string &path)
{
    std::ifstream in(path.c_str());

    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

static int
run (const std::string &pluginPath, const std::string &dsp)
{
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    gthPatchManager *patchMgr = gthPatchManager::instance();

    /* A patch on the first channel and on the third, before the window,
       as a restored session has them. */
    if (!patchMgr->newPatch(dsp, 0) || !patchMgr->newPatch(dsp, 2))
    {
        fail("could not put the patch under test on a channel");
        return failures;
    }

    TestWindow *win = new TestWindow;

    win->set_visible(true);
    pump(8);

    ok("the window builds and shows");

    /* ---- the first layout ---- */

    {
        const std::vector<std::string> closed = win->panes_->closed();

        check(closed.size() == 2 &&
              std::find(closed.begin(), closed.end(), "patches") !=
                  closed.end() &&
              std::find(closed.begin(), closed.end(), "midimap") !=
                  closed.end(),
              "the first layout has every pane up but the patch list and "
              "the MIDI routing");
    }

    check(win->panes_->isVisible("channelbox") &&
          win->panes_->isVisible("nodeview") &&
          win->panes_->isVisible("paramview") &&
          win->panes_->isVisible("keyboard"),
          "...and the channels, the graph, the parameters and the keys in "
          "view");

    check(win->mode_ == "patch" &&
          !win->panes_->isVisible("composerview") &&
          !win->panes_->isVisible("roll") &&
          !win->panes_->isVisible("selection") &&
          !win->panes_->isVisible("pieceedit") &&
          !isClosed(win, "composerview") && !isClosed(win, "roll"),
          "...in patch mode, where the piece's panes are neither up nor "
          "in the drawer");

    check(ticked(win, "keyboard") && !ticked(win, "composerview") &&
          !ticked(win, "patches"),
          "the menu ticks what is in view, and not what is behind a tab");

    /* Nothing loads a piece until it is looked at: a piece's instruments
       go onto channels. */
    check(!win->composer_->started() &&
          !win->composer_->transport().get_visible(),
          "the composer waits, and its transport with it, until the piece "
          "is looked at");

    /* Every pane's minimum, added across the widest row: the narrowest the
       window can be. A laptop's 1280 pixels have to hold it. */
    {
        int min = 0, nat = 0, a = 0, b = 0;

        win->panes_->widget().measure(Gtk::Orientation::HORIZONTAL, -1,
                                      min, nat, a, b);
        check(min > 0 && min <= 1200,
              "the first layout fits a 1280-pixel screen");
    }

    /* ---- the channel ---- */

    {
        int rows = 0;

        while (win->chanList_.get_row_at_index(rows) != NULL)
            rows++;

        check(rows == 2 && win->rowChans_.size() == 2 &&
              win->rowChans_[0] == 0 && win->rowChans_[1] == 2,
              "Channels has a row for each channel with a patch on it, and "
              "none for the empty ones");
    }

    check(win->chan_ == 0 && win->editors_.count(0) == 1,
          "the first channel is picked, and its graph built as it came "
          "into view");

    check(win->editors_.size() == 1,
          "...and only its: a graph nobody has looked at is not built");

    win->chanList_.select_row(*rowFor(win, 2));
    pump(4);

    check(win->chan_ == 2, "picking a row in Channels picks the channel");
    check(win->dspEntryLbl_.get_label() == "Channel 3 DSP:",
          "...and Patch params says which");
    check(win->editors_.count(2) == 1,
          "...and its graph is built, since the graph is in view");

    /* A reload rebuilds every page; the channel has to survive it. */
    patchMgr->newPatch(dsp, 5);
    pump(4);

    check(win->chan_ == 2, "loading a patch elsewhere keeps the channel");

    /* A graph with no patch file saved for it -- as every instrument a
       piece carries is -- goes by the graph's name. */
    {
        Gtk::ListBoxRow *row = rowFor(win, 5);
        Gtk::Label *lbl = row != NULL
            ? dynamic_cast<Gtk::Label *>(row->get_child()) : NULL;
        const std::string want =
            "6: " + std::filesystem::path(dsp).filename().string();

        check(lbl != NULL && std::string(lbl->get_text()) == want,
              "a channel with a graph and no patch file is named for the "
              "graph");
    }

    /* Add channel...: the lowest channel with nothing on it, and a row
       for it once something is. */
    check(win->firstFreeChannel() == 1 && win->addChanBtn_.get_sensitive(),
          "Add channel... is offered, for the lowest empty channel");

    win->onBrowseChosen(dsp, win->firstFreeChannel());
    pump(4);

    check(rowFor(win, 1) != NULL && win->chan_ == 1 &&
          win->chanList_.get_selected_row() == rowFor(win, 1) &&
          win->rowChans_.size() == 4 && win->rowChans_[1] == 1,
          "...and what it loads gets a row, in channel order, and is "
          "picked");

    win->selectChannel(2);
    pump(4);


    {
        Gtk::ListBoxRow *row = win->chanList_.get_selected_row();

        check(row != NULL && row == rowFor(win, 2),
              "...and its row selected");
    }

    /* And one loaded on this channel through the DSP entry: the pages are
       rebuilt under the entry, and the channel stays. */
    win->dspEntry_.set_text(dsp);
    win->onDspEntryActivate();
    pump(4);

    check(win->chan_ == 2 && win->editors_.count(2) == 1,
          "the DSP entry reloads the channel and keeps it, graph and all");

    /* ---- the piece ---- */

    /* A menu command for the piece before anything has shown it: piece
       mode comes up with the canvas, the composer starts, and then the
       command acts -- New was undone by the load that followed when it ran
       first. */
    win->activate_action("composer.new");
    pump(8);

    check(win->mode_ == "piece" && win->composer_->started() &&
          win->panes_->isVisible("composerview") &&
          win->composer_->transport().get_visible(),
          "New Piece in patch mode goes to piece mode, brings the canvas "
          "up, starts the composer and puts its transport up");

    check(win->composer_->status().get_text().find("Untitled") !=
              Glib::ustring::npos,
          "...and the new piece is what is left, not the one loaded after");

    {
        const std::vector<std::string> closed = win->panes_->closed();

        check(win->panes_->isVisible("pieceedit") &&
              win->panes_->isVisible("roll") &&
              isClosed(win, "channelbox") && isClosed(win, "nodeview") &&
              isClosed(win, "paramview") && isClosed(win, "patches") &&
              isClosed(win, "midimap") && closed.size() == 5,
              "piece mode's first layout has the canvas, the settings and "
              "the roll up, and the patch's panes in the drawer");
    }

    check(!win->panes_->isVisible("keyboard") && !isClosed(win, "keyboard"),
          "...and the keys a tab behind the roll");

    check(!win->panes_->isVisible("seqview") && !isClosed(win, "seqview"),
          "...and the sequencer a tab behind the canvas");

    activate(win, "pane-selection");

    check(win->panes_->isVisible("selection") &&
          win->panes_->isVisible("composerview"),
          "the selection comes up beside the canvas");

    check(descendants(&win->composer_->selectionView()) > 3 &&
          descendants(&win->composer_->settingsView()) > 3,
          "...built, now that it is in view");

    activate(win, "pane-seqview");

    check(win->panes_->isVisible("seqview") &&
          !win->panes_->isVisible("composerview") && ticked(win, "seqview"),
          "the sequencer's tick raises it over the canvas");

    activate(win, "pane-composerview");

    check(win->panes_->isVisible("composerview") &&
          !win->panes_->isVisible("seqview"),
          "...and the canvas's raises the canvas again");

    pickMode(win, "patch");

    check(win->mode_ == "patch" && win->panes_->isVisible("nodeview") &&
          !win->panes_->isVisible("composerview") &&
          win->composer_->transport().get_visible(),
          "patch mode puts the graph back and the canvas away, and a "
          "piece that is up keeps its transport");

    activate(win, "pane-composerview");

    check(win->mode_ == "piece" && win->panes_->isVisible("composerview"),
          "the piece's tick in patch mode goes to piece mode");

    /* Each mode's layout is its own: a change in one is not in the other. */
    activate(win, "pane-roll");

    check(isClosed(win, "roll") && !ticked(win, "roll"),
          "a second tick on the roll closes it");

    pickMode(win, "patch");
    pickMode(win, "piece");

    check(isClosed(win, "roll"),
          "...and it is still closed after a trip through patch mode");

    activate(win, "reset-layout");

    check(win->mode_ == "piece" && win->panes_->isVisible("roll") &&
          win->panes_->closed().size() == 5,
          "Reset Layout in piece mode puts piece mode's first layout back");

    pickMode(win, "patch");

    /* ---- View's ticks ---- */

    activate(win, "pane-keyboard");

    check(isClosed(win, "keyboard") && !ticked(win, "keyboard"),
          "unticking Keys closes it to the drawer");

    activate(win, "pane-keyboard");

    check(!isClosed(win, "keyboard") && ticked(win, "keyboard") &&
          win->panes_->isVisible("keyboard"),
          "ticking it puts it back, in view");

    activate(win, "pane-patches");

    check(win->panes_->isVisible("patches") && ticked(win, "patches"),
          "ticking Patch Selector brings the patch list up");

    activate(win, "pane-midimap");

    check(win->panes_->isVisible("midimap") && ticked(win, "midimap"),
          "Ctrl+M brings the MIDI routing up from the drawer");

    activate(win, "reset-layout");

    {
        const std::vector<std::string> closed = win->panes_->closed();

        check(win->mode_ == "patch" && closed.size() == 2 &&
              isClosed(win, "patches") && isClosed(win, "midimap") &&
              !ticked(win, "patches"),
              "Reset Layout puts the first layout back, and the ticks with "
              "it");
    }

    pickMode(win, "piece");

    /* ---- a pane in a window of its own ---- */

    mln_panes_undock(win->panes_->gobj(), "keyboard");
    pump(20);

    {
        GtkWindow *own = mln_panes_get_window(win->panes_->gobj(), "keyboard");
        GtkWidget *keys = mln_panes_get_content(win->panes_->gobj(), "keyboard");

        check(own != NULL && gtk_window_get_transient_for(own) == win->gobj() &&
              gtk_window_get_application(own) == NULL,
              "a pane moved out goes into a window over this one, not the "
              "application's");

        /* What is in it reaches the main window's actions: the roll's
           toggle, twice, from the keys' own widget. */
        bool away = false;

        if (keys != NULL &&
            gtk_widget_activate_action(keys, "win.pane-roll", NULL))
        {
            pump(4);
            away = !win->panes_->isVisible("roll");
        }

        check(away &&
              gtk_widget_activate_action(keys, "win.pane-roll", NULL),
              "...where its pane still reaches the main window's actions");

        pump(4);
    }

    mln_panes_dock(win->panes_->gobj(), "keyboard");
    pump(20);

    check(mln_panes_get_window(win->panes_->gobj(), "keyboard") == NULL &&
          win->panes_->isVisible("keyboard"),
          "...and moved back, it is back in the main window");

    /* ---- the layout, kept ---- */

    const std::string path = win->layoutPath();

    pickMode(win, "patch");
    activate(win, "pane-keyboard");
    win->writeLayout();

    {
        const std::string text = readAll(path);
        const size_t patch = text.find("patch=");
        const size_t piece = text.find("piece=");
        const std::string patchLine =
            patch == std::string::npos ? std::string()
                : text.substr(patch, text.find('\n', patch) - patch);

        check(text.find("[layouts]") != std::string::npos &&
              patch != std::string::npos && piece != std::string::npos &&
              patchLine.find("\"keyboard\"") == std::string::npos &&
              patchLine.find("\"nodeview\"") != std::string::npos,
              "each mode's layout is kept in panes.ini, patch mode's "
              "without the closed keys");
    }

    delete win;
    pump(4);

    ok("the window comes down with editors built in it");

    win = new TestWindow;
    win->set_visible(true);
    pump(8);

    check(win->mode_ == "patch" && isClosed(win, "keyboard") &&
          !ticked(win, "keyboard"),
          "a new window reads the kept layout back");

    win->setDesktopMode("piece");
    pump(4);

    check(!isClosed(win, "keyboard"),
          "...and piece mode's, where the keys were not closed");

    delete win;
    pump(4);

    /* The mode last used is where the next window starts. */
    win = new TestWindow;
    win->applyPrefs();
    win->set_visible(true);
    pump(8);

    check(win->mode_ == "piece",
          "the mode last used is the one a new window starts in");

    win->setDesktopMode("patch");
    pump(4);

    /* ...and a kept layout that is no layout is the first one. */
    delete win;
    pump(4);

    {
        std::ofstream out(path.c_str(), std::ios::trunc);

        out << "[layouts]\npatch={\"tabs\":\n";
    }

    win = new TestWindow;
    win->set_visible(true);
    pump(8);

    {
        const std::vector<std::string> closed = win->panes_->closed();

        check(closed.size() == 2 && isClosed(win, "patches") &&
              isClosed(win, "midimap"),
              "a kept layout that does not parse gives the first layout");
    }

    delete win;
    pump(4);

    /* A panes.ini from before the modes: its one layout is patch mode's. */
    {
        std::ofstream out(path.c_str(), std::ios::trunc);

        out << "[layouts]\ndesktop={\"dir\":\"row\",\"size\":[0.5,0.5],"
               "\"kids\":[{\"tabs\":[\"nodeview\"]},"
               "{\"tabs\":[\"paramview\"]}]}\n";
    }

    win = new TestWindow;
    win->set_visible(true);
    pump(8);

    check(win->mode_ == "patch" && win->panes_->isVisible("nodeview") &&
          isClosed(win, "channelbox") && isClosed(win, "keyboard"),
          "a layout kept before there were modes is patch mode's");

    delete win;
    pump(4);

    /* A first run with panes, by someone who had hidden the keys when that
       was a preference: they stay hidden. */
    {
        std::error_code ec;

        std::filesystem::remove(path, ec);

        string **vals = new string *[2];

        vals[0] = new string("0");
        vals[1] = NULL;
        gthPrefs::instance()->Set("keyboard", vals);
    }

    win = new TestWindow;
    win->applyPrefs();
    win->set_visible(true);
    pump(8);

    check(isClosed(win, "keyboard"),
          "keys hidden by the old preference stay hidden on the first run");

    delete win;
    pump(4);

    return failures;
}

int
main (int argc, char **argv)
{
    std::string pluginPath = PLUGIN_PATH;
    const char *dsp = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p"))
        {
            if (++i >= argc)
                return 2;

            pluginPath = argv[i];
        }
        else
            dsp = argv[i];
    }

    if (dsp == NULL)
    {
        fprintf(stderr, "usage: windowcheck -p PLUGIN_DIR DSP_FILE\n");
        return 2;
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    /* Skipped rather than failed where there is no display, and checked
       before Gtk::Application::create, as editorcheck does. */
    if (getenv("DISPLAY") == NULL && getenv("WAYLAND_DISPLAY") == NULL)
    {
        printf("skipped: no display (run under xvfb-run to exercise this)\n");
        return 0;
    }

    /* A configuration directory of its own, so the layout this writes is
       nobody's, and set before anything asks GLib where it is: GLib asks
       once and remembers. */
    std::string config = thUtil::tempFile("windowcheck-config-");
    std::error_code ec;

    std::filesystem::remove(config, ec);
    std::filesystem::create_directory(config, ec);
    Glib::setenv("XDG_CONFIG_HOME", config);

    Glib::RefPtr<Gtk::Application> app =
        Gtk::Application::create("org.metaphonic.thinksynth.windowcheck",
                                 Gio::Application::Flags::NON_UNIQUE);

    int rc = 0;

    app->signal_activate().connect(
        [&]()
        {
            /* Held, so that the application does not quit between the
               windows this makes and destroys. */
            app->hold();
            rc = run(pluginPath, dsp);
            app->release();
        });

    app->run();

    std::filesystem::remove_all(config, ec);

    printf("\n%d checks, %d failed\n", checks, failures);

    return rc;
}
