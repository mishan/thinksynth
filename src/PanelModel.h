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

#ifndef PANEL_MODEL_H
#define PANEL_MODEL_H 1

/*
 * What a parameter panel *is*, with no widget and no DOM around it.
 *
 * The same move CanvasContent made for the two canvases. A panel used to be
 * described twice over -- once as gtkmm widgets in src/gui/, once as rows of
 * an HTML page in wasm/web/ -- and the two descriptions were written from the
 * domain objects separately, so every rule about what a parameter is had two
 * implementations that nothing made agree. The page's were the weaker ones:
 * it guessed at decimal places with toPrecision(3) and knew nothing about
 * units at all, so an envelope time read 882000 there and 20000 ms here.
 *
 * This is that description, once: plain data a provider fills in from live
 * state, and a shell renders. What moves into it is the rules --
 *
 *   units      thUnfoldUnit at the synth's rate, so a duration reads in ms
 *   resolution decimals that suit the range, and a box wide enough
 *   whole      a parameter its plugin reads `switch ((int)x)' steps by one
 *   named      the rows of a selector, which are not the identity
 *   groups     declared beats inferred, and a group of one is not a group
 *
 * -- and what stays out is every layout decision. No columns, no pixels, no
 * ordering beyond the groups. The model says "these rows, this one needs six
 * characters of value box"; the desktop's PanelView decides the rest with a
 * FlowBox and the page decides it with CSS, and neither answer belongs here.
 *
 * The guard on the toolkit-free claim is the build: nothing that compiles
 * this links a toolkit -- scripts/panelcheck builds the whole model and both
 * of its providers with no gtkmm anywhere on the include path -- so the
 * first Gtk:: that gets in fails to build.
 *
 * An edit does not write. A provider turns "row `cutoff', the person typed
 * 4000" into a thPanelEdit and says whether it is allowed; a shell delivers
 * it. On the desktop that delivery is a thArg::setValue and a markDirty; in
 * the page it is a command -- stamped, broadcast and applied by every peer
 * including the one that made it, because the local page has no privileged
 * path to the scheduler. A model that wrote directly could not be used by the
 * page at all.
 */

#include <string>
#include <vector>
#include <utility>      /* std::pair; <map> is not required to provide it */

/* Named explicitly rather than relying on think.h's `using namespace std'
   having been pulled in first, for the reason NodeGraph.h gives: this header
   is meant to be includable from a translation unit that knows nothing about
   libthink. */
using std::string;
using std::vector;
using std::pair;

/* One settable thing, drawn the way its kind asks for.
 *
 * A snapshot. Nothing here points into the engine, so a panel stays readable
 * -- and comparable, which is what the parity gate needs -- after the channel
 * it was built from has been replaced. */
struct thPanelRow
{
    /* SLIDER  a range to drag, with a number box beside it
       CHOICE  named values; `choices' says which number each row means
       NUMBER  a number with no useful range to drag through
       TEXT    a string: a note set, a file name
       READONLY  shown, not offered -- an output, a driven param
       TOGGLE  two states */
    enum Kind { SLIDER = 0, CHOICE, NUMBER, TEXT, READONLY, TOGGLE };

    Kind kind;

    /* Stable, and what an edit names. A chanarg's name, a param's name, a
       knob's index spelled out. Not a row number: rows move when a panel is
       rebuilt and an intent in flight must still mean the same control. */
    string id;

    string label;       /* what to show: the arg's label, or its name */
    string desc;        /* tooltip; may be empty */
    string group;       /* "" for a loose row */
    string units;       /* "ms", "Hz" -- shown with the label */

    /* In display units, already unfolded: 20000 for an envelope time the
       engine holds as 882000 samples. */
    double value;

    /* TEXT and READONLY rows, and a CHOICE's spelling. For a number, the
       value written out at `decimals' places, which is the spelling an edit
       of it would be compared against. */
    string text;

    /* The travel, and what one press of an arrow key moves.
     *
     * `step' is the row's resolution rather than the plugin's -- ten to the
     * minus `decimals' -- so a control cannot be left holding a number finer
     * than the one it shows. A parameter its plugin reads `switch ((int)x)'
     * is the case where that comes out as a step of 1 with no decimals, and
     * it is exactly the case that matters: 3.4 is a triangle spelled
     * misleadingly, and a control that can produce it is a control most of
     * whose travel does nothing. */
    double lo, hi, step;

    int decimals;       /* thPanelDecimals(hi), or 0 for a whole number */
    int valueChars;     /* thPanelValueChars(hi, decimals) */

