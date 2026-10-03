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

#include <stdio.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>

#include <glib.h>
#include <RtMidi.h>

#include "gthMidiOut.h"

/* Last, and lean: windows.h defines macros (ERROR among them) that break
   glibmm's headers if it comes before them. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#endif

#ifdef __APPLE__
#include <pthread.h>
#endif

const char *const gthMidiOut::PLAY_ON_SYNTH = thcMidiRouter::PLAY_ON_SYNTH;

namespace {

RtMidi::Api apiByName (const std::string &api)
{
    return api.empty() ? RtMidi::UNSPECIFIED
                       : RtMidi::getCompiledApiByName(api);
}

class RtPort : public gthMidiOut::Port
{
public:
    explicit RtPort (RtMidiOut *out) : out_(out) {}
    ~RtPort (void) { delete out_; }

    void send (const uint8_t *msg, size_t len) override
    {
        try
        {
            out_->sendMessage(msg, len);
        }
        catch (RtMidiError &e)
        {
            /* A device unplugged mid-piece. Said once, and the port is
               opened again by the next attach -- a reroute, a reload. */
            if (!dead_)
                fprintf(stderr, "midi out: %s\n", e.getMessage().c_str());

            dead_ = true;
        }
    }

    bool dead (void) const override { return dead_; }

private:
    RtMidiOut *out_;
    bool       dead_ = false;
};

}

std::string
gthMidiOut::stableName (const std::string &name)
{
    return thcMidiRouter::stableName(name);
}

std::vector<std::string>
gthMidiOut::probePorts (const std::string &clientName, const std::string &api)
{
    std::vector<std::string> names;

    try
    {
        RtMidiOut out(apiByName(api), clientName);

        for (unsigned int i = 0; i < out.getPortCount(); i++)
            names.push_back(out.getPortName(i));
    }
    catch (RtMidiError &e)
    {
        fprintf(stderr, "midi out: %s\n", e.getMessage().c_str());
    }

    return names;
}

gthMidiOut::gthMidiOut (const std::string &clientName, const std::string &api)
    : router_([this](const thcMidiRouter::Msg &m) { queue(m); })
{
    lister_ = [clientName, api] { return probePorts(clientName, api); };

    opener_ = [clientName, api](const std::string &name,
                                std::string &why) -> Port *
    {
        try
        {
            RtMidiOut *out = new RtMidiOut(apiByName(api), clientName);

            for (unsigned int i = 0; i < out->getPortCount(); i++)
                if (out->getPortName(i) == name)
                {
                    out->openPort(i, clientName);
                    return new RtPort(out);
                }

            delete out;
            why = "port '" + name + "' went away";
        }
        catch (RtMidiError &e)
        {
            why = e.getMessage();
        }

        return NULL;
    };

    start();
}

gthMidiOut::gthMidiOut (const Lister &lister, const Opener &opener)
    : lister_(lister), opener_(opener),
      router_([this](const thcMidiRouter::Msg &m) { queue(m); })
{
    start();
}

gthMidiOut::~gthMidiOut (void)
{
    flush(-1);

    {
        std::lock_guard<std::mutex> l(lock_);

        quit_ = true;
    }

    wake_.notify_all();
    thread_.join();
}

void
gthMidiOut::start (void)
{
    thread_ = std::thread([this] { run(); });
}

void
gthMidiOut::setDelay (int ms)
{
    {
        std::lock_guard<std::mutex> l(lock_);

        delayUs_ = (gint64)std::max(0, ms) * 1000;
    }

    /* What the thread is waiting for is due at a different time now. */
    wake_.notify_all();

    if (changed_)
        changed_();
}

std::vector<std::string>
gthMidiOut::ports (void) const
{
    return lister_ ? lister_() : std::vector<std::string>();
}

std::string
gthMidiOut::resolve (const std::string &pattern) const
{
    const std::vector<std::string> names = ports();
    std::lock_guard<std::mutex> l(lock_);
    const int at = router_.resolve(pattern, names);

    return at < 0 ? "" : names[at];
}

void
gthMidiOut::setRoute (const std::string &pattern, const std::string &to)
{
    {
        std::lock_guard<std::mutex> l(lock_);

        router_.setRoute(pattern, to);
    }

    if (changed_)
        changed_();
}

std::string
gthMidiOut::route (const std::string &pattern) const
{
    std::lock_guard<std::mutex> l(lock_);

    return router_.route(pattern);
}

