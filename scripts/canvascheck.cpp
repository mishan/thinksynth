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
 * canvascheck -- the node canvas's gestures, with no toolkit.
 *
 * NodeCanvas is the desktop's canvas and the browser's: the widget and the
 * page are shells that hand it presses, motions and keys in their own
 * pixels and act on the signals it raises. So what a click, a drag, a band,
 * a wire or a key does is decided here, and checked here, against a bare
 * NodeCanvas over a small patch -- no widget, no display, no browser. The
 * editors' own harnesses (editorcheck, nodecheck, pagetest) check what they
 * make of the signals.
 *
 * Every case starts from the same graph, each box put where the case can
 * aim at it, and says what the selection, the boxes and the signals are
 * after its gesture.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "think.h"

#include "NodeCanvas.h"
#include "NodeGraph.h"

using std::string;
using std::vector;

static int failures = 0;

static const char *PATCH =
    "@lvl = 0.5;\n@lvl.widget = 1;\n@lvl.min = 0;\n@lvl.max = 1;\n"
    "node ionode { channels = 1; out0 = mix->out; play = 1; };\n"
    "node lfo osc::simple { freq = 2; };\n"
    "node osc osc::simple { freq = ionode->note; amp = @lvl; };\n"
    "node spare osc::simple { freq = 3; };\n"
    "node mix mixer::mul { in0 = osc->out; in1 = lfo->out; };\n"
    "io ionode;\n";

/* Where each top-level box is put, so a case can aim at it: lfo and osc
   in one column, which a band can take together, and the rest apart. */
static const struct { const char *name; double x, y; } PLACES[] = {
    { "lfo", 100, 100 }, { "osc", 100, 400 }, { "spare", 700, 100 },
    { "mix", 400, 250 },
};

/* A shell with a view, so a pan and a selection kept in sight have
   somewhere to scroll. Its pixels are the drawing's, as a widget's inside
   a scroller are: a pointer held still on the screen comes in at another
   point of the drawing once the view has moved under it. */
class Shell : public NodeCanvas
{
public:
    double scrolledX = -1, scrolledY = -1;
    double viewX = 0, viewY = 0;

protected:
    bool shellViewport (double &x, double &y, double &w,
                        double &h) const override
    {
        x = viewX;
        y = viewY;
        w = 500;
        h = 400;
        return true;
    }

    void scrollShellTo (double x, double y) override
    {
        scrolledX = viewX = x;
        scrolledY = viewY = y;
    }
};

/* What the canvas raised during a case. */
struct Heard
{
    vector<int> moved, nudged, wires, cut;
    int deletes = 0, bands = 0;
};

struct Case
{
    const char *what;
    std::function<void(Shell &, NodeGraph &)> act;
    std::function<bool(const Shell &, const NodeGraph &, const NodeGraph &,
                       const Heard &, string &)> holds;
};

static int boxNamed (const NodeGraph &g, const string &name)
{
    for (size_t b = 0; b < g.boxes().size(); b++)
        if (g.boxes()[b].name == name && g.boxes()[b].attachedTo < 0 &&
            !g.boxes()[b].isIoSource)
            return (int)b;

    return -1;
}

/* A point on a box's title, clear of its ports. */
static double atX (const NodeGraph &g, const string &n)
{
    return g.boxes()[boxNamed(g, n)].x + 8;
}

static double atY (const NodeGraph &g, const string &n)
{
    return g.boxes()[boxNamed(g, n)].y + 8;
}

static void click (Shell &c, double x, double y, unsigned mods = 0)
{
    c.onPressed(1, x, y, mods);
    c.onReleased(1, x, y);
}

/* A pointer moved `dx', `dy' on the screen from `x', `y' in the drawing:
   each event at the drawing point under it, for the view as it is then. */
static void drag (Shell &c, double x, double y, double dx, double dy,
                  unsigned mods = 0)
{
    const double sx = x - c.viewX, sy = y - c.viewY;

    c.onPressed(1, x, y, mods);
    c.onMotion(sx + dx / 2 + c.viewX, sy + dy / 2 + c.viewY);
    c.onMotion(sx + dx + c.viewX, sy + dy + c.viewY);
    c.onReleased(1, sx + dx + c.viewX, sy + dy + c.viewY);
}

