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
 * panelcheck -- what a parameter panel says, before anybody draws it.
 *
 * src/PanelModel.h describes a panel as plain data and src/ArgPanel.cpp
 * builds one from a channel; between them they hold every rule about what a
 * parameter is that used to live inside a widget-building function. A rule
 * inside a widget can only be checked by pressing the widget, which needs a
 * display -- and it was checked on one platform and re-implemented, badly, on
 * the other. The rules are ordinary functions over ordinary data now, so this
 * runs everywhere gencheck does and the browser's copy of them is the same
 * copy.
 *
 * What is checked here is the description: units unfolded at the synth's
 * rate, decimals that suit the range, a selector's holes, a group of one
 * dissolved, a declared group beating an inferred one, and what a typed value
 * is allowed to become. Then a piece's knobs, which are the other provider
 * and the one that shows what describing an edit and delivering it are two
 * things for. What is checked in editorcheck is that a PanelView over the
 * same description draws and moves real widgets.
 *
 * Builds its own .dsp files: the cases that matter are ones the corpus cannot
 * contain, and one of them -- a selector with a hole in it -- is the case the
 * shipped files are careful not to have.
 *
 *     scripts/panelcheck -p build/plugins/
 *
 * Exit status is the number of failures.
 *
 * `-j' prints the channel's panel instead of checking anything: the rows as
 * tw_panel_json would hand them to a page. That is the reference
 * wasm/web/panelcheck.mjs holds the module's own dump against, which is the
 * gate that keeps one description from becoming two again -- one model, two
 * builds, byte-identical rows, or the build fails. It writes the fixture
 * .dsp beside the dump, so the wasm side loads exactly the same text.
 *
 * The instrument alone, and not the effect panel checked below it: a
 * channel effect reaches the browser through a piece's `effect' clause and
 * there is no way to put one on a bare channel there, so there is nothing
 * for the other side of the comparison to load. Every rule the model has is
 * in the instrument's rows anyway; what the effect panel is for is the
 * prefix, and that is a lookup rather than a description.
 *
 *     scripts/panelcheck -p build/plugins/ -j /tmp/panel
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <filesystem>
#include <fstream>

#include "think.h"

#include "ArgPanel.h"
#include "KnobPanel.h"
#include "NodeGraph.h"
#include "NodePanel.h"

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

static void check (bool good, const string &what, const string &detail = "")
{
    if (good)
        ok(what);
    else
        fail(what, detail);
}

/* Not "/tmp/...", for the reason argtype.cpp gives at length: this is a gate,
   so it runs on the Windows runner, where there is no such directory. */
static string scratchPath (const char *leaf)
{
    std::error_code ec;

    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);

    if (ec)
        dir = ".";      /* the build tree; ctest runs us in it */

    return (dir / leaf).string();
}

static bool writeFile (const string &path, const string &text)
{
    ofstream out(path.c_str(), ios::binary | ios::trunc);

    out << text;
    out.close();

    if (out.good())
        return true;

    fail("could not write the scratch file", path);

    return false;
}

/* The instrument.
 *
 * Every control here is a case. `decay' is written in milliseconds and stored
 * in samples; `cutoff' runs 0..1 and needs four decimals where `decay' needs
 * none; `wave' names three of four values and leaves a hole at 1; `quant'
 * says it means a whole number; `quiet' says not to draw it at all.
 *
 * The wiring is the other half. `decay', `cutoff' and `wave' all drive the
 * one oscillator, which is a group nobody wrote down; `fa' and `fb' drive
 * different nodes and declare the same group, which is the author overruling
 * that inference; `quant' and `lonely' would each be a group of one; and
 * `both' is read by two nodes, so it belongs to neither. */
static const char *INSTRUMENT =
    "name \"panelcheck\";\n"
    "\n"
    "@decay = 500 ms;\n"
    "@decay.widget = 1;\n"
    "@decay.min = 0;\n"
    "@decay.max = 2000ms;\n"
    "@decay.label = \"Decay\";\n"
    "\n"
    "@cutoff = 0.25;\n"
    "@cutoff.widget = 1;\n"
    "@cutoff.min = 0;\n"
    "@cutoff.max = 1;\n"
    "\n"
    "@wave = 2;\n"
    "@wave.widget = 1;\n"
    "@wave.values = \"Sine,,Square,Triangle\";\n"
    "\n"
    "@quant = 3;\n"
    "@quant.widget = 1;\n"
    "@quant.min = 0;\n"
    "@quant.max = 12;\n"
    "@quant.step = 1;\n"
    "\n"
    "@quiet = 0.5;\n"
    "@quiet.widget = 0;\n"
    "\n"
    "@fa = 0.5;\n"
    "@fa.widget = 1;\n"
    "@fa.min = 0;\n"
    "@fa.max = 1;\n"
    "@fa.group = \"Filter\";\n"
    "\n"
    "@fb = 0.5;\n"
    "@fb.widget = 1;\n"
    "@fb.min = 0;\n"
    "@fb.max = 1;\n"
    "@fb.group = \"Filter\";\n"
    "\n"
    "@lonely = 0.5;\n"
    "@lonely.widget = 1;\n"
    "@lonely.min = 0;\n"
    "@lonely.max = 1;\n"
    "@lonely.group = \"Alone\";\n"
    "\n"
    "@both = 0.5;\n"
    "@both.widget = 1;\n"
    "@both.min = 0;\n"
    "@both.max = 1;\n"
    "\n"
    "node ionode {\n"
    "    out0 = b2->out;\n"
    "    out1 = b2->out;\n"
    "    channels = 2;\n"
    "    play = 1;\n"
    "};\n"
    "\n"
    "node osc osc::simple {\n"
    "    freq = ionode->note;\n"
    "    mul = @decay;\n"
    "    pw = @cutoff;\n"
    "    waveform = @wave;\n"
    "};\n"
    "\n"
    "node f1 math::mul { in0 = osc->out; in1 = @fa; };\n"
    "node f2 math::mul { in0 = f1->out;  in1 = @fb; };\n"
    "node q  math::mul { in0 = f2->out;  in1 = @quant; };\n"
    "node l  math::mul { in0 = q->out;   in1 = @lonely; };\n"
    "node b1 math::mul { in0 = l->out;   in1 = @both; };\n"
    "node b2 math::mul { in0 = b1->out;  in1 = @both; };\n"
    "\n"
    "io ionode;\n";