std::map<std::string, std::string>
gthMidiOut::routes (void) const
{
    std::lock_guard<std::mutex> l(lock_);

    return router_.routes();
}

int
gthMidiOut::openPort (const std::string &name, std::string &why)
{
    int at = -1;

    for (size_t i = 0; i < portNames_.size(); i++)
        if (portNames_[i] == name)
            at = (int)i;

    if (at >= 0 && !ports_[at]->dead())
        return at;

    Port *p = opener_ ? opener_(name, why) : NULL;

    if (p == NULL)
    {
        if (why.empty())
            why = "port '" + name + "' would not open";

        return -1;
    }

    /* A dead port is replaced where it stood, so the routes and keys
       that name its index go on naming it. What it held died with the
       connection. */
    if (at >= 0)
    {
        ports_[at].reset(p);

        for (auto k = sounding_.begin(); k != sounding_.end(); )
            k = std::get<0>(k->first) == at ? sounding_.erase(k) : ++k;

        return at;
    }

    portNames_.push_back(name);
    ports_.emplace_back(p);
    return (int)ports_.size() - 1;
}

bool
gthMidiOut::attach (int channel, const thcInstrument &inst, std::string &why)
{
    if (route(inst.midi) == PLAY_ON_SYNTH)
    {
        why = "set to play on this synth";
        return false;
    }

    const std::string name = resolve(inst.midi);

    if (name.empty())
    {
        why = "no MIDI output port matches '" + inst.midi + "'";
        return false;
    }

    std::lock_guard<std::mutex> l(lock_);

    const int port = openPort(name, why);

    if (port < 0)
        return false;

    /* Stamped back by the delay, so the program change goes now. */
    router_.attach(channel, port, inst, g_get_monotonic_time() - delayUs_);
    return true;
}

void
gthMidiOut::detach (int channel)
{
    flush(channel);

    std::lock_guard<std::mutex> l(lock_);

    router_.detach(channel);
}

/* With lock_ held: the router emits from inside the calls below. */
void
gthMidiOut::queue (const thcMidiRouter::Msg &r)
{
    Msg m;

    m.when = r.when;
    m.seq = seq_++;
    m.channel = r.channel;
    m.port = r.port;
    m.bytes[0] = r.bytes[0];
    m.bytes[1] = r.bytes[1];
    m.bytes[2] = r.bytes[2];
    m.len = r.len;

    const bool first = heap_.empty() || Later()(heap_.front(), m);

    heap_.push_back(m);
    std::push_heap(heap_.begin(), heap_.end(), Later());

    /* Only an event due before the one the thread is waiting for moves
       its wake-up. */
    if (first)
        wake_.notify_one();
}

void
gthMidiOut::noteOn (int channel, int note, int velocity, float level,
                    gint64 when)
{
    std::lock_guard<std::mutex> l(lock_);

    router_.noteOn(channel, note, velocity, level, when);
}

void
gthMidiOut::noteOff (int channel, int note, gint64 when)
{
    std::lock_guard<std::mutex> l(lock_);

    router_.noteOff(channel, note, when);
}

void
gthMidiOut::control (int channel, const std::string &name, double value,
                     gint64 when)
{
    std::lock_guard<std::mutex> l(lock_);

    router_.control(channel, name, value, when);
}

void
gthMidiOut::flush (int channel)
{
    std::lock_guard<std::mutex> l(lock_);

    /* Nothing queued for the channel goes out any more... */
    heap_.erase(std::remove_if(heap_.begin(), heap_.end(),
                               [channel](const Msg &m)
                               {
                                   return channel < 0 ||
                                          m.channel == channel;
                               }),
                heap_.end());
    std::make_heap(heap_.begin(), heap_.end(), Later());

    /* ...and what it has sounding ends now: its own notes, not another
       channel's on the same device channel. */
    for (auto k = sounding_.begin(); k != sounding_.end(); )
    {
        if (channel >= 0 && k->second != channel)
        {
            ++k;
            continue;
        }

        const uint8_t off[3] = {
            (uint8_t)(0x80 | std::get<1>(k->first)),
            (uint8_t)std::get<2>(k->first), 64 };

        ports_[std::get<0>(k->first)]->send(off, 3);
        k = sounding_.erase(k);
    }

    /* Whatever the device heard last is still what it has; a route
       starting again sends expression and controllers afresh. */
    router_.forget(channel, g_get_monotonic_time() - delayUs_);
}

