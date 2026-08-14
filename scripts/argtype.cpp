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

static const char *scratch = "/tmp/argtype-scratch.dsp";

static void write (const string &text)
{
    ofstream out(scratch, ios::binary | ios::trunc);

    out << text;
}

/* The smallest file that loads, plus whatever the caller wants in it. */
static string wrap (const string &controls, const string &nodes)
{
    return string("name \"argtype\";\n\n") + controls +
           "\nnode ionode {\n    channels = 2;\n    play = 1;\n};\n\n" +
           nodes + "\nio ionode;\n";
}

/* Parses the scratch file and hands back the named chanarg, or NULL. The tree
   is leaked deliberately -- the arg points into it and this process is about to
   exit either way. */
static thArg *chanargOf (thSynth &synth, const string &name)
{
    thSynthTree *tree = synth.parseTree(scratch);

    if (tree == NULL)
        return NULL;

    return tree->getChanArg(name);
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

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    /* ---- the plugin declares it ---------------------------------------- */

    /* Through a node rather than through thPluginManager, whose map is keyed by
       a path this harness would have to reconstruct. A .dsp naming the plugin
       is how everything else in the tree gets at one. */
    write(wrap("", "node osc osc::simple {\n    freq = ionode->note;\n};\n"));

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
    }

    /* ---- and it reaches the control wired to it ------------------------- */

    write(wrap("@wave = 3;\n@wave.widget = 1;\n@wave.min = 0;\n"
               "@wave.max = 5;\n",
               "node osc osc::simple {\n    waveform = @wave;\n};\n"));

    {
        thArg *a = chanargOf(synth, "wave");

        if (a == NULL)
            fail("@wave survives a parse", "");
        else if (a->step() != 1)
            fail("@wave picks up the step from osc::simple", "");
        else if (a->valueNames().size() != 6)
            fail("@wave picks up the names", joined(a->valueNames()));
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

        write(wrap("@wave = 3;\n@wave.widget = 1;\n@wave.min = 0;\n"
                   "@wave.max = 5;\n", nodes));

        thArg *a = chanargOf(synth, "wave");

        if (a == NULL)
            fail("@wave survives a parse with two consumers", "");
        else if (a->step() != 0 || !a->valueNames().empty())
            fail(order ? "the selector is visited second"
                       : "the selector is visited first",
                 "two consumers that disagree left the control typed as " +
                 joined(a->valueNames()));
        else if (order)
            ok("a control two things disagree about is left alone, whichever "
               "the pass sees first");
    }

    /* Two consumers that agree are not a disagreement. */
    write(wrap("@wave = 3;\n@wave.widget = 1;\n@wave.min = 0;\n"
               "@wave.max = 5;\n",
               "node osc1 osc::simple {\n    waveform = @wave;\n};\n"
               "node osc2 osc::simple {\n    waveform = @wave;\n};\n"));

    {
        thArg *a = chanargOf(synth, "wave");

        if (a == NULL || a->valueNames().size() != 6)
            fail("two consumers that agree still type the control",
                 a ? joined(a->valueNames()) : string("no arg"));
        else
            ok("two consumers that agree are not a disagreement");
    }

    /* ---- the .dsp can say it itself, and wins --------------------------- */

    write(wrap("@steps = 2;\n@steps.widget = 1;\n@steps.min = 0;\n"
               "@steps.max = 8;\n@steps.step = 1;\n",
               "node gain math::mul {\n    in0 = ionode->note;\n"
               "    in1 = @steps;\n};\n"));

    {
        thArg *a = chanargOf(synth, "steps");

        if (a == NULL || a->step() != 1)
            fail("@x.step types a control the plugin says nothing about", "");
        else
            ok("a .dsp can type a control the plugin knows nothing about");
    }

    write(wrap("@mode = 1;\n@mode.widget = 1;\n@mode.min = 0;\n"
               "@mode.max = 2;\n@mode.values = \"Off, Low, High\";\n",
               "node gain math::mul {\n    in0 = ionode->note;\n"
               "    in1 = @mode;\n};\n"));

    {
        thArg *a = chanargOf(synth, "mode");

        if (a == NULL || a->valueNames().size() != 3)
            fail("@x.values names them", a ? joined(a->valueNames()) : "");
        else if (a->valueNames()[2] != "High")
            fail("the space after a comma is not part of the name",
                 a->valueNames()[2]);
        else if (a->step() != 1)
            fail("naming the values implies a step", "");
        else
            ok("@x.values names a control's values, spaces trimmed");
    }

    /* A hole. osc::window implements waveforms 0, 2 and 3 and not 1, and a
       value with no name has to be sayable or a list cannot describe it. */
    write(wrap("@mode = 0;\n@mode.widget = 1;\n@mode.min = 0;\n"
               "@mode.max = 2;\n@mode.values = \"Off,,High\";\n",
               "node gain math::mul {\n    in0 = ionode->note;\n"
               "    in1 = @mode;\n};\n"));

    {
        thArg *a = chanargOf(synth, "mode");

        if (a == NULL || a->valueNames().size() != 3)
            fail("a hole is a value, not the end of the list",
                 a ? joined(a->valueNames()) : "");
        else if (!a->valueNames()[1].empty() || a->valueNames()[2] != "High")
            fail("the hole is in the middle", joined(a->valueNames()));
        else
            ok("a value with no name is a gap in the list, not its end");
    }

    /* The override: the file says continuous, the plugin says otherwise, and
       the file wins. Without typedByFile this cannot be expressed at all --
       a step of 0 is the default, so saying it and saying nothing would be the
       same state. */
    write(wrap("@wave = 3;\n@wave.widget = 1;\n@wave.min = 0;\n"
               "@wave.max = 5;\n@wave.step = 0;\n",
               "node osc osc::simple {\n    waveform = @wave;\n};\n"));

    {
        thArg *a = chanargOf(synth, "wave");

        if (a == NULL)
            fail("@wave survives a parse with an explicit step", "");
        else if (a->step() != 0 || !a->valueNames().empty())
            fail("`@wave.step = 0' is overridden by the plugin",
                 joined(a->valueNames()));
        else
            ok("the file has the last word, including when it says "
               "`continuous'");
    }

    /* ---- and the graph draws it ---------------------------------------- */

    /* The padded maximum, which is the thing that started all this: eight
       shipped patches declare 5.1 or 5.5 for six waveforms so that a
       continuous slider could still truncate to the last one. */
    write(wrap("@wave = 3;\n@wave.widget = 1;\n@wave.min = 0;\n"
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

    remove(scratch);

    printf("\n%d failure(s)\n", failed);

    return failed;
}
