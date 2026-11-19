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
 */

#include <algorithm>

#include "think.h"
#include "thMidiRoutes.h"

namespace {

void addOnce (std::vector<int> &out, int engine)
{
    if (engine >= 0 && engine < TH_ENGINE_CHANNELS &&
        std::find(out.begin(), out.end(), engine) == out.end())
        out.push_back(engine);
}

}

void
thMidiRoutes::set (Layer layer, const std::vector<thMidiRoute> &routes)
{
    if (layer >= 0 && layer < LAYERS)
        layers_[layer] = routes;
}

const std::vector<thMidiRoute> *
thMidiRoutes::deciding (int midi) const
{
    for (const std::vector<thMidiRoute> &layer : layers_)
        for (const thMidiRoute &r : layer)
            if (r.midi == midi)
                return &layer;

    return NULL;
}

void
thMidiRoutes::base (int midi, std::vector<int> &out) const
{
    addOnce(out, omni_ >= 0 ? omni_ : midi);
}

void
thMidiRoutes::resolve (int midi, int note, std::vector<int> &out) const
{
    out.clear();

    if (midi < 0 || midi >= TH_MIDI_CHANNELS || note < 0 || note > 127)
        return;

    const std::vector<thMidiRoute> *layer = deciding(midi);

    if (layer == NULL)
    {
        base(midi, out);
        return;
    }

    for (const thMidiRoute &r : *layer)
        if (r.midi == midi && note >= r.lo && note <= r.hi)
            addOnce(out, r.engine);
}

void
thMidiRoutes::reach (int midi, std::vector<int> &out) const
{
    out.clear();

    if (midi < 0 || midi >= TH_MIDI_CHANNELS)
        return;

    const std::vector<thMidiRoute> *layer = deciding(midi);

    if (layer == NULL)
    {
        base(midi, out);
        return;
    }

    for (const thMidiRoute &r : *layer)
        if (r.midi == midi && std::max(r.lo, 0) <= std::min(r.hi, 127))
            addOnce(out, r.engine);
}
