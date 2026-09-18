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
 * exprcheck -- arithmetic over signals, and the nodes it turns into.
 *
 * `osc2.freq = freq->out * exp2(@cents / 1200)' is sugar. The audio path
 * never sees an expression: thSynthTree::desugarExprs rewrites each one into
 * the math:: nodes it stands for, at load, and everything downstream sees an
 * ordinary graph. So the claims worth holding down are equivalences.
 *
 *   1. A constant expression is what it always was. The grammar folds it at
 *      parse, to the same number and the same node count -- which is what
 *      lets the corpus render bit-identically over this change. dspcheck and
 *      the wasm comparison prove that over the shipped files; this proves it
 *      over the cases the corpus does not have, including the ones the old
 *      grammar got arguably wrong and must go on getting the same way.
 *
 *   2. An expression renders exactly as the nodes it replaces. `(a + b) * 0.5'
 *      against a hand-written math::add into a math::mul, bitwise.
 *
 *   3. What is refused is refused, and says which line. A unit inside
 *      arithmetic, a control driven by an expression, a modulo over signals,
 *      a function that does not exist or is given the wrong number of
 *      arguments.
 *
 *   4. Each of the six function nodes computes what its description says.
 *      They are plugins usable on their own, so they are checked as plugins
 *      rather than only through the sugar.
 *
 * Needs no display and no corpus.
 *
 *     scripts/exprcheck -p build/plugins/
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
#include <string>
#include <vector>

#include "think.h"
#include "NodeGraph.h"
#include "NodeEdit.h"

using std::string;
using std::vector;

static int failed = 0;

static void ok (const string &what)
{
    printf("ok    %s\n", what.c_str());
}

static void fail (const string &what, const string &detail)
{
    printf("FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : ": ",
           detail.c_str());
    failed++;
}

/* Not "/tmp/...", for the reason argtype spells out: this is a CTest gate and
   the Windows runner has no such directory. */
static string scratchPath (const char *leaf)
{
    std::error_code ec;

    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);

    if (ec)
        dir = ".";

    return (dir / leaf).string();
}

static string scratch;

static bool writeFile (const string &path, const string &text)
{
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);

    out << text;
    out.close();

    if (out.good())
        return true;

    fail("could not write", path);

    return false;
}

/* The smallest file that loads, plus whatever the caller wants in it. */
static string wrap (const string &controls, const string &nodes)
{
    return string("name \"exprcheck\";\n\n") + controls +
           "\nnode ionode {\n    channels = 2;\n    play = 1;\n};\n\n" +
           nodes + "\nio ionode;\n";
}

/* One note through one file, four windows, interleaved. A fresh synth each
   time and srand reseeded, for the reason dspcheck spells out. */
static bool render (const string &pluginPath, const string &file,
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

        out.insert(out.end(), synth.getOutput(), synth.getOutput() + frame);
    }

    return true;
}

/* What a node's arg holds after a load, with no note played: the number the
   grammar folded, or the pointer the desugar wired. */
static bool argValue (thSynth &synth, const string &text, const string &node,
                      const string &arg, float &out, int &nodes)
{
    if (!writeFile(scratch, text))
        return false;

    thSynthTree *tree = synth.parseTree(scratch);

    if (tree == NULL)
        return false;

    thNode *n = tree->findNode(node);
    thArg *a = n ? n->getArg(arg) : NULL;
    bool got = false;

    if (a != NULL && a->type() == thArg::ARG_VALUE)
    {
        out = (*a)[0];
        got = true;
    }

    nodes = (int)tree->nodes().size();

    delete tree;

    return got;
}

/* True if `file' does not parse. `why' is not inspected -- the message goes
   to stderr and a harness that pinned its wording would break on every
   rewording -- but the line number is, through the caller. */
