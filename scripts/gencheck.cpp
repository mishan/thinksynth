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

/* gencheck -- the composer framework's gate.
 *
 * Three claims are held down here, because each is the kind that decays
 * silently if nothing is watching:
 *
 * 1. The shared pitch parser is right, at the values people argue about
 *    (middle C, the octave boundary, flats on C).
 *
 * 2. The loader rejects what GEN_FORMAT.md says it rejects, with the
 *    file and line in the message. Each bad file is generated here --
 *    the corpus cannot contain them, for the same reason argtype builds
 *    its own .dsp files.
 *
 * 3. Replay determinism: the same .gen with its pinned seed, rendered
 *    twice through the virtual clock with a reset between, delivers a
 *    byte-identical event stream. This is the framework's foundational
 *    promise ("same file + same seed = same piece") and the reason
 *    every composer draws randomness from its instance seed.
 *
 * Headless on purpose: no display, no audio device, no Glib main loop.
 * The scheduler's stepTransport is the virtual clock; sigDelivered is
 * the tape.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <glibmm.h>

#include "think.h"

#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"

static int failures = 0;

static void
fail (const std::string &what)
{
    fprintf(stderr, "gencheck: FAIL: %s\n", what.c_str());
    failures++;
}

/* ---- 1. the pitch parser ---------------------------------------------- */

static void
checkNote (const char *text, int expect)
{
    std::vector<int> out;
    std::string bad;

    if (!thcGenLoader::parseNoteList(text, out, bad) || out.size() != 1)
    {
        fail(std::string("parseNoteList refused '") + text + "'");
        return;
    }

    if (out[0] != expect)
    {
        std::ostringstream s;

        s << "'" << text << "' resolved to " << out[0]
          << ", wanted " << expect;
        fail(s.str());
    }
}

static void
checkNotes (void)
{
    checkNote("C4", 60);        /* middle C, the convention the spec pins */
    checkNote("A4", 69);
    checkNote("C0", 12);
    checkNote("A0", 21);        /* bottom of the piano                    */
    checkNote("G9", 127);       /* top of MIDI                            */
    checkNote("C#4", 61);
    checkNote("Db4", 61);       /* enharmonic agreement                   */
    checkNote("Cb4", 59);       /* a flat can cross the octave boundary   */
    checkNote("B#3", 60);       /* and so can a sharp                     */

    std::vector<int> out;
    std::string bad;

    if (!thcGenLoader::parseNoteList("F3 Ab3 C4", out, bad) ||
        out.size() != 3 || out[0] != 53 || out[1] != 56 || out[2] != 60)
        fail("space-separated list did not resolve to 53,56,60");

    if (!thcGenLoader::parseNoteList("F3,Ab3,C4", out, bad) ||
        out.size() != 3)
        fail("comma-separated list did not resolve");

    if (thcGenLoader::parseNoteList("H3", out, bad))
        fail("'H3' was accepted; H is not a note");

    if (thcGenLoader::parseNoteList("C", out, bad))
        fail("'C' with no octave was accepted");

    if (thcGenLoader::parseNoteList("", out, bad))
        fail("an empty note list was accepted");
}

/* ---- module loading --------------------------------------------------- */

static void
loadComposers (const std::string &pluginDir,
               std::map<std::string, thcPlugin *> &out)
{
    std::filesystem::path root =
        std::filesystem::path(pluginDir) / "composer";
    std::error_code ec;

    if (!std::filesystem::is_directory(root, ec))
        return;

    for (const auto &f : std::filesystem::directory_iterator(root, ec))
    {
        if (ec)
            break;

        if (f.path().extension() != PLUGIN_SUFFIX)
            continue;

        thcPlugin *p = new thcPlugin(f.path().string());

        if (p->state() != thcPlugin::LOADED)
        {
            delete p;
            continue;
        }

        out[p->name()] = p;
    }
}

