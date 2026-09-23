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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmath>
#include <sstream>

#include "think.h"

#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"

#include "StagePanel.h"

/* The three spellings a duration may be written in, in the order a shell
 * shows them.
 *
 * `b' is the file's fourth spelling and is an alias the loader takes for
 * `beats'; it is not offered, because two menu entries that mean the same
 * thing are a choice nobody has. A line that already says `4 b' reads as
 * beats and is written back as `beats', which is the writer's spelling
 * (docs/GEN_FORMAT.md). */
static const char *const UNITS[] = { "s", "ms", "beats" };
static const size_t UNITCOUNT = sizeof UNITS / sizeof UNITS[0];

/* ---- what an authored right-hand side is ------------------------------ */

/* The `shapeOf' that used to sit at the top of ComposerWindow.cpp, over the
 * loader's own tokenizer rather than over the characters.
 *
 * Reading it by hand was wrong in a way that only showed up once .gen grew
 * arithmetic: `prob = lfo->out * 0.5' begins with a letter and came back a
 * bare word, and `0.5 * 2' began with a digit and came back as the number
 * 0.5 with a unit of `*'. Both are values this panel must show and refuse to
 * splice, and neither is distinguishable from a preset name or a duration by
 * looking at the first character. thcGenLoader::tokenize is what the loader
 * reads the line with and what thcGenEdit decides the same question with, so
 * it is what this reads it with too. */
struct Authored
{
    enum Kind
    {
        NUMBER,     /* 4, 4000 ms, 2 beats                              */
        KNOB,       /* @warmth                                          */
        QUOTED,     /* "C4 E4 G4"                                       */
        WORD,       /* warm -- a scale's name or a preset's             */

        /* Arithmetic, a node binding, or anything else the file may hold
           and the writer will not splice over. */
        OTHER
    };

    Kind kind;

    double num;
    string unit;    /* "", "s", "ms", "beats"                           */
    string text;    /* the knob's name, the string's body, the word     */

    Authored (void) : kind(OTHER), num(0) {}
};

static Authored shapeOf (const string &valueText)
{
    Authored v;

    std::vector<thcGenToken> t;
    string err;
    int line = 0;

    if (!thcGenLoader::tokenize(valueText, t, err, line) || t.empty())
        return v;

    const size_t n = t.size() - 1;      /* the END token is not a value */

    if (n == 1)
    {
        switch (t[0].kind)
        {
            case thcGenToken::NUMBER:
                v.kind = Authored::NUMBER;
                v.num = t[0].num;
                return v;

            case thcGenToken::KNOB:
                v.kind = Authored::KNOB;
                v.text = t[0].text;
                return v;

            case thcGenToken::STRING:
                v.kind = Authored::QUOTED;
                v.text = t[0].text;
                return v;

            case thcGenToken::WORD:
                v.kind = Authored::WORD;
                v.text = t[0].text;
                return v;

            default:
                return v;
        }
    }

    if (n == 2 && t[0].kind == thcGenToken::NUMBER &&
        t[1].kind == thcGenToken::WORD)
    {
        for (size_t u = 0; u < UNITCOUNT; u++)
            if (t[1].text == UNITS[u] ||
                (t[1].text == "b" && string("beats") == UNITS[u]))
            {
                v.kind = Authored::NUMBER;
                v.num = t[0].num;
                v.unit = UNITS[u];

                return v;
            }
    }

    return v;
}

/* "53,56,60" -> "F3 Ab3 C4", for showing a resolved default as notes.
 *
 * The default a plugin registers for a note set is the resolved list, since
 * that is what it will be handed; nobody writes a piece in MIDI numbers, and
 * a box offering to edit one is a box whose contents have to be translated
 * before they can be read. */
static string intsToNotes (const string &ints)
{
    string out;
    const char *s = ints.c_str();

    while (s && *s)
    {
        const string n = thcGenLoader::noteName(atoi(s));

        if (!n.empty())
            out += (out.empty() ? "" : " ") + n;

        s = strchr(s, ',');

        if (s)
            s++;
    }

    return out;
}

/* ---- the provider ----------------------------------------------------- */

StagePanel::StagePanel (void)
    : doc_(NULL), sched_(NULL), chain_(-1), stage_(-1)
{
}

void StagePanel::setPiece (const thcGenEdit::Doc *doc, thcScheduler *sched)
{
    doc_ = doc;
    sched_ = sched;
}

