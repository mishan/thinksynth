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
#include <cmath>

#include "thcMidiFile.h"

namespace {

/* The controllers MIDI leaves undefined, in the order a channel's
   chanargs take them. The fine ones are the 14-bit pairs' MSBs; each
   one's LSB is 32 above it. */
const int COARSE_CCS[] = { 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
                           102, 103, 104, 105, 106, 107, 108, 109, 110,
                           111, 112, 113, 114, 115, 116, 117, 118, 119 };
const int FINE_CCS[]   = { 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };

const int CC_EXPRESSION = 11;

/* At one tick: names and labels, then offs, then controllers, then ons,
   so a note ends before its key sounds again and starts at the level and
   the settings sent for it. */
enum { AT_META = -1, AT_OFF, AT_CONTROL, AT_ON };

struct TrackEvent
{
    uint32_t tick;
    int      order;
    std::vector<uint8_t> data;
};

bool earlier (const TrackEvent &a, const TrackEvent &b)
{
    if (a.tick != b.tick)
        return a.tick < b.tick;

    return a.order < b.order;
}

void put16 (std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)v);
}

void put32 (std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back((uint8_t)(v >> 24));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)v);
}

/* A variable-length quantity: seven bits a byte, high bit set on all but
   the last. */
void putVlq (std::vector<uint8_t> &out, uint32_t v)
{
    uint8_t buf[5];
    int n = 0;

    buf[n++] = v & 0x7f;

    while ((v >>= 7) != 0)
        buf[n++] = (uint8_t)(0x80 | (v & 0x7f));

    while (n > 0)
        out.push_back(buf[--n]);
}

std::vector<uint8_t> meta (uint8_t type, const std::string &text)
{
    std::vector<uint8_t> d = { 0xff, type };

    putVlq(d, (uint32_t)text.size());
    d.insert(d.end(), text.begin(), text.end());
    return d;
}

std::vector<uint8_t> cc (int channel, int number, int value)
{
    return { (uint8_t)(0xb0 | channel), (uint8_t)number,
             (uint8_t)std::min(127, std::max(0, value)) };
}

/* An MTrk chunk: the events in order, delta-timed, then End of Track at
   `last' or the last event, whichever is later. Stable, so events at one
   tick and of one kind keep the order they were made in. */
void putTrack (std::vector<uint8_t> &out, std::vector<TrackEvent> events,
               uint32_t last)
{
    std::stable_sort(events.begin(), events.end(), earlier);

    std::vector<uint8_t> body;
    uint32_t at = 0;

    for (const TrackEvent &e : events)
    {
        putVlq(body, e.tick - at);
        body.insert(body.end(), e.data.begin(), e.data.end());
        at = e.tick;
    }

    putVlq(body, last > at ? last - at : 0);
    body.push_back(0xff);
    body.push_back(0x2f);
    body.push_back(0x00);

    out.insert(out.end(), { 'M', 'T', 'r', 'k' });
    put32(out, (uint32_t)body.size());
    out.insert(out.end(), body.begin(), body.end());
}

std::string number (double v)
{
    char buf[32];

    snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

}

thcMidiFile::thcMidiFile (double tempo, double meter, int division)
    : tempo_(tempo > 0 ? tempo : 120), meter_(meter),
      division_(division > 0 && division < 0x8000 ? division : 480),
      skipped_(0), endAt_(0), ended_(false), fine_(false)
{
}

void
thcMidiFile::setChainName (int chain, const std::string &name)
{
    chainNames_[chain] = name;
}

void
thcMidiFile::setChannelName (int channel, const std::string &name)
{
    channelNames_[channel] = name;
}

void
thcMidiFile::addMarker (double at, const std::string &text)
{
    markers_.push_back({ ticks(at), text });
}

uint32_t
thcMidiFile::ticks (double seconds) const
{
    if (!(seconds > 0))
        return 0;

    return (uint32_t)llround(seconds * tempo_ / 60.0 * division_);
}

/* The index of `name' in the channel's controls, taking it a CC number
   the first time; -1 once the channel has used them all. */