/* The channel effect: a second arg map on the same channel, reached through
   the same lookup under `fx.'. Two controls, so the panel over it is not
   empty, and neither of them shares a name with the instrument's. */
static const char *EFFECT =
    "name \"panelfx\";\n"
    "\n"
    "@wet = 0.5;\n"
    "@wet.widget = 1;\n"
    "@wet.min = 0;\n"
    "@wet.max = 1;\n"
    "@wet.label = \"Wet\";\n"
    "\n"
    "@trim = 0.8;\n"
    "@trim.widget = 1;\n"
    "@trim.min = 0;\n"
    "@trim.max = 2;\n"
    "\n"
    "node ionode {\n"
    "    channels = 2;\n"
    "    in0 = 0;\n"
    "    in1 = 0;\n"
    "    out0 = wl->out;\n"
    "    out1 = wr->out;\n"
    "};\n"
    "\n"
    "node wl math::mul { in0 = ionode->in0; in1 = @wet; };\n"
    "node wr math::mul { in0 = ionode->in1; in1 = @trim; };\n"
    "\n"
    "io ionode;\n";

/* The row ids of a panel, in order, as "a,b,c" -- which is the whole of what
   this has to say about ordering, and reads in a failure line. */
static string idsOf (const thPanel &panel)
{
    string s;

    for (size_t i = 0; i < panel.rows.size(); i++)
        s += (i ? "," : "") + panel.rows[i].id;

    return s;
}

static string groupsOf (const thPanel &panel)
{
    string s;

    for (size_t i = 0; i < panel.groupOrder.size(); i++)
        s += (i ? "," : "") + panel.groupOrder[i];

    return s;
}

static string groupOf (const thPanel &panel, const string &id)
{
    const thPanelRow *row = panel.find(id);

    return row ? row->group : string("(no such row)");
}

/* ---- the rules, on their own ---------------------------------------- */

static void checkArithmetic (void)
{
    check(thPanelDecimals(882000) == 0 && thPanelDecimals(2000) == 0,
          "a range in the thousands shows no decimals");
    check(thPanelDecimals(120) == 1, "a range in the hundreds shows one");
    check(thPanelDecimals(16) == 2, "a range in the tens shows two");
    check(thPanelDecimals(1) == 4 && thPanelDecimals(0.5) == 4,
          "a range of 0..1 shows four, which is its whole resolution");

    /* Relative, not absolute: the point of the whole rule. */
    check(thPanelDecimals(-2000) == 0,
          "the sign of the range does not change its resolution");

    /* `288000.0312' is eleven characters, and it was cut off in a box sized
       for nine. Six is the floor, so a narrow range still gets a usable
       box. */
    check(thPanelValueChars(1, 4) == 7,
          "0..1 at four decimals wants seven characters",
          to_string(thPanelValueChars(1, 4)));
    check(thPanelValueChars(882000, 0) == 7,
          "0..882000 whole wants seven",
          to_string(thPanelValueChars(882000, 0)));
    check(thPanelValueChars(12, 0) == 6,
          "a short range still gets six",
          to_string(thPanelValueChars(12, 0)));

    /* A range with no finite end.
     *
       This one is a hang and not a wrong answer: the digits were counted by
       dividing by ten until the number fell under it, and inf / 10 is inf.
       `@x.max = inf' is a number a .dsp may write, and so is one big enough
       to overflow the float it is read into -- so the way this used to fail
       was the editor never coming back from opening a patch. Reaching the
       check below at all is most of what it is for. */
    check(thPanelValueChars(INFINITY, 4) == 6,
          "an endless range gets the floor rather than a loop that never "
          "ends", to_string(thPanelValueChars(INFINITY, 4)));
    check(thPanelValueChars(-INFINITY, 0) == 6,
          "and so does one endless the other way",
          to_string(thPanelValueChars(-INFINITY, 0)));
    check(thPanelValueChars(NAN, 2) == 6, "and one that is not a number",
          to_string(thPanelValueChars(NAN, 2)));
    check(thPanelDecimals(INFINITY) == 0,
          "an endless range is spelled whole",
          to_string(thPanelDecimals(INFINITY)));

    check(thPanelSpell(0.25, 4) == "0.2500" && thPanelSpell(500, 0) == "500",
          "a number is spelled at the row's resolution",
          thPanelSpell(0.25, 4) + " / " + thPanelSpell(500, 0));

    /* Every rounding of a small negative produces one, and it is a value
       nothing means. */
    check(thPanelSpell(-0.00001, 4) == "0.0000",
          "a rounded-away negative keeps its places and loses its sign",
          thPanelSpell(-0.00001, 4));

    vector<pair<string, int> > choices;

    choices.push_back(make_pair(string("Sine"), 0));
    choices.push_back(make_pair(string("Square"), 2));
    choices.push_back(make_pair(string("Triangle"), 3));

    check(thPanelChoiceIndex(choices, 3) == 2,
          "a named value selects its own row");
    check(thPanelChoiceIndex(choices, 3.9) == 2,
          "a value is truncated, the way `switch ((int)x)' truncates it");
    check(thPanelChoiceIndex(choices, 1) == -1,
          "a value the plugin does not implement selects nothing");
}

/* The readout the page reads.
 *
 * Hand-built rather than taken off a channel, because what is being checked
 * is the writer and the cases that break one are characters no .dsp in the
 * corpus has in a label. The panel over a real channel is checked by dumping
 * it -- `panelcheck -j' -- and diffing that against the same dump out of the
 * wasm module, which is the gate that keeps the two builds saying the same
 * thing. */
