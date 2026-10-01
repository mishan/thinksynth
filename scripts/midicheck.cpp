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
 * still sounding when the transport stopped -- and what the rest of a
 * piece becomes: a track per chain, a level as expression, a chanarg as a
 * labeled controller, a swap or an edit as text.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "config.h"
#include "think.h"

#include "libthink/thDynLib.h"
#include "thcMidiExport.h"
#include "thcMidiFile.h"
#include "thcPlugin.h"

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
            else if ((b[at] & 0xf0) == 0x80 || (b[at] & 0xf0) == 0x90 ||
                     (b[at] & 0xf0) == 0xb0)      /* 8n, 9n, Bn: three bytes */
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

/* The controller changes on one track, as (tick, number, value). */
struct Change
{
    uint32_t tick;
    int number, value;
};

static std::vector<Change> changes (const std::vector<Event> &track)
{
    std::vector<Change> out;

    for (const Event &e : track)
        if ((e.data[0] & 0xf0) == 0xb0)
            out.push_back({ e.tick, e.data[1], e.data[2] });

    return out;
}

static std::string showChanges (const std::vector<Change> &v)
{
    std::string s;

    for (const Change &c : v)
        s += "[" + std::to_string(c.tick) + " cc" + std::to_string(c.number) +
             "=" + std::to_string(c.value) + "]";

    return s;
}

