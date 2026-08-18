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

#include <filesystem>
#include <fstream>

#include <gtkmm.h>

#include "think.h"

#include "thUtil.h"

#include "thcPlugin.h"
#include "thcScheduler.h"
#include "gthSignal.h"
#include "gui/ComposerCanvas.h"
#include "gui/ComposerWindow.h"
#include "gui/PianoRoll.h"

/* The five application-wide signals gthSignal.h declares, defined here
 * because main.cpp defines them there and this harness is not main.cpp.
 * Empty and never emitted: what is under test is the window's own
 * wiring, and a keyboard that never plays is exactly the state the Kbd
 * input toggle has to survive. */
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
    using ComposerWindow::canvas_;
    using ComposerWindow::canvasScroll_;
    using ComposerWindow::doc_;
    using ComposerWindow::paramPop_;
    using ComposerWindow::structuralReload;
    using ComposerWindow::workPath_;
    using ComposerWindow::status_;
    using ComposerWindow::acts_;
    using ComposerWindow::saveAct_;
    using ComposerWindow::rollAct_;
    using ComposerWindow::tabs_;
    using ComposerWindow::roll_;
    using ComposerWindow::sched_;
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

static std::string
readFile (const std::string &path)
{
    std::ifstream in(path.c_str(), std::ios::binary);

    if (!in)
        return std::string();

    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

/* Put a piece where the window will look for it.
 *
 * THINK_GEN_PATH names a *directory* -- findDataFile joins it with the
 * file name it is after -- and this harness used to set it to a file.
 * The join therefore never matched, the search fell through to the
 * cwd-relative "gen/airports.gen", and what got loaded depended on where
 * the harness was run from: from the source tree, the real piece; under
 * ctest, which runs in the build tree, nothing at all. Every check here
 * that did not look at the piece passed either way, which is why it
 * survived four rounds of additions -- and the refused-piece section,
 * whose entire subject is a bad file, was looking at a blank window.
 *
 * So: a scratch directory with the piece in it under the name the window
 * asks for, which is what the variable was always for. Returns the
 * directory, for the caller to remove. */
static std::string
stagePiece (const std::string &content)
{
    if (content.empty())
        return "";

    std::string dir = thUtil::tempFile("composercheck-gen-");

    if (dir.empty())
        return "";

    /* tempFile makes a file; what is wanted is a directory of that name,
       so the name is claimed and then re-used. */
    std::error_code ec;

    std::filesystem::remove(dir, ec);

    if (!std::filesystem::create_directory(dir, ec) || ec)
        return "";

    std::ofstream out((dir + "/airports.gen").c_str(), std::ios::trunc);

    out << content;
    out.close();

    if (!out)
    {
        /* The directory exists whether or not the piece got into it, and
           a harness that leaves one behind per failed run is a harness
           that fills /tmp on a machine where something is already
           wrong. */
        std::filesystem::remove_all(dir, ec);
        return "";
    }

    Glib::setenv("THINK_GEN_PATH", dir);

    return dir;
}

static int
run (const std::string &pluginPath, const char *genFile)
{
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    /* The window opens whatever piece it finds under THINK_GEN_PATH,
       which is how the ctest entry points it at the shipped piece
       without this having to know where a source tree is.
     *
       Glib::setenv, not setenv: the POSIX one does not exist on MinGW's
       UCRT, and glibmm is a hard dependency of this harness anyway.
       pathcheck spells the same thing with an `#ifdef _WIN32' and
       _putenv_s because it deliberately links nothing but libthink and
       cannot assume glibmm -- so the rule for the tree is glibmm where
       it is already linked, and the ifdef only where it is not. */
    std::string staged;

    if (genFile != NULL)
    {
        staged = stagePiece(readFile(genFile));

        if (staged.empty())
        {
            fail("could not stage the piece under test");
            return failures;
        }
    }

    TestComposer *win = new TestComposer(&synth);

    win->set_visible(true);
    pump(4);

    ok("the composer window builds and shows");

    /* With the piece in it.
     *
       This is the check whose absence let the THINK_GEN_PATH bug live:
       everything below asks about widgets, and widgets come up whether
       or not there is a piece behind them, so a window showing nothing
       passed the lot. Anything that reads the piece has to say so
       first. */
    if (win->doc_.chains.empty())
    {
        fail("the piece under test did not load");
        delete win;
        return failures;
    }

    ok("...with the piece in it");

    /* Save, before anything has been done to the piece. Asked here
       rather than next to the other menu checks because by then the
       knob section has already dirtied the file, and "it is enabled"
       would have been true either way. */
    if (!win->saveAct_)
        fail("there is no save action to ask about");
    else if (win->saveAct_->get_enabled())
        fail("Save was offered on a piece nobody had edited");
    else
        ok("Save starts greyed out");

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

    /* The zoom the canvas inherited from GraphCanvas. What is asserted
       is the arithmetic, not the picture: a zoom that clamps, a fit that
       never magnifies, and a canvas that still answers gestures
       afterwards -- the last one because every handler now converts
       widget pixels to laid-out coordinates on the way in, and a missed
       conversion is a click that lands somewhere else. */
    {
        const double before = win->canvas_->zoom();

        win->canvas_->setZoom(100.0);
        pump(2);

        if (win->canvas_->zoom() > 3.001)
            fail("the zoom did not clamp at the top");

        win->canvas_->setZoom(0.0001);
        pump(2);

        if (win->canvas_->zoom() < 0.249)
            fail("the zoom did not clamp at the bottom");

        win->canvas_->zoomToFit();
        pump(2);

        if (win->canvas_->zoom() > 1.001)
            fail("zoomToFit magnified a drawing that already fitted");

        win->canvas_->setZoom(before);
        pump(2);

        ok("the canvas zooms, and clamps at both ends");
    }

    /* The enlarged view, which is a canvas mode and therefore a place
       to get stuck. Entered, left by Escape, entered again, left by a
       click on the surround -- a mode with one way in and no way out is
       the failure this is here for, not anything about how it looks. */
    {
        ComposerCanvas::Selection sel;

        sel.kind = ComposerCanvas::Selection::STAGE;
        sel.chain = 0;
        sel.index = 0;

        win->canvas_->setEnlarged(sel);
        pump(4);

        if (win->canvas_->enlarged().kind ==
            ComposerCanvas::Selection::NONE)
            fail("a stage did not fill the canvas when asked to");
        else
            ok("a stage fills the canvas when asked to");

        /* And it follows the view rather than the drawing.
         *
           The canvas is sized to the whole piece and lives in a
           scroller, so its own width and height are the size of
           everything -- eight chains tall for an eight-chain piece.
           Laying the enlarged stage out in *that* puts it at the top of
           the content: fine while scrolled to the origin, and gone the
           moment anybody scrolls, with a plugin handed a rectangle far
           bigger than anything on screen. So: scroll, and check the
           picture came along. */
        {
            double ex, ey, ew, eh;

            Glib::RefPtr<Gtk::Adjustment> va =
                win->canvasScroll_.get_vadjustment();

            /* Skipped rather than failed with no piece loaded, which
               is this harness's own state until the next commit fixes
               it: THINK_GEN_PATH is being pointed at a file when
               findDataFile wants a directory, so under ctest there is
               nothing on the canvas to enlarge and nothing to scroll.
               This check therefore says "skip" here and does its job
               from the next commit onward -- which is where it was
               verified by breaking enlargedRect. */
            if (!win->canvas_->enlargedArea(ex, ey, ew, eh) ||
                !va || va->get_upper() - va->get_page_size() < 60)
                printf("skip  nothing large enough here to scroll (area=%d up=%g page=%g)\n", (int)win->canvas_->enlargedArea(ex, ey, ew, eh), va?va->get_upper():-1.0, va?va->get_page_size():-1.0);
            else
            {
                const double was = ey;

                va->set_value(va->get_value() + 60);
                pump(4);

                if (!win->canvas_->enlargedArea(ex, ey, ew, eh))
                    fail("the enlarged stage lost its rectangle");
                else if (ey <= was + 1)
                    fail("the enlarged stage stayed behind when the "
                         "canvas scrolled");
                else
                    ok("the enlarged stage follows the view");

                va->set_value(0);
                pump(4);
            }
        }

        win->canvas_->setEnlarged(ComposerCanvas::Selection());
        pump(4);

        if (win->canvas_->enlarged().kind !=
            ComposerCanvas::Selection::NONE)
            fail("the enlarged view would not go away");
        else
            ok("the enlarged view can be left again");
    }

    /* The params handle on a stage box, pressed rather than read.
     *
       This is the section the coordinate conversions were missing. Every
       gesture handler divides widget pixels by the zoom on the way in,
       and a handler that forgot would still look right at 1:1 -- which
       is the zoom a person opens the window at and the zoom a reviewer
       reads the code at. So the whole section runs at 0.5, where a
       missed division is off by a factor of two and lands on a different
       box or misses every box there is.

       What is asserted is that pressing the handle brings up the params
       and selects the stage, that the box does not change size doing it
       -- the first version of this grew the box in place, and a chain of
       grown boxes is what made a popover the answer -- and that the
       popover goes away again. */
    {
        double hx, hy;
        Gdk::Rectangle before, after;

        win->canvas_->setZoom(0.5);
        pump(2);

        if (win->paramPop_ != NULL)
            fail("a params popover was up before anything was pressed");

        if (!win->canvas_->stageRect(0, 0, before) ||
            !win->canvas_->paramsHandle(0, 0, hx, hy))
            fail("the first stage has no params handle");
        else
        {
            win->canvas_->pressAt(hx, hy, 1, 1);
            pump(4);

            if (win->paramPop_ == NULL)
                fail("pressing the params handle brought up nothing");
            else if (!win->paramPop_->get_visible())
                fail("the params popover was built but never shown");
            else
                ok("a stage's params come up on its handle");

            const ComposerCanvas::Selection &sel =
                win->canvas_->selection();

            if (sel.kind != ComposerCanvas::Selection::STAGE ||
                sel.chain != 0 || sel.index != 0)
                fail("opening a stage's params did not select the stage");
            else
                ok("...and the panel is looking at the same stage");

            if (!win->canvas_->stageRect(0, 0, after))
                fail("the stage box went missing");
            else if (after.get_width() != before.get_width() ||
                     after.get_height() != before.get_height())
                fail("the stage box changed size to show its params");
            else
                ok("...without the box changing size");

            /* And it puts itself away, so the popover is not a mode
               either. Guarded, because the branch above can have found
               nothing to put away -- a harness that segfaults instead of
               failing tells you something is wrong and nothing about
               what, which is the one thing a check must not do. */
            if (win->paramPop_ != NULL)
                win->paramPop_->popdown();

            pump(4);

            if (win->paramPop_ != NULL && win->paramPop_->get_visible())
                fail("the params popover would not go away");
            else
                ok("the params popover closes again");
        }

        win->canvas_->setZoom(1.0);
        pump(2);
    }

    /* The knob nodes: a control and a source, both on the canvas.
     *
       Still at zoom 0.5, and for the same reason -- a knob node's track
       and its port are two small targets a few pixels apart, and a
       handler that forgot to convert would hit the wrong one or neither.

       airports is the piece for this too: one knob, @density, bound into
       every one of its seven chains, which is the case the wire drawing
       exists to survive. */
    {
        double x0, x1, ky, px, py;

        win->canvas_->setZoom(0.5);
        pump(2);

        if (!win->canvas_->knobTrack("density", x0, x1, ky) ||
            !win->canvas_->knobPort("density", px, py))
            fail("@density has no node on the canvas");
        else
        {
            thArg *arg = win->sched_->knob("density");

            if (arg == NULL)
                fail("the piece has no live @density to drive");
            else
            {
                const std::string before = readFile(win->workPath_);
                const double was = (*arg)[0];

                /* Left end, then right end: whichever the knob started
                   at, one of the two is a change, and the far end is
                   its declared maximum. */
                const double toX = was > (arg->min() + arg->max()) / 2
                    ? x0 : x1;

                win->canvas_->pressAt((x0 + x1) / 2, ky, 1, 1);
                pump(1);
                win->canvas_->motionTo(toX, ky);
                pump(1);
                win->canvas_->releaseAt(toX, ky, 1);
                pump(4);

                if ((*arg)[0] == was)
                    fail("dragging the knob node changed nothing");
                else
                    ok("a knob node drives the live piece");

                if (readFile(win->workPath_) == before)
                    fail("the knob drag never reached the file");
                else
                    ok("...and the working copy remembers where it ended");
            }

            /* A wire, pulled from the port onto a stage box. What is
               asserted is that the drop asks -- the canvas deliberately
               does not choose a param for you, because a stage with six
               of them is six honest answers. */
            Gdk::Rectangle at;

            if (!win->canvas_->stageRect(0, 0, at))
                fail("the first stage has no box to drop a wire on");
            else
            {
                const double dx = at.get_x() + at.get_width() / 2;
                const double dy = at.get_y() + at.get_height() / 2;

                win->canvas_->pressAt(px, py, 1, 1);
                pump(1);
                win->canvas_->motionTo(dx, dy);
                pump(1);
                win->canvas_->releaseAt(dx, dy, 1);
                pump(4);

                if (win->paramPop_ == NULL ||
                    !win->paramPop_->get_visible())
                    fail("dropping a wire on a stage asked nothing");
                else
                    ok("a wire dropped on a stage asks which param");

                if (win->paramPop_ != NULL)
                    win->paramPop_->popdown();

                pump(4);
            }

            /* The port's lower half, which is outside the box.
             *
               The port is centred on the knob box's bottom edge, so half
               of it hangs below -- and hit() stops at the edge, so a
               press down there used to find no box and start no wire.
               The target described as generous was half a small circle,
               and which half depended on nothing anyone could see. */
            if (win->canvas_->stageRect(0, 0, at))
            {
                const double dx = at.get_x() + at.get_width() / 2;
                const double dy = at.get_y() + at.get_height() / 2;

                win->canvas_->pressAt(px, py + 4, 1, 1);
                pump(1);
                win->canvas_->motionTo(dx, dy);
                pump(1);
                win->canvas_->releaseAt(dx, dy, 1);
                pump(4);

                if (win->paramPop_ == NULL ||
                    !win->paramPop_->get_visible())
                    fail("the underside of a knob's port started no wire");
                else
                    ok("...from either side of the port");

                if (win->paramPop_ != NULL)
                    win->paramPop_->popdown();

                pump(4);
            }

            /* And dropped on nothing, it is nothing: a wire the user
               thought better of has to be abandonable, or the only way
               out of starting one is to bind something. */
            win->canvas_->pressAt(px, py, 1, 1);
            pump(1);
            win->canvas_->motionTo(px + 400, py + 400);
            pump(1);
            win->canvas_->releaseAt(px + 400, py + 400, 1);
            pump(4);

            if (win->paramPop_ != NULL && win->paramPop_->get_visible())
                fail("a wire dropped on empty canvas bound something");
            else
                ok("...and a wire dropped on nothing is nothing");
        }

        win->canvas_->setZoom(1.0);
        pump(2);
    }

    /* A splice that cannot be written says so.
     *
       Every edit in the window reports through editOk, which puts the
       reason on the status line; the knob's commit tested for OK and
       otherwise did nothing, so a knob dragged against a working copy it
       could not write moved on screen, moved the piece, and left the
       file behind in silence. That is the failure where silence costs
       most, because everything visible looked like it had worked.

       Forced by moving the working copy out from under the window. The
       first attempt at this took write permission off the file instead,
       and it did not bite: thcGenEdit writes a temp file and renames it
       over the target, and a rename is governed by the directory. The
       splice succeeded, the status line went on showing the piece's
       name, and the check passed with the fix removed -- which is how it
       was caught.

       Compared against what the line said before rather than against
       empty, for the same reason: a successful edit rewrites it with the
       piece's name, so "not empty" is true either way. */
    {
        double kx0, kx1, kyy;

        if (win->canvas_->knobTrack("density", kx0, kx1, kyy) &&
            !win->workPath_.empty())
        {
            const std::string aside = win->workPath_ + ".away";

            /* Glib::ustring, not std::string. get_text() returns the
               former, and comparing the two only works on glibmm builds
               where ustring's templated operator== is available -- 2.88
               here takes it, the runner's does not, and the error is a
               page of candidate lists rather than "these are different
               types". Kept in the toolkit's own type so there is nothing
               to convert on either. */
            const Glib::ustring before = win->status_->get_text();

            std::error_code ec;

            std::filesystem::rename(win->workPath_, aside, ec);

            if (ec)
                printf("skip  could not move the working copy aside\n");
            else
            {
                win->canvas_->pressAt((kx0 + kx1) / 2, kyy, 1, 1);
                pump(1);
                win->canvas_->releaseAt(kx1, kyy, 1);
                pump(4);

                if (win->status_->get_text() == before)
                    fail("a knob splice failed and said nothing");
                else
                    ok("a splice that cannot be written says so");

                std::filesystem::rename(aside, win->workPath_, ec);
            }
        }
    }

    /* The window's own furniture: the menu that replaced the file row,
       the two tabs that replaced one long column, and the roll that can
       now be got out of the way.
     *
       New and Open used to be buttons inside the Edit panel, which meant
       the two things you do before there is anything to edit were behind
       a toggle for editing. They are actions now, and an action that is
       not in the group is not in the menu either -- so what is checked
       is that the group has them, by name, which is the same list the
       menu model refers to. */
    {
        static const char *want[] = { "new", "open", "save", "saveas",
                                      "revert", "roll" };
        int missing = 0;

        for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++)
        {
            if (!win->acts_ || !win->acts_->has_action(want[i]))
            {
                printf("      no action `composer.%s'\n", want[i]);
                missing++;
            }
        }

        if (missing > 0)
            fail("the file menu's actions are all there");
        else
            ok("the file menu's actions are all there");
    }

    /* And Save after an edit. It was a button's sensitivity and is an
       action's enabled flag; the menu reads the latter, and nothing else
       does, so this is the only thing left that greys the item out. */
    if (win->saveAct_ && !win->saveAct_->get_enabled())
        fail("Save stayed greyed out after the piece was edited");
    else
        ok("...and wakes up once the piece has been edited");

    /* The roll is a strip now, not a fixture. Checked both ways: a view
       you can hide and not show again is worse than one you cannot
       hide. */
    {
        win->activate_action("composer.roll");
        pump(4);

        if (win->roll_->get_visible())
            fail("the piano roll would not go away");
        else
            ok("the piano roll can be hidden");

        win->activate_action("composer.roll");
        pump(4);

        if (!win->roll_->get_visible())
            fail("the piano roll would not come back");
        else
            ok("...and brought back");
    }

    /* Clicking the canvas raises Selection. Without this the tabs are a
       worse version of the column they replaced: the panel would go on
       showing the piece's name while the thing just clicked sat behind
       a tab nobody was told about. */
    {
        win->editBtn_->set_active(true);
        pump(4);
        win->tabs_.set_current_page(0);
        pump(2);

        ComposerCanvas::Selection sel;

        sel.kind = ComposerCanvas::Selection::STAGE;
        sel.chain = 0;
        sel.index = 0;

        win->canvas_->select(sel);
        pump(4);

        if (win->tabs_.get_current_page() != 1)
            fail("selecting a stage did not raise the Selection tab");
        else
            ok("clicking the canvas raises Selection");

        /* But deselecting does not: being thrown into an empty tab
           reads as the window losing its place. */
        win->tabs_.set_current_page(0);
        pump(2);
        win->canvas_->select(ComposerCanvas::Selection());
        pump(4);

        if (win->tabs_.get_current_page() != 0)
            fail("deselecting yanked the panel to an empty tab");
        else
            ok("...and deselecting leaves it where it was");

        win->editBtn_->set_active(false);
        pump(4);
    }

    delete win;
    pump(2);

    if (!staged.empty())
    {
        /* The error_code overload, as pathcheck uses for its fixtures.
           The throwing one turns a locked or unreadable scratch
           directory into an exception out of the tail of a run that has
           already finished -- so the process dies after the checks have
           passed and the report they wrote never reaches anyone. A
           cleanup that fails should be a leaked directory, not a lost
           result. */
        std::error_code ec;

        std::filesystem::remove_all(staged, ec);
    }

    ok("the window closes without taking anything with it");

    return failures;
}

