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

#ifndef TH_MIDIROUTES_H
#define TH_MIDIROUTES_H 1

#include <vector>

#include "thExport.h"

/* MIDI channel `midi' (0-15) to engine channel `engine', for the keys lo to
   hi inclusive; an engine of -1 is `off', which names the MIDI channel and
   sends it nowhere. */
struct thMidiRoute
{
    int midi, engine, lo = 0, hi = 127;
};

/* Which engine channels a live MIDI note goes to.
 *
 * Decided per MIDI channel, so a route owns its MIDI channel: the highest
 * layer that names m decides all of m, machine over piece, and a layer's
 * routes from m that take the key all play it (a layer is two routes, a
 * split two ranges). No layer names m: the base decides, identity (m to
 * engine channel m) or omni (every m to one engine channel). A piece's
 * route from MIDI channel 3 therefore takes 3 away from engine channel 3,
 * and an engine channel past MIDI's sixteen hears only what is routed to
 * it.
 *
 * Pure: no I/O and no locking; its host owns the thread it is used on. */
class THINK_API thMidiRoutes
{
public:
    enum Layer { MACHINE, PIECE, LAYERS };

    void set (Layer layer, const std::vector<thMidiRoute> &routes);
    const std::vector<thMidiRoute> &routes (Layer layer) const
    { return layers_[layer]; }

    void setIdentity (void) { omni_ = -1; }
    void setOmni (int engine) { omni_ = engine; }

    /* The engine channels note `note' on MIDI channel `midi' plays, each
       once, in the order the deciding layer names them; empty for none. */
    void resolve (int midi, int note, std::vector<int> &out) const;

    /* Every engine channel some note on `midi' plays: where a channel-wide
       message from it, the sustain pedal's, goes. */
    void reach (int midi, std::vector<int> &out) const;

private:
    const std::vector<thMidiRoute> *deciding (int midi) const;
    void base (int midi, std::vector<int> &out) const;

    std::vector<thMidiRoute> layers_[LAYERS];
    int omni_ = -1;
};

#endif /* TH_MIDIROUTES_H */
