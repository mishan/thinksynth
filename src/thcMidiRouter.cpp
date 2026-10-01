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

#include <algorithm>
#include <cctype>
#include <cmath>

#include "thcMidiRouter.h"

const char *const thcMidiRouter::PLAY_ON_SYNTH = "@synth";

namespace {

const int CC_EXPRESSION = 11;

std::string lower (std::string s)
{
    for (char &c : s)
        c = (char)tolower((unsigned char)c);

    return s;
}

}

std::string
thcMidiRouter::stableName (const std::string &name)
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

void
thcMidiRouter::setRoute (const std::string &pattern, const std::string &to)
{
    if (to.empty())
        patternRoutes_.erase(pattern);
    else
        patternRoutes_[pattern] = to;
}

std::string
thcMidiRouter::route (const std::string &pattern) const
{
    auto r = patternRoutes_.find(pattern);

    return r == patternRoutes_.end() ? "" : r->second;
}

int
thcMidiRouter::resolve (const std::string &pattern,
                        const std::vector<std::string> &names) const
{
    std::string want = pattern;
    const std::string to = route(pattern);

    if (to == PLAY_ON_SYNTH)
        return -1;

    if (!to.empty())
        want = to;

    for (size_t i = 0; i < names.size(); i++)
        if (names[i] == want || stableName(names[i]) == want)
            return (int)i;

    for (size_t i = 0; i < names.size(); i++)
        if (names[i].find(want) != std::string::npos)
            return (int)i;

    const std::string lw = lower(want);

    for (size_t i = 0; i < names.size(); i++)
        if (lower(names[i]).find(lw) != std::string::npos)
            return (int)i;

    return -1;
}

void
thcMidiRouter::attach (int channel, int port, const thcInstrument &inst,
                       gint64 now)
{
    Route r;

    r.port = port;
    r.midiChannel = inst.midiChannel & 0x0f;
    r.ccs = inst.ccs;
    routes_[channel] = r;

    /* Now: nothing sounds before the first note, and the device wants the
       patch in place by then. */
    if (inst.midiProgram >= 0)
        emit(channel, r, now, 0xc0, (uint8_t)inst.midiProgram, 0, 2);
}

void
thcMidiRouter::detach (int channel)
{
    routes_.erase(channel);
}

int
thcMidiRouter::portOf (int channel) const
{
    auto r = routes_.find(channel);

    return r == routes_.end() ? -1 : r->second.port;
}

int
thcMidiRouter::midiChannelOf (int channel) const
{
    auto r = routes_.find(channel);

    return r == routes_.end() ? -1 : r->second.midiChannel;
}

void
thcMidiRouter::emit (int channel, const Route &r, gint64 when,
                     uint8_t status, uint8_t d1, uint8_t d2, uint8_t len)
{
    Msg m;

    m.when = when;
    m.channel = channel;
    m.port = r.port;
    m.bytes[0] = (uint8_t)(status | r.midiChannel);
    m.bytes[1] = d1 & 0x7f;
    m.bytes[2] = d2 & 0x7f;
    m.len = len;

    emit_(m);
}

void
thcMidiRouter::noteOn (int channel, int note, int velocity, float level,
                       gint64 when)
{
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
        emit(channel, r->second, when, 0xb0, CC_EXPRESSION,
             (uint8_t)expression);
        r->second.expression = expression;
    }

    emit(channel, r->second, when, 0x90, (uint8_t)note,
         (uint8_t)std::min(127, std::max(1, velocity)));
}

void
thcMidiRouter::noteOff (int channel, int note, gint64 when)
{
    auto r = routes_.find(channel);

    if (r == routes_.end() || note < 0 || note > 127)
        return;

    emit(channel, r->second, when, 0x80, (uint8_t)note, 64);
}

void
thcMidiRouter::control (int channel, const std::string &name, double value,
                        gint64 when)
{
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
        emit(channel, r->second, when, 0xb0, (uint8_t)cc.cc, (uint8_t)v);
        return;
    }
}

void
thcMidiRouter::forget (int channel)
{
    for (auto &r : routes_)
        if (channel < 0 || r.first == channel)
        {
            r.second.expression = -1;
            r.second.sent.clear();
        }
}
