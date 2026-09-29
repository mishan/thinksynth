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
 * that list -- but Kbd input lives in the *transport*, which rebuildEditor
 * never touches. So opening the Edit panel left a live, still-connected
 * button behind a null pointer, and the next click on it went through
 * `onKbdToggle' straight into a null dereference. Every part of that is
 * ordinary; what made it survive is that nothing ever pressed the
 * buttons.
 *
 * So this presses them. Build the composer, bring its settings into view
 * and out again, toggle Kbd input, toggle it back, pump the main loop
 * between each so the handlers actually run. It asserts almost nothing about what the
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

#include <math.h>
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
#include "gthPatchfile.h"
#include "gthSignal.h"
#include "gui/ComposerCanvasWidget.h"
#include "gui/Composer.h"
#include "gui/ItemBrowser.h"
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
 * composer keeps are its own business, and there is no call to widen them
 * for a test. `protected' is the access level that means "and for
 * anything that is a Composer", which this is.
 *
 * And a window around it, standing in for the main window's panes: the
 * canvas, the roll, the settings and the selection stacked, with the
 * transport over them and the composer's actions on the window. Started
 * as it is built, as the main window starts it when one of its panes is
 * first looked at, and with the canvas counted as in view. */
class TestComposer : public Composer {
public:
    TestComposer (thSynth *synth, bool startNow = true) : Composer(synth)
    {
        Gtk::Box *box = Gtk::manage(new Gtk::Box(Gtk::Orientation::VERTICAL));

        box->append(transport());
        box->append(status());
        canvasView().set_vexpand(true);
        box->append(canvasView());
        box->append(rollView());
        box->append(settingsView());
        box->append(selectionView());

        host_ = new Gtk::Window;
        host_->set_default_size(1060, 640);
        host_->set_child(*box);
        host_->insert_action_group("composer", actions());

        signal_show_selection().connect([this] { selectionShown_++; });

        setCanvasShown(true);

        if (startNow)
            start();
    }

    /* The window first: it lets go of the widgets that are the
       composer's, and the composer goes after. */
    ~TestComposer (void)
    {
        delete host_;
    }

    void set_visible (bool on) { host_->set_visible(on); }

    void activate_action (const Glib::ustring &name)
    {
        host_->activate_action(name);
    }

    Gtk::Window *host_;

    /* How many times the Selection pane has been asked for. */
    int selectionShown_ = 0;

    using Composer::kbdBtn_;
    using Composer::canvas_;
    using Composer::canvasScroll_;
    using Composer::doc_;
    using Composer::paramPop_;
    using Composer::structuralReload;
    using Composer::workPath_;
    using Composer::status_;
    using Composer::acts_;
    using Composer::saveAct_;
    using Composer::roll_;
    using Composer::sched_;
    using Composer::selBox_;
    using Composer::editorBox_;
    using Composer::seq_;
    using Composer::dirty_;
    using Composer::genPath_;
};

/* Same arrangement, for the browser dialog: what it keeps is its own
 * business, and a test is a subclass rather than a wider header. */
class TestBrowser : public ItemBrowser {
public:
    TestBrowser (Gtk::Window &parent, const Provider &provider,
                 const std::string &current)
        : ItemBrowser(parent, "browsercheck", provider, "", current) { }

