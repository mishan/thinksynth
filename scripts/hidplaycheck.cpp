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
#include <sstream>
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

/* A profile, short: each binding control:what, each layer [name: ...]. */
static string
describe (const thcHidBlock &p)
{
    std::ostringstream o;

    o << "base" << p.base;
    for (size_t i = 0; i < p.bindings.size(); i++)
    {
        const thcHidBinding &b = p.bindings[i];

        o << " " << b.control << ":" << (b.kind == thcHidBinding::VALUE ? b.arg
            : b.kind == thcHidBinding::HOLD ? "hold " + b.layer
            : std::to_string(b.semis));
    }
    for (size_t l = 0; l < p.layers.size(); l++)
    {
        o << " [" << p.layers[l].name << ":";
        for (size_t i = 0; i < p.layers[l].bindings.size(); i++)
            o << " " << p.layers[l].bindings[i].control << ":"
              << p.layers[l].bindings[i].semis;
        o << "]";
    }
    return o.str();
}

/* What a file of the player's makes of the controller's own profile. */
static void
checkProfiles (const string &scratch)
{
    const thhControls &c = thhIndexControls();
    const char *own =
        "device index {\n"
        "    a = note 0;\n"
        "    b = note 7;\n"
        "    grip_force = \"level\";\n"
        "    grip_force = \"bright\";\n"
        "    stick_y = \"wobble\";\n"
        "    thumbstick = hold fist;\n"
        "    layer fist { a = note 12; };\n"
        "};\n";
    const string asIs = "base60 a:0 b:7 grip_force:level grip_force:bright "
                        "stick_y:wobble thumbstick:hold fist [fist: a:12]";
    struct Row
    {
        const char *name, *file;    /* NULL: there is no file */
        bool        ok;
        const char *want;           /* describe(), or the start of err */
    };
    const Row rows[] = {
        { "no file is the controller's own profile", NULL, true, NULL },
        { "an empty file is too", "device index { };", true, NULL },
        { "a control of the player's replaces the controller's",
          "device index { a = note 5; };", true,
          "base60 b:7 grip_force:level grip_force:bright stick_y:wobble "
          "thumbstick:hold fist a:5 [fist: a:12]" },
        { "every binding of the control, not just one",
          "device index { grip_force = \"cutoff\"; };", true,
          "base60 a:0 b:7 stick_y:wobble thumbstick:hold fist "
          "grip_force:cutoff [fist: a:12]" },
        { "a control ignored",
          "device index { ignore stick_y; };", true,
          "base60 a:0 b:7 grip_force:level grip_force:bright "
          "thumbstick:hold fist [fist: a:12]" },
        { "a layer of the player's replaces the one of its name",
          "device index { layer fist { a = note 24; }; };", true,
          "base60 a:0 b:7 grip_force:level grip_force:bright stick_y:wobble "
          "thumbstick:hold fist [fist: a:24]" },
        { "a setting of the player's",
          "device index { base = \"C3\"; };", true,
          "base48 a:0 b:7 grip_force:level grip_force:bright stick_y:wobble "
          "thumbstick:hold fist [fist: a:12]" },
        { "ignoring a layer the controller's own still holds",
          "device index { ignore fist; };", false,
          "the controller's own profile, line 7: there is no layer 'fist'" },
        { "ignoring what isn't there",
          "device index {\n  ignore nothing;\n};", false, "/mine:2: " },
        { "a control the device doesn't have",
          "device index {\n  zz = note 5;\n};", false, "/mine:2: " },
        { "a file that doesn't parse",
          "device index {\n  a = note\n", false, "/mine:" },
    };

    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
    {
        const Row &r = rows[i];
        const string path = scratch + "/mine";
        thcHidBlock blk;
        string err;

        fs::remove(path);
        if (r.file)
            std::ofstream(path) << r.file;

        const bool ok = thhLoadProfile(path, c, own, blk, err);
        const string want = r.want ? r.want : asIs;

        if (r.ok)
            report(r.name, ok && describe(blk) == want ? "" :
                   !ok ? "refused: " + err :
                   "read as\n    " + describe(blk) + "\n  wanted\n    " +
                   want);
        else
            report(r.name,
                   !ok && err.find(want) != string::npos &&
                   describe(blk) == asIs ? "" :
                   ok ? "accepted as " + describe(blk) :
                   "said \"" + err + "\", played " + describe(blk));
    }

    thcHidBlock blk;
    thhMapper m;
    std::vector<thhAction> none;
    string err;
    int line;

    report("the Index's own profile fits it",
           thcHidParseProfile(THH_INDEX_PROFILE, blk, err, line) &&
           m.configure(blk, c, none, err, line) ? "" : err);

    /* docs/HID.md's example of a profile of changes, as written there. */
    std::ofstream(scratch + "/example") <<
        "device index {\n"
        "    grip_mid = \"wobble\";          # in place of growl\n"
        "    ignore stick_y;               # no vibrato on the stick\n"
        "    layer fist {                  # a fist of your own hand's measure\n"
        "        when { grip_top = 0.7; grip_mid = 0.7; grip_low = 0.7; };\n"
        "        roll = \"bend\" { min = -1; max = 1; smooth = 0.05; };\n"
        "    };\n"
        "};\n";
    report("docs/HID.md's example fits the Index's own profile",
           thhLoadProfile(scratch + "/example", c, THH_INDEX_PROFILE, blk,
                          err) ? "" : err);

    std::ofstream(scratch + "/unreadable") << "device index { };\n";
    fs::permissions(scratch + "/unreadable", fs::perms::none);
    if (!std::ifstream(scratch + "/unreadable"))
        report("a file it may not read says so",
               !thhLoadProfile(scratch + "/unreadable", c, own, blk, err) &&
               err.find(scratch + "/unreadable") == 0 ? "" :
               "said \"" + err + "\"");
    else
        printf("skip  a file it may not read (it can read anything)\n");
    /* Windows takes none as read-only, which remove_all can't delete. */
    fs::permissions(scratch + "/unreadable", fs::perms::owner_all);
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

    /* A held note's bend reaches its voice. */
    thhPlay(synth, { action(thhAction::NOTE_ON, 1, 62, NULL, 0) }, hear);
    thhAction bend = action(thhAction::NOTE_BEND, 1, 62, NULL, 1.5);

    thhPlay(synth, { bend }, hear);
    run(2);

    thMidiNote *bent = synth.getChannel(1)->getNote(62);
    thArg *bentTo = bent ? bent->synthTree()->IONode()->getArg("note") : NULL;

    report("a bend reaches the note's voice",
           bentTo && (*bentTo)[0] == 63.5f ? "" : "it doesn't");
    thhPlay(synth, { action(thhAction::NOTE_OFF, 1, 62, NULL, 0) }, hear);
    heard -= 2;

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

/* index.dsp panned all the way by its knob, as a controller's roll pans it:
   the side it is panned to sounds and the other doesn't. */
static void
checkPan (const string &plugins, const string &dsp)
{
    const double ends[2] = { 0, 1 };    /* t: pan -1 and pan 1 */

    for (int e = 0; e < 2; e++)
    {
        thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        if (synth.loadTree(dsp + "/index.dsp", 0, 100) == NULL)
        {
            report("index.dsp loads", "it doesn't");
            return;
        }

        thhAction pan = action(thhAction::VALUE, 0, 0, "pan", ends[e]);

        thhPlay(synth, { pan, action(thhAction::NOTE_ON, 0, 60, NULL, 0) },
                NULL);

        double side[2] = { 0, 0 };

        for (int w = 0; w < 40; w++)
        {
            synth.process();

            const thMidiChan *c = synth.getChannel(0);
            const int len = synth.getWindowlen();

            /* Interleaved: sample i of side ch at i * channels + ch. */
            for (int ch = 0; ch < 2 && ch < c->numChannels(); ch++)
                for (int i = 0; i < len; i++)
                {
                    const float x = c->output()[i * c->numChannels() + ch];

                    side[ch] += (double)x * x;
                }
        }

        const int loud = e ? 1 : 0;
        char got[96];

        snprintf(got, sizeof(got), "left %.3g, right %.3g", side[0], side[1]);
        report(e ? "index.dsp panned right is on the right"
                 : "index.dsp panned left is on the left",
               side[loud] > 1 && side[1 - loud] < side[loud] * 1e-3 ? "" : got);
    }
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
    checkPan(plugins, dsp);
    fs::remove_all(scratch);

    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");

    return failed;
}
