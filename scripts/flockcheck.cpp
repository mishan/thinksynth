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
 * flockcheck -- a population of instruments, bred (src/NodeBreed.h).
 *
 * A saw with its filter taken out, bred toward itself as written, comes
 * nearer it; the same dice breed the same flock, through a save and
 * a load on the way; with no target the champion keeps changing and keeps
 * its level; and no member goes past the depth allowed.
 *
 *   flockcheck -p build/plugins/
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "think.h"
#include "NodeBreed.h"
#include "NodeEvolve.h"

using std::string;
using std::vector;

static int failed = 0;

static void okOrFail (bool good, const string &what, const string &detail)
{
    if (good)
        printf("ok    %s\n", what.c_str());
    else
    {
        printf("FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : ": ",
               detail.c_str());
        failed++;
    }

    fflush(stdout);
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;

    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            pluginPath = argv[++i];

    /* An enveloped saw through a filter, and the same with the filter
       taken out: small, so a generation is seconds. */
    const string filtered =
        "node ionode { channels = 1; out0 = vca->out; play = env->play; };\n"
        "io ionode;\n"
        "node freq misc::midi2freq { note = ionode->note; };\n"
        "node osc osc::simple { freq = freq->out; amp = ionode->velocity; "
        "waveform = 1; };\n"
        "node filt filt::svf { in = osc->out; cutoff = 700; res = 0.3; };\n"
        "node env env::adsr { a = 5 ms; d = 200 ms; s = 0.7; r = 100 ms;\n"
        "    trigger = ionode->trigger; };\n"
        "node vca mixer::mul { in0 = filt->out_low; in1 = env->out; };\n";
    const string svf = "node filt filt::svf { in = osc->out; cutoff = 700; "
                       "res = 0.3; };\n";
    string bare = filtered;

    bare.erase(bare.find(svf), svf.size());
    bare.replace(bare.find("in0 = filt->out_low"),
                 strlen("in0 = filt->out_low"), "in0 = osc->out");

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    NodeBreed::Params params;
    string champion, why;
    size_t depth = 0;

    params.population = 6;
    params.tune = 3;
    params.kinds = "splice";

    /* Toward the graph with the filter. */
    {
        const NodeBreed::Ear ear = NodeBreed::localEar(pluginPath, bare,
                                                       filtered, "");
        const double from = ear.target(bare);
        NodeBreed::Flock flock;
        double nearest = from;
        bool bred = true;

        for (unsigned int g = 1; bred && g <= 2; g++)
        {
            bred = NodeBreed::generation(&synth, bare, flock, params, g, ear,
                                         champion, depth, why);
            nearest = std::min(nearest, ear.target(champion));
        }

        char said[96];

        snprintf(said, sizeof said, "%.2f dB -> %.2f dB", from, nearest);
        okOrFail(bred && nearest < from - 1, "a flock bred toward a target "
                 "comes nearer it", bred ? said : why);
    }

    /* The same dice, the same flock -- one of them saved and loaded
       between every generation, as a host keeps it. */
    {
        const NodeBreed::Ear ear = NodeBreed::localEar(pluginPath, bare, "",
                                                       "");
        NodeBreed::Flock kept, carried;
        bool same = true, changed = false, level = true, deep = true;
        string last;
        double rms, peak, baseRms = 0, basePeak = 0;

        params.depth = 2;
        params.kinds = "";
        ear.level(bare, baseRms, basePeak);

        for (unsigned int g = 1; same && g <= 4; g++)
        {
            string a, b;
            size_t da, db;

            same = NodeBreed::generation(&synth, bare, kept, params, 100 + g,
                                         ear, a, da, why);

            NodeBreed::Flock loaded;

            same &= NodeBreed::load(NodeBreed::save(carried), loaded) &&
                    NodeBreed::generation(&synth, bare, loaded, params,
                                          100 + g, ear, b, db, why) &&
                    a == b && da == db &&
                    NodeBreed::save(loaded) == NodeBreed::save(kept);
            carried = loaded;

            changed |= g > 1 && a != last;
            last = a;
            level &= ear.level(a, rms, peak) &&
                     NodeMutate::keepsLevel(rms, peak, baseRms, basePeak);

            for (const NodeBreed::Member &m : kept.members)
                deep &= m.ops.size() <= params.depth;
        }

        okOrFail(same, "the same dice breed the same flock, saved and "
                 "loaded between generations or not", why);
        okOrFail(changed && level, "with no target the champion keeps "
                 "changing, and keeps its level", "");
        okOrFail(deep, "no member is more edits from the declared graph "
                 "than the depth allowed", "");
    }

    NodeBreed::Flock bad;

    okOrFail(!NodeBreed::load("flock\t0\t5\nab", bad) &&
                 !NodeBreed::load("member\t0\n", bad),
             "a flock that is not one does not load", "");

    printf("\n%d failure(s)\n", failed);

    return failed;
}