    /* name -> the number it means, for CHOICE. Not the identity: a plugin
       may implement some of a range and not the rest, and a row for a value
       that does nothing is worse than no row. */
    vector<pair<string, int> > choices;

    /* The named thing this row's value is read through, bare and without
     * its `@', or empty for a plain value.
     *
     * Three readings of one field, and they are the same one from different
     * ends. On a composer param it is the knob binding -- `fmin = @warmth'
     * -- which is why such a row is shown and not offered: what moves it is
     * the knob. On a node's param it is the chanarg -- `in1 = @cutoff' --
     * for the same reason, and the thing that moves that one is the
     * channel's own panel. On a KNOB panel's row it is the knob's own name,
     * because there the row *is* the knob; the id is the number a command
     * names it by and this is what the .gen calls it.
     *
     * Bare in all three, because it is an identity and not a spelling: what
     * a command, a preset or a lookup names. Where the `@' is wanted it is
     * in the row's text, which is what a reader sees. */
    string knob;

    /* False for an output, a wired param, one bound to a knob. A shell draws
       it all the same -- seeing what a patch produces is half of reading it
       -- but does not offer to change it. */
    bool editable;

    thPanelRow (void)
        : kind(SLIDER), value(0), lo(0), hi(0), step(0), decimals(0),
          valueChars(0), editable(true) {}
};

/* A button the panel carries: "Capture to file", "Remove stage". Named
   rather than drawn, for the same reason a row is. */
struct thPanelAction
{
    string id, label;
    bool enabled;

    thPanelAction (void) : enabled(true) {}
};

struct thPanel
{
    /* Which provider built this, so a shell that renders any of them can
       stamp an intent without being told separately. */
    enum Kind { CHANARG = 0, KNOB, GEN_PARAM, NODE_VALUE };

    Kind kind;

    string title, subtitle;

    /* What the rows are about: a channel, or a chain and a stage, or a box.
       Carried through to the intent, which is the only thing that needs it. */
    int a, b;

    /* The groups, in the order they were first seen. Rows carrying no group
       come first in `rows'; each group's rows follow, contiguously. */
    vector<string> groupOrder;

    vector<thPanelRow> rows;
    vector<thPanelAction> actions;

    /* Bumped when rows appear, vanish, change editability or change what a
       control is made of -- its travel, its step, its width -- and never
       when a value moves. A shell rebuilds its widgets when this changes and
       otherwise pushes values into the ones it has, which is what lets a
       panel follow a MIDI controller without being torn down sixty times a
       second. */
    unsigned shape;

    thPanel (void) : kind(CHANARG), a(-1), b(-1), shape(0) {}

    /* -1 for a row that is not there. Callers hold ids, not indices. */
    int indexOf (const string &id) const;

    const thPanelRow *find (const string &id) const;
};

/* What a shell asks for, and a provider allows. Never applied by the model:
   see the header comment. */
struct thPanelEdit
{
    enum Kind { CHANARG = 0, KNOB, GEN_PARAM, NODE_VALUE };

    Kind kind;

    string row;

    /* The authored spelling: "4000", "Square", "@sweep", "\"C4 E4 G4\"".
       What a .gen line would carry, and what a command posts. */
    string valueText;

    /* Folded, engine units -- what thArg::setValue would be handed. */
    double value;

    /* The channel, or the chain and the stage, or the box. */
    int a, b;

    thPanelEdit (void) : kind(CHANARG), value(0), a(-1), b(-1) {}
};

/* Whether an edit is allowed, and whether there is anything left to do.
 *
 * `changed' is the catching-up guard, and it is here rather than in each
 * shell because both shells had to grow one and only one of them did. A
 * value that arrives from the arg rather than from the person comes back
 * round through the same path -- the panel follows the arg, so moving the
 * arg moves the panel, so the panel reports an edit -- and a patch was
 * therefore modified by the act of looking at it. An intent whose value is
 * the one already there is `ok' with `changed' false: permitted, and nothing
 * to deliver. */
struct thPanelResult
{
    bool ok;
    bool changed;
    string why;         /* why not, when !ok; empty otherwise */

    thPanelResult (void) : ok(true), changed(true) {}

    static thPanelResult refuse (const string &why);
    static thPanelResult echo (void);
};

/* ---- the rules ----------------------------------------------------- */

/* Samples <-> the unit the value was written in.
 *
 * The engine works in samples and in fractions of TH_MAX, and that is what is
 * stored; these only decide what a panel puts on screen. Both folds the
 * loader performs are exact and exactly invertible, so a value written
 * `7000 ms' comes back as 7000 and not 6999.97.
 *
 * The rate is the synth's, because that is the rate the loader folded at: a
 * session started with `-r 48000' would otherwise show every envelope time
 * 8.8% long and write it back that way. TH_SAMPLE is the fallback for a panel
 * with no synth behind it, which in practice means a test. */
