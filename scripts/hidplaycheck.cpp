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
 * hidplaycheck -- a profile found or not, and a controller's actions played
 * on a real synth.
 *
 * dsp/riser.dsp is the instrument: it sustains while a note is held, so a
 * note that is still sounding a long while after its on, and gone soon
 * after its off, says the off arrived; and its @from (40..4000) is a value
 * to aim at.
 */

#include "config.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "thhIndex.h"
#include "thhPlay.h"

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

static void
checkProfiles (const string &scratch)
{
    const thhControls &c = thhIndexControls();
    thcHidBlock blk;
    thhMapper m;
    std::vector<thhAction> none;
    string err;
    int line;

    report("the Index's own profile fits it",
           thcHidParseProfile(THH_INDEX_PROFILE, blk, err, line) &&
           m.configure(blk, c, none, err, line) ? "" : err);

    const bool missing = thhLoadProfile(scratch + "/nothing-here", c,
                                        THH_INDEX_PROFILE, blk, err);

    report("no file is the Index's own profile",
           missing && !blk.bindings.empty() ? "" : "said \"" + err + "\"");

    std::ofstream(scratch + "/mine") << "device index {\n  a = note 5;\n};\n";
    report("a file of the player's own",
           thhLoadProfile(scratch + "/mine", c, THH_INDEX_PROFILE, blk, err) &&
           blk.bindings.size() == 1 && blk.bindings[0].semis == 5 ? "" :
           "said \"" + err + "\"");

    std::ofstream(scratch + "/wrong") << "device index {\n  zz = note 5;\n};\n";

    const bool wrong = thhLoadProfile(scratch + "/wrong", c, THH_INDEX_PROFILE,
                                      blk, err);

    report("a file that doesn't fit says where, and plays the Index's own",
           !wrong && err.find(scratch + "/wrong:2: ") == 0 &&
           blk.bindings.size() > 1 ? "" : "said \"" + err + "\"");

    std::ofstream(scratch + "/unreadable") << "device index { };\n";
    fs::permissions(scratch + "/unreadable", fs::perms::none);
    if (!std::ifstream(scratch + "/unreadable"))
        report("a file it may not read says so",
               !thhLoadProfile(scratch + "/unreadable", c, THH_INDEX_PROFILE,
                               blk, err) &&
               err.find(scratch + "/unreadable") == 0 ? "" :
               "said \"" + err + "\"");
    else
        printf("skip  a file it may not read (it can read anything)\n");
    /* Windows takes none as read-only, which remove_all can't delete. */
    fs::permissions(scratch + "/unreadable", fs::perms::owner_all);

    std::ofstream(scratch + "/broken") << "device index {\n  a = note\n";
    report("a file that doesn't parse says where",
           !thhLoadProfile(scratch + "/broken", c, THH_INDEX_PROFILE, blk,
                           err) &&
           err.find(scratch + "/broken:") == 0 ? "" : "said \"" + err + "\"");
}

static thhAction
action (thhAction::Kind kind, int channel, int note, const char *arg,
        double t)
{
    thhAction a = thhAction();

    a.kind = kind;
    a.channel = channel;
    a.note = note;
    a.vel = 90;
    a.arg = arg ? arg : "";
    a.t = t;
    return a;
}

static void
checkPlay (const string &plugins, const string &dsp)
{
    thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    int heard = 0;
    const auto hear = [&](const thhAction &) { heard++; };
    const auto run = [&](int windows) {
        for (int i = 0; i < windows; i++)
            synth.process();
    };

    if (synth.loadTree(dsp + "/riser.dsp", 0, 100) == NULL ||
        synth.loadTree(dsp + "/riser.dsp", 1, 100) == NULL)
    {
        report("riser.dsp loads", "it doesn't");
        return;
    }

    thhPlay(synth, { action(thhAction::NOTE_ON, 1, 60, NULL, 0) }, hear);
    run(200);
    report("a note on sounds on its channel and no other",
           synth.getChannel(1)->getNote(60) &&
           !synth.getChannel(0)->getNote(60) &&
           heard == 1 ? "" : "heard " + std::to_string(heard));

    thhPlay(synth, { action(thhAction::NOTE_OFF, 1, 60, NULL, 0) }, hear);

    int windows = 0;

    while (synth.getChannel(1)->getNote(60) && windows < 20000)
    {
        synth.process();
        windows++;
    }
    report("a note off ends it", windows < 20000 && heard == 2 ? "" :
           "still sounding after " + std::to_string(windows) + " windows");

    thArg *from = synth.getChanArg(1, "from");
    thhAction v = action(thhAction::VALUE, 1, 0, "from", 0.5);

    thhPlay(synth, { v }, hear);
    report("a value with no range of its own takes the arg's",
           from && std::fabs((*from)[0] - 2020) < 1e-3 ? "" :
           from ? "from is " + std::to_string((*from)[0]) : "no @from");

    v.t = 0.25;
    v.hasMin = v.hasMax = true;
    v.min = 100;
    v.max = 200;
    thhPlay(synth, { v }, hear);
    report("a value with the profile's range",
           from && std::fabs((*from)[0] - 125) < 1e-3 ? "" :
           "from is " + std::to_string(from ? (*from)[0] : 0));

    thhPlay(synth, { action(thhAction::VALUE, 1, 0, "nope", 0.5),
                     action(thhAction::VALUE, 5, 0, "from", 0.5),
                     action(thhAction::VALUE, 99, 0, "from", 0.5) }, hear);
    report("a value with nowhere to go is dropped",
           heard == 2 && from && std::fabs((*from)[0] - 125) < 1e-3 ? "" :
           "heard " + std::to_string(heard));
}

int
main (int argc, char *argv[])
{
    string plugins = PLUGIN_PATH, dsp;

    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            plugins = argv[++i];
        else
            dsp = argv[i];

    if (dsp.empty())
    {
        fprintf(stderr, "usage: hidplaycheck [-p plugins/] <dsp/>\n");
        return 2;
    }
    if (plugins.back() != '/')
        plugins += '/';

    const fs::path scratch = fs::temp_directory_path() /
        ("hidplaycheck-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;

    if (!fs::create_directory(scratch, ec))
    {
        fprintf(stderr, "can't make %s\n", scratch.string().c_str());
        return 2;
    }

    checkProfiles(scratch.string());
    checkPlay(plugins, dsp);
    fs::remove_all(scratch);

    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");

    return failed;
}
