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
 * notebendcheck -- one held note's pitch, and its aux values, moved while
 * it sounds, and nothing else's.
 *
 * A note is found by the whole number it was played at, so a bend that
 * moved that number would leave its off with nothing to find: the first
 * thing held here is that a bent note still ends. The rest is that a bend
 * is the voice's own (another held note doesn't move), that it is measured
 * from the note and not added up, and that aux reaches a graph that reads
 * it.
 */

#include "config.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "think.h"

namespace fs = std::filesystem;
using std::string;

static int failed = 0;

static void
report (const string &name, const string &why)
{
    if (why.empty())
        printf("ok    %s\n", name.c_str());
    else
    {
        printf("FAIL  %s\n  %s\n", name.c_str(), why.c_str());
        failed++;
    }
}

static const char GRAPH[] =
    "node ionode { channels = 1; out0 = osc->out; play = env->play; };\n"
    "node freq misc::midi2freq { note = ionode->note; };\n"
    "node env env::adsr { a = 0; d = 0; s = th_max; r = 1 ms;\n"
    "                     trigger = ionode->trigger; };\n"
    "node osc osc::simple { freq = freq->out; waveform = 0;\n"
    "                       amp = 0.5 + ionode->aux0 * 0; };\n"
    "io ionode;\n";

/* What a held voice's io node says its note, or an aux, is now. */
static float
io (thSynth &synth, int note, const char *arg)
{
    thMidiNote *v = synth.getChannel(0)->getNote(note);
    thArg *a = v ? v->synthTree()->IONode()->getArg(arg) : NULL;

    return a ? (*a)[0] : NAN;
}

int
main (int argc, char *argv[])
{
    string plugins = PLUGIN_PATH;

    if (argc > 2 && !strcmp(argv[1], "-p"))
        plugins = argv[2];
    if (plugins.back() != '/')
        plugins += '/';

    const fs::path file = fs::temp_directory_path() / "notebendcheck.dsp";

    std::ofstream(file) << GRAPH;

    thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    const auto run = [&](int n) { for (int i = 0; i < n; i++) synth.process(); };
    char got[96];

    if (synth.loadTree(file.string(), 0, 100) == NULL)
    {
        report("the graph loads", "it doesn't");
        return 1;
    }

    synth.addNote(0, 69, 100);
    synth.addNote(0, 57, 100);
    run(2);

    synth.bendNote(0, 69, 12);
    run(2);
    snprintf(got, sizeof(got), "69 reads %g, 57 reads %g", io(synth, 69, "note"),
             io(synth, 57, "note"));
    report("a bend moves its own note and no other",
           io(synth, 69, "note") == 81 && io(synth, 57, "note") == 57 ? "" : got);

    synth.bendNote(0, 69, -0.5f);
    run(1);
    snprintf(got, sizeof(got), "69 reads %g", io(synth, 69, "note"));
    report("a bend is from the note, not from the last bend",
           io(synth, 69, "note") == 68.5f ? "" : got);

    synth.setNoteAux(0, 57, 0, 0.75f);
    run(1);
    snprintf(got, sizeof(got), "57's aux0 reads %g", io(synth, 57, "aux0"));
    report("an aux reaches a graph that reads it",
           io(synth, 57, "aux0") == 0.75f ? "" : got);

    synth.delNote(0, 69);
    run(200);
    /* Where a bend that re-keyed the voice would have left it, too. */
    report("a bent note still ends",
           !synth.getChannel(0)->getNote(69) && !synth.getChannel(0)->getNote(68) &&
           !synth.getChannel(0)->getNote(81) ? "" : "it is still sounding");

    synth.bendNote(0, 69, 3);
    synth.bendNote(0, 99, 3);
    synth.setNoteAux(0, 57, 3, 1);
    run(1);
    report("a bend or an aux with no note, or no aux, to go to is nothing",
           io(synth, 57, "note") == 57 && io(synth, 57, "aux0") == 0.75f ? "" :
           "57 moved");

    fs::remove(file);
    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");

    return failed;
}