int
thcMidiFile::control (int channel, const std::string &name)
{
    std::vector<Control> &list = controls_[channel];

    for (size_t i = 0; i < list.size(); i++)
        if (list[i].name == name)
            return (int)i;

    const size_t pool = fine_ ? sizeof FINE_CCS / sizeof FINE_CCS[0]
                              : sizeof COARSE_CCS / sizeof COARSE_CCS[0];

    if (list.size() >= pool)
        return -1;

    Control c;

    c.name = name;
    c.cc = fine_ ? FINE_CCS[list.size()] : COARSE_CCS[list.size()];
    c.min = 0;
    c.max = 0;
    c.declared = range_ && range_(channel, name, c.min, c.max) &&
                 c.max > c.min;

    if (!c.declared)
    {
        c.min = HUGE_VAL;
        c.max = -HUGE_VAL;
    }

    list.push_back(c);
    return (int)list.size() - 1;
}

void
thcMidiFile::add (const thcEvent &ev, int chain)
{
    if (ev.channel < 0 || ev.channel > 15)
    {
        skipped_++;
        return;
    }

    const uint32_t at = ticks(ev.at);

    switch (ev.type)
    {
        case THC_EV_NOTE:
        {
            if (ev.u.note.note < 0 || ev.u.note.note > 127)
            {
                skipped_++;
                return;
            }

            Note n;

            n.channel  = ev.channel;
            n.key      = ev.u.note.note;
            n.velocity = std::min(127, std::max(1, ev.u.note.velocity));
            /* 0 is read as 1, as the scheduler reads it. */
            n.level    = ev.u.note.level > 0 ? ev.u.note.level : 1;
            n.on       = at;
            n.open     = !(ev.u.note.duration > 0);
            n.off      = n.open ? at : ticks(ev.at + ev.u.note.duration);

            parts_[chain >= 0 ? chain : -1 - ev.channel].notes.push_back(n);
            return;
        }
        case THC_EV_NOTEOFF:
        {
            /* The oldest held note on that key and channel, whichever
               chain played it: the scheduler releases in the order it
               holds. An off nobody holds is a release that raced a
               flush, and ends nothing. */
            Note *oldest = NULL;

            for (auto &p : parts_)
                for (Note &n : p.second.notes)
                    if (n.open && n.channel == ev.channel &&
                        n.key == ev.u.note.note &&
                        (oldest == NULL || n.on < oldest->on))
                        oldest = &n;

            if (oldest != NULL)
            {
                oldest->off = at;
                oldest->open = false;
            }

            return;
        }
        case THC_EV_CHANARG:
        {
            const int i = ev.u.chanarg.name != NULL
                              ? control(ev.channel, ev.u.chanarg.name)
                              : -1;

            if (i < 0)
            {
                skipped_++;
                return;
            }

            Control &c = controls_[ev.channel][i];
            const double v = ev.u.chanarg.value;

            if (!c.declared)
            {
                c.min = std::min(c.min, v);
                c.max = std::max(c.max, v);
            }

            channelEvents_[ev.channel].push_back({ at, i, v, "" });
            return;
        }
        case THC_EV_PATCH:
            channelEvents_[ev.channel].push_back(
                { at, -1, 0, std::string("swap: ") +
                             (ev.u.patch.name ? ev.u.patch.name : "") });
            return;
        case THC_EV_NODEARG:
            channelEvents_[ev.channel].push_back(
                { at, -1, 0,
                  std::string("edit: ") +
                  (ev.u.nodearg.node ? ev.u.nodearg.node : "") + "." +
                  (ev.u.nodearg.arg ? ev.u.nodearg.arg : "") + " = " +
                  number(ev.u.nodearg.value) });
            return;
        default:
            skipped_++;
            return;
    }
}

void
thcMidiFile::end (double at)
{
    endAt_ = at;
    ended_ = true;

    const uint32_t last = ticks(at);

    for (auto &p : parts_)
        for (Note &n : p.second.notes)
        {
            if (n.open || n.off > last)
                n.off = last;

            n.open = false;
        }
}

/* The notes a file can say: sorted by start within a channel and key,
   each cut off at the next one's start there, and none of no length. Two
   that start together on one key -- a doubled hit -- are one note, as
   long and as loud as the longer and the louder. Ordered by start. */