void StagePanel::setStage (int chain, int stage)
{
    chain_ = chain;
    stage_ = stage;
}

thcStage *StagePanel::live (void) const
{
    if (sched_ == NULL || chain_ < 0 ||
        (size_t)chain_ >= sched_->chainCount())
        return NULL;

    const thcChain *c = sched_->chain((size_t)chain_);

    if (c == NULL || stage_ < 0 || (size_t)stage_ >= c->stages.size())
        return NULL;

    return c->stages[(size_t)stage_].get();
}

const thcGenEdit::Stage *StagePanel::authored (void) const
{
    if (doc_ == NULL || chain_ < 0 ||
        (size_t)chain_ >= doc_->chains.size())
        return NULL;

    const thcGenEdit::Chain &chain = doc_->chains[(size_t)chain_];
    const int at = thcGenEdit::docIndex(chain, stage_);

    if (at < 0 || (size_t)at >= chain.stages.size())
        return NULL;

    return &chain.stages[(size_t)at];
}

string StagePanel::defaultText (const thcPlugin *plugin, int param)
{
    const thcPlugin::ParamInfo *pi = plugin->paramInfo(param);

    if (pi == NULL)
        return string();

    string spelled;

    /* Every enumerator named, and no `default:'.
     *
     * This switch had one, and a param type added years after it was written
     * fell through to the numeric case and wrote `from = 0;' -- a generated
     * stage the loader then refused. -Wall's -Wswitch only fires on a switch
     * that covers the enum and misses a value, so a default label is exactly
     * what buys the silence. Spelling the numeric types out costs three
     * lines and turns the next addition into a compiler warning instead of a
     * bug report. */
    switch (pi->type)
    {
        case THC_PARAM_NOTESET:
            return "\"" + intsToNotes(pi->defString) + "\"";

        case THC_PARAM_STRING:
            return "\"" + pi->defString + "\"";

        /* The two with nothing to write. See the header. */
        case THC_PARAM_PRESET:
        case THC_PARAM_INSTRSET:
            return string();

        case THC_PARAM_FLOAT:
        case THC_PARAM_INT:
        case THC_PARAM_NOTE:
            if (!thcGenEdit::format(pi->def, spelled))
                return string();

            /* A duration's unit is not decoration: the loader refuses a bare
               number on one, so a default spelled without it is a line that
               would not load. Seconds, which is what the plugin reads and so
               what the number already is. */
            if (pi->isDuration())
                spelled += " s";

            return spelled;
    }

    return string();
}

/* The right-hand side the file gives, or the default's spelling for a line
   the file does not have. */
string StagePanel::valueTextOf (const thcPlugin *plugin, int param) const
{
    const thcPlugin::ParamInfo *pi = plugin->paramInfo(param);
    const thcGenEdit::Stage *stage = authored();

    if (stage != NULL)
        for (size_t i = 0; i < stage->params.size(); i++)
            if (stage->params[i].name == pi->name)
                return stage->params[i].valueText;

    return defaultText(plugin, param);
}

/* A knob's value, spelled as the number it actually is.
 *
 * A thArg holds a float and thcGenEdit::format writes a double, so the ten
 * significant figures it asks for turn the 0.35 somebody declared into
 * 0.349999994 -- a true account of the float, and a line nobody wrote. What
 * goes into the file is the shortest spelling that reads back as the same
 * float: 0.35 for a knob declared 0.35, and every digit it takes for one
 * that really is somewhere between two of them.
 *
 * Only for a value that came off a knob. A number typed into a box is
 * already the spelling somebody chose. */
static bool formatKnob (double value, string &out)
{
    const float want = (float)value;

    for (int digits = 1; digits <= 9; digits++)
    {
        char buf[64];

        snprintf(buf, sizeof buf, "%.*g", digits, value);

        if (strchr(buf, 'e') != NULL || (float)atof(buf) != want)
            continue;

        out = buf;

        return true;
    }

    return thcGenEdit::format(value, out);
}

/* The knob a bound row is read through, or NULL. */
static thArg *knobNamed (thcScheduler *sched, const string &name)
{
    return (sched == NULL || name.empty()) ? NULL : sched->knob(name);
}

