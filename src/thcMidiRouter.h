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
 */

#ifndef THC_MIDIROUTER_H
#define THC_MIDIROUTER_H 1

#include <stdint.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "thcScheduler.h"

/* What an instrument played over MIDI turns into on the wire, with no
 * port, no thread and no toolkit: the half of a thcMidiOut the desktop
 * and the browser share.
 *
 * It knows which port an instrument's `midi' pattern resolves to among
 * the names it is given -- exactly, then as a substring, then ignoring
 * case, with a per-machine route (setRoute) in front -- and, per engine
 * channel, the route attach() set up: the port's index, the device's
 * channel, the `cc' mappings. noteOn, noteOff and control become MIDI
 * messages handed to the Emit given at construction, each with the stamp
 * it came with: a note's level as CC 11 (1 is 100) ahead of the note
 * where it moves, a chanarg scaled onto 0..127 -- or onto the pitch
 * wheel's 14 bits, for a `bend' -- and not repeated, a program change on
 * attach.
 *
 * What it does not do is send, so it keeps no account of what a device
 * holds: that is the sender's, which alone knows what has gone out. Not
 * thread-safe; the owner locks.
 */
class thcMidiRouter
{
public:
    struct Msg
    {
        gint64  when;
        int     channel;            /* engine channel                     */
        int     port;               /* index into the names attach saw    */
        uint8_t bytes[3];
        uint8_t len;
    };

    typedef std::function<void (const Msg &)> Emit;

    /* A route that keeps an instrument on its dsp. */
    static const char *const PLAY_ON_SYNTH;

    /* A port name without ALSA's trailing " 128:0". */
    static std::string stableName (const std::string &name);

    /* A thcMidiOut clock message's bytes: F8 tick, FA start, FB continue,
       FC stop, F2 and two 7-bit halves for Song Position. Returns how
       many, 0 for a kind that is none of them. */
    static int clockBytes (int kind, int position, uint8_t out[3]);

    explicit thcMidiRouter (const Emit &emit) : emit_(emit) {}

    /* Where an instrument naming `pattern' plays on this machine: a port
       by name, PLAY_ON_SYNTH, or empty for the pattern's own match. */
    void setRoute (const std::string &pattern, const std::string &to);
    std::string route (const std::string &pattern) const;
    const std::map<std::string, std::string> &routes (void) const
    {
        return patternRoutes_;
    }

    /* The index in `names' that `pattern' answers to, route included; -1
       where none does or the route keeps it on the synth. */
    int resolve (const std::string &pattern,
                 const std::vector<std::string> &names) const;

    /* Engine channel `channel' plays `inst' on port `port', from `now':
       the program change, if it names one, goes then. */
    void attach (int channel, int port, const thcInstrument &inst,
                 gint64 now);
    void detach (int channel);

    /* The same route, at a new index: the port list was made again and
       the device is still in it. */
    void renumber (int channel, int port);
    bool attached (int channel) const { return routes_.count(channel) > 0; }
    int  portOf (int channel) const;
    int  midiChannelOf (int channel) const;

    void noteOn (int channel, int note, int velocity, float level,
                 gint64 when);
    void noteOff (int channel, int note, gint64 when);
    void control (int channel, const std::string &name, double value,
                  gint64 when);

    /* After a flush the device's expression and controllers are what it
       last heard, and the next note or value sends them again; a pitch
       wheel bent away from center is centered, stamped `now'. -1 is every
       channel. */
    void forget (int channel, gint64 now);

private:
    struct Route
    {
        int port;
        int midiChannel;
        std::vector<thcMidiCC> ccs;
        int expression = -1;            /* last CC 11 emitted             */
        std::map<int, int> sent;        /* cc -> last value emitted       */
    };

    void emit (int channel, const Route &r, gint64 when, uint8_t status,
               uint8_t d1, uint8_t d2, uint8_t len = 3);

    Emit                               emit_;
    std::map<int, Route>               routes_;
    std::map<std::string, std::string> patternRoutes_;
};

#endif /* THC_MIDIROUTER_H */
