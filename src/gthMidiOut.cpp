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

#include <stdio.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>

#include <glib.h>
#include <RtMidi.h>

#include "gthMidiOut.h"

const char *const gthMidiOut::PLAY_ON_SYNTH = "@synth";

namespace {

const int CC_EXPRESSION = 11;

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
            /* A device unplugged mid-piece. Said once per message, which
               is noisy, and the alternative is a piece that goes quiet
               with nothing on the terminal to say why. */
            fprintf(stderr, "midi out: %s\n", e.getMessage().c_str());
        }
    }

private:
    RtMidiOut *out_;
};

std::string lower (std::string s)
{
    for (char &c : s)
        c = (char)tolower((unsigned char)c);

    return s;
}

}

std::string
gthMidiOut::stableName (const std::string &name)
{
    /* "Surge XT:Surge XT MIDI In 128:0" -> "Surge XT:Surge XT MIDI In". */
    size_t sp = name.find_last_of(' ');

    if (sp == std::string::npos)
        return name;

    const std::string tail = name.substr(sp + 1);
    const size_t colon = tail.find(':');

    if (colon == std::string::npos || colon == 0 || colon + 1 == tail.size())
        return name;

    for (size_t i = 0; i < tail.size(); i++)
        if (i != colon && !isdigit((unsigned char)tail[i]))
            return name;

    return name.substr(0, sp);
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
    : lister_(lister), opener_(opener)
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
    std::string want = pattern;
    const std::string to = route(pattern);

    if (to == PLAY_ON_SYNTH)
        return "";

    if (!to.empty())
        want = to;

    const std::vector<std::string> names = ports();

    for (const std::string &n : names)
        if (n == want || stableName(n) == want)
            return n;

    for (const std::string &n : names)
        if (n.find(want) != std::string::npos)
            return n;

    const std::string lw = lower(want);

    for (const std::string &n : names)
        if (lower(n).find(lw) != std::string::npos)
            return n;

    return "";
}

void
gthMidiOut::setRoute (const std::string &pattern, const std::string &to)
{
    {
        std::lock_guard<std::mutex> l(lock_);

        if (to.empty())
            patternRoutes_.erase(pattern);
        else
            patternRoutes_[pattern] = to;
    }

    if (changed_)
        changed_();
}

std::string
gthMidiOut::route (const std::string &pattern) const
{
    std::lock_guard<std::mutex> l(lock_);
    auto r = patternRoutes_.find(pattern);

    return r == patternRoutes_.end() ? "" : r->second;
}

std::map<std::string, std::string>
gthMidiOut::routes (void) const
{
    std::lock_guard<std::mutex> l(lock_);

    return patternRoutes_;
}

