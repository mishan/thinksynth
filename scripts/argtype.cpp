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
#include "NodeCatalog.h"
#include "NodeEdit.h"

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

/* One note through one file. A fresh synth each time, and srand reseeded, for
   the reason dspcheck spells out: twelve DSPs are built on osc::static and are
   deterministic only because both renders start from the same seed. */
static bool render (const string &pluginPath, const char *file,
                    vector<float> &out)
{
    srand(1);

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    if (synth.loadTree(file, 0, 100) == NULL)
        return false;

    synth.addNote(0, 60, 100);

    const int frame = synth.audioChannelCount() * synth.getWindowlen();

    out.clear();

    for (int w = 0; w < 4; w++)
    {
        synth.process();

        const float *buf = synth.getOutput();

        out.insert(out.end(), buf, buf + frame);
    }

    return true;
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
            else if (!p->getArgDesc(freq).empty() || p->argHasDefault(freq))
                fail("an undeclared arg has no description and no default",
                     p->getArgDesc(freq));
            else
                ok("an arg the plugin says nothing about stays continuous, "
                   "undescribed and without a default");

            /* The description is the plugin author's own trailing comment,
               moved somewhere a panel can read it. Checked against the text
               rather than merely for non-emptiness: the point of harvesting
               these instead of writing them is that they say what the author
               said. */
            int mul = -1;

            for (int k = 0; k < p->argCount(); k++)
                if (p->getArgName(k) == "mul")
                    mul = k;

            if (mul < 0)
                fail("osc::simple registers mul", "");
            else if (p->getArgDesc(mul) != "Multiply the wavelength by this")
                fail("mul carries the comment the plugin already had",
                     p->getArgDesc(mul));
            else
                ok("an arg's description is the one its author wrote");

            /* And the default is the plugin's own zero-case: osc::simple does
               `if (amp_max == 0) amp_max = TH_MAX;', so its amp already had a
               default and it was written where nothing could read it. */
            int amp = -1;

            for (int k = 0; k < p->argCount(); k++)
                if (p->getArgName(k) == "amp")
                    amp = k;

            if (amp < 0 || !p->argHasDefault(amp))
                fail("osc::simple declares a default for amp", "");
            else if (p->getArgDefault(amp) != TH_MAX)
                fail("amp defaults to what the callback substitutes for 0", "");
            else
                ok("a default is the plugin's own zero-case, not a taste "
                   "judgement");
        }

        /* The plugin outlives this -- thPluginManager owns it and the synth
           owns that -- but the tree does not. */
        delete tree;
    }

    /* ---- an arg cast to int is a whole number -------------------------- */

    /* Eight args across six plugins are read `(int)(*in_x)[0]' and used as a
       length or a count. None is a selector, so none has names -- a delay line
       of 220 samples has nothing to call itself.
     *
     * Worth checking rather than assuming: this is the one part of the sweep
     * where the shipped corpus can say nothing at all. No .dsp drives any of
     * these from a control, and every one of them is bound to a whole number
     * already, so nothing here changes any existing patch. It changes what
     * happens when someone builds the next one. */
    {
        static const struct { const char *node; const char *arg; } cases[] = {
            { "delay::echo",        "size"  },
            { "filt::comb",         "size"  },
            { "impulse::blackman",  "len"   },
            { "impulse::parabola",  "len"   },
            { "impulse::sine",      "len"   },
            { "impulse::square",    "len"   },
            { "osc::multiwave",     "waves" },
            { "osc::multisined",    "waves" },
            { NULL, NULL }
        };

        int whole = 0;

        for (int i = 0; cases[i].node; i++)
        {
            char nodes[256];

            snprintf(nodes, sizeof(nodes),
                     "node n %s {\n    freq = ionode->note;\n};\n",
                     cases[i].node);

            writeOrFail(wrap("", nodes));

            thSynthTree *t = synth.parseTree(scratch);

            thNode *n = t ? t->findNode("n") : NULL;

            thPlugin *p = n ? n->plugin() : NULL;

            if (p == NULL)
            { fail("a plugin loads", cases[i].node); delete t; continue; }

            int idx = -1;

            for (int k = 0; k < p->argCount(); k++)
                if (p->getArgName(k) == cases[i].arg)
                    idx = k;

            if (idx < 0)
                fail("registers its length arg", cases[i].node);
            else if (p->getArgStep(idx) != 1)
                fail("is read as a whole number",
                     string(cases[i].node) + "." + cases[i].arg);
            else if (!p->getArgValues(idx).empty())
                fail("a length has no value names", cases[i].node);
            else
                whole++;

            delete t;
        }

        if (whole == 8)
            ok("eight args their plugins cast to int read as whole numbers, "
               "and none pretends to be a selector");
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

    /* ---- writing a default changes the file, not the sound -------------- */

    /* The claim NodeEdit::addNode's comment makes, checked rather than
     * asserted. A declared default is the value the plugin already substitutes
     * for 0, so a node that spells it out and a node that leaves it at the zero
     * buildArgMap() invents must render the same samples. If that ever stops
     * being true, the defaults have stopped being transcriptions and become
     * somebody's opinion.
     */
    {
        const string plain = scratchPath("argtype-plain.dsp");
        const string spelt = scratchPath("argtype-spelt.dsp");

        vector<pair<string, double> > none, initial;

        NodeCatalog::Entry e;
        NodeCatalog cat;

        cat.scan(pluginPath);

        if (!cat.describe("osc::simple", synth.getPluginManager(), e))
            fail("the catalog describes osc::simple", "");
        else if (e.defaults.empty())
            fail("osc::simple declares at least one default", "");
        else
        {
            for (size_t i = 0; i < e.defaults.size(); i++)
                initial.push_back(make_pair(e.defaults[i].name,
                                            e.defaults[i].value));

            string why;
            bool built = true;

            for (int which = 0; which < 2 && built; which++)
            {
                const string f = which ? spelt : plain;

                remove(f.c_str());

                if (NodeEdit::createFile(f.c_str(), "argtype", "argtype", why)
                        != NodeEdit::OK ||
                    NodeEdit::addNode(f.c_str(), "osc", "osc::simple",
                                      which ? initial : none, why)
                        != NodeEdit::OK ||
                    NodeEdit::connect(f.c_str(), "osc", "freq", "ionode", "note", why)
                        != NodeEdit::OK ||
                    NodeEdit::connect(f.c_str(), "ionode", "out0", "osc", "out", why)
                        != NodeEdit::OK)
                { fail("building a file to render", why); built = false; }
            }

            if (built)
            {
                vector<float> a, b;

                if (!render(pluginPath, plain.c_str(), a) ||
                    !render(pluginPath, spelt.c_str(), b))
                    fail("both files render", "");
                else if (a.empty())
                    fail("the rendered note is not empty", "");
                else if (a != b)
                    fail("spelling out a plugin's own defaults changed the "
                         "sound", "");
                else
                {
                    /* And it did put them in the file, or the comparison above
                       is comparing a file with itself. */
                    string text;

                    ifstream in(spelt.c_str());

                    text.assign((istreambuf_iterator<char>(in)),
                                istreambuf_iterator<char>());

                    if (text.find("mul = 1;") == string::npos)
                        fail("the defaults were actually written", text);
                    else
                        ok("a node written with its plugin's defaults renders "
                           "identically to one written without them");
                }
            }
        }

        remove(plain.c_str());
        remove(spelt.c_str());
    }

    /* ---- units are folded at the synth's rate, not the compiler's ------ */

    /* `5 ms' used to become 220.5 inside the grammar action that read it,
     * using the compile-time TH_SAMPLE. Two things were wrong with that.
     * The visible one: `thinksynth -r 48000' opened the device at 48k and
     * played every envelope in every patch 8.8% short, because the numbers
     * had been converted for a rate nothing was running at. The quieter
     * one: a parse should say what the file says, and a parser that folds
     * engine semantics into a literal has answered a question that was not
     * its own.
     *
     * The corpus sweeps cannot see any of this -- they run at 44100, where
     * the old fold and the new one agree exactly, which is the point of
     * running them. What has to be checked here is that the two disagree
     * when the rate does.
     */
    {
        /* A duration and a percentage, and a range written in samples
           rather than in the value's own unit -- which is the case that
           makes the fold a property of each *site* rather than of the arg.
           A node arg too: those never carried a unit before. */
        writeOrFail(wrap("@decay = 500 ms;\n"
                         "@decay.max = 88200;\n"
                         "@gain = 90%;\n",
                         "node osc osc::simple {\n"
                         "    freq = ionode->note;\n"
                         "    mul = 5 ms;\n"
                         "};\n"));

        struct Read { bool ok; float decay, decayMax, gain, mul; string units; };

        auto readAt = [&](long rate) -> Read
        {
            Read r;

            r.ok = false;
            r.decay = r.decayMax = r.gain = r.mul = 0;

            thSynth s(pluginPath, TH_DEFAULT_WINDOW_LENGTH, (int)rate);
            thSynthTree *t = s.parseTree(scratch);

            if (t == NULL)
                return r;

            thArg *decay = t->getChanArg("decay");
            thArg *gain = t->getChanArg("gain");
            thArg *mul = t->getArg("osc", "mul");

            if (decay && gain && mul)
            {
                r.ok = true;
                r.decay = (*decay)[0];
                r.decayMax = decay->max();
                r.gain = (*gain)[0];
                r.mul = (*mul)[0];
                r.units = mul->units();
            }

            delete t;

            return r;
        };

        const Read at44 = readAt(44100);
        const Read at48 = readAt(48000);

        if (!at44.ok || !at48.ok)
            fail("the units file loads at both rates", "");
        else
        {
            /* 500 ms is 22050 samples at 44100 and 24000 at 48000. Exact in
               both cases, so this is an equality and not a tolerance. */
            if (at44.decay != 22050.0f || at48.decay != 24000.0f)
                fail("a duration follows the synth's sample rate",
                     "44100 -> " + to_string(at44.decay) + ", 48000 -> " +
                     to_string(at48.decay));
            else
                ok("a duration folds at the rate the synth is running at");

            /* The range was written in samples. Folding the arg by its unit
               rather than each site by its own would have converted it
               twice over. */
            if (at44.decayMax != 88200.0f || at48.decayMax != 88200.0f)
                fail("a range written without a unit is left alone",
                     to_string(at44.decayMax) + " / " +
                     to_string(at48.decayMax));
            else
                ok("a range written without a unit is not folded");

            /* A percentage is a fraction of TH_MAX and has nothing to do
               with time; the rate must not touch it. */
            if (at44.gain != 0.9f || at48.gain != 0.9f)
                fail("a percentage ignores the sample rate",
                     to_string(at44.gain) + " / " + to_string(at48.gain));
            else
                ok("a percentage is the same at any sample rate");

            /* Node args never used to carry a unit at all -- only chanargs
               did, because only chanargs were ever drawn. */
            if (at44.mul != 220.5f || at48.mul != 240.0f)
                fail("a node arg's duration follows the rate too",
                     to_string(at44.mul) + " / " + to_string(at48.mul));
            else if (at44.units != "ms")
                fail("a node arg remembers what it was written in",
                     at44.units.empty() ? "(none)" : at44.units);
            else
                ok("a node arg folds at the rate and remembers its unit");
        }
    }

    /* Declaring one control twice is what the deferred folds have to be
     * swept for: setChanArg deletes the arg it replaces, and a record still
     * aimed at that arg would be aimed at freed memory by the time
     * foldUnits ran. Only reachable from a file with a duplicate in it,
     * which is exactly the sort of use-after-free nothing exercises until
     * someone's file has a typo in it. */
    {
        writeOrFail(wrap("@decay = 500 ms;\n"
                         "@decay = 250 ms;\n",
                         "node osc osc::simple {\n"
                         "    freq = ionode->note;\n"
                         "};\n"));

        thSynthTree *t = synth.parseTree(scratch);
        thArg *decay = t ? t->getChanArg("decay") : NULL;

        if (decay == NULL)
            fail("a redeclared control loads", "");
        else if ((*decay)[0] != 11025.0f)
            fail("a redeclared control folds the declaration that won",
                 to_string((*decay)[0]));
        else
            ok("a control declared twice folds once, on the arg that "
               "survived");

        delete t;
    }

    /* A unit inside arithmetic is refused rather than guessed. It used to
       produce a number by accident -- the leaf was folded before the
       operator ran -- and with the fold deferred there is no accident left
       to have, so the file is rejected instead of quietly meaning
       something else. */
    {
        writeOrFail(wrap("", "node osc osc::simple {\n"
                             "    freq = ionode->note;\n"
                             "    mul = 5 ms + 3;\n"
                             "};\n"));

        thSynthTree *t = synth.parseTree(scratch);

        if (t != NULL)
        {
            fail("arithmetic on a united value is refused", "it parsed");
            delete t;
        }
        else
            ok("a unit inside arithmetic is a parse error");
    }

    /* The other way a parse gives up partway, and the reason the grammar
     * grew %destructor declarations: YYERROR unwinds the stack, and a
     * failure from inside a node body leaves the node's name, its plugin's
     * name and the arg's name on it. The arithmetic case above is what
     * found that; this one is here because the plugin-load failure stopped
     * freeing by hand when the destructors took the job, and a double free
     * would look exactly like a pass until asan ran. Both paths are
     * exercised, and the CI job with detect_leaks=1 is the assertion. */
    {
        writeOrFail(wrap("", "node osc gen::no_such_plugin {\n"
                             "    freq = ionode->note;\n"
                             "};\n"));

        thSynthTree *t = synth.parseTree(scratch);

        if (t != NULL)
        {
            fail("a node naming a plugin that will not load is refused",
                 "it parsed");
            delete t;
        }
        else
            ok("a plugin that will not load fails the parse, and unwinds it");
    }

    remove(scratch.c_str());

    printf("\n%d failure(s)\n", failed);

    return failed;
}
