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

#include <string.h>

#include "think.h"
#include "thLexer.h"
#include "thUnits.h"

#include "thinksynth_dsp.h"

#include "Controls.h"

/* The graph's controls, read off a synth that loads it once. */
static std::vector<Control> readControls (void)
{
    std::vector<Control> out;
    thSynth synth("", kWindow, kReadRate);

    if (synth.loadTreeText(thPluginDspName, thPluginDspText, 0,
                           TH_DEFAULT_CHAN_AMP) == NULL)
        return out;

    const thArgMap args = synth.getChanArgs(0);

    /* The order the .dsp first names each control in -- its declarations,
       which come before any node reads one. The map is sorted by name. */
    std::vector<thLexToken> tokens;
    std::vector<std::string> order;

    thLexString(thPluginDspText, tokens);

    for (size_t i = 0; i + 1 < tokens.size(); i++)
    {
        if (tokens[i].kind != thLexToken::PUNCT || tokens[i].text != "@" ||
            tokens[i + 1].kind != thLexToken::WORD)
            continue;

        const std::string &name = tokens[i + 1].text;
        bool seen = false;

        for (size_t k = 0; k < order.size() && !seen; k++)
            seen = order[k] == name;

        if (!seen)
            order.push_back(name);
    }

    order.push_back("amp");

    for (size_t k = 0; k < order.size(); k++)
    {
        thArgMap::const_iterator i = args.find(order[k]);

        if (i == args.end() || i->second == NULL)
            continue;

        thArg *arg = i->second;

        if (arg->widgetType() == thArg::HIDE ||
            arg->type() != thArg::ARG_VALUE || arg->len() != 1)
            continue;

        Control c;
        const std::string &units = arg->units();

        c.name = order[k];
        c.label = arg->label().empty() ? c.name : arg->label();
        c.group = arg->group();
        c.units = units;
        c.min = (float)thUnfoldUnit(arg->min(), units, kReadRate);
        c.max = (float)thUnfoldUnit(arg->max(), units, kReadRate);
        c.def = (float)thUnfoldUnit((*arg)[0], units, kReadRate);
        c.step = arg->step();
        c.valueNames = arg->valueNames();

        /* The level is the channel's, not the graph's. */
        if (c.name == "amp")
        {
            c.label = "Level";
            c.group = "Output";
        }

        out.push_back(c);
    }

    return out;
}

const std::vector<Control> &controls (void)
{
    static const std::vector<Control> c = readControls();

    return c;
}

