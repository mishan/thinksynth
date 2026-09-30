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

#ifndef GTH_MIDIOUT_H
#define GTH_MIDIOUT_H 1

#include <stdint.h>

#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "thcScheduler.h"

/* MIDI output: the application's thcMidiOut.
 *
 * The scheduler delivers on a 20 ms timer, so a note reaches this up to a
 * step after it is due, and a step late by a different amount each time.
 * Sent as it arrives, a straight sixteenth line would come out of the
 * device with that jitter in it. Instead every event carries the monotonic
 * time it is due (thcMidiOut), and a thread of its own sends it at that
 * time plus a fixed delay. The delay has to cover the step, and it is also
 * what lines the device up with the synth, whose notes are heard a window
 * or two after they are delivered: 40 ms by default, set by the person
 * listening (setDelay).
 *
 * MIDI has one voice per key per channel. A note-on for a key already
 * sounding on the device sends that key's note-off first; a note-off for a
 * key not sounding is not sent. So the device's picture of what is held is
 * the one kept here, and flush() can end exactly that.
 *
 * Ports are matched by name: an instrument's `midi' pattern is compared
 * with each output port's name, exactly, then as a substring, then as a
 * substring ignoring case. A per-machine route (setRoute) can replace the
 * pattern with a port's name, or with PLAY_ON_SYNTH to keep the instrument
 * on its dsp; the host keeps the routes across sessions (onChanged).
 * The trailing "client:port" numbers ALSA puts on a name change between
 * sessions, so a route names a port by its stableName().
 */
class gthMidiOut : public thcMidiOut
{
public:
    /* A device messages go to. RtMidiOut in the application; a harness
       gives its own and reads back what was sent. send() is called on the
       sending thread. */
    class Port
    {
    public:
        virtual ~Port (void) {}
        virtual void send (const uint8_t *msg, size_t len) = 0;
    };

    typedef std::function<std::vector<std::string> (void)> Lister;
    typedef std::function<Port *(const std::string &name,
                                 std::string &why)> Opener;

    static const char *const PLAY_ON_SYNTH;

    /* Through RtMidi, as `clientName', on `api' (empty: RtMidi's
       choice). */
    explicit gthMidiOut (const std::string &clientName,
                         const std::string &api = "");

    /* Through a harness's ports. */
    gthMidiOut (const Lister &lister, const Opener &opener);

    ~gthMidiOut (void);

    void setDelay (int ms);
    int  delay (void) const { return (int)(delayUs_ / 1000); }

    /* Where an instrument naming `pattern' plays on this machine: a port
       by name, PLAY_ON_SYNTH, or empty for the pattern's own match. */
    void setRoute (const std::string &pattern, const std::string &to);
    std::string route (const std::string &pattern) const;
    std::map<std::string, std::string> routes (void) const;

    /* Called after setRoute or setDelay, for the host to save them. */
    void onChanged (const std::function<void (void)> &fn)
    {
        changed_ = fn;
    }

    std::vector<std::string> ports (void) const;

    /* The port `pattern' answers to, override included; empty where none
       does or it is set to play on the synth. */
    std::string resolve (const std::string &pattern) const;

    /* A port name without ALSA's trailing " 128:0". */
    static std::string stableName (const std::string &name);

    /* Every output port RtMidi sees, for -L, without opening one. */
    static std::vector<std::string> probePorts (const std::string &clientName,
                                                const std::string &api = "");

    /* thcMidiOut */
    bool attach (int channel, const thcInstrument &inst,
                 std::string &why) override;
    void detach (int channel) override;
    void noteOn (int channel, int note, int velocity, float level,
                 gint64 when) override;
    void noteOff (int channel, int note, gint64 when) override;
    void control (int channel, const std::string &name, double value,
                  gint64 when) override;
    void flush (int channel) override;

    /* Where engine channel `channel' is sent, "port name, channel N", or
       empty where it is not attached. */
    std::string routeOf (int channel) const;

private:
    struct Route
    {
        int port;                       /* into ports_                    */
        int midiChannel;
        std::vector<thcMidiCC> ccs;
        int expression = -1;            /* last CC 11 queued              */
        std::map<int, int> sent;        /* cc -> last value queued        */
    };

    struct Msg
    {
        gint64        when;
        unsigned long seq;
        int           channel;          /* engine channel, for flush      */
        int           port;
        uint8_t       bytes[3];
        uint8_t       len;
    };

    struct Later
    {
        bool operator() (const Msg &a, const Msg &b) const
        {
            return a.when != b.when ? a.when > b.when : a.seq > b.seq;
        }
    };

    void start (void);
    void run (void);
    void queue (int channel, const Route &r, gint64 when, uint8_t status,
                uint8_t d1, uint8_t d2, uint8_t len = 3);
    void sendNow (const Msg &m);        /* with lock_ held                */
    int  openPort (const std::string &name, std::string &why);

    Lister   lister_;
    Opener   opener_;
    gint64   delayUs_ = 40000;

    std::map<std::string, std::string> patternRoutes_;
    std::function<void (void)>         changed_;

    mutable std::mutex      lock_;
    std::condition_variable wake_;
    std::thread             thread_;
    bool                    quit_ = false;
    unsigned long           seq_ = 0;

    std::vector<Msg> heap_;
    std::map<int, Route> routes_;

    std::vector<std::string>           portNames_;
    std::vector<std::unique_ptr<Port> > ports_;

    /* (port, MIDI channel, key) sounding on a device. */
    std::set<std::tuple<int, int, int> > sounding_;
};

#endif /* GTH_MIDIOUT_H */
