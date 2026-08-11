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
 * canvasbench -- what does redrawing the node canvas cost?
 *
 * Live visualizers mean redrawing on a timer, and GTK4's queue_draw()
 * invalidates the whole widget. So animating a 128x64 panel repaints the
 * entire graph, and VISUALIZERS.md §5 says plainly that this measurement comes
 * before the canvas design rather than after it: if a full repaint is cheap,
 * probe panels can cache into image surfaces and let the canvas redraw
 * normally; if it is not, each panel needs its own small DrawingArea in an
 * overlay, repositioned on every scroll and zoom.
 *
 * This drives the real NodeCanvas over real graphs. Not a stand-in that draws
 * "something of similar complexity" -- that answers a different question, and
 * describing the same scene twice is exactly how NODE_EDITOR.md §12's "clicking
 * a wire selects a different wire" happened.
 *
 * UNLIKE EVERY OTHER HARNESS HERE, THIS ONE NEEDS A DISPLAY. It builds a real
 * window, because the thing being measured is the widget's own draw path. It
 * is therefore a measuring instrument rather than a CI gate, and it is not in
 * the ctest list.
 *
 *   xvfb-run -a ./build/scripts/canvasbench -p build/plugins/ dsp/old/bd9.dsp
 *
 * What it reports, per file: the graph's size, and the mean and worst
 * wall-clock time NodeCanvas::drawGraph took. The number that matters is the
 * mean against a 33ms frame: that is the share of a 30fps budget one repaint
 * costs, before GTK has uploaded or composited anything.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <vector>

#include <gtkmm.h>

#include "think.h"
#include "NodeGraph.h"
#include "gui/NodeCanvas.h"