std::vector<thcMidiFile::Note>
thcMidiFile::playable (const std::vector<Note> &notes)
{
    std::vector<Note> v = notes, out;

    std::stable_sort(v.begin(), v.end(),
                     [](const Note &a, const Note &b)
                     {
                         if (a.channel != b.channel)
                             return a.channel < b.channel;
                         if (a.key != b.key)
                             return a.key < b.key;
                         return a.on < b.on;
                     });

    auto sameKey = [](const Note &a, const Note &b)
    { return a.channel == b.channel && a.key == b.key; };

    for (size_t i = 0; i < v.size(); i++)
    {
        Note n = v[i];

        while (i + 1 < v.size() && sameKey(v[i + 1], n) &&
               v[i + 1].on == n.on)
        {
            i++;
            n.off = std::max(n.off, v[i].off);
            n.velocity = std::max(n.velocity, v[i].velocity);
            n.level = std::max(n.level, v[i].level);
        }

        if (i + 1 < v.size() && sameKey(v[i + 1], n) && v[i + 1].on < n.off)
            n.off = v[i + 1].on;

        if (n.off > n.on)
            out.push_back(n);
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const Note &a, const Note &b)
                     { return a.on < b.on; });
    return out;
}

size_t
thcMidiFile::notes (void) const
{
    size_t count = 0;

    for (const auto &p : parts_)
        count += playable(p.second.notes).size();

    return count;
}

