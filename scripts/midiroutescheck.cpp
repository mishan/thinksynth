/*
 * Copyright (C) 2004-2026 The thinksynth authors
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
 */

/*
 * midiroutescheck -- the MIDI map's routes table: an edit made in it puts
 * the route into the MIDI player's machine layer, as main.cpp connects
 * them, and into the configuration file, where the next run finds it.
 *
 * NEEDS A DISPLAY, like editorcheck, and skips itself without one.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

#include <gtkmm.h>

#include "think.h"
#include "thMidiRoutes.h"
#include "gthMidiPlay.h"
#include "gthPrefs.h"
#include "MidiRoutesView.h"

namespace fs = std::filesystem;

static int failures = 0;

static void
check (bool ok, const std::string &what, const std::string &detail = "")
{
    printf("%s  %s%s%s\n", ok ? "ok  " : "FAIL", what.c_str(),
           detail.empty() ? "" : ": ", detail.c_str());

    if (!ok)
        failures++;
}

static void
pump (void)
{
    while (g_main_context_iteration(NULL, FALSE))
        ;
}

static std::string
shown (const std::vector<int> &v)
{
    std::string s;

    for (int e : v)
        s += (s.empty() ? "" : " ") + std::to_string(e);

    return "[" + s + "]";
}

static int
run (const std::string &plugins)
{
    const fs::path cfg = fs::temp_directory_path() /
                         ("midiroutescheck-" + std::to_string(getpid()));

    thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    gthMidiPlay play(&synth);

    {
        gthPrefs prefs(cfg.string());

        prefs.Load();

        /* As main.cpp connects them. */
        play.routes().set(thMidiRoutes::MACHINE, prefs.midiRoutes());
        prefs.signal_midi_routes().connect(
            [&play](std::vector<thMidiRoute> routes)
            { play.routes().set(thMidiRoutes::MACHINE, routes); });

        Gtk::Window win;
        MidiRoutesView *view = MidiRoutesView::forPrefs(&prefs);

        win.set_child(*view);
        win.set_visible(true);
        pump();

        /* MIDI channel 3, keys from C4 up, onto channel 20. */
        view->addRoute();
        view->midiSpin(0)->set_value(3);
        view->engineSpin(0)->set_value(20);
        view->lowSpin(0)->set_value(60);
        pump();

        /* Written once the edits stop. */
        g_usleep(700 * 1000);
        pump();

        std::vector<int> high, low, other;

        play.routes().resolve(2, 60, high);
        play.routes().resolve(2, 59, low);
        play.routes().resolve(0, 60, other);
        check(high == std::vector<int>({ 19 }) && low.empty() &&
              other == std::vector<int>({ 0 }),
              "an edit in the routes table plays at once",
              shown(high) + " " + shown(low) + " " + shown(other));

        win.unset_child();
    }

    {
        gthPrefs prefs(cfg.string());

        prefs.Load();

        Gtk::Window win;
        MidiRoutesView *view = MidiRoutesView::forPrefs(&prefs);
        const std::vector<thMidiRoute> r = view->routes();

        win.set_child(*view);
        check(r.size() == 1 && r[0].midi == 2 && r[0].engine == 19 &&
              r[0].lo == 60 && r[0].hi == 127,
              "and the next run reads it from the file and shows it",
              std::to_string(r.size()) + " routes");

        /* Off, and taken away. */
        if (view->engineSpin(0) != NULL)
            view->engineSpin(0)->set_value(0);
        pump();

        const bool off = prefs.midiRoutes().size() == 1 &&
                         prefs.midiRoutes()[0].engine == -1;

        view->removeRoute(0);
        g_usleep(700 * 1000);
        pump();
        check(off && prefs.midiRoutes().empty(),
              "a route set off is off, and removed is gone");
        win.unset_child();
    }

    fs::remove(cfg);
    return failures;
}

int
main (int argc, char **argv)
{
    std::string plugins = PLUGIN_PATH;

    if (argc > 2 && !strcmp(argv[1], "-p"))
        plugins = argv[2];
    if (plugins.back() != '/')
        plugins += '/';

    if (getenv("DISPLAY") == NULL && getenv("WAYLAND_DISPLAY") == NULL)
    {
        printf("skipped: no display (run under xvfb-run to exercise this)\n");
        return 0;
    }

    Glib::RefPtr<Gtk::Application> app =
        Gtk::Application::create("org.metaphonic.thinksynth.midiroutescheck",
                                 Gio::Application::Flags::NON_UNIQUE);
    int rc = 0;

    app->signal_activate().connect([&] { rc = run(plugins); });
    app->run();
    printf("\n%d failure%s\n", rc, rc == 1 ? "" : "s");

    return rc;
}
