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

struct TrackEvent
{
    uint32_t tick;
    int      order;          /* at one tick: offs, then ons            */
    std::vector<uint8_t> data;
};

bool earlier (const TrackEvent &a, const TrackEvent &b)
{
    if (a.tick != b.tick)
        return a.tick < b.tick;

    if (a.order != b.order)
        return a.order < b.order;

    return a.data < b.data;
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

/* An MTrk chunk: the events in order, delta-timed, then End of Track at
   `last' or the last event, whichever is later. */
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

}

thcMidiFile::thcMidiFile (double tempo, double meter, int division)
    : tempo_(tempo > 0 ? tempo : 120), meter_(meter),
      division_(division > 0 && division < 0x8000 ? division : 480),
      skipped_(0), endAt_(0), ended_(false)
{
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

void
thcMidiFile::add (const thcEvent &ev)
{
    if (ev.type != THC_EV_NOTE && ev.type != THC_EV_NOTEOFF)
    {
        skipped_++;
        return;
    }

    if (ev.channel < 0 || ev.channel > 15 ||
        ev.u.note.note < 0 || ev.u.note.note > 127)
    {
        skipped_++;
        return;
    }

    std::vector<Note> &notes = channels_[ev.channel];
    const uint32_t at = ticks(ev.at);

    if (ev.type == THC_EV_NOTEOFF)
    {
        /* The oldest held note on that key: the scheduler releases in the
           order it holds. An off nobody holds is a release that raced a
           flush, and ends nothing. */
        for (Note &n : notes)
            if (n.open && n.key == ev.u.note.note)
            {
                n.off = at;
                n.open = false;
                break;
            }

        return;
    }

    Note n;

    n.key      = ev.u.note.note;
    n.velocity = std::min(127, std::max(1, ev.u.note.velocity));
    n.on       = at;
    n.open     = !(ev.u.note.duration > 0);
    n.off      = n.open ? at : ticks(ev.at + ev.u.note.duration);

    notes.push_back(n);
}

void
thcMidiFile::end (double at)
{
    endAt_ = at;
    ended_ = true;

    const uint32_t last = ticks(at);

    for (auto &ch : channels_)
        for (Note &n : ch.second)
        {
            if (n.open || n.off > last)
                n.off = last;

            n.open = false;
        }
}

/* The notes a file can say: sorted by start within a key, each cut off
   at the next one's start on that key, and none of no length. Two that
   start together on one key -- two chains doubling a hit -- are one
   note, as long and as loud as the longer and the louder. Ordered by
   key, then start. */
std::vector<thcMidiFile::Note>
thcMidiFile::playable (const std::vector<Note> &notes)
{
    std::vector<Note> v = notes, out;

    std::stable_sort(v.begin(), v.end(),
                     [](const Note &a, const Note &b)
                     { return a.key != b.key ? a.key < b.key : a.on < b.on; });

    for (size_t i = 0; i < v.size(); i++)
    {
        Note n = v[i];

        while (i + 1 < v.size() && v[i + 1].key == n.key &&
               v[i + 1].on == n.on)
        {
            i++;
            n.off = std::max(n.off, v[i].off);
            n.velocity = std::max(n.velocity, v[i].velocity);
        }

        if (i + 1 < v.size() && v[i + 1].key == n.key && v[i + 1].on < n.off)
            n.off = v[i + 1].on;

        if (n.off > n.on)
            out.push_back(n);
    }

    return out;
}

size_t
thcMidiFile::notes (void) const
{
    size_t count = 0;

    for (const auto &ch : channels_)
        count += playable(ch.second).size();

    return count;
}

std::vector<uint8_t>
thcMidiFile::bytes (void) const
{
    std::vector<uint8_t> out;
    uint32_t last = ended_ ? ticks(endAt_) : 0;

    std::vector<std::vector<TrackEvent> > tracks;

    for (const auto &ch : channels_)
    {
        const uint8_t status = (uint8_t)ch.first;
        std::vector<TrackEvent> events;

        for (const Note &n : playable(ch.second))
        {
            const uint8_t key = (uint8_t)n.key;

            events.push_back({ n.on, 1, { (uint8_t)(0x90 | status), key,
                                          (uint8_t)n.velocity } });
            events.push_back({ n.off, 0, { (uint8_t)(0x80 | status), key,
                                           64 } });

            last = std::max(last, n.off);
        }

        if (events.empty())
            continue;

        auto name = channelNames_.find(ch.first);

        events.push_back({ 0, -1,
                           meta(0x03, name != channelNames_.end() &&
                                      !name->second.empty()
                                      ? name->second
                                      : "channel " +
                                        std::to_string(ch.first + 1)) });
        tracks.push_back(events);
    }

    /* Track 0: the name, the tempo, the meter and the sections. */
    std::vector<TrackEvent> conductor;

    if (!name_.empty())
        conductor.push_back({ 0, -1, meta(0x03, name_) });

    const uint32_t usPerBeat = (uint32_t)llround(60e6 / tempo_);

    conductor.push_back({ 0, 0, { 0xff, 0x51, 0x03,
                                  (uint8_t)(usPerBeat >> 16),
                                  (uint8_t)(usPerBeat >> 8),
                                  (uint8_t)usPerBeat } });

    if (meter_ >= 1 && meter_ <= 255 && meter_ == std::floor(meter_))
        conductor.push_back({ 0, 0, { 0xff, 0x58, 0x04, (uint8_t)meter_,
                                      2, 24, 8 } });

    for (const auto &m : markers_)
        if (!ended_ || m.first < last)
            conductor.push_back({ m.first, 1, meta(0x06, m.second) });

    out.insert(out.end(), { 'M', 'T', 'h', 'd' });
    put32(out, 6);
    put16(out, 1);
    put16(out, (uint32_t)(tracks.size() + 1));
    put16(out, (uint32_t)division_);

    putTrack(out, conductor, last);

    for (const auto &t : tracks)
        putTrack(out, t, last);

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
