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
#include <fstream>
#include <functional>
#include <vector>

#include <gtkmm.h>

#include "think.h"
#include "NodeGraph.h"
#include "gui/NodeEditor.h"

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
    using NodeEditor::graph;
    using NodeEditor::reload;

    /* Save is a button handler with no return value; this is the same write
       path, reporting whether it worked. */
    bool save (void)
    {
        string why;

        return writeAll(why) && copyFile(workFile(), sourceFile());
    }

    const string &sourceFile (void) const { return filename(); }
    string workFile (void) const { return work(); }

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

    /* And disarming really lets go of all three. */
    ed->disarmProbe(0);

    ok(ed->probes_.empty(), "disarming removes the probe");
    ok(ed->panels() == 0, "and its panel (%d)", ed->panels());

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