static void checkJson (void)
{
    thPanel panel;
    thPanelBuilder build;

    thPanelRow row;

    row.kind = thPanelRow::SLIDER;
    row.id = "cut\\off";
    row.label = "He said \"stop\"";
    row.desc = "one\ttwo\nthree";
    row.units = "ms";
    row.value = 0.1;
    row.text = "0.1000";
    row.lo = 0;
    row.hi = 1;
    row.step = 0.0001;
    row.decimals = 4;
    row.valueChars = 7;
    row.editable = false;
    row.choices.push_back(make_pair(string("Sine"), 0));

    build.add(row, "", "");
    build.finish(panel);

    const string json = thPanelToJson(panel);

    check(json.find("\"id\":\"cut\\\\off\"") != string::npos,
          "a backslash in a row id is escaped", json);
    check(json.find("\"label\":\"He said \\\"stop\\\"\"") != string::npos,
          "and so are the quotes in a label", json);
    check(json.find("\"desc\":\"one\\ttwo\\nthree\"") != string::npos,
          "and the control characters in a description", json);
    check(json.find("\"choices\":[{\"name\":\"Sine\",\"value\":0}]") !=
          string::npos,
          "a choice is a name and the number it means", json);
    check(json.find("\"editable\":false") != string::npos,
          "and a row that is only shown says so", json);

    /* Seventeen significant figures, so a double survives the round trip
       through text exactly -- which is what a diff between a native dump
       and a wasm one is asking about. %g's six would hide the last bit. */
    check(json.find("\"value\":0.10000000000000001") != string::npos,
          "a value is written to the precision that round-trips", json);
}

/* ---- a piece's knobs ------------------------------------------------ */

/* Built by hand rather than loaded from a .gen.
 *
 * A knob is a thArg with a range and a label on it, and what KnobPanel says
 * about one depends on nothing else -- so a fixture here is four thArgs and
 * no composer host, no scheduler and no piece. The case that matters is the
 * one no shipped piece has: a hidden knob in the middle of the list, which
 * keeps its number and gets no row.
 */
static thArg *makeKnob (const char *name, float value, float lo, float hi,
                        const char *label, bool shown)
{
    thArg *knob = new thArg(string(name), value);

    knob->setMin(lo);
    knob->setMax(hi);
    knob->setLabel(label);
    knob->setWidgetType(shown ? thArg::SLIDER : thArg::HIDE);

    return knob;
}

static void checkKnobs (void)
{
    std::vector<thArg *> knobs;

    knobs.push_back(makeKnob("density", 0.85f, 0, 1, "Density", true));
    knobs.push_back(makeKnob("seedy", 3, 0, 9, "", false));
    knobs.push_back(makeKnob("warmth", 0.5f, 0, 1, "", true));
    knobs.push_back(makeKnob("dwell", 7000, 0, 20000, "Dwell", true));

    KnobPanel panel;

    panel.setKnobs(knobs);

    thPanel built;

    check(panel.build(built), "a piece's knobs make a panel");
    check(built.kind == thPanel::KNOB, "of their own kind");

    /* A row id is the number a command names the knob by, so the hidden
       one's number is missing rather than closed over. A list that
       renumbered them would send a command to the wrong knob. */
    check(idsOf(built) == "0,2,3",
          "a hidden knob keeps its number and gets no row", idsOf(built));

    const thPanelRow *density = built.find("0");

    if (density == NULL)
        fail("the first knob has a row", idsOf(built));
    else
    {
        check(density->label == "Density", "the label is the piece's",
              density->label);
        check(density->knob == "density" && density->desc == "@density",
              "and the row carries the name the .gen writes",
              density->knob + " / " + density->desc);
        check(density->kind == thPanelRow::SLIDER,
              "a knob is a slider: a number a hand moves while it plays");

        /* toPrecision(3) on a 0..1 knob is three digits of the four its
           range is worth. The range says four. */
        check(density->decimals == 4 && density->text == "0.8500",
              "and its resolution follows its range",
              to_string(density->decimals) + " / " + density->text);
    }

    const thPanelRow *warmth = built.find("2");

    check(warmth && warmth->label == "warmth",
          "a knob with no label is called what the piece calls it",
          warmth ? warmth->label : string("(no row)"));

    /* And the other end of the same rule: a knob running to twenty
       thousand has no decimals worth showing. */
    const thPanelRow *dwell = built.find("3");

    check(dwell && dwell->decimals == 0 && dwell->text == "7000",
          "a knob with a wide range shows none",
          dwell ? dwell->text : string("(no row)"));

    thPanelEdit edit;
    thPanelResult r = panel.propose("3", "12000", edit);

    check(r.ok && r.changed && edit.value == 12000 &&
          edit.kind == thPanelEdit::KNOB,
          "a typed number becomes a knob intent", r.why);

    /* The index a command names it by, which is the whole reason a knob
       row's id is a number: the intent has to survive being broadcast to a
       peer with no panel open. */
    check(edit.a == 3, "carrying the number the command names it by",
          to_string(edit.a));

    check((double)(*knobs[3])[0] == 7000,
          "proposing does not write", to_string((double)(*knobs[3])[0]));

    check(panel.deliver(edit) && (double)(*knobs[3])[0] == 12000,
          "delivering does -- which is what the desktop does with one, and "
          "what a stamped command does with one in the browser",
          to_string((double)(*knobs[3])[0]));

    r = panel.propose("3", "12000", edit);

    check(r.ok && !r.changed,
          "an intent equal to what is there moves nothing", r.why);

    r = panel.propose("3", "99999", edit);

    check(r.ok && edit.value == 20000,
          "a number past the end of the range is held to it",
          to_string(edit.value));

    r = panel.propose("3", "loud", edit);

    check(!r.ok, "a value that is not a number is refused", r.why);

    r = panel.propose("dwell", "1", edit);

    check(!r.ok, "and so is a row named by anything but its number", r.why);

    r = panel.propose("9", "1", edit);

    check(!r.ok, "or by a number no knob has", r.why);

    /* An empty value box, and a box holding nothing but blanks.
     *
     * Both read as a perfectly good 0 through a strtod that is asked whether
     * it read any digits only after the trailing blanks have been skipped --
     * and 0 is not merely wrong, it is outside the declared range of plenty
     * of a piece's knobs. The model's own parse is the one every provider
     * uses now, and it asks first. */
    r = panel.propose("3", "", edit);

    check(!r.ok, "an empty box is not a knob set to zero", r.why);

    r = panel.propose("3", "   ", edit);

    check(!r.ok, "and neither is one holding blanks", r.why);

    check((double)(*knobs[3])[0] == 12000,
          "and the knob stayed where it was",
          to_string((double)(*knobs[3])[0]));

    /* A knob's resolution binds what it can be left holding, the way a
       chanarg's does: the slider running to 20000 shows no decimals, so a
       number between two of them is a number no control can show or hand
       back. */
    r = panel.propose("3", "12345.678", edit);

    check(r.ok && edit.value == 12346,
          "a typed number is held to the knob's resolution",
          to_string(edit.value));

    r = panel.propose("0", "0.30", edit);

    check(r.ok && r.changed && panel.deliver(edit),
          "a value no float holds exactly is delivered", r.why);

    r = panel.propose("0", "0.3000", edit);

    check(r.ok && !r.changed,
          "and proposing the spelling the row now shows is an echo", r.why);

    for (size_t i = 0; i < knobs.size(); i++)
        delete knobs[i];
}

