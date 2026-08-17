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
 * composercheck -- can you press the buttons?
 *
 * gencheck covers the loader, the scheduler, the editor's splices and
 * the replay contract, and covers them well. What it cannot cover is the
 * window, because the window is widgets: gencheck links the composer
 * host and no toolkit at all.
 *
 * That gap had a crash in it. `rebuildEditor' nulls the pointers to the
 * widgets it is about to destroy, and someone had added `kbdBtn_' to
 * that list -- but Kbd input lives in the *toolbar*, which rebuildEditor
 * never touches. So opening the Edit panel left a live, still-connected
 * button behind a null pointer, and the next click on it went through
 * `onKbdToggle' straight into a null dereference. Every part of that is
 * ordinary; what made it survive is that nothing ever pressed the
 * buttons.
 *
 * So this presses them. Build the window, toggle Edit on and off,
 * toggle Kbd input, toggle it back, pump the main loop between each so
 * the handlers actually run. It asserts almost nothing about what the
 * widgets *say* -- that is what a screenshot would be for. What it
 * asserts is that the program is still alive afterwards, which for this
 * class of bug is the whole of the question.
 *
 * NEEDS A DISPLAY, like editorcheck and for the same reason. It skips
 * itself loudly without one.
 *
 *   xvfb-run -a ./build/scripts/composercheck -p build/plugins/ \
 *       gen/airports.gen
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtkmm.h>

#include "think.h"

#include "thcPlugin.h"
#include "thcScheduler.h"
#include "gthSignal.h"
#include "gui/ComposerWindow.h"

/* The six application-wide signals ComposerWindow listens to, defined
 * here because main.cpp defines them there and this harness is not
 * main.cpp. Empty and never emitted: what is under test is the window's
 * own wiring, and a keyboard that never plays is exactly the state the
 * Kbd input toggle has to survive. */
sigNoteOn    m_sigNoteOn;
sigNoteOff   m_sigNoteOff;
sigNoteClear m_sigNoteClear;
sigNoteOn    m_sigKbdNoteOn;
sigNoteOff   m_sigKbdNoteOff;

/* A subclass, for the same reason editorcheck has one: which widgets the
 * window keeps are its own business, and there is no call to widen them
 * for a test. `protected' is the access level that means "and for
 * anything that is a ComposerWindow", which this is. */
class TestComposer : public ComposerWindow {
public:
    TestComposer (thSynth *synth) : ComposerWindow(synth) { }

    using ComposerWindow::editBtn_;
    using ComposerWindow::kbdBtn_;
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

/* Let the toolkit act on what was just asked of it.
 *
 * A toggle's handler runs from the main loop, not from set_active(), so
 * a test that set three things and then looked would be looking at a
 * window that had not caught up. Bounded rather than "until idle":
 * a window with a running frame clock in it never is. */
static void
pump (int rounds)
{
    Glib::RefPtr<Glib::MainContext> ctx = Glib::MainContext::get_default();

    for (int i = 0; i < rounds; i++)
        while (ctx->pending())
            ctx->iteration(false);
}

static int
run (const std::string &pluginPath, const char *genFile)
{
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    /* The window opens whatever THINK_GEN_PATH names, which is how the
       ctest entry points it at the shipped piece without this having to
       know where a source tree is.
     *
       Glib::setenv, not setenv: the POSIX one does not exist on MinGW's
       UCRT, and glibmm is a hard dependency of this harness anyway.
       pathcheck spells the same thing with an `#ifdef _WIN32' and
       _putenv_s because it deliberately links nothing but libthink and
       cannot assume glibmm -- so the rule for the tree is glibmm where
       it is already linked, and the ifdef only where it is not. */
    if (genFile != NULL)
        Glib::setenv("THINK_GEN_PATH", genFile);

    TestComposer *win = new TestComposer(&synth);

    win->set_visible(true);
    pump(4);

    ok("the composer window builds and shows");

    if (win->editBtn_ == NULL || win->kbdBtn_ == NULL)
    {
        fail("the toolbar's toggles exist");
        delete win;
        return failures;
    }

    /* The Edit panel, which is what nulls the pointers. Twice, because
       the second build is the one that runs after rebuildEditor has
       already cleared everything once. */
    for (int round = 0; round < 2; round++)
    {
        win->editBtn_->set_active(true);
        pump(4);
        win->editBtn_->set_active(false);
        pump(4);
    }

    ok("the edit panel opens and closes twice");

    /* And now the button the panel is not allowed to have forgotten.
       This is the crash: before the fix, the first press here went
       through a null kbdBtn_ and took the process with it. */
    win->kbdBtn_->set_active(true);
    pump(4);
    win->kbdBtn_->set_active(false);
    pump(4);

    ok("kbd input toggles after the edit panel has been rebuilt");

    /* The other order too, in case a future rebuild only forgets on one
       of the two paths. */
    win->editBtn_->set_active(true);
    pump(4);
    win->kbdBtn_->set_active(true);
    pump(4);
    win->editBtn_->set_active(false);
    pump(4);
    win->kbdBtn_->set_active(false);
    pump(4);

    ok("kbd input toggles with the edit panel open");

    delete win;
    pump(2);

    ok("the window closes without taking anything with it");

    return failures;
}

int
main (int argc, char **argv)
{
    std::string pluginPath = PLUGIN_PATH;
    const char *genFile = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p"))
        {
            if (++i >= argc)
                return 2;

            pluginPath = argv[i];
        }
        else
            genFile = argv[i];
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    /* Skipped rather than failed where there is no display, and checked
       before Gtk::Application::create because GTK's answer to no display
       is to abort -- which reads as a crash rather than as a machine
       without a screen. Same reasoning as editorcheck's, which is where
       this was copied from on purpose: two harnesses skipping in two
       different ways would be one more thing to keep straight. */
    if (getenv("DISPLAY") == NULL && getenv("WAYLAND_DISPLAY") == NULL)
    {
        printf("skipped: no display (run under xvfb-run to exercise this)\n");
        return 0;
    }

    Glib::RefPtr<Gtk::Application> app =
        Gtk::Application::create("org.metaphonic.thinksynth.composercheck",
                                 Gio::Application::Flags::NON_UNIQUE);

    int rc = 0;

    app->signal_activate().connect(
        [&]() { rc = run(pluginPath, genFile); });

    app->run();

    printf("\n%d checks, %d failed\n", checks, failures);

    return rc;
}
