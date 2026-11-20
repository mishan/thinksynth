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

#include <functional>

#include "thhPlay.h"

void
thhPlay (thSynth &synth, const std::vector<thhAction> &acts,
         const std::function<void (const thhAction &)> &heard)
{
    for (size_t i = 0; i < acts.size(); i++)
    {
        const thhAction &a = acts[i];

        if (a.kind == thhAction::NOTE_ON)
            synth.addNote(a.channel, a.note, a.vel);
        else if (a.kind == thhAction::NOTE_OFF)
            synth.delNote(a.channel, a.note);
        else if (a.kind == thhAction::NOTE_BEND)
        {
            synth.bendNote(a.channel, a.note, (float)a.t);
            continue;
        }
        else if (a.kind == thhAction::NOTE_AUX)
        {
            synth.setNoteAux(a.channel, a.note, a.aux, (float)a.t);
            continue;
        }
        else if (a.kind == thhAction::LOOP)
            continue;
        else
        {
            synth.setChanArgFraction(a.channel, a.arg, a.t,
                                     a.hasMin ? &a.min : NULL,
                                     a.hasMax ? &a.max : NULL, a.exp);
            continue;
        }

        if (heard)
            heard(a);
    }
}