/* ---- a node's parameters --------------------------------------------- */

/* A graph with one of each kind of parameter in it.
 *
 * What NodePanel has to get right is which of them a person may type into
 * and what the others say instead of a number, and the corpus has no single
 * node carrying all five: a plain value, a value the plugin writes, one
 * driven by a wire, one driven by a control, and one whose values the plugin
 * named. */
static const char *GRAPH =
    "name \"panelnode\";\n"
    "\n"
    "@cutoff = 0.25;\n"
    "@cutoff.widget = 1;\n"
    "@cutoff.min = 0;\n"
    "@cutoff.max = 1;\n"
    "\n"
    "node ionode {\n"
    "    out0 = osc->out;\n"
    "    out1 = osc->out;\n"
    "    channels = 2;\n"
    "    play = 1;\n"
    "};\n"
    "\n"
    "node lfo osc::simple {\n"
    "    freq = 3;\n"
    "};\n"
    "\n"
    "node osc osc::simple {\n"
    "    freq = ionode->note;\n"
    "    waveform = 2;\n"
    "    pw = @cutoff;\n"
    "    fm = lfo->out;\n"
    "    mul = 1.5;\n"
    "};\n"
    "\n"
    "io ionode;\n";

static void checkNodes (const string &pluginPath, const string &file)
{
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    thSynthTree *tree = synth.parseTree(file);

    if (tree == NULL)
    {
        fail("the node fixture parses", file);
        return;
    }

    NodeGraph graph;

    if (!graph.build(tree))
    {
        fail("it builds a graph", "");

        delete tree;

        return;
    }

    int box = -1;

    for (size_t i = 0; i < graph.boxes().size(); i++)
        if (graph.boxes()[i].name == "osc")
            box = (int)i;

    if (box < 0)
    {
        fail("the graph has the osc node in it", "");

        delete tree;

        return;
    }

    NodePanel panel;
    thPanel built;

    panel.setBox(&graph, box);

    check(panel.build(built), "a node's parameters make a panel");
    check(built.kind == thPanel::NODE_VALUE, "of their own kind");

    /* A panel over one box of thirty has to say which, and the plugin's
       spelling is how a reader knows what the rows mean. */
    check(built.title == "osc" && built.subtitle == "osc::simple",
          "named for the node and what it is",
          built.title + " / " + built.subtitle);

    const thPanelRow *mul = built.find("mul");

    if (mul == NULL)
        fail("a plain value has a row", idsOf(built));
    else
    {
        check(mul->kind == thPanelRow::NUMBER && mul->editable,
              "a plain value is a number box, and offered");

        /* A number box and no slider. A node arg's min and max are not a
           control's travel and most args have none, so a slider would be a
           handle sweeping a range nobody declared. */
        check(mul->text == "1.5",
              "spelled the way the file spells it, not the panel's way",
              mul->text);
    }

    const thPanelRow *waveform = built.find("waveform");

    if (waveform == NULL)
        fail("a named-value param has a row", idsOf(built));
    else
    {
        /* The plugin named six waveforms, so this is a list. The panel it
           replaced showed the names in a tooltip beside a spin button and
           said why: one row of a grid of spin buttons behaving differently
           was the worse trade. A panel row is drawn by its kind, so the
           trade has gone. */
        check(waveform->kind == thPanelRow::CHOICE,
              "a param whose values the plugin named is a list");
        check(waveform->choices.size() == 6 && waveform->text == "Square",
              "with the plugin's own names on it",
              to_string(waveform->choices.size()) + " / " + waveform->text);

        /* Eight shipped patches say `.max = 5.1' for six waveforms --
           padding for a slider that could not otherwise reach the last
           one -- and honouring that would offer a seventh position that
           does nothing. */
        check(waveform->lo == 0 && waveform->hi == 5,
              "and its travel is the list", to_string(waveform->hi));
    }

    const thPanelRow *out = built.find("out");

    check(out && out->kind == thPanelRow::READONLY && !out->editable,
          "an output is shown and not offered: the plugin writes it every "
          "window");

    const thPanelRow *fm = built.find("fm");

    check(fm && fm->kind == thPanelRow::READONLY && !fm->editable &&
          fm->text == "lfo->out",
          "a wired param says where its value comes from",
          fm ? fm->text : string("(no row)"));

    const thPanelRow *pw = built.find("pw");

    /* The control's name bare in `knob' -- what a lookup would use -- and
       the spelling the file wrote in the text beside it. */
    check(pw && pw->kind == thPanelRow::READONLY && !pw->editable &&
          pw->knob == "cutoff" && pw->text == "@cutoff = 0.25",
          "and so does one a control drives",
          pw ? (pw->knob + " / " + pw->text) : string("(no row)"));

    /* The tooltip precedence: the plugin's own description of the arg,
       where the file said nothing about it. */
    check(mul && mul->desc.find("Multiply") != string::npos,
          "a row carries what the plugin says the arg is for",
          mul ? mul->desc : string("(no row)"));

    thPanelEdit edit;
    thPanelResult r = panel.propose("mul", "2.25", edit);

    check(r.ok && r.changed && edit.value == 2.25 &&
          edit.kind == thPanelEdit::NODE_VALUE && edit.a == box,
          "a typed number becomes a node intent, naming the box", r.why);

    r = panel.propose("waveform", "Triangle", edit);

    check(r.ok && edit.value == 3, "a list takes the value's name",
          to_string(edit.value));

    r = panel.propose("mul", "1.5", edit);

    check(r.ok && !r.changed,
          "an intent equal to what the file holds splices nothing", r.why);

    /* A shown row is not an offered one, and a panel drawn before the graph
       was rewired may still have one. */
    r = panel.propose("fm", "1", edit);

    check(!r.ok, "a wired param refuses an edit: the thing to change is the "
                 "wire", r.why);

    r = panel.propose("out", "1", edit);

    check(!r.ok, "and so does an output", r.why);

    r = panel.propose("nosucharg", "1", edit);

    check(!r.ok, "an edit naming an arg the node has not got is refused",
          r.why);

    /* Which boxes have anything to set, asked once. The page's node view
       and the harnesses that click on one both want this, and three
       answers to it is two too many. */
    check(NodePanel::settable(&graph, box),
          "the osc node has something to set");

    /* A control is a knob on the canvas and not a node with args on it:
       nothing in its box is a number this panel could offer. */
    int control = -1;

    for (size_t i = 0; i < graph.boxes().size(); i++)
        if (graph.boxes()[i].isControl)
            control = (int)i;

    check(control >= 0 && !NodePanel::settable(&graph, control),
          "and a control box has not");

    check(!NodePanel::settable(&graph, 9999),
          "nor has a box that is not there");

    delete tree;
}

