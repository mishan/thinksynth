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

#ifndef ARG_PANEL_H
#define ARG_PANEL_H 1

/*
 * A channel's parameters, as a thPanel.
 *
 * The first of the four providers (see PanelModel.h), and the one that holds
 * most of the model's rules, because the desktop's channel panel is where
 * they were worked out. What used to be src/gui/ArgTable.cpp is this file and
 * src/gui/PanelView.cpp: what a parameter is, and how to draw one.
 *
 * Everything is looked up through the channel, never held.
 *
 * Loading a patch onto a channel replaces the channel and frees every arg on
 * it, and a panel is rebuilt from the same signal -- so a stored thArg * is a
 * pointer this class does not own, cannot be told about and has no way to
 * check. The patch bar's amplitude slider has always looked its arg up
 * through the channel number for that reason; this is the same move applied
 * to the rest of them.
 */

#include <map>
#include <string>
#include <vector>

#include "PanelModel.h"

class thArg;
class thSynthTree;

class ArgPanel
{
public:
    ArgPanel (void);

    /* Which channel these parameters belong to; -1 for none. */
    void setChannel (int chan) { chan_ = chan; }
    int channel (void) const { return chan_; }

    /* What goes in front of a parameter's name when it is looked up through
     * the channel.
     *
     * Empty for the instrument's own parameters, TH_EFFECT_PREFIX for the
     * channel effect's. A channel has two chanarg maps and one lookup --
     * thSynth::getChanArg reads `fx.delay' as the effect's -- so a panel over
     * the second map differs from a panel over the first by this string and
     * nothing else. The row ids are unprefixed, and the prefix goes back on
     * at the lookup, so what a row is called does not depend on which of the
     * two maps it came from. */
    void setPrefix (const string &prefix) { prefix_ = prefix; }
    const string &prefix (void) const { return prefix_; }

    /* A parameter this panel is not to draw, by name.
     *
     * One caller and one name: the channel amplitude, which the desktop pins
     * to the patch bar where it is in the same place on every page. Drawing
     * it here as well would be two controls for one value. A shell's
     * decision rather than a rule, which is why it is asked for rather than
     * assumed. */
    void exclude (const string &name);

    /* The panel, from whatever is on the channel now. False -- and an empty
       panel -- when there is no channel or it has no parameters. */
    bool build (thPanel &out) const;

    /* What an edit of `row' to `valueText' means.
     *
     * Fills `out' and returns whether it is allowed and whether there is
     * anything to deliver. Writes nothing: see the header of PanelModel.h
     * for why not even the desktop, which obviously could, does it here.
     *
     * `valueText' is in display units for a number and is the value's name
     * for a selector; a selector also accepts the number itself, since that
     * is what a command carrying one would spell it as. */
    thPanelResult propose (const string &row, const string &valueText,
                           thPanelEdit &out) const;

    /* The last step, and the only thing here that writes.
     *
     * The desktop calls this from the handler that made the intent. The page
     * does not: it posts the intent as a command, and the command's handler
     * calls this on every peer including the one that typed it. Same write,
     * two arrivals -- which is the whole reason propose() and deliver() are
     * two functions.
     *
     * False when the arg has gone, which is what a stale intent looks like. */
    bool deliver (const thPanelEdit &edit) const;

    /* What a row's control should be showing, from the arg as it stands
     * now, in whatever units that row is drawn in.
     *
     * A slider's value is unfolded and a selector's is not -- `Square' is
     * not 2 milliseconds of anything -- and a shell pushing a value that
     * moved behind the panel has to make the same distinction the row was
     * built with. Asked for here so that it cannot come to be made twice
     * and differently. False when the arg has gone. */
    bool valueFor (const string &row, double &display) const;

    /* The arg a row stands for, so a shell can subscribe to it. NULL when
       there is none. Valid until the channel is next loaded, and no longer:
       see the header. */
    thArg *argFor (const string &row) const;

private:
    /* Which node drives each control, for grouping the panel by.
     *
     * Almost no patch declares `.group', but almost every patch groups its
     * controls all the same -- by what they are wired to. `@a', `@d', `@s'
     * and `@r' all feed the same env node, and that is the envelope, whether
     * or not anyone wrote the word down. The node editor already draws them
     * that way, stacked on the node they drive, so the two views agree about
     * what belongs together.
     *
     * Only controls with exactly one consumer are grouped. One read by
     * several nodes belongs to no single one of them -- it is a patch-wide
     * control, and the node editor leaves those free-standing for the same
     * reason.
     *
     * It lived in MainSynthWindow, which meant the page could only have
     * grouped its panel by writing the walk a second time in JavaScript. */
    static std::map<string, string> inferGroups (thSynthTree *tree);

    /* The tree behind this panel's arg map: the channel's for the
       instrument, the effect's under TH_EFFECT_PREFIX. */
    thSynthTree *tree (void) const;

    int chan_;
    string prefix_;
    std::vector<string> hidden_;
};

#endif /* ARG_PANEL_H */