int
gthMidiOut::openPort (const std::string &name, std::string &why)
{
    for (size_t i = 0; i < portNames_.size(); i++)
        if (portNames_[i] == name)
            return (int)i;

    Port *p = opener_ ? opener_(name, why) : NULL;

    if (p == NULL)
    {
        if (why.empty())
            why = "port '" + name + "' would not open";

        return -1;
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

    Route r;

    r.port = port;
    r.midiChannel = inst.midiChannel & 0x0f;
    r.ccs = inst.ccs;
    routes_[channel] = r;

    /* Now, not delayed: nothing sounds before the first note, and the
       device wants the patch in place by then. */
    if (inst.midiProgram >= 0)
        queue(channel, r, g_get_monotonic_time() - delayUs_, 0xc0,
              (uint8_t)inst.midiProgram, 0, 2);

    return true;
}

void
gthMidiOut::detach (int channel)
{
    flush(channel);

    std::lock_guard<std::mutex> l(lock_);

    routes_.erase(channel);
}

void
gthMidiOut::queue (int channel, const Route &r, gint64 when, uint8_t status,
                   uint8_t d1, uint8_t d2, uint8_t len)
{
    Msg m;

    m.when = when + delayUs_;
    m.seq = seq_++;
    m.channel = channel;
    m.port = r.port;
    m.bytes[0] = (uint8_t)(status | r.midiChannel);
    m.bytes[1] = d1 & 0x7f;
    m.bytes[2] = d2 & 0x7f;
    m.len = len;

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
    auto r = routes_.find(channel);

    if (r == routes_.end() || note < 0 || note > 127)
        return;

    /* The note's level as expression, 1 as 100 -- the MIDI file's
       mapping -- sent ahead of the note wherever it moves. */
    const int expression =
        std::min(127, std::max(0, (int)lrintf(100 * (level > 0 ? level
                                                               : 1))));

    if (expression != r->second.expression)
    {
        queue(channel, r->second, when, 0xb0, CC_EXPRESSION,
              (uint8_t)expression);
        r->second.expression = expression;
    }

    queue(channel, r->second, when, 0x90, (uint8_t)note,
          (uint8_t)std::min(127, std::max(1, velocity)));
}

void
gthMidiOut::noteOff (int channel, int note, gint64 when)
{
    std::lock_guard<std::mutex> l(lock_);
    auto r = routes_.find(channel);

    if (r == routes_.end() || note < 0 || note > 127)
        return;

    queue(channel, r->second, when, 0x80, (uint8_t)note, 64);
}

void
gthMidiOut::control (int channel, const std::string &name, double value,
                     gint64 when)
{
    std::lock_guard<std::mutex> l(lock_);
    auto r = routes_.find(channel);

    if (r == routes_.end())
        return;

    for (const thcMidiCC &cc : r->second.ccs)
    {
        if (cc.name != name)
            continue;

        const double unit =
            std::min(1.0, std::max(0.0, (value - cc.min) / (cc.max - cc.min)));
        const int v = (int)lrint(unit * 127);
        auto last = r->second.sent.find(cc.cc);

        /* A chain that sends the same value every step is not a stream
           the device needs to hear. */
        if (last != r->second.sent.end() && last->second == v)
            return;

        r->second.sent[cc.cc] = v;
        queue(channel, r->second, when, 0xb0, (uint8_t)cc.cc, (uint8_t)v);
        return;
    }
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

    /* ...and what it has sounding ends now. */
    std::vector<std::pair<int, int> > where;

    if (channel < 0)
        for (const auto &r : routes_)
            where.push_back({ r.second.port, r.second.midiChannel });
    else
    {
        auto r = routes_.find(channel);

        if (r != routes_.end())
            where.push_back({ r->second.port, r->second.midiChannel });
    }

    for (auto k = sounding_.begin(); k != sounding_.end(); )
    {
        const std::pair<int, int> pc(std::get<0>(*k), std::get<1>(*k));

        if (std::find(where.begin(), where.end(), pc) == where.end())
        {
            ++k;
            continue;
        }

        const uint8_t off[3] = { (uint8_t)(0x80 | pc.second),
                                 (uint8_t)std::get<2>(*k), 64 };

        ports_[pc.first]->send(off, 3);
        k = sounding_.erase(k);
    }

    /* Whatever the device heard last is still what it has; a route
       starting again sends expression and controllers afresh. */
    for (auto &r : routes_)
        if (channel < 0 || r.first == channel)
        {
            r.second.expression = -1;
            r.second.sent.clear();
        }
}

std::string
gthMidiOut::routeOf (int channel) const
{
    std::lock_guard<std::mutex> l(lock_);
    auto r = routes_.find(channel);

    if (r == routes_.end())
        return "";

    return stableName(portNames_[r->second.port]) + ", channel " +
           std::to_string(r->second.midiChannel + 1);
}

void
gthMidiOut::sendNow (const Msg &m)
{
    Port *port = ports_[m.port].get();
    const uint8_t status = m.bytes[0] & 0xf0;
    const auto key = std::make_tuple(m.port, m.bytes[0] & 0x0f,
                                     (int)m.bytes[1]);

    if (status == 0x90)
    {
        /* One voice per key: a retrigger ends the note it replaces. */
        if (sounding_.count(key))
        {
            const uint8_t off[3] = { (uint8_t)(0x80 | (m.bytes[0] & 0x0f)),
                                     m.bytes[1], 64 };

            port->send(off, 3);
        }

        sounding_.insert(key);
    }
    else if (status == 0x80)
    {
        if (!sounding_.count(key))
            return;

        sounding_.erase(key);
    }

    port->send(m.bytes, m.len);
}

void
gthMidiOut::run (void)
{
    std::unique_lock<std::mutex> l(lock_);

    while (!quit_)
    {
        if (heap_.empty())
        {
            wake_.wait(l);
            continue;
        }

        const gint64 now = g_get_monotonic_time();
        const gint64 due = heap_.front().when;

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
}
