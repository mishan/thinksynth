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

/*
 * mpecheck -- MIDI's pitch bend, and an MPE controller's per-note bend,
 * pressure and timbre, reaching the notes they are for.
 *
 * Each row plays MIDI bytes into gthMidiPlay on a real synth and reads back
 * what each held note's voice says its note and aux values are now.
 */

#include "config.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "think.h"
#include "gthMidiPlay.h"

namespace fs = std::filesystem;
using std::string;

static int failed = 0;

static const char GRAPH[] =
    "node ionode { channels = 1; out0 = osc->out; play = env->play; };\n"
    "node freq misc::midi2freq { note = ionode->note; };\n"
    "node env env::adsr { a = 0; d = 0; s = th_max; r = 1 ms;\n"
    "                     trigger = ionode->trigger; };\n"
    "node osc osc::simple { freq = freq->out; waveform = 0;\n"
    "                       amp = 0.5 + ionode->aux0 * 0 + ionode->aux1 * 0; };\n"
    "io ionode;\n";

/* "note=<n> a0=<x> a1=<y>" for a held note on a synth channel, or "gone". */
static string
voice (thSynth &synth, int chan, int note)
{
    thMidiNote *v = synth.getChannel(chan)->getNote(note);
    char s[64];

    if (!v)
        return "gone";

    thNode *io = v->synthTree()->IONode();

    /* Two places: a 14-bit bend doesn't land on a semitone exactly. */
    snprintf(s, sizeof(s), "note=%.2f a0=%.2f a1=%.2f", (*io->getArg("note"))[0],
             (*io->getArg("aux0"))[0], (*io->getArg("aux1"))[0]);
    return s;
}

struct Row
{
    const char *name;
    int         mpe;        /* the synth channel MPE folds to, or -1 */
    const char *midi;       /* hex messages, three bytes each */
    int         chan, note; /* the voice to read */
    const char *want;
};

static const Row ROWS[] = {
    { "a bend wheel bends what its channel holds", -1,
      "90 3c 64  e0 7f 7f", 0, 60, "note=62.00 a0=0.00 a1=0.00" },
    { "a bend wheel at rest is the note", -1,
      "90 3c 64  e0 00 40", 0, 60, "note=60.00 a0=0.00 a1=0.00" },
    { "a note played while bent starts bent", -1,
      "e0 00 60  90 3c 64", 0, 60, "note=61.00 a0=0.00 a1=0.00" },
    { "another channel's bend is its own", -1,
      "90 3c 64  e1 7f 7f", 0, 60, "note=60.00 a0=0.00 a1=0.00" },
    { "a bent note still ends", -1,
      "90 3c 64  e0 7f 7f  80 3c 00", 0, 60, "gone" },
    { "MPE plays every member channel on one synth channel", 2,
      "93 3c 64", 2, 60, "note=60.00 a0=0.00 a1=0.00" },
    { "a member's bend is its own note's, 48 semitones", 2,
      "92 3c 64  93 40 64  e2 00 50", 2, 60, "note=72.00 a0=0.00 a1=0.00" },
    { "...and not the other's", 2,
      "92 3c 64  93 40 64  e2 00 50", 2, 64, "note=64.00 a0=0.00 a1=0.00" },
    { "the master's bend bends every note, two semitones", 2,
      "92 3c 64  93 40 64  e0 7f 7f", 2, 64, "note=66.00 a0=0.00 a1=0.00" },
    { "a member's and the master's add", 2,
      "92 3c 64  e2 00 50  e0 00 60", 2, 60, "note=73.00 a0=0.00 a1=0.00" },
    { "pressure is aux0 and CC 74 aux1", 2,
      "92 3c 64  d2 40 00  b2 4a 7f", 2, 60, "note=60.00 a0=0.50 a1=1.00" },
    { "RPN 0 sets a member's bend range", 2,
      "b2 65 00  b2 64 00  b2 06 0c  92 3c 64  e2 7f 7f", 2, 60,
      "note=72.00 a0=0.00 a1=0.00" },
    { "a velocity of 0 is an off", 2,
      "92 3c 64  92 3c 00", 2, 60, "gone" },
};

int
main (int argc, char *argv[])
{
    string plugins = PLUGIN_PATH;

    if (argc > 2 && !strcmp(argv[1], "-p"))
        plugins = argv[2];
    if (plugins.back() != '/')
        plugins += '/';

    const fs::path file = fs::temp_directory_path() / "mpecheck.dsp";

    std::ofstream(file) << GRAPH;

    for (size_t r = 0; r < sizeof(ROWS) / sizeof(ROWS[0]); r++)
    {
        const Row &row = ROWS[r];
        thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
        gthMidiPlay midi(&synth);
        std::istringstream bytes(row.midi);
        std::vector<unsigned> b;
        unsigned x;

        for (int c = 0; c < 3; c++)
            synth.loadTree(file.string(), c, 100);
        midi.setMpe(row.mpe);
        while (bytes >> std::hex >> x)
            b.push_back(x);
        for (size_t i = 0; i + 2 < b.size(); i += 3)
        {
            midi.play(b[i], b[i + 1], b[i + 2]);
            synth.process();
        }
        for (int i = 0; i < 200; i++)
            synth.process();

        const string got = voice(synth, row.chan, row.note);

        if (got == row.want)
            printf("ok    %s\n", row.name);
        else
        {
            printf("FAIL  %s\n  read %s, wanted %s\n", row.name, got.c_str(),
                   row.want);
            failed++;
        }
    }

    fs::remove(file);
    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");

    return failed;
}
