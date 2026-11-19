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

/*
 * routecheck -- live MIDI input routes: which engine channels a note goes
 * to (thMidiRoutes), and gthMidiPlay keeping each held note and the pedal
 * on the channels they went to while the routes change under them.
 *
 *   routecheck -p PLUGINS [SEED]
 *
 * The property runs are seeded; a failure names its seed, and SEED runs
 * that one alone.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "think.h"
#include "thMidiRoutes.h"
#include "gthMidiPlay.h"

namespace fs = std::filesystem;

static int failed = 0;

static void check (bool ok, const std::string &what,
                   const std::string &detail = "")
{
    printf("%s  %s%s%s\n", ok ? "ok  " : "FAIL", what.c_str(),
           detail.empty() ? "" : ": ", detail.c_str());

    if (!ok)
        failed++;
}

static std::string shown (const std::vector<int> &v)
{
    std::string s;

    for (int e : v)
        s += (s.empty() ? "" : " ") + std::to_string(e);

    return "[" + s + "]";
}

/* ---- resolution -------------------------------------------------------- */

struct Resolve
{
    const char *name;
    std::vector<thMidiRoute> machine, piece;
    int omni;                   /* -1: identity */
    int midi, note;
    std::vector<int> want;
};

static void
checkResolve (void)
{
    const Resolve rows[] = {
        { "identity: a MIDI channel plays the engine channel of its number",
          {}, {}, -1, 3, 60, { 3 } },
        { "omni: every MIDI channel plays the one engine channel",
          {}, {}, 7, 3, 60, { 7 } },
        { "a piece route takes its MIDI channel from identity, no bleed",
          {}, { { 3, 20 } }, -1, 3, 60, { 20 } },
        { "...and leaves another MIDI channel to the base",
          {}, { { 3, 20 } }, -1, 4, 60, { 4 } },
        { "a machine route replaces the piece's for its MIDI channel",
          { { 3, 30 } }, { { 3, 20 }, { 3, 21 } }, -1, 3, 60, { 30 } },
        { "...and only for that one",
          { { 3, 30 } }, { { 4, 20 } }, -1, 4, 60, { 20 } },
        { "off silences a MIDI channel",
          {}, { { 3, -1 } }, -1, 3, 60, {} },
        { "a machine off outranks a piece route",
          { { 3, -1 } }, { { 3, 20 } }, -1, 3, 60, {} },
        { "a layer plays both",
          {}, { { 0, 2 }, { 0, 5 } }, -1, 0, 60, { 2, 5 } },
        { "a split's low half takes its top key",
          {}, { { 0, 2, 0, 59 }, { 0, 5, 60, 127 } }, -1, 0, 59, { 2 } },
        { "...and its high half its bottom key",
          {}, { { 0, 2, 0, 59 }, { 0, 5, 60, 127 } }, -1, 0, 60, { 5 } },
        { "a key no range of its layer takes plays nothing",
          {}, { { 0, 2, 0, 59 } }, -1, 0, 60, {} },
        { "a MIDI channel past 16 is no MIDI channel",
          {}, {}, -1, 16, 60, {} },
        { "an engine channel past the engine's is dropped",
          {}, { { 0, TH_ENGINE_CHANNELS } }, -1, 0, 60, {} },
    };

    for (const Resolve &r : rows)
    {
        thMidiRoutes routes;
        std::vector<int> got;

        routes.set(thMidiRoutes::MACHINE, r.machine);
        routes.set(thMidiRoutes::PIECE, r.piece);

        if (r.omni >= 0)
            routes.setOmni(r.omni);

        routes.resolve(r.midi, r.note, got);
        check(got == r.want, r.name, shown(got));
    }

    /* Unrouted, no MIDI channel reaches an engine channel past 16. */
    {
        thMidiRoutes identity;
        std::vector<int> got, past;

        for (int m = 0; m < TH_MIDI_CHANNELS; m++)
        {
            identity.resolve(m, 60, got);

            for (int e : got)
                if (e >= TH_MIDI_CHANNELS)
                    past.push_back(e);
        }

        check(past.empty(), "an engine channel past 16 hears nothing "
              "unrouted", shown(past));
    }

    /* The pedal's reach: every engine channel some key goes to. */
    thMidiRoutes routes;
    std::vector<int> got;

    routes.set(thMidiRoutes::PIECE, { { 0, 2, 0, 59 }, { 0, 5, 60, 127 } });
    routes.reach(0, got);
    check(got == std::vector<int>({ 2, 5 }),
          "a split's pedal reaches both halves", shown(got));
}

/* ---- gthMidiPlay ------------------------------------------------------- */

