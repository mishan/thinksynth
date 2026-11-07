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
#include <vector>

class thSynth;

/* MIDI from a port played on the synth: notes, a channel's pitch bend on the
   notes it holds, and, from an MPE controller, each note's own bend,
   pressure and timbre.

   Without MPE a MIDI channel plays the synth channel of its number, and
   its bend wheel bends what it holds, two semitones either way. With MPE
   on (setMpe), the lower zone is folded onto one synth channel: channel 1
   is its master, whose bend bends every note, and each of 2-16 carries one
   note whose own bend (48 semitones either way unless the controller says
   otherwise, by RPN 0), pressure (aux0) and CC 74 (aux1) are that note's.

   No toolkit. On the thread MIDI is dispatched on, the GUI's. */
class gthMidiPlay
{
public:
    explicit gthMidiPlay (thSynth *synth);

    /* The synth channel, 0-15, MPE folds onto; -1 for no MPE. */
    void setMpe (int channel);

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
        std::vector<int> held;
    };

    thSynth *synth_;
    int      mpe_;
    Chan     chans_[16];

    int target (int midiChannel) const;
    float bendOf (int midiChannel) const;
    void express (int midiChannel, bool bend, bool pressure, bool timbre);
};

#endif /* GTH_MIDI_PLAY_H */