double thPanelToDisplay (double raw, const string &units);
double thPanelFromDisplay (double shown, const string &units);

/* The whole of `text' as a number, or false.
 *
 * strtod alone is not that: it reads a prefix, so "4k" is 4 and "" is 0, and
 * a panel that took either would write a number nobody typed. Trailing space
 * is allowed because a value box hands back what is in it.
 *
 * Here rather than in a provider because every provider needs it and the
 * copies drifted: the check that the number had any digits at all was made
 * after the trailing blanks were skipped in two of them, which accepts "   "
 * as a valid nothing and calls it 0. A knob set to 0 by an empty box is a
 * knob outside its own range. */
bool thPanelNumberIn (const string &text, double &out);

/* Decimal places worth showing for a control whose range runs to `hi'.
 *
 * The resolution that matters is relative: a filter cutoff between 0 and 1
 * needs four places, an envelope time in samples between 0 and 882000 needs
 * none. Anything finer is noise the control cannot address anyway -- and
 * four places on everything is how `288000.0312' came to be printed into a
 * box sized for nine characters. */
int thPanelDecimals (double hi);

/* Characters the widest value in this range needs, so nothing is cut off. */
int thPanelValueChars (double hi, int decimals);

/* A number as a panel spells it: `decimals' places, and no exponent.
 *
 * One function because the spelling is compared as well as shown -- an edit
 * carries the text the shell produced, and a shell that wrote the number its
 * own way would make every comparison a near-miss. */
string thPanelSpell (double value, int decimals);

/* The index in `choices' meaning `value', or -1 where the list has no such
 * row.
 *
 * The second half is the point. A .dsp can hold a number the plugin does not
 * implement -- a 4 in an osc::window waveform, whose switch has no case for
 * it -- and the list has no row for that. Selecting nothing says so. Falling
 * back to row 0 would put "Sine" on screen next to an arg holding 4, which is
 * a display that lies; picking the nearest row would be a silent correction
 * no panel makes anywhere else.
 *
 * Truncating rather than rounding, matching what the plugin does with it:
 * `switch ((int)x)' reads 3.9 as 3. */
int thPanelChoiceIndex (const vector<pair<string, int> > &choices,
                        double value);

/* The whole panel as JSON, which is how it crosses into a page.
 *
 * JSON rather than a flat table like twdraw's, and the difference is what is
 * being read: a drawing is thousands of ops read every frame, and a panel is
 * tens of rows read once when it opens. So the page's half is a JSON.parse
 * rather than a marshalling loop with a call per field per row -- the shape
 * the composer panel's message is hand-built into today, ten accessors at a
 * time. Values, which do move continuously, go through a separate poll and
 * never re-serialize.
 *
 * Key order is the declaration order below and is not sorted, so the same
 * panel from two builds is the same bytes and can simply be diffed. Nothing
 * is omitted for being empty or zero, for the same reason.
 *
 * Doubles are written to seventeen significant figures: that is what makes a
 * double survive the round trip through text exactly, which is what a
 * comparison between a native dump and a wasm one is asking about. */
string thPanelToJson (const thPanel &panel);

/* Collects rows, works out their groups, and lays them into a thPanel.
 *
 * Grouping is a rule rather than a layout, which is why it is here and not in
 * either shell: a patch may declare `@a.group = "Envelope"' and a caller may
 * infer one from the graph, the declaration wins because it is the author
 * saying so, and a group of one is not a group.
 *
 * That last is not tidiness either. The inference is literal -- it groups by
 * the node a control drives -- and most controls drive a little math::mul of
 * their own, so ts1 came up with eight groups of one beside the single real
 * one. A titled, foldable block around a lone slider costs a header and a
 * fold nobody wants and says nothing the parameter's own label does not; it
 * also hides the worst of the names, since a node is called `cutcalc2'
 * because that is plumbing and not because it is what anyone calls that
 * knob. */
class thPanelBuilder
{
public:
    thPanelBuilder (void);

    /* `declared' is what the domain object says about itself and `inferred'
       is the caller's guess; either may be empty. */
    void add (const thPanelRow &row, const string &declared,
              const string &inferred);

    /* Fills `panel.rows', `panel.groupOrder' and `panel.shape'. Leaves the
       panel's title, kind and subject alone -- those are the provider's. */
    void finish (thPanel &panel) const;

private:
    vector<thPanelRow> rows_;
    vector<string> groups_;     /* parallel to rows_ */
};

#endif /* PANEL_MODEL_H */