/* The wire into `node'.`arg' and the middle of its curve. */
static int wireInto (const NodeGraph &g, const string &node, const string &arg,
                     double &x, double &y)
{
    for (size_t e = 0; e < g.edges().size(); e++)
    {
        const NodeGraph::Edge &ed = g.edges()[e];
        const NodeGraph::Box &to = g.boxes()[ed.toBox];

        if (to.name != node || to.ports[ed.toPort].name != arg)
            continue;

        double xs[4], ys[4];

        g.edgeCurve((int)e, xs, ys);
        x = (xs[0] + 3 * xs[1] + 3 * xs[2] + xs[3]) / 8;
        y = (ys[0] + 3 * ys[1] + 3 * ys[2] + ys[3]) / 8;

        return (int)e;
    }

    return -1;
}

static std::set<string> heldNames (const Shell &c, const NodeGraph &g)
{
    std::set<string> out;

    for (int b : c.selection())
        out.insert(g.boxes()[b].name);

    return out;
}

static string names (const std::set<string> &s)
{
    string out;

    for (const string &n : s)
        out += (out.empty() ? "" : " ") + n;

    return "{" + out + "}";
}

static bool same (const NodeGraph &a, const NodeGraph &b, const string &n,
                  double dx = 0, double dy = 0)
{
    const NodeGraph::Box &x = a.boxes()[boxNamed(a, n)];
    const NodeGraph::Box &y = b.boxes()[boxNamed(b, n)];

    return y.x == x.x + dx && y.y == x.y + dy;
}

