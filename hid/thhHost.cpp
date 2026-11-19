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

#include <cstdio>

#include "thhHost.h"
#include "thhPane.h"

thhHost::thhHost ()
    : now_(0), channel_(0), next_(1),
      session_([this](const std::vector<thhAction> &acts) {
                   taken_.insert(taken_.end(), acts.begin(), acts.end());
               },
               [this]() { return now_; },
               [this](const std::map<std::string, std::string> &k) {
                   kept_ = k;
               })
{
    std::string err;

    session_.load("", "", "", "", err);
}

int
thhHost::at (int id) const
{
    for (size_t i = 0; i < slots_.size(); i++)
        if (slots_[i].id == id)
            return (int)i;
    return -1;
}

int
thhHost::add (const std::string &hand, const std::string &serial, double now)
{
    Slot s;

    s.id = next_++;
    s.hand = hand;
    s.state.connected = true;
    s.state.time = now_ = now;
    slots_.push_back(s);
    session_.add(serial);
    session_.update((int)slots_.size() - 1, {}, s.state, hand, channel_);
    return s.id;
}

bool
thhHost::report (int id, From from, const uint8_t *data, size_t len,
                 double now)
{
    const int i = at(id);

    if (i < 0)
        return false;

    Slot &s = slots_[i];
    const bool fed =
        from == INPUT ? thhIndexFeed(data, len, s.state)
        : from == IMU ? thhIndexFeedImu(data, len, s.imu, s.state)
                      : thhIndexFeedWireless(data, len, s.wireless, s.state);

    if (!fed)
        return false;
    s.state.time = now_ = now;
    session_.update(i, {}, s.state, s.hand, channel_);
    return !s.state.connected;
}

void
thhHost::drop (int id, double now)
{
    const int i = at(id);

    if (i < 0)
        return;
    slots_[i].state = thhState();
    slots_[i].state.time = now_ = now;
    session_.update(i, {}, slots_[i].state, slots_[i].hand, channel_);
    session_.remove(i);
    slots_.erase(slots_.begin() + i);
}

std::string
thhHost::profile (const std::string &text)
{
    std::string err;

    profile_ = text;
    session_.load(profile_, "your profile", block_, name_, err);
    return err;
}

std::string
thhHost::patch (const std::string &block, const std::string &name,
                const std::set<std::string> &args)
{
    std::string err;

    args_ = args;
    name_ = name;
    if (block == block_)
    {
        session_.rename(name);
        return "";
    }
    block_ = block;
    session_.load(profile_, "your profile", block_, name_, err);
    return err;
}

void
thhHost::remembered (const std::map<std::string, std::string> &hands)
{
    kept_ = hands;
    session_.remembered(hands);
}

bool
thhHost::tick (double now)
{
    now_ = now;
    return session_.tick();
}

void
thhHost::release (double now)
{
    now_ = now;
    session_.release();
}

/* A string in JSON: an arg's name, a hand, a pane's text. */
static std::string
quoted (const std::string &s)
{
    std::string out = "\"";

    for (char c : s)
        if (c == '"' || c == '\\')
            out += std::string("\\") + c;
        else if ((unsigned char)c < 0x20)
        {
            char u[8];

            snprintf(u, sizeof(u), "\\u%04x", (unsigned char)c);
            out += u;
        }
        else
            out += c;
    return out + "\"";
}

std::string
thhHost::pane () const
{
    const thhSession &s = session_;
    std::string hands, rows;
    const auto has = [this](int ch, const std::string &arg) {
        return ch != channel_ || args_.count(arg) > 0;
    };

    for (int i = 0; i < s.controllers(); i++)
    {
        const thhState now = s.stateOf(i);
        const std::string hand = s.handOf(i);

        if (!now.connected)
            continue;
        if (!hand.empty())
            hands += (hands.empty() ? "" : ",") + quoted(hand);
        for (const thhPaneRow &r : thhPaneRows(s.profileOf(i),
                                               thhIndexControls(), now,
                                               channel_, has))
            rows += std::string(rows.empty() ? "" : ",") + "{\"layer\":" +
                    quoted(hand + (hand.empty() || r.layer.empty() ? "" : " ") +
                           r.layer) +
                    ",\"control\":" + quoted(r.control) +
                    ",\"action\":" + quoted(r.action) +
                    ",\"target\":" + quoted(r.target) +
                    ",\"value\":" + quoted(r.value) +
                    ",\"warning\":" + quoted(r.warning) +
                    ",\"yours\":" + (r.yours ? "true" : "false") +
                    ",\"patch\":" + (r.patch ? "true" : "false") + "}";
    }
    return "{\"hands\":[" + hands + "],\"rows\":[" + rows +
           "],\"canSwap\":" + (s.canSwapHands() ? "true" : "false") +
           ",\"loops\":" + quoted(s.loops()) +
           ",\"patch\":" + quoted(s.patch()) + "}";
}

std::string
thhHost::take ()
{
    static const char *const KIND[] = { "on", "off", "value", "bend", "aux",
                                        "loop" };
    std::string out = "[";
    char num[160];

    for (const thhAction &a : taken_)
    {
        snprintf(num, sizeof(num),
                 "{\"kind\":\"%s\",\"channel\":%d,\"note\":%d,"
                 "\"velocity\":%d,\"t\":%.9g,\"aux\":%d",
                 KIND[a.kind], a.channel, a.note, a.vel, a.t, a.aux);
        out += (out.size() > 1 ? "," : "") + std::string(num);
        if (a.kind == thhAction::VALUE)
        {
            out += ",\"arg\":" + quoted(a.arg);
            if (a.hasMin)
                out += (snprintf(num, sizeof(num), ",\"min\":%.9g", a.min),
                        std::string(num));
            if (a.hasMax)
                out += (snprintf(num, sizeof(num), ",\"max\":%.9g", a.max),
                        std::string(num));
            if (a.exp)
                out += ",\"exp\":true";
        }
        out += "}";
    }
    taken_.clear();
    return out + "]";
}
