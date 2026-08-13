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

#ifndef TH_MIDICONTROLLER_H
#define TH_MIDICONTROLLER_H 1

#include "thExport.h"

/* The thMidiController class has an array of 16 * 128 pointers to midi
   controller event handlers.  A thMidiControllerConnection has a pointer to
   the thArg to modify, and some information like min/max value, exponential/
   linear, etc.  Optionally, the thMidiControllerConnection can have
   instructions to the synth to handle a global operation such as portamento,
   foot pedal and RPN/NRPN parameter handling.  The thMidiControllerConnection
   class also has a pointer to optional addition thMidiControllerConnection
   classes, so one midi controller can control multiple parameters. */

class THINK_API thMidiController {
public:
    thMidiController (void);
    ~thMidiController (void);

    typedef map<unsigned int, thMidiControllerConnection *> ConnectionMap;

    void handleMidi (unsigned char channel, unsigned int param,
                     unsigned int value);

    void newConnection (unsigned char channel, unsigned int param,
                        thMidiControllerConnection *connection);
    void clearByDestChan (unsigned int chan);

    ConnectionMap *connectionMap (void) { return &connectionMap_; }

    /* Off the wire both of these are in range by construction: a channel is
       the low nibble of a status byte and a controller number is seven bits.
       They do not all come off the wire. MidiMap builds connections from spin
       buttons, and a restored .thinkrc carries whatever was in the file. An
       out-of-range subscript into a 2048-pointer member array is a silent read
       or write past the end of the object, so it is checked here rather than
       assumed at each of the four call sites. */
    static bool validAddress (unsigned int channel, unsigned int param)
    {
        return channel < TH_MIDI_CHANNELS && param < TH_MIDI_CONTROLLERS;
    }

    thMidiControllerConnection *getConnection (unsigned char channel,
                                               unsigned int param)
    {
        if (!validAddress(channel, param))
            return NULL;

        return connections_[channel][param];
    }

private:
    thMidiControllerConnection *connections_[TH_MIDI_CHANNELS]
                                            [TH_MIDI_CONTROLLERS];
    ConnectionMap connectionMap_;
};

#endif /* TH_MIDICONTROLLER_H */
