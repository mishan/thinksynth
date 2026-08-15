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
 * argtype -- what a parameter *is*, as opposed to what it currently holds.
 *
 * A plugin can say that one of its args means a whole number, and can name that
 * number's values: osc::simple reads `waveform' as `switch ((int)x)' and its six
 * cases are Sine through Parabola. Nothing in the audio path reads any of that;
 * it exists so a control driving such a parameter can be a list of six names
 * rather than a slider whose travel is five sixths decoration.
 *
 * Getting there means crossing three seams, and this checks all three:
 *
 *   thPlugin  the plugin declares it about its own arg
 *   thSynthTree::typeChanArgs  carries it along the wire to the control
 *   NodeGraph  which draws it, and must not let a drag land between values
 *
 * Written as its own harness rather than folded into dspcheck because the
 * cases that matter are not in the corpus and cannot be. Every shipped .dsp
 * that drives a waveform does so from exactly one control, so the disagreement
 * rule -- the one thing here that is a judgement call rather than a lookup --
 * has nothing to fire on. Those files are built here instead.
 *
 * Needs no display and no corpus.
 *
 *     scripts/argtype -p build/plugins/
 *
 * Exit status is the number of failures.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <filesystem>
#include <fstream>

#include "think.h"
#include "NodeGraph.h"

static int failed = 0;

static void ok (const char *what)
{
    printf("ok    %s\n", what);
}

static void fail (const char *what, const string &detail)
{
    printf("FAIL  %s%s%s\n", what, detail.empty() ? "" : ": ", detail.c_str());
    failed++;
}

/* Not "/tmp/...".
 *
 * The other file-writing harnesses hardcode it and get away with it because
 * none of them is a CTest gate -- they take a corpus argument and are run by
 * hand, on the machine of somebody who has a /tmp. This one is a gate, so it
 * runs on the Windows runner, where there is no such directory: every write
 * silently did nothing and every parse then failed to find a file, which is
 * what twelve failures with no other symptom look like.
 *
 * A function rather than a static, because temp_directory_path() consults the
 * environment and doing that before main() is a habit worth not forming. */
static string scratchPath (const char *leaf)
{
    std::error_code ec;

    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);

    if (ec)
        dir = ".";      /* the build tree; ctest runs us in it */

    return (dir / leaf).string();
}

static string scratch;

/* Returns false rather than failing silently. A write that does nothing and a
   parse that then finds no file report the parse, which is two steps from the
   cause. */
static bool writeOrFail (const string &text)
{
    ofstream out(scratch.c_str(), ios::binary | ios::trunc);

    out << text;
    out.close();

    if (out.good())
        return true;

    fail("could not write the scratch file", scratch);

    return false;
}

/* The smallest file that loads, plus whatever the caller wants in it. */
static string wrap (const string &controls, const string &nodes)
{
    return string("name \"argtype\";\n\n") + controls +
           "\nnode ionode {\n    channels = 2;\n    play = 1;\n};\n\n" +
           nodes + "\nio ionode;\n";
}

/* What the parse decided about one control, copied out so the tree can go.
 *
 * This used to hand back the thArg itself and leak the tree it points into, on
 * the reasoning that the process was about to exit anyway. That is not true
 * under a gate: the asan job runs ctest with detect_leaks=1, every other
 * harness in this tree is clean under it, and "about to exit" is exactly the
 * excuse LeakSanitizer exists to refuse. Three fields is all any caller wanted
 * of the arg. */
struct Typing {
    bool found;
    float step;
    vector<string> names;

    Typing (void) : found(false), step(0) {}
};

static Typing typingOf (thSynth &synth, const string &name)
{
    Typing t;

    thSynthTree *tree = synth.parseTree(scratch);

    if (tree == NULL)
        return t;

    thArg *a = tree->getChanArg(name);

    if (a)
    {
        t.found = true;
        t.step = a->step();
        t.names = a->valueNames();
    }

    delete tree;

    return t;
}

static string joined (const vector<string> &v)
{
    string s;

    for (size_t i = 0; i < v.size(); i++)
        s += (i ? "|" : "") + v[i];

    return s;
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;

    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            pluginPath = argv[++i];

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    scratch = scratchPath("argtype-scratch.dsp");

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    /* ---- the plugin declares it ---------------------------------------- */

    /* Through a node rather than through thPluginManager, whose map is keyed by
       a path this harness would have to reconstruct. A .dsp naming the plugin
       is how everything else in the tree gets at one. */
    writeOrFail(wrap("", "node osc osc::simple {\n    freq = ionode->note;\n};\n"));

    {
        thSynthTree *tree = synth.parseTree(scratch);

        thNode *n = tree ? tree->findNode("osc") : NULL;

        thPlugin *p = n ? n->plugin() : NULL;

        if (p == NULL)
            fail("osc::simple loads", "");
        else
        {
            int idx = -1;

            for (int k = 0; k < p->argCount(); k++)
                if (p->getArgName(k) == "waveform")
                    idx = k;

            if (idx < 0)
                fail("osc::simple registers waveform", "");
            else if (p->getArgValues(idx).size() != 6)
                fail("osc::simple names six waveforms",
                     joined(p->getArgValues(idx)));
            else if (p->getArgStep(idx) != 1)
                fail("naming the values implies a step of 1", "");
            else if (p->getArgValues(idx)[3] != "Triangle")
                fail("waveform 3 is the triangle", p->getArgValues(idx)[3]);
            else
                ok("a plugin names its selector's values, and that implies a "
                   "step");

            /* An arg nobody said anything about stays continuous, which is
               what every other arg of every other plugin is. */
            int freq = -1;

            for (int k = 0; k < p->argCount(); k++)
                if (p->getArgName(k) == "freq")
                    freq = k;

            if (freq < 0 || p->getArgStep(freq) != 0 ||
                !p->getArgValues(freq).empty())
                fail("an undeclared arg stays continuous", "");
            else
                ok("an arg the plugin says nothing about stays continuous");
        }

        /* The plugin outlives this -- thPluginManager owns it and the synth
           owns that -- but the tree does not. */
        delete tree;
    }

    /* ---- and it reaches the control wired to it ------------------------- */

    writeOrFail(wrap("@wave = 3;\n@wave.widget = 1;\n@wave.min = 0;\n"
               "@wave.max = 5;\n",
               "node osc osc::simple {\n    waveform = @wave;\n};\n"));

    {
        const Typing a = typingOf(synth, "wave");

        if (!a.found)
            fail("@wave survives a parse", "");
        else if (a.step != 1)
            fail("@wave picks up the step from osc::simple", "");
        else if (a.names.size() != 6)
            fail("@wave picks up the names", joined(a.names));
        else
            ok("a control driving a named selector is named too");
    }

    /* ---- one consumer that disagrees is enough to stop it --------------- */

    /* The case the corpus cannot supply. `math::mul' has no opinion about in1,
     * so @wave now drives one parameter that wants six named values and one
     * that wants any number at all. Typing it would take 0.35 away from the
     * multiplier.
     *
     * Both orderings, and that is not belt and braces. thSynthTree::nodes_ is
     * keyed by name, so the pass visits nodes alphabetically -- and the first
     * version of this check named them `osc' and `gain', which put the
     * *untyped* one first. A pass with no disagreement rule at all, that simply
     * kept whatever it saw first, passed it. Naming them either way round is
     * what makes the assertion about the rule rather than about the alphabet.
     */
    for (int order = 0; order < 2; order++)
    {
        const char *oscName = order ? "zosc" : "aosc";
        const char *mulName = order ? "amul" : "zmul";

        char nodes[512];

        snprintf(nodes, sizeof(nodes),
                 "node %s osc::simple {\n    waveform = @wave;\n};\n"
                 "node %s math::mul {\n    in0 = %s->out;\n"
                 "    in1 = @wave;\n};\n",
                 oscName, mulName, oscName);

        writeOrFail(wrap("@wave = 3;\n@wave.widget = 1;\n@wave.min = 0;\n"
                   "@wave.max = 5;\n", nodes));

        const Typing a = typingOf(synth, "wave");

        if (!a.found)
            fail("@wave survives a parse with two consumers", "");
        else if (a.step != 0 || !a.names.empty())
            fail(order ? "the selector is visited second"
                       : "the selector is visited first",
                 "two consumers that disagree left the control typed as " +
                 joined(a.names));
        else if (order)
            ok("a control two things disagree about is left alone, whichever "
               "the pass sees first");
    }

    /* Two consumers that agree are not a disagreement. */
    writeOrFail(wrap("@wave = 3;\n@wave.widget = 1;\n@wave.min = 0;\n"
               "@wave.max = 5;\n",
               "node osc1 osc::simple {\n    waveform = @wave;\n};\n"
               "node osc2 osc::simple {\n    waveform = @wave;\n};\n"));

    {
        const Typing a = typingOf(synth, "wave");

        if (!a.found || a.names.size() != 6)
            fail("two consumers that agree still type the control",
                 joined(a.names));
        else
            ok("two consumers that agree are not a disagreement");
    }

    /* ---- the .dsp can say it itself, and wins --------------------------- */

    writeOrFail(wrap("@steps = 2;\n@steps.widget = 1;\n@steps.min = 0;\n"
               "@steps.max = 8;\n@steps.step = 1;\n",
               "node gain math::mul {\n    in0 = ionode->note;\n"
               "    in1 = @steps;\n};\n"));

    {
        const Typing a = typingOf(synth, "steps");

        if (!a.found || a.step != 1)
            fail("@x.step types a control the plugin says nothing about", "");
        else
            ok("a .dsp can type a control the plugin knows nothing about");
    }

    writeOrFail(wrap("@mode = 1;\n@mode.widget = 1;\n@mode.min = 0;\n"
               "@mode.max = 2;\n@mode.values = \"Off, Low, High\";\n",
               "node gain math::mul {\n    in0 = ionode->note;\n"
               "    in1 = @mode;\n};\n"));

    {
        const Typing a = typingOf(synth, "mode");

        if (!a.found || a.names.size() != 3)
            fail("@x.values names them", joined(a.names));
        else if (a.names[2] != "High")
            fail("the space after a comma is not part of the name",
                 a.names[2]);
        else if (a.step != 1)
            fail("naming the values implies a step", "");
        else
            ok("@x.values names a control's values, spaces trimmed");
    }

    /* A hole. osc::window implements waveforms 0, 2 and 3 and not 1, and a
       value with no name has to be sayable or a list cannot describe it. */
    writeOrFail(wrap("@mode = 0;\n@mode.widget = 1;\n@mode.min = 0;\n"
               "@mode.max = 2;\n@mode.values = \"Off,,High\";\n",
               "node gain math::mul {\n    in0 = ionode->note;\n"
               "    in1 = @mode;\n};\n"));

    {
        const Typing a = typingOf(synth, "mode");

        if (!a.found || a.names.size() != 3)
            fail("a hole is a value, not the end of the list",
                 joined(a.names));
        else if (!a.names[1].empty() || a.names[2] != "High")
            fail("the hole is in the middle", joined(a.names));
        else
            ok("a value with no name is a gap in the list, not its end");
    }

    /* The override: the file says continuous, the plugin says otherwise, and
       the file wins. Without typedByFile this cannot be expressed at all --
       a step of 0 is the default, so saying it and saying nothing would be the
       same state. */
    writeOrFail(wrap("@wave = 3;\n@wave.widget = 1;\n@wave.min = 0;\n"
               "@wave.max = 5;\n@wave.step = 0;\n",
               "node osc osc::simple {\n    waveform = @wave;\n};\n"));

    {
        const Typing a = typingOf(synth, "wave");

        if (!a.found)
            fail("@wave survives a parse with an explicit step", "");
        else if (a.step != 0 || !a.names.empty())
            fail("`@wave.step = 0' is overridden by the plugin",
                 joined(a.names));
        else
            ok("the file has the last word, including when it says "
               "`continuous'");
    }

    /* ---- and the graph draws it ---------------------------------------- */

    /* The padded maximum, which is the thing that started all this: eight
       shipped patches declare 5.1 or 5.5 for six waveforms so that a
       continuous slider could still truncate to the last one. */
    writeOrFail(wrap("@wave = 3;\n@wave.widget = 1;\n@wave.min = 0;\n"
               "@wave.max = 5.1;\n",
               "node osc osc::simple {\n    waveform = @wave;\n};\n"));

    {
        thSynthTree *tree = synth.parseTree(scratch);

        if (tree == NULL)
            fail("the padded file parses", "");
        else
        {
            NodeGraph g;

            g.build(tree);
            g.layout();

            int box = -1;

            for (size_t b = 0; b < g.boxes().size(); b++)
                if (g.boxes()[b].isControl && g.boxes()[b].ctlArg == "wave")
                    box = (int)b;

            if (box < 0)
                fail("@wave is a control box", "");
            else
            {
                const NodeGraph::Box &bx = g.boxes()[box];

                /* The declared numbers stay on the box, because NodeEdit writes
                   them back into the file -- a derived range here would mean
                   opening a patch and saving it silently rewrote a line nobody
                   touched. dspwrite fails on exactly that, and did. */
                if (fabs(bx.ctlMax - 5.1f) > 1e-4)
                    fail("the declared maximum survives the graph", "");
                else if (fabs(bx.ctlDrawMax() - 5.0f) > 1e-4)
                    fail("the drawn maximum comes from the names", "");
                else
                    ok("a padded maximum is drawn over the names and written "
                       "back as it was");

                /* A drag cannot land between two waveforms. */
                double x0, x1, y, hx;

                if (!g.sliderGeometry(box, x0, x1, y, hx))
                    fail("@wave has a track", "");
                else
                {
                    bool between = false;

                    for (int s = 0; s <= 40; s++)
                    {
                        const double x = x0 + (x1 - x0) * (s / 40.0);

                        g.setControlValue(box, g.sliderValueAt(box, x));

                        const float v = g.boxes()[box].ctlValue;

                        if (fabs(v - floorf(v + 0.5f)) > 1e-4)
                            between = true;
                    }

                    if (between)
                        fail("a drag can stop between two waveforms", "");
                    else
                        ok("a drag over a stepped control lands on a value");
                }

                /* And it reads its name rather than its number. */
                g.setControlValue(box, 3);

                if (g.boxes()[box].ctlValueName() != "Triangle")
                    fail("the strip names the value it is on",
                         g.boxes()[box].ctlValueName());
                else
                    ok("a stepped control shows the name of its value");
            }

            delete tree;
        }
    }

    /* ---- a list with holes in it ---------------------------------------- */

    /* osc::window declares six waveform indices and implements 0, 2 and 3. The
     * three gaps are not decoration: its switch has no case for 1, 4 or 5 and
     * no default:, so those values leave the output buffer holding whatever was
     * in it. Nothing may offer them.
     *
     * All four of these came out of review, and all four are cases the corpus
     * cannot produce -- no shipped .dsp drives an osc::window waveform at all.
     */
    writeOrFail(wrap("@w = 0;\n@w.widget = 1;\n@w.min = 0;\n@w.max = 5;\n",
               "node osc osc::window {\n    waveform = @w;\n};\n"));

    {
        thSynthTree *tree = synth.parseTree(scratch);

        if (tree == NULL)
            fail("an osc::window patch parses", "");
        else
        {
            NodeGraph g;

            g.build(tree);
            g.layout();

            int box = -1;

            for (size_t b = 0; b < g.boxes().size(); b++)
                if (g.boxes()[b].isControl && g.boxes()[b].ctlArg == "w")
                    box = (int)b;

            if (box < 0)
                fail("@w is a control box", "");
            else
            {
                const NodeGraph::Box &bx = g.boxes()[box];

                /* Six indices declared, because that is what the switch spans
                   and the length of the list is a statement about the arg. */
                if (bx.ctlValueNames.size() != 6)
                    fail("osc::window declares all six indices",
                         joined(bx.ctlValueNames));

                /* ...but the track ends at the last one that means something,
                   or its final two fifths would select nothing. */
                else if (bx.ctlDrawMax() != 3 || bx.ctlDrawMin() != 0)
                    fail("the track spans the named values, not the list", "");
                else
                    ok("a list with holes declares its whole range and is "
                       "drawn over the part that means something");

                /* Nothing anywhere on the track may land on a hole. Every
                   pixel, not a sample of them: a gap at one end is exactly
                   what a coarse sweep steps over. */
                double x0, x1, y, hx;

                if (!g.sliderGeometry(box, x0, x1, y, hx))
                    fail("@w has a track", "");
                else
                {
                    int bad = 0;

                    for (int s = 0; s <= 200; s++)
                    {
                        const double x = x0 + (x1 - x0) * (s / 200.0);

                        g.setControlValue(box, g.sliderValueAt(box, x));

                        if (g.boxes()[box].ctlValueName().empty())
                            bad++;
                    }

                    /* And from outside the track too -- a value arriving from
                       the parameter panel or from a reload goes through the
                       same call and must be held to the same rule. */
                    static const float outside[] = { -3, 0.6f, 1, 1.4f, 4, 5,
                                                     9 };

                    for (size_t k = 0; k < sizeof(outside)/sizeof(outside[0]);
                         k++)
                    {
                        g.setControlValue(box, outside[k]);

                        if (g.boxes()[box].ctlValueName().empty())
                            bad++;
                    }

                    if (bad)
                        fail("a control can be left on a value its plugin does "
                             "not implement", "");
                    else
                        ok("no drag and no written value can select a hole");
                }
            }

            delete tree;
        }
    }

    /* ---- a step is measured from zero ----------------------------------- */

    /* Because that is what a step means here: the plugin reads the arg
     * `(int)x', so the values it can tell apart are the whole numbers, and
     * whole numbers are counted from zero rather than from wherever a
     * particular patch decided its range should begin.
     *
     * The range below starts at 0.3 on purpose -- it is also the case where
     * rounding and clamping as two passes puts the value back off the grid,
     * since 0.3 is not itself a multiple. */
    writeOrFail(wrap("@n = 3;\n@n.widget = 1;\n@n.min = 0.3;\n@n.max = 8;\n"
               "@n.step = 1;\n",
               "node gain math::mul {\n    in0 = ionode->note;\n"
               "    in1 = @n;\n};\n"));

    {
        thSynthTree *tree = synth.parseTree(scratch);

        if (tree == NULL)
            fail("the stepped patch parses", "");
        else
        {
            NodeGraph g;

            g.build(tree);
            g.layout();

            int box = -1;

            for (size_t b = 0; b < g.boxes().size(); b++)
                if (g.boxes()[b].isControl && g.boxes()[b].ctlArg == "n")
                    box = (int)b;

            if (box < 0)
                fail("@n is a control box", "");
            else
            {
                int bad = 0;
                float low = 0;

                static const float tries[] = { -1, 0, 0.4f, 0.6f, 2.5f, 7.7f,
                                               20 };

                for (size_t k = 0; k < sizeof(tries)/sizeof(tries[0]); k++)
                {
                    g.setControlValue(box, tries[k]);

                    const float v = g.boxes()[box].ctlValue;

                    if (fabs(v - floorf(v + 0.5f)) > 1e-4)
                        bad++;          /* not a whole number */

                    if (v < 0.3f - 1e-4 || v > 8 + 1e-4)
                        bad++;          /* outside its own range */
                }

                /* The bottom of a range that is not itself a multiple has to
                   push inwards to the next one, not sit on the bound. */
                g.setControlValue(box, -5);
                low = g.boxes()[box].ctlValue;

                if (bad)
                    fail("a step is a whole number inside the range", "");
                else if (low != 1)
                    fail("a bound below the first multiple pushes inwards",
                         "");
                else
                    ok("a step counts from zero, and a bound that is not one "
                       "pushes inwards rather than off the grid");
            }

            delete tree;
        }
    }

    remove(scratch.c_str());

    printf("\n%d failure(s)\n", failed);

    return failed;
}