/* ---- the instrument's panel ----------------------------------------- */

static void checkRows (const thPanel &panel)
{
    const thPanelRow *decay = panel.find("decay");

    if (decay == NULL)
        fail("the panel has a row for `decay'", idsOf(panel));
    else
    {
        check(decay->kind == thPanelRow::SLIDER,
              "a plain number is a slider");
        check(decay->label == "Decay", "the label is the one the file gave",
              decay->label);
        check(decay->units == "ms", "and it remembers the unit",
              decay->units);

        /* Stored as samples, shown as what the author wrote. The page has
           never done this at all. */
        check(fabs(decay->value - 500) < 1e-6,
              "a duration reads in the unit it was written in",
              to_string(decay->value));
        check(fabs(decay->lo) < 1e-6 && fabs(decay->hi - 2000) < 1e-6,
              "and so does its range",
              to_string(decay->lo) + ".." + to_string(decay->hi));
        check(decay->decimals == 0 && decay->valueChars == 6,
              "a range to 2000 gets no decimals and a six-character box",
              to_string(decay->decimals) + " / " +
              to_string(decay->valueChars));
        check(decay->text == "500", "and its spelling is the value",
              decay->text);
    }

    const thPanelRow *cutoff = panel.find("cutoff");

    if (cutoff == NULL)
        fail("the panel has a row for `cutoff'", idsOf(panel));
    else
    {
        check(cutoff->label == "cutoff",
              "a control with no label is called what the file calls it",
              cutoff->label);
        check(cutoff->decimals == 4 && cutoff->valueChars == 7,
              "a 0..1 control gets four decimals and room for them",
              to_string(cutoff->decimals) + " / " +
              to_string(cutoff->valueChars));
        check(cutoff->text == "0.2500", "spelled at that resolution",
              cutoff->text);
        check(cutoff->step < 1, "and it is not a whole number",
              to_string(cutoff->step));
    }

    const thPanelRow *quant = panel.find("quant");

    if (quant == NULL)
        fail("the panel has a row for `quant'", idsOf(panel));
    else
        check(quant->step == 1 && quant->decimals == 0,
              "a control that means a whole number steps by one and shows "
              "no decimals",
              to_string(quant->step) + " / " + to_string(quant->decimals));

    const thPanelRow *wave = panel.find("wave");

    if (wave == NULL)
        fail("the panel has a row for `wave'", idsOf(panel));
    else
    {
        check(wave->kind == thPanelRow::CHOICE,
              "a control whose values are named is a list, not a scale");

        /* Three, not four: the file named 0, 2 and 3 and left 1 a hole. A
           row for a value the plugin does not implement is worse than no
           row. */
        check(wave->choices.size() == 3,
              "only the named values get rows",
              to_string(wave->choices.size()));

        if (wave->choices.size() == 3)
            check(wave->choices[1].first == "Square" &&
                  wave->choices[1].second == 2,
                  "and the row list is not the identity",
                  wave->choices[1].first + " -> " +
                  to_string(wave->choices[1].second));

        check(wave->text == "Square",
              "a selector's spelling is the value's name", wave->text);

        /* The file gave it no range at all. A list of names implies one --
           first named entry to last -- which is what stops a selector
           inheriting the four decimals a range of 0..0 would ask for. */
        check(wave->lo == 0 && wave->hi == 3 && wave->step == 1 &&
              wave->decimals == 0,
              "and its travel is the list rather than whatever .min and "
              ".max say",
              to_string(wave->lo) + ".." + to_string(wave->hi) + " by " +
              to_string(wave->step));

        /* The plugin names six waveforms and would have said so through
           typeChanArgs; the file said otherwise and the file wins. */
        check(wave->value == 2, "and its value is the number, unconverted",
              to_string(wave->value));
    }

    check(panel.find("quiet") == NULL,
          "a control the file says not to draw is not drawn");
}