/* ---- 2. loader validation --------------------------------------------- */

/* Write `body' to a scratch .gen, load it, and demand it fails with a
 * message mentioning `expect'. The message contract matters as much as
 * the rejection: "by name and line" is what makes an error actionable. */
static void
expectReject (const std::map<std::string, thcPlugin *> &plugins,
              thSynth *synth, const char *label, const std::string &body,
              const std::string &expect)
{
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        (std::string("gencheck-") + label + ".gen");

    {
        std::ofstream out(path);

        out << body;
    }

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (loader.load(path.string(), &sched))
        fail(std::string(label) + ": a file that should not load, loaded");
    else
    {
        bool found = false;

        for (size_t i = 0; i < loader.errors().size(); i++)
            if (loader.errors()[i].find(expect) != std::string::npos)
                found = true;

        if (!found)
        {
            std::string got = loader.errors().empty()
                ? "(no errors recorded)" : loader.errors()[0];

            fail(std::string(label) + ": rejected, but the message was '" +
                 got + "' with no mention of '" + expect + "'");
        }

        if (sched.chainCount() != 0)
            fail(std::string(label) +
                 ": a failed load left chains in the scheduler");
    }

    std::filesystem::remove(path);
}

static void
checkValidation (const std::map<std::string, thcPlugin *> &plugins,
                 thSynth *synth)
{
    /* A duration with no unit: the whole point of §2 of the format. */
    expectReject(plugins, synth, "bare-duration",
        "chain c { stage s gen::eno_line { period = 20; };"
        " sink { channel = 0; }; };",
        "write a unit");

    /* A plugin that does not exist, by name. */
    expectReject(plugins, synth, "no-such-plugin",
        "chain c { stage s gen::no_such_thing { };"
        " sink { channel = 0; }; };",
        "no_such_thing");

    /* A param the plugin never registered, by name. */
    expectReject(plugins, synth, "no-such-param",
        "chain c { stage s gen::eno_line { frobnicate = 3; };"
        " sink { channel = 0; }; };",
        "frobnicate");

    /* gen:: asked of a transformer. */
    expectReject(plugins, synth, "wrong-role",
        "chain c { stage s gen::quantize { };"
        " sink { channel = 0; }; };",
        "cannot be a gen:: stage");

    /* A knob used before it is declared. */
    expectReject(plugins, synth, "undeclared-knob",
        "chain c { stage s gen::eno_line { prob = @nope; };"
        " sink { channel = 0; }; };",
        "@nope");

    /* A chain with no sink has nowhere to deliver. */
    expectReject(plugins, synth, "no-sink",
        "chain c { stage s gen::eno_line { }; };",
        "has no sink");

    /* Textual order is execution order; a stage after a sink is a
       contradiction, not a style choice. */
    expectReject(plugins, synth, "stage-after-sink",
        "chain c { stage s gen::eno_line { }; sink { channel = 0; };"
        " stage t xform::quantize { }; };",
        "stage after sink");

    /* All transformers and no input: nothing would ever flow. */
    expectReject(plugins, synth, "no-source",
        "chain c { stage s xform::quantize { };"
        " sink { channel = 0; }; };",
        "no generator");

    /* A scale nobody declared, by name. */
    expectReject(plugins, synth, "no-such-scale",
        "chain c { stage s gen::eno_line { notes = ghost; };"
        " sink { channel = 0; }; };",
        "ghost");

    /* A seed after a chain cannot mean what it says. */
    expectReject(plugins, synth, "late-seed",
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 0; }; };\n"
        "seed 42;",
        "before the first chain");
}

/* ---- 3. replay determinism -------------------------------------------- */

/* Render `seconds' of the piece through the virtual clock and tape every
 * delivered event, at full precision -- %.17g is round-trip-exact for a
 * double, so two identical streams compare identical and two different
 * ones cannot collide. */