/* One parameter, described.
 *
 * `pi' is what the plugin registered and `authored' is what the file says,
 * and every decision below is one or the other of them -- the live stage is
 * consulted only through `bound', which is the one thing neither can say. */
static thPanelRow rowFor (const thcPlugin::ParamInfo *pi,
                          const string &authored, const Authored &v,
                          thArg *bound, bool anyKnobs)
{
    thPanelRow row;

    row.id = pi->name;
    row.label = pi->name;
    row.desc = pi->desc;

    /* Arithmetic, a node's output, or a spelling nothing here understands.
     *
     * Shown whole and not offered. The value in the file is a graph, and
     * thcGenEdit::setParam refuses to splice a number over one on purpose
     * (docs/GEN_FORMAT.md 5a) -- so a box to type in would be a box whose
     * every use is refused, which is worse than no box and a reason. */
    if (!authored.empty() && v.kind == Authored::OTHER)
    {
        row.kind = thPanelRow::READONLY;
        row.editable = false;
        row.text = authored;
        row.desc = row.desc.empty()
            ? string("The file works this out; edit the text to change it.")
            : row.desc + "  The file works this out; edit the text to "
                         "change it.";

        return row;
    }

    switch (pi->type)
    {
        case THC_PARAM_FLOAT:
        case THC_PARAM_INT:
            break;

        /* Everything else is a name or a list of them, and none of the
         * three a knob could drive. A box with the file's own spelling in
         * it: note names rather than the MIDI numbers the store holds, the
         * body of the string without its quotes, the bare word a preset is
         * named by.
         *
         * A `.gen' says which of those it is by the param's type and never
         * by the value -- `warm' is a preset here and a scale there -- so
         * the tooltip says what this one wants, because the entry cannot. */
        case THC_PARAM_NOTE:
            row.kind = thPanelRow::TEXT;
            row.text = v.kind == Authored::QUOTED
                ? v.text
                : (v.kind == Authored::NUMBER
                       ? thcGenLoader::noteName((int)v.num) : v.text);
            row.desc += row.desc.empty() ? "" : "  ";
            row.desc += "One note name, like C4.";

            return row;

        case THC_PARAM_NOTESET:
            row.kind = thPanelRow::TEXT;
            row.text = v.text;
            row.desc += row.desc.empty() ? "" : "  ";
            row.desc += "Note names, or the name of a scale this piece "
                        "declares.";

            return row;

        case THC_PARAM_PRESET:
            row.kind = thPanelRow::TEXT;
            row.text = v.text;
            row.desc += row.desc.empty() ? "" : "  ";
            row.desc += "The name of a preset this piece declares.";

            return row;

        case THC_PARAM_INSTRSET:
            row.kind = thPanelRow::TEXT;
            row.text = v.text;
            row.desc += row.desc.empty() ? "" : "  ";
            row.desc += "The instruments this piece declares, by name, "
                        "separated by commas.";

            return row;

        case THC_PARAM_STRING:
            row.kind = thPanelRow::TEXT;
            row.text = v.text;

            return row;
    }

    /* A number, then, and the only kind of row a knob can be bound to. */
    row.kind = thPanelRow::NUMBER;
    row.bindable = anyKnobs;
    row.bounded = false;

    if (pi->isDuration())
    {
        /* A duration is offered in whichever of the three units the line
           was written in, and a bound one in seconds, which is the unit a
           line that is only a `@name' resolves to. */
        for (size_t u = 0; u < UNITCOUNT; u++)
            row.unitChoices.push_back(UNITS[u]);

        row.units = v.unit.empty() ? UNITS[0] : v.unit;
    }

    /* The declared range, in the unit this row is written in.
     *
     * A hint for a control's travel and not a bound the host enforces:
     * docs/GEN_FORMAT.md is explicit that a plugin's min and max are
     * advisory, and shipped pieces set values outside them on purpose. So
     * the range only decides how many decimals are worth showing and how
     * wide the box is, propose() clamps nothing, and a value the file
     * actually holds widens it rather than being pulled back into it. */
    row.lo = pi->min;
    row.hi = pi->max;

    if (row.units == "ms")
    {
        row.lo *= 1000;
        row.hi *= 1000;
    }

    if (bound != NULL)
    {
        /* Shown as the number the knob is at, because that is what the
         * stage is playing -- and not offered, because what moves it is the
         * knob. The binding is what there is to change here, and a shell
         * offers that whether or not it offers the number.
         *
         * Seconds whatever the menu says: a bound duration carries no unit
         * in the file, and the knob's number reaches the plugin as the
         * seconds it reads. */
        row.knob = v.text;
        row.editable = false;
        row.value = (*bound)[0];
        row.units = pi->isDuration() ? UNITS[0] : row.units;

        /* And no unit menu on it.
         *
           The menu means `2 s' and `2 beats' are different pieces rather
           than two spellings of one, and a bound line is neither: it carries
           no unit for anybody to change. Offered all the same -- both shells
           draw one for any row that has the choices, and neither disables it
           -- picking from it took the isUnitOf branch, spliced the knob's
           current number with a unit after it, and unbound the knob without
           anybody saying so. */
        row.unitChoices.clear();
    }
    else
        row.value = v.num;

    if (row.value < row.lo) row.lo = row.value;
    if (row.value > row.hi) row.hi = row.value;

    /* A param the plugin reads as a whole number steps by one and takes no
       decimals; everything else takes what its range is worth, which is
       what composerview.js's toPrecision(4) and the window's flat three
       places were each guessing at from different ends. */
    row.decimals = (pi->type == THC_PARAM_INT) ? 0 : thPanelDecimals(row.hi);
    row.step = pow(10.0, -row.decimals);
    row.valueChars = thPanelValueChars(row.hi, row.decimals);
    row.text = thPanelSpell(row.value, row.decimals);

    return row;
}