static thcEvent chanarg (double at, int channel, const char *name,
                         float value)
{
    thcEvent ev = {};

    ev.type = THC_EV_CHANARG;
    ev.at = at;
    ev.channel = channel;
    ev.u.chanarg.name = name;
    ev.u.chanarg.value = value;
    return ev;
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

/* `--read FILE': the file genwav wrote, one line per thing a check on
   genwav's side names -- track names, markers, controller labels, note
   counts -- for cmake/RunGenwavMidi.cmake to match. Exit 1 where it does
   not parse. */
static int read (const char *path)
{
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    const Smf s = parse(bytes);

    if (!s.ok)
    {
        printf("unparsed\n");
        return 1;
    }

    printf("format %d division %d tracks %zu\n", s.format, s.division,
           s.tracks.size());

    for (size_t t = 0; t < s.tracks.size(); t++)
    {
        size_t ons = 0, ccs = 0;

        for (const Event &e : s.tracks[t])
        {
            if ((e.data[0] & 0xf0) == 0x90)
                ons++;
            else if ((e.data[0] & 0xf0) == 0xb0)
                ccs++;
            else if (e.data[0] == 0xff && (e.data[1] == 0x01 ||
                                           e.data[1] == 0x03 ||
                                           e.data[1] == 0x06))
                printf("track %zu %s %u %s\n", t,
                       e.data[1] == 0x01 ? "text"
                       : e.data[1] == 0x03 ? "name" : "marker",
                       e.tick,
                       std::string(e.data.begin() + 2, e.data.end()).c_str());
        }

        printf("track %zu notes %zu controls %zu\n", t, ons, ccs);
    }

    return 0;
}

/* `--export PLUGINS SECONDS GEN OUT': the piece through
   thcMidiExport::render, as the Composer's Export MIDI and the page's run
   it, written to OUT -- for cmake/RunGenwavMidi.cmake to read beside what
   genwav --midi wrote of the same piece. */
static int exportPiece (const std::string &pluginDir, double seconds,
                        const std::string &gen, const std::string &out)
{
    std::map<std::string, thcPlugin *> plugins;
    std::error_code ec;
    const std::filesystem::path root =
        std::filesystem::path(pluginDir) / "composer";

    for (const auto &f : std::filesystem::directory_iterator(root, ec))
    {
        if (f.path().extension() != PLUGIN_SUFFIX)
            continue;

        thcPlugin *p = new thcPlugin(f.path().string());

        if (p->state() != thcPlugin::LOADED)
        {
            delete p;
            continue;
        }

        plugins[p->name()] = p;

        /* genwav's reason: the mapping outlives ~thcPlugin. */
        thDynLib::open(f.path().string());
    }

    int rc = 1;

    {
        thSynth synth(pluginDir, TH_DEFAULT_WINDOW_LENGTH,
                      TH_DEFAULT_SAMPLES);
        thcMidiExport::Options options;
        std::vector<uint8_t> bytes;
        std::string why;

        synth.setSilent(true);
        options.seconds = seconds;

        if (!thcMidiExport::render(plugins, &synth, gen, options, bytes,
                                   why))
            fprintf(stderr, "midicheck: %s\n", why.c_str());
        else
        {
            std::ofstream f(out.c_str(), std::ios::binary | std::ios::trunc);

            f.write((const char *)bytes.data(), (std::streamsize)bytes.size());
            rc = f.good() ? 0 : 1;
        }
    }

    for (auto &p : plugins)
        delete p.second;

    return rc;
}

int main (int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--read"))
        return read(argv[2]);

    if (argc == 6 && !strcmp(argv[1], "--export"))
        return exportPiece(argv[2], atof(argv[3]), argv[4], argv[5]);

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

        check(f.skipped() == 1, "a note on channel 17 is skipped",
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

    {
        /* Two chains on one channel are two tracks, named for them, and
           one voice per key holds across them: a hit they double is one
           note, in the longer one's track, and a retrigger from the other
           chain cuts a note. */
        thcMidiFile f(120);

        f.setChainName(0, "hats");
        f.setChainName(1, "hats_open");
        f.add(note(0, 2, 54, 100, 0.25), 1);
        f.add(note(0, 2, 54, 90, 0.05), 0);
        f.add(note(1, 2, 60, 100, 1), 0);
        f.add(note(1.5, 2, 60, 100, 1), 1);
        f.end(4);

        const Smf s = parse(f.bytes());

        check(s.ok && s.tracks.size() == 3 &&
              metaText(s.tracks[1], 0x03) == "hats" &&
              metaText(s.tracks[2], 0x03) == "hats_open",
              "a track per chain, in chain order");

        if (s.tracks.size() == 3)
        {
            const std::vector<Heard> a = heard(s.tracks[1]);
            const std::vector<Heard> b = heard(s.tracks[2]);

            check(a.size() == 1 && b.size() == 2 &&
                  b[0].key == 54 && b[0].on == 0 && b[0].off == 240 &&
                  b[0].velocity == 100,
                  "a hit two chains double is one note, the longer's",
                  b.empty() ? "" : show(b[0]));
            check(a.size() == 1 && a[0].key == 60 && a[0].on == 960 &&
                  a[0].off == 1439 && b.size() == 2 && b[1].on == 1440 &&
                  b[1].off == 2400,
                  "another chain's retrigger cuts it a tick early",
                  a.empty() ? "" : show(a[0]));
        }
    }

    {
        /* Level as expression: before the first note, then only where the
           level moves, and nothing at all for a track at level 1. */
        thcMidiFile f(120);

        thcEvent a = note(0.5, 0, 60, 100, 0.25);
        thcEvent b = note(1, 0, 62, 100, 0.25);
        thcEvent c = note(1.5, 0, 64, 100, 0.25);
        thcEvent d = note(2, 0, 65, 100, 0.25);

        a.u.note.level = 0.7f;
        b.u.note.level = 0.7f;
        c.u.note.level = 1.15f;
        d.u.note.level = 0;                   /* read as 1 */

        f.add(a, 0);
        f.add(b, 0);
        f.add(c, 0);
        f.add(d, 0);
        f.add(note(0, 1, 36, 100, 0.25), 1);
        f.end(3);

        const Smf s = parse(f.bytes());
        const std::vector<Change> e =
            s.tracks.size() == 3 ? changes(s.tracks[1]) : std::vector<Change>();

        check(e.size() == 3 &&
              e[0].tick == 480 && e[0].number == 11 && e[0].value == 70 &&
              e[1].tick == 1440 && e[1].value == 115 &&
              e[2].tick == 1920 && e[2].value == 100,
              "level is CC 11, sent where it changes", showChanges(e));
        check(s.tracks.size() == 3 && changes(s.tracks[2]).empty(),
              "a track at level 1 sends none");

        /* The controller goes ahead of the note it is for. */
        bool before = false;

        if (s.tracks.size() == 3)
            for (size_t i = 0; i + 1 < s.tracks[1].size(); i++)
                if (s.tracks[1][i].tick == 480)
                {
                    before = (s.tracks[1][i].data[0] & 0xf0) == 0xb0;
                    break;
                }

        check(before, "expression goes before the note-on");
    }

    {
        /* Chanargs: a CC each, in order of appearance, scaled through
           the declared range where there is one and the values played
           where there is not, a label for each, a repeat left out, and
           every track playing the channel carrying them. */
        thcMidiFile f(120);

        f.setRangeLookup([](int, const std::string &name, double &min,
                            double &max)
                         {
                             if (name != "cutoff")
                                 return false;
                             min = 100;
                             max = 1100;
                             return true;
                         });
        f.add(note(0, 3, 60, 100, 2), 0);
        f.add(note(0, 3, 67, 100, 2), 1);
        f.add(chanarg(0, 3, "cutoff", 600), 5);
        f.add(chanarg(0.5, 3, "drive", 2), 5);
        f.add(chanarg(1, 3, "cutoff", 600.001f), 5);  /* the same CC */
        f.add(chanarg(1, 3, "drive", 4), 5);
        f.add(chanarg(1.5, 3, "cutoff", 5000), 5);    /* past the max */
        f.end(2);

        const Smf s = parse(f.bytes());

        check(s.ok && s.tracks.size() == 3,
              "chanargs make no track where notes play the channel",
              std::to_string(s.tracks.size()) + " tracks");

        if (s.tracks.size() == 3)
        {
            const std::vector<Change> c = changes(s.tracks[1]);

            check(c.size() == 4 &&
                  c[0].tick == 0    && c[0].number == 20 && c[0].value == 64 &&
                  c[1].tick == 480  && c[1].number == 21 && c[1].value == 0 &&
                  c[2].tick == 960  && c[2].number == 21 && c[2].value == 127 &&
                  c[3].tick == 1440 && c[3].number == 20 && c[3].value == 127,
                  "scaled, numbered, repeats dropped", showChanges(c));
            check(metaText(s.tracks[1], 0x01, 0) == "CC 20 = cutoff (100..1100)"
                  && metaText(s.tracks[1], 0x01, 1) ==
                     "CC 21 = drive (2..4, as played)",
                  "a label per controller",
                  metaText(s.tracks[1], 0x01, 0) + " / " +
                  metaText(s.tracks[1], 0x01, 1));
            check(showChanges(changes(s.tracks[2])) == showChanges(c),
                  "every track on the channel carries them");
        }
    }

    {
        /* Controls for a channel no track plays get a track of their own;
           fine controllers are MSB and LSB pairs; a channel runs out of
           numbers after twelve of them. */
        thcMidiFile f(120);
        std::vector<std::string> names;

        for (int i = 0; i < 13; i++)
            names.push_back("k" + std::to_string(i));

        f.setFineControllers(true);
        f.setChannelName(5, "pad");
        f.setRangeLookup([](int, const std::string &, double &min,
                            double &max)
                         { min = 0; max = 1; return true; });

        for (int i = 0; i < 13; i++)
            f.add(chanarg(0, 5, names[i].c_str(), 0.5f), 2);

        const Smf s = parse(f.bytes());

        check(s.ok && s.tracks.size() == 2 &&
              metaText(s.tracks[1], 0x03) == "pad controls",
              "a channel with no notes gets a controls track");

        const std::vector<Change> c =
            s.tracks.size() == 2 ? changes(s.tracks[1]) : std::vector<Change>();

        check(c.size() == 24 && c[0].number == 20 && c[0].value == 64 &&
              c[1].number == 52 && c[1].value == 0 &&
              c[22].number == 31 && c[23].number == 63,
              "fine: 8192 as 64 and 0, on 20-31 and 52-63",
              c.size() >= 2 ? showChanges({ c[0], c[1] }) : "");
        check(f.skipped() == 1, "the thirteenth has no controller left",
              std::to_string(f.skipped()));
        check(s.tracks.size() == 2 &&
              metaText(s.tracks[1], 0x01, 0) == "CC 20/52 = k0 (0..1)",
              "a fine label names both numbers");
    }

    {
        thcMidiFile f(120);
        thcEvent swap = {}, edit = {};

        swap.type = THC_EV_PATCH;
        swap.at = 1;
        swap.channel = 0;
        swap.u.patch.name = "pad2";

        edit.type = THC_EV_NODEARG;
        edit.at = 1.5;
        edit.channel = 0;
        edit.u.nodearg.node = "filt";
        edit.u.nodearg.arg = "cutoff";
        edit.u.nodearg.value = 440;

        f.add(note(0, 0, 60, 100, 2), 0);
        f.add(swap, 0);
        f.add(edit, 0);
        f.end(2);

        const Smf s = parse(f.bytes());
        bool swapAt = false, editAt = false;

        if (s.tracks.size() == 2)
            for (const Event &e : s.tracks[1])
                if (e.data[0] == 0xff && e.data[1] == 0x01)
                {
                    const std::string t(e.data.begin() + 2, e.data.end());

                    swapAt = swapAt || (t == "swap: pad2" && e.tick == 960);
                    editAt = editAt ||
                             (t == "edit: filt.cutoff = 440" && e.tick == 1440);
                }

        check(swapAt && editAt, "a swap and an edit are text where they land");
        check(f.skipped() == 0, "and neither is skipped");
    }

    {
        /* Expression on a channel two tracks share: the one at level 1
           still sends 100 before its note, since the other left 50. */
        thcMidiFile f(120);
        thcEvent quiet = note(0, 0, 60, 100, 0.5);

        quiet.u.note.level = 0.5f;
        f.add(quiet, 0);
        f.add(note(1, 0, 62, 100, 0.5), 1);
        f.end(2);

        const Smf s = parse(f.bytes());

        if (s.tracks.size() == 3)
        {
            const std::vector<Change> b = changes(s.tracks[2]);

            check(b.size() == 1 && b[0].tick == 960 && b[0].value == 100,
                  "a shared channel's level-1 track restores 100",
                  showChanges(b));
        }
        else
            check(false, "a shared channel's level-1 track restores 100");
    }

    {
        /* A chord whose notes differ in level: one CC 11, the loudest. */
        thcMidiFile f(120);
        thcEvent a = note(0.5, 0, 60, 100, 0.5);
        thcEvent b = note(0.5, 0, 64, 100, 0.5);

        a.u.note.level = 0.6f;
        b.u.note.level = 0.9f;
        f.add(a, 0);
        f.add(b, 0);
        f.end(2);

        const Smf s = parse(f.bytes());
        const std::vector<Change> c =
            s.tracks.size() == 2 ? changes(s.tracks[1]) : std::vector<Change>();

        check(c.size() == 1 && c[0].tick == 480 && c[0].value == 90,
              "a chord sends one level, the loudest", showChanges(c));
    }

    {
        /* A held key struck again, then two releases: the first ends the
           key, as the engine's release ends whatever holds it. */
        thcMidiFile f(120);

        f.add(note(0, 0, 60, 100, 0), 0);
        f.add(note(1, 0, 60, 100, 0), 0);
        f.add(noteOff(1.5, 0, 60));
        f.add(noteOff(3, 0, 60));
        f.end(4);

        const Smf s = parse(f.bytes());
        const std::vector<Heard> h =
            s.tracks.size() == 2 ? heard(s.tracks[1]) : std::vector<Heard>();

        check(h.size() == 2 && h[0].off == 960 && h[1].on == 960 &&
              h[1].off == 1440,
              "a held retrigger ends at the first release",
              h.size() == 2 ? show(h[0]) + " / " + show(h[1]) : "");
    }

    {
        /* A note shorter than half a tick is still a tick long. */
        thcMidiFile f(120);

        f.add(note(1, 0, 60, 100, 0.0004), 0);
        f.end(2);

        const Smf s = parse(f.bytes());
        const std::vector<Heard> h =
            s.tracks.size() == 2 ? heard(s.tracks[1]) : std::vector<Heard>();

        check(h.size() == 1 && h[0].on == 960 && h[0].off == 961,
              "a very short note is a tick long",
              h.empty() ? "" : show(h[0]));
    }

    {
        /* Below about 3.58 bpm the tempo would overflow its 24 bits; the
           file says the slowest it can, and counts ticks at that. */
        thcMidiFile f(2);

        f.add(note(60, 0, 60, 100, 60), 0);
        f.end(120);

        const Smf s = parse(f.bytes());
        const std::string t = s.ok ? metaText(s.tracks[0], 0x51) : "";

        check(t.size() == 3 && (uint8_t)t[0] == 0xff &&
              (uint8_t)t[1] == 0xff && (uint8_t)t[2] == 0xff,
              "a tempo below the meta's range is clamped");

        const std::vector<Heard> h =
            s.tracks.size() == 2 ? heard(s.tracks[1]) : std::vector<Heard>();
        const double beat = 0xffffff / 1e6;       /* seconds, clamped */

        check(h.size() == 1 && h[0].on == (uint32_t)llround(60 / beat * 480),
              "and the note lands at the same second",
              h.empty() ? "" : show(h[0]));
    }

    printf("\n%s\n", failures ? "midicheck FAILED" : "midicheck ok");

    return failures ? 1 : 0;
}