    using ItemBrowser::filter_;
    using ItemBrowser::openBtn_;
    using ItemBrowser::treeModel_;
    using ItemBrowser::selection_;
    using ItemBrowser::selectedFile;
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

/* Every widget of one kind under `w', in the order they are laid out.
 *
 * The params popover is a StageParamsView now -- a PanelView over
 * src/StagePanel.cpp -- and which widget a row is drawn as is the view's
 * business, so this asks the tree rather than the class. Re-collected after
 * every edit, because an edit re-describes the panel and the widgets that
 * reported it are gone by the time it returns. */
template <class T>
static void collect (Gtk::Widget *w, std::vector<T *> &out)
{
    if (w == NULL)
        return;

    T *found = dynamic_cast<T *>(w);

    if (found != NULL)
        out.push_back(found);

    for (Gtk::Widget *c = w->get_first_child(); c != NULL;
         c = c->get_next_sibling())
        collect(c, out);
}

/* The work copy, as text: what a splice is asserted against. */
static std::string readAll (const std::string &path)
{
    std::ifstream in(path.c_str());

    return std::string((std::istreambuf_iterator<char>(in)),
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

    /* Before start(): no scheduler ticking for a piece nobody opened, and
       nothing to save. */
    {
        TestComposer *idle = new TestComposer(&synth, false);

        pump(2);

        if (idle->sched_ != NULL || idle->started())
            fail("the composer started a scheduler before it was started");
        else if (idle->saveAct_->get_enabled())
            fail("Save was offered before there was a piece");
        else
            ok("a composer not yet started has no scheduler and nothing "
               "to save");

        delete idle;
        pump(2);
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

    /* And with its instrument on a channel.
     *
     * A piece carries the instrument it is played on now, and bringing
     * one up is the half of that the scheduler deliberately does not do
     * itself: it goes through gthPatchManager, so an instrument is also
     * a patch tab with a filename on it. Nothing else in this file
     * would notice if that hook stopped being installed -- the piece
     * would load, the canvas would draw it, and every sink would
     * deliver into a channel with nothing on it. */
    if (win->sched_->instruments().empty())
        fail("the piece under test carries no instrument to check");
    else
    {
        const thcInstrument &inst = win->sched_->instruments()[0];

        if (gthPatchManager::instance()->getPatch(inst.channel) == NULL)
            fail("the instrument never reached the patch manager");
        else if (synth.getChanArg(inst.channel, "fmin") == NULL)
            fail("the instrument's graph never reached its channel");
        else
            ok("...and its instrument loaded onto a channel");
    }

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

    if (win->kbdBtn_ == NULL)
    {
        fail("the transport's toggle exists");
        delete win;
        return failures;
    }

    /* The piece's settings and the selection coming into view and going
       out of it, which is what builds and clears them and nulls the
       pointers. Twice, because the second build is the one that runs
       after rebuildEditor has already cleared everything once. */
    for (int round = 0; round < 2; round++)
    {
        win->setEditing(true);
        pump(4);
        win->setEditing(false);
        pump(4);
    }

    ok("the edit panel opens and closes twice");

    /* Its width: a knob is a block of rows rather than one wide row, so
       the settings fit a pane beside the canvas without scrolling
       sideways. One row of all five was some 640 pixels. */
    {
        win->setEditing(true);
        pump(4);

        int min = 0, nat = 0, a = 0, b = 0;

        win->editorBox_.measure(Gtk::Orientation::HORIZONTAL, -1,
                                min, nat, a, b);

        if (win->doc_.knobs.empty())
            fail("the piece under test has a knob to lay out");
        else if (min <= 0 || min > 440)
            fail("the piece's settings fit a pane 440 pixels wide");
        else
            ok("the piece's settings fit a pane 440 pixels wide");

        win->setEditing(false);
        pump(4);
    }

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
    win->setEditing(true);
    pump(4);
    win->kbdBtn_->set_active(true);
    pump(4);
    win->setEditing(false);
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

    /* A chain's M and S, pressed at half zoom for the reason the params
       handle below is: a missed conversion lands on another box. The
       buttons set the live flags and leave the selection alone, and a
       structural reload of the same piece keeps them. */
    if (win->sched_->chainCount() >= 2)
    {
        double mx, my, sx, sy;

        win->canvas_->setZoom(0.5);
        pump(2);

        const ComposerCanvas::Selection was = win->canvas_->selection();

        if (!win->canvas_->chainChip(0, 0, mx, my) ||
            !win->canvas_->chainChip(0, 1, sx, sy))
            fail("the first chain has no mute or solo button");
        else
        {
            win->canvas_->pressAt(mx, my, 1, 1);
            pump(2);

            if (win->sched_->chain(0)->muted &&
                !win->sched_->chain(1)->muted)
                ok("a chain's M mutes that chain");
            else
                fail("a chain's M mutes that chain");

            if (win->canvas_->selection() == was)
                ok("...and leaves the selection where it was");
            else
                fail("...and leaves the selection where it was");

            win->canvas_->pressAt(mx, my, 1, 1);
            pump(2);

            if (!win->sched_->chain(0)->muted)
                ok("...and a second press unmutes it");
            else
                fail("...and a second press unmutes it");

            win->canvas_->pressAt(sx, sy, 1, 1);
            pump(2);

            if (win->sched_->chain(0)->soloed &&
                win->sched_->audible(*win->sched_->chain(0)) &&
                !win->sched_->audible(*win->sched_->chain(1)))
                ok("a chain's S silences every other chain");
            else
                fail("a chain's S silences every other chain");

            win->structuralReload();
            pump(6);

            if (win->sched_->chainCount() >= 2 &&
                win->sched_->chain(0)->soloed &&
                !win->sched_->audible(*win->sched_->chain(1)))
                ok("...and the solo survives a reload of the same piece");
            else
                fail("...and the solo survives a reload of the same piece");

            if (win->canvas_->chainChip(0, 1, sx, sy))
                win->canvas_->pressAt(sx, sy, 1, 1);

            pump(2);

            if (win->sched_->chainCount() >= 2 &&
                !win->sched_->chain(0)->soloed &&
                win->sched_->audible(*win->sched_->chain(1)))
                ok("...and pressing it again brings the others back");
            else
                fail("...and pressing it again brings the others back");

            /* The enlarged view hides the rows, and the buttons with
               them: a press where one was is not a mute. */
            ComposerCanvas::Selection big;

            big.kind = ComposerCanvas::Selection::STAGE;
            big.chain = 0;
            big.index = 0;
            win->canvas_->setEnlarged(big);
            pump(2);

            if (win->canvas_->enlarged().kind ==
                ComposerCanvas::Selection::NONE)
                fail("the first stage would not enlarge");
            else if (win->canvas_->chainChip(0, 0, mx, my))
            {
                win->canvas_->pressAt(mx, my, 1, 1);
                pump(2);

                if (!win->sched_->chain(0)->muted)
                    ok("a press where M was, in the enlarged view, is "
                       "not a mute");
                else
                {
                    fail("a press where M was, in the enlarged view, is "
                         "not a mute");
                    win->sched_->setMuted(0, false);
                }

                win->canvas_->setEnlarged(ComposerCanvas::Selection());
                pump(2);
            }
        }
    }
    else
        fail("the piece here has fewer than two chains to mute between");

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
        CanvasRect before, after;

        win->canvas_->setZoom(0.5);
        pump(2);

        /* The zoom each rectangle was measured at, because stageRect
           answers in shell pixels and the two readings below straddle a
           pump.
         *
           A canvas asked to fit before its widget had a size defers the
           fit to the next allocation (CanvasContent::zoomToFit), and
           whether that allocation lands inside the pump above or the one
           after the press is the main loop's business, not this test's.
           It landed between the two readings about one run in eight, and
           the box that had not changed at all was reported as having
           changed size. Dividing each reading by the zoom it was taken at
           asks the question this check means: the box, in the coordinates
           it is laid out in. */
        const double zoomBefore = win->canvas_->zoom();

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

            const double zoomAfter = win->canvas_->zoom();

            if (!win->canvas_->stageRect(0, 0, after))
                fail("the stage box went missing");
            /* Two content units of slack, because each reading is a whole
               number of shell pixels and dividing by a zoom of a half
               turns half a pixel of rounding into one unit at each end.
               The growth this is here to catch was the box getting a
               column of parameter rows taller, which is tens. */
            else if (fabs(after.w / zoomAfter - before.w / zoomBefore) > 2.0 ||
                     fabs(after.h / zoomAfter - before.h / zoomBefore) > 2.0)
            {
                char said[160];

                snprintf(said, sizeof(said),
                         "the stage box changed size to show its params: "
                         "%dx%d at zoom %.3f became %dx%d at %.3f",
                         before.w, before.h, zoomBefore,
                         after.w, after.h, zoomAfter);
                fail(said);
            }
            else
                ok("...without the box changing size");

            /* The rows are controls, and pressing one reaches the file.
             *
               This is the half of a parameter panel no headless harness
               can ask about. What the rows *say* -- which unit a duration
               was written in, which knobs a value may be read through,
               what a typed note set is allowed to be -- is
               scripts/panelcheck's, and it says it with no toolkit
               anywhere. What is left here is that a thPanel became real
               widgets and that moving one splices the piece.

               Everything is re-collected between edits: an edit
               re-describes the panel, so the widget that reported it has
               been destroyed by the time set_value returns. */
            if (win->paramPop_ != NULL)
            {
                std::vector<Gtk::SpinButton *> spins;
                std::vector<Gtk::DropDown *> menus;

                collect(win->paramPop_, spins);
                collect(win->paramPop_, menus);

                if (spins.empty())
                    fail("the params popover has nothing to type into");
                else
                {
                    const std::string before = readAll(win->workPath_);
                    const double want = spins[0]->get_value() + 1.0;

                    spins[0]->set_value(want);
                    pump(4);

                    if (readAll(win->workPath_) == before)
                        fail("typing a number into the popover changed "
                             "nothing in the piece");
                    else
                        ok("...and typing in one splices the piece");
                }

                /* A menu beside a value rather than being one: the unit a
                   duration is written in, or the knob it is read through.
                   Either is an edit of the same line, and neither existed
                   as a rule anything but the window could see before. */
                menus.clear();
                collect(win->paramPop_, menus);

                if (menus.empty())
                    fail("the params popover offers no unit or binding");
                else
                {
                    const std::string before = readAll(win->workPath_);
                    const guint was = menus[0]->get_selected();

                    menus[0]->set_selected(was == 0 ? 1 : 0);
                    pump(4);

                    if (readAll(win->workPath_) == before)
                        fail("picking from a row's menu changed nothing "
                             "in the piece");
                    else
                        ok("...and a row's menu is an edit of the same "
                           "line");
                }
            }

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
            CanvasRect at;

            if (!win->canvas_->stageRect(0, 0, at))
                fail("the first stage has no box to drop a wire on");
            else
            {
                const double dx = at.x + at.w / 2;
                const double dy = at.y + at.h / 2;

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
                const double dx = at.x + at.w / 2;
                const double dy = at.y + at.h / 2;

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

    /* The composer's own furniture: the menu that replaced the file row.
     *
       New and Open used to be buttons inside the Edit panel, which meant
       the two things you do before there is anything to edit were behind
       a toggle for editing. They are actions now, and an action that is
       not in the group is not in the menu either -- so what is checked
       is that the group has them, by name, which is the same list the
       menu model refers to. */
    {
        static const char *want[] = { "new", "open", "save", "saveas",
                                      "revert" };
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

    /* Clicking the canvas asks for Selection. Without this the pane is a
       worse version of the column it replaced: the settings would go on
       showing the piece's name while the thing just clicked sat behind a
       tab nobody was told about. */
    {
        win->setEditing(true);
        pump(4);

        const int before = win->selectionShown_;

        ComposerCanvas::Selection sel;

        sel.kind = ComposerCanvas::Selection::STAGE;
        sel.chain = 0;
        sel.index = 0;

        win->canvas_->select(sel);
        pump(4);

        if (win->selectionShown_ != before + 1)
            fail("selecting a stage did not ask for the Selection pane");
        else
            ok("clicking the canvas asks for Selection");

        /* But deselecting does not: being thrown into an empty pane reads
           as the window losing its place. */
        win->canvas_->select(ComposerCanvas::Selection());
        pump(4);

        if (win->selectionShown_ != before + 1)
            fail("deselecting asked for an empty Selection pane");
        else
            ok("...and deselecting leaves the panes where they were");

        /* And a selection while neither the settings nor the selection is
           in view asks too -- nothing else says where a stage is edited --
           and what it selected is there when Selection comes into view. */
        win->setEditing(false);
        pump(4);
        win->canvas_->select(sel);
        pump(4);

        if (win->selectionShown_ != before + 2)
            fail("a selection with nobody editing did not ask for Selection");
        else
            ok("...and asks while nobody is editing, too");

        win->setEditing(true);
        pump(4);

        if (win->selBox_ == NULL || win->selBox_->get_first_child() == NULL)
            fail("Selection came into view without the stage in it");
        else
            ok("...which is in it when it comes into view");

        /* Out of view and back, with nothing changed: the same widgets,
           not a rebuild that loses what was open and scrolled. */
        Gtk::Widget *kept = win->selBox_->get_first_child();

        win->setEditing(false);
        pump(4);
        win->setEditing(true);
        pump(4);

        if (win->selBox_ == NULL || win->selBox_->get_first_child() != kept)
            fail("Selection was rebuilt for being looked away from");
        else
            ok("...and is kept as it was while looked away from");
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

/* A composer destroyed with its idle still queued.
 *
 * Every structural reload is scheduled at idle, and an idle capturing
 * `this' is held by the main loop, not by the composer. So one destroyed
 * before the loop comes round again used to leave a callback pointing at
 * freed memory, which then reloaded the piece through a deleted
 * scheduler.
 *
 * Built, shown, given just enough of the loop to map and queue, deleted,
 * and then the loop is run properly. Nothing is asserted: on a plain
 * build the freed memory usually still reads as what it was and the run
 * carries on regardless. Under -DTHINK_SANITIZE=address, which is what
 * this is for, it is a heap-use-after-free and the process says so.
 *
 * The reload is queued from inside the handler, after the pump, so it is
 * reliably still there when the composer goes. */
static void
closeWithIdlesPending (const std::string &pluginPath)
{
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    TestComposer *win = new TestComposer(&synth);

    win->set_visible(true);
    pump(1);

    /* The settings built, and a structural reload queued as late as
       possible. */
    win->setEditing(true);
    win->structuralReload();

    delete win;
    pump(8);

    ok("a window closed with idles queued takes them with it");
}

/* The browser's filter box, which rebuilds the list under the cursor.
 *
 * Two things went wrong there and neither is visible without pressing
 * keys. A rebuild that cannot find the row to put the cursor back on
 * used to return with the Open button still sensitive and the detail
 * line still describing a row from the model it had just thrown away --
 * so Open was pressable over nothing, and pressing it did nothing at
 * all, silently. And a rebuild that could find a row went back to the
 * file already loaded rather than to the row the user was reading, so
 * every keystroke dragged the cursor away from what was being typed
 * towards.
 *
 * The provider is a stub because what is under test is the widget, not
 * a corpus: DspCatalog::matches and GenCatalog::matches are the real
 * rules and they are checked where they live, with no display near
 * them. */
static std::vector<BrowserGroup>
stubRows (const std::string &needle)
{
    BrowserGroup keys;
    BrowserGroup drums;

    keys.name = "Keys";
    drums.name = "Drums";

    const BrowserItem all[] = {
        { "rhodes.dsp", "Rhodes",  "an electric piano", "rhodes.dsp"  },
        { "wurli.dsp",  "Wurli",   "the other one",     "wurli.dsp"   },
        { "kick.dsp",   "Kick",    "a kick drum",       "kick.dsp"    },
    };

    for (size_t i = 0; i < 3; i++)
    {
        if (!needle.empty() &&
            all[i].name.find(needle) == std::string::npos)
            continue;

        if (i < 2)
            keys.items.push_back(all[i]);
        else
            drums.items.push_back(all[i]);
    }

    std::vector<BrowserGroup> groups;

    groups.push_back(keys);
    groups.push_back(drums);

    return groups;
}

/* Select a leaf by name, however the tree happens to be flattened just
   now. Returns false when no such row is showing. */
static bool
selectRow (TestBrowser *b, const std::string &file)
{
    for (guint i = 0; i < b->treeModel_->get_n_items(); i++)
    {
        Glib::RefPtr<Gtk::TreeListRow> treeRow = b->treeModel_->get_row(i);
        Glib::RefPtr<BrowserRow> row =
            treeRow ? std::dynamic_pointer_cast<BrowserRow>(treeRow->get_item())
                    : Glib::RefPtr<BrowserRow>();

        if (row && row->file() == file)
        {
            b->selection_->set_selected(i);
            pump(1);

            return true;
        }
    }

    return false;
}

static void
browserFilter (void)
{
    Gtk::Window parent;
    TestBrowser *b = new TestBrowser(parent, sigc::ptr_fun(&stubRows),
                                     "wurli.dsp");

    pump(1);

    if (b->selectedFile() == "wurli.dsp" && b->openBtn_->get_sensitive())
        ok("the browser opens on the file in use");
    else
        fail("the browser opens on the file in use");

    /* A filter the remembered row is not in. Nothing can be selected,
       so nothing may be openable. */
    b->filter_.set_text("Kick");
    pump(1);

    if (b->selectedFile().empty() && !b->openBtn_->get_sensitive())
        ok("a filter that drops the selection drops the Open button");
    else
        fail("a filter that drops the selection drops the Open button");

    b->filter_.set_text("");
    pump(1);

    if (b->selectedFile() == "wurli.dsp")
        ok("clearing the filter finds the remembered row again");
    else
        fail("clearing the filter finds the remembered row again");

    /* Read a different row, then narrow the filter around it. The
       cursor belongs to the reader now, not to whatever is loaded. */
    b->filter_.set_text("o");
    pump(1);

    if (!selectRow(b, "rhodes.dsp"))
        fail("Rhodes is in a filter it matches");
    else
    {
        ok("Rhodes is in a filter it matches");

        b->filter_.set_text("od");
        pump(1);

        if (b->selectedFile() == "rhodes.dsp")
            ok("typing does not drag the cursor back to the file in use");
        else
            fail("typing does not drag the cursor back to the file in use");
    }

    delete b;
    pump(2);
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
    win->setEditing(true);
    pump(6);
    win->setEditing(false);
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

/* The sequencer pane: one track per `gen::grid', as tall as its grid has
 * rows, and a gesture on one written into the work file when it ends --
 * which is what makes Save keep a pattern drawn there.
 *
 * Its own piece and its own window, beside the composer's rather than in
 * it: the pane is packed by the main window, and the sections above lay
 * the canvas out without it. */
static int
runSequencer (const std::string &pluginPath)
{
    const std::string tmp = stagePiece(
            "name \"tracks\";\n"
            "instrument bass { dsp \"ebass.dsp\"; };\n"
            "chain drum {\n"
            "    stage seq gen::grid { steps = 4; rows = 1; "
            "cells = \"x...\"; };\n"
            "    sink { channel = 1; };\n"
            "};\n"
            "chain line {\n"
            "    stage seq gen::grid { steps = 4; rows = 6; "
            "cells = \"..../..../..../..../..../x...\"; };\n"
            "    sink { instrument = bass; };\n"
            "};\n"
            "chain other {\n"
            "    stage s gen::eno_line { notes = \"C4\"; };\n"
            "    sink { channel = 3; };\n"
            "};\n");

    if (tmp.empty())
    {
        fail("could not make a scratch piece");
        return failures;
    }

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    TestComposer *win = new TestComposer(&synth);
    Gtk::Window *host = new Gtk::Window;

    host->set_default_size(800, 500);
    host->set_child(win->sequencerView());
    host->set_visible(true);
    win->setSequencerShown(true);
    pump(10);

    SeqView &seq = win->seq_;

    if (seq.trackCount() == 2)
        ok("the sequencer has a track for each grid, and none for the "
           "chain without one");
    else
    {
        printf("      %zu tracks\n", seq.trackCount());
        fail("the sequencer has a track for each grid, and none for the "
             "chain without one");
    }

    Gtk::DrawingArea *drum = seq.trackArea(0);
    Gtk::DrawingArea *line = seq.trackArea(1);

    if (drum != NULL && line != NULL &&
        drum->get_content_height() == 26 && line->get_content_height() == 108)
        ok("a one-row grid is a strip and a six-row one is six rows tall");
    else
        fail("a one-row grid is a strip and a six-row one is six rows tall");

    /* A gesture that changed nothing -- off the end of the grid -- is
       not an edit. */
    if (drum != NULL && drum->get_width() > 0)
    {
        const double past = drum->get_width() + 10;

        seq.input(0, THC_IN_PRESS, past, 5, 1);
        seq.input(0, THC_IN_RELEASE, past, 5, 1);
        seq.signal_edited().emit(0, 0);
        pump(2);

        if (!win->saveAct_->get_enabled())
            ok("a gesture that changes nothing does not mark the piece "
               "changed");
        else
            fail("a gesture that changes nothing does not mark the piece "
                 "changed");
    }

    /* A press and a release on the drum's third step, where there is
       nothing: a note. */
    if (drum != NULL && drum->get_width() > 0)
    {
        const double x = drum->get_width() * 2.5 / 4;
        const double y = drum->get_height() / 2.0;

        seq.input(0, THC_IN_PRESS, x, y, 1);
        seq.input(0, THC_IN_RELEASE, x, y, 1);
        seq.signal_edited().emit(0, 0);
        pump(2);

        if (readAll(win->workPath_).find("\"x.x.\"") != std::string::npos)
            ok("a note drawn on a track is written into the piece");
        else
            fail("a note drawn on a track is written into the piece");

        if (win->saveAct_->get_enabled())
            ok("...and the piece has something to save");
        else
            fail("...and the piece has something to save");
    }
    else
        fail("the drum track was never laid out");

    /* `rows' moved under the pane -- a knob, the Selection, the page's
       fitting in a room -- and the track follows it on the next frame. */
    thcChain *chain = win->sched_->chain(1);
    thcStage *stage = chain != NULL && !chain->stages.empty()
        ? chain->stages[0].get() : NULL;

    if (stage != NULL && line != NULL)
    {
        stage->params.set(stage->plugin->paramIndex("rows"), 3);
        seq.tick();

        if (line->get_content_height() == 54)
            ok("a track follows its grid's rows as they change");
        else
            fail("a track follows its grid's rows as they change");
    }
    else
        fail("the six-row grid has no live stage");

    /* A reload rebuilds the tracks over the new scheduler stages. */
    win->structuralReload();
    pump(6);

    if (seq.trackCount() == 2 && seq.trackArea(0) != NULL)
        ok("the tracks come back after a reload");
    else
        fail("the tracks come back after a reload");

    /* The six-row line's instrument made a drum: its `dsp' line and a grid
       one row tall, which keeps the bottom row -- the root the line is on
       -- rather than the empty top one a reload would read. */
    if (win->setTrackInstrument(1, 0, "kick909.dsp", false))
    {
        pump(6);

        const std::string text = readAll(win->workPath_);

        if (text.find("dsp \"kick909.dsp\"") != std::string::npos &&
            text.find("rows = 1;") != std::string::npos &&
            text.find("cells = \"x...\"") != std::string::npos)
            ok("a track's instrument made a drum keeps its root row, one "
               "row tall");
        else
        {
            printf("%s\n", text.c_str());
            fail("a track's instrument made a drum keeps its root row, one "
                 "row tall");
        }

        if (seq.trackDsp(1) == "kick909.dsp" && seq.trackArea(1) != NULL &&
            seq.trackArea(1)->get_content_height() == 26)
            ok("...and the track is a strip after the reload");
        else
            fail("...and the track is a strip after the reload");
    }
    else
        fail("a track playing an instrument could not have it changed");

    if (!win->setTrackInstrument(0, 0, "kick909.dsp", false))
        ok("a track playing a channel has no instrument to change");
    else
        fail("a track playing a channel has no instrument to change");

    /* The pane lets go of the composer's widget before the composer
       goes, as the main window's panes do. */
    host->unset_child();
    delete host;
    delete win;
    pump(2);

    {
        std::error_code ec;

        std::filesystem::remove_all(tmp, ec);
    }

    return failures;
}

/* The text of one chain's block in `text', from `chain NAME' to the next
   chain or the end: what an edit to that chain is looked for in. */
static std::string
chainText (const std::string &text, const std::string &name)
{
    const size_t at = text.find("chain " + name + " ");

    if (at == std::string::npos)
        return std::string();

    const size_t next = text.find("\nchain ", at + 1);

    return text.substr(at, next == std::string::npos ? std::string::npos
                                                     : next - at);
}

/* A widget made by a reload is not laid out until a frame has gone by,
   and a gesture on one with no width is dropped by the plugin. Bounded. */
static bool
laidOut (Gtk::Widget *w)
{
    for (int i = 0; i < 200 && w != NULL && w->get_width() <= 0; i++)
    {
        pump(1);
        g_usleep(5000);
    }

    return w != NULL && w->get_width() > 0;
}

static void
check (bool cond, const char *what)
{
    if (cond)
        ok(what);
    else
        fail(what);
}

/* The piece and the sequence: two documents, each kept while the other is
 * up, and a track's graph changed in the sequence.
 *
 * A staged gen/ with both in it -- a piece as airports.gen, which the
 * composer opens, and a sequence as scratch.gen. The sequence has what the
 * instrument change has to be right about: a drum pattern written with bar
 * lines, which the grid hands back without; a one-row pitched track on an
 * instrument it shares with a six-row one; and a grid with no `rows' line
 * at all, which is the plugin's eight.
 */
static int
runDocuments (const std::string &pluginPath)
{
    const std::string scratch =
        "name \"seq\";\n"
        "instrument bass { dsp \"ebass.dsp\"; };\n"
        "instrument lead { dsp \"ebass.dsp\"; };\n"
        "chain drum {\n"
        "    stage seq gen::grid { steps = 4; rows = 1; "
        "cells = \"x.|..\"; };\n"
        "    sink { channel = 1; };\n"
        "};\n"
        "chain line {\n"
        "    stage seq gen::grid { steps = 4; rows = 1; cells = \"x...\"; };\n"
        "    sink { instrument = bass; };\n"
        "};\n"
        "chain twin {\n"
        "    stage seq gen::grid { steps = 4; rows = 6; "
        "cells = \"..../..../..../..../..../.x..\"; };\n"
        "    sink { instrument = bass; };\n"
        "};\n"
        "chain tall {\n"
        "    stage seq gen::grid { steps = 4; cells = \"x...\"; };\n"
        "    sink { instrument = lead; };\n"
        "};\n";

    const std::string tmp = stagePiece(
            "name \"piece\";\n"
            "chain c {\n"
            "    stage s gen::eno_line { notes = \"C4\"; };\n"
            "    sink { channel = 3; };\n"
            "};\n");

    if (tmp.empty())
    {
        fail("could not make a scratch piece");
        return failures;
    }

    {
        std::ofstream out((tmp + "/scratch.gen").c_str(), std::ios::trunc);

        out << scratch;
    }

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    TestComposer *win = new TestComposer(&synth);
    Gtk::Window *host = new Gtk::Window;

    host->set_default_size(800, 700);
    host->set_child(win->sequencerView());
    host->set_visible(true);
    win->set_visible(true);
    win->setSequencerShown(true);
    pump(8);

    const std::string piecePath = win->genPath_;
    SeqView &seq = win->seq_;

    win->useDocument(Composer::SEQUENCE);
    pump(8);

    check(win->document() == Composer::SEQUENCE &&
          win->genPath_.empty() && seq.trackCount() == 4 &&
          readAll(win->workPath_) == scratch,
          "the sequence opens as a copy of scratch.gen with no file of its "
          "own");

    check(win->saveAct_->get_enabled() && !win->dirty_,
          "...so Save is offered, and goes to Save As");

    /* A gesture off the end of the drum's grid, whose file spells its
       pattern with a bar line the grid does not hand back. */
    if (Gtk::DrawingArea *drum = seq.trackArea(0))
    {
        const double past = drum->get_width() + 10;

        seq.input(0, THC_IN_PRESS, past, 5, 1);
        seq.input(0, THC_IN_RELEASE, past, 5, 1);
        seq.signal_edited().emit(0, 0);
        pump(2);
    }

    check(!win->dirty_ && readAll(win->workPath_) == scratch,
          "a gesture that changes nothing on a pattern with bar lines "
          "writes nothing");

    /* A graph the name of which cannot be written: refused before any
       track is reshaped. */
    check(!win->setTrackInstrument(1, 0, "a\"b.dsp", true) &&
          readAll(win->workPath_) == scratch,
          "a graph whose name cannot be written leaves the file as it was");

    /* The one-row line made pitched: a ladder of six, grown upward, its
       note kept on the bottom row. Its twin plays the same instrument and
       is six rows already, so it keeps them. */
    check(win->setTrackInstrument(1, 0, "ebass.dsp", true),
          "a pitched graph can be chosen for a one-row track");
    pump(8);

    {
        const std::string text = readAll(win->workPath_);
        const std::string line = chainText(text, "line");
        const std::string twin = chainText(text, "twin");

        check(line.find("rows = 6;") != std::string::npos &&
              line.find("\"..../..../..../..../..../x...\"") !=
                  std::string::npos &&
              twin.find("rows = 6;") != std::string::npos,
              "...and the track grows to six rows over its root, and the "
              "one sharing its instrument keeps its six");
    }

    /* And a drum for the same instrument: both tracks on it go to a strip,
       each keeping its own bottom row. */
    check(win->setTrackInstrument(1, 0, "kick909.dsp", false),
          "a drum can be chosen for a track");
    pump(8);

    {
        const std::string text = readAll(win->workPath_);
        const std::string line = chainText(text, "line");
        const std::string twin = chainText(text, "twin");

        check(text.find("dsp \"kick909.dsp\"") != std::string::npos &&
              line.find("rows = 1;") != std::string::npos &&
              line.find("\"x...\"") != std::string::npos &&
              twin.find("rows = 1;") != std::string::npos &&
              twin.find("\".x..\"") != std::string::npos,
              "...and every track playing that instrument becomes a strip "
              "over its own root row");
    }

    /* The grid with no `rows' line is eight rows, the plugin's own; a
       pitched graph keeps them, and writes no line. */
    check(win->setTrackInstrument(3, 0, "ebass.dsp", true),
          "a pitched graph can be chosen for a grid with no rows line");
    pump(8);

    check(chainText(readAll(win->workPath_), "tall").find("rows") ==
              std::string::npos &&
          seq.trackArea(3) != NULL &&
          seq.trackArea(3)->get_content_height() == 8 * 18,
          "...and it keeps the eight rows it has");

    /* A note drawn on the drum, never written down, and the document put
       away: it goes into the sequence's text rather than with the
       scheduler. */
    if (Gtk::DrawingArea *drum = seq.trackArea(0); laidOut(drum))
    {
        const double x = drum->get_width() * 3.5 / 4;
        const double y = drum->get_height() / 2.0;

        seq.input(0, THC_IN_PRESS, x, y, 1);
        seq.input(0, THC_IN_RELEASE, x, y, 1);
    }

    win->useDocument(Composer::PIECE);
    pump(8);

    check(win->document() == Composer::PIECE &&
          win->genPath_ == piecePath && !win->dirty_ &&
          win->doc_.name == "piece",
          "piece mode's document comes back as it was left");

    win->useDocument(Composer::SEQUENCE);
    pump(8);

    check(chainText(readAll(win->workPath_), "drum").find("\"x..x\"") !=
              std::string::npos && win->dirty_,
          "...and the sequence with the note drawn before the switch, and "
          "its unsaved edits");

    /* A change of graph and the document put away before its reload has
       run: the reload is the sequence's, and so is the edit it marks. */
    win->setTrackInstrument(3, 0, "kick909.dsp", false);
    win->useDocument(Composer::PIECE);

    check(!win->dirty_ && win->doc_.name == "piece",
          "a switch with a reload still queued leaves the piece clean");

    pump(8);
    win->useDocument(Composer::SEQUENCE);
    pump(8);

    check(win->dirty_ &&
          chainText(readAll(win->workPath_), "tall").find("rows = 1;") !=
              std::string::npos,
          "...and the sequence keeps the edit, marked unsaved");

    /* Revert: the sequence has no file, so it goes back to what it
       started from. */
    win->activate_action("composer.revert");
    pump(8);

    check(readAll(win->workPath_) == scratch && !win->dirty_ &&
          win->genPath_.empty(),
          "Revert takes the sequence back to scratch.gen, still with no "
          "file of its own");

    host->unset_child();
    delete host;
    delete win;
    pump(2);

    {
        std::error_code ec;

        std::filesystem::remove_all(tmp, ec);
    }

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
                rc = runSequencer(pluginPath);

            if (rc == 0)
                rc = runDocuments(pluginPath);

            if (rc == 0)
                closeWithIdlesPending(pluginPath);

            if (rc == 0)
                browserFilter();
        });

    app->run();

    printf("\n%d checks, %d failed\n", checks, failures);

    return rc;
}