/* A window closed with its idles still queued.
 *
 * The window schedules work at idle -- the first split of the canvas
 * against the roll, the Edit panel's width, and every structural reload
 * -- and an idle capturing
 * `this' is held by the main loop, not by the window. So a window closed
 * before the loop comes round again used to leave a callback pointing at
 * freed memory, which then set a paned position through a destroyed
 * widget or reloaded the piece through a deleted scheduler.
 *
 * Built, shown, given just enough of the loop to map and queue, deleted,
 * and then the loop is run properly. Nothing is asserted: on a plain
 * build the freed memory usually still reads as what it was and the run
 * carries on regardless. Under -DTHINK_SANITIZE=address, which is what
 * this is for, it is a heap-use-after-free and the process says so.
 *
 * A reload is queued as well as the map, because the map's idle fires at
 * default priority and a pump generous enough to show the window may
 * well have drained it; scheduleReload's is queued from inside the
 * handler and is reliably still there. */
static void
closeWithIdlesPending (const std::string &pluginPath)
{
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    TestComposer *win = new TestComposer(&synth);

    win->set_visible(true);
    pump(1);

    /* Three idles, queued as late as possible: the first split of the
       canvas against the roll (from the map, above), the Edit panel's
       width (from the toggle), and a structural reload. */
    win->editBtn_->set_active(true);
    win->structuralReload();

    delete win;
    pump(8);

    ok("a window closed with idles queued takes them with it");
}