namespace {

struct Result {
    string file;
    int boxes, wires;
    double graphW, graphH;
    double meanMicros;
    double worstMicros;
    unsigned long frames;
};

/* The editor's own window is roughly this. The viewport matters: GTK clips to
   it, so a graph wider than the window does not cost its full width -- and
   pretending otherwise would flatter the result. */
const int VIEW_W = 1200;
const int VIEW_H = 700;

/* Long enough to average out scheduler noise, short enough to run over the
   corpus. */
const int FRAMES = 120;

bool benchOne (const string &pluginPath, const char *file, bool fit,
               Result &out)
{
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    thSynthTree *tree = synth.parseTree(file);

    if (tree == NULL)
        return false;

    NodeGraph graph;

    graph.build(tree);
    graph.layout();
    delete tree;

    /* On the heap and deliberately never freed: a Gtk::Window destroyed while
       its surface is still being torn down takes the process with it, and this
       is a measuring instrument that exits in a second. */
    Gtk::Window *windowp = new Gtk::Window();
    Gtk::ScrolledWindow *scroller = Gtk::manage(new Gtk::ScrolledWindow());
    NodeCanvas *canvasp = Gtk::manage(new NodeCanvas());

    Gtk::Window &window = *windowp;
    NodeCanvas &canvas = *canvasp;

    canvas.setGraph(&graph);

    scroller->set_child(canvas);
    window.set_child(*scroller);
    window.set_default_size(VIEW_W, VIEW_H);
    window.present();

    /* Pump the loop until `done' says so, or the deadline passes.
     *
     * Blocking iterations, which is the part that took a while to get right.
     * A non-blocking iteration returns immediately when nothing is pending,
     * and between queue_draw() and GTK's frame clock deciding to render there
     * is a stretch where nothing is pending -- so spinning non-blocking never
     * let a single frame happen and every graph measured as zero. */
    struct Pump {
        static bool until (const std::function<bool()> &done, double seconds)
        {
            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds((int)(seconds * 1000));

            while (!done())
            {
                if (std::chrono::steady_clock::now() > deadline)
                    return false;

                Glib::MainContext::get_default()->iteration(true);
            }

            return true;
        }
    };

    /* Let the window map, allocate and draw once before anything is timed, or
       the first frames measure GTK settling rather than the drawing. */
    if (!Pump::until([&]() { return canvas.drawCount() > 0; }, 10.0))
        return false;

    if (fit)
        canvas.zoomToFit();

    canvas.resetDrawStats();

    double worst = 0.0;

    /* Frame by frame rather than by wall clock. Under Xvfb there is no vsync
       and GTK's frame clock free-runs, so frames per second would measure the
       clock; the cost of one repaint is what is wanted. */
    for (int f = 0; f < FRAMES; f++)
    {
        const double before = canvas.drawMicros();
        const unsigned long lastCount = canvas.drawCount();

        canvas.queue_draw();

        if (!Pump::until([&]() { return canvas.drawCount() > lastCount; }, 2.0))
            break;   /* nothing is drawing; the numbers below would be a lie */

        const double took = canvas.drawMicros() - before;

        if (took > worst)
            worst = took;
    }

    out.file = file;
    out.boxes = (int)graph.boxes().size();
    out.wires = (int)graph.edges().size();
    out.graphW = graph.width();
    out.graphH = graph.height();
    out.frames = canvas.drawCount();
    out.meanMicros = out.frames ? canvas.drawMicros() / (double)out.frames : 0;
    out.worstMicros = worst;

    window.set_visible(false);

    return out.frames > 0;
}

bool byMean (const Result &a, const Result &b)
{
    return a.meanMicros > b.meanMicros;
}

} /* namespace */

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;
    bool fit = false;
    int firstFile = -1;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p")) { if (++i >= argc) return 2; pluginPath = argv[i]; }
        else if (!strcmp(argv[i], "-f")) fit = true;
        else { firstFile = i; break; }
    }

    if (firstFile < 0)
    {
        printf("usage: %s [-p PATH] [-f] file.dsp ...\n"
               "  -f  zoom to fit first, as the Fit button does\n"
               "\n"
               "Needs a display. Under CI or a headless box:\n"
               "  xvfb-run -a %s ...\n", argv[0], argv[0]);
        return 2;
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    /* Through activate() rather than by pumping a context by hand: a widget
       does not draw until it has been realized and mapped, and that only
       happens once the application is running and has a display. An earlier
       version created the Application and pumped the default context, which
       measured every graph as zero frames -- correctly, since nothing had
       drawn. */
    Glib::RefPtr<Gtk::Application> app =
        Gtk::Application::create("org.metaphonic.thinksynth.canvasbench",
                                 Gio::Application::Flags::NON_UNIQUE);

    std::vector<Result> results;

    app->signal_activate().connect([&]() {
        for (int f = firstFile; f < argc; f++)
        {
            Result r;

            if (!benchOne(pluginPath, argv[f], fit, r))
            {
                printf("skip  %s\n", argv[f]);
                continue;
            }

            results.push_back(r);

            printf("%-30s %4d boxes %5d wires  %5.0fx%-4.0f  mean %6.2f ms  "
                   "worst %6.2f ms\n", r.file.c_str(), r.boxes, r.wires,
                   r.graphW, r.graphH, r.meanMicros / 1000.0,
                   r.worstMicros / 1000.0);
            fflush(stdout);
        }
    });

    /* No arguments: they have already been parsed, and GApplication would
       reject the ones it does not know. */
    app->run();

    if (results.empty())
    {
        printf("nothing measured\n");
        return 1;
    }

    std::sort(results.begin(), results.end(), byMean);

    double total = 0.0;

    for (size_t i = 0; i < results.size(); i++)
        total += results[i].meanMicros;

    const double mean = total / (double)results.size();
    const double worst = results[0].meanMicros;

    printf("\n%d graphs, %d frames each, viewport %dx%d%s\n",
           (int)results.size(), FRAMES, VIEW_W, VIEW_H,
           fit ? ", zoomed to fit" : "");
    printf("  mean repaint  %6.2f ms  (%4.1f%% of a 33ms frame)\n",
           mean / 1000.0, mean / 33000.0 * 100.0);
    printf("  worst graph   %6.2f ms  (%4.1f%% of a 33ms frame)  %s\n",
           worst / 1000.0, worst / 33000.0 * 100.0, results[0].file.c_str());

    return 0;
}