static void checkGroups (const thPanel &panel)
{
    /* `decay', `cutoff' and `wave' all drive the oscillator, and nobody
       wrote that down. The node editor draws them stacked on the node they
       feed; so does the panel. */
    check(groupOf(panel, "decay") == "osc" &&
          groupOf(panel, "cutoff") == "osc" &&
          groupOf(panel, "wave") == "osc",
          "controls driving one node are grouped by it",
          groupOf(panel, "decay") + "/" + groupOf(panel, "cutoff") + "/" +
          groupOf(panel, "wave"));

    /* Declared beats inferred: `fa' and `fb' drive different nodes, so the
       inference would have split them, and the author said they belong
       together. */
    check(groupOf(panel, "fa") == "Filter" && groupOf(panel, "fb") == "Filter",
          "a declared group beats the one the graph implies",
          groupOf(panel, "fa") + "/" + groupOf(panel, "fb"));

    /* A group of one is not a group -- whether it was inferred (`quant'
       drives one node of its own) or declared (`lonely' says "Alone" and is
       the only member). */
    check(groupOf(panel, "quant").empty(),
          "an inferred group of one is dissolved", groupOf(panel, "quant"));
    check(groupOf(panel, "lonely").empty(),
          "a declared group of one is dissolved too",
          groupOf(panel, "lonely"));

    /* Read by two nodes, so it belongs to neither: it is a patch-wide
       control. */
    check(groupOf(panel, "both").empty(),
          "a control several nodes read belongs to none of them",
          groupOf(panel, "both"));

    check(groupsOf(panel) == "osc,Filter",
          "the groups are listed in the order they were first seen",
          groupsOf(panel));

    /* Loose rows first, then each group's rows together, and a dissolved
       group's row keeps its place among the loose ones rather than being
       swept to the end. */
    check(idsOf(panel) == "amp,both,lonely,quant,cutoff,decay,wave,fa,fb",
          "the loose rows come first and each group's rows are contiguous",
          idsOf(panel));
}

static void checkEdits (ArgPanel &argPanel, thSynth &synth)
{
    thPanelEdit edit;
    thPanelResult r;

    thArg *cutoff = synth.getChanArg(0, "cutoff");

    if (cutoff == NULL)
    {
        fail("the cutoff arg is reachable", "");
        return;
    }

    r = argPanel.propose("cutoff", "0.5", edit);

    check(r.ok && r.changed && fabs(edit.value - 0.5) < 1e-9,
          "a typed number becomes an intent", r.why);
    check(edit.kind == thPanelEdit::CHANARG && edit.a == 0 &&
          edit.row == "cutoff",
          "and the intent says which control on which channel");

    /* Nothing is written until a shell delivers it. That is the whole point
       of the split: in the browser the delivery is a broadcast command, and
       a model that wrote here could not be used there at all. */
    check(fabs((double)(*cutoff)[0] - 0.25) < 1e-6,
          "proposing does not write", to_string((double)(*cutoff)[0]));

    check(argPanel.deliver(edit) &&
          fabs((double)(*cutoff)[0] - 0.5) < 1e-6,
          "delivering does", to_string((double)(*cutoff)[0]));

    /* The catching-up guard. Without it a MIDI controller moving this arg
       moves the control, which reports an edit, which marks the patch
       modified -- so a patch was edited by the act of looking at it. */
    r = argPanel.propose("cutoff", "0.5", edit);

    check(r.ok && !r.changed,
          "an intent that matches what is already there is allowed and does "
          "nothing", r.why);

    /* A control's min and max are its declared travel, and the desktop has
       always held a typed value to them through the adjustment a slider and
       its box share. */
    r = argPanel.propose("cutoff", "9", edit);

    check(r.ok && fabs(edit.value - 1) < 1e-9,
          "a number past the end of the range is held to it",
          to_string(edit.value));

    r = argPanel.propose("cutoff", "not a number", edit);

    check(!r.ok && !r.why.empty(), "a value that is not a number is refused",
          r.why);

    /* strtod reads a prefix, so "4k" used to be 4. */
    r = argPanel.propose("cutoff", "0.5k", edit);

    check(!r.ok, "and so is a number with something after it", r.why);

    /* Folded on the way in, exactly as it was unfolded on the way out. */
    r = argPanel.propose("decay", "1000", edit);

    check(r.ok && fabs(edit.value - 44100) < 1e-6,
          "a duration typed in milliseconds is folded back to samples",
          to_string(edit.value));

    r = argPanel.propose("wave", "Triangle", edit);

    check(r.ok && edit.value == 3, "a selector takes the value's name",
          to_string(edit.value));

    /* What a command carrying one would spell it as. */
    r = argPanel.propose("wave", "3", edit);

    check(r.ok && edit.value == 3, "or the number itself", to_string(edit.value));

    /* atof() reads a misspelled name as 0, which would have selected
       whatever value 0 means. */
    r = argPanel.propose("wave", "Sqare", edit);

    check(!r.ok, "a name that is not on the list is refused, not rounded",
          r.why);

    r = argPanel.propose("wave", "1", edit);

    check(!r.ok, "and so is a value the plugin does not implement", r.why);

    r = argPanel.propose("nosuchcontrol", "1", edit);

    check(!r.ok, "an edit naming a control that has gone is refused", r.why);
    check(!argPanel.deliver(edit), "and delivering it writes nothing");

    /* A control this panel does not draw is not a control it will write.
       `quiet' is a live arg on this channel that the file said not to draw,
       and the panel is the only thing between a command off the wire and the
       write -- a shell cannot check what it was never told about. */
    r = argPanel.propose("quiet", "0.9", edit);

    check(!r.ok, "an edit of a control the file hid is refused too", r.why);

    thArg *quiet = synth.getChanArg(0, "quiet");

    check(!argPanel.deliver(edit) && quiet && fabs((double)(*quiet)[0] - 0.5)
          < 1e-6, "and it stays where it was");

    /* A number finer than the row shows cannot be left in the arg: the
       control that would have to display it has four decimals, and the value
       box would read one number while the arg held another. */
    r = argPanel.propose("cutoff", "0.123456789", edit);

    check(r.ok && fabs(edit.value - 0.1235) < 1e-9,
          "a typed number is held to the row's resolution",
          to_string(edit.value));

    /* And it rounds in display units, before the fold, so the samples that
       land are the ones the milliseconds shown spell. */
    r = argPanel.propose("decay", "500.7", edit);

    check(r.ok && fabs(edit.value - thPanelFromDisplay(501, "ms")) < 1e-6,
          "a whole-number row rounds before it is folded",
          to_string(edit.value));

    /* The catching-up guard has to survive the round trip through the
       spelling a row is drawn with, or it never fires at all: the arg holds a
       float and the spelling folds back to a double, and comparing those two
       at their own widths makes every edit a change. The failure is a patch
       that reports itself modified when a control is put back where it was,
       and a page that re-posts its own edit for ever. */
    r = argPanel.propose("cutoff", "0.3", edit);

    check(r.ok && r.changed && argPanel.deliver(edit),
          "a value no float holds exactly is delivered", r.why);

    thPanel spelled;

    argPanel.build(spelled);

    const thPanelRow *back = spelled.find("cutoff");

    check(back && back->text == "0.3000",
          "and the panel spells it back as it was typed",
          back ? back->text : string("(gone)"));

    if (back)
    {
        r = argPanel.propose("cutoff", back->text, edit);

        check(r.ok && !r.changed,
              "and proposing that same spelling again is an echo, not an "
              "edit that never lands", r.why);
    }

    cutoff->setValue(0.25);
}

