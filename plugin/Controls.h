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

/* The controls of the graph the plugin was built from, as a host and the
 * editor both see them.
 *
 * Read off the engine at build time, by controlsgen (ReadControls.cpp),
 * and compiled in as a table (thinksynth_controls.h), so the DSP side
 * (ThinkPlugin.cpp) and the editor (ThinkUI.cpp) share one list without
 * the editor -- a module of its own in LV2 -- having to carry the engine
 * to parse the graph. plugincheck holds the table to a parse at run time. */

#ifndef TH_PLUGIN_CONTROLS_H
#define TH_PLUGIN_CONTROLS_H 1

#include <string.h>

#include <string>
#include <vector>

/* Frames per synth window. 64 keeps a note within 1.3 ms of its frame at
   48 kHz; the engine's cost per window is small against that. */
const int kWindow = 64;

/* The rate the controls are read at. Any rate would do: every folded value
   is unfolded again at the same rate, which is exact. */
const long kReadRate = 48000;

/* One parameter as the host sees it. */
struct Control
{
    std::string name;       /* the chanarg, or "amp" for the level */
    std::string label;
    std::string group;      /* the .dsp's @x.group; "Output" for the level */
    std::string units;      /* as the .dsp wrote them */
    float min, max, def;    /* in those units */
    float step;
    std::vector<std::string> valueNames;
};

/* The row controlsgen writes per control: a Control with its value names
   joined by newlines, so the table is plain data. */
struct ControlRow
{
    const char *name, *label, *group, *units;
    float min, max, def, step;
    const char *valueNames;
};

/* Every control the graph declares with a widget, in the order it declares
 * them, and then the channel's level. */
const std::vector<Control> &controls (void);

/* The same for the graph `text', named `name' in log lines, parsed off
   the engine: controlsgen's source, and plugincheck's reference. Needs
   the engine linked; nothing in a host calls it. Empty if the graph did
   not load. */
std::vector<Control> readControls (const std::string &name,
                                   const std::string &text);

/* Whether a control turns on a log scale: a range starting above zero and
 * spanning more than a factor of twenty, which is where cutoffs and rates
 * want their resolution. The editor's knobs, and the hint a host's own
 * controls are given. */
inline bool logScale (const Control &c)
{
    return c.valueNames.empty() && c.step != 1.0f && c.min > 0 &&
           c.max / c.min >= 20;
}

/* What the graph says about itself, from the same table. */
const char *controlsDescription (void);

#endif /* TH_PLUGIN_CONTROLS_H */