static std::string
render (thcScheduler &sched, double seconds, double step)
{
    std::string tape;

    sigc::connection conn = sched.sigDelivered.connect(
        [&tape](const thcEvent &ev)
        {
            char buf[160];

            if (ev.type == THC_EV_NOTE)
                snprintf(buf, sizeof(buf), "N %.17g %d %d %d %.17g\n",
                         ev.at, ev.channel, ev.u.note.note,
                         ev.u.note.velocity, ev.u.note.duration);
            else
                snprintf(buf, sizeof(buf), "C %.17g %d %s %.17g\n",
                         ev.at, ev.channel,
                         ev.u.chanarg.name ? ev.u.chanarg.name : "",
                         (double)ev.u.chanarg.value);

            tape += buf;
        });

    sched.start();

    while (sched.now() < seconds)
        sched.stepTransport(step);

    sched.stop();
    conn.disconnect();

    return tape;
}

static void
checkReplay (const std::map<std::string, thcPlugin *> &plugins,
             thSynth *synth, const std::string &genFile)
{
    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(genFile, &sched))
    {
        for (size_t i = 0; i < loader.errors().size(); i++)
            fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

        fail(genFile + " did not load");
        return;
    }

    if (!loader.hasSeed())
        fail(genFile + " pins no seed; the replay gate needs one");

    std::string first = render(sched, 180.0, 0.02);

    sched.reset();

    std::string second = render(sched, 180.0, 0.02);

    if (first.empty())
        fail("three minutes of the piece delivered nothing at all");

    if (first != second)
    {
        fail("replay diverged: same file, same seed, different stream");

        /* Show where, because "different" alone is undebuggable. */
        size_t n = 0;

        while (n < first.size() && n < second.size() &&
               first[n] == second[n])
            n++;

        size_t line0 = first.rfind('\n', n);

        line0 = line0 == std::string::npos ? 0 : line0 + 1;

        fprintf(stderr, "  first : %.60s\n", first.c_str() + line0);
        fprintf(stderr, "  second: %.60s\n", second.c_str() + line0);
    }

    /* The piece exercises the whole seam or this gate is weaker than it
       looks: notes from the eno lines, quantized notes from the
       wildcard, and chanarg events from the drift chain. */
    if (first.find("N ") == std::string::npos)
        fail("no note events in the stream");

    if (first.find("C ") == std::string::npos)
        fail("no chanarg events in the stream -- the drift chain is not "
             "flowing");

    if (first.find("cutoff") == std::string::npos)
        fail("the chanarg sink's name never reached delivery");
}

/* ----------------------------------------------------------------------- */

int
main (int argc, char *argv[])
{
    Glib::init();

    std::string pluginDir;
    std::string genFile;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            pluginDir = argv[++i];
        else
            genFile = argv[i];
    }

    if (pluginDir.empty() || genFile.empty())
    {
        fprintf(stderr, "usage: gencheck -p <plugindir> <file.gen>\n");
        return 2;
    }

    checkNotes();

    std::map<std::string, thcPlugin *> plugins;

    loadComposers(pluginDir, plugins);

    if (plugins.find("eno_line") == plugins.end() ||
        plugins.find("quantize") == plugins.end() ||
        plugins.find("walk") == plugins.end())
    {
        fprintf(stderr, "gencheck: composer modules missing from %s -- "
                "build the plugins first\n", pluginDir.c_str());
        return 2;
    }

    thSynth synth;

    checkValidation(plugins, &synth);
    checkReplay(plugins, &synth, genFile);

    /* Freed for the leak checker's sake, not the OS's: a gate that
       runs under sanitizers should not salt the report. The schedulers
       are already gone -- each check scoped its own. */
    for (std::map<std::string, thcPlugin *>::iterator i = plugins.begin();
         i != plugins.end(); ++i)
        delete i->second;

    if (failures == 0)
        printf("gencheck: OK\n");
    else
        printf("gencheck: %d failure%s\n", failures,
               failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
