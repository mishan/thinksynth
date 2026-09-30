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

/*
 * midicheck -- does thcMidiFile write the notes it was given, as a file a
 * sequencer reads back the same way?
 *
 * Every case builds a file from hand-made events and reads the bytes back
 * with the small SMF reader below, so a check names ticks and messages
 * rather than a byte offset: the header, the conductor track, the length
 * of a note, and the cases MIDI cannot say as the engine played them -- a
 * retrigger before the off, two chains doubling a hit, a held note, a note
 * still sounding when the transport stopped.
 */

#include <stdio.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "thcMidiFile.h"

static int failures = 0;

static void check (bool ok, const char *what, const std::string &detail = "")
{
    printf("%-52s %s%s%s\n", what, ok ? "ok" : "FAILED",
           detail.empty() ? "" : "  ", detail.c_str());

    if (!ok)
        failures++;
}

/* ---- a reader, just enough for what the writer writes ---------------- */

struct Event
{
    uint32_t tick;
    std::vector<uint8_t> data;   /* status and data bytes, or FF type ... */
};

struct Smf
{
    bool ok = false;
    int format = -1, division = 0;
    std::vector<std::vector<Event> > tracks;
};

static uint32_t be (const std::vector<uint8_t> &b, size_t at, int n)
{
    uint32_t v = 0;

    for (int i = 0; i < n; i++)
        v = (v << 8) | b[at + i];

    return v;
}

static bool vlq (const std::vector<uint8_t> &b, size_t &at, size_t end,
                 uint32_t &v)
{
    v = 0;

    for (int i = 0; i < 4 && at < end; i++)
    {
        const uint8_t c = b[at++];

        v = (v << 7) | (c & 0x7f);

        if (!(c & 0x80))
            return true;
    }

    return false;
}

static Smf parse (const std::vector<uint8_t> &b)
{
    Smf smf;

    if (b.size() < 14 || std::string(b.begin(), b.begin() + 4) != "MThd" ||
        be(b, 4, 4) != 6)
        return smf;

    smf.format = (int)be(b, 8, 2);
    smf.division = (int)be(b, 12, 2);

    const uint32_t ntracks = be(b, 10, 2);
    size_t at = 14;

    for (uint32_t t = 0; t < ntracks; t++)
    {
        if (at + 8 > b.size() ||
            std::string(b.begin() + at, b.begin() + at + 4) != "MTrk")
            return smf;

        const size_t end = at + 8 + be(b, at + 4, 4);

        if (end > b.size())
            return smf;

        at += 8;

        std::vector<Event> events;
        uint32_t tick = 0;
        bool eot = false;

        while (at < end && !eot)
        {
            uint32_t delta, len;

            if (!vlq(b, at, end, delta) || at >= end)
                return smf;

            tick += delta;

            Event e;

            e.tick = tick;

            if (b[at] == 0xff)
            {
                const uint8_t type = b[at + 1];

                at += 2;

                if (!vlq(b, at, end, len) || at + len > end)
                    return smf;

                e.data = { 0xff, type };
                e.data.insert(e.data.end(), b.begin() + at,
                              b.begin() + at + len);
                at += len;
                eot = type == 0x2f;
            }
            else if ((b[at] & 0xe0) == 0x80)       /* 8n, 9n: three bytes */
            {
                if (at + 3 > end)
                    return smf;

                e.data.assign(b.begin() + at, b.begin() + at + 3);
                at += 3;
            }
            else
                return smf;                         /* nothing else is written */

            events.push_back(e);
        }

        if (!eot || at != end)
            return smf;

        smf.tracks.push_back(events);
    }

    smf.ok = at == b.size();
    return smf;
}

/* A note as the file says it: on and off ticks, key, velocity. */
struct Heard
{
    uint32_t on, off;
    int channel, key, velocity;
};

static std::vector<Heard> heard (const std::vector<Event> &track)
{
    std::vector<Heard> out;

    for (size_t i = 0; i < track.size(); i++)
    {
        const Event &e = track[i];

        if ((e.data[0] & 0xf0) != 0x90)
            continue;

        for (size_t j = i + 1; j < track.size(); j++)
            if (track[j].data[0] == (0x80 | (e.data[0] & 0x0f)) &&
                track[j].data[1] == e.data[1])
            {
                out.push_back({ e.tick, track[j].tick, e.data[0] & 0x0f,
                                e.data[1], e.data[2] });
                break;
            }
    }

    return out;
}

