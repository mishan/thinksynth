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

#ifndef PANEL_VIEW_H
#define PANEL_VIEW_H

#include "PanelModel.h"

/* A thPanel, drawn: label/control/value rows, in as many columns as fit.
 *
 * The desktop half of the split PanelModel.h describes -- one of two shells
 * over the same description, the other being wasm/web/panel.js. Everything
 * here is a layout decision and nothing here is a rule: what the rows are,
 * what they are worth in milliseconds and which of them may be changed are
 * all settled before a thPanel arrives.
 *
 * A box rather than a grid, because a panel can put its rows in named groups
 * and each group is drawn as its own titled, foldable block above the loose
 * ones. So this holds a flow of the ungrouped rows and an expander per group,
 * each with a flow inside.
 *
 * It reports what was typed, not what it means. A shell can spell a number
 * and can name a choice; only the provider that built the panel knows what
 * either is worth -- the units to fold through, the values a note set may
 * contain -- so signal_edited carries the row and the spelling and the
 * provider turns that into an intent.
 */
class PanelView : public Gtk::Box
{
public:
    PanelView (void);
    ~PanelView (void);

    /* Builds the widgets. Replaces whatever was drawn before. */
    void setPanel (const thPanel &panel);

    const thPanel &panel (void) const { return panel_; }

    /* A value that moved behind the panel -- a MIDI controller, the node
     * editor, another peer.
     *
     * In display units, like everything else a row carries. Does not emit
     * signal_edited: the panel following the arg is not a person editing it,
     * and the two being told apart is what stops a patch being reported as
     * modified by the act of looking at it. */
    void setValue (const string &row, double display);

    /* The same, for a row whose value is a string rather than a number: a
     * note set, a file name, the words a READONLY row stands in for.
     *
     * Two calls and not one because the two cannot be told apart from the
     * value alone -- spelling a double into a row that holds `driven by
     * @cut' would replace it with a number that means nothing, and a TEXT
     * row has no number to spell in the first place. The provider knows
     * which of the two its row is, and this is where it says so. */
    void setText (const string &row, const string &text);

    /* "The person put this in that row." Row id and the authored spelling. */
    typedef sigc::signal<void(const string &, const string &)>
            type_signal_edited;

    type_signal_edited signal_edited (void) { return signal_edited_; }

    /* "The person pressed that button." The action's id. */
    typedef sigc::signal<void(const string &)> type_signal_action;

    type_signal_action signal_action (void) { return signal_action_; }

protected:
    /* What one row was drawn as, so a value arriving from behind the panel
       can be put into it without the row being found again. */
    struct Bound
    {
        thPanelRow row;

        Glib::RefPtr<Gtk::Adjustment> adjust;    /* SLIDER, NUMBER */
        Gtk::DropDown *choice;                   /* CHOICE */
        Gtk::Entry *entry;                       /* TEXT */
        Gtk::Label *readout;                     /* READONLY */
        Gtk::CheckButton *toggle;                /* TOGGLE */

        Bound (void)
            : choice(NULL), entry(NULL), readout(NULL), toggle(NULL) {}
    };

    void clear (void);

    /* Rows are addressed by index throughout: the label and the control
       have to end up in the same Bound, and a row id is a string lookup
       away from one. */
    Gtk::Widget *makeRow (size_t at);
    Gtk::Widget *makeControl (size_t at);
    Gtk::Widget *makeSlider (size_t at);
    Gtk::Widget *makeNumber (size_t at);
    Gtk::Widget *makeChoice (size_t at);
    Gtk::Widget *makeText   (size_t at);
    Gtk::Widget *makeRead   (size_t at);
    Gtk::Widget *makeToggle (size_t at);

    /* These rows, wrapped into however many columns the width allows. */
    Gtk::FlowBox *makeFlow (const std::vector<size_t> &rows);

    void emitEdit (const string &row, const string &valueText);

    void onAdjust (size_t at);
    void onChoice (size_t at);
    void onEntry (size_t at);
    void onToggle (size_t at);
    void onAction (string id);

    thPanel panel_;
    std::vector<Bound> bound_;

    /* True while setValue() is pushing a value into a widget.
     *
     * Every one of these widgets reports a change whoever made it, so
     * without this the panel catching up with an arg arrives back as an
     * edit. The model has the same guard in thPanelResult::echo, and both
     * are wanted: that one keeps a shell from marking a patch dirty, this
     * one keeps the round trip from happening at all. */
    bool settingValue_;

    type_signal_edited signal_edited_;
    type_signal_action signal_action_;

    /* Every label shares a width and every value box shares a width, across
       the whole panel and not merely within one block. Each row is its own
       little box, so without this there are no columns to line up in -- and
       lining them up across the groups as well is better than the grids
       managed, where each block worked its widths out alone. */
    Glib::RefPtr<Gtk::SizeGroup> nameWidth_;
    Glib::RefPtr<Gtk::SizeGroup> valueWidth_;
};

#endif /* PANEL_VIEW_H */