bool StagePanel::build (thPanel &out) const
{
    out = thPanel();
    out.kind = thPanel::GEN_PARAM;
    out.a = chain_;
    out.b = stage_;

    thcStage *stage = live();
    const thcGenEdit::Stage *doc = authored();

    if (stage == NULL || stage->plugin == NULL || doc == NULL)
        return false;

    /* Which stage, and what it is. A panel over one stage of a dozen has to
       name it, and the plugin's spelling is how a reader knows what the rows
       mean -- `gen::euclid' says more about a row called `k' than the row
       ever will. */
    out.title = doc->name;
    out.subtitle = doc->category + "::" + doc->plugin;

    /* The piece's knobs, in the order it declared them: what a bindable row
       may be bound to. Filled before the builder finishes, because the
       binding menu is a widget and so is part of the shape. */
    for (size_t i = 0; i < doc_->knobs.size(); i++)
        out.knobs.push_back(doc_->knobs[i].name);

    thPanelBuilder build;

    for (int p = 0; p < stage->plugin->paramCount(); p++)
    {
        const string text = valueTextOf(stage->plugin, p);
        const Authored v = shapeOf(text);

        build.add(rowFor(stage->plugin->paramInfo(p), text, v,
                         v.kind == Authored::KNOB
                             ? knobNamed(sched_, v.text) : NULL,
                         !out.knobs.empty()),
                  "", "");
    }

    build.finish(out);

    return !out.rows.empty();
}

/* ---- what a typed value becomes --------------------------------------- */

/* True if `name' is one of the units this row offers. */
static bool isUnitOf (const thPanelRow &row, const string &name)
{
    for (size_t i = 0; i < row.unitChoices.size(); i++)
        if (row.unitChoices[i] == name)
            return true;

    return false;
}

/* A preset's name, and nothing else.
 *
 * Bare, not quoted: the loader refuses a quoted one on purpose, because a
 * timbre vector spelled inline is a preset that cannot be saved under a name
 * or morphed towards. Checked here rather than left to the load, because the
 * panel is where the person is. */
static bool namesAPreset (const thcGenEdit::Doc *doc, const string &text)
{
    for (size_t i = 0; i < doc->presets.size(); i++)
        if (doc->presets[i].name == text)
            return true;

    return false;
}

/* Every name in `list' is an instrument this piece declares, and none of them
   twice; `why' is the refusal when one of those is untrue. The separators are
   the loader's: a comma, and the whitespace a quoted string may hold.
 *
 * Both halves are the loader's checks, made here because the panel is where
 * the person is. Only the first was, so a list naming one instrument twice
 * was spliced happily and then refused -- "'bell' is named twice" -- by the
 * next person to open the file, which is the failure this exists to stop. */