static std::string metaText (const std::vector<Event> &track, uint8_t type,
                             size_t nth = 0)
{
    for (const Event &e : track)
        if (e.data[0] == 0xff && e.data[1] == type && nth-- == 0)
            return std::string(e.data.begin() + 2, e.data.end());

    return "<none>";
}

static thcEvent note (double at, int channel, int key, int velocity,
                      double duration)
{
    thcEvent ev = {};

    ev.type = THC_EV_NOTE;
    ev.at = at;
    ev.channel = channel;
    ev.u.note.note = key;
    ev.u.note.velocity = velocity;
    ev.u.note.duration = duration;
    return ev;
}

static thcEvent noteOff (double at, int channel, int key)
{
    thcEvent ev = {};

    ev.type = THC_EV_NOTEOFF;
    ev.at = at;
    ev.channel = channel;
    ev.u.note.note = key;
    return ev;
}

static std::string show (const Heard &h)
{
    char buf[80];

    snprintf(buf, sizeof buf, "ch %d key %d vel %d  %u..%u", h.channel,
             h.key, h.velocity, h.on, h.off);
    return buf;
}

int main (void)
{
    /* At 120 and 480 to the beat a second is 960 ticks, so every time
       below is a whole number of ticks. */

    {
        thcMidiFile f(120, 4);

        f.setName("piece");
        f.setChannelName(0, "kick");
        f.addMarker(0, "intro");
        f.addMarker(2, "verse");
        f.addMarker(9, "never reached");
        f.add(note(0, 0, 36, 100, 0.25));
        f.add(note(1, 3, 60, 80, 0.5));
        f.end(4);

        const Smf s = parse(f.bytes());

        check(s.ok, "the file parses");
        check(s.format == 1 && s.division == 480, "format 1, 480 per beat");
        check(s.tracks.size() == 3, "a conductor and a track per channel",
              std::to_string(s.tracks.size()) + " tracks");

        if (s.tracks.size() == 3)
        {
            const std::vector<Event> &c = s.tracks[0];

            check(metaText(c, 0x03) == "piece", "the piece names track 0");
            check(metaText(c, 0x51) == std::string("\x07\xa1\x20", 3),
                  "tempo is 500000 us to the beat");
            check(metaText(c, 0x58).size() == 4 &&
                  metaText(c, 0x58)[0] == 4 && metaText(c, 0x58)[1] == 2,
                  "meter is 4/4");
            check(metaText(c, 0x06, 0) == "intro" &&
                  metaText(c, 0x06, 1) == "verse" &&
                  metaText(c, 0x06, 2) == "<none>",
                  "markers up to the end, none past it");
            check(c.back().tick == 3840, "every track runs to the end",
                  std::to_string(c.back().tick));

            check(metaText(s.tracks[1], 0x03) == "kick",
                  "a channel's track is named for its instrument");
            check(metaText(s.tracks[2], 0x03) == "channel 4",
                  "an unnamed one for its channel, counted from 1");

            const std::vector<Heard> k = heard(s.tracks[1]);
            const std::vector<Heard> p = heard(s.tracks[2]);

            check(k.size() == 1 && k[0].on == 0 && k[0].off == 240 &&
                  k[0].key == 36 && k[0].velocity == 100 &&
                  k[0].channel == 0,
                  "a note is where and as long as it was played",
                  k.empty() ? "" : show(k[0]));
            check(p.size() == 1 && p[0].on == 960 && p[0].off == 1440 &&
                  p[0].channel == 3,
                  "on the channel it was played on",
                  p.empty() ? "" : show(p[0]));
        }
    }

    {
        thcMidiFile f(120);

        f.add(note(0, 0, 60, 90, 1));         /* cut by the retrigger   */
        f.add(note(0.5, 0, 60, 70, 1));
        f.add(note(2, 0, 62, 40, 0.1));       /* doubled at one instant */
        f.add(note(2, 0, 62, 110, 0.5));
        f.add(note(3, 0, 64, 90, 0.25));      /* the same, reversed     */
        f.add(note(3, 0, 64, 20, 0.05));
        f.end(10);

        const Smf s = parse(f.bytes());
        const std::vector<Heard> h =
            s.tracks.size() == 2 ? heard(s.tracks[1]) : std::vector<Heard>();

        check(h.size() == 4, "a retrigger and two doubles make four notes",
              std::to_string(h.size()));

        if (h.size() == 4)
        {
            check(h[0].on == 0 && h[0].off == 480,
                  "a retrigger ends the note before it", show(h[0]));
            check(h[1].on == 480 && h[1].off == 1440, "and plays whole",
                  show(h[1]));
            check(h[2].on == 1920 && h[2].off == 2400 && h[2].velocity == 110,
                  "a doubled hit is the longer and the louder", show(h[2]));
            check(h[3].on == 2880 && h[3].off == 3120 && h[3].velocity == 90,
                  "whichever order they came in", show(h[3]));
        }

        check(f.notes() == 4, "notes() counts what is written");

        /* The off goes before the on at the retrigger's tick, or a reader
           pairing them in order ends the new note at once. */
        bool offFirst = false;

        if (s.tracks.size() == 2)
            for (size_t i = 0; i + 1 < s.tracks[1].size(); i++)
                if (s.tracks[1][i].tick == 480)
                {
                    offFirst = (s.tracks[1][i].data[0] & 0xf0) == 0x80;
                    break;
                }

        check(offFirst, "at one tick, the off comes first");
    }

    {
        thcMidiFile f(120);
        thcEvent chanarg = {};

        chanarg.type = THC_EV_CHANARG;
        chanarg.u.chanarg.name = "cutoff";

        f.add(note(1, 0, 48, 100, 0));        /* held, released         */
        f.add(noteOff(1.5, 0, 48));
        f.add(note(2, 0, 50, 100, 0));        /* held, never released   */
        f.add(note(2.5, 0, 52, 100, 4));      /* sounding at the stop   */
        f.add(noteOff(2, 0, 55));             /* an off nobody holds    */
        f.add(note(0, 16, 60, 100, 1));       /* no such MIDI channel   */
        f.add(chanarg);
        f.end(3);

        const Smf s = parse(f.bytes());
        const std::vector<Heard> h =
            s.tracks.size() == 2 ? heard(s.tracks[1]) : std::vector<Heard>();

        check(s.ok && h.size() == 3, "held notes and the stop",
              std::to_string(h.size()) + " notes");

        if (h.size() == 3)
        {
            check(h[0].on == 960 && h[0].off == 1440,
                  "a held note ends at its release", show(h[0]));
            check(h[1].on == 1920 && h[1].off == 2880,
                  "one never released ends at the stop", show(h[1]));
            check(h[2].on == 2400 && h[2].off == 2880,
                  "and so does one still sounding", show(h[2]));
        }

        check(f.skipped() == 2, "channel 16 and a chanarg are skipped",
              std::to_string(f.skipped()));
    }

    {
        thcMidiFile f(90, 7.5);

        f.add(note(0, 0, 60, 100, 1));
        f.end(1);

        const Smf s = parse(f.bytes());

        check(s.ok && metaText(s.tracks[0], 0x58) == "<none>",
              "no time signature for a fractional meter");
        check(s.ok && metaText(s.tracks[0], 0x51) ==
                      std::string("\x0a\x2c\x2b", 3),
              "tempo 90 is 666667 us to the beat");

        const std::vector<Heard> h = heard(s.tracks[1]);

        check(h.size() == 1 && h[0].off == 720, "a second at 90 is 720 ticks",
              h.empty() ? "" : show(h[0]));
    }

    {
        thcMidiFile f(120);

        f.add(note(1, 0, 60, 100, 0.5));
        f.end(1);

        const Smf s = parse(f.bytes());

        check(s.ok && s.tracks.size() == 1,
              "a note that starts at the stop is not written");
    }

    {
        /* Long enough to need a four-byte delta. */
        thcMidiFile f(120);

        f.add(note(0, 0, 60, 100, 3000));
        f.end(3000);

        const Smf s = parse(f.bytes());
        const std::vector<Heard> h =
            s.tracks.size() == 2 ? heard(s.tracks[1]) : std::vector<Heard>();

        check(h.size() == 1 && h[0].off == 2880000,
              "a fifty-minute note survives its delta",
              h.empty() ? "" : show(h[0]));
    }

    printf("\n%s\n", failures ? "midicheck FAILED" : "midicheck ok");

    return failures ? 1 : 0;
}