/* A piece the loader refuses, drawn anyway.
 *
 * parseWork deliberately keeps the window up after a failed load: the
 * error has to be readable and the piece has to be editable, so the
 * canvas goes on drawing what `describe' read -- and describe reports a
 * file as written, not as validated. Everything downstream of it is
 * therefore looking at numbers no loader ever approved.
 *
 * `channel = 0' is the sharpest case, because it is the one spelling the
 * 1-16 renumbering made invalid, so it is exactly what an older file
 * hands over. The canvas subtracts one to reach the engine's colour
 * numbering and used to accept anything non-negative, which turned that
 * into channel -1.
 *
 * What is asserted is only that the window builds, draws and closes.
 * A wrong hue is not something a test can see; a window that will not
 * come up is. */
static int
runRefused (const std::string &pluginPath)
{
    const std::string tmp = stagePiece(
            "name \"refused\";\n"
            "chain c {\n"
            "    stage s gen::eno_line { notes = \"C4\"; };\n"
            "    sink { channel = 0; };\n"     /* the old numbering       */
            "};\n"
            "chain d {\n"
            "    stage s gen::eno_line { notes = \"E4\"; };\n"
            "    sink { channel = 99; };\n"    /* and something absurd    */
            "};\n");

    if (tmp.empty())
    {
        fail("could not make a scratch piece");
        return failures;
    }

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    TestComposer *win = new TestComposer(&synth);

    win->set_visible(true);
    pump(6);

    /* Drawn twice, with the edit panel in between, because the canvas is
       rebuilt on the way through and a box built from a rejected file
       has to survive both passes. */
    win->editBtn_->set_active(true);
    pump(6);
    win->editBtn_->set_active(false);
    pump(6);

    /* And this one is the refused piece's version of it: the section is
       about a file the loader rejected, so a file that never arrived
       would be the wrong subject entirely -- describe reports a file as
       written, which is why there are chains here at all. */
    if (win->doc_.chains.size() != 2)
        fail("the refused piece did not reach the window");
    else
        ok("a piece the loader refused still draws");

    delete win;
    pump(2);

    {
        std::error_code ec;

        std::filesystem::remove_all(tmp, ec);
    }

    ok("...and closes again");

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
        [&]()
        {
            rc = run(pluginPath, genFile);

            if (rc == 0)
                rc = runRefused(pluginPath);

            if (rc == 0)
                closeWithIdlesPending(pluginPath);
        });

    app->run();

    printf("\n%d checks, %d failed\n", checks, failures);

    return rc;
}