static bool namesInstruments (const thcGenEdit::Doc *doc, const string &list,
                              string &why)
{
    string one;
    vector<string> seen;

    for (size_t i = 0; i <= list.size(); i++)
    {
        const char c = i < list.size() ? list[i] : ',';

        if (c != ',' && c != ' ' && c != '\t' && c != '\n' && c != '\r')
        {
            one += c;
            continue;
        }

        if (one.empty())
            continue;

        size_t k = 0;

        while (k < doc->instruments.size() &&
               doc->instruments[k].name != one)
            k++;

        if (k == doc->instruments.size())
        {
            why = "this piece declares no instrument called '" + one + "'";

            return false;
        }

        for (size_t j = 0; j < seen.size(); j++)
            if (seen[j] == one)
            {
                why = "'" + one + "' is named twice";

                return false;
            }

        seen.push_back(one);
        one.clear();
    }

    if (seen.empty())
    {
        why = "the names of instruments this piece declares";

        return false;
    }

    return true;
}

static bool namesAScale (const thcGenEdit::Doc *doc, const string &text)
{
    for (size_t i = 0; i < doc->scales.size(); i++)
        if (doc->scales[i].name == text)
            return true;

    return false;
}

thPanelResult StagePanel::propose (const string &row, const string &valueText,
                                   thPanelEdit &out) const
{
    out = thPanelEdit();
    out.kind = thPanelEdit::GEN_PARAM;
    out.row = row;
    out.a = chain_;
    out.b = stage_;

    thcStage *stage = live();

    if (stage == NULL || stage->plugin == NULL || doc_ == NULL)
        return thPanelResult::refuse("no such stage");

    const int param = stage->plugin->paramIndex(row);

    if (param < 0)
        return thPanelResult::refuse("no parameter called " + row);

    const thcPlugin::ParamInfo *pi = stage->plugin->paramInfo(param);
    const string was = valueTextOf(stage->plugin, param);
    const Authored old = shapeOf(was);
    const thPanelRow described =
        rowFor(pi, was, old,
               old.kind == Authored::KNOB ? knobNamed(sched_, old.text)
                                          : NULL,
               !doc_->knobs.empty());

    if (described.kind == thPanelRow::READONLY)
        return thPanelResult::refuse(row + " is worked out by the file");

    string wrote;

    if (described.kind == thPanelRow::TEXT)
    {
        /* A .gen string literal is `"[^"\n]*"' with no escapes at all, so a
           quote or a newline simply cannot be written into one. Said here,
           where it names the row and the person is looking, rather than
           three layers down where the writer would refuse it. */
        if (valueText.find('"') != string::npos ||
            valueText.find('\n') != string::npos)
            return thPanelResult::refuse("a .gen string holds no quotes or "
                                         "newlines");


        /* A name, or a list of them. Which kind of name is the param's to
           say and never the word's: `warm' is a preset on one row and a
           scale on the next, and a typo has to be reported against what the
           plugin actually asked for. */
        switch (pi->type)
        {
            case THC_PARAM_NOTESET:
                if (namesAScale(doc_, valueText))
                    wrote = valueText;
                else
                {
                    std::vector<int> notes;
                    string bad;

                    if (!thcGenLoader::parseNoteList(valueText, notes, bad))
                        return thPanelResult::refuse(
                            "'" + bad + "' is not a note name");

                    wrote = "\"" + valueText + "\"";
                }
                break;

            case THC_PARAM_NOTE:
            {
                std::vector<int> notes;
                string bad;

                if (!thcGenLoader::parseNoteList(valueText, notes, bad) ||
                    notes.size() != 1)
                    return thPanelResult::refuse("one note name, like C4");

                /* A rest is good spelling in every note *list* the file
                   has and is not a note: it would set the param to -1 and
                   a plugin would read a pitch off the bottom of the
                   keyboard. The loader says so in its own words and so
                   does this. */
                if (notes[0] < 0)
                    return thPanelResult::refuse("a note, and a rest is "
                                                 "not one");

                wrote = "\"" + valueText + "\"";
                break;
            }

            case THC_PARAM_PRESET:
                if (!namesAPreset(doc_, valueText))
                    return thPanelResult::refuse(
                        "this piece declares no preset called '" +
                        valueText + "'");

                wrote = valueText;
                break;

            case THC_PARAM_INSTRSET:
            {
                if (valueText.empty())
                    return thPanelResult::refuse("the names of instruments "
                                                 "this piece declares");

                /* Every name checked here, so that a typo is an answer to
                   the person who made it rather than a piece that will not
                   load the next time anyone opens it. The loader checks the
                   same list at the same boundary and for the same reason. */
                string why;

                if (!namesInstruments(doc_, valueText, why))
                    return thPanelResult::refuse(why);

                wrote = "\"" + valueText + "\"";
                break;
            }

            /* Free text, and the numeric types, which cannot reach here:
               a row is TEXT because its param is one of the named kinds. */
            case THC_PARAM_STRING:
            case THC_PARAM_FLOAT:
            case THC_PARAM_INT:
                wrote = "\"" + valueText + "\"";
                break;
        }
    }
    else if (valueText == "@")
    {
        /* Let it go, and hold the number it was showing. A row offered as a
           binding has no number in the file to fall back on, so what it
           keeps is what the knob had it at -- which is the value anybody
           watching the panel was looking at when they unbound it. */
        string num;

        if (!formatKnob(described.value, num))
            return thPanelResult::refuse("the knob's value cannot be "
                                         "written down");

        /* A duration keeps a unit, and it is seconds: a knob is not
           tempo-scaled and what it holds is what the plugin reads. */
        wrote = num + (pi->isDuration() ? " " + described.units : "");
    }
    else if (!valueText.empty() && valueText[0] == '@')
    {
        const string name = valueText.substr(1);

        if (knobNamed(sched_, name) == NULL)
            return thPanelResult::refuse("'@" + name +
                                         "' is not a declared knob");

        wrote = "@" + name;
    }
    else if (isUnitOf(described, valueText))
    {
        /* The unit on its own: the number stays where it is and means
           something else, which is what a person changing the menu is
           saying. `2 s' picked over to beats is `2 beats' -- twice as long
           at 30bpm and half as long at 240 -- and not 2 seconds restated. */
        string num;

        if (!thcGenEdit::format(described.value, num))
            return thPanelResult::refuse("that value cannot be written "
                                         "down");

        wrote = num + " " + valueText;
    }
    else
    {
        /* A number, and the unit it is in if it was given one.
         *
         * A shell hands over the part the person touched, so what usually
         * arrives is a bare number and the unit the row already had is the
         * one that stays -- typing into a box is nobody saying anything
         * about seconds or beats. Both together is a whole authored line,
         * which is what a peer's command carries and what a caller holding
         * one already spelled out has. */
        string numText = valueText;
        string unit;

        const size_t sp = valueText.find_first_of(" \t");

        if (sp != string::npos)
        {
            numText = valueText.substr(0, sp);

            /* There may be nothing after the blanks at all -- "4 " is what a
               value box hands back -- and substr() from npos throws rather
               than answering "". This text arrives off a room command
               through tw_param with no shape to it, so the throw was the
               module gone, where the two branches beside this one were
               written to produce a refusal. */
            const size_t at = valueText.find_first_not_of(" \t", sp);

            if (at != string::npos)
                unit = valueText.substr(at);
        }

        if (!unit.empty() && !isUnitOf(described, unit))
            return thPanelResult::refuse("'" + unit + "' is not a unit " +
                                         row + " can be written in");

        double typed = 0;

        if (!thPanelNumberIn(numText, typed))
            return thPanelResult::refuse(valueText + " is not a number");

        string num;

        if (!thcGenEdit::format(pi->type == THC_PARAM_INT
                                    ? floor(typed + 0.5) : typed, num))
            return thPanelResult::refuse(valueText + " cannot be written "
                                         "down");

        if (pi->isDuration())
            wrote = num + " " + (unit.empty() ? described.units : unit);
        else
            wrote = num;
    }

    out.valueText = wrote;

    const Authored now = shapeOf(wrote);

    /* What the store will hold: seconds for a duration written in seconds or
       milliseconds, the beat count for one written in beats, and the number
       itself for everything else. Informational -- deliver() works from the
       line, which is the only form that can also say `@warmth' -- but it is
       what a shell reports and what a test reads. */
    if (now.kind == Authored::NUMBER)
        out.value = now.unit == "ms" ? now.num / 1000.0 : now.num;
    else if (now.kind == Authored::KNOB)
    {
        thArg *knob = knobNamed(sched_, now.text);

        out.value = knob != NULL ? (double)(*knob)[0] : 0;
    }

    /* The catching-up guard, and here it is the authored line that is
       compared rather than the number. Two lines that fold to the same value
       are not the same edit -- `2 s' and `2 beats' agree at 30bpm -- and a
       panel rebuilt after a splice reports the line it was just given, which
       without this would splice it again. */
    if (wrote == was)
        return thPanelResult::echo();

    return thPanelResult();
}