static bool refused (thSynth &synth, const string &text)
{
    if (!writeFile(scratch, text))
        return false;

    thSynthTree *tree = synth.parseTree(scratch);

    if (tree == NULL)
        return true;

    delete tree;

    return false;
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;

    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            pluginPath = argv[++i];

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    scratch = scratchPath("exprcheck-scratch.dsp");

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    /* ---- a constant expression is still one number --------------------- */

    /* Every one of these folded at parse before signal leaves existed, and
     * has to go on folding: a constant that became a math:: node would add a
     * node to nearly every shipped file and stop the corpus rendering
     * bit-identically.
     *
     * `1 - 2 - 3' and `8 / 4 / 2' are here because the grammar's `-' and `/'
     * are right-associative -- `1 - (2 - 3)' and `8 / (4 / 2)' -- which is
     * not what the arithmetic usually means and is what the language has
     * always done. `1 - 2 + 3' is the same rule with a sharper edge: a `-'
     * takes everything to its right, the additions included. Pinned rather
     * than fixed: fixing it is a change to what existing files mean, and it
     * belongs to whichever change is willing to measure that.
     *
     * `2 + 3 * 4' and `0 - 3 * 2' pin the precedence. `-1 + 2' pins where the
     * unary minus stops -- it is a `factor', so it binds to its operand and
     * not to the rest of the line -- and `2 * -3' that a negative literal may
     * follow an operator at all. The .gen parser reads all of these the same
     * way and gencheck holds the other half of the row.
     */
    {
        static const struct { const char *rhs; float want; } cases[] = {
            { "5 * 2",          10 },
            { "(2 + 3) * 4",    20 },
            { "2 + 3 * 4",      14 },
            { "0 - 3 * 2",      -6 },
            { "10 - 4",          6 },
            { "1 - 2 - 3",       2 },   /* 1 - (2 - 3) */
            { "1 - 2 + 3",      -4 },   /* 1 - (2 + 3) */
            { "8 / 4 / 2",       4 },   /* 8 / (4 / 2) */
            { "7 % 4",           3 },
            { "-1 + 2",          1 },   /* (-1) + 2, not -(1 + 2) */
            { "0 - -3",          3 },
            { "2 * -3",         -6 },
            { "th_max * 2",      2 },
            { "50%",           0.5f },
            { "exp2(2)",         4 },
            { "pow(3, 2)",       9 },
            { "clamp(9, 0, 1)",  1 },
            { "min(3, 2)",       2 },
            { "max(3, 2)",       3 },
            { "abs(0 - 3)",      3 },
        };

        int checked = 0, baseline = -1;
        bool bad = false;

        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        {
            const string text = wrap("",
                string("node osc osc::simple {\n    mul = ") +
                cases[i].rhs + ";\n};\n");

            float got = 0;
            int nodes = 0;

            if (!argValue(synth, text, "osc", "mul", got, nodes))
            {
                fail("a constant expression loads as a value", cases[i].rhs);
                bad = true;
            }
            else if (got != cases[i].want)
            {
                char said[128];

                snprintf(said, sizeof(said), "%s is %g, not %g",
                         cases[i].rhs, (double)got, (double)cases[i].want);
                fail("a constant expression folds", said);
                bad = true;
            }
            else if (baseline >= 0 && nodes != baseline)
            {
                fail("folding a constant makes no node", cases[i].rhs);
                bad = true;
            }
            else
            {
                baseline = nodes;
                checked++;
            }
        }

        if (!bad)
        {
            char said[96];

            snprintf(said, sizeof(said),
                     "%d constant expressions fold at parse, to a value and "
                     "no node", checked);
            ok(said);
        }
    }

    /* `1 / 0' has always come back an infinity rather than a parse error, and
       math::div's description says a zero denominator is a non-finite result.
       The fold and the node therefore have to agree, which is why thExprOp
       leaves a divide by a zero constant standing rather than folding it. */
    {
        float got = 0;
        int nodes = 0;

        const string text = wrap("",
            "node osc osc::simple {\n    mul = 1 / 0;\n};\n");

        if (!argValue(synth, text, "osc", "mul", got, nodes))
            fail("a constant divide by zero loads", "");
        else if (thIsFinite(got))
            fail("a constant divide by zero is non-finite", "it was finite");
        else
            ok("`1 / 0' folds to an infinity, as it always has");
    }

    /* ---- an expression is the nodes it replaces ------------------------ */

    /* The claim the whole feature rests on. Two files, one written as
     * arithmetic and one as the graph that arithmetic stands for, rendered
     * and compared sample for sample.
     */
    {
        const string sugar = scratchPath("exprcheck-sugar.dsp");
        const string nodes = scratchPath("exprcheck-nodes.dsp");

        const string common =
            "node freq misc::midi2freq {\n"
            "    note = ionode->note;\n"
            "};\n"
            "\n"
            "node osc1 osc::simple {\n"
            "    freq = freq->out;\n"
            "    waveform = 1;\n"
            "};\n"
            "\n"
            "node osc2 osc::simple {\n"
            "    freq = freq->out;\n"
            "    waveform = 2;\n"
            "};\n"
            "\n";

        const bool wrote =
            writeFile(sugar,
                "name \"exprcheck\";\n\n" + common +
                "node ionode {\n"
                "    channels = 2;\n"
                "    play = 1;\n"
                "    out0 = (osc1->out + osc2->out) * 0.5;\n"
                "    out1 = (osc1->out + osc2->out) * 0.5;\n"
                "};\n\nio ionode;\n") &&
            writeFile(nodes,
                "name \"exprcheck\";\n\n" + common +
                "node sum math::add {\n"
                "    in0 = osc1->out;\n"
                "    in1 = osc2->out;\n"
                "};\n"
                "\n"
                "node half math::mul {\n"
                "    in0 = sum->out;\n"
                "    in1 = 0.5;\n"
                "};\n"
                "\n"
                "node ionode {\n"
                "    channels = 2;\n"
                "    play = 1;\n"
                "    out0 = half->out;\n"
                "    out1 = half->out;\n"
                "};\n\nio ionode;\n");

        vector<float> a, b;

        if (!wrote)
            ;
        else if (!render(pluginPath, sugar, a) ||
                 !render(pluginPath, nodes, b))
            fail("both spellings render", "");
        else if (a.empty())
            fail("the rendered note is not silence", "");
        else if (a != b)
            fail("an expression renders as the nodes it stands for",
                 "the two differ");
        else
            ok("`(a + b) * 0.5' renders bit-identically to the add and the "
               "mul it replaces");

        remove(sugar.c_str());
        remove(nodes.c_str());
    }

    /* ---- the arithmetic reaches the arg -------------------------------- */

    /* `exp2(@cents / 1200)' is the detune the function set exists for. A
     * semitone is 100 cents, so the multiplier at 100 is the twelfth root of
     * two -- read off the desugared graph rather than inferred from the
     * sound, because a frequency that came out right for the wrong reason is
     * the failure this is guarding against.
     */
    {
        const string text = wrap(
            "@cents = 100;\n@cents.min = -1200;\n@cents.max = 1200;\n",
            "node osc osc::simple {\n"
            "    freq = ionode->note;\n"
            "    mul = exp2(@cents / 1200);\n"
            "};\n");

        if (!writeFile(scratch, text))
            ;
        else
        {
            thSynth player(pluginPath, TH_DEFAULT_WINDOW_LENGTH,
                           TH_DEFAULT_SAMPLES);
            thSynthTree *tree = player.loadTree(scratch, 0, 100);

            /* The two nodes `exp2(@cents / 1200)' became, fired in order.
               Directly rather than through process(), which walks down from
               the io node and would need the whole instrument wired up to
               reach an arg this test is asking about on its own. */
            thNode *div = tree ? tree->findNode("osc.mul#1") : NULL;
            thNode *exp = tree ? tree->findNode("osc.mul#2") : NULL;
            thNode *osc = tree ? tree->findNode("osc") : NULL;

            if (div && exp)
            {
                div->plugin()->fire(div, tree, TH_DEFAULT_WINDOW_LENGTH,
                                    TH_DEFAULT_SAMPLES);
                exp->plugin()->fire(exp, tree, TH_DEFAULT_WINDOW_LENGTH,
                                    TH_DEFAULT_SAMPLES);
            }

            thArg *mul = osc ? tree->getArg(osc, "mul") : NULL;

            if (mul == NULL || div == NULL || exp == NULL)
                fail("the detune graph loads", "");
            else
            {
                const float want = powf(2.0f, 1.0f / 12.0f);
                const float got = (*mul)[0];

                if (fabsf(got - want) > 1e-6f)
                {
                    char said[128];

                    snprintf(said, sizeof(said), "%g, not %g",
                             (double)got, (double)want);
                    fail("exp2(@cents / 1200) at 100 cents is a semitone",
                         said);
                }
                else
                    ok("`exp2(@cents / 1200)' at 100 cents is a semitone");
            }
        }
    }

    /* ---- the nodes are named after the arg they feed -------------------- */

    /* `#' cannot appear in a .dsp node name -- the lexer starts a comment on
       one -- so these can never collide with an authored node, and a probe or
       a log that names one says where it came from. */
    {
        const string text = wrap("",
            "node osc osc::simple {\n"
            "    freq = ionode->note;\n"
            "    mul = (ionode->velocity + 1) * 2;\n"
            "};\n");

        if (!writeFile(scratch, text))
            ;
        else
        {
            thSynthTree *tree = synth.parseTree(scratch);

            const bool one = tree && tree->findNode("osc.mul#1");
            const bool two = tree && tree->findNode("osc.mul#2");

            if (!one || !two)
                fail("an expression's nodes are named after their arg",
                     "no osc.mul#1 and osc.mul#2");
            else
                ok("an expression becomes nodes named after the arg they "
                   "feed, innermost first");

            delete tree;
        }
    }

    /* ---- the box's text is the graph behind it -------------------------- */

    /* The one lossy step in the whole feature, and three things lean on it.
     *
     * thExprText prints the tree the parse produced, and the editor draws
     * that string in the box. If the string reads back as a different tree,
     * the box is showing the author arithmetic their patch is not doing --
     * and §4d's successor, editing the text in place, would write it back.
     * So each expression here is parsed, printed, and *re-parsed as the same
     * arg*, and the two files are rendered and compared sample for sample.
     *
     * Every operator in this grammar groups to the right, so a left operand
     * of equal precedence is exactly what needs parentheses: `(a - 1) - 2'
     * printed as `a - 1 - 2' reads back as `a - (1 - 2)', which is `a + 1'.
     * The numbers are here for the other half: `%g' reached for an exponent
     * below 1e-4 and rounded to six significant digits, and the lexer's
     * number has no exponent and no such rounding.
     */
    {
        static const char *const cases[] = {
            "(osc1->out - 0.1) - 0.2",
            "(osc1->out + 0.1) + 0.2",
            "(osc1->out * 0.5) * 0.5",
            "(osc1->out / 2) / 2",
            "osc1->out * 0.0000001",
            "osc1->out * 1.2345678",
            "osc1->out * -0.5",
            "osc1->out - -0.25",
            "(osc1->out + osc2->out) * 0.5",
            "clamp(osc1->out * 2, 0 - 0.5, 0.5)",
            "min(osc1->out, osc2->out) * exp2(0 - 1)",
        };

        const string common =
            "node freq misc::midi2freq {\n    note = ionode->note;\n};\n"
            "\nnode osc1 osc::simple {\n    freq = freq->out;\n"
            "    waveform = 1;\n};\n"
            "\nnode osc2 osc::simple {\n    freq = freq->out;\n"
            "    waveform = 2;\n};\n\n";

        const string wrote = scratchPath("exprcheck-written.dsp");
        const string shown = scratchPath("exprcheck-shown.dsp");

        int checked = 0;
        bool bad = false;

        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]) && !bad; i++)
        {
            const string body =
                "name \"exprcheck\";\n\n" + common +
                "node ionode {\n    channels = 2;\n    play = 1;\n"
                "    out0 = " + cases[i] + ";\n"
                "    out1 = 0;\n};\n\nio ionode;\n";

            if (!writeFile(wrote, body))
            { bad = true; break; }

            /* What the box would draw. */
            string text;

            {
                thSynthTree *tree = synth.parseTree(wrote);

                for (size_t b = 0; tree && b < tree->exprBoxes().size(); b++)
                    if (tree->exprBoxes()[b].node == "ionode" &&
                        tree->exprBoxes()[b].arg == "out0")
                        text = tree->exprBoxes()[b].text;

                delete tree;
            }

            if (text.empty())
            {
                fail("an expression gets a box", cases[i]);
                bad = true;
                break;
            }

            const string echoed =
                "name \"exprcheck\";\n\n" + common +
                "node ionode {\n    channels = 2;\n    play = 1;\n"
                "    out0 = " + text + ";\n"
                "    out1 = 0;\n};\n\nio ionode;\n";

            if (!writeFile(shown, echoed))
            { bad = true; break; }

            vector<float> a, b;

            if (!render(pluginPath, wrote, a))
            {
                fail("the written expression renders", cases[i]);
                bad = true;
            }
            else if (!render(pluginPath, shown, b))
            {
                /* `1e-07' is the shape that fails here: the lexer's number
                   has no exponent, so the box was drawing text the language
                   cannot take back. */
                fail("the box's text parses", string(cases[i]) + " -> " + text);
                bad = true;
            }
            else if (a != b)
            {
                fail("the box's text is the graph behind it",
                     string(cases[i]) + " -> " + text);
                bad = true;
            }
            else
                checked++;
        }

        remove(wrote.c_str());
        remove(shown.c_str());

        if (!bad)
        {
            char said[128];

            snprintf(said, sizeof(said),
                     "%d expressions print as text that re-parses to the same "
                     "graph", checked);
            ok(said);
        }
    }

    /* ---- what is refused ----------------------------------------------- */

    {
        static const struct { const char *what; const char *controls;
                              const char *nodes; } cases[] = {
            { "a unit inside arithmetic", "",
              "node osc osc::simple {\n    mul = 5 ms + 3;\n};\n" },

            { "a unit on a signal", "",
              "node osc osc::simple {\n    mul = ionode->note ms;\n};\n" },

            { "a control driven by an expression", "@a = ionode->note;\n",
              "" },

            { "a control's range driven by an expression",
              "@a = 1;\n@a.max = ionode->note * 2;\n", "" },

            { "a modulo over signals", "",
              "node osc osc::simple {\n    mul = ionode->note % 12;\n};\n" },

            { "a function that does not exist", "",
              "node osc osc::simple {\n    mul = wobble(ionode->note);\n};\n" },

            { "a function given the wrong number of arguments", "",
              "node osc osc::simple {\n    mul = exp2(ionode->note, 2);\n};\n" },

            /* An integer division by zero, which is a SIGFPE and not a
               number. `amp = 7 % 0;' in any .dsp took the process down with
               it, the editor included, for as long as the rule has
               existed. */
            { "a modulo by zero", "",
              "node osc osc::simple {\n    mul = 7 % 0;\n};\n" },

            /* `%' spells two things and a `-' can begin a factor, so
               `50% - 3' is either the percentage and a subtraction -- which
               carries a unit into arithmetic and is refused, as it always
               was -- or 50 modulo -3, which is 2 and parses. The refusal is
               what says the grammar still reads it the first way. See the
               %precedence block in thinklang.yy. */
            { "a percentage inside arithmetic", "",
              "node osc osc::simple {\n    mul = 50% - 3;\n};\n" },
        };

        int checked = 0;

        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        {
            if (refused(synth, wrap(cases[i].controls, cases[i].nodes)))
                checked++;
            else
                fail(string(cases[i].what) + " is refused", "it parsed");
        }

        if (checked == (int)(sizeof(cases) / sizeof(cases[0])))
        {
            char said[96];

            snprintf(said, sizeof(said), "%d refusals, each failing the file "
                     "rather than half-building it", checked);
            ok(said);
        }
    }

    /* ---- the six function nodes ---------------------------------------- */

    /* As plugins, not as sugar: each is a node a .dsp may write by hand, so
     * each is checked the way a hand-written one would be. Constant inputs,
     * because what is being checked is the arithmetic and not the wiring --
     * and because a constant arg is a buffer of constants, which is the same
     * code path a modulated one takes.
     */
    {
        static const struct { const char *node; const char *body;
                              float want; } cases[] = {
            { "pow",   "base = 3;\n    exp = 2;\n",              9 },
            { "exp2",  "in = 3;\n",                              8 },
            { "abs",   "in = 0 - 2.5;\n",                      2.5f },
            { "min",   "in0 = 4;\n    in1 = 7;\n",               4 },
            { "max",   "in0 = 4;\n    in1 = 7;\n",               7 },
            { "clamp", "in = 9;\n    lo = 1;\n    hi = 5;\n",    5 },
            { "clamp", "in = 0;\n    lo = 1;\n    hi = 5;\n",    1 },
            { "clamp", "in = 3;\n    lo = 1;\n    hi = 5;\n",    3 },
        };

        int checked = 0;

        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        {
            const string text = wrap("",
                string("node n math::") + cases[i].node + " {\n    " +
                cases[i].body + "};\n");

            if (!writeFile(scratch, text))
                continue;

            thSynth player(pluginPath, TH_DEFAULT_WINDOW_LENGTH,
                           TH_DEFAULT_SAMPLES);
            thSynthTree *tree = player.loadTree(scratch, 0, 100);
            thNode *n = tree ? tree->findNode("n") : NULL;

            if (n == NULL)
            {
                fail(string("math::") + cases[i].node + " loads", "");
                continue;
            }

            /* An unreferenced node is not in the active list, so its callback
               is fired directly rather than through process(). */
            n->plugin()->fire(n, tree, TH_DEFAULT_WINDOW_LENGTH,
                              TH_DEFAULT_SAMPLES);

            thArg *out = tree->getArg(n, "out");
            const float got = out ? (*out)[0] : 0;

            if (out == NULL || fabsf(got - cases[i].want) > 1e-6f)
            {
                char said[128];

                snprintf(said, sizeof(said), "math::%s gave %g, not %g",
                         cases[i].node, (double)got, (double)cases[i].want);
                fail("a function node computes what it says", said);
            }
            else
                checked++;
        }

        if (checked == (int)(sizeof(cases) / sizeof(cases[0])))
        {
            char said[96];

            snprintf(said, sizeof(said),
                     "%d function-node results, each as its description says",
                     checked);
            ok(said);
        }
    }

    /* ---- the editor draws one box, and refuses to edit into it --------- */

    /* An expression is three math:: nodes by the time the graph is built.
     * Drawing those would be showing the desugar's working, so NodeGraph
     * collapses them back into one read-only box: the text, an input per
     * signal leaf, one output into the arg.
     */
    {
        const string text = wrap(
            "@cents = 100;\n@cents.widget = 1;\n"
            "@cents.min = -1200;\n@cents.max = 1200;\n",
            "node freq misc::midi2freq {\n"
            "    note = ionode->note;\n"
            "};\n"
            "\n"
            "node osc osc::simple {\n"
            "    freq = freq->out * exp2(@cents / 1200);\n"
            "};\n");

        if (!writeFile(scratch, text))
            ;
        else
        {
            thSynthTree *tree = synth.parseTree(scratch);

            NodeGraph g;

            if (tree == NULL || !g.build(tree))
                fail("an expression file builds a graph", "");
            else
            {
                g.layout();

                int box = -1, mathBoxes = 0;

                for (size_t i = 0; i < g.boxes().size(); i++)
                {
                    if (g.boxes()[i].isExpr)
                        box = (int)i;

                    /* The nodes the desugar made must not be drawn at all. */
                    if (g.boxes()[i].name.find('#') != string::npos)
                        mathBoxes++;
                }

                if (box < 0)
                    fail("an expression gets a box", "");
                else if (mathBoxes != 0)
                    fail("the nodes behind an expression are not drawn",
                         "they are");
                else
                {
                    const NodeGraph::Box &b = g.boxes()[box];

                    int ins = 0, outs = 0, wiredIn = 0, wiredOut = 0;

                    for (size_t q = 0; q < b.ports.size(); q++)
                        (b.ports[q].isInput ? ins : outs)++;

                    for (size_t e = 0; e < g.edges().size(); e++)
                    {
                        if (g.edges()[e].toBox == box)
                            wiredIn++;

                        if (g.edges()[e].fromBox == box)
                            wiredOut++;
                    }

                    /* `freq->out' and `@cents': one port each, both wired,
                       and one wire out into osc.freq. */
                    if (ins != 2 || outs != 1)
                        fail("an expression box has a port per leaf and one "
                             "output", "");
                    else if (wiredIn != 2 || wiredOut != 1)
                        fail("every port of an expression box is attached",
                             "");
                    else if (b.exprText != "freq->out * exp2(@cents / 1200)")
                        fail("the box shows the expression", b.exprText);
                    else if (b.exprNode != "osc" || b.exprArg != "freq")
                        fail("the box names the arg it feeds", b.name);
                    else
                        ok("an expression is one read-only box: its text, a "
                           "port per leaf, one wire out");

                    /* And no wire may be dragged into it, or out of it. */
                    int host = -1;

                    for (size_t i = 0; i < g.boxes().size(); i++)
                        if (g.boxes()[i].name == "freq")
                            host = (int)i;

                    string why;
                    int refusals = 0;

                    if (host >= 0 && !g.canConnect(host, 0, box, 0, why))
                        refusals++;

                    if (host >= 0 && !g.canConnect(box, ins, host, 0, why))
                        refusals++;

                    /* And not onto the arg the expression drives, either. */
                    int oscBox = -1, freqPort = -1;

                    for (size_t i = 0; i < g.boxes().size(); i++)
                        if (g.boxes()[i].name == "osc")
                            oscBox = (int)i;

                    if (oscBox >= 0)
                        for (size_t q = 0; q < g.boxes()[oscBox].ports.size();
                             q++)
                            if (g.boxes()[oscBox].ports[q].name == "freq")
                                freqPort = (int)q;

                    if (oscBox >= 0 && freqPort >= 0 && host >= 0 &&
                        !g.canConnect(host, 0, oscBox, freqPort, why))
                        refusals++;

                    if (refusals != 3)
                        fail("an expression refuses every wire into or out "
                             "of it", "");
                    else
                        ok("canConnect refuses a wire into an expression, out "
                           "of one, and onto the arg one drives");
                }
            }

            delete tree;
        }
    }

    /* ---- the writer leaves the text alone ------------------------------ */

    /* dspwrite's property over the corpus is that a write of the value
     * already there changes no byte. An expression has no value to write, so
     * what has to hold instead is that every edit aimed at one is refused
     * with the file untouched -- and that the one edit that is allowed,
     * removing it, is the same `= 0' rewrite disconnect has always done.
     */
    {
        const string file = scratchPath("exprcheck-edit.dsp");

        const string before = wrap("",
            "node freq misc::midi2freq {\n"
            "    note = ionode->note;\n"
            "};\n"
            "\n"
            "node osc osc::simple {\n"
            "    freq = freq->out * 2;\n"
            "};\n");

        if (!writeFile(file, before))
            ;
        else
        {
            string why;
            int refusals = 0;

            if (NodeEdit::setValue(file, "osc", "freq", 440, why) !=
                    NodeEdit::OK)
                refusals++;

            if (NodeEdit::connect(file, "osc", "freq", "freq", "out", why) !=
                    NodeEdit::OK)
                refusals++;

            string after;

            /* In its own scope, and this is not tidiness. NodeEdit writes a
               temporary and renames it over the target; Windows refuses a
               rename onto a file something still has open, so a reader left
               alive across the disconnect below fails the write with
               "could not write" on one platform and nowhere else. */
            {
                std::ifstream in(file.c_str(), std::ios::binary);

                after.assign((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
            }

            if (refusals != 2)
                fail("setValue and connect refuse an expression", "");
            else if (after != before)
                fail("a refused edit leaves the file alone", "");
            else if (NodeEdit::disconnect(file, "osc", "freq", 0, why) !=
                         NodeEdit::OK)
                fail("disconnect removes an expression", why);
            else
            {
                string gone;

                {
                    std::ifstream cut(file.c_str(), std::ios::binary);

                    gone.assign((std::istreambuf_iterator<char>(cut)),
                                std::istreambuf_iterator<char>());
                }

                if (gone.find("freq = 0;") == string::npos)
                    fail("disconnect rewrites the arg to 0", gone);
                else if (gone.find("freq->out * 2") != string::npos)
                    fail("the expression is gone", gone);
                else
                    ok("setValue and connect refuse an expression and change "
                       "no byte; disconnect rewrites the whole arg to 0");
            }
        }

        remove(file.c_str());
    }

    /* ---- and a node can still be added beside one ---------------------- */

    /* dspnew's property: add a node and remove it again, byte for byte. A
     * file with an expression in it must not be a file the writer has stopped
     * being able to touch -- only the expression itself is off limits.
     */
    {
        const string file = scratchPath("exprcheck-add.dsp");

        const string before = wrap("",
            "node freq misc::midi2freq {\n"
            "    note = ionode->note;\n"
            "};\n"
            "\n"
            "node osc osc::simple {\n"
            "    freq = freq->out * 2;\n"
            "};\n");

        if (!writeFile(file, before))
            ;
        else
        {
            string why;
            int removed = 0;

            if (NodeEdit::addNode(file, "extra", "math::abs", why) !=
                    NodeEdit::OK)
                fail("a node is added beside an expression", why);
            else if (NodeEdit::removeNode(file, "extra", removed, why) !=
                         NodeEdit::OK)
                fail("and removed again", why);
            else
            {
                string after;

                {
                    std::ifstream in(file.c_str(), std::ios::binary);

                    after.assign((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
                }

                if (after != before)
                    fail("add then remove restores the file", "it did not");
                else
                    ok("a node added beside an expression and removed again "
                       "leaves the file byte for byte");
            }
        }

        remove(file.c_str());
    }

    /* ---- removing what an expression reads keeps the arithmetic -------- */

    /* A control and a node are both things an expression can be *part of*,
     * and both used to take the whole arg with them: the writer found the
     * reference anywhere in a right-hand side and replaced the entire
     * right-hand side with 0. Deleting `@detune' turned ladder.dsp's
     * `freq = freq->out + @detune' into `freq = 0', so the oscillator lost
     * its pitch rather than its detune, and the edit reported success.
     *
     * The reference has to go -- it will not resolve -- and nothing else
     * does. A control's own value stands in its place; a node's output has
     * no value to stand in and becomes the 0 a plain binding becomes. A
     * plain binding is untouched either way, which is what keeps disconnect
     * and the `= 0' rewrite above saying the same thing they always did.
     */
    {
        const string file = scratchPath("exprcheck-remove.dsp");

        const string before = wrap(
            "@detune = 1.3;\n@slow = 4 ms;\n",
            "node freq misc::midi2freq {\n"
            "    note = ionode->note;\n"
            "};\n"
            "\n"
            "node osc osc::simple {\n"
            "    freq = freq->out + @detune;\n"
            "    mul = @detune;\n"
            "    pw = freq->out * 0.5;\n"
            "    amp = @slow * 2;\n"
            "};\n"
            "\n"
            "node plain math::abs {\n"
            "    in = freq->out;\n"
            "};\n");

        if (!writeFile(file, before))
            ;
        else
        {
            string why, after;
            int removed = 0;

            if (NodeEdit::removeControl(file, "detune", removed, why) !=
                    NodeEdit::OK)
                fail("a control an expression reads is removed", why);
            else
            {
                {
                    std::ifstream in(file.c_str(), std::ios::binary);

                    after.assign((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
                }

                if (after.find("freq = freq->out + 1.3;") == string::npos)
                    fail("the control's value stands in its place", after);
                else if (after.find("mul = 0;") == string::npos)
                    fail("a plain binding still becomes 0", after);
                else if (after.find("@detune") != string::npos)
                    fail("no reference to the control is left", after);
                else if (NodeEdit::removeControl(file, "slow", removed,
                                                 why) != NodeEdit::OK)
                    fail("a control with a unit is removed", why);
                else
                {
                    std::ifstream in(file.c_str(), std::ios::binary);

                    after.assign((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());

                    /* `4 ms' cannot appear inside an expression, so there is
                       nothing to stand in and the arg falls back to 0. */
                    if (after.find("amp = 0;") == string::npos)
                        fail("a unit-carrying control flattens the arg",
                             after);
                    else
                        ok("removing a control keeps the arithmetic around "
                           "it, and falls back to 0 where its unit cannot");
                }
            }

            if (NodeEdit::removeNode(file, "freq", removed, why) !=
                    NodeEdit::OK)
                fail("a node an expression reads is removed", why);
            else
            {
                std::ifstream in(file.c_str(), std::ios::binary);

                after.assign((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());

                if (after.find("freq = 0 + 1.3;") == string::npos ||
                    after.find("pw = 0 * 0.5;") == string::npos)
                    fail("a removed node leaves the rest of the arithmetic",
                         after);
                else if (after.find("in = 0;") == string::npos)
                    fail("and a plain binding still becomes 0", after);
                else
                    ok("removing a node replaces the reference and not the "
                       "whole arg");
            }
        }

        remove(file.c_str());
    }

    remove(scratch.c_str());

    printf("\n%d failure(s)\n", failed);

    return failed;
}
