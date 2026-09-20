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
 * is allowed to become. What is checked in editorcheck is that a PanelView
 * over the same description draws and moves real widgets.
 *
 * Builds its own .dsp files: the cases that matter are ones the corpus cannot
 * contain, and one of them -- a selector with a hole in it -- is the case the
 * shipped files are careful not to have.
 *
 *     scripts/panelcheck -p build/plugins/
 *
 * Exit status is the number of failures.
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

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;

    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            pluginPath = argv[++i];

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    const string instrument = scratchPath("panelcheck-scratch.dsp");
    const string effect = scratchPath("panelcheck-scratch-fx.dsp");

    if (!writeFile(instrument, INSTRUMENT) || !writeFile(effect, EFFECT))
        return 1;

    checkArithmetic();

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

    checkRate(pluginPath, instrument);

    remove(instrument.c_str());
    remove(effect.c_str());

    printf("\n%d failure(s)\n", failed);

    return failed;
}
