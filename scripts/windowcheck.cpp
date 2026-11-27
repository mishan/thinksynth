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
 * the patch list, the MIDI routing and the piece's five, as panes
 * (src/gui/Panes.h), in three modes: patch, piece and sequence. This
 * builds it the way main() does, with a patch on a channel, and asks what
 * a person would see: which panes are up the first time, which channel
 * they are about, that each mode has its own panes and its own layout,
 * that the View menu's ticks put panes up and take them down, that the
 * layouts are kept in panes.ini and read back, and that the window comes
 * down cleanly with an editor built in it.
 *
 * Needs a display, and skips itself without one, as editorcheck does.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <chrono>
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
#include "gui/Keyboard.h"
#include "gui/MainSynthWindow.h"
#include "gui/NodeCanvasWidget.h"
#include "gui/NodeEditor.h"
#include "gui/Panes.h"
#include "gui/PanelView.h"
#include "gui/KeyboardPanel.h"
#include "gui/SeqView.h"

/* The application-wide signals gthSignal.h declares, defined here because
 * main.cpp defines them there and this harness is not main.cpp. */
sigNoteOn    m_sigNoteOn;
sigNoteOff   m_sigNoteOff;
sigNoteClear m_sigNoteClear;
sigNoteOn    m_sigKbdNoteOn;
sigNoteOff   m_sigKbdNoteOff;

/* A subclass, for the reason composercheck has one: what the window keeps
 * is its own business, and there is no call to widen it for a test. */
/* The composer's scheduler, which the window does not hand out. */
class ComposerPeek : public Composer {
public:
    using Composer::sched_;
    using Composer::loadPiece;
};

class TestWindow : public MainSynthWindow {
public:
    TestWindow (void) : MainSynthWindow(NULL) { }

    using MainSynthWindow::panes_;
    using MainSynthWindow::chanList_;
    using MainSynthWindow::editors_;
    using MainSynthWindow::chan_;
    using MainSynthWindow::actions_;
    using MainSynthWindow::composer_;
    using MainSynthWindow::paramStack_;
    using MainSynthWindow::selectChannel;
    using MainSynthWindow::writeLayout;
    using MainSynthWindow::layoutPath;
    using MainSynthWindow::setDesktopMode;
    using MainSynthWindow::mode_;
    using MainSynthWindow::rowChans_;
    using MainSynthWindow::addChanBtn_;
    using MainSynthWindow::firstFreeChannel;
    using MainSynthWindow::onBrowseChosen;
    using MainSynthWindow::kbPanel_;
    using MainSynthWindow::keyboardChannel;
    using MainSynthWindow::setChannelTuning;

    /* The page's questions, answered here: what each would pick, and a
       count of the discard prompts with the last one's text. A held DSP
       answer waits in `held' until the test gives it. */
    std::string patchAnswer, dspAnswer;
    bool holdDsp = false;
    sigc::slot<void (std::string)> held;
    bool discardAnswer = false, holdDiscard = false;
    int discardAsked = 0;
    std::string discardWhat;
    sigc::slot<void ()> heldDiscard;

    void askPatchFile (sigc::slot<void (std::string)> chosen) override
    {
        chosen(patchAnswer);
    }

    void askDsp (int, sigc::slot<void (std::string)> chosen) override
    {
        if (holdDsp)
            held = chosen;
        else
            chosen(dspAnswer);
    }

    std::string tuningAnswer;
    bool holdTuning = false;
    sigc::slot<void (std::string)> heldTuning;

    void askTuningFile (bool, sigc::slot<void (std::string)> chosen) override
    {
        if (holdTuning)
            heldTuning = chosen;
        else
            chosen(tuningAnswer);
    }