/* ---- the audible half ------------------------------------------------- */

bool StagePanel::deliver (const thPanelEdit &edit) const
{
    thcStage *stage = live();

    if (stage == NULL || stage->plugin == NULL || doc_ == NULL ||
        sched_ == NULL)
        return false;

    const int idx = stage->plugin->paramIndex(edit.row);

    if (idx < 0)
        return false;

    const thcPlugin::ParamInfo *pi = stage->plugin->paramInfo(idx);
    const Authored v = shapeOf(edit.valueText);

    switch (v.kind)
    {
        case Authored::KNOB:
        {
            thArg *knob = knobNamed(sched_, v.text);

            if (knob == NULL)
                return false;

            /* A knob reads as plain seconds; a beats flag left over from
               `2 beats' would tempo-scale it, which is neither what the new
               line says nor what a reload would do. */
            stage->params.setBeats(idx, false);
            sched_->bindKnob(stage, idx, knob);

            return true;
        }

        case Authored::NUMBER:
            /* A binding shadows the stored value entirely, so without this
               the new number would be set and never heard. */
            sched_->unbindParam(stage, idx);

            stage->params.setBeats(idx, v.unit == "beats");
            stage->params.set(idx, v.unit == "ms" ? v.num / 1000.0 : v.num);

            return true;

        case Authored::QUOTED:
        {
            sched_->unbindParam(stage, idx);

            if (pi->type != THC_PARAM_NOTESET && pi->type != THC_PARAM_NOTE)
            {
                stage->params.setString(idx, v.text);

                return true;
            }

            /* The host resolves pitch text once, at the file boundary, and
               no plugin ever parses a note name. An edit is that boundary
               too: what the store holds is the resolved list. */
            std::vector<int> notes;
            string bad;

            if (!thcGenLoader::parseNoteList(v.text, notes, bad))
                return false;

            if (pi->type == THC_PARAM_NOTE)
            {
                if (notes.size() != 1)
                    return false;

                stage->params.set(idx, notes[0]);

                return true;
            }

            std::ostringstream ints;

            for (size_t i = 0; i < notes.size(); i++)
                ints << (i ? "," : "") << notes[i];

            stage->params.setString(idx, ints.str());

            return true;
        }

        case Authored::WORD:
        {
            /* A scale's name, a preset's or an instrument's -- the named
               objects a param can refer to, spelled identically. Which one
               it is comes from the param's type, not from the word. */
            sched_->unbindParam(stage, idx);

            if (pi->type == THC_PARAM_PRESET)
            {
                for (size_t i = 0; i < doc_->presets.size(); i++)
                {
                    if (doc_->presets[i].name != v.text)
                        continue;

                    /* The same "name=value,..." the loader hands a plugin,
                       built the same way, so a preset set through a panel
                       and one read from the file are indistinguishable to
                       the composer. */
                    std::ostringstream vec;

                    for (size_t k = 0; k < doc_->presets[i].values.size();
                         k++)
                    {
                        string num;

                        if (!thcGenEdit::format(
                                doc_->presets[i].values[k].value, num))
                            return false;

                        vec << (k ? "," : "")
                            << doc_->presets[i].values[k].name << "=" << num;
                    }

                    stage->params.setString(idx, vec.str());

                    return true;
                }

                return false;
            }

            if (pi->type == THC_PARAM_INSTRSET)
            {
                stage->params.setString(idx, v.text);

                return true;
            }

            for (size_t i = 0; i < doc_->scales.size(); i++)
            {
                if (doc_->scales[i].name != v.text)
                    continue;

                std::vector<int> notes;
                string bad;

                if (!thcGenLoader::parseNoteList(doc_->scales[i].notes,
                                                 notes, bad))
                    return false;

                std::ostringstream ints;

                for (size_t j = 0; j < notes.size(); j++)
                    ints << (j ? "," : "") << notes[j];

                stage->params.setString(idx, ints.str());

                return true;
            }

            return false;
        }

        case Authored::OTHER:
            break;
    }

    return false;
}
