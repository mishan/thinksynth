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

#ifndef GTH_MIDI_PLAY_H
#define GTH_MIDI_PLAY_H

#include <functional>
#include <set>
#include <vector>

#include "think.h"
#include "thMidiRoutes.h"

class thSynth;

/* MIDI from a port played on the synth: notes, a channel's pitch bend on the
   notes it holds, and, from an MPE controller, each note's own bend,
   pressure and timbre.

   Without MPE a MIDI channel plays the engine channels routes() sends its
   notes to -- with no routes set, the one of its number -- and its bend
   wheel bends what it holds, two semitones either way. Each note keeps
   the engine channels its note-on went to: its off, a second on for its
   key, and its bend, pressure and timbre go there, whatever the routes
   say by then. The sustain pedal goes down on every engine channel its
   MIDI channel reaches and comes up on every one it went down on that no
   other MIDI channel's pedal still holds. With MPE
   on (setMpe), the lower zone is folded onto one synth channel: channel 1
   is its master, whose bend bends every note, and each of 2-16 carries one
   note whose own bend (48 semitones either way unless the controller says
   otherwise, by RPN 0, for the whole zone), pressure (aux0) and CC 74
   (aux1) are that note's. Two members on one pitch are one voice, the
   later one's: the earlier's bend and off leave it be. MPE outranks the
   routes: with it on, every note goes to the one synth channel.

   No toolkit. On the thread MIDI is dispatched on, the GUI's. */
class gthMidiPlay
{
public:
    explicit gthMidiPlay (thSynth *synth);

    /* The synth channel MPE folds onto; -1 for no MPE. */
    void setMpe (int channel);

    /* Where notes go without MPE: identity until a layer is set. */
    thMidiRoutes &routes (void) { return routes_; }

    /* Each note played and let go, on the synth channel it played on. */
    std::function<void (int channel, int note, int vel)> noteOn;
    std::function<void (int channel, int note)> noteOff;

    void play (unsigned char status, unsigned char data1,
               unsigned char data2);

private:
    struct Chan
    {
        float            bend, range, pressure, timbre;
        int              rpn[2];        /* CC 101, CC 100 */
        bool             nrpn;          /* CC 99 or 98 since */
        std::vector<int> held;
    };

    thSynth *synth_;
    int      mpe_;
    Chan     chans_[16];

    /* Which MIDI channel each note sounding on each synth channel is from:
       two MPE members on one pitch are one voice, the later one's. */
    int      owner_[TH_ENGINE_CHANNELS][128];

    thMidiRoutes routes_;

    /* The engine channels each held note's on went to, by MIDI channel
       and key; and the MIDI channels whose pedal holds each engine
       channel's down. */
    std::vector<int> targets_[16][128];
    std::set<int>    pedalBy_[TH_ENGINE_CHANNELS];

    void ranges ();

    /* MIDI Map's key for a controller from `midiChannel': the channel it
       is applied on, the MPE target with MPE. */
    int mapChannel (int midiChannel) const;
    void strike (int midiChannel, int note, int velocity);
    void letGo (int midiChannel, int note);
    void sustain (int midiChannel, int value);
    float bendOf (int midiChannel) const;
    void express (int midiChannel, bool bend, bool pressure, bool timbre);
};

#endif /* GTH_MIDI_PLAY_H */
