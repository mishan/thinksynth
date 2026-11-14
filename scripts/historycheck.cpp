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
 * historycheck -- src/NodeHistory, with no toolkit: the steps an editor's
 * undo goes back through, and what changed in the sound between two graphs.
 *
 *   historycheck [-p PLUGINS]
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "think.h"
#include "NodeGraph.h"
#include "NodeHistory.h"

using std::string;
using std::vector;

static int failed = 0;

static void check (bool good, const string &what)
{
    printf("%s  %s\n", good ? "ok  " : "FAIL", what.c_str());

    if (!good)
        failed++;
}

static NodeHistory::Snapshot state (const string &text, double x)
{
    NodeHistory::Snapshot s;

    s.text = text;
    s.positions["osc"] = std::make_pair(x, 20.0);

    return s;
}

static bool same (const NodeHistory::Snapshot &a,
                  const NodeHistory::Snapshot &b)
{
    return a.text == b.text && a.positions == b.positions;
}

static bool graphOf (thSynth &synth, const string &text, NodeGraph &out)
{
    thSynthTree *tree = synth.parseTreeText("historycheck", text);

    if (tree == NULL)
        return false;

    const bool built = out.build(tree);

    delete tree;

    return built;
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;

    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            pluginPath = argv[++i];

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    /* ---- back and forth ----------------------------------------------- */
    {
        NodeHistory h;
        NodeHistory::Snapshot out;

        check(!h.canUndo() && !h.canRedo() && !h.undo(state("c", 0), out),
              "a new history has nothing to undo or redo");

        h.record(state("a", 10), "first", "", 0);
        h.record(state("b", 20), "second", "", 0);

        check(h.canUndo() && h.undoName() == "second" && !h.canRedo(),
              "Undo offers the last step by its name");

        const bool back = h.undo(state("c", 30), out);

        check(back && same(out, state("b", 20)) && h.canRedo() &&
              h.redoName() == "second" && h.undoName() == "first",
              "Undo hands back the state before the last step, text and "
              "positions, and Redo offers that step");

        const bool again = h.redo(state("b", 20), out);

        check(again && same(out, state("c", 30)) && !h.canRedo(),
              "Redo hands back the state Undo left");

        h.undo(state("c", 30), out);
        h.record(state("b", 20), "third", "", 0);

        check(!h.canRedo() && h.undoName() == "third",
              "an edit after an Undo leaves nothing to redo");
    }

    /* ---- one step for one gesture -------------------------------------- */
    {
        NodeHistory h;
        NodeHistory::Snapshot out;

        h.record(state("a", 0), "drag osc.amp", "osc.amp", 0.0);
        h.record(state("b", 0), "drag osc.amp", "osc.amp", 0.7);
        h.record(state("c", 0), "drag osc.amp", "osc.amp", 1.4);

        h.undo(state("d", 0), out);

        check(same(out, state("a", 0)) && !h.canUndo(),
              "edits joined by one key, each within a second of the last, "
              "are one step back to where the first began");

        NodeHistory k;

        k.record(state("a", 0), "drag osc.amp", "osc.amp", 0.0);
        k.record(state("b", 0), "drag osc.mul", "osc.mul", 0.2);
        k.record(state("c", 0), "drag osc.mul", "osc.mul", 1.5);
        k.undo(state("d", 0), out);

        check(same(out, state("c", 0)),
              "another key, or the same one after a pause, is a step of "
              "its own");

        k.undo(state("c", 0), out);
        k.undo(state("b", 0), out);

        check(same(out, state("a", 0)) && !k.canUndo(),
              "...one each");

        NodeHistory u;

        u.record(state("a", 0), "drag osc.amp", "osc.amp", 0.0);
        u.undo(state("b", 0), out);
        u.redo(state("a", 0), out);
        u.record(state("b", 0), "drag osc.amp", "osc.amp", 0.1);
        u.undo(state("c", 0), out);

        check(same(out, state("b", 0)),
              "an Undo and a Redo end a gesture: what follows them does not "
              "join what came before");

        NodeHistory v;

        v.record(state("a", 0), "drag osc.amp", "osc.amp", 0.0);
        v.record(state("b", 0), "set osc.mul", "osc.mul", 0.3);
        v.undo(state("c", 0), out);
        v.record(state("b", 0), "drag osc.amp", "osc.amp", 0.6);
        v.undo(state("d", 0), out);

        check(same(out, state("b", 0)),
              "and an Undo alone ends one too: a drag after it is a step of "
              "its own");

        NodeHistory w;

        w.record(state("a", 0), "drag osc.amp", "osc.amp", 5.0);
        w.record(state("b", 0), "drag osc.amp", "osc.amp", 1.0);
        w.undo(state("c", 0), out);

        check(same(out, state("b", 0)),
              "a clock that goes backwards does not join");

        NodeHistory::Snapshot shown = state("c", 30);

        w.undo(shown, shown);
        w.redo(shown, shown);

        check(same(shown, state("c", 30)),
              "the state shown can be handed in and taken back in one");
    }

    /* ---- the cap --------------------------------------------------------- */
    {
        NodeHistory h(3);
        NodeHistory::Snapshot out;

        for (int i = 0; i < 5; i++)
            h.record(state(std::to_string(i), i), "step", "", 0);

        int steps = 0;

        while (h.undo(state("x", 0), out))
            steps++;

        check(steps == 3 && same(out, state("2", 2)),
              "past its cap the history forgets its oldest steps");
    }

    /* ---- what changed in the sound ------------------------------------- */
    {
        thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH,
                      TH_DEFAULT_SAMPLES);

        const string head =
            "name \"Live\";\n"
            "@lvl = %LVL%;\n@lvl.widget = 1;\n@lvl.min = 0;\n@lvl.max = 1;\n"
            "@cut = 0.5;\n@cut.widget = 1;\n@cut.min = 0;\n@cut.max = 1;\n"
            "@raw = %RAW%;\n"
            "node ionode { channels = 2; mono = %MONO%; vol = %VOL%; "
            "out[] = gain->out; play = env->play; };\n"
            "node gain mixer::mul { in0 = tone[0]->out; in1 = @raw; };\n"
            "node level math::abs { in = ionode->vol; };\n"
            "node tone[2] osc::simple { freq = ionode->note; amp = %AMP%; "
            "mul = @lvl; };\n"
            "node env env::adsr { trigger = ionode->trigger; s = @cut; "
            "a = 10; };\n"
            "io ionode;\n";

        const auto text = [&] (const string &lvl, const string &mono,
                               const string &amp, const string &vol,
                               const string &raw)
        {
            string t = head;

            t.replace(t.find("%LVL%"), 5, lvl);
            t.replace(t.find("%MONO%"), 6, mono);
            t.replace(t.find("%AMP%"), 5, amp);
            t.replace(t.find("%VOL%"), 5, vol);
            t.replace(t.find("%RAW%"), 5, raw);

            return t;
        };

        NodeGraph before, after, rawer;

        const bool built =
            graphOf(synth, text("0.25", "0", "1", "0.5", "1"), before) &&
            graphOf(synth, text("0.75", "1", "0.5", "0.8", "1"), after) &&
            graphOf(synth, text("0.75", "1", "0.5", "0.8", "2"), rawer);

        check(built, "graphs to compare build");

        bool complete = false;
        vector<NodeHistory::LiveChange> changes =
            NodeHistory::liveChanges(before, after, complete);

        const auto has = [&] (bool control, const string &node,
                              const string &arg, double value)
        {
            for (const NodeHistory::LiveChange &c : changes)
                if (c.control == control && c.node == node && c.arg == arg &&
                    (float)c.value == (float)value)
                    return true;

            return false;
        };

        check(has(true, "", "lvl", 0.75),
              "a control that moved is a change, by its name");
        check(has(false, "tone[0]", "amp", 0.5) &&
              has(false, "tone[1]", "amp", 0.5),
              "an arg of an array is a change to every element");
        check(has(false, "ionode", "mono", 1) &&
              has(false, "ionode", "vol", 0.8),
              "an arg of the io node is a change, by the node's name -- one "
              "other nodes read from too");
        check(changes.size() == 5 && complete,
              "and nothing that stayed the same is one: @cut, env.a, the "
              "freq read from a wire -- and the list is all of it");

        check(NodeHistory::liveChanges(after, after, complete).empty() &&
              complete, "a graph against itself has no changes");

        NodeHistory::liveChanges(after, rawer, complete);

        check(!complete, "a @name with no slider is a change the list cannot "
              "make, there being no control to tell, and it says so");

        NodeGraph fewer;

        graphOf(synth, text("0.75", "1", "0.5", "0.8", "1") +
                "node extra osc::simple { freq = 1; };\n", fewer);
        NodeHistory::liveChanges(after, fewer, complete);

        check(!complete, "nor can it add a node");
    }

    printf("\n%d failure(s)\n", failed);

    return failed ? 1 : 0;
}