/* A value the plugin does not implement is already in the file: the display
   says so rather than lying about it. */
static void checkUnlistable (ArgPanel &argPanel, thSynth &synth)
{
    thArg *wave = synth.getChanArg(0, "wave");

    if (wave == NULL)
    {
        fail("the wave arg is reachable", "");
        return;
    }

    const float was = (*wave)[0];

    wave->setValue(1);

    thPanel panel;

    argPanel.build(panel);

    const thPanelRow *row = panel.find("wave");

    if (row == NULL)
        fail("the selector still has a row", "");
    else
    {
        check(row->text.empty(),
              "a stored value with no row on the list shows as nothing "
              "rather than as the wrong name", row->text);
        check(thPanelChoiceIndex(row->choices, row->value) == -1,
              "and nothing is selected");
    }

    wave->setValue(was);
}

/* Rows appearing or vanishing is a rebuild; a value moving is not.
 *
 * This is what lets a panel follow a MIDI controller without being torn down
 * and built again sixty times a second, and it is the number the browser's
 * half polls. */
static void checkShape (ArgPanel &argPanel, thSynth &synth)
{
    thPanel first, again;

    argPanel.build(first);
    argPanel.build(again);

    check(first.shape == again.shape,
          "the same channel describes the same shape twice");

    thArg *cutoff = synth.getChanArg(0, "cutoff");

    if (cutoff)
    {
        const float was = (*cutoff)[0];

        cutoff->setValue(0.75);

        thPanel moved;

        argPanel.build(moved);

        check(moved.shape == first.shape,
              "a value moving does not change the shape");
        check(moved.find("cutoff") &&
              fabs(moved.find("cutoff")->value - 0.75) < 1e-6,
              "though it does change the value");

        cutoff->setValue(was);
    }

    ArgPanel fewer;

    fewer.setChannel(0);
    fewer.exclude("amp");

    thPanel hidden;

    fewer.build(hidden);

    check(hidden.find("amp") == NULL,
          "a control the shell asks not to draw is not drawn");
    check(hidden.shape != first.shape,
          "and a panel with a row missing is a different shape");

    thPanelEdit edit;

    const thPanelResult r = fewer.propose("amp", "0.1", edit);

    check(!r.ok, "and it is not one an edit can reach either", r.why);

    /* The travel, the step and the width of the value box are cut into a
       widget when it is made and cannot be pushed into one afterwards, so a
       shape that does not cover them is a shape that leaves a 0..1 slider of
       four decimals standing in front of a parameter running to 2000. */
    if (cutoff)
    {
        const float wasMax = cutoff->max();

        cutoff->setMax(2000);

        thPanel wider;

        argPanel.build(wider);

        check(wider.find("cutoff") && wider.find("cutoff")->hi == 2000 &&
              wider.find("cutoff")->decimals == 0,
              "a wider range is a wider row");
        check(wider.shape != first.shape,
              "and a row whose range changed is a different shape");

        cutoff->setMax(wasMax);

        thPanel narrow;

        argPanel.build(narrow);

        check(narrow.shape == first.shape,
              "and putting the range back puts the shape back");
    }
}

/* The second arg map on the same channel, reached through the same lookup.
 *
 * The way this fails is silently -- a control that moves and changes nothing,
 * because the name it wrote was the instrument's and there is nothing there. */
static void checkEffect (thSynth &synth, const string &file)
{
    if (synth.loadEffect(file, 0) == NULL)
    {
        fail("the effect loads onto the channel", file);
        return;
    }

    ArgPanel fx;

    fx.setChannel(0);
    fx.setPrefix(TH_EFFECT_PREFIX);

    thPanel panel;

    check(fx.build(panel), "the effect has a panel of its own");
    check(idsOf(panel) == "trim,wet",
          "and it holds the effect's controls, not the instrument's",
          idsOf(panel));

    /* Unprefixed, so what a row is called does not depend on which of the
       two maps it came from. The prefix goes back on at the lookup. */
    check(panel.find("wet") && panel.find("wet")->label == "Wet",
          "the rows are named without the prefix");
    check(fx.argFor("wet") == synth.getChanArg(0, "fx.wet"),
          "and a row still finds the arg the prefix reaches");

    thPanelEdit edit;

    const thPanelResult r = fx.propose("wet", "0.75", edit);

    check(r.ok && r.changed && fx.deliver(edit), "an edit of one is allowed",
          r.why);

    thArg *wet = synth.getChanArg(0, "fx.wet");

    check(wet && fabs((double)(*wet)[0] - 0.75) < 1e-6,
          "and it lands on the effect's arg",
          wet ? to_string((double)(*wet)[0]) : string("(gone)"));

    /* The instrument's own controls are a separate map and must not have
       moved. */
    thArg *instrument = synth.getChanArg(0, "wet");

    check(instrument == NULL,
          "the instrument has no control of that name to have hit");
}

/* The rate the loader folded at is the rate the panel unfolds at.
 *
 * It used to be the compile-time TH_SAMPLE on both sides, so the mistake
 * cancelled out and the panel was right by luck. Now that the loader folds at
 * the synth's rate, a session started with `-r 48000' would show every
 * envelope time 8.8% long -- and write it back that way -- if the panel asked
 * anyone else.
 *
 * Its own synth, after the first has been destroyed: thSynth::instance() is
 * the first one constructed, and that is what the model asks. */
