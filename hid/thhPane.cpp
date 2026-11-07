/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#include <cstdio>

#include "thhPane.h"

static std::string
number (double v)
{
    char s[32];

    snprintf(s, sizeof(s), "%g", v);
    return s;
}

static std::string
signedInt (int v)
{
    return (v < 0 ? "" : "+") + std::to_string(v);
}

static std::string
range (const thcHidBinding &b)
{
    if (!b.hasMin && !b.hasMax)
        return "";
    return ", " + (b.hasMin ? number(b.min) : "its least") + " to " +
           (b.hasMax ? number(b.max) : "its most");
}

/* One binding's row. channel is the one its layer, or the profile, plays
   on, 1-16, or 0 for the keyboard's. */
static thhPaneRow
row (const thcHidBinding &b, const std::string &layer, int channel,
     const thhControls &controls, const thhState &now, int curChannel,
     const std::function<bool (int, const std::string &)> &hasArg)
{
    thhPaneRow r;
    const int ch = b.channel ? b.channel : channel;
    const std::string where = ch ? "channel " + std::to_string(ch)
        : "the keyboard's channel (" + std::to_string(curChannel + 1) + ")";
    const int button = controls.button(b.control);
    const int axis = controls.axis(b.control);
    char v[16];

    r.layer = layer;
    r.control = b.control;
    r.yours = b.line > 0;

    switch (b.kind)
    {
    case thcHidBinding::NOTE:
        r.action = "note " + signedInt(b.semis) + (b.latch ? ", latched" : "");
        r.target = where;
        break;
    case thcHidBinding::STRIKE:
        r.action = "strike " + signedInt(b.semis) + " at " + number(b.at);
        r.target = where;
        break;
    case thcHidBinding::BEND:
        r.action = "bend held notes, " + std::to_string(b.range) +
                   " semitones" + (axis >= 0 && controls.axes[axis].bipolar
                                   ? " either way" : " up");
        break;
    case thcHidBinding::AUX:
        r.action = "aux" + std::to_string(b.semis) + " of held notes" + range(b);
        break;
    case thcHidBinding::OCTAVE:
        r.action = "octave " + signedInt(b.semis);
        break;
    case thcHidBinding::HOLD:
    case thcHidBinding::TOGGLE:
        r.action = std::string(b.kind == thcHidBinding::HOLD ? "hold"
                                                             : "toggle") +
                   " layer " + b.layer;
        break;
    case thcHidBinding::VALUE:
        r.action = "knob " + b.arg + range(b) +
                   (b.shape == thcHidBinding::EXP ? ", exp" : "") +
                   (b.smooth ? ", smoothed over " + number(b.smooth) + " s"
                             : "");
        r.target = where;
        if (!hasArg(ch ? ch - 1 : curChannel, b.arg))
            r.warning = (ch ? "channel " + std::to_string(ch)
                            : "the keyboard's channel") +
                        "'s patch has no knob '" + b.arg + "'";
        break;
    case thcHidBinding::PITCH:
    {
        const bool both = axis >= 0 && controls.axes[axis].bipolar;
        const std::string unit = !b.scale.empty() ? " degree"
                                 : b.octaves      ? " octave"
                                                  : " semitone";

        r.action = "pitch, " + std::to_string(b.range) + unit +
                   (b.range == 1 ? "" : "s") +
                   (b.scale.empty() ? "" : " of a scale") +
                   (both ? " either way" : " up");
        break;
    }
    case thcHidBinding::VELOCITY:
        r.action = "velocity" + range(b);
        break;
    }

    if (button >= 0)
        r.value = now.buttons & (1u << button) ? "down" : "up";
    else if (axis >= 0)
    {
        snprintf(v, sizeof(v), "%.2f", now.axis[axis]);
        r.value = v;
    }

    return r;
}

std::vector<thhPaneRow>
thhPaneRows (const thcHidBlock &profile, const thhControls &controls,
             const thhState &now, int curChannel,
             const std::function<bool (int, const std::string &)> &hasArg)
{
    std::vector<thhPaneRow> rows;

    for (size_t i = 0; i < profile.bindings.size(); i++)
        rows.push_back(row(profile.bindings[i], "", profile.channel, controls,
                           now, curChannel, hasArg));

    for (size_t l = 0; l < profile.layers.size(); l++)
    {
        const thcHidLayer &layer = profile.layers[l];

        for (size_t i = 0; i < layer.bindings.size(); i++)
            rows.push_back(row(layer.bindings[i], layer.name,
                               layer.channel ? layer.channel : profile.channel,
                               controls, now, curChannel, hasArg));
    }

    return rows;
}