std::vector<uint8_t>
thcMidiFile::bytes (void) const
{
    uint32_t last = ended_ ? ticks(endAt_) : 0;

    /* The note tracks: chains in their order, then what no chain made in
       channel order -- keyed -1 - channel, so backwards through the map. */
    std::vector<int> keys;

    for (const auto &p : parts_)
        if (p.first >= 0)
            keys.push_back(p.first);

    for (auto p = parts_.rbegin(); p != parts_.rend(); ++p)
        if (p->first < 0)
            keys.push_back(p->first);

    struct Track
    {
        std::vector<TrackEvent> events;
        std::vector<bool> channels;     /* which ones it plays notes on */
    };

    std::vector<Track> tracks;

    for (int key : keys)
    {
        const std::vector<Note> notes = playable(parts_.at(key).notes);

        if (notes.empty())
            continue;

        Track t;
        std::string name;

        t.channels.assign(16, false);

        if (key >= 0)
        {
            auto n = chainNames_.find(key);

            name = n != chainNames_.end() && !n->second.empty()
                       ? n->second
                       : "chain " + std::to_string(key + 1);
        }
        else
        {
            auto n = channelNames_.find(-1 - key);

            name = n != channelNames_.end() && !n->second.empty()
                       ? n->second
                       : "channel " + std::to_string(-key);
        }

        t.events.push_back({ 0, AT_META, meta(0x03, name) });

        bool levels = false;

        for (const Note &n : notes)
            levels = levels || n.level != 1;

        /* Expression per MIDI channel the track plays on: it is a channel
           message, and one chain can feed two channels. -1 until the
           channel's first note. */
        std::vector<int> expression(16, -1);

        for (const Note &n : notes)
        {
            t.channels[n.channel] = true;

            if (levels)
            {
                const int value = (int)lrintf(100 * n.level);

                /* A track that sends expression starts at 100, so a DAW
                   that remembers the last value it saw starts the piece
                   where the piece starts. */
                if (expression[n.channel] < 0 && n.on > 0)
                {
                    t.events.push_back({ 0, AT_CONTROL,
                                         cc(n.channel, CC_EXPRESSION, 100) });
                    expression[n.channel] = 100;
                }

                if (value != expression[n.channel])
                    t.events.push_back({ n.on, AT_CONTROL,
                                         cc(n.channel, CC_EXPRESSION,
                                            value) });

                expression[n.channel] = value;
            }

            const uint8_t key = (uint8_t)n.key;

            t.events.push_back({ n.on, AT_ON,
                                 { (uint8_t)(0x90 | n.channel), key,
                                   (uint8_t)n.velocity } });
            t.events.push_back({ n.off, AT_OFF,
                                 { (uint8_t)(0x80 | n.channel), key, 64 } });

            last = std::max(last, n.off);
        }

        tracks.push_back(t);
    }

    /* A channel's chanargs and edits, as the events every track playing
       it carries. */
    for (const auto &ce : channelEvents_)
    {
        const int ch = ce.first;
        const std::vector<Control> none;
        auto cl = controls_.find(ch);
        const std::vector<Control> &controls =
            cl != controls_.end() ? cl->second : none;

        std::vector<TrackEvent> events;

        for (const Control &c : controls)
        {
            std::string label = "CC " + std::to_string(c.cc);

            if (fine_)
                label += "/" + std::to_string(c.cc + 32);

            label += " = " + c.name + " (" + number(c.min) + ".." +
                     number(c.max) + (c.declared ? ")" : ", as played)");
            events.push_back({ 0, AT_META, meta(0x01, label) });
        }

        std::vector<int> sent(controls.size(), -1);

        for (const ChannelEvent &e : ce.second)
        {
            if (e.control < 0)
            {
                events.push_back({ e.tick, AT_META, meta(0x01, e.text) });
                continue;
            }

            const Control &c = controls[e.control];
            const double span = c.max - c.min;
            const double unit = span > 0
                ? std::min(1.0, std::max(0.0, (e.value - c.min) / span))
                : 0.0;
            const int value = (int)lrint(unit * (fine_ ? 16383 : 127));

            if (value == sent[e.control])
                continue;

            sent[e.control] = value;

            if (fine_)
            {
                events.push_back({ e.tick, AT_CONTROL,
                                   cc(ch, c.cc, value >> 7) });
                events.push_back({ e.tick, AT_CONTROL,
                                   cc(ch, c.cc + 32, value & 0x7f) });
            }
            else
                events.push_back({ e.tick, AT_CONTROL, cc(ch, c.cc, value) });
        }

        for (const TrackEvent &e : events)
            last = std::max(last, e.tick);

        bool placed = false;

        for (Track &t : tracks)
            if (t.channels[ch])
            {
                t.events.insert(t.events.end(), events.begin(), events.end());
                placed = true;
            }

        if (!placed)
        {
            auto n = channelNames_.find(ch);
            const std::string name =
                n != channelNames_.end() && !n->second.empty()
                    ? n->second
                    : "channel " + std::to_string(ch + 1);
            Track t;

            t.channels.assign(16, false);
            t.events.push_back({ 0, AT_META,
                                 meta(0x03, name + " controls") });
            t.events.insert(t.events.end(), events.begin(), events.end());
            tracks.push_back(t);
        }
    }

    /* Track 0: the name, the tempo, the meter and the sections. */
    std::vector<TrackEvent> conductor;

    if (!name_.empty())
        conductor.push_back({ 0, AT_META, meta(0x03, name_) });

    const uint32_t usPerBeat = (uint32_t)llround(60e6 / tempo_);

    conductor.push_back({ 0, AT_META, { 0xff, 0x51, 0x03,
                                        (uint8_t)(usPerBeat >> 16),
                                        (uint8_t)(usPerBeat >> 8),
                                        (uint8_t)usPerBeat } });

    if (meter_ >= 1 && meter_ <= 255 && meter_ == std::floor(meter_))
        conductor.push_back({ 0, AT_META, { 0xff, 0x58, 0x04,
                                            (uint8_t)meter_, 2, 24, 8 } });

    for (const auto &m : markers_)
        if (!ended_ || m.first < last)
            conductor.push_back({ m.first, AT_META, meta(0x06, m.second) });

    std::vector<uint8_t> out = { 'M', 'T', 'h', 'd' };

    put32(out, 6);
    put16(out, 1);
    put16(out, (uint32_t)(tracks.size() + 1));
    put16(out, (uint32_t)division_);

    putTrack(out, conductor, last);

    for (const Track &t : tracks)
        putTrack(out, t.events, last);

    return out;
}

bool
thcMidiFile::write (const std::string &path) const
{
    const std::vector<uint8_t> data = bytes();
    FILE *f = fopen(path.c_str(), "wb");

    if (f == NULL)
        return false;

    const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();

    return fclose(f) == 0 && ok;
}
