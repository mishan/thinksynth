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
 * editorcheck -- does arming a probe in the editor actually put samples on the
 *                screen?
 *
 * dspprobe proves the tap sums the right buffer. visualcheck proves a module
 * survives what it is handed and draws deterministically. dspgraph proves a
 * panel lands where it should. Nothing so far joins them up, and the joining
 * is where a probe becomes three things at once -- a Box in the graph, a slot
 * in the engine, an instance of a module -- held together by NodeEditor and by
 * nothing else.
 *
 * So this drives the real editor: opens a .dsp on a live channel, arms a
 * probe through the same call the menu makes, plays a chord, pumps the main
 * loop so the frame tick runs, and then asks the module what it saw. If any
 * link in that chain is wrong the meter reads silence, which is exactly the
 * failure a screenshot would not show.
 *
 * A subclass, because arming and the probe list are the editor's own business
 * and there is no reason to widen them for a test. Protected is the access
 * level that means "and for anything that is a NodeEditor", which this is.
 *
 * NEEDS A DISPLAY, like canvasbench and unlike everything else here: it builds
 * real widgets. Run it under xvfb-run on a headless box. It is a gate all the
 * same -- what it covers is not otherwise covered -- so it is in the ctest
 * list, guarded by whether a display can be had.
 *
 *   xvfb-run -a ./build/scripts/editorcheck -p build/plugins/ dsp/ts1.dsp
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <vector>

#include <gtkmm.h>

#include "think.h"
#include "NodeCatalog.h"
#include "NodeEdit.h"
#include "NodeGraph.h"
#include "NodeLayout.h"
#include "gui/NodeEditor.h"
#include "gui/ArgPanelView.h"
#include "gui/PanelView.h"
#include "gui/NodeParamsView.h"

/* Picked up automatically by LeakSanitizer, the same way visualcheck and
 * dspstress supply theirs -- so this stays a real gate under CI's
 * ASAN_OPTIONS=detect_leaks=1 rather than needing an environment variable
 * nobody will remember.
 *
 * Building a GTK window means initialising fontconfig and, under Xvfb, Mesa's
 * EGL. Both keep global caches they never free: 1.8MB in 44008 allocations
 * here, every one of them inside those two libraries and none reachable from
 * anything in this tree. Suppressed by module rather than by turning leak
 * detection off, so a real leak in NodeEditor -- an instance not closed, a
 * module not unloaded -- still fails the run. Verified by leaking one on
 * purpose.
 */
extern "C" const char *__lsan_default_suppressions (void)
{
    return "leak:libfontconfig\n"
           "leak:libEGL\n"
           "leak:libGLX\n"
           "leak:libglapi\n"
           "leak:swrast\n"
           "leak:libgallium\n";
}

namespace {

int failures = 0;
int checks = 0;

void ok (bool cond, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)))
#endif
;

void ok (bool cond, const char *fmt, ...)
{
    checks++;

    if (cond)
        return;

    va_list ap;

    va_start(ap, fmt);
    printf("FAIL  ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);

    failures++;
}

/* The way in. Nothing here does anything the menu does not; it only reaches
   the same calls without a pointer. */
class TestEditor : public NodeEditor {
public:
    TestEditor (thSynth *synth) : NodeEditor(synth) { }

    using NodeEditor::armProbe;
    using NodeEditor::disarmProbe;
    using NodeEditor::probes_;
    using NodeEditor::visuals_;
    using NodeEditor::disarmAllProbes;
    using NodeEditor::graph;
    using NodeEditor::reload;
    using NodeEditor::onProbeActivated;
    using NodeEditor::paintEnlarged;
    using NodeEditor::enlarged_;
    using NodeEditor::probeTick_;
    using NodeEditor::showPanels;
    using NodeEditor::paletteShown;
    using NodeEditor::paramsShown;
    using NodeEditor::onSave;
    using NodeEditor::listBtn_;
    using NodeEditor::listed_;
    using NodeEditor::boxList_;
    using NodeEditor::wiring_;
    using NodeEditor::onDisconnect;
    using NodeEditor::onConnect;
    using NodeEditor::onDeleteNode;
    using NodeEditor::text;
    using NodeEditor::onBoxMoved;
    using NodeEditor::onParamEdited;
    using NodeEditor::onControlChanged;
    using NodeEditor::onPaletteAdd;

    /* The box index of the first probe panel, or -1. */
    int firstPanel (void) const
    {
        for (size_t b = 0; b < graph().boxes().size(); b++)
            if (graph().boxes()[b].isProbe)
                return (int)b;

        return -1;
    }

    /* Save is a button handler with no return value; this is the same write
       path, reporting whether it worked. */
    bool save (void)
    {
        writeLayout();
        return writeFile(sourceFile(), text());
    }

    const string &sourceFile (void) const { return filename(); }

    /* How many panels the graph is carrying. */
    int panels (void) const
    {
        int n = 0;

        for (size_t b = 0; b < graph().boxes().size(); b++)
            if (graph().boxes()[b].isProbe)
                n++;

        return n;
    }
};

void pump (double seconds)
{
    const std::chrono::steady_clock::time_point until =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds((int)(seconds * 1000));

    while (std::chrono::steady_clock::now() < until)
    {
        while (Glib::MainContext::get_default()->pending())
            Glib::MainContext::get_default()->iteration(false);

        Glib::usleep(1000);
    }
}

/* The peak the meter is showing, read back out of the module by drawing it and
   looking at the pixels.
 *
 * Deliberately not by reaching into the module's state, which the ABI does not
 * expose and should not: what is being checked is that something arrived and
 * was drawn, and the pixels are the only honest evidence of that. A silent
 * meter draws its bar at zero width, so any non-background pixel in the bar's
 * row means the chain delivered. */
bool drewSomething (thVisual *v, void *inst)
{
    Cairo::RefPtr<Cairo::ImageSurface> a =
        Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, 128, 24);
    Cairo::RefPtr<Cairo::ImageSurface> b =
        Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, 128, 24);

    /* Against a fresh instance rather than against "not blank": the frame, the
       knee tick and the text are drawn whatever the signal is, so a picture is
       never blank and comparing to blank would pass on silence. What says
       something arrived is that this instance draws differently from one that
       has been fed nothing. */
    void *fresh = v->open(TH_DEFAULT_SAMPLES);

    {
        Cairo::RefPtr<Cairo::Context> cr = Cairo::Context::create(a);

        v->draw(inst, cr->cobj(), 128, 24);
    }

    {
        Cairo::RefPtr<Cairo::Context> cr = Cairo::Context::create(b);

        v->draw(fresh, cr->cobj(), 128, 24);
    }

    v->close(fresh);

    a->flush();
    b->flush();

    const int stride = a->get_stride();

    return memcmp(a->get_data(), b->get_data(), (size_t)stride * 24) != 0;
}

/* Every Gtk::Scale under `w', depth first. */
void collectScales (Gtk::Widget *w, std::vector<Gtk::Scale *> &out)
{
    if (w == NULL)
        return;

    Gtk::Scale *scale = dynamic_cast<Gtk::Scale *>(w);

    if (scale != NULL)
        out.push_back(scale);

    for (Gtk::Widget *c = w->get_first_child(); c != NULL;
         c = c->get_next_sibling())
        collectScales(c, out);
}

/* Every widget of one type under `w', depth first. */
template <typename T>
void collectWidgets (Gtk::Widget *w, std::vector<T *> &out)
{
    if (w == NULL)
        return;

    T *found = dynamic_cast<T *>(w);

    if (found != NULL)
        out.push_back(found);

    for (Gtk::Widget *c = w->get_first_child(); c != NULL;
         c = c->get_next_sibling())
        collectWidgets<T>(c, out);
}

/* PanelView over a thPanel built by hand, rather than by a provider.
 *
 * Everything else here draws a panel some provider filled in, and a provider
 * uses thPanelBuilder, which keeps a panel's rows and its groupOrder in step
 * and only ever makes the two kinds of row ArgPanel has. setPanel is public
 * and takes neither of those for granted, so the cases a provider cannot
 * currently produce are checked at that boundary instead of left until some
 * later provider produces one. */
void checkPanelViewRows (void)
{
    thPanel panel;

    thPanelRow loose;

    loose.kind = thPanelRow::SLIDER;
    loose.id = "a";
    loose.label = "A";
    loose.lo = 0;
    loose.hi = 1;
    loose.step = 0.0001;
    loose.decimals = 4;
    loose.value = 0.25;

    thPanelRow grouped = loose;

    grouped.id = "b";
    grouped.label = "B";
    grouped.group = "Nowhere";

    thPanelRow words;

    words.kind = thPanelRow::TEXT;
    words.id = "c";
    words.label = "C";
    words.text = "c4 e4 g4";

    panel.rows.push_back(loose);
    panel.rows.push_back(grouped);
    panel.rows.push_back(words);

    /* `Nowhere' deliberately left out of groupOrder: a row is drawn for what
       it carries, not for what some other list remembers about it. Drawing
       only the groups named there loses the row silently -- indexOf still
       finds it and setValue still writes to it, and it is on no screen. */
    Gtk::Window *win = new Gtk::Window();
    PanelView *view = Gtk::manage(new PanelView);

    view->setPanel(panel);

    win->set_child(*view);
    win->set_default_size(500, 300);
    win->set_visible(true);

    pump(0.3);

    std::vector<Gtk::Scale *> scales;
    std::vector<Gtk::Entry *> entries;

    collectWidgets<Gtk::Scale>(view, scales);
    collectWidgets<Gtk::Entry>(view, entries);

    ok(scales.size() == 2,
       "a row whose group is not in groupOrder is drawn anyway (%zu sliders)",
       scales.size());

    /* One Entry and not three: a GTK4 SpinButton is not one, so the only
       plain box on this panel is the text row's. */
    ok(entries.size() == 1, "and the text row got a box (%zu)",
       entries.size());

    int edits = 0;

    view->signal_edited().connect(
        [&](const string &, const string &) { edits++; });

    view->setValue("b", 0.75);

    pump(0.2);

    ok(fabs(scales[1]->get_value() - 0.75) < 1e-4,
       "a value pushed into that row reaches its slider (%f)",
       scales[1]->get_value());

    /* A string row is set with the string. Spelling a double into it is what
       setValue would have had to do, and a note set is not a number. */
    view->setText("c", "d4 f4 a4");

    pump(0.2);

    Gtk::Entry *box = entries.empty() ? NULL : entries[0];

    ok(box != NULL && box->get_text() == "d4 f4 a4",
       "and a text row takes the words it is given (%s)",
       box ? box->get_text().c_str() : "(no box)");

    ok(edits == 0,
       "and none of that came back round as an edit (%d)", edits);

    win->set_visible(false);
    delete win;
}