int main (int argc, char **argv)
{
    string plugins = PLUGIN_PATH;

    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "-p"))
            plugins = argv[++i];

    if (plugins.empty() || plugins[plugins.size() - 1] != '/')
        plugins += '/';

    thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    thSynthTree *tree = synth.parseTreeText("canvascheck", PATCH);
    NodeGraph base;

    if (tree == NULL || !base.build(tree))
    {
        printf("FAIL  the patch builds a graph\n");
        delete tree;
        return 1;
    }

    delete tree;
    base.layout();

    for (const auto &p : PLACES)
        base.moveBox(boxNamed(base, p.name), p.x, p.y);

    for (size_t b = 0; b < base.boxes().size(); b++)
        if (base.boxes()[b].isIoSource)
            base.moveBox((int)b, 400, 700);
        else if (base.boxes()[b].isIoSink)
            base.moveBox((int)b, 1000, 250);

    base.refreshExtent();

    using M = CanvasContent;

    const Case cases[] = {
        { "a click on a box selects it and moves nothing",
          [] (Shell &c, NodeGraph &g) { click(c, atX(g, "lfo"), atY(g, "lfo")); },
          [] (const Shell &c, const NodeGraph &a, const NodeGraph &g,
              const Heard &h, string &why)
          {
              why = names(heldNames(c, g));
              return heldNames(c, g) == std::set<string>{ "lfo" } &&
                     h.moved.empty() && same(a, g, "lfo");
          } },
        { "a box dragged moves by the drag, and says so once",
          [] (Shell &c, NodeGraph &g)
          { drag(c, atX(g, "lfo"), atY(g, "lfo"), 40, 20); },
          [] (const Shell &, const NodeGraph &a, const NodeGraph &g,
              const Heard &h, string &why)
          {
              why = std::to_string(h.moved.size()) + " moves";
              return h.moved.size() == 1 && same(a, g, "lfo", 40, 20);
          } },
        { "a click on nothing lets go of the selection",
          [] (Shell &c, NodeGraph &g)
          {
              click(c, atX(g, "lfo"), atY(g, "lfo"));
              click(c, 600, 600);
          },
          [] (const Shell &c, const NodeGraph &, const NodeGraph &g,
              const Heard &, string &why)
          {
              why = names(heldNames(c, g));
              return c.selection().empty() && c.selected() < 0;
          } },
        { "a band gathers what it covers, and says how many",
          [] (Shell &c, NodeGraph &) { drag(c, 50, 50, 300, 500); },
          [] (const Shell &c, const NodeGraph &, const NodeGraph &g,
              const Heard &h, string &why)
          {
              why = names(heldNames(c, g)) + " banded " +
                    std::to_string(h.bands);
              return heldNames(c, g) == std::set<string>{ "lfo", "osc" } &&
                     h.bands == 2;
          } },
        { "Shift and a click adds a box, and takes it out again",
          [] (Shell &c, NodeGraph &g)
          {
              click(c, atX(g, "lfo"), atY(g, "lfo"));
              click(c, atX(g, "spare"), atY(g, "spare"), M::MOD_SHIFT);
              click(c, atX(g, "mix"), atY(g, "mix"), M::MOD_SHIFT);
              click(c, atX(g, "lfo"), atY(g, "lfo"), M::MOD_SHIFT);
          },
          [] (const Shell &c, const NodeGraph &, const NodeGraph &g,
              const Heard &, string &why)
          {
              why = names(heldNames(c, g));
              return heldNames(c, g) == std::set<string>{ "spare", "mix" };
          } },
        { "dragging one box of a selection moves the whole of it",
          [] (Shell &c, NodeGraph &g)
          {
              c.setSelection({ boxNamed(g, "lfo"), boxNamed(g, "spare") },
                             boxNamed(g, "lfo"));
              drag(c, atX(g, "lfo"), atY(g, "lfo"), 30, 10);
          },
          [] (const Shell &, const NodeGraph &a, const NodeGraph &g,
              const Heard &, string &why)
          {
              why = "";
              return same(a, g, "lfo", 30, 10) &&
                     same(a, g, "spare", 30, 10) && same(a, g, "mix");
          } },
        { "a click on a wire selects it and cuts nothing",
          [] (Shell &c, NodeGraph &g)
          {
              double x, y;

              wireInto(g, "mix", "in1", x, y);
              click(c, x, y);
          },
          [] (const Shell &c, const NodeGraph &, const NodeGraph &g,
              const Heard &h, string &why)
          {
              double x, y;
              const int e = wireInto(g, "mix", "in1", x, y);

              why = std::to_string(h.wires.size()) + " selected, " +
                    std::to_string(h.cut.size()) + " cut";
              return c.selectedWire() == e && h.wires.size() == 1 &&
                     h.wires[0] == e && h.cut.empty() && c.selected() < 0;
          } },
        { "Delete on a wire asks for it cut",
          [] (Shell &c, NodeGraph &g)
          {
              double x, y;

              wireInto(g, "mix", "in1", x, y);
              click(c, x, y);
              c.keyPressed(M::KEY_DELETE);
          },
          [] (const Shell &, const NodeGraph &, const NodeGraph &g,
              const Heard &h, string &why)
          {
              double x, y;
              const int e = wireInto(g, "mix", "in1", x, y);

              why = std::to_string(h.cut.size()) + " cut";
              return h.cut.size() == 1 && h.cut[0] == e && h.deletes == 0;
          } },
        { "Delete on a box asks for the selection deleted, and on nothing "
          "is the shell's",
          [] (Shell &c, NodeGraph &g)
          {
              click(c, atX(g, "lfo"), atY(g, "lfo"));
              c.keyPressed(M::KEY_DELETE);
              click(c, 600, 600);
              c.keyPressed(M::KEY_DELETE);
          },
          [] (const Shell &, const NodeGraph &, const NodeGraph &,
              const Heard &h, string &why)
          {
              why = std::to_string(h.deletes) + " deletes";
              return h.deletes == 1;
          } },
        { "Escape lets go, and with nothing held is the shell's",
          [] (Shell &c, NodeGraph &g)
          {
              click(c, atX(g, "lfo"), atY(g, "lfo"));

              if (!c.keyPressed(M::KEY_ESCAPE) ||
                  c.keyPressed(M::KEY_ESCAPE))
                  c.setSelected(boxNamed(g, "spare"));
          },
          [] (const Shell &c, const NodeGraph &, const NodeGraph &g,
              const Heard &, string &why)
          {
              why = names(heldNames(c, g));
              return c.selection().empty() && c.selected() < 0;
          } },
        { "Ctrl+A selects every box but the strips on them",
          [] (Shell &c, NodeGraph &) { c.keyPressed(M::KEY_SELECT_ALL); },
          [] (const Shell &c, const NodeGraph &, const NodeGraph &g,
              const Heard &, string &why)
          {
              size_t free = 0;

              for (const NodeGraph::Box &b : g.boxes())
                  free += b.attachedTo < 0;

              why = std::to_string(c.selection().size()) + " of " +
                    std::to_string(free);
              return c.selection().size() == free;
          } },
        { "an arrow goes to the next box that way",
          [] (Shell &c, NodeGraph &g)
          {
              click(c, atX(g, "lfo"), atY(g, "lfo"));
              c.keyPressed(M::KEY_RIGHT);
          },
          [] (const Shell &c, const NodeGraph &, const NodeGraph &g,
              const Heard &h, string &why)
          {
              why = c.selected() < 0 ? string("none")
                                     : g.boxes()[c.selected()].name;
              return c.selected() >= 0 &&
                     g.boxes()[c.selected()].x > g.boxes()[boxNamed(g, "lfo")].x &&
                     h.moved.empty();
          } },
        { "Shift and an arrow nudges 10 pixels, with Ctrl 1",
          [] (Shell &c, NodeGraph &g)
          {
              click(c, atX(g, "lfo"), atY(g, "lfo"));
              c.keyPressed(M::KEY_RIGHT, M::MOD_SHIFT);
              c.keyPressed(M::KEY_DOWN, M::MOD_SHIFT | M::MOD_CTRL);
          },
          [] (const Shell &, const NodeGraph &a, const NodeGraph &g,
              const Heard &h, string &why)
          {
              why = std::to_string(h.nudged.size()) + " nudges";
              return same(a, g, "lfo", 10, 1) && h.nudged.size() == 2 &&
                     h.moved.empty();
          } },
        { "Ctrl and an arrow is left to the scroller",
          [] (Shell &c, NodeGraph &g)
          {
              click(c, atX(g, "lfo"), atY(g, "lfo"));

              if (c.keyPressed(M::KEY_RIGHT, M::MOD_CTRL))
                  c.setSelected(-1);
          },
          [] (const Shell &c, const NodeGraph &, const NodeGraph &g,
              const Heard &, string &why)
          {
              why = names(heldNames(c, g));
              return heldNames(c, g) == std::set<string>{ "lfo" };
          } },
        { "a drag with the pan held moves the view and nothing in it",
          [] (Shell &c, NodeGraph &g)
          { drag(c, atX(g, "lfo"), atY(g, "lfo"), -60, -30, M::MOD_PAN); },
          [] (const Shell &c, const NodeGraph &a, const NodeGraph &g,
              const Heard &h, string &why)
          {
              why = std::to_string(c.scrolledX) + "," +
                    std::to_string(c.scrolledY);
              return c.scrolledX == 60 && c.scrolledY == 30 &&
                     same(a, g, "lfo") && h.moved.empty() &&
                     c.selection().empty();
          } },
    };

    for (const Case &k : cases)
    {
        NodeGraph g = base;
        Shell c;
        Heard h;
        string why;

        c.setGraph(&g);
        c.signal_box_moved().connect([&h] (int b) { h.moved.push_back(b); });
        c.signal_nudged().connect([&h] (int b) { h.nudged.push_back(b); });
        c.signal_wire_selected().connect(
            [&h] (int e) { h.wires.push_back(e); });
        c.signal_disconnect_requested().connect(
            [&h] (int e) { h.cut.push_back(e); });
        c.signal_delete_requested().connect([&h] () { h.deletes++; });
        c.signal_selection().connect([&h] (int n) { h.bands = n; });

        k.act(c, g);

        if (k.holds(c, base, g, h, why))
            printf("ok    %s\n", k.what);
        else
        {
            printf("FAIL  %s: %s\n", k.what, why.c_str());
            failures++;
        }
    }

    printf("\n%d failed\n", failures);

    return failures == 0 ? 0 : 1;
}
