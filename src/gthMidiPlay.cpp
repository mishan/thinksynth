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

#include <algorithm>

#include "think.h"
#include "gthMidiPlay.h"

/* MPE's bend ranges unless a controller sets its own: the zone's master
   channel two semitones, each member 48. */
#define GTH_BEND_RANGE 2.0f
#define GTH_MPE_MEMBER_RANGE 48.0f

gthMidiPlay::gthMidiPlay (thSynth *synth) : synth_(synth), mpe_(-1)
{
    setMpe(-1);
}

void gthMidiPlay::setMpe (int channel)
{
    mpe_ = channel;
    for (int c = 0; c < 16; c++)
    {
        Chan &ch = chans_[c];

        ch.bend = ch.pressure = ch.timbre = 0;
        ch.rpn[0] = ch.rpn[1] = 127;
        ch.nrpn = false;
        ch.held.clear();
    }
    for (int c = 0; c < 16; c++)
        for (int n = 0; n < 128; n++)
            owner_[c][n] = -1;
    ranges();
}

/* Each channel's bend range as MPE has it unless a controller says other:
   members 48 semitones, the master and every channel without MPE 2. */
void gthMidiPlay::ranges ()
{
    for (int c = 0; c < 16; c++)
        chans_[c].range = mpe_ >= 0 && c > 0 ? GTH_MPE_MEMBER_RANGE
                                             : GTH_BEND_RANGE;
}

int gthMidiPlay::target (int midiChannel) const
{
    return mpe_ >= 0 ? mpe_ : midiChannel;
}

/* A member's bend is its own and the master's together. */
float gthMidiPlay::bendOf (int midiChannel) const
{
    return chans_[midiChannel].bend +
           (mpe_ >= 0 && midiChannel > 0 ? chans_[0].bend : 0);
}

/* What changed on a MIDI channel, to every note it holds. */
void gthMidiPlay::express (int midiChannel, bool bend, bool pressure,
                           bool timbre)
{
    const Chan &ch = chans_[midiChannel];

    for (size_t i = 0; i < ch.held.size(); i++)
    {
        const int to = target(midiChannel), note = ch.held[i];

        if (owner_[to][note] != midiChannel)
            continue;

        if (bend)
            synth_->bendNote(to, note, bendOf(midiChannel));
        if (pressure)
            synth_->setNoteAux(to, note, 0, ch.pressure);
        if (timbre)
            synth_->setNoteAux(to, note, 1, ch.timbre);
    }
}

void gthMidiPlay::play (unsigned char status, unsigned char data1,
                        unsigned char data2)
{
    const unsigned char kind = status & 0xf0;
    const int c = status & 0x0f;
    Chan &ch = chans_[c];
    const bool mpe = mpe_ >= 0;

    switch (kind)
    {
    case 0x90:
        if (data2)
        {
            if (std::find(ch.held.begin(), ch.held.end(), data1) ==
                ch.held.end())
                ch.held.push_back(data1);
            owner_[target(c)][data1 & 0x7f] = c;
            synth_->addNote(target(c), data1, data2);
            if (noteOn)
                noteOn(target(c), data1, data2);

            /* A note starts where its channel's expression is. */
            if (bendOf(c))
                synth_->bendNote(target(c), data1, bendOf(c));
            if (mpe && ch.pressure)
                synth_->setNoteAux(target(c), data1, 0, ch.pressure);
            if (mpe && ch.timbre)
                synth_->setNoteAux(target(c), data1, 1, ch.timbre);
            break;
        }
        /* a velocity of 0 is an off */
        [[fallthrough]];
    case 0x80:
        ch.held.erase(std::remove(ch.held.begin(), ch.held.end(), data1),
                      ch.held.end());
        if (owner_[target(c)][data1 & 0x7f] != c)
            break;              /* another member's now */
        owner_[target(c)][data1 & 0x7f] = -1;
        synth_->delNote(target(c), data1);
        if (noteOff)
            noteOff(target(c), data1);
        break;

    case 0xe0:
    {
        /* 14 bits, 8192 the middle; each half reaches its end. */
        const int v = (data2 << 7) | data1;

        ch.bend = (v - 8192) / (v >= 8192 ? 8191.0f : 8192.0f) * ch.range;
        if (mpe && c == 0)
            for (int m = 0; m < 16; m++)
                express(m, true, false, false);
        else
            express(c, true, false, false);
        break;
    }

    case 0xd0:
        if (mpe)
        {
            ch.pressure = data1 / 127.0f;
            express(c, false, true, false);
        }
        break;

    case 0xb0:
    {
        /* Data entry (6, 38, increment 96 and decrement 97) is the selected
           parameter's, never a knob's: RPN 0, the bend range, whole for
           an MPE zone; RPN 6, MPE's configuration, which puts the ranges
           back; anything else, and any NRPN, nothing here. */
        const bool entry = data1 == 6 || data1 == 38 || data1 == 96 ||
                           data1 == 97;
        const bool selected = ch.nrpn || ch.rpn[0] != 127 || ch.rpn[1] != 127;

        if (data1 == 101 || data1 == 100)
        {
            ch.rpn[data1 == 101 ? 0 : 1] = data2;
            ch.nrpn = false;
        }
        else if (data1 == 99 || data1 == 98)
            ch.nrpn = true;
        else if (data1 == 6 && !ch.nrpn && ch.rpn[0] == 0 && ch.rpn[1] == 0)
        {
            for (int m = 0; m < 16; m++)
                if (m == c || (mpe && c > 0 && m > 0))
                    chans_[m].range = data2;
        }
        else if (data1 == 6 && !ch.nrpn && ch.rpn[0] == 0 && ch.rpn[1] == 6)
            ranges();
        else if (entry && selected)
            ;
        else if (mpe && data1 == 74)
        {
            ch.timbre = data2 / 127.0f;
            express(c, false, false, true);
        }
        else
            synth_->handleMidiController(target(c), data1, data2);
        break;
    }
    }
}