    void askDiscard (const std::string &what,
                     sigc::slot<void ()> discard) override
    {
        discardAsked++;
        discardWhat = what;

        if (holdDiscard)
            heldDiscard = discard;
        else if (discardAnswer)
            discard();
    }
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

/* The graph's canvas, somewhere inside its editor. */
static NodeCanvasWidget *
findCanvas (Gtk::Widget *w)
{
    if (NodeCanvasWidget *c = dynamic_cast<NodeCanvasWidget *>(w))
        return c;

    for (Gtk::Widget *k = w->get_first_child(); k != NULL;
         k = k->get_next_sibling())
        if (NodeCanvasWidget *c = findCanvas(k))
            return c;

    return NULL;
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

/* The title bar's toggles and the menu's items, which are one
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

/* The controls showing under `w' that a screen reader would read with no
   name, those Tab cannot reach, and the rows Tab stops on before their
   control, by type and tooltip. A check button
   with text, like any button, is named by it. Only the window's own: a
   widget gtkmm made has a gtkmm__ type, and the parts GTK builds inside
   one (a spin button's own buttons) are GTK's to name. */
static void
inaccessible (Gtk::Widget *w, std::vector<std::string> &unnamed,
              std::vector<std::string> &unreachable,
              std::vector<std::string> &doubled, bool canFocus = true)
{
    for (Gtk::Widget *c = w->get_first_child(); c != NULL;
         c = c->get_next_sibling())
    {
        if (!c->get_mapped())
            continue;

        GtkAccessible *a = GTK_ACCESSIBLE(c->gobj());
        Gtk::CheckButton *check = dynamic_cast<Gtk::CheckButton *>(c);
        const bool ours = g_str_has_prefix(G_OBJECT_TYPE_NAME(c->gobj()),
                                           "gtkmm__");
        const bool control = ours && ( dynamic_cast<Gtk::Range *>(c) != NULL ||
                             dynamic_cast<Gtk::SpinButton *>(c) != NULL ||
                             dynamic_cast<Gtk::DropDown *>(c) != NULL ||
                             dynamic_cast<Gtk::Entry *>(c) != NULL ||
                             (check != NULL && check->get_label().empty()));
        const std::string what = std::string(G_OBJECT_TYPE_NAME(c->gobj())) +
                                 " \"" + c->get_tooltip_text() + "\"";

        if (control &&
            !gtk_test_accessible_has_property(a, GTK_ACCESSIBLE_PROPERTY_LABEL) &&
            !gtk_test_accessible_has_relation(
                a, GTK_ACCESSIBLE_RELATION_LABELLED_BY))
            unnamed.push_back(what);

        /* can-focus off anywhere above shuts out everything under it. */
        const bool reaches = canFocus && c->get_can_focus();
        Gtk::Button *button = dynamic_cast<Gtk::Button *>(c);

        if ((control || (ours && button != NULL)) && c->is_sensitive() &&
            (!reaches || (button != NULL && !button->get_focusable())))
            unreachable.push_back(what);

        if (reaches && c->get_focusable() &&
            dynamic_cast<Gtk::FlowBoxChild *>(c) != NULL)
            doubled.push_back(what);

        inaccessible(c, unnamed, unreachable, doubled, reaches);
    }
}

/* The first widget of type T under `w', or NULL. */
template <typename T>
static T *
findUnder (Gtk::Widget *w)
{
    for (Gtk::Widget *c = w->get_first_child(); c != NULL;
         c = c->get_next_sibling())
    {
        if (T *t = dynamic_cast<T *>(c))
            return t;

        if (T *t = findUnder<T>(c))
            return t;
    }

    return NULL;
}

/* Every widget of type T under `w'. */
template <typename T>
static void
collectUnder (Gtk::Widget *w, std::vector<T *> &out)
{
    for (Gtk::Widget *c = w->get_first_child(); c != NULL;
         c = c->get_next_sibling())
    {
        if (T *t = dynamic_cast<T *>(c))
            out.push_back(t);

        collectUnder<T>(c, out);
    }
}

/* Whether `w''s description is `want'. */
static bool
described (Gtk::Widget *w, const char *want)
{
    char *diff = gtk_test_accessible_check_property(
        GTK_ACCESSIBLE(w->gobj()), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, want);

    const bool same = diff == NULL;

    g_free(diff);

    return same;
}

/* In every parameter panel showing under `root': a number box beside a
   slider has no - and +, since the slider and the arrow keys step it; one
   with no slider keeps them, its only way to step by pointer. */
static void
checkNumberBoxes (Gtk::Widget *root, const char *where)
{
    std::vector<PanelView *> panels;
    std::vector<Gtk::SpinButton *> boxes;
    int beside = 0, wrong = 0;

    collectUnder<PanelView>(root, panels);

    for (PanelView *p : panels)
        if (p->get_mapped())
            collectUnder<Gtk::SpinButton>(p, boxes);

    for (Gtk::SpinButton *box : boxes)
    {
        bool slider = false, buttons = false;

        for (Gtk::Widget *w = box->get_parent()->get_first_child(); w != NULL;
             w = w->get_next_sibling())
            slider = slider || dynamic_cast<Gtk::Scale *>(w) != NULL;

        for (Gtk::Widget *c = box->get_first_child(); c != NULL;
             c = c->get_next_sibling())
            buttons = buttons || (GTK_IS_BUTTON(c->gobj()) && c->get_visible());

        beside += slider;
        wrong += slider == buttons;
    }

    check(beside > 0 && wrong == 0,
          (std::string("a number box in ") + where +
           " has - and + only where there is no slider beside it").c_str());
}

/* Every pane agrees that `c' is the current channel: the window, Patch
   params' page, the keys and what the HID controller plays, and Channels'
   row (none for an empty channel). Says which disagreed. */
static std::string
disagrees (TestWindow *win, int c)
{
    std::string why;
    Gtk::ListBoxRow *row = rowFor(win, c);

    if (win->chan_ != c)
        why += " window " + std::to_string(win->chan_);
    if (std::string(win->paramStack_.get_visible_child_name()) !=
        "ch" + std::to_string(c))
        why += " page " + std::string(win->paramStack_.get_visible_child_name());
    if (win->kbPanel_->channel() != c || win->keyboardChannel() != c)
        why += " keys " + std::to_string(win->kbPanel_->channel());
    if (win->chanList_.get_selected_row() != row)
        why += " Channels";

    /* The tracks on it are marked, in the heading and to a screen reader,
       and no others. */
    const SeqView &seq = win->composer_->sequencer();

    for (size_t i = 0; i < seq.trackCount(); i++)
    {
        const bool on = seq.trackChannel(i) >= 0 && seq.trackChannel(i) == c;
        Gtk::Widget *head = seq.trackArea(i)->get_parent()->get_first_child();
        Gtk::Label *num = dynamic_cast<Gtk::Label *>(head->get_first_child());
        const bool shown = num != NULL &&
                           num->get_text().find("\xe2\x96\xb8") == 0;

        if (shown != on ||
            !described(seq.trackArea(i), SeqView::describeTrack(on).c_str()))
            why += " track " + std::to_string(i + 1);
    }

    return why;
}

static void
checkAgree (TestWindow *win, int c, const std::string &what)
{
    const std::string why = disagrees(win, c);

    check(why.empty(), (what + ": every pane on channel " +
                        std::to_string(c + 1) +
                        (why.empty() ? "" : " (not:" + why + ")")).c_str());
}

/* The Keys stepper turned to channel `c' (0-based) as a person turns it:
   through its spin button. */
static void
turnKeys (TestWindow *win, int c)
{
    Gtk::SpinButton *spin = findUnder<Gtk::SpinButton>(win->kbPanel_);

    if (spin != NULL)
        spin->set_value(c + 1);

    pump(4);
}

/* That check, said: the first few offenders by name. */
static void
checkAccessible (TestWindow *win, const char *where)
{
    std::vector<std::string> unnamed, unreachable, doubled;

    inaccessible(win, unnamed, unreachable, doubled);

    for (const auto &[list, what] :
         { std::pair(&unnamed, "has a name"),
           std::pair(&unreachable, "can be reached by Tab"),
           std::pair(&doubled, "is one Tab stop, not its row and it") })
    {
        std::string msg = std::string("every control in ") + where + " " +
                          what;

        for (size_t i = 0; i < list->size() && i < 5; i++)
            msg += (i == 0 ? ": not " : ", ") + (*list)[i];

        if (list->size() > 5)
            msg += ", and " + std::to_string(list->size() - 5) + " more";

        check(list->empty(), msg.c_str());
    }
}

/* The page Patch params has for `chan'. */
static Gtk::Widget *
pageFor (TestWindow *win, int chan)
{
    return win->paramStack_.get_child_by_name("ch" + std::to_string(chan));
}

/* The widget named `name' under `w', or NULL. */
static Gtk::Widget *
namedUnder (Gtk::Widget *w, const char *name)
{
    for (Gtk::Widget *c = w->get_first_child(); c != NULL;
         c = c->get_next_sibling())
    {
        if (c->get_name() == name)
            return c;

        if (Gtk::Widget *n = namedUnder(c, name))
            return n;
    }

    return NULL;
}

/* The text of every label under `w', run together. */
static std::string
labelsUnder (Gtk::Widget *w)
{
    std::vector<Gtk::Label *> labels;
    std::string all;

    collectUnder<Gtk::Label>(w, labels);

    for (Gtk::Label *l : labels)
        all += std::string(l->get_text()) + "\n";

    return all;
}

/* Whether `chan''s page has its strip and the strip says `what'. */
static bool
stripSays (TestWindow *win, int chan, const std::string &what)
{
    Gtk::Widget *page = pageFor(win, chan);
    Gtk::Widget *strip = page != NULL ? namedUnder(page, "patchstrip") : NULL;

    return strip != NULL && labelsUnder(strip).find(what) != std::string::npos;
}

/* The button on `chan''s page labeled `label' (with its underline), menus
   included, or NULL. */
static Gtk::Button *
pageButton (TestWindow *win, int chan, const std::string &label)
{
    Gtk::Widget *page = pageFor(win, chan);
    std::vector<Gtk::Button *> buttons;

    if (page != NULL)
        collectUnder<Gtk::Button>(page, buttons);

    for (Gtk::Button *b : buttons)
        if (std::string(b->get_label()) == label)
            return b;

    return NULL;
}

/* Clicks `label' on `chan''s page, as a person would; false if it is not
   there or not clickable. */
static bool
press (TestWindow *win, int chan, const std::string &label)
{
    Gtk::Button *b = pageButton(win, chan, label);

    if (b == NULL || !b->is_sensitive())
        return false;

    g_signal_emit_by_name(b->gobj(), "clicked");
    pump(4);

    return true;
}

static float
argValue (int chan, const char *name)
{
    thArg *a = thSynth::instance()->getChanArg(chan, name);

    return a != NULL ? (*a)[0] : -1;
}

/* An edit as the panel makes one: the value, and the patch marked. */
static void
edit (int chan, const char *name, float value)
{
    thSynth::instance()->getChanArg(chan, name)->setValue(value);
    gthPatchManager::instance()->markDirty(chan);
}

static unsigned
generationOf (int chan)
{
    gthPatchManager::PatchFile *p = gthPatchManager::instance()->getPatch(chan);

    return p != NULL ? p->generation : 0;
}

/* The patch page's strip, and the three ways it replaces a channel's
   patch. Driven on channel 3 (index 2) while another channel is selected,
   so a control that acted on the selected channel instead fails. */
static void
checkPatchPage (TestWindow *win, const std::string &dsp)
{
    gthPatchManager *patchMgr = gthPatchManager::instance();
    const std::string dspName = std::filesystem::path(dsp).filename().string();
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("windowcheck-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::string saved = (dir / "kept.patch").string();

    std::filesystem::create_directories(dir);

    /* Every kind of page has the strip: a patch, and an empty channel. */
    check(stripSays(win, 2, "3: ") && stripSays(win, 2, dspName),
          "a patch page's strip names its channel and its graph");
    check(stripSays(win, 3, "4: ") && pageButton(win, 3, "_Open patch...") &&
          pageButton(win, 3, "_Open patch...")->is_sensitive() &&
          pageButton(win, 3, "_Save patch") != NULL &&
          !pageButton(win, 3, "_Save patch")->is_sensitive(),
          "...and an empty channel's: Open offered, Save not");
    win->selectChannel(3);
    pump(4);
    checkAccessible(win, "an empty channel's page");

    /* A graph with no settings of its own: the strip, and a page that says
       so rather than an empty panel. */
    {
        const std::string bare = (dir / "bare.dsp").string();
        std::ofstream out(bare);

        out << "node ionode { out0 = osc->out; out1 = osc->out; "
               "channels = 2; play = 1; };\n"
               "node osc osc::simple { freq = 440; };\n"
               "io ionode;\n";
        out.close();

        patchMgr->newPatch(bare, 3);
        pump(4);

        check(stripSays(win, 3, "4: ") &&
                  labelsUnder(pageFor(win, 3)).find("no settings") !=
                      std::string::npos &&
                  labelsUnder(pageFor(win, 2)).find("no settings") ==
                      std::string::npos,
              "a graph with no settings gets the strip and says so");

        patchMgr->unloadPatch(3);
        pump(4);
    }

    win->selectChannel(0);
    pump(4);

    /* Revert: the file's values back, and nothing left to save. */
    check(pageButton(win, 2, "Re_vert patch") != NULL &&
          !pageButton(win, 2, "Re_vert patch")->is_sensitive(),
          "Revert is greyed on a patch never saved");

    patchMgr->savePatch(saved, 2);
    pump(4);

    const float before = argValue(2, "pw1");

    edit(2, "pw1", before + 0.25f);
    pump(4);
    win->discardAsked = 0;
    win->discardAnswer = false;

    const bool asked = press(win, 2, "Re_vert patch") &&
                       win->discardAsked == 1;

    check(asked && argValue(2, "pw1") == before + 0.25f &&
              patchMgr->isDirty(2),
          "Revert on an edited patch asks first, and Cancel keeps the edit");

    win->discardAnswer = true;
    press(win, 2, "Re_vert patch");
    pump(4);

    check(win->discardAsked == 2 && argValue(2, "pw1") == before &&
              !patchMgr->isDirty(2) && win->chan_ == 0,
          "...and Discard puts the file's values back, on its own channel");

    /* Open: the chosen file on this page's channel, named in the strip. */
    std::filesystem::copy_file(saved, dir / "other.patch",
                               std::filesystem::copy_options::overwrite_existing);
    win->patchAnswer = (dir / "other.patch").string();
    win->discardAsked = 0;

    check(press(win, 2, "_Open patch...") && win->discardAsked == 0 &&
              stripSays(win, 2, "other.patch") &&
              !stripSays(win, 0, "other.patch"),
          "Open patch... loads onto its page's channel, unasked when clean");

    /* Change DSP: onto this page's channel, graph and all, asked when
       edited. */
    edit(2, "pw1", before + 0.5f);
    win->dspAnswer = dsp;
    win->discardAnswer = false;
    win->discardAsked = 0;

    const unsigned gen = generationOf(2), gen0 = generationOf(0);

    check(press(win, 2, "Change _DSP...") && win->discardAsked == 1 &&
              generationOf(2) == gen && argValue(2, "pw1") == before + 0.5f,
          "Change DSP... on an edited patch asks, and Cancel keeps it");

    win->discardAnswer = true;
    press(win, 2, "Change _DSP...");
    pump(4);

    check(generationOf(2) != gen && stripSays(win, 2, "(unsaved)") &&
              generationOf(0) == gen0 && win->chan_ == 2,
          "...and Discard puts the graph on its own channel, and picks it");

    /* An answer that comes after the channel was replaced by something else
       replaces nothing. */
    win->holdDsp = true;
    press(win, 2, "Change _DSP...");
    patchMgr->newPatch(dsp, 2);
    pump(4);

    const unsigned replaced = generationOf(2);
    const int askedBefore = win->discardAsked;

    win->holdDsp = false;

    if (win->held)
        win->held(dsp);

    pump(4);

    check(win->held && generationOf(2) == replaced &&
              win->discardAsked == askedBefore,
          "an answer for a channel since replaced is dropped, unasked");
    win->held = sigc::slot<void (std::string)>();

    /* The same for a discard answered after the channel moved on. */
    edit(2, "pw1", before + 0.5f);
    win->holdDiscard = true;
    press(win, 2, "Change _DSP...");
    patchMgr->newPatch(dsp, 2);
    pump(4);

    const unsigned again = generationOf(2);

    win->holdDiscard = false;

    if (win->heldDiscard)
        win->heldDiscard();

    pump(4);

    check(win->heldDiscard && generationOf(2) == again,
          "...and so is a discard for one");
    win->heldDiscard = sigc::slot<void ()>();

    /* Graph edits and a replace, every way round: each action, its load
       succeeding or failing, with the channel's edited graph on screen or
       held across a rebuild, and editing its own copy of the graph (as after
       its Save As). A replace that happened leaves no editor showing the
       old edits; one that failed leaves the same editor, edits and all. */
    struct Case { const char *action; bool works; bool held; bool copy; };
    const Case cases[] = {
        { "Change _DSP...", true, false }, { "Change _DSP...", true, true },
        { "Change _DSP...", false, false }, { "Change _DSP...", false, true },
        { "_Open patch...", true, false }, { "_Open patch...", true, true },
        { "_Open patch...", false, false }, { "_Open patch...", false, true },
        { "Re_vert patch", true, false }, { "Re_vert patch", true, true },
        { "Re_vert patch", false, false }, { "Re_vert patch", false, true },
        { "Change _DSP...", false, false, true },
        { "Change _DSP...", false, true, true },
        { "_Open patch...", false, false, true },
        { "_Open patch...", false, true, true },
        { "Re_vert patch", false, false, true },
        { "Re_vert patch", false, true, true },
    };
    const std::string copy = (dir / "copy.dsp").string();

    std::filesystem::copy_file(dsp, copy,
                               std::filesystem::copy_options::overwrite_existing);

    win->discardAnswer = true;

    for (const Case &c : cases)
    {
        const std::string what = std::string(c.action) +
                                 (c.works ? " loading" : " failing") +
                                 (c.held ? ", its graph held" : ", its graph shown") +
                                 (c.copy ? " on a copy" : "");

        patchMgr->newPatch(dsp, 2);
        patchMgr->savePatch(saved, 2);
        win->selectChannel(2);
        pump(4);

        NodeEditor *graph = win->editors_.count(2) ? win->editors_[2] : NULL;

        if (graph != NULL && c.copy)
            graph->open(copy, -1);

        pump(4);

        NodeCanvasWidget *canvas = graph != NULL ? findCanvas(graph) : NULL;

        if (canvas != NULL && canvas->graph() != NULL &&
            !canvas->graph()->boxes().empty())
        {
            const double x = canvas->graph()->boxes()[0].x + 6;
            const double y = canvas->graph()->boxes()[0].y + 6;

            canvas->onPressed(1, x, y);
            canvas->onMotion(x + 40, y);
            canvas->onReleased(1, x + 40, y);
        }

        if (graph == NULL || !graph->dirty())
        {
            fail((what + ": the graph could not be edited").c_str());
            continue;
        }

        if (c.held)
        {
            win->selectChannel(0);
            patchMgr->newPatch(dsp, 5);
            pump(4);
        }

        win->dspAnswer = c.works ? dsp : (dir / "missing.dsp").string();
        win->patchAnswer = c.works ? saved : (dir / "missing.patch").string();

        if (!c.works)
            std::filesystem::remove(saved);

        win->discardAsked = 0;

        const unsigned gen = generationOf(2);
        const bool pressed = press(win, 2, c.action);

        pump(4);
        win->selectChannel(2);
        pump(4);

        const bool replaced = generationOf(2) != gen;
        NodeEditor *now = win->editors_.count(2) ? win->editors_[2] : NULL;

        check(pressed && win->discardAsked == 1 &&
                  win->discardWhat.find("graph") != std::string::npos &&
                  replaced == c.works &&
                  (c.works ? now != NULL && !now->dirty()
                           : now == graph && graph->dirty()),
              (what + ": " + (c.works ? "the edits go with the old patch"
                                      : "nothing replaced, the edits stay"))
                  .c_str());
    }

    std::filesystem::remove_all(dir);
}

static std::string readAll (const std::string &path);

/* The note key `key' sounds at on `chan', or -1 with no channel. */
static double
soundsAt (int chan, int key)
{
    thMidiChan *mc = thSynth::instance()->getChannel(chan);

    if (mc == NULL)
        return -1;

    return mc->tuning() != NULL ? mc->tuning()->pitch(key) : key;
}

/* The tuning menu on a patch's strip: a scale and a keyboard map read
   from files, taken off again, and one that will not build refused. On
   channel 3 (index 2), its knob values kept through every change. */
static void
checkTuning (TestWindow *win, const std::string &dsp)
{
    gthPatchManager *patchMgr = gthPatchManager::instance();
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("windowcheck-tuning-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const double step = 12.0 / 19;

    std::filesystem::create_directories(dir);

    {
        std::ofstream scl(dir / "edo19.scl");

        scl << "19-EDO test\n19\n";

        /* snprintf, not <<: the stream takes the desktop's locale, and
           "1,010.53" is not a number of cents. */
        for (int i = 1; i < 19; i++)
        {
            char cents[32];

            snprintf(cents, sizeof cents, "%.12f\n", 1200.0 * i / 19);
            scl << cents;
        }

        scl << "2/1\n";
    }

    std::ofstream(dir / "gaps.kbm")
        << "2\n0\n127\n60\n60\n261.6255653005986\n1\n0\nx\n";
    std::ofstream(dir / "bad.scl") << "not a scale\nseven\n";

    patchMgr->newPatch(dsp, 2);
    pump(4);
    edit(2, "pw1", 0.42f);
    pump(4);

    win->tuningAnswer = (dir / "edo19.scl").string();

    const unsigned loaded = patchMgr->getPatch(2)->generation;
    const bool opened = press(win, 2, "Open _scale (.scl)...");

    pump(4);

    check(opened && stripSays(win, 2, "Tuning: 19-EDO test") &&
          std::fabs(soundsAt(2, 61) - (60 + step)) < 1e-9 &&
          argValue(2, "pw1") == 0.42f && patchMgr->isDirty(2),
          "a scale opened from the strip tunes its channel and keeps its "
          "values");
    check(patchMgr->getPatch(2)->generation == loaded,
          "...on the channel as it stands, not a reload of it");

    const std::string saved = (dir / "tuned.patch").string();

    patchMgr->savePatch(saved, 2);
    check(readAll(saved).find("tuning scale ") != std::string::npos,
          "...and is saved with the patch");

    win->tuningAnswer = (dir / "gaps.kbm").string();
    press(win, 2, "Open _keyboard map (.kbm)...");
    pump(4);

    const double mapped = soundsAt(2, 61);

    press(win, 2, "_No keyboard map");
    pump(4);

    check(mapped == 61 && std::fabs(soundsAt(2, 61) - (60 + step)) < 1e-9,
          "a keyboard map leaves out the keys it names, and goes again");

    const unsigned before = patchMgr->getPatch(2)->generation;

    win->tuningAnswer = (dir / "bad.scl").string();
    press(win, 2, "Open _scale (.scl)...");
    pump(4);

    std::string err;
    const bool took = patchMgr->setTuning(2, { "odd", "100.0 abc", "" }, err);

    check(!took && !err.empty() &&
          patchMgr->getPatch(2)->generation == before &&
          std::fabs(soundsAt(2, 61) - (60 + step)) < 1e-9,
          "a scale that will not read, or values that will not build, "
          "change nothing");

    /* An answer for a patch since replaced on the channel is dropped. */
    win->tuningAnswer = (dir / "edo19.scl").string();
    win->holdTuning = true;
    press(win, 2, "Open _scale (.scl)...");
    pump(4);
    win->holdTuning = false;
    patchMgr->newPatch(dsp, 2);
    pump(4);

    if (win->heldTuning)
        win->heldTuning(win->tuningAnswer);

    pump(4);

    check(win->heldTuning && soundsAt(2, 61) == 61 &&
          patchMgr->getPatch(2)->doc.tuningScale.empty(),
          "a scale chosen for a patch since replaced is not put on the new "
          "one");
    win->heldTuning = sigc::slot<void (std::string)>();

    /* ...and so is one answered after the current channel or the mode
       moved, even back again: it was asked of what was in view. */
    {
        const char *const acts[] = { "another channel and back",
                                     "another mode and back" };

        for (const char *act : acts)
        {
            win->selectChannel(2);
            win->tuningAnswer = (dir / "edo19.scl").string();
            win->holdTuning = true;
            press(win, 2, "Open _scale (.scl)...");
            pump(4);
            win->holdTuning = false;

            if (act == acts[0])
            {
                win->selectChannel(0);
                win->selectChannel(2);
            }
            else
            {
                pickMode(win, "piece");
                pickMode(win, "patch");
            }

            if (win->heldTuning)
                win->heldTuning(win->tuningAnswer);

            pump(4);

            check(win->heldTuning && soundsAt(2, 61) == 61,
                  (std::string("a scale chosen before ") + act +
                   " is not taken").c_str());
            win->heldTuning = sigc::slot<void (std::string)>();
        }
    }

    edit(2, "pw1", 0.42f);
    win->tuningAnswer = (dir / "edo19.scl").string();
    press(win, 2, "Open _scale (.scl)...");
    pump(4);
    press(win, 2, "_12-TET");
    pump(4);

    check(soundsAt(2, 61) == 61 && stripSays(win, 2, "Tuning: 12-TET") &&
          argValue(2, "pw1") == 0.42f,
          "12-TET puts the channel back, values and all");

    std::filesystem::remove_all(dir);
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

        check(closed.size() == 3 &&
              std::find(closed.begin(), closed.end(), "patches") !=
                  closed.end() &&
              std::find(closed.begin(), closed.end(), "midimap") !=
                  closed.end() &&
              std::find(closed.begin(), closed.end(), "controller") !=
                  closed.end(),
              "the first layout has every pane up but the patch list, the "
              "MIDI routing and the controller");
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
    check(win->paramStack_.get_visible_child_name() == "ch2" &&
              stripSays(win, 2, "3: "),
          "...and Patch params shows its page, which says which");
    check(win->editors_.count(2) == 1,
          "...and its graph is built, since the graph is in view");

    /* A click on a box is not an edit; a drag is. */
    NodeEditor *edited = win->editors_[2];
    NodeCanvasWidget *canvas = findCanvas(edited);

    if (canvas != NULL && canvas->graph() != NULL &&
        !canvas->graph()->boxes().empty())
    {
        const double x = canvas->graph()->boxes()[0].x + 6;
        const double y = canvas->graph()->boxes()[0].y + 6;

        canvas->onPressed(1, x, y);
        canvas->onMotion(x, y);
        canvas->onReleased(1, x, y);

        check(!edited->dirty(), "a click on a box in the graph is not an edit");

        canvas->onPressed(1, x, y);
        canvas->onMotion(x + 40, y);
        canvas->onReleased(1, x + 40, y);

        check(edited->dirty(), "...and dragging it is");
    }
    else
        fail("the graph on channel 3 has a canvas with boxes on it");

    /* A reload rebuilds every page; the channel has to survive it. */
    patchMgr->newPatch(dsp, 5);
    pump(4);

    check(win->chan_ == 2, "loading a patch elsewhere keeps the channel");
    check(win->editors_.count(2) == 1 && win->editors_[2] == edited &&
          edited->dirty(),
          "...and the graph on it, with the edits not saved yet");

    /* Undone, the graph is its file again but has a Redo, which a reload
       must not take away. */
    {
        std::vector<Gtk::Button *> buttons;
        Gtk::Button *undo = NULL, *redo = NULL;

        collectUnder<Gtk::Button>(edited, buttons);

        for (Gtk::Button *b : buttons)
            if (b->get_icon_name() == "edit-undo-symbolic")
                undo = b;
            else if (b->get_icon_name() == "edit-redo-symbolic")
                redo = b;

        if (undo != NULL)
            g_signal_emit_by_name(undo->gobj(), "clicked");

        const bool undone = undo != NULL && redo != NULL && !edited->dirty();

        patchMgr->newPatch(dsp, 5);
        pump(4);

        check(undone && win->editors_.count(2) == 1 &&
              win->editors_[2] == edited && redo->get_sensitive(),
              "...and one with only a step to redo");
    }

    /* An editor on another file -- as after its Save As -- with edits not
       saved, held across a rebuild while its channel is not looked at:
       back when it is, the channel still playing the patch it was editing;
       not back once something else has been put on the channel. */
    {
        const std::filesystem::path copy =
            std::filesystem::temp_directory_path() /
            ("windowcheck-copy-" +
             std::to_string(std::chrono::steady_clock::now()
                                .time_since_epoch().count()) + ".dsp");

        std::filesystem::copy_file(dsp, copy);

        bool back = false, gone = false;

        for (int replace = 0; replace < 2; replace++)
        {
            win->selectChannel(2);
            pump(4);

            NodeEditor *ed = win->editors_.count(2) ? win->editors_[2] : NULL;

            if (ed != NULL)
                ed->open(copy.string(), -1);

            pump(4);

            NodeCanvasWidget *c = ed != NULL ? findCanvas(ed) : NULL;

            if (c != NULL && c->graph() != NULL && !c->graph()->boxes().empty())
            {
                const double x = c->graph()->boxes()[0].x + 6;
                const double y = c->graph()->boxes()[0].y + 6;

                c->onPressed(1, x, y);
                c->onMotion(x + 40, y);
                c->onReleased(1, x + 40, y);
            }

            const bool edited = ed != NULL && ed->dirty();

            win->selectChannel(0);
            patchMgr->newPatch(dsp, replace ? 2 : 5);
            pump(4);
            win->selectChannel(2);
            pump(4);

            NodeEditor *now = win->editors_.count(2) ? win->editors_[2] : NULL;

            if (replace)
                gone = edited && now != NULL && !now->dirty();
            else
                back = edited && now == ed && ed->dirty();
        }

        std::filesystem::remove(copy);

        check(back, "a graph editor on another file comes back with its "
                    "edits across a rebuild");
        check(gone, "...and not over a patch put on its channel since");
    }

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

    checkPatchPage(win, dsp);
    checkTuning(win, dsp);

    checkAccessible(win, "patch mode");
    checkNumberBoxes(win, "patch mode");

    {
        Keyboard *keys = findUnder<Keyboard>(win);

        check(keys != NULL &&
              keys->get_accessible_role() == Gtk::Accessible::Role::GROUP &&
              gtk_test_accessible_has_property(
                  GTK_ACCESSIBLE(keys->gobj()), GTK_ACCESSIBLE_PROPERTY_LABEL),
              "the keys are a named group to a screen reader");

        if (keys != NULL)
        {
            keys->SetNote(60, true);
            keys->SetNote(63, true);
            pump(4);

            const bool held = described(keys, "Held: C4 E flat 4");

            keys->SetNote(60, false);
            keys->SetNote(63, false);
            pump(4);

            check(held && described(keys, "No keys held"),
                  "...which says the notes held, and when none are");
        }
    }

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
              isClosed(win, "midimap") && isClosed(win, "controller") &&
              closed.size() == 6,
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
          win->panes_->closed().size() == 6,
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

        check(win->mode_ == "patch" && closed.size() == 3 &&
              isClosed(win, "patches") && isClosed(win, "midimap") &&
              isClosed(win, "controller") &&
              !ticked(win, "patches"),
              "Reset Layout puts the first layout back, and the ticks with "
              "it");
    }

    pickMode(win, "piece");

    /* ---- the sequence ---- */

    /* Sequence mode's piece is gen/scratch.gen, from the source tree the
       graph under test came out of -- set here rather than for the whole
       run, since a piece found at startup would put its instruments on
       channels the sections above count. */
    Glib::setenv("THINK_GEN_PATH",
                 (std::filesystem::path(dsp).parent_path().parent_path() /
                  "gen").string());

    pickMode(win, "seq");
    pump(8);

    SeqView &seq = win->composer_->sequencer();

    check(win->mode_ == "seq" && win->panes_->isVisible("seqview") &&
          win->panes_->isVisible("roll") &&
          !win->panes_->isVisible("composerview") &&
          !isClosed(win, "composerview") && !isClosed(win, "selection"),
          "sequence mode has the tracks and the roll, and not the canvas "
          "or the piece's editors");

    check(win->composer_->document() == Composer::SEQUENCE &&
          seq.trackCount() == 5 &&
          win->composer_->status().get_text().find("Scratch") !=
              Glib::ustring::npos,
          "...and opens on scratch.gen's five tracks");

    check(seq.trackChooser(0) != NULL && seq.trackDsp(3) == "ebass.dsp",
          "...each with a button for the graph that plays it");

    check(win->chan_ == seq.trackChannel(4),
          "...and the keys on the last track's channel");

    /* A track from the keys: its cursor walks to the last step, puts a
       note there, accents it, clears it, and holds a note a step longer
       and back; each lands in the grid and is said. The Rhodes, whose
       ladder names its rows. */
    {
        const Gdk::ModifierType none = Gdk::ModifierType(0);
        const Gdk::ModifierType shift = Gdk::ModifierType::SHIFT_MASK;
        const std::string was = seq.trackCells(4);
        auto count = [](const std::string &s, char c)
        { return std::count(s.begin(), s.end(), c); };

        seq.key(4, GDK_KEY_End, none);

        const std::string atEnd = seq.trackSaid(4);

        seq.key(4, GDK_KEY_Return, none);

        const std::string one = seq.trackSaid(4);
        const std::string noted = seq.trackCells(4);

        seq.key(4, GDK_KEY_Return, none);

        const std::string two = seq.trackSaid(4);

        seq.key(4, GDK_KEY_Delete, none);

        const std::string three = seq.trackSaid(4);
        const std::string cleared = seq.trackCells(4);

        seq.key(4, GDK_KEY_Left, none);
        seq.key(4, GDK_KEY_Return, none);
        seq.key(4, GDK_KEY_Right, shift);

        const std::string longer = seq.trackSaid(4);
        const long ties = count(seq.trackCells(4), '-');

        seq.key(4, GDK_KEY_Left, shift);

        const std::string shorter = seq.trackSaid(4);
        const long untied = count(seq.trackCells(4), '-');

        seq.key(4, GDK_KEY_Delete, none);
        pump(4);

        printf("      %s | %s | %s | %s | %s | %s\n", atEnd.c_str(),
               one.c_str(), two.c_str(), three.c_str(), longer.c_str(),
               shorter.c_str());
        check(atEnd.find("step 16: empty") != std::string::npos &&
              count(noted, 'x') == count(was, 'x') + 1 &&
              two.find(": accent") != std::string::npos &&
              cleared == was && three.find(": empty") != std::string::npos &&
              ties == count(was, '-') + 1 &&
              longer.find("held 2 steps") != std::string::npos &&
              untied == count(was, '-') &&
              shorter.find("step 15: note") != std::string::npos &&
              seq.trackCells(4) == was,
              "a track's keys place, accent, clear and hold a note, in the "
              "grid and aloud");
    }

    /* One current channel, whichever pane picks it. */
    {
        const Gdk::ModifierType none = Gdk::ModifierType(0);
        ComposerCanvas &canvas = win->composer_->canvas();
        ComposerCanvas::Selection sel;

        seq.key(1, GDK_KEY_Right, none);
        pump(4);
        checkAgree(win, seq.trackChannel(1), "a key on a track");

        /* A press as the track's row hears it: its click gesture, which
           is what a pointer reaches; windowcheck cannot make a pointer. */
        {
            Gtk::Widget *row = seq.trackArea(2) != NULL
                ? seq.trackArea(2)->get_parent() : NULL;
            GtkGesture *click = NULL;
            GListModel *ctrls = row != NULL
                ? gtk_widget_observe_controllers(row->gobj()) : NULL;

            for (guint k = 0; ctrls != NULL && k < g_list_model_get_n_items(ctrls); k++)
            {
                GObject *c = G_OBJECT(g_list_model_get_item(ctrls, k));

                if (GTK_IS_GESTURE_CLICK(c))
                    click = GTK_GESTURE(c);

                g_object_unref(c);
            }

            if (ctrls != NULL)
                g_object_unref(ctrls);

            if (click != NULL)
                g_signal_emit_by_name(click, "pressed", 1, 4.0, 4.0);

            pump(4);
            checkAgree(win, seq.trackChannel(2), "a click on a track");
        }

        turnKeys(win, 9);
        checkAgree(win, 9, "the Keys stepper, to an empty channel");

        turnKeys(win, seq.trackChannel(0));
        checkAgree(win, seq.trackChannel(0), "the Keys stepper");

        /* A press, as the canvas reports one: composercheck presses
           the canvas itself. */
        sel.kind = ComposerCanvas::Selection::CHAIN;
        sel.chain = 3;
        canvas.sigPressed.emit(sel);
        pump(4);
        checkAgree(win, seq.trackChannel(3), "a chain on the canvas");

        sel.kind = ComposerCanvas::Selection::STAGE;
        sel.chain = 1;
        sel.index = 0;
        canvas.select(sel);
        canvas.sigPressed.emit(sel);
        pump(4);
        checkAgree(win, seq.trackChannel(1), "a stage on the canvas");

        turnKeys(win, seq.trackChannel(0));
        canvas.sigPressed.emit(sel);
        pump(4);
        checkAgree(win, seq.trackChannel(1),
                   "the selected stage pressed again after the keys moved");

        sel.kind = ComposerCanvas::Selection::SINK;
        sel.chain = 4;
        canvas.sigPressed.emit(sel);
        pump(4);
        checkAgree(win, seq.trackChannel(4), "a sink on the canvas");

        sel.kind = ComposerCanvas::Selection::NONE;
        canvas.select(sel);
        pump(4);
        checkAgree(win, seq.trackChannel(4), "nothing on the canvas keeps it");

        win->chanList_.select_row(*rowFor(win, seq.trackChannel(2)));
        pump(4);
        checkAgree(win, seq.trackChannel(2), "a row in Channels");
    }

    /* The keys go into the piece on a channel it listens on, and play the
       instrument anywhere else; a key is let go the way it was pressed,
       whatever the window is in by then. scratch.gen's keys track (4)
       takes `input midi', its bassline (3) does not. */
    {
        thcScheduler *sched = static_cast<ComposerPeek *>(win->composer_)->sched_;
        Keyboard *keys = findUnder<Keyboard>(win->kbPanel_);
        const int listening = seq.trackChannel(4);
        const int deaf = seq.trackChannel(3);
        int heard = 0;
        bool sounded = false;
        sigc::connection count = sched->sigDelivered.connect(
            [&heard, sched] (const thcEvent &)
            { if (sched->deliveringChain() >= 0) heard++; });

        /* Pressed and released on `chan', in `mode', and what the piece
           heard of it. */
        auto play = [&] (int chan, bool between, const char *mode = "seq")
        {
            pickMode(win, mode);
            win->selectChannel(chan);
            pump(2);

            const int before = heard;

            keys->signal_note_on().emit(chan, 60, 100);
            pump(2);

            const int on = heard - before;

            /* A window of sound, which is what takes a queued note on. */
            thSynth::instance()->process();

            const thMidiChan *mc = thSynth::instance()->getChannel(chan);

            sounded = mc != NULL && mc->sounding();

            if (between)
                pickMode(win, "patch");

            keys->signal_note_off().emit(chan, 60);
            pump(2);

            pickMode(win, "seq");

            return std::make_pair(on, heard - before - on);
        };

        const std::pair<int, int> into = play(listening, false);
        const std::pair<int, int> past = play(deaf, false);
        const bool deafSounded = sounded;
        const std::pair<int, int> held = play(listening, true);
        const std::pair<int, int> patch = play(listening, false, "patch");
        const bool patchSounded = sounded;

        count.disconnect();

        check(into.first > 0 && into.second > 0,
              "a key on a channel the piece listens on goes into the piece, "
              "press and release");
        check(deafSounded && past.first == 0 && past.second == 0,
              "...and on one it does not, past it, to the instrument");
        check(held.first > 0 && held.second > 0,
              "...and a key pressed into the piece is let go into it after "
              "a trip to patch mode");
        check(patchSounded && patch.first == 0 && patch.second == 0,
              "...and in patch mode, on that same channel, to the instrument");
    }

    /* A sequence's channels are the person's, instruments and tuning
       alike: the menu is live on a track's channel, a tuning taken there
       is kept through a reload of the sequence, and 12-TET puts it back. */
    {
        pump(4);

        const int track = seq.trackChannel(4);
        Gtk::Widget *page = pageFor(win, track);
        std::vector<Gtk::MenuButton *> menus;
        Gtk::MenuButton *tuning = NULL;

        if (page != NULL)
            collectUnder<Gtk::MenuButton>(page, menus);

        for (Gtk::MenuButton *m : menus)
            if (labelsUnder(m).rfind("Tuning:", 0) == 0)
                tuning = m;

        /* Read now: a retune rebuilds the pages, this button with them. */
        const bool live = tuning != NULL && tuning->is_sensitive();

        win->setChannelTuning(track, { "seq19", "150.0 1200.0", "" });
        pump(4);

        const double tuned = soundsAt(track, 61);

        /* A reload of the sequence, through the track chooser: the bass
           track given the graph it already plays. */
        win->composer_->setTrackInstrument(3, 0, seq.trackDsp(3), true);
        pump(8);

        gthPatchManager::PatchFile *after =
            gthPatchManager::instance()->getPatch(track);

        check(live && std::fabs(tuned - 61.5) < 1e-9 &&
              std::fabs(soundsAt(track, 61) - tuned) < 1e-9 &&
              after != NULL && after->doc.tuningName == "seq19",
              "a sequence's channel has its tuning live, kept through a "
              "reload");

        win->setChannelTuning(track, thTuningText());
        pump(4);
    }

    /* The bass track, chain 3, played by a kick: a graph that ignores the
       note gets a grid one row tall. */
    check(win->composer_->setTrackInstrument(3, 0, "kick909.dsp", false),
          "a track's graph can be changed");

    pump(8);

    check(seq.trackDsp(3) == "kick909.dsp" && seq.trackArea(3) != NULL &&
          seq.trackArea(3)->get_content_height() == 26,
          "...and the track is a one-row strip after the reload");
    checkAgree(win, win->chan_, "...and the current channel still marked");

    checkAccessible(win, "sequence mode");

    pickMode(win, "piece");
    pump(8);

    checkAccessible(win, "piece mode");

    check(win->composer_->document() == Composer::PIECE &&
          win->composer_->status().get_text().find("Untitled") !=
              Glib::ustring::npos &&
          seq.trackChooser(0) == NULL,
          "piece mode puts the piece back, with no choosers on its tracks");

    pickMode(win, "seq");
    pump(8);

    check(seq.trackDsp(3) == "kick909.dsp" &&
          win->composer_->status().get_text().find("(edited)") !=
              Glib::ustring::npos,
          "...and sequence mode the sequence, edits and all");

    win->composer_->signal_file_command().emit();
    pump(4);

    check(win->mode_ == "piece",
          "New and Open in sequence mode go to piece mode first");

    pickMode(win, "patch");
    activate(win, "pane-seqview");

    check(win->mode_ == "seq" && win->panes_->isVisible("seqview"),
          "the Sequencer's tick in patch mode goes to sequence mode");

    /* The keys went to the last track the first time; a channel picked
       after that is somebody's, and stays through a trip elsewhere --
       where another picked is that mode's own. */
    win->selectChannel(0);
    pickMode(win, "piece");
    turnKeys(win, 6);
    pickMode(win, "seq");
    checkAgree(win, 0, "a channel picked in sequence mode, back from piece "
                       "mode with another picked there");
    pickMode(win, "piece");
    checkAgree(win, 6, "...and piece mode's own, back in piece mode");
    pickMode(win, "seq");

    /* A change to sequence mode's layout, for the section that keeps
       them. */
    activate(win, "pane-roll");

    check(isClosed(win, "roll") && win->mode_ == "seq",
          "the roll closes in sequence mode");

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

        const size_t seqAt = text.find("seq=");
        const std::string seqLine =
            seqAt == std::string::npos ? std::string()
                : text.substr(seqAt, text.find('\n', seqAt) - seqAt);

        check(!seqLine.empty() && seqLine.find("\"roll\"") ==
                                      std::string::npos,
              "sequence mode's layout is kept too, without its closed roll");

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

    win->setDesktopMode("seq");
    pump(4);

    check(isClosed(win, "roll"),
          "...and sequence mode's, with the roll it closed");

    win->setDesktopMode("piece");
    pump(4);

    delete win;
    pump(4);

    /* The mode last used is where the next window starts. */
    win = new TestWindow;
    win->applyPrefs();
    win->set_visible(true);
    pump(8);

    check(win->mode_ == "piece",
          "the mode last used is the one a new window starts in");

    win->setDesktopMode("seq");
    pump(4);
    delete win;
    pump(4);

    /* ...sequence mode among them, which has no tracks until its pane is
       in view and the composer starts -- and then the keys go to the
       last one. */
    win = new TestWindow;
    win->applyPrefs();
    win->set_visible(true);
    pump(8);

    {
        SeqView &started = win->composer_->sequencer();
        const size_t n = started.trackCount();

        check(win->mode_ == "seq" &&
              win->composer_->document() == Composer::SEQUENCE &&
              n == 5 && win->chan_ == started.trackChannel(n - 1),
              "a window that starts in sequence mode opens the sequence, "
              "with the keys on its last track");
    }

    win->setDesktopMode("patch");
    pump(4);

    /* A piece loaded where the keys reach nothing aims them at a channel
       it listens on, until somebody picks one in piece mode. hands.gen
       listens on its first three channels and nothing else. */
    delete win;
    pump(4);

    {
        const std::string gens = Glib::getenv("THINK_GEN_PATH");
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() /
            ("windowcheck-hands-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));

        std::filesystem::create_directories(dir);
        std::filesystem::copy_file(std::filesystem::path(gens) / "hands.gen",
                                   dir / "airports.gen");
        Glib::setenv("THINK_GEN_PATH", dir.string());

        win = new TestWindow;
        win->set_visible(true);
        pump(8);
        win->selectChannel(9);
        win->setDesktopMode("piece");
        pump(8);
        checkAgree(win, 0, "a piece the keys reach nothing in aims them at a "
                           "channel it listens on");

        turnKeys(win, 9);
        static_cast<ComposerPeek *>(win->composer_)->loadPiece();
        pump(8);
        checkAgree(win, 9, "...and a reload leaves one somebody picked there");

        win->setDesktopMode("patch");
        pump(4);
        Glib::setenv("THINK_GEN_PATH", gens);
        std::filesystem::remove_all(dir);
    }

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

        check(closed.size() == 3 && isClosed(win, "patches") &&
              isClosed(win, "midimap") && isClosed(win, "controller"),
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