static void checkRate (const string &pluginPath, const string &file)
{
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, 48000);

    if (synth.loadTree(file, 0, 100) == NULL)
    {
        fail("the instrument loads at 48000", file);
        return;
    }

    thArg *decay = synth.getChanArg(0, "decay");

    if (decay == NULL)
    {
        fail("the decay arg is reachable at 48000", "");
        return;
    }

    /* 500 ms is 24000 samples at 48000 and 22050 at 44100. Both folds are
       exact, so this is an equality and not a tolerance. */
    check((double)(*decay)[0] == 24000.0,
          "a duration is stored at the rate the synth is running at",
          to_string((double)(*decay)[0]));

    ArgPanel argPanel;

    argPanel.setChannel(0);

    thPanel panel;

    argPanel.build(panel);

    const thPanelRow *row = panel.find("decay");

    check(row && fabs(row->value - 500) < 1e-6,
          "and reads as the same 500 ms it was written as",
          row ? to_string(row->value) : string("(no row)"));

    thPanelEdit edit;

    const thPanelResult r = argPanel.propose("decay", "250", edit);

    check(r.ok && fabs(edit.value - 12000) < 1e-6,
          "and an edit of it folds at that rate too", to_string(edit.value));
}

/* `-j': the two panels, printed, and the .dsp text they were built from
 * written out beside the dump.
 *
 * The files as well as the dump, because the other side of the comparison
 * has to load the same bytes and has no compiled-in copy of them. A
 * directory rather than a file, for the three of them.
 *
 * Returns nonzero on a failure, so that a dump that could not be made is not
 * mistaken for an empty panel. */
/* thPanelSpell over the cases where a second implementation of it goes
 * wrong, as JSON, for the page's half of the gate to reproduce.
 *
 * The panel dump cannot carry this. Its rows hold whatever the fixture's args
 * happen to be worth, and what the page has to agree about is the rule --
 * which of two adjacent spellings a value exactly between them gets. printf
 * rounds such a tie to the even digit; JavaScript's toFixed rounds it up, and
 * reads the shortest decimal that names a double rather than the double, so a
 * page spelling its own numbers disagrees here and nowhere else. A tie is not
 * an exotic input either: a control's travel is powers of ten.
 */
static string spellings (void)
{
    static const double values[] = {
        0.25, 0.35, 0.125, 2.5, 500.5, 501.5, 1.005, -0.25, -0.35,
        0.0001, 0.00005, 882000, 20000.5, 1234.5678, 0, -0.00001,
        0.1 + 0.2, 1.0 / 3.0, 16.0 / 3.0
    };

    static const int places[] = { 0, 1, 2, 3, 4 };

    string out = "[";

    for (size_t v = 0; v < sizeof values / sizeof *values; v++)
        for (size_t d = 0; d < sizeof places / sizeof *places; d++)
        {
            char buf[64];

            snprintf(buf, sizeof buf, "%s[%.17g,%d,\"", out == "[" ? "" : ",",
                     values[v], places[d]);

            out += buf;
            out += thPanelSpell(values[v], places[d]);
            out += "\"]";
        }

    return out + "]\n";
}

static int dumpTo (const string &dir, const string &pluginPath,
                   const string &instrument)
{
    std::error_code ec;

    std::filesystem::create_directories(dir, ec);

    if (!writeFile((std::filesystem::path(dir) / "panel.dsp").string(),
                   INSTRUMENT))
        return 1;

    /* The level tw_load uses, so that `amp' holds the same number on both
       sides: it is a row like the rest, and a panel is compared whole. */
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    if (synth.loadTree(instrument, 0, TH_DEFAULT_CHAN_AMP) == NULL)
    {
        fprintf(stderr, "panelcheck: %s did not load\n", instrument.c_str());

        return 1;
    }

    ArgPanel argPanel;
    thPanel panel;

    argPanel.setChannel(0);
    argPanel.build(panel);

    const string text = thPanelToJson(panel) + "\n";

    if (!writeFile((std::filesystem::path(dir) / "panel.json").string(), text))
        return 1;

    if (!writeFile((std::filesystem::path(dir) / "spell.json").string(),
                   spellings()))
        return 1;

    fputs(text.c_str(), stdout);

    return 0;
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;
    string dumpDir;

    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            pluginPath = argv[++i];
        else if (!strcmp(argv[i], "-j") && i + 1 < argc)
            dumpDir = argv[++i];

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    const string instrument = scratchPath("panelcheck-scratch.dsp");
    const string effect = scratchPath("panelcheck-scratch-fx.dsp");
    const string graph = scratchPath("panelcheck-scratch-graph.dsp");

    if (!writeFile(instrument, INSTRUMENT) || !writeFile(effect, EFFECT) ||
        !writeFile(graph, GRAPH))
        return 1;

    if (!dumpDir.empty())
    {
        const int bad = dumpTo(dumpDir, pluginPath, instrument);

        remove(instrument.c_str());
        remove(effect.c_str());

        return bad;
    }

    checkArithmetic();
    checkJson();
    checkKnobs();

    {
        thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH,
                      TH_DEFAULT_SAMPLES);

        if (synth.loadTree(instrument, 0, 100) == NULL)
        {
            fail("the instrument loads", instrument);

            printf("\n%d failure(s)\n", failed);

            return failed;
        }

        ArgPanel argPanel;

        argPanel.setChannel(0);

        thPanel panel;

        check(argPanel.build(panel), "the channel has a panel");

        checkRows(panel);
        checkGroups(panel);
        checkShape(argPanel, synth);
        checkUnlistable(argPanel, synth);
        checkEdits(argPanel, synth);
        checkEffect(synth, effect);
    }

    checkNodes(pluginPath, graph);
    checkRate(pluginPath, instrument);

    remove(instrument.c_str());
    remove(effect.c_str());
    remove(graph.c_str());

    printf("\n%d failure(s)\n", failed);

    return failed;
}
