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
 * unitcheck -- the engine running compiled units (thUnit.h).
 *
 * The fingerprint: two loads of a graph match where nothing but the rate,
 * a knob or the loader differs, and not where one number, one wire or
 * one unit does.
 */

#include "config.h"

#include <stdio.h>

#include <fstream>
#include <sstream>
#include <string>

#include "think.h"
#include "thSynth.h"
#include "thSynthTree.h"

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

static string
slurp (const string &path)
{
    std::ifstream in(path);
    std::stringstream s;

    s << in.rdbuf();
    return s.str();
}

/* `text' with `from' replaced by `to', which must be in it. */
static string
edit (string text, const string &from, const string &to)
{
    const size_t at = text.find(from);

    if (at == string::npos)
        return "";
    return text.replace(at, from.size(), to);
}

/* The fingerprint of `text' loaded at `rate', as an instrument or an
   effect, or "" for one that didn't load. */
static string
print (const string &plugins, const string &text, int rate, bool effect)
{
    thSynth synth(plugins, 1000, rate, 3);
    thSynthTree *tree = effect ? synth.loadMasterEffectText("fx", text)
                               : synth.loadTreeText("dsp", text, 0, MIDIVALMAX);

    return tree ? tree->fingerprint() : "";
}

int
main (int argc, char **argv)
{
    string plugins = "plugins/", video = "video/";

    for (int i = 1; i + 1 < argc; i += 2)
        if (string(argv[i]) == "-p")
            plugins = argv[i + 1];
        else if (string(argv[i]) == "-v")
            video = argv[i + 1];

    const string rings = slurp(video + "rings.dsp"), palette = slurp(video + "fx/palette.dsp");
    const int R = 1152000;
    const string a = print(plugins, rings, R, false);
    const struct
    {
        const char *name;
        string other;
        bool same;
    } PAIRS[] = {
        { "at another rate", print(plugins, rings, 48000, false), true },
        { "with another knob", print(plugins, edit(rings, "@speed = 4;", "@speed = 9;"), R,
                                     false), true },
        { "with one number changed", print(plugins, edit(rings, "in1 = 0.5", "in1 = 0.6"), R,
                                           false), false },
        { "rewired", print(plugins, edit(rings, "out1 = g->out", "out1 = b->out"), R, false),
          false },
        { "with 500 ms as 0.5 s", print(plugins, edit(rings, "r = 500 ms", "r = 0.5 s"), R,
                                        false), false },
        { "with midi2freq's note a number", print(plugins, edit(rings, "note = ionode->note",
                                                                "note = 60"), R, false),
          false },
    };

    report("rings.dsp has a fingerprint", a.empty() ? "it's empty" : "");
    for (const auto &p : PAIRS)
        report(string("rings.dsp ") + (p.same ? "matches itself " : "doesn't match itself ") +
                   p.name,
               p.other.empty() ? "it didn't load"
               : (p.other == a) != p.same ? (p.same ? "they differ" : "they match") : "");

    const string loaded = print(plugins, palette, R, false), fx = print(plugins, palette, R, true);

    report("palette.dsp matches itself loaded as an instrument and as an effect",
           loaded.empty() || fx.empty() ? "it didn't load"
           : loaded != fx ? "they differ" : "");

    return failed ? 1 : 0;
}