static const char GRAPH[] =
    "node ionode { channels = 1; out0 = osc->out; play = env->play; };\n"
    "node freq misc::midi2freq { note = ionode->note; };\n"
    "node env env::adsr { a = 0; d = 0; s = th_max; r = 1 ms;\n"
    "                     trigger = ionode->trigger; };\n"
    "node osc osc::simple { freq = freq->out; waveform = 0; };\n"
    "io ionode;\n";

/* The engine channels with a graph on them: past MIDI's sixteen too. */
static const int LOADED = 20;

struct Rig
{
    thSynth synth;
    gthMidiPlay midi;

    /* The engine channels each note-on reached, by key, since cleared. */
    std::vector<std::pair<int, int> > ons;

    Rig (const std::string &plugins, const std::string &graph)
        : synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES),
          midi(&synth)
    {
        for (int c = 0; c < LOADED; c++)
            synth.loadTree(graph, c, 100);

        midi.noteOn = [this](int e, int note, int) { ons.push_back({ e, note }); };
    }

    void send (int status, int d1, int d2)
    {
        midi.play((unsigned char)status, (unsigned char)d1, (unsigned char)d2);
        synth.process();
    }

    void settle (void)
    {
        for (int i = 0; i < 200; i++)
            synth.process();
    }

    bool sounding (int e, int note)
    {
        thMidiChan *c = synth.getChannel(e);

        return c != NULL && c->getNote(note) != NULL;
    }

    /* Any voice anywhere, for the keys `notes' names. */
    std::vector<int> held (const std::vector<int> &notes)
    {
        std::vector<int> where;

        for (int e = 0; e < LOADED; e++)
            for (int n : notes)
                if (sounding(e, n))
                    where.push_back(e * 1000 + n);

        return where;
    }

    double noteOf (int e, int note)
    {
        thMidiNote *v = synth.getChannel(e)->getNote(note);

        return v != NULL ? (*v->synthTree()->IONode()->getArg("note"))[0]
                         : -1;
    }
};

static void
checkNamed (const std::string &plugins, const std::string &graph)
{
    {
        Rig r(plugins, graph);

        r.midi.routes().set(thMidiRoutes::PIECE, { { 0, 5 } });
        r.send(0xb0, 64, 127);              /* pedal down, onto 5 */
        r.send(0x90, 60, 100);
        r.send(0x80, 60, 0);
        r.settle();

        const bool kept = r.sounding(5, 60);    /* held by the pedal */

        r.midi.routes().set(thMidiRoutes::PIECE, { { 0, 7 } });
        r.send(0xb0, 64, 0);                /* up: 5 as well as 7 */
        r.settle();
        check(kept && !r.sounding(5, 60),
              "a pedal let up after a route change lets go where it went "
              "down");
    }

    {
        Rig r(plugins, graph);

        r.midi.routes().set(thMidiRoutes::PIECE, { { 0, 5 }, { 1, 7 } });
        r.send(0xb0, 64, 127);              /* MIDI 1's pedal onto 5 */
        r.send(0xb1, 64, 127);              /* MIDI 2's onto 7 */
        r.send(0x91, 60, 100);
        r.send(0x81, 60, 0);
        r.midi.routes().set(thMidiRoutes::PIECE, { { 0, 7 }, { 1, 7 } });
        r.send(0xb0, 64, 0);                /* 7 is MIDI 2's still */
        r.settle();

        const bool kept = r.sounding(7, 60);

        r.send(0xb1, 64, 0);
        r.settle();
        check(kept && !r.sounding(7, 60),
              "a pedal let up on a channel another still holds down leaves "
              "it down until that one lets up");
    }

    {
        Rig r(plugins, graph);

        r.midi.routes().set(thMidiRoutes::PIECE, { { 0, 5 } });
        r.send(0x90, 60, 100);
        r.midi.routes().set(thMidiRoutes::PIECE, { { 0, 7 } });
        r.send(0xe0, 0x7f, 0x7f);           /* two semitones up */
        r.settle();

        const double bent = r.noteOf(5, 60);

        check(bent > 61.9 && bent < 62.1 && !r.sounding(7, 60),
              "a bend after a route change bends the note where it is",
              std::to_string(bent));
    }

    {
        Rig r(plugins, graph);

        r.midi.routes().set(thMidiRoutes::PIECE, { { 0, 5 } });
        r.send(0x90, 60, 100);
        r.midi.routes().set(thMidiRoutes::PIECE, { { 0, 7 } });
        r.ons.clear();
        r.send(0x90, 60, 90);               /* the same key again */
        r.send(0x80, 60, 0);
        r.settle();
        check(r.ons.size() == 1 && r.ons[0].first == 5 &&
              !r.sounding(5, 60) && !r.sounding(7, 60),
              "a key struck again after a route change goes where it went",
              std::to_string(r.ons.size()) + " ons");
    }

    {
        Rig r(plugins, graph);

        r.midi.routes().set(thMidiRoutes::PIECE, { { 0, 5 } });
        r.midi.setMpe(2);
        r.send(0x90, 60, 100);
        r.settle();
        check(r.sounding(2, 60) && !r.sounding(5, 60) && !r.sounding(0, 60),
              "MPE outranks a route for its zone");
    }

    {
        Rig r(plugins, graph);

        r.midi.routes().set(thMidiRoutes::PIECE, { { 0, 18 } });
        r.send(0x90, 60, 100);
        r.settle();

        const bool there = r.sounding(18, 60) && !r.sounding(0, 60);

        r.send(0x80, 60, 0);
        r.settle();
        check(there && !r.sounding(18, 60),
              "a note routed past 16 plays there and ends there");
    }
}