/* The parameter panel over a channel *effect's* chanargs.
 *
 * A channel has two chanarg maps, and one lookup reaches both: thSynth::
 * getChanArg reads `fx.delay' as the effect's. So the patch page draws the
 * second map with the same widget as the first, differing by the prefix its
 * lookups carry -- and the way that fails is silently, with a slider that
 * moves and changes nothing, because the name it looked up was the
 * instrument's and there is nothing there.
 *
 * What the panel says is scripts/panelcheck's business, and it says it with
 * no toolkit anywhere. What is left here is the half that needs a display:
 * that a thPanel becomes real widgets, and that dragging one of them reaches
 * the arg its row stands for.
 */
void checkEffectPanel (thSynth &synth, const string &pluginPath)
{
    const string fx =
        thUtil::findDataFile("fx/echo.dsp", "dsp", "THINK_DSP_PATH", DSP_PATH);

    if (fx.empty())
    {
        printf("      (fx/echo.dsp not found; skipping the effect panel)\n");
        return;
    }

    ok(synth.loadEffect(fx, 0) != NULL,
       "an effect loads onto the channel the editor was using");

    thArg *mix = synth.getChanArg(0, "fx.mix");

    ok(mix != NULL, "its parameters are reachable under `fx.'");

    if (mix == NULL)
        return;

    const float before = (*mix)[0];

    Gtk::Window *win = new Gtk::Window();
    ArgPanelView *table = Gtk::manage(new ArgPanelView);

    table->setChannel(0);
    table->setPrefix(TH_EFFECT_PREFIX);

    ok(table->rebuild(), "the effect's parameters make a panel");

    win->set_child(*table);
    win->set_default_size(500, 200);
    win->present();

    pump(0.2);

    std::vector<Gtk::Scale *> scales;

    collectScales(table, scales);

    /* One slider per row, in the panel's own order -- which is what lets the
       one belonging to `mix' be found without reaching into the widgets by
       name. */
    const int at = table->panel().indexOf("mix");

    ok(scales.size() == table->panel().rows.size(),
       "the panel drew a slider for each of its rows (%zu of %zu)",
       scales.size(), table->panel().rows.size());

    if (at >= 0 && scales.size() == table->panel().rows.size())
    {
        const double want = (before < 0.5) ? 0.8 : 0.2;

        scales[at]->set_value(want);

        pump(0.2);

        ok(fabs((double)(*mix)[0] - want) < 1e-6,
           "moving it moved the effect's parameter and not a name that is "
           "not there (%f -> %f, wanted %f)", (double)before,
           (double)(*mix)[0], want);
    }
    else
        ok(false, "the panel has a row for `mix' (%d)", at);

    /* The other direction: the arg moves behind the panel -- a MIDI
       controller, another window -- and the slider follows without that
       coming back round as an edit. */
    mix->setValue(0.35);

    pump(0.2);

    if (at >= 0 && (size_t)at < scales.size())
        ok(fabs(scales[at]->get_value() - 0.35) < 1e-4,
           "and the slider follows the arg when something else moves it "
           "(%f)", scales[at]->get_value());

    win->set_visible(false);
    delete win;
}

/* The panel over one node of a graph.
 *
 * What it says is scripts/panelcheck's business and is checked there with no
 * toolkit anywhere. What is left for here is the half that needs a display:
 * that a thPanel becomes real widgets, that a parameter the plugin writes is
 * drawn without one to type into, and that typing in the one that is offered
 * reports the edit the window is waiting for -- the box, the arg's name and
 * the number.
 *
 * The signal and no write, because there is nothing to write: a node's value
 * is a number in the .dsp, and splicing it is NodeEditor's.
 */
void checkNodePanel (thSynth &synth, const char *file)
{
    thSynthTree *tree = synth.parseTree(file);

    if (tree == NULL)
    {
        ok(false, "the patch parses for the node panel");
        return;
    }

    NodeGraph graph;

    if (!graph.build(tree))
    {
        ok(false, "it builds a graph");

        delete tree;

        return;
    }

    /* The first node with something a person could type into, which is the
       same question the page asks before it draws a panel at all. */
    int box = -1;

    for (size_t i = 0; i < graph.boxes().size() && box < 0; i++)
        if (!graph.boxes()[i].isControl &&
            NodePanel::settable(&graph, (int)i))
            box = (int)i;

    ok(box >= 0, "the patch has a node with a value to set");

    if (box < 0)
    {
        delete tree;

        return;
    }

    Gtk::Window *win = new Gtk::Window();
    NodeParamsView *panel = Gtk::manage(new NodeParamsView);

    int gotBox = -1;
    string gotArg;
    double gotValue = 0;
    int edits = 0;

    panel->signal_param_edited().connect(
        [&] (int b, string arg, double v)
        {
            gotBox = b;
            gotArg = arg;
            gotValue = v;
            edits++;
        });

    panel->setBox(&graph, box);

    win->set_child(*panel);
    win->set_default_size(420, 400);
    win->present();

    pump(0.2);

    /* A spin button per offered row and none for the rest. Every shipped
       graph has more parameters shown than offered -- outputs, wires,
       controls -- so this is not the trivial case. */
    std::vector<Gtk::SpinButton *> spins;

    collectWidgets<Gtk::SpinButton>(panel, spins);

    size_t offered = 0;

    for (size_t i = 0; i < panel->panel().rows.size(); i++)
        if (panel->panel().rows[i].editable)
            offered++;

    ok(offered > 0 && offered < panel->panel().rows.size(),
       "the panel offers %zu of %zu rows, and shows the rest",
       offered, panel->panel().rows.size());

    ok(spins.size() == offered,
       "a box to type in for each offered row (%zu of %zu)",
       spins.size(), offered);

    /* Building the panel must not report an edit. The other way round, a
       patch was modified by the act of looking at it. */
    ok(edits == 0, "drawing it reported no edit (%d)", edits);

    if (!spins.empty())
    {
        const double want = spins[0]->get_value() + 1.0;

        spins[0]->set_value(want);

        /* Committed on Enter or on focus leaving, not on every increment:
           a spin button moves once per arrow click and each of those would
           otherwise be a separate splice into the file. */
        win->set_focus(*win);

        pump(0.2);

        ok(edits == 1 && gotBox == box && !gotArg.empty() &&
           fabs(gotValue - want) < 1e-6,
           "typing in one reports the box, the arg and the number "
           "(%d edits, box %d, `%s' = %f, wanted %f)",
           edits, gotBox, gotArg.c_str(), gotValue, want);

        /* And typing it back to what the file holds is reported too.
         *
           A node's value is not written when it is typed: the window keeps
           it as a pending edit until Save, so the panel goes on showing the
           file's number and typing that number back is how a person takes
           the edit back. Dropped as an echo -- which is the right answer for
           a channel's arg, where the write has already happened -- the
           window keeps the edit, stays modified, and saves a number the
           panel is not showing. */
        const double back = want - 1.0;

        spins[0]->set_value(back);
        win->set_focus(*win);

        pump(0.2);

        ok(edits == 2 && fabs(gotValue - back) < 1e-6,
           "and typing it back to what the file holds is reported as well, "
           "so the window can forget the edit (%d edits, %f)",
           edits, gotValue);
    }

    win->set_visible(false);
    delete win;

    delete tree;
}

/* A position saved in the file is where the box is drawn: on opening, and
   after a probe is armed, which lays its panel out above its node. A panel
   that would go off the top of the canvas moves its node down instead. */
void checkLayoutRestored (thSynth &synth, const char *file)
{
    const string copy = thUtil::tempFile("editorcheck-layout-");

    std::filesystem::copy_file(file, copy,
        std::filesystem::copy_options::overwrite_existing);

    TestEditor *ed = Gtk::manage(new TestEditor(&synth));
    Gtk::Window *w = new Gtk::Window();

    w->set_child(*ed);
    w->present();

    pump(0.2);

    ok(ed->open(copy), "an editor opens a copy of %s", file);

    /* A node with an output. */
    int box = -1;
    string out;

    for (size_t b = 0; b < ed->graph().boxes().size() && box < 0; b++)
    {
        const NodeGraph::Box &x = ed->graph().boxes()[b];

        if (x.attachedTo >= 0 || x.isControl || x.isExpr || x.isIoSource ||
            x.isIoSink)
            continue;

        for (const NodeGraph::Port &p : x.ports)
            if (!p.isInput)
            {
                box = (int)b;
                out = p.name;
                break;
            }
    }

    ok(box >= 0 && !ed->visuals_.empty(),
       "%s has a node with an output, and there is a visual to watch it with",
       file);

    if (box >= 0 && !ed->visuals_.empty())
    {
        const string visual = ed->visuals_.begin()->first;
        const string name = ed->graph().boxes()[box].name;
        const string key = NodeLayout::keyFor(ed->graph(), box);
        const double x = ed->graph().boxes()[box].x + 300;
        const double y = ed->graph().boxes()[box].y + 200;

        const auto placeAt = [&] (double px, double py)
        {
            std::filesystem::copy_file(file, copy,
                std::filesystem::copy_options::overwrite_existing);

            ofstream o(copy.c_str(), std::ios::binary | std::ios::app);

            o << "# @layout " << key << " " << (long)px << " " << (long)py
              << "\n";
        };

        const auto where = [&] ()
        {
            const int b = ed->graph().boxByName(name);

            return b < 0 ? make_pair(-1.0, -1.0)
                         : make_pair(ed->graph().boxes()[b].x,
                                     ed->graph().boxes()[b].y);
        };

        /* The top of the panel on `name', or 1e9 if it has none. */
        const auto panelTop = [&] ()
        {
            for (const NodeGraph::Box &p : ed->graph().boxes())
                if (p.isProbe && p.name == name)
                    return p.y;

            return 1e9;
        };

        placeAt(x, y);

        ok(ed->open(copy) && where() == make_pair(x, y),
           "a box opens where the file's layout puts it (%s at %g,%g, not "
           "%g,%g)", name.c_str(), where().first, where().second, x, y);

        ok(ed->armProbe(name, out, visual) && where() == make_pair(x, y) &&
           panelTop() < y,
           "and stays there when a probe is armed on it, the panel above it "
           "(%s at %g,%g)", name.c_str(), where().first, where().second);

        ed->disarmAllProbes();
        placeAt(x, 0);

        const bool armed = ed->open(copy) && ed->armProbe(name, out, visual);

        ok(armed && panelTop() >= 0 && where().second > 0,
           "a probe on a box the file puts at the top keeps its panel on the "
           "canvas, the box moving down under it (panel at %g, box at %g)",
           panelTop(), where().second);
    }

    w->set_visible(false);
    delete w;

    std::filesystem::remove(copy);
}

