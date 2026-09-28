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
 * the patch list and the MIDI routing, as panes (src/gui/Panes.h). This
 * builds it the way main() does, with a patch on a channel, and asks what a
 * person would see: which panes are up the first time, which channel they
 * are about, that the View menu's ticks put panes up and take them down,
 * that the layout is kept in panes.ini and read back, and that the window
 * comes down cleanly with an editor built in it.
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
    using MainSynthWindow::selectChannel;
    using MainSynthWindow::writeLayout;
    using MainSynthWindow::layoutPath;
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

        check(closed.size() == 1 && closed[0] == "patches",
              "the first layout has every pane up but the patch list");
    }

    check(win->panes_->isVisible("channelbox") &&
          win->panes_->isVisible("nodeview") &&
          win->panes_->isVisible("paramview") &&
          win->panes_->isVisible("keyboard"),
          "...and the channels, the graph, the parameters and the keys in "
          "view");

    check(!win->panes_->isVisible("midimap"),
          "...with the MIDI routing a tab behind the parameters");

    check(ticked(win, "keyboard") && ticked(win, "midimap") &&
          !ticked(win, "patches"),
          "View ticks what the layout holds, behind a tab or not");

    /* ---- the channel ---- */

    {
        int rows = 0;

        while (win->chanList_.get_row_at_index(rows) != NULL)
            rows++;

        check(rows == NUM_PATCHES, "Channels has a row for every channel");
    }

    check(win->chan_ == 0 && win->editors_.count(0) == 1,
          "the first channel is picked, and its graph built as it came "
          "into view");

    check(win->editors_.size() == 1,
          "...and only its: a graph nobody has looked at is not built");

    win->chanList_.select_row(*win->chanList_.get_row_at_index(2));
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

    {
        Gtk::ListBoxRow *row = win->chanList_.get_selected_row();

        check(row != NULL && row->get_index() == 2,
              "...and its row selected");
    }

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

    check(isClosed(win, "midimap"),
          "unticking a pane behind a tab closes it too");

    activate(win, "reset-layout");

    {
        const std::vector<std::string> closed = win->panes_->closed();

        check(closed.size() == 1 && closed[0] == "patches" &&
              ticked(win, "midimap") && !ticked(win, "patches"),
              "Reset Layout puts the first layout back, and the ticks with "
              "it");
    }

    /* ---- the layout, kept ---- */

    const std::string path = win->layoutPath();

    activate(win, "pane-keyboard");
    win->writeLayout();

    {
        const std::string text = readAll(path);

        check(text.find("[layouts]") != std::string::npos &&
              text.find("desktop=") != std::string::npos &&
              text.find("\"keyboard\"") == std::string::npos &&
              text.find("\"nodeview\"") != std::string::npos,
              "the layout is kept in panes.ini, without the closed keys");
    }

    delete win;
    pump(4);

    ok("the window comes down with editors built in it");

    win = new TestWindow;
    win->set_visible(true);
    pump(8);

    check(isClosed(win, "keyboard") && !ticked(win, "keyboard"),
          "a new window reads the kept layout back");

    /* ...and a kept layout that is no layout is the first one. */
    delete win;
    pump(4);

    {
        std::ofstream out(path.c_str(), std::ios::trunc);

        out << "[layouts]\ndesktop={\"tabs\":\n";
    }

    win = new TestWindow;
    win->set_visible(true);
    pump(8);

    {
        const std::vector<std::string> closed = win->panes_->closed();

        check(closed.size() == 1 && closed[0] == "patches",
              "a kept layout that does not parse gives the first layout");
    }

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