/* Random notes, offs, repeated keys, bends, pedals and route changes over
   random tables: no engine channel outside a note's targets hears it, and
   once every key is up and every pedal let go no voice is left. */
static bool
property (const std::string &plugins, const std::string &graph,
          unsigned seed)
{
    std::mt19937 rng(seed);
    const auto pick = [&rng](int n) { return (int)(rng() % (unsigned)n); };
    const std::vector<int> keys = { 60, 61, 62, 63 };
    const int MIDI = 4;

    Rig r(plugins, graph);

    /* The model's own record: each held key's targets, as the table said
       when its on came. */
    std::vector<int> want[MIDI][128];
    bool down[MIDI][128] = {};

    const auto table = [&]()
    {
        std::vector<thMidiRoute> v;
        const int n = pick(5);

        for (int i = 0; i < n; i++)
        {
            const int lo = 58 + pick(8);

            v.push_back({ pick(MIDI), pick(8) == 0 ? -1 : pick(LOADED), lo,
                          lo + pick(6) });
        }

        return v;
    };

    for (int step = 0; step < 150; step++)
    {
        const int m = pick(MIDI), k = keys[pick((int)keys.size())];

        switch (pick(7))
        {
            case 0:
            case 1:
            {
                std::vector<int> expect;

                if (down[m][k])
                    expect = want[m][k];
                else
                    r.midi.routes().resolve(m, k, expect);

                r.ons.clear();
                r.send(0x90 | m, k, 1 + pick(127));

                std::set<int> got, wanted(expect.begin(), expect.end());

                for (const auto &on : r.ons)
                    got.insert(on.first);

                if (got != wanted)
                {
                    printf("      seed %u step %d: ch %d key %d reached %zu "
                           "channels, %zu wanted\n", seed, step, m, k,
                           got.size(), wanted.size());
                    return false;
                }

                want[m][k] = expect;
                down[m][k] = true;
                break;
            }
            case 2:
            case 3:
                r.send(0x80 | m, k, 0);
                down[m][k] = false;
                break;
            case 4:
                r.send(0xe0 | m, pick(128), pick(128));
                break;
            case 5:
                r.send(0xb0 | m, 64, pick(2) ? 127 : 0);
                break;
            case 6:
                r.midi.routes().set(pick(2) ? thMidiRoutes::PIECE
                                            : thMidiRoutes::MACHINE, table());
                break;
        }
    }

    for (int m = 0; m < MIDI; m++)
    {
        for (int k : keys)
            if (down[m][k])
                r.send(0x80 | m, k, 0);

        r.send(0xb0 | m, 64, 0);
    }

    r.settle();

    const std::vector<int> left = r.held(keys);

    if (!left.empty())
        printf("      seed %u: %zu voices left, the first channel %d key %d\n",
               seed, left.size(), left[0] / 1000, left[0] % 1000);

    return left.empty();
}

int
main (int argc, char *argv[])
{
    std::string plugins = PLUGIN_PATH;
    long only = -1;

    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            plugins = argv[++i];
        else
            only = strtol(argv[i], NULL, 10);

    if (plugins.back() != '/')
        plugins += '/';

    checkResolve();

    const fs::path file = fs::temp_directory_path() /
                          ("routecheck-" + std::to_string(getpid()) + ".dsp");

    std::ofstream(file) << GRAPH;
    checkNamed(plugins, file.string());

    int bad = 0;
    const unsigned first = only >= 0 ? (unsigned)only : 1;
    const unsigned last = only >= 0 ? (unsigned)only : 40;

    for (unsigned seed = first; seed <= last; seed++)
        if (!property(plugins, file.string(), seed))
            bad++;

    check(bad == 0, "random play over random routes reaches only its "
          "targets and leaves nothing held",
          std::to_string(last - first + 1 - bad) + " of " +
          std::to_string(last - first + 1) + " seeds");

    fs::remove(file);
    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");

    return failed;
}