std::string
gthMidiOut::routeOf (int channel) const
{
    std::lock_guard<std::mutex> l(lock_);
    const int port = router_.portOf(channel);

    if (port < 0)
        return "";

    return stableName(portNames_[port]) + ", channel " +
           std::to_string(router_.midiChannelOf(channel) + 1);
}

void
gthMidiOut::setClockPorts (const std::vector<std::string> &names)
{
    const std::vector<std::string> all = ports();

    {
        std::lock_guard<std::mutex> l(lock_);

        clockNames_ = names;
        clockPorts_.clear();

        for (const std::string &want : names)
            for (const std::string &name : all)
                if (name == want || stableName(name) == want)
                {
                    std::string why;
                    const int port = openPort(name, why);

                    if (port >= 0)
                        clockPorts_.push_back(port);

                    break;
                }
    }

    if (changed_)
        changed_();
}

std::vector<std::string>
gthMidiOut::clockPorts (void) const
{
    std::lock_guard<std::mutex> l(lock_);

    return clockNames_;
}

bool
gthMidiOut::wantsClock (void) const
{
    std::lock_guard<std::mutex> l(lock_);

    return !clockPorts_.empty();
}

/* To every clock port, at its stamp plus the delay, as a note goes: the
   clock and the notes it times have to arrive lined up. Channel -2, which
   no flush of an engine channel drops. */
void
gthMidiOut::clock (int kind, int position, gint64 when)
{
    uint8_t bytes[3] = { 0, 0, 0 };
    const int len = thcMidiRouter::clockBytes(kind, position, bytes);
    std::lock_guard<std::mutex> l(lock_);

    if (len == 0)
        return;

    for (int port : clockPorts_)
    {
        thcMidiRouter::Msg m;

        m.when = when;
        m.channel = -2;
        m.port = port;
        m.bytes[0] = bytes[0];
        m.bytes[1] = bytes[1];
        m.bytes[2] = bytes[2];
        m.len = (uint8_t)len;
        queue(m);
    }
}

void
gthMidiOut::sendNow (const Msg &m)
{
    Port *port = ports_[m.port].get();
    const uint8_t status = m.bytes[0] & 0xf0;

    /* System real-time and common messages: no channel, no key. */
    if (m.bytes[0] >= 0xf0)
    {
        port->send(m.bytes, m.len);
        return;
    }
    const auto key = std::make_tuple(m.port, m.bytes[0] & 0x0f,
                                     (int)m.bytes[1]);

    if (status == 0x90)
    {
        /* One voice per key: a retrigger ends the note it replaces,
           whichever engine channel struck that one, and the key is this
           channel's now. */
        if (sounding_.count(key))
        {
            const uint8_t off[3] = { (uint8_t)(0x80 | (m.bytes[0] & 0x0f)),
                                     m.bytes[1], 64 };

            port->send(off, 3);
        }

        sounding_[key] = m.channel;
    }
    else if (status == 0x80)
    {
        /* Only the note this channel struck: an off for a key another
           channel has since taken over would end that one. */
        auto held = sounding_.find(key);

        if (held == sounding_.end() || held->second != m.channel)
            return;

        sounding_.erase(held);
    }

    port->send(m.bytes, m.len);
}

void
gthMidiOut::run (void)
{
#ifdef _WIN32
    /* Windows wakes a waiting thread on its timer tick, 15.6 ms by
       default, which is the jitter this thread exists to take out. One
       millisecond while it runs. */
    timeBeginPeriod(1);
#endif

#ifdef __APPLE__
    /* macOS coalesces the timers of a thread at the default QoS class,
       and a wake-up can come a hundred milliseconds late; a thread whose
       whole job is being on time asks not to be. */
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif

    std::unique_lock<std::mutex> l(lock_);

    while (!quit_)
    {
        if (heap_.empty())
        {
            wake_.wait(l);
            continue;
        }

        const gint64 now = g_get_monotonic_time();
        const gint64 due = heap_.front().when + delayUs_;

        if (due > now)
        {
            wake_.wait_for(l, std::chrono::microseconds(due - now));
            continue;
        }

        std::pop_heap(heap_.begin(), heap_.end(), Later());
        const Msg m = heap_.back();
        heap_.pop_back();

        sendNow(m);
    }

#ifdef _WIN32
    timeEndPeriod(1);
#endif
}