/* What Save writes for edits made through the editor, across the corpus,
 * held against the same edits applied with NodeEdit's own splices in the
 * order Save has always applied them: wires, then controls, then values, then
 * the layout block. The edits are on different args, so their order changes
 * no byte, and anything the editor does to the file in between -- a working
 * copy, a buffer, a reparse -- has to come out the same.
 *
 * Two runs a file: a wire cut, a control's slider and a value set and a box
 * dragged; then, over what that saved -- so over a layout block already
 * there -- a value set, a box dragged and a node added, which has to keep
 * both. Where the boxes end up is read back out of the saved file, not out
 * of the editor. */
void checkSaveCorpus (thSynth &synth, const string &pluginPath)
{
    const char *dsps = getenv("THINK_DSP_PATH");
    std::error_code ec;

    if (dsps == NULL || !std::filesystem::is_directory(dsps, ec))
    {
        ok(false, "THINK_DSP_PATH names the corpus");
        return;
    }

    vector<string> files;

    for (const auto &e : std::filesystem::recursive_directory_iterator(dsps))
        if (e.path().extension() == ".dsp")
            files.push_back(e.path().string());

    std::sort(files.begin(), files.end());

    NodeCatalog catalog;
    NodeCatalog::Entry simple;

    catalog.scan(pluginPath);
    catalog.describe("osc::simple", synth.getPluginManager(), simple);

    vector<pair<string, double> > defaults;

    for (const NodeCatalog::Default &d : simple.defaults)
        defaults.push_back(make_pair(d.name, d.value));

    const string copy = thUtil::tempFile("editorcheck-corpus-");

    const auto read = [] (const string &path)
    {
        ifstream in(path.c_str(), std::ios::binary);

        return string((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    };

    /* Where a box's position is written: the same rounding as the file. */
    const auto at = [] (const NodeGraph::Box &b)
    {
        return make_pair((double)(long)(b.x + 0.5), (double)(long)(b.y + 0.5));
    };

    int compared = 0, mismatched = 0;
    int cuts = 0, sliders = 0, values = 0, drags = 0, adds = 0;

    for (const string &file : files)
    {
        /* One editor a file, so nothing one file armed reaches the next. */
        TestEditor *ed = Gtk::manage(new TestEditor(&synth));
        Gtk::Window *w = new Gtk::Window();

        w->set_child(*ed);
        w->set_default_size(1000, 600);
        w->present();

        pump(0.05);

        std::vector<NodeCanvasWidget *> canvases;

        collectWidgets(ed, canvases);

        NodeCanvas *canvas = canvases.empty() ? NULL : canvases[0];
        string input = read(file);

        /* The presses below are in the graph's own coordinates. */
        ok(canvas != NULL && canvas->zoom() == 1.0,
           "%s opens on a canvas at 1:1", file.c_str());

        for (int run = 0; run < 2 && canvas != NULL; run++)
        {
            {
                ofstream out(copy.c_str(), std::ios::binary | std::ios::trunc);

                out << input;
            }

            if (!ed->open(copy))
            {
                ok(false, "the editor opens %s (run %d)", file.c_str(), run);
                break;
            }

            const NodeGraph &g = ed->graph();
            string want = input, why;
            pair<string, string> cut;

            /* What the oracle applies, in the order Save once applied them:
               wires, controls, values. The gestures go in the opposite order
               -- the drag first, the wire last -- and the editor writes each
               as it is made, so the bytes cannot depend on the order. */
            vector<pair<int, std::function<void (string &)> > > splices;
            int cutEdge = -1;

            if (run == 0)
                for (size_t e = 0; e < g.edges().size() && cutEdge < 0; e++)
                {
                    const NodeGraph::Edge &x = g.edges()[e];
                    const NodeGraph::Box &to = g.boxes()[x.toBox];
                    const string arg = to.ports[x.toPort].name;
                    string t = want;

                    if (to.isExpr || g.boxes()[x.fromBox].isExpr ||
                        NodeEdit::Text::disconnect(t, to.name, arg, 0, why) !=
                            NodeEdit::OK || t == want)
                        continue;

                    cutEdge = (int)e;
                    cut = make_pair(to.name, arg);
                    splices.push_back(make_pair(0, [=] (string &text)
                        { string w; NodeEdit::Text::disconnect(text, cut.first,
                                                               cut.second, 0,
                                                               w); }));
                }

            /* A box dragged by its title. */
            string dragged;
            pair<double, double> draggedTo;

            for (size_t b = 0; b < g.boxes().size(); b++)
            {
                const NodeGraph::Box &x = g.boxes()[b];

                if (x.attachedTo >= 0 || x.isProbe || x.isControl)
                    continue;

                const double px = x.x + 6, py = x.y + 6;
                const pair<double, double> from = at(x);

                canvas->onPressed(1, px, py);
                canvas->onMotion(px + 23, py + 11);
                canvas->onReleased(1, px + 23, py + 11);

                if (at(g.boxes()[b]) == make_pair(from.first + 23,
                                                  from.second + 11))
                {
                    dragged = NodeLayout::keyFor(g, (int)b);
                    draggedTo = at(g.boxes()[b]);
                    drags++;
                }
                else
                    ok(false, "%s (run %d): %s moves when dragged by its "
                       "title", file.c_str(), run, x.name.c_str());

                break;
            }

            /* A plain value on a node, other than the arg a wire is cut
               from, that the writer will set to 0.25. */
            [&] ()
            {
                for (size_t b = 0; b < g.boxes().size(); b++)
                {
                    const NodeGraph::Box &x = g.boxes()[b];

                    if (x.isIoSource || x.isIoSink || x.isControl ||
                        x.isExpr || x.isProbe)
                        continue;

                    for (const NodeGraph::Param &p : x.params)
                    {
                        string t = want;

                        if (p.kind != NodeGraph::Param::VALUE ||
                            !p.hasValue || p.isOutput || p.isExpr ||
                            p.value == 0.25f ||
                            make_pair(x.name, p.name) == cut ||
                            NodeEdit::Text::setValue(t, x.name, p.name, 0.25,
                                                     why) != NodeEdit::OK ||
                            t == want)
                            continue;

                        const string node = x.name, arg = p.name;

                        ed->onParamEdited((int)b, arg, 0.25);
                        splices.push_back(make_pair(2, [=] (string &text)
                            { string w; NodeEdit::Text::setValue(text, node,
                                                                 arg, 0.25,
                                                                 w); }));
                        values++;
                        return;
                    }
                }
            }();

            /* A press on a slider's track and a release, as a hand would --
               well away from the handle, so that a slider that snaps to whole
               values still lands on another one. The value is whatever the
               slider took. */
            if (run == 0)
                for (size_t b = 0; b < g.boxes().size(); b++)
                {
                    double x0, x1, y, hx;

                    if (!g.boxes()[b].isControl ||
                        !g.sliderGeometry((int)b, x0, x1, y, hx))
                        continue;

                    const float was = g.boxes()[b].ctlValue;
                    const double px = x0 + (x1 - x0) *
                                      (hx < (x0 + x1) / 2 ? 0.875 : 0.125);

                    canvas->onPressed(1, px, y);
                    canvas->onReleased(1, px, y);

                    const string ctl = g.boxes()[b].ctlArg;
                    const double v = g.boxes()[b].ctlValue;

                    if (g.boxes()[b].ctlValue == was)
                        ok(false, "%s: @%s's slider moves when pressed",
                           file.c_str(), ctl.c_str());
                    else
                    {
                        splices.push_back(make_pair(1, [=] (string &text)
                            { string w; NodeEdit::Text::setChanArg(text, ctl,
                                                                   v, w); }));
                        sliders++;
                    }

                    break;
                }

            if (cutEdge >= 0)
            {
                ed->onDisconnect(cutEdge);
                cuts++;
            }

            std::stable_sort(splices.begin(), splices.end(),
                             [] (const auto &a, const auto &b)
                             { return a.first < b.first; });

            for (const auto &sp : splices)
                sp.second(want);

            NodeLayout::PosMap before;
            string added;

            if (run == 1)
            {
                for (size_t b = 0; b < g.boxes().size(); b++)
                    if (g.boxes()[b].attachedTo < 0)
                        before[NodeLayout::keyFor(g, (int)b)] =
                            at(g.boxes()[b]);

                std::set<string> names;

                for (const NodeGraph::Box &x : g.boxes())
                    names.insert(x.name);

                ed->onPaletteAdd("osc::simple");

                for (const NodeGraph::Box &x : ed->graph().boxes())
                    if (!names.count(x.name))
                        added = x.name;

                if (added.empty() ||
                    NodeEdit::Text::addNode(want, added, "osc::simple",
                                            defaults, why) != NodeEdit::OK)
                    ok(false, "%s: a node is added from the palette (%s)",
                       file.c_str(), why.c_str());
                else
                    adds++;
            }

            NodeLayout::Text::write(want, ed->graph());

            ed->onSave();

            const string got = read(copy);

            compared++;

            if (got != want)
            {
                mismatched++;
                ok(false, "Save of %s (run %d) wrote what the same edits make "
                   "through NodeEdit", file.c_str(), run);
            }

            NodeLayout::PosMap saved;

            NodeLayout::Text::read(got, saved);

            if (!dragged.empty() &&
                (!saved.count(dragged) || saved[dragged] != draggedTo))
                ok(false, "%s (run %d): %s is saved where it was dragged "
                   "(%g,%g, not %g,%g)", file.c_str(), run, dragged.c_str(),
                   saved.count(dragged) ? saved[dragged].first : -1.0,
                   saved.count(dragged) ? saved[dragged].second : -1.0,
                   draggedTo.first, draggedTo.second);

            for (const auto &k : before)
                if (!saved.count(k.first) || saved[k.first] != k.second)
                {
                    ok(false, "%s: %s is where it was before a node was "
                       "added", file.c_str(), k.first.c_str());
                    break;
                }

            input = got;
        }

        w->set_visible(false);
        delete w;
    }

    ok(compared == 2 * (int)files.size() && mismatched == 0,
       "Save writes what NodeEdit's own splices make, in %d runs over %d "
       "files (%d differed)", compared, (int)files.size(), mismatched);
    ok(cuts > 0 && sliders > 0 && values > 0 && drags > 0 && adds > 0,
       "...having cut %d wires, moved %d sliders, set %d values, dragged %d "
       "boxes and added %d nodes", cuts, sliders, values, drags, adds);

    std::filesystem::remove(copy);
}

/* The editor holds the patch in memory and writes only the file it saves to,
   so it works with no temporary directory to write in: opening, editing,
   adding a node and saving. */
void checkNoScratchFile (thSynth &synth, const char *file)
{
    const string copy = thUtil::tempFile("editorcheck-scratch-");

    std::filesystem::copy_file(file, copy,
        std::filesystem::copy_options::overwrite_existing);

    TestEditor *ed = Gtk::manage(new TestEditor(&synth));
    Gtk::Window *w = new Gtk::Window();

    w->set_child(*ed);
    w->present();

    pump(0.2);

    /* TMPDIR on POSIX, TMP and TEMP on Windows: none of them anywhere. */
    const char *vars[] = { "TMPDIR", "TMP", "TEMP" };
    std::vector<std::pair<bool, string> > was;

    for (const char *v : vars)
    {
        bool set = false;
        const string value = Glib::getenv(v, set);

        was.push_back(std::make_pair(set, value));
        Glib::setenv(v, "/nonexistent/editorcheck");
    }

    const bool opened = ed->open(copy);
    size_t boxes = 0;

    if (opened)
    {
        boxes = ed->graph().boxes().size();
        ed->onPaletteAdd("osc::simple");
        ed->onSave();
    }

    for (size_t i = 0; i < was.size(); i++)
        if (was[i].first)
            Glib::setenv(vars[i], was[i].second);
        else
            Glib::unsetenv(vars[i]);

    TestEditor *again = Gtk::manage(new TestEditor(&synth));
    Gtk::Window *w2 = new Gtk::Window();

    w2->set_child(*again);

    ok(opened && again->open(copy) &&
       again->graph().boxes().size() == boxes + 1,
       "with no temporary directory, a graph opens, takes a node and saves "
       "it (%d boxes, then %d)", (int)boxes,
       (int)again->graph().boxes().size());

    delete w;
    delete w2;

    std::filesystem::remove(copy);
}

/* Unsaved means the text differs from the file: a box dragged away and back,
   or a probe armed and disarmed, on a file whose layout block already says
   where everything is, leaves nothing to save. */
void checkLayoutUndone (thSynth &synth, const char *file)
{
    const string copy = thUtil::tempFile("editorcheck-undone-");

    std::filesystem::copy_file(file, copy,
        std::filesystem::copy_options::overwrite_existing);

    TestEditor *ed = Gtk::manage(new TestEditor(&synth));
    Gtk::Window *w = new Gtk::Window();

    w->set_child(*ed);
    w->set_default_size(1000, 600);
    w->present();

    pump(0.2);

    std::vector<NodeCanvasWidget *> canvases;

    collectWidgets(ed, canvases);

    const bool opened = ed->open(copy) && !canvases.empty() &&
                        !ed->visuals_.empty();

    ok(opened, "an editor opens a copy of %s", file);

    /* A node with an output and room above it for a panel: one at the top of
       the canvas moves down for its panel, and stays moved. */
    int box = -1;
    string out;

    for (size_t b = 0; opened && b < ed->graph().boxes().size() && box < 0;
         b++)
    {
        const NodeGraph::Box &x = ed->graph().boxes()[b];

        if (x.attachedTo >= 0 || x.isControl || x.isExpr || x.isIoSource ||
            x.isIoSink || x.y < 200)
            continue;

        for (const NodeGraph::Port &p : x.ports)
            if (!p.isInput)
            {
                box = (int)b;
                out = p.name;
                break;
            }
    }

    ok(!opened || box >= 0, "%s has a node with an output and room above it",
       file);

    if (box >= 0)
    {
        /* Saved once, so the file's layout block holds every box. */
        ed->onBoxMoved(0);
        ed->onSave();

        /* Copies: arming a probe adds a box, and moves the rest. */
        const string name = ed->graph().boxes()[box].name;
        const double px = ed->graph().boxes()[box].x + 6;
        const double py = ed->graph().boxes()[box].y + 6;
        const bool clean = !ed->dirty();

        canvases[0]->onPressed(1, px, py);
        canvases[0]->onMotion(px + 40, py + 20);
        canvases[0]->onReleased(1, px + 40, py + 20);

        const bool moved = ed->dirty();

        canvases[0]->onPressed(1, px + 40, py + 20);
        canvases[0]->onMotion(px, py);
        canvases[0]->onReleased(1, px, py);

        ok(clean && moved && !ed->dirty(),
           "a box dragged away is unsaved and dragged back is not (%d %d %d)",
           clean, moved, ed->dirty());

        const bool armed = ed->armProbe(name, out,
                                        ed->visuals_.begin()->first) &&
                           ed->dirty();

        for (size_t i = 0; i < ed->probes_.size(); i++)
            if (ed->probes_[i].node == name && ed->probes_[i].arg == out)
                ed->disarmProbe(i);

        ok(armed && !ed->dirty(),
           "a probe armed is unsaved and disarmed again is not");
    }

    w->set_visible(false);
    delete w;

    std::filesystem::remove(copy);
}

/* A control's slider goes into the text when it is let go. One the file
   spells as arithmetic cannot be written, and goes back to what the text
   says rather than waiting to fail at Save; one moved and put back leaves
   nothing to save. */
void checkControlCommit (thSynth &synth)
{
    const string copy = thUtil::tempFile("editorcheck-control-");

    {
        ofstream out(copy.c_str(), std::ios::binary | std::ios::trunc);

        out << "name \"Ctl\";\n"
               "@lvl = 0.25 + 0.25;\n"
               "@lvl.widget = 1;\n"
               "@lvl.min = 0;\n"
               "@lvl.max = 1;\n"
               "@cut = 0.50;\n"
               "@cut.widget = 1;\n"
               "@cut.min = 0;\n"
               "@cut.max = 1;\n"
               "node ionode { channels = 1; out0 = osc->out; "
               "play = env->play; };\n"
               "node osc osc::simple { freq = ionode->note; amp = @lvl; "
               "mul = @cut; };\n"
               "node env env::adsr { trigger = ionode->trigger; };\n"
               "io ionode;\n";
    }

    TestEditor *ed = Gtk::manage(new TestEditor(&synth));
    Gtk::Window *w = new Gtk::Window();

    w->set_child(*ed);
    w->set_default_size(1000, 600);
    w->present();

    pump(0.2);

    std::vector<NodeCanvasWidget *> canvases;

    collectWidgets(ed, canvases);

    const bool opened = ed->open(copy) && !canvases.empty();

    ok(opened, "an editor opens a graph with two controls");

    const auto control = [&] (const string &name)
    {
        for (size_t b = 0; opened && b < ed->graph().boxes().size(); b++)
            if (ed->graph().boxes()[b].isControl &&
                ed->graph().boxes()[b].ctlArg == name)
                return (int)b;

        return -1;
    };

    const int lvl = control("lvl");
    const int cut = control("cut");
    double x0, x1, y, hx;

    if (lvl >= 0 && ed->graph().sliderGeometry(lvl, x0, x1, y, hx))
    {
        const float was = ed->graph().boxes()[lvl].ctlValue;
        const double px = x0 + (x1 - x0) * (hx < (x0 + x1) / 2 ? 0.875 : 0.125);

        canvases[0]->onPressed(1, px, y);
        canvases[0]->onReleased(1, px, y);

        ok(ed->graph().boxes()[lvl].ctlValue == was && !ed->dirty(),
           "a slider the file spells as arithmetic goes back when let go "
           "(%g, not %g), with nothing to save",
           ed->graph().boxes()[lvl].ctlValue, was);
    }
    else
        ok(false, "@lvl has a slider");

    if (cut >= 0)
    {
        const double was = ed->graph().boxes()[cut].ctlValue;

        ed->onControlChanged(cut, 0.75, true);

        const bool moved = ed->dirty();

        ed->onControlChanged(cut, was, true);

        ok(moved && !ed->dirty(),
           "a slider moved is unsaved and put back is not, the file's "
           "`0.50' spelled as it was");
    }
    else
        ok(false, "@cut is a box");

    delete w;

    std::filesystem::remove(copy);
}

/* A wire goes into the text when it is made, in the order things were done:
   a value set and then wired over saves as the wire, where it used to fail at
   Save. And cutting the wire out of an expression takes the expression off
   the canvas there and then. */
void checkWireCommit (thSynth &synth)
{
    const string copy = thUtil::tempFile("editorcheck-wire-");

    {
        ofstream out(copy.c_str(), std::ios::binary | std::ios::trunc);

        out << "name \"Wire\";\n"
               "node ionode { channels = 1; out0 = mix->out; "
               "play = env->play; };\n"
               "node lfo osc::simple { freq = 2; };\n"
               "node osc osc::simple { freq = ionode->note; amp = 1; };\n"
               "node env env::adsr { trigger = ionode->trigger; };\n"
               "node mix mixer::mul { in0 = osc->out; "
               "in1 = env->out * ionode->velocity; };\n"
               "io ionode;\n";
    }

    TestEditor *ed = Gtk::manage(new TestEditor(&synth));
    Gtk::Window *w = new Gtk::Window();

    w->set_child(*ed);
    w->present();

    pump(0.2);

    ok(ed->open(copy), "an editor opens a graph with a wire to make");

    const auto port = [&] (const string &box, const string &name, bool in,
                           int &b, int &p)
    {
        b = ed->graph().boxByName(box);
        p = -1;

        if (b >= 0)
            for (size_t k = 0; k < ed->graph().boxes()[b].ports.size(); k++)
                if (ed->graph().boxes()[b].ports[k].name == name &&
                    ed->graph().boxes()[b].ports[k].isInput == in)
                    p = (int)k;

        return b >= 0 && p >= 0;
    };

    int lfo, lfoOut, osc, oscAmp;

    if (port("lfo", "out", false, lfo, lfoOut) &&
        port("osc", "amp", true, osc, oscAmp))
    {
        /* The file says nowhere where its boxes go, and the wire moves lfo
           a column -- but none of them moves on the canvas. */
        NodeLayout::PosMap before;

        for (size_t b = 0; b < ed->graph().boxes().size(); b++)
            if (ed->graph().boxes()[b].attachedTo < 0)
                before[NodeLayout::keyFor(ed->graph(), (int)b)] =
                    make_pair(ed->graph().boxes()[b].x,
                              ed->graph().boxes()[b].y);

        ed->onParamEdited(osc, "amp", 0.5);
        ed->onConnect(lfo, lfoOut, osc, oscAmp);

        string moved;

        for (size_t b = 0; b < ed->graph().boxes().size(); b++)
        {
            const string key = NodeLayout::keyFor(ed->graph(), (int)b);

            if (before.count(key) &&
                before[key] != make_pair(ed->graph().boxes()[b].x,
                                         ed->graph().boxes()[b].y))
                moved += " " + key;
        }

        ok(moved.empty(), "wiring moves no box on the canvas (moved:%s)",
           moved.c_str());

        ed->onSave();

        string saved;

        {
            ifstream in(copy.c_str(), std::ios::binary);

            saved.assign((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        }

        ok(!ed->dirty() && saved.find("amp = lfo->out") != string::npos,
           "a value set and then wired over saves as the wire");
    }
    else
        ok(false, "lfo has an output and osc an amp input");

    int expr = -1;

    for (size_t b = 0; b < ed->graph().boxes().size(); b++)
        if (ed->graph().boxes()[b].isExpr)
            expr = (int)b;

    int cut = -1;

    for (size_t e = 0; expr >= 0 && e < ed->graph().edges().size(); e++)
        if (ed->graph().edges()[e].fromBox == expr)
            cut = (int)e;

    ok(cut >= 0, "the graph has an expression with a wire out of it");

    if (cut >= 0)
    {
        ed->onDisconnect(cut);

        bool gone = true;

        for (const NodeGraph::Box &b : ed->graph().boxes())
            if (b.isExpr)
                gone = false;

        ok(gone, "cutting the wire out of an expression takes it off the "
           "canvas at once");
    }

    delete w;

    std::filesystem::remove(copy);
}

/* A value typed into the panel goes into the text as it is typed, and the
   graph says it at once. One the writer cannot spell is refused there and
   then, and one typed back to what the file says leaves the file's spelling. */
void checkValueCommit (thSynth &synth)
{
    const string copy = thUtil::tempFile("editorcheck-value-");

    {
        ofstream out(copy.c_str(), std::ios::binary | std::ios::trunc);

        out << "name \"Value\";\n"
               "node ionode { channels = 1; out0 = osc->out; "
               "play = env->play; };\n"
               "node osc osc::simple { freq = ionode->note; amp = 1.0; };\n"
               "node env env::adsr { trigger = ionode->trigger; };\n"
               "io ionode;\n";
    }

    TestEditor *ed = Gtk::manage(new TestEditor(&synth));
    Gtk::Window *w = new Gtk::Window();

    w->set_child(*ed);
    w->present();

    pump(0.2);

    ok(ed->open(copy), "an editor opens a graph with a value to set");

    const int osc = ed->graph().boxByName("osc");

    const auto amp = [&] ()
    {
        for (const NodeGraph::Param &p : ed->graph().boxes()[osc].params)
            if (p.name == "amp")
                return (double)p.value;

        return -1.0;
    };

    if (osc >= 0)
    {
        ed->onParamEdited(osc, "amp", 0.5);

        ok(ed->dirty() && amp() == 0.5,
           "a value typed in is unsaved, and the graph says it (%g)", amp());

        const string before = ed->text();

        ed->onParamEdited(osc, "amp", 1e31);

        ok(amp() == 0.5 && ed->text() == before,
           "a value the writer cannot spell is refused, the text and the "
           "graph as they were (%g)", amp());

        ed->onParamEdited(osc, "amp", 1);

        ok(!ed->dirty(), "a value typed back leaves nothing to save, the "
           "file's `1.0' spelled as it was");
    }
    else
        ok(false, "osc is a box");

    /* One element of an array is the block that makes them all. */
    {
        ofstream out(copy.c_str(), std::ios::binary | std::ios::trunc);

        out << "name \"Array\";\n"
               "node ionode { channels = 2; out[] = tone[]->out; "
               "play = env->play; };\n"
               "node tone[2] osc::simple { freq = ionode->note; amp = 1; };\n"
               "node env env::adsr { trigger = ionode->trigger; };\n"
               "io ionode;\n";
    }

    const auto ampOf = [&] (const string &name)
    {
        const int b = ed->graph().boxByName(name);

        if (b >= 0)
            for (const NodeGraph::Param &p : ed->graph().boxes()[b].params)
                if (p.name == "amp")
                    return (double)p.value;

        return -1.0;
    };

    const int one = ed->open(copy) ? ed->graph().boxByName("tone[1]") : -1;

    ok(one >= 0, "an editor opens a graph with an array of two");

    if (one >= 0)
    {
        ed->onParamEdited(one, "amp", 0.5);

        ok(ampOf("tone[0]") == 0.5 && ampOf("tone[1]") == 0.5,
           "a value set on one element of an array is every element's "
           "(%g, %g)", ampOf("tone[0]"), ampOf("tone[1]"));
    }

    delete w;

    std::filesystem::remove(copy);
}

/* A structural edit whose result will not parse leaves the text as it was:
   the canvas still shows the old graph, and Save would otherwise write a file
   that does not load. A node from a plugin that will not load -- one the
   palette lists from the directory -- is such an edit. */
void checkFailedReparse (thSynth &synth, const char *file)
{
    const string copy = thUtil::tempFile("editorcheck-reparse-");

    std::filesystem::copy_file(file, copy,
        std::filesystem::copy_options::overwrite_existing);

    TestEditor *ed = Gtk::manage(new TestEditor(&synth));
    Gtk::Window *w = new Gtk::Window();

    w->set_child(*ed);
    w->present();

    pump(0.2);

    ok(ed->open(copy), "an editor opens a copy of %s", file);

    const string before = ed->text();

    ed->onPaletteAdd("osc::notaplugin");

    ok(ed->text() == before && !ed->dirty(),
       "a node whose plugin will not load leaves the text as it was");

    delete w;

    std::filesystem::remove(copy);
}

/* What a graph says: its boxes and wires, and every number it shows. Positions
   and probe panels left out -- the one is where things are drawn and the
   other is not in the patch. */
struct GraphSays {
    std::multiset<string> shape;
    std::map<string, double> numbers;
};

GraphSays graphSays (const NodeGraph &g)
{
    GraphSays out;

    for (const NodeGraph::Box &b : g.boxes())
    {
        if (b.isProbe)
            continue;

        out.shape.insert("box " + b.name + (b.isIoSource ? "#in" : "") +
                         (b.isIoSink ? "#out" : "") +
                         (b.isControl ? " control" : "") +
                         (b.isExpr ? " expr" : ""));

        if (b.isControl)
            out.numbers[b.name] = b.ctlValue;

        for (const NodeGraph::Param &p : b.params)
            if (p.hasValue && p.kind == NodeGraph::Param::VALUE)
                out.numbers[b.name + "." + p.name] = p.value;
    }

    for (const NodeGraph::Edge &e : g.edges())
        out.shape.insert("wire " + g.boxes()[e.fromBox].name + "." +
                         g.boxes()[e.fromBox].ports[e.fromPort].name + " -> " +
                         g.boxes()[e.toBox].name + "." +
                         g.boxes()[e.toBox].ports[e.toPort].name);

    return out;
}

/* The same graph, or the first thing that differs. Numbers to a part in a
   hundred thousand: a value written with a unit comes back from the text a
   float's last bit away from the one typed. */
bool graphsAgree (const NodeGraph &a, const NodeGraph &b, string &why)
{
    const GraphSays x = graphSays(a), y = graphSays(b);

    for (const string &l : x.shape)
        if (!y.shape.count(l))
        {
            why = "only the text has " + l;
            return false;
        }

    for (const string &l : y.shape)
        if (!x.shape.count(l))
        {
            why = "only the canvas has " + l;
            return false;
        }

    for (const auto &n : x.numbers)
    {
        const auto m = y.numbers.find(n.first);

        if (m == y.numbers.end() ||
            std::fabs(m->second - n.second) >
                1e-5 * std::max(1.0, std::fabs(n.second)))
        {
            char buf[96];

            snprintf(buf, sizeof(buf), " is %g in the text and %g on the "
                     "canvas", n.second,
                     m == y.numbers.end() ? 0.0 : m->second);
            why = n.first + buf;
            return false;
        }
    }

    return true;
}

/* Seeded runs of random edits through the editor's handlers and gestures,
 * over the corpus. After every edit, two things hold:
 *
 *   - the editor's text is what the same edits make applied straight through
 *     NodeEdit::Text, in the order they were made, each refused one skipped;
 *   - the graph on the canvas says what a fresh parse of that text says, so
 *     what is changed in place never drifts from the text.
 *
 * And Save writes the text, with the layout block, to the file. */
void checkRandomEdits (thSynth &synth, const string &pluginPath)
{
    const char *dsps = getenv("THINK_DSP_PATH");
    std::error_code ec;

    if (dsps == NULL || !std::filesystem::is_directory(dsps, ec))
    {
        ok(false, "THINK_DSP_PATH names the corpus");
        return;
    }

    vector<string> files;

    for (const auto &e : std::filesystem::recursive_directory_iterator(dsps))
        if (e.path().extension() == ".dsp")
            files.push_back(e.path().string());

    std::sort(files.begin(), files.end());

    NodeCatalog catalog;
    NodeCatalog::Entry simple;

    catalog.scan(pluginPath);
    catalog.describe("osc::simple", synth.getPluginManager(), simple);

    vector<pair<string, double> > defaults;

    for (const NodeCatalog::Default &d : simple.defaults)
        defaults.push_back(make_pair(d.name, d.value));

    const string copy = thUtil::tempFile("editorcheck-random-");

    const auto read = [] (const string &path)
    {
        ifstream in(path.c_str(), std::ios::binary);

        return string((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    };

    const double values[] = { 0, 0.25, 0.5, 1, 2, 440, 1e31 };
    const int STEPS = 24;
    int runs = 0, made = 0, refused = 0, diverged = 0;

    for (size_t f = 0; f < files.size(); f++)
    {
        const string orig = read(files[f]);

        {
            ofstream out(copy.c_str(), std::ios::binary | std::ios::trunc);

            out << orig;
        }

        TestEditor *ed = Gtk::manage(new TestEditor(&synth));
        Gtk::Window *w = new Gtk::Window();

        w->set_child(*ed);
        w->set_default_size(1000, 600);
        w->present();

        pump(0.02);

        std::vector<NodeCanvasWidget *> canvases;

        collectWidgets(ed, canvases);

        if (canvases.empty() || !ed->open(copy))
        {
            ok(false, "the editor opens %s", files[f].c_str());
            delete w;
            continue;
        }

        NodeCanvas *canvas = canvases[0];
        std::mt19937 rng((unsigned)(f + 1));
        string want = orig;
        string why;
        bool ok_ = true;

        const auto pick = [&] (size_t n) { return (size_t)(rng() % n); };

        for (int step = 0; step < STEPS && ok_; step++)
        {
            const NodeGraph &g = ed->graph();
            const size_t nb = g.boxes().size();
            const int op = (int)pick(6);
            const string prev = want;
            string what;

            if (op == 0)
            {
                /* A value typed into the panel. */
                const int b = (int)pick(nb);
                const NodeGraph::Box &x = g.boxes()[b];
                vector<string> args;

                for (const NodeGraph::Param &p : x.params)
                    if (p.kind == NodeGraph::Param::VALUE && p.hasValue &&
                        !p.isOutput && !p.isExpr && !x.isControl &&
                        !x.isProbe && !x.isExpr)
                        args.push_back(p.name);

                if (args.empty())
                    continue;

                const string arg = args[pick(args.size())];
                const double v = values[pick(7)];
                const string node = x.name;

                what = "value " + node + "." + arg;

                if (NodeEdit::Text::setValue(want, node, arg, v, orig, why) !=
                    NodeEdit::OK)
                    refused++;

                ed->onParamEdited(b, arg, v);
            }
            else if (op == 1)
            {
                /* A slider pressed somewhere along its track. */
                vector<int> ctls;

                for (size_t b = 0; b < nb; b++)
                    if (g.boxes()[b].isControl)
                        ctls.push_back((int)b);

                if (ctls.empty())
                    continue;

                const int b = ctls[pick(ctls.size())];
                double x0, x1, y, hx;

                if (!g.sliderGeometry(b, x0, x1, y, hx))
                    continue;

                const double px = x0 + (x1 - x0) * (double)pick(9) / 8;

                /* Strips sit close: the slider pressed is whichever the
                   point is on. */
                const int hit = g.sliderAt(px, y);

                if (hit < 0)
                    continue;

                const string ctl = g.boxes()[hit].ctlArg;

                what = "slider @" + ctl;

                canvas->onPressed(1, px, y);

                const double v = g.boxes()[hit].ctlValue;

                canvas->onReleased(1, px, y);

                if (NodeEdit::Text::setChanArg(want, ctl, v, orig, why) !=
                    NodeEdit::OK)
                    refused++;
            }
            else if (op == 2)
            {
                /* A wire drawn from an output to an input. */
                const int a = (int)pick(nb), z = (int)pick(nb);
                const NodeGraph::Box &from = g.boxes()[a], &to = g.boxes()[z];

                if (from.ports.empty() || to.ports.empty())
                    continue;

                const int ap = (int)pick(from.ports.size());
                const int zp = (int)pick(to.ports.size());

                if (!g.canConnect(a, ap, z, zp, why))
                    continue;

                const string node = to.name, arg = to.ports[zp].name;

                what = "wire into " + node + "." + arg;

                const NodeEdit::Result r = from.isControl
                    ? NodeEdit::Text::connectControl(want, node, arg,
                                                     from.ctlArg, why)
                    : NodeEdit::Text::connect(want, node, arg, from.name,
                                              from.ports[ap].name, why);

                if (r != NodeEdit::OK)
                    refused++;

                ed->onConnect(a, ap, z, zp);
            }
            else if (op == 3)
            {
                /* A wire cut. */
                if (g.edges().empty())
                    continue;

                const int e = (int)pick(g.edges().size());
                const NodeGraph::Box &to = g.boxes()[g.edges()[e].toBox];

                if (to.isExpr)
                    continue;

                const string node = to.name;
                const string arg = to.ports[g.edges()[e].toPort].name;

                what = "cut " + node + "." + arg;

                if (NodeEdit::Text::disconnect(want, node, arg, 0, why) !=
                    NodeEdit::OK)
                    refused++;

                ed->onDisconnect(e);
            }
            else if (op == 4)
            {
                /* A node added from the palette. */
                std::set<string> names;

                for (const NodeGraph::Box &x : g.boxes())
                    names.insert(x.name);

                what = "add osc::simple";

                ed->onPaletteAdd("osc::simple");

                string added;

                for (const NodeGraph::Box &x : ed->graph().boxes())
                    if (!names.count(x.name))
                        added = x.name;

                if (added.empty() ||
                    NodeEdit::Text::addNode(want, added, "osc::simple",
                                            defaults, why) != NodeEdit::OK)
                    refused++;
                else
                    NodeLayout::Text::write(want, ed->graph());
            }
            else
            {
                /* A plain node selected and deleted. */
                vector<int> nodes;

                for (size_t b = 0; b < nb; b++)
                {
                    const NodeGraph::Box &x = g.boxes()[b];

                    if (!x.isIoSource && !x.isIoSink && !x.isControl &&
                        !x.isExpr && !x.isProbe && x.name.find('[') ==
                        string::npos)
                        nodes.push_back((int)b);
                }

                if (nodes.empty())
                    continue;

                const string node = g.boxes()[nodes[pick(nodes.size())]].name;
                int n = 0;

                what = "delete " + node;

                if (NodeEdit::Text::removeNode(want, node, n, why) !=
                    NodeEdit::OK)
                    refused++;

                canvas->setSelected(g.boxByName(node));
                ed->onDeleteNode();
            }

            made++;

            /* An edit whose text will not parse is refused, the editor
               keeping the text it had. */
            if (want != prev)
            {
                thSynthTree *t = synth.parseTreeText(files[f], want);

                if (t == NULL)
                {
                    want = prev;
                    refused++;
                }

                delete t;
            }

            /* The drag-less edits leave positions alone, so the text is
               held without its layout block on both sides. */
            string have = ed->text(), mine = want;

            NodeLayout::Text::write(have, ed->graph());
            NodeLayout::Text::write(mine, ed->graph());

            NodeGraph fresh;
            thSynthTree *tree = synth.parseTreeText(files[f], ed->text());
            const bool built = tree != NULL && fresh.build(tree);

            delete tree;

            if (have != mine)
            {
                ok(false, "%s, seed %d, step %d (%s): the editor's text is "
                   "what NodeEdit makes of the same edits", files[f].c_str(),
                   (int)f + 1, step, what.c_str());
                ok_ = false;
            }
            else if (!built || !graphsAgree(fresh, ed->graph(), why))
            {
                ok(false, "%s, seed %d, step %d (%s): the canvas says what "
                   "the text does (%s)", files[f].c_str(), (int)f + 1, step,
                   what.c_str(), built ? why.c_str() : "it does not parse");
                ok_ = false;
            }
        }

        if (ok_)
        {
            string mine = ed->text();

            NodeLayout::Text::write(mine, ed->graph());
            ed->onSave();

            if (read(copy) != mine)
            {
                ok(false, "%s: Save writes the text and its layout",
                   files[f].c_str());
                ok_ = false;
            }
        }

        diverged += !ok_;
        runs++;

        delete w;
    }

    ok(runs == (int)files.size() && diverged == 0 && made > runs * STEPS / 2 &&
       refused > 0,
       "%d random edits over %d files keep the text, the canvas and the file "
       "agreeing (%d refused by the writer, %d files diverged)", made, runs,
       refused, diverged);

    std::filesystem::remove(copy);
}

/* Opening a graph that will not parse changes nothing: the next Save still
   writes the graph on screen to where it came from. From a file and from a
   piece's text, since a piece's graph carries its own save. */
void checkFailedOpen (thSynth &synth, const char *file)
{
    const string good = thUtil::tempFile("editorcheck-open-");
    const string bad = thUtil::tempFile("editorcheck-open-bad-");

    std::filesystem::copy_file(file, good,
        std::filesystem::copy_options::overwrite_existing);

    {
        ofstream out(bad.c_str(), std::ios::binary | std::ios::trunc);

        out << "not a graph\n";
    }

    TestEditor *ed = Gtk::manage(new TestEditor(&synth));
    Gtk::Window *w = new Gtk::Window();

    w->set_child(*ed);
    w->present();

    pump(0.2);

    const auto read = [] (const string &path)
    {
        ifstream in(path.c_str(), std::ios::binary);

        return string((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    };

    bool pieceSaved = false;

    ok(ed->open(good), "an editor opens a copy of %s", file);

    ed->onBoxMoved(0);

    ok(!ed->open(bad), "and refuses a file that does not parse");
    ok(!ed->openInline("bad#0.dsp", "not a graph\n", -1,
                       [&pieceSaved](const string &, string &, string &)
                       {
                           pieceSaved = true;
                           return true;
                       }),
       "and a piece's graph that does not parse");

    ed->onSave();

    ok(!pieceSaved && read(good).find("not a graph") == string::npos,
       "and Save after both still writes the graph it shows to its file, "
       "and nothing to the piece");

    w->set_visible(false);
    delete w;

    std::filesystem::remove(good);
    std::filesystem::remove(bad);
}

/* A save that takes a box out of the graph -- cutting the wire out of an
   expression removes the whole expression -- keeps the box that was selected
   selected, though the reparse moves every box after the one removed. */
void checkSelectionAfterSave (thSynth &synth)
{
    const string body =
        "name \"Sel\";\n"
        "node ionode { channels = 1; out0 = out->out; play = env->play; };\n"
        "node osc osc::simple { freq = ionode->note; };\n"
        "node env env::adsr { trigger = ionode->trigger; };\n"
        "node out mixer::mul {\n"
        "    in0 = osc->out;\n"
        "    in1 = env->out * ionode->velocity;\n"
        "};\n"
        "node tail math::min { in0 = osc->out * ionode->velocity; };\n"
        "io ionode;\n";

    TestEditor *ed = Gtk::manage(new TestEditor(&synth));
    Gtk::Window *w = new Gtk::Window();

    w->set_child(*ed);
    w->set_default_size(1000, 600);
    w->present();

    pump(0.2);

    const NodeEditor::InlineSave save =
        [](const string &, string &name, string &)
        {
            name = "sel#0.dsp";
            return true;
        };

    ok(ed->openInline("sel#0.dsp", body, -1, save),
       "an editor opens a graph with an expression in it");

    std::vector<NodeCanvasWidget *> canvases;

    collectWidgets(ed, canvases);

    NodeCanvas *canvas = canvases.empty() ? NULL : canvases[0];

    ok(canvas != NULL, "the editor has a canvas");

    if (canvas == NULL)
        return;

    const NodeGraph &g = ed->graph();
    int cut = -1;

    for (size_t e = 0; e < g.edges().size(); e++)
        if (g.boxes()[g.edges()[e].fromBox].name == "out.in1")
            cut = (int)e;

    std::vector<string> names;

    for (const NodeGraph::Box &b : g.boxes())
        names.push_back(b.name);

    ok(cut >= 0, "out.in1's expression has a wire out of it");

    for (const string &want : names)
    {
        if (want == "out.in1")
            continue;

        if (!ed->openInline("sel#0.dsp", body, -1, save))
            continue;

        int box = -1;

        for (size_t b = 0; b < ed->graph().boxes().size(); b++)
            if (ed->graph().boxes()[b].name == want)
                box = (int)b;

        canvas->setSelected(box);
        ed->onDisconnect(cut);
        ed->onSave();

        const int sel = canvas->selected();

        ok(sel >= 0 && ed->graph().boxes()[sel].name == want,
           "%s is still the box selected after a save takes the expression "
           "out (selected: %s)", want.c_str(),
           sel >= 0 ? ed->graph().boxes()[sel].name.c_str() : "nothing");
    }

    w->set_visible(false);
    delete w;
}

int run (const string &pluginPath, const char *file)
{
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    if (synth.loadTree(file, 0, 100) == NULL)
    {
        printf("FAIL  could not load %s\n", file);
        return 1;
    }

    /* The editor is a widget, so it needs a window to live in -- and it needs
       to be realized before its canvas will draw. */
    Gtk::Window *window = new Gtk::Window();
    TestEditor *ed = Gtk::manage(new TestEditor(&synth));

    window->set_child(*ed);
    window->set_default_size(1000, 600);
    window->present();

    pump(0.3);

    ok(!ed->visuals_.empty(),
       "the editor found at least one visual module in %svisual/",
       pluginPath.c_str());

    ok(ed->open(file, 0), "the editor opens %s on channel 0", file);

    pump(0.2);

    /* The palette and the parameter panel start collapsed, the graph
       taking the pane, and each toggle opens its own and closes it
       again. */
    ok(!ed->paletteShown() && !ed->paramsShown(),
       "the palette and the parameter panel start collapsed");

    ed->showPanels(true, true);
    pump(0.2);

    ok(ed->paletteShown() && ed->paramsShown(),
       "...and their toggles open them");

    ed->showPanels(false, false);
    pump(0.2);

    ok(!ed->paletteShown() && !ed->paramsShown(),
       "...and close them again");

    /* The graph as a list: List shows it under the canvas, a row chosen is
       its box chosen, and the chosen one's wire is cut, made again from
       the Connect row, and the box deleted, each in the graph. */
    {
        ed->listBtn_.set_active(true);
        pump(0.2);

        /* A button under the list's wiring panel, by its label, in the row
           whose first label starts with `in' if there is one. */
        auto button = [&](const std::string &label,
                          const std::string &in = "") -> Gtk::Button *
        {
            std::vector<Gtk::Button *> all;

            collectWidgets<Gtk::Button>(&ed->wiring_, all);

            for (Gtk::Button *b : all)
            {
                Gtk::Label *first = dynamic_cast<Gtk::Label *>(
                    b->get_parent()->get_first_child());

                if (b->get_label().raw() == label &&
                    (in.empty() || (first != NULL &&
                                    first->get_text().raw().find(in) == 0)))
                    return b;
            }

            return NULL;
        };

        int row = -1;
        NodeGraph::Edge wire{};

        /* A node's, not an expression's, whose inputs are its text's. */
        for (size_t r = 0; r < ed->listed_.size() && row < 0; r++)
            for (const NodeGraph::Edge &e : ed->graph().edges())
                if (e.toBox == ed->listed_[r] &&
                    !ed->graph().boxes()[e.toBox].isExpr &&
                    !ed->graph().boxes()[e.toBox].isIoSink)
                {
                    row = (int)r;
                    wire = e;
                    break;
                }

        ok(row >= 0, "the list has a box with a wire into it");

        if (row >= 0)
        {
            const std::vector<NodeGraph::Box> &boxes = ed->graph().boxes();
            const std::string name = boxes[wire.toBox].name;
            const std::string input = boxes[wire.toBox].ports[wire.toPort].name;
            const std::string source = boxes[wire.fromBox].name + "." +
                boxes[wire.fromBox].ports[wire.fromPort].name;
            const size_t edges = ed->graph().edges().size();

            ed->boxList_.get_row_at_index(row)->activate();
            pump(0.1);

            Gtk::Button *cut = button("Disconnect", input + " from ");

            if (cut != NULL)
                g_signal_emit_by_name(cut->gobj(), "clicked");
            pump(0.2);

            const size_t cutTo = ed->graph().edges().size();

            /* The Connect row: its input menu, then its source menu. */
            Gtk::Widget *go = button("Connect");
            Gtk::DropDown *to = NULL, *from = NULL;

            for (Gtk::Widget *c = go == NULL ? NULL
                     : go->get_parent()->get_first_child();
                 c != NULL; c = c->get_next_sibling())
                if (Gtk::DropDown *d = dynamic_cast<Gtk::DropDown *>(c))
                    (to == NULL ? to : from) = d;

            auto pick = [](Gtk::DropDown *d, const std::string &what)
            {
                auto list = std::dynamic_pointer_cast<Gtk::StringList>(
                    d->get_model());

                for (guint i = 0; list && i < list->get_n_items(); i++)
                    if (list->get_string(i).raw() == what)
                        d->set_selected(i);
            };

            if (to != NULL && from != NULL)
            {
                pick(to, input);
                pick(from, source);
                g_signal_emit_by_name(go->gobj(), "clicked");
            }
            pump(0.2);

            bool back = false;

            for (const NodeGraph::Edge &e : ed->graph().edges())
                back |= e.toBox == wire.toBox && e.toPort == wire.toPort &&
                        e.fromBox == wire.fromBox && e.fromPort == wire.fromPort;

            ok(cut != NULL && cutTo == edges - 1 && back &&
               ed->graph().edges().size() == edges,
               "...whose wire %s.%s from %s is cut from the list and made "
               "again from its Connect row", name.c_str(), input.c_str(),
               source.c_str());

            Gtk::Button *gone = button("Delete " + name);

            if (gone != NULL)
                g_signal_emit_by_name(gone->gobj(), "clicked");
            pump(0.2);

            bool still = false;

            for (const NodeGraph::Box &b : ed->graph().boxes())
                still |= b.name == name;

            ok(gone != NULL && !still,
               "...and %s deleted from the list is gone from the graph",
               name.c_str());

            /* The file as it was: the delete is in the working copy, which
               a reload would read back. */
            ed->open(file, 0);
            pump(0.2);
        }

        ed->listBtn_.set_active(false);
        pump(0.1);
    }

    ok(ed->panels() == 0, "a freshly opened patch carries no panels");

    /* Somewhere to point it: the first output port of the first node box. */
    string node, arg;

    for (size_t b = 0; b < ed->graph().boxes().size() && node.empty(); b++)
    {
        const NodeGraph::Box &bx = ed->graph().boxes()[b];

        if (bx.isControl || bx.isProbe)
            continue;

        for (size_t p = 0; p < bx.ports.size(); p++)
            if (!bx.ports[p].isInput)
            {
                node = bx.name;
                arg = bx.ports[p].name;
                break;
            }
    }

    if (node.empty())
    {
        printf("FAIL  %s has no output port to probe\n", file);
        return failures + 1;
    }

    const string visual = ed->visuals_.begin()->first;

    ok(ed->armProbe(node, arg, visual),
       "arming %s on %s.%s", visual.c_str(), node.c_str(), arg.c_str());

    ok(ed->probes_.size() == 1, "one probe is armed (%d)",
       (int)ed->probes_.size());
    ok(ed->panels() == 1, "and one panel is on the canvas (%d)",
       ed->panels());

    if (ed->probes_.empty())
        return failures + 1;

    ok(ed->probes_[0].slot >= 0,
       "the engine gave it a tap slot (%d)", ed->probes_[0].slot);
    ok(ed->probes_[0].inst != NULL, "the module opened an instance");

    /* Arming the same point again must not stack a second panel. */
    ed->armProbe(node, arg, visual);
    ok(ed->probes_.size() == 1 && ed->panels() == 1,
       "arming the same point twice changes nothing (%d probes, %d panels)",
       (int)ed->probes_.size(), ed->panels());

    /* Now make a noise and let the frame tick run. process() is called here
       rather than by an audio thread: what is being tested is the drain and
       the feed, not the threading, which dspstress covers. */
    synth.addNote(0, 60, 110);
    synth.addNote(0, 64, 110);
    synth.addNote(0, 67, 110);

    for (int w = 0; w < 200; w++)
        synth.process();

    pump(0.4);   /* several frame ticks */

    ok(drewSomething(ed->probes_[0].module, ed->probes_[0].inst),
       "the module drew something it would not have drawn unfed -- samples "
       "reached the screen");

    /* A reload renumbers every box and drops every tap. The probe has to come
       back on both counts, which is the whole reason it is keyed on names. */
    const int slotBefore = ed->probes_[0].slot;

    ok(ed->reload(), "the file reloads");

    pump(0.2);

    ok(ed->probes_.size() == 1, "the probe survives a reload");
    ok(ed->panels() == 1, "and so does its panel (%d)", ed->panels());
    ok(ed->probes_[0].slot >= 0,
       "and it has a tap again (%d, was %d)", ed->probes_[0].slot, slotBefore);

    /* A probe whose tap the engine dropped gets another one.
     *
     * The engine disarms every probe on a channel when a patch is loaded onto
     * it -- the node ids a probe holds are only meaningful against the tree
     * they were measured on. The editor is supposed to notice and re-arm on
     * the next frame. It could not: the tick skipped anything with no slot
     * before it reached the code that would have asked for one, so a probe
     * that lost its tap was silent for good and only a reload of the *editor*
     * brought it back.
     *
     * Provoked directly rather than by loading a patch, because a reload also
     * rebuilds the graph and would hide the thing being tested behind the
     * re-arm that reapplyProbes does anyway. */
    {
        ok(!ed->probes_.empty(), "a probe to drop the tap of");

        if (!ed->probes_.empty() && ed->probes_[0].slot >= 0)
        {
            const int had = ed->probes_[0].slot;

            synth.disarmProbe(had);
            synth.process();      /* let the audio side apply it */

            ok(synth.probe(had) == NULL, "the engine dropped the tap");

            pump(0.3);            /* several frame ticks */

            ok(!ed->probes_.empty() && ed->probes_[0].slot >= 0,
               "and the editor asked for another one (slot %d)",
               ed->probes_.empty() ? -1 : ed->probes_[0].slot);
        }
    }

    /* The enlarged view: the same instance, drawn bigger. */
    {
        const int panel = ed->firstPanel();

        ok(panel >= 0, "there is a panel to enlarge");

        if (panel >= 0)
        {
            ed->onProbeActivated(panel);

            ok(ed->enlarged_.size() == 1, "double-clicking it opens a window "
               "(%d)", (int)ed->enlarged_.size());

            /* Twice must not open two windows on one signal: they would be
               two views of one instance with no way to tell them apart. */
            ed->onProbeActivated(panel);

            ok(ed->enlarged_.size() == 1,
               "and doing it again raises that one rather than opening a "
               "second (%d)", (int)ed->enlarged_.size());

            pump(0.2);

            /* It has to reach the module. paintEnlarged looks the probe up by
               name every frame, so what is being checked here is that lookup
               -- an index cached when the window opened would still work
               today and break after the next reload. */
            if (ed->enlarged_.size() == 1)
            {
                Cairo::RefPtr<Cairo::ImageSurface> big =
                    Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32,
                                                400, 200);
                Cairo::RefPtr<Cairo::ImageSurface> blank =
                    Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32,
                                                400, 200);

                {
                    Cairo::RefPtr<Cairo::Context> cr =
                        Cairo::Context::create(big);

                    ed->paintEnlarged(cr, 400, 200, node, arg);
                }

                big->flush();
                blank->flush();

                ok(memcmp(big->get_data(), blank->get_data(),
                          (size_t)big->get_stride() * 200) != 0,
                   "and it draws through the module rather than staying "
                   "blank");
            }

            /* The title bar's close button, which is what close() sends.
             *
             * The lifecycle, not a crash: closing a window must leave it in
             * the list until a tick reaps it, and reopening must then work.
             * Deliberately not claimed as a use-after-free guard -- removing
             * the close-request handler passes this and passes under ASan
             * too, because gtkmm's wrapper holds its own reference and the
             * window survives GTK destroying it. The handler is there so the
             * list decides when a window goes; see NodeEditor.cpp. */
            if (ed->enlarged_.size() == 1)
            {
                ed->enlarged_[0].win->close();

                ok(ed->enlarged_.size() == 1,
                   "the close button hides the window rather than destroying "
                   "it under us");

                pump(0.3);

                ok(ed->enlarged_.empty(),
                   "and the next tick reaps it (%d left)",
                   (int)ed->enlarged_.size());

                /* Reopened, so the checks after this still have one. */
                ed->onProbeActivated(panel);
                pump(0.1);
            }
        }
    }

    /* Save and reopen. This is the property anyone would actually notice:
     * a probe put on a patch is still there tomorrow.
     *
     * Against a copy, so the corpus is never written to -- the editor saves to
     * the file it was opened from, and dsp/ts1.dsp is not that file's to
     * change. */
    {
        const string copy = "/tmp/editorcheck-save.dsp";

        {
            ifstream in(file, std::ios::binary);
            ofstream out(copy.c_str(), std::ios::binary | std::ios::trunc);

            out << in.rdbuf();
        }

        TestEditor *ed2 = Gtk::manage(new TestEditor(&synth));
        Gtk::Window *w2 = new Gtk::Window();

        w2->set_child(*ed2);
        w2->set_default_size(1000, 600);
        w2->present();

        pump(0.2);

        ok(ed2->open(copy, -1), "a second editor opens the copy");
        ok(ed2->armProbe(node, arg, visual), "and arms a probe on it");
        ok(ed2->save(), "and saves");

        pump(0.2);

        TestEditor *ed3 = Gtk::manage(new TestEditor(&synth));
        Gtk::Window *w3 = new Gtk::Window();

        w3->set_child(*ed3);
        w3->set_default_size(1000, 600);
        w3->present();

        pump(0.2);

        ok(ed3->open(copy, -1), "a third editor opens the saved file");

        ok(ed3->probes_.size() == 1,
           "the probe came back from the file (%d)",
           (int)ed3->probes_.size());
        ok(ed3->panels() == 1, "and so did its panel (%d)", ed3->panels());

        if (ed3->probes_.size() == 1)
        {
            ok(ed3->probes_[0].node == node && ed3->probes_[0].arg == arg &&
               ed3->probes_[0].visual == visual,
               "pointing where it did: %s.%s/%s",
               ed3->probes_[0].node.c_str(), ed3->probes_[0].arg.c_str(),
               ed3->probes_[0].visual.c_str());

            /* Opened with no channel, so there is nothing to tap -- and the
               panel has to be there regardless, or a probe would silently
               vanish from any patch not currently playing. */
            ok(ed3->probes_[0].slot < 0,
               "and not tapping anything, since nothing is playing it");
        }

        w2->set_visible(false);
        w3->set_visible(false);
        delete w2;
        delete w3;

        ::remove(copy.c_str());
    }

    /* A graph written inside a piece: opened from its text, and saved back
       through the piece rather than to a file -- there is none, and the
       name it is loaded under is not a path anything may be written to. */
    {
        string body;

        {
            ifstream in(file, std::ios::binary);

            body.assign((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
        }

        string saved;
        TestEditor *ed4 = Gtk::manage(new TestEditor(&synth));
        Gtk::Window *w4 = new Gtk::Window();

        w4->set_child(*ed4);
        w4->set_default_size(1000, 600);
        w4->present();

        pump(0.2);

        ok(ed4->openInline("pad#0.dsp", body, -1,
                           [&saved](const string &text, string &name, string &)
                           {
                               saved = text;
                               name = "pad#1.dsp";
                               return true;
                           }),
           "an editor opens a graph from the text of a piece");
        ok(ed4->armProbe(node, arg, visual), "and arms a probe on it");

        ed4->onSave();

        ok(saved.find("@probe") != string::npos,
           "Save hands the piece the edited text");
        ok(ed4->sourceFile() == "pad#1.dsp",
           "and the editor takes the name the graph is loaded under now");
        ok(!std::filesystem::exists("pad#0.dsp") &&
           !std::filesystem::exists("pad#1.dsp"),
           "and writes no file under either name");

        /* A node marked as grown is drawn otherwise than when it is not. */
        NodeGraph shown = ed4->graph();
        NodeCanvas canvas;
        string plain;

        canvas.setGraph(&shown);
        canvas.setZoom(1.0);

        for (const NodeGraph::Box &b : shown.boxes())
            if (!b.isIoSource && !b.isIoSink && !b.isControl && !b.isExpr &&
                !b.isProbe && b.attachedTo < 0)
                plain = b.name;

        const auto picture = [&canvas] ()
        {
            Cairo::RefPtr<Cairo::ImageSurface> s =
                Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32,
                                            1000, 600);
            Cairo::RefPtr<Cairo::Context> cr = Cairo::Context::create(s);

            canvas.drawGraph(cr, 1000, 600);
            s->flush();

            return string((const char *)s->get_data(),
                          (size_t)s->get_stride() * 600);
        };
        const string unmarked = picture();

        canvas.setMarked({ plain });

        ok(!plain.empty() && picture() != unmarked,
           "a node marked as grown is drawn as one");

        w4->set_visible(false);
        delete w4;
    }

    /* And disarming really lets go of all three. */
    ed->disarmProbe(0);

    ok(ed->probes_.empty(), "disarming removes the probe");
    ok(ed->panels() == 0, "and its panel (%d)", ed->panels());

    /* And the window that was drawing it. The tick is what notices, so this
       has to let one run -- a window left open would keep calling into an
       instance that has been closed. */
    pump(0.3);

    ok(ed->enlarged_.empty(),
       "and the enlarged window that was showing it (%d left)",
       (int)ed->enlarged_.size());

    /* And nothing is left ticking. An editor with no probes and no windows
       should cost exactly what it did before probes existed. */
    ok(!ed->probeTick_.connected(),
       "with no probes and no windows, the frame tick has stopped");

    {
        int armed = 0;

        for (int s = 0; s < synth.probeCount(); s++)
            if (synth.probe(s))
                armed++;

        ok(armed == 0, "and gives the engine's slot back (%d still armed)",
           armed);
    }

    window->set_visible(false);

    /* Deliberately destroyed: the destructor's ordering -- stop the tick,
       close the instances, then unload the modules -- is a real thing to get
       wrong, and running it under ASan is the only way anyone would find out.
       Gtk::manage means the editor goes with the window. */
    delete window;

    checkSelectionAfterSave(synth);
    checkFailedOpen(synth, file);
    checkLayoutRestored(synth, file);
    checkNoScratchFile(synth, file);
    checkLayoutUndone(synth, file);
    checkControlCommit(synth);
    checkWireCommit(synth);
    checkValueCommit(synth);
    checkFailedReparse(synth, file);
    checkRandomEdits(synth, pluginPath);
    checkSaveCorpus(synth, pluginPath);
    checkNodePanel(synth, file);
    checkEffectPanel(synth, pluginPath);
    checkPanelViewRows();

    return failures;
}

} /* namespace */

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;
    const char *file = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p")) { if (++i >= argc) return 2; pluginPath = argv[i]; }
        else { file = argv[i]; break; }
    }

    if (file == NULL)
    {
        printf("usage: %s [-p PATH] file.dsp\n\nNeeds a display; run under "
               "xvfb-run on a headless box.\n", argv[0]);
        return 2;
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    /* Skip rather than fail where there is no display.
     *
     * This is a gate, so it is in the ctest list, and ctest runs in places
     * that have no X server -- a CI container, someone's build over ssh. The
     * alternative to skipping is either a red suite everywhere it cannot run,
     * or leaving it out of the suite entirely and having it rot. Saying so on
     * stdout is what keeps the skip from being silent.
     *
     * Checked before Gtk::Application::create rather than after, because GTK's
     * response to no display is to abort the process, which reads as a crash
     * rather than as a machine without a screen. */
    if (getenv("DISPLAY") == NULL && getenv("WAYLAND_DISPLAY") == NULL)
    {
        printf("skipped: no display (run under xvfb-run to exercise this)\n");
        return 0;
    }

    Glib::RefPtr<Gtk::Application> app =
        Gtk::Application::create("org.metaphonic.thinksynth.editorcheck",
                                 Gio::Application::Flags::NON_UNIQUE);

    int rc = 0;

    app->signal_activate().connect([&]() { rc = run(pluginPath, file); });

    app->run();

    printf("\n%d checks, %d failed\n", checks, failures);

    return rc;
}
