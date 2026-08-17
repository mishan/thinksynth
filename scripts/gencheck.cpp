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

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <glibmm.h>

#include "think.h"

#include "libthink/thDynLib.h"
#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"
#include "thcGenEdit.h"

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

        /* Pin the module's mapping -- and, more to the point, its
           dependency closure -- for the life of the process: a second
           dlopen of the same path bumps the loader's reference count,
           and this handle is never closed on purpose.

           Why: on runners where cairo links gobject (cairo >= 1.18 as
           Ubuntu 24.04 ships it), a draw module's dlopen is what first
           loads the glib stack, and the teardown's dlclose -- which
           this harness performs deliberately, so the unload path runs
           under ASan at all -- would drop the last reference. The
           loader then unmaps those libraries and their once-per-process
           init heap, which glib documents as never-freed, turns into
           six LeakSanitizer reports. And they cannot be suppressed by
           library name, because an unmapped library symbolizes as
           "<unknown module>": the name a suppression would match is
           exactly what the unload destroyed.

           So: keep the mapping. dlclose still runs in ~thcPlugin and
           still exercises its path; the reference held here just means
           the count never reaches zero, the libraries stay mapped, and
           their init-once allocations remain reachable at exit --
           which is what they are in every process that links them the
           ordinary way. */
        thDynLib::open(f.path().string());
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
    /* thUtil::tempFile, not a fixed name under the shared temp dir:
       two gencheck processes (parallel ctest, two build trees) writing
       the same scratch path is a flaky failure nobody can reproduce. */
    std::string path = thUtil::tempFile(
        std::string("gencheck-") + label + "-");

    if (path.empty())
    {
        fail(std::string(label) + ": could not make a scratch file");
        return;
    }

    {
        std::ofstream out(path.c_str(), std::ios::trunc);

        out << body;

        if (!out.good())
        {
            fail(std::string(label) + ": could not write " + path);
            std::filesystem::remove(path);
            return;
        }
    }

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (loader.load(path, &sched))
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

    /* reset() must announce itself: the piano roll drops its history on
       this signal, and a reset nobody hears about leaves the previous
       piece's notes on screen as a future that already happened. */
    bool announced = false;
    sigc::connection resetConn = sched.sigReset.connect(
        [&announced] { announced = true; });

    sched.reset();

    if (!announced)
        fail("reset() did not emit sigReset");

    resetConn.disconnect();

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

/* ---- 4. the planners --------------------------------------------------- */

/* lsystem and evolve emit whole phrases into the future -- the first
 * composers that plan. Two claims worth a gate: the pending heap
 * actually holds a scheduled future right after a tick (the piano
 * roll's ghosted half is drawn from it, and a regression here would be
 * invisible until someone looked), and the planning replays exactly --
 * evolve especially, since a GA that drifted off its seed would corrupt
 * the determinism story in the least debuggable way possible. */

static void
checkPlanners (const std::map<std::string, thcPlugin *> &plugins,
               thSynth *synth)
{
    {
        /* Named, so the failure says which module to go and build
           rather than waving at a category. */
        const char *need[] = { "lsystem", "evolve", "markov", "ca",
                               NULL };
        bool missing = false;

        for (int i = 0; need[i] != NULL; i++)
            if (plugins.find(need[i]) == plugins.end())
            {
                fail(std::string("module '") + need[i] +
                     "' is missing; build the plugins first");
                missing = true;
            }

        if (missing)
            return;
    }

    /* Unique for the same reason every other scratch here is. */
    std::string tmp = thUtil::tempFile("gencheck-planners-");

    if (tmp.empty())
    {
        fail("could not make a planners scratch file");
        return;
    }

    {
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        out <<
            "seed 7;\n"
            "chain canopy {\n"
            "    stage src gen::lsystem {\n"
            "        axiom = \"X\"; rules = \"X=F[+X]F[-X]\";\n"
            "        depth = 3; step = 0.15 s; hold = 0.2 s;\n"
            "    };\n"
            "    sink { channel = 0; };\n"
            "};\n"
            "chain roots {\n"
            "    stage src gen::evolve {\n"
            "        length = 8; population = 8;\n"
            "        step = 0.2 s; hold = 0.2 s;\n"
            "    };\n"
            "    sink { channel = 1; };\n"
            "};\n"
            "chain dream {\n"
            "    stage teacher gen::lsystem {\n"
            "        axiom = \"X\"; rules = \"X=F[+X]F[-X]\";\n"
            "        depth = 3; step = 0.15 s; hold = 0.2 s;\n"
            "    };\n"
            "    stage student gen::markov {\n"
            "        pass = 0; period = 0.2 s; hold = 0.2 s;\n"
            "    };\n"
            "    sink { channel = 2; };\n"
            "};\n"
            "chain grid {\n"
            "    stage src gen::ca {\n"
            "        rule = 110; width = 8;\n"
            "        period = 0.2 s; hold = 0.1 s;\n"
            "    };\n"
            "    sink { channel = 3; };\n"
            "};\n";
    }

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(tmp, &sched))
    {
        for (size_t i = 0; i < loader.errors().size(); i++)
            fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

        fail("the planners piece did not load");
        std::filesystem::remove(tmp);
        return;
    }

    /* One tick in: both plugins have committed a phrase, and the future
       is sitting in the pending heap where the roll can see it. */
    sched.start();
    sched.stepTransport(0.05);

    if (sched.peekPending().size() < 5)
        fail("planners scheduled almost nothing ahead; the ghosted "
             "future would be empty");

    sched.stop();
    sched.reset();

    std::string first = render(sched, 30.0, 0.02);

    sched.reset();

    std::string second = render(sched, 30.0, 0.02);

    if (first.empty())
        fail("thirty seconds of planners delivered nothing");

    if (first != second)
        fail("planner replay diverged -- evolution is drawing "
             "randomness from somewhere outside its seed");

    /* The learner and the automaton both spoke: the markov's channel
       proves receive() trained it from its upstream teacher (pass = 0,
       so anything on channel 2 is the student's own), and the ca's
       proves the ring is advancing. */
    {
        int perChan[4] = { 0, 0, 0, 0 };
        std::istringstream in(first);
        std::string line;
        double at;
        int chan, note;

        while (std::getline(in, line))
            if (sscanf(line.c_str(), "N %lg %d %d", &at, &chan,
                       &note) == 3 && chan >= 0 && chan < 4)
                perChan[chan]++;

        if (perChan[2] < 5)
            fail("the markov student never dreamed -- receive() is not "
                 "training from upstream");

        if (perChan[3] < 5)
            fail("the cellular automaton never fired");
    }

    /* The GA must not freeze. With a static fitness landscape and two
       protected elites it used to: a local optimum inside a minute,
       then the same bars forever. The boredom tax is what keeps the
       optimum moving, and this holds it down -- deterministic, since
       the piece is seeded, so it either passes always or fails always.
       Group the evolve chain's notes (channel 1) into cycle-length
       windows and count distinct phrases across ~18 cycles. */
    {
        std::istringstream in(first);
        std::string line;
        std::map<int, std::string> cycles;

        while (std::getline(in, line))
        {
            double at;
            int chan, note;

            if (sscanf(line.c_str(), "N %lg %d %d", &at, &chan,
                       &note) == 3 && chan == 1)
            {
                char b[16];

                snprintf(b, sizeof(b), "%d,", note);
                cycles[(int)(at / 1.6)] += b;   /* 8 steps * 0.2s      */
            }
        }

        std::map<std::string, int> distinct;

        for (std::map<int, std::string>::iterator i = cycles.begin();
             i != cycles.end(); ++i)
            distinct[i->second]++;

        if (distinct.size() < 4)
            fail("evolve froze: fewer than four distinct phrases in "
                 "thirty seconds -- the boredom tax is not biting");
    }

    std::filesystem::remove(tmp);
}

/* ---- 5. live input ----------------------------------------------------- */

/* The injectMidiEvent path end to end: a chain with `input midi' hears
 * presses and releases, an arp stage turns held keys into steps, a
 * bare input chain passes the performance straight through -- and all
 * of it replays exactly when the same events arrive at the same
 * transport times, which is what makes recorded performances a future
 * feature instead of a rewrite. Also pins the decided open question:
 * keys pressed on a STOPPED transport sound immediately instead of
 * waiting for Play behind a frozen clock. */

static void
checkLiveInput (const std::map<std::string, thcPlugin *> &plugins,
                thSynth *synth)
{
    if (plugins.find("arp") == plugins.end())
    {
        fail("arp module missing; build the plugins first");
        return;
    }

    /* Unique for the same reason every other scratch here is. */
    std::string tmp = thUtil::tempFile("gencheck-live-");

    if (tmp.empty())
    {
        fail("could not make a live-input scratch file");
        return;
    }

    {
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        out <<
            "seed 11;\n"
            "chain hands {\n"
            "    input midi;\n"
            "    stage a gen::arp { period = 0.1 s; hold = 0.08 s; };\n"
            "    sink { channel = 0; };\n"
            "};\n"
            "chain thru {\n"
            "    input midi;\n"
            "    sink { channel = 1; };\n"
            "};\n";
    }

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(tmp, &sched))
    {
        for (size_t i = 0; i < loader.errors().size(); i++)
            fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

        fail("the live-input piece did not load");
        std::filesystem::remove(tmp);
        return;
    }

    /* One scripted performance, in virtual time. Routing is by the
       chains' sink channels: channel 0 reaches `hands' (the arp),
       channel 1 reaches `thru'. */
    auto press = [&sched](int chan, int note, int vel)
    {
        thcEvent ev = {};

        ev.type = THC_EV_NOTE;
        ev.at = sched.now();
        ev.channel = chan;
        ev.u.note.note = note;
        ev.u.note.velocity = vel;
        ev.u.note.duration = 0;
        sched.injectMidiEvent(ev);
    };
    auto release = [&sched](int chan, int note)
    {
        thcEvent ev = {};

        ev.type = THC_EV_NOTEOFF;
        ev.at = sched.now();
        ev.channel = chan;
        ev.u.note.note = note;
        sched.injectMidiEvent(ev);
    };

    auto perform = [&]() -> std::string
    {
        std::string tape;
        sigc::connection conn = sched.sigDelivered.connect(
            [&tape](const thcEvent &ev)
            {
                char b[96];

                if (ev.type == THC_EV_NOTE)
                    snprintf(b, sizeof(b), "N %.17g %d %d %d\n", ev.at,
                             ev.channel, ev.u.note.note,
                             ev.u.note.velocity);
                else if (ev.type == THC_EV_NOTEOFF)
                    snprintf(b, sizeof(b), "O %.17g %d %d\n", ev.at,
                             ev.channel, ev.u.note.note);
                else
                    b[0] = 0;

                tape += b;
            });

        sched.start();

        while (sched.now() < 3.0)
        {
            sched.stepTransport(0.02);

            /* The scripted hands, at exact virtual moments. */
            double t = sched.now();

            if (t >= 0.10 && t < 0.12) { press(0, 60, 90); }
            if (t >= 0.14 && t < 0.16)
            {
                press(0, 64, 70);
                press(0, 67, 50);
            }
            if (t >= 0.50 && t < 0.52) { press(1, 48, 111); }
            if (t >= 1.00 && t < 1.02) { release(1, 48); }
            if (t >= 2.00 && t < 2.02)
            {
                release(0, 60);
                release(0, 64);
                release(0, 67);
            }
        }

        sched.stop();
        conn.disconnect();

        return tape;
    };

    std::string first = perform();

    sched.reset();

    std::string second = perform();

    if (first != second)
        fail("a scripted performance replayed differently");

    /* The arp stepped through exactly the held pitches, inheriting the
       performance's velocities (vel = 0 means as played). */
    int arpNotes = 0;
    bool wrongPitch = false, wrongVel = false, thruOk = false;
    {
        std::istringstream in(first);
        std::string line;
        double at;
        int chan, note, vel;

        while (std::getline(in, line))
            if (sscanf(line.c_str(), "N %lg %d %d %d", &at, &chan, &note,
                       &vel) == 4)
            {
                if (chan == 0)
                {
                    arpNotes++;

                    if (note != 60 && note != 64 && note != 67)
                        wrongPitch = true;

                    if (vel != 90 && vel != 70 && vel != 50)
                        wrongVel = true;
                }

                if (chan == 1 && note == 48 && vel == 111)
                    thruOk = true;
            }
    }

    if (arpNotes < 10)
        fail("the arp barely stepped; held notes are not reaching it");

    if (wrongPitch)
        fail("the arp emitted a pitch nobody held");

    if (wrongVel)
        fail("vel = 0 did not inherit the performance's velocities");

    if (!thruOk)
        fail("the pass-through chain never delivered the raw press");

    /* Keys on a stopped transport sound immediately. */
    {
        sched.reset();

        int now = 0;
        sigc::connection conn = sched.sigDelivered.connect(
            [&now](const thcEvent &ev)
            {
                if (ev.type == THC_EV_NOTE)
                    now++;
            });

        press(1, 72, 100);       /* the bare chain: nothing swallows it */

        if (now < 1)
            fail("a key pressed on a stopped transport made no sound");

        release(1, 72);
        conn.disconnect();
        sched.reset();
    }

    std::filesystem::remove(tmp);
}

/* ---- 6. the editor's splices ------------------------------------------ */

/* thcGenEdit's whole promise is that an edit touches the bytes it names
 * and nothing else -- so every comment in the file survives any sequence
 * of edits, an edit that changes nothing writes nothing, and the file
 * after each edit still loads. Exercised on a scratch copy of the real
 * piece, because the real piece is where the comments are. */

static std::string
slurp (const std::string &path)
{
    std::ifstream in(path.c_str());

    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

static std::vector<std::string>
commentLines (const std::string &text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;

    while (std::getline(in, line))
    {
        size_t sp = line.find_first_not_of(" \t");

        if (sp != std::string::npos && line[sp] == '#')
            out.push_back(line.substr(sp));
    }

    return out;
}

static void
editOk (thcGenEdit::Result r, const std::string &why, const char *what)
{
    if (r != thcGenEdit::OK)
        fail(std::string(what) + ": " + why + " (" +
             thcGenEdit::resultText(r) + ")");
}

static void
checkEdits (const std::map<std::string, thcPlugin *> &plugins,
            thSynth *synth, const std::string &genFile)
{
    /* Unique for the same reason expectReject's scratch is: parallel
       gencheck runs must not edit each other's copy. */
    std::string path = thUtil::tempFile("gencheck-edit-");

    if (path.empty())
    {
        fail("could not make a scratch copy for editing");
        return;
    }

    std::filesystem::copy_file(genFile, path,
        std::filesystem::copy_options::overwrite_existing);
    std::vector<std::string> comments = commentLines(slurp(path));
    std::string why;

    if (comments.empty())
        fail("the shipped piece has no comments; this check needs them");

    /* Reading the structure back. */
    thcGenEdit::Doc doc;

    editOk(thcGenEdit::describe(path, doc, why), why, "describe");

    if (doc.chains.size() != 9)
        fail("describe found the wrong number of chains");

    if (!doc.hasSeed || doc.seed != 1978)
        fail("describe missed the pinned seed");

    if (doc.knobs.size() != 1 || doc.knobs[0].name != "density")
        fail("describe missed the density knob");

    if (doc.chains[0].stages.size() != 1 ||
        doc.chains[0].stages[0].params.size() < 2 ||
        doc.chains[0].stages[0].params[1].valueText != "23.9 s")
        fail("describe did not keep the authored '23.9 s'");

    /* An edit that changes nothing writes nothing. */
    std::string before = slurp(path);

    editOk(thcGenEdit::setParam(path, "loop_f3", 0, "period", "23.9 s",
                                why), why, "no-op setParam");

    if (slurp(path) != before)
        fail("writing the value already there changed the file");

    /* One of everything, on the scratch copy. */
    editOk(thcGenEdit::setParam(path, "loop_f3", 0, "period", "21.5 s",
                                why), why, "setParam replace");
    editOk(thcGenEdit::setParam(path, "loop_f3", 0, "vel_jitter", "12",
                                why), why, "setParam replace 2");
    editOk(thcGenEdit::setKnobValue(path, "density", 0.7, why), why,
           "setKnobValue");
    editOk(thcGenEdit::setKnobMeta(path, "density", 0, 1, "How often",
                                   why), why, "setKnobMeta");
    editOk(thcGenEdit::setInfo(path, "author", "gencheck", why), why,
           "setInfo");
    editOk(thcGenEdit::setSeed(path, 4242, why), why, "setSeed");
    editOk(thcGenEdit::setTempo(path, 90, why), why, "setTempo");
    editOk(thcGenEdit::addKnob(path, "shimmer", 0.5, 0, 1, "Shimmer",
                               why), why, "addKnob");
    editOk(thcGenEdit::setParam(path, "loop_ab3", 0, "prob", "@shimmer",
                                why), why, "bind to new knob");
    editOk(thcGenEdit::addScale(path, "pent", "C4 D4 E4 G4 A4", why),
           why, "addScale");
    editOk(thcGenEdit::setScale(path, "pent", "C3 D3 E3 G3 A3", why),
           why, "setScale");

    {
        std::vector<thcGenEdit::PresetValue> vals;
        thcGenEdit::PresetValue pv;

        pv.name = "res";  pv.value = 0.4;  vals.push_back(pv);
        pv.name = "fmin"; pv.value = 0.1;  vals.push_back(pv);

        editOk(thcGenEdit::addPreset(path, "dim", vals, why), why,
               "addPreset");
    }

    editOk(thcGenEdit::setPresetValue(path, "dim", "res", 0.55, why), why,
           "setPresetValue");
    editOk(thcGenEdit::addPresetValue(path, "dim", "fmax", 0.8, why), why,
           "addPresetValue");
    editOk(thcGenEdit::removePresetValue(path, "dim", "fmin", why), why,
           "removePresetValue");

    /* A preset written on one line, which addPreset never produces and a
       person writes all the time. Its `}' shares a line with its
       `preset', so the start of that line is *before* the block -- an
       insert aimed there puts the new component above the statement it
       belongs to, and the file stops loading. Written by hand here for
       exactly that reason: this editor's own output would never have
       found it. */
    {
        std::string text = slurp(path);

        text += "\npreset flat { one = 1; };\n";

        std::ofstream out(path.c_str(), std::ios::trunc);

        out << text;
    }

    editOk(thcGenEdit::addPresetValue(path, "flat", "two", 0.25, why), why,
           "addPresetValue on a one-liner");

    {
        const std::string after = slurp(path);
        const size_t at = after.find("preset flat");

        if (at == std::string::npos)
            fail("the one-line preset survived at all");
        else
        {
            const size_t eol = after.find('\n', at);
            const std::string line = after.substr(at, eol - at);

            if (line.find("two = 0.25;") == std::string::npos)
                fail("the new component did not land inside the one-line "
                     "block: " + line);
        }
    }

    std::vector<std::pair<std::string, std::string> > params;

    params.push_back(std::make_pair(std::string("notes"),
                                    std::string("pent")));
    params.push_back(std::make_pair(std::string("period"),
                                    std::string("2 beats")));
    params.push_back(std::make_pair(std::string("prob"),
                                    std::string("0.5")));

    editOk(thcGenEdit::addChain(path, "pulse", 2, "src", "gen",
                                "eno_line", params, why), why, "addChain");

    std::vector<std::pair<std::string, std::string> > qparams;

    qparams.push_back(std::make_pair(std::string("scale"),
                                     std::string("pent")));

    editOk(thcGenEdit::addStage(path, "pulse", "q", "xform", "quantize",
                                qparams, why), why, "addStage");
    editOk(thcGenEdit::addSink(path, "pulse", 5, "cutoff", why), why,
           "addSink");
    editOk(thcGenEdit::setSink(path, "pulse", 1, 6, "", why), why,
           "setSink to note sink");
    editOk(thcGenEdit::setSink(path, "pulse", 0, 2, "bright", why), why,
           "setSink add chanarg");
    editOk(thcGenEdit::setChainInput(path, "pulse", true, why), why,
           "setChainInput on");
    editOk(thcGenEdit::setChainInput(path, "pulse", false, why), why,
           "setChainInput off");
    editOk(thcGenEdit::renameChain(path, "pulse", "pulse2", why), why,
           "renameChain");
    editOk(thcGenEdit::moveStage(path, "wildcard", 0, 1, why), why,
           "moveStage");
    editOk(thcGenEdit::moveStage(path, "wildcard", 1, 0, why), why,
           "moveStage back");

    int rewritten = 0;

    editOk(thcGenEdit::removeKnob(path, "shimmer", 0.5, rewritten, why),
           why, "removeKnob");

    if (rewritten != 1)
        fail("removeKnob did not rewrite the one binding to it");

    editOk(thcGenEdit::removeScale(path, "pent", rewritten, why), why,
           "removeScale");

    if (rewritten != 2)
        fail("removeScale did not inline its two references");

    editOk(thcGenEdit::removeStage(path, "pulse2", 1, why), why,
           "removeStage");
    editOk(thcGenEdit::removeSink(path, "pulse2", 1, why), why,
           "removeSink");
    editOk(thcGenEdit::clearSeed(path, why), why, "clearSeed");
    editOk(thcGenEdit::clearTempo(path, why), why, "clearTempo");
    editOk(thcGenEdit::removeChain(path, "pulse2", why), why,
           "removeChain");

    /* Guard rails. */
    if (thcGenEdit::removeSink(path, "drift", 0, why) !=
        thcGenEdit::REFUSED)
        fail("removing a chain's last sink was not refused");

    if (thcGenEdit::setParam(path, "loop_f3", 0, "period", "20 furlongs",
                             why) != thcGenEdit::UNWRITABLE)
        fail("a unit the lexer does not know was accepted");

    if (thcGenEdit::addChain(path, "loop_f3", 0, "s", "gen", "eno_line",
            std::vector<std::pair<std::string, std::string> >(), why) !=
        thcGenEdit::REFUSED)
        fail("a duplicate chain name was accepted");

    /* The preset guard rails, all three of which exist because the state
       they would leave behind does not load. */
    if (thcGenEdit::addPreset(path, "empty",
            std::vector<thcGenEdit::PresetValue>(), why) !=
        thcGenEdit::REFUSED)
        fail("a preset that sets nothing was accepted");

    {
        std::vector<thcGenEdit::PresetValue> one;
        thcGenEdit::PresetValue pv;

        pv.name = "res"; pv.value = 0.1; one.push_back(pv);

        if (thcGenEdit::addPreset(path, "dim", one, why) !=
            thcGenEdit::REFUSED)
            fail("a duplicate preset name was accepted");
    }

    /* Unlike a scale, a preset reference cannot be inlined on the way
       out: the format has no literal form for a chanarg vector. So a
       preset something still names is refused, and the message says
       which stage -- "it is used" without "by what" sends the reader
       through the file. */
    {
        std::vector<thcGenEdit::PresetValue> vals;
        thcGenEdit::PresetValue pv;

        pv.name = "res"; pv.value = 0.9; vals.push_back(pv);

        editOk(thcGenEdit::addPreset(path, "held", vals, why), why,
               "addPreset held");

        /* `held' sets one thing, so removing it would leave a preset
           that sets nothing -- which does not load, and every state this
           editor writes has to. */
        if (thcGenEdit::removePresetValue(path, "held", "res", why) !=
            thcGenEdit::REFUSED)
            fail("removing a preset's last value was not refused");

        std::vector<std::pair<std::string, std::string> > mparams;

        mparams.push_back(std::make_pair(std::string("from"),
                                         std::string("held")));
        mparams.push_back(std::make_pair(std::string("to"),
                                         std::string("dim")));

        editOk(thcGenEdit::addChain(path, "sweep", 4, "m", "gen", "morph",
                                    mparams, why), why, "addChain morph");

        if (thcGenEdit::removePreset(path, "held", why) !=
            thcGenEdit::REFUSED)
            fail("removing a preset a stage still names was not refused");
        else if (why.find("sweep's stage m") == std::string::npos)
            fail("the refusal did not say which stage still names it: " +
                 why);

        editOk(thcGenEdit::removeChain(path, "sweep", why), why,
               "removeChain sweep");
        editOk(thcGenEdit::removePreset(path, "held", why), why,
               "removePreset");
    }

    /* After all of that: every comment intact, and the file loads. */
    std::string after = slurp(path);
    std::vector<std::string> commentsAfter = commentLines(after);

    for (size_t i = 0; i < comments.size(); i++)
    {
        bool found = false;

        for (size_t j = 0; j < commentsAfter.size(); j++)
            if (commentsAfter[j] == comments[i])
                found = true;

        if (!found)
            fail("a comment was lost in editing: " + comments[i]);
    }

    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(path, &sched))
        {
            for (size_t i = 0; i < loader.errors().size(); i++)
                fprintf(stderr, "gencheck: %s\n",
                        loader.errors()[i].c_str());

            fail("the edited file no longer loads");
        }

        if (loader.hasSeed())
            fail("clearSeed left a seed behind");
    }

    /* The edits round-trip through describe: the changed period reads
       back as authored. */
    editOk(thcGenEdit::describe(path, doc, why), why, "describe after");

    if (doc.chains[0].stages[0].params[1].valueText != "21.5 s")
        fail("the edited period did not read back as '21.5 s'");

    if (doc.author != "gencheck")
        fail("the edited author did not read back");

    /* The preset reads back as the vector it now is, in order: `res'
       edited, `fmin' removed, `fmax' appended at the end rather than in
       some canonical slot -- a preset's order is its author's. */
    {
        const thcGenEdit::Preset *dim = NULL;

        for (size_t i = 0; i < doc.presets.size(); i++)
            if (doc.presets[i].name == "dim")
                dim = &doc.presets[i];

        if (dim == NULL)
            fail("describe did not read the preset back");
        else if (dim->values.size() != 2 ||
                 dim->values[0].name != "res" ||
                 dim->values[0].value != 0.55 ||
                 dim->values[1].name != "fmax")
            fail("the edited preset did not read back as edited");
    }

    std::filesystem::remove(path);
}

/* ---- 6. presets, the wildcard sink, and morph -------------------------- */

/* Tier 2 of COMPOSITION_HANDOFF.md §9: the piece composes the instrument
 * as well as the notes. Three things have to hold together for that, and
 * none of them is provable from any other section here.
 *
 * A preset has to arrive at the plugin resolved -- the same bargain
 * NOTESET made, and the reason no composer has ever parsed a note name.
 * The `*' sink has to deliver each component under its own name, because
 * a vector routed through a sink that renames everything arrives as one
 * knob taking three values in turn. And a morph has to replay exactly:
 * it draws no randomness at all, so if this one ever diverges the cause
 * is the scheduler and not the plugin, which makes it a sharper tripwire
 * than a seeded composer would be.
 */
static void
checkPresets (const std::map<std::string, thcPlugin *> &plugins,
              thSynth *synth)
{
    if (plugins.find("morph") == plugins.end())
    {
        fail("the 'morph' module is missing; build the plugins first");
        return;
    }

    /* --- the rejections the format promises --- */

    expectReject(plugins, synth, "no-such-preset",
        "chain c { stage s gen::morph { from = ghost; to = ghost; };"
        " sink { channel = 0; chanarg = \"*\"; }; };",
        "no preset called 'ghost'");

    expectReject(plugins, synth, "duplicate-preset",
        "preset a { x = 1; };\npreset a { x = 2; };\n"
        "chain c { stage s gen::morph { }; sink { channel = 0; }; };",
        "already declared");

    expectReject(plugins, synth, "empty-preset",
        "preset a { };\n"
        "chain c { stage s gen::morph { }; sink { channel = 0; }; };",
        "sets nothing");

    /* A preset is a vector or it is nothing: interpolating towards a
       component whose value depends on where a slider happens to be is
       not a preset, it is an expression with a value right now. */
    expectReject(plugins, synth, "knob-in-preset",
        "@k = 1;\npreset a { x = @k; };\n"
        "chain c { stage s gen::morph { }; sink { channel = 0; }; };",
        "cannot be a knob");

    expectReject(plugins, synth, "preset-set-twice",
        "preset a { x = 1; x = 2; };\n"
        "chain c { stage s gen::morph { }; sink { channel = 0; }; };",
        "sets 'x' twice");

    /* A preset param takes a name, not the resolved text: a vector
       spelled inline cannot be morphed towards or saved under a name,
       which is the whole reason the noun exists. */
    expectReject(plugins, synth, "preset-as-string",
        "chain c { stage s gen::morph { from = \"x=1\"; };"
        " sink { channel = 0; }; };",
        "declare it with `preset'");

    expectReject(plugins, synth, "preset-as-number",
        "chain c { stage s gen::morph { from = 3; };"
        " sink { channel = 0; }; };",
        "wants a preset name");

    /* A sink pointed at a name no .dsp could declare would fail silently
       at delivery, which is a long way from the typo. */
    expectReject(plugins, synth, "bad-sink-name",
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 0; chanarg = \"cut off\"; }; };",
        "is not a chanarg name");

    /* --- what it does when it is right --- */

    std::string tmp = thUtil::tempFile("gencheck-presets-");

    if (tmp.empty())
    {
        fail("could not make a presets scratch file");
        return;
    }

    {
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        /* `arrive' names only what moves. A component one preset
           mentions and the other does not must hold still, which is what
           lets a target be a correction rather than a restatement. */
        out <<
            "seed 11;\n"
            "preset depart { res = 0.2; fmin = 0.10; fmax = 0.30; };\n"
            "preset arrive { fmax = 0.90; };\n"
            "chain sweep {\n"
            "    stage m gen::morph {\n"
            "        from = depart; to = arrive;\n"
            "        time = 4 s; steps = 9; curve = 1; mode = 0;\n"
            "    };\n"
            "    sink { channel = 5; chanarg = \"*\"; };\n"
            "};\n";
    }

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(tmp, &sched))
    {
        for (size_t i = 0; i < loader.errors().size(); i++)
            fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

        fail("the presets piece did not load");
        std::filesystem::remove(tmp);
        return;
    }

    const std::string first = render(sched, 6.0, 0.02);

    sched.reset();

    const std::string second = render(sched, 6.0, 0.02);

    if (first != second)
        fail("a morph replayed differently; it draws no randomness at "
             "all, so this is the scheduler");

    /* Each component under its own name. Without the `*' sink all three
       would arrive as whatever one name the sink carried. */
    if (first.find(" res ") == std::string::npos ||
        first.find(" fmin ") == std::string::npos ||
        first.find(" fmax ") == std::string::npos)
        fail("the wildcard sink did not deliver each component under its "
             "own name");

    /* Routing still belongs to the piece: the sink's channel overwrites
       whatever the plugin put in the event. */
    if (first.find("C ") != std::string::npos &&
        first.find(" 5 ") == std::string::npos)
        fail("the sink's channel did not reach delivery");

    /* The endpoints, exactly. A sweep that stopped at 0.98 of the way
       would leave the instrument almost at the preset the file named,
       forever -- and 0.9 is the only value `arrive' asks for. */
    if (first.find("fmax 0.30000001192092896") == std::string::npos)
        fail("the morph did not start at the preset it departs from");

    if (first.find("fmax 0.89999997615814209") == std::string::npos)
        fail("the morph did not arrive exactly at the preset it names");

    /* `res' is in `depart' and not in `arrive', so it must be emitted
       and must never move. */
    if (first.find("res 0.20000000298023224") == std::string::npos)
        fail("a component only one preset names was not emitted");

    /* ...and must never move, on any of the nine steps. Counted rather
       than spot-checked: "it was 0.2 at the start" and "it was 0.2
       throughout" are different claims and only the second one is the
       rule being stated. */
    {
        size_t seen = 0, held = 0;

        for (size_t at = first.find("res "); at != std::string::npos;
             at = first.find("res ", at + 1))
        {
            seen++;

            if (first.compare(at, strlen("res 0.20000000298023224"),
                              "res 0.20000000298023224") == 0)
                held++;
        }

        if (seen == 0 || seen != held)
            fail("a component only one preset names did not hold still: " +
                 std::to_string(held) + " of " + std::to_string(seen) +
                 " emissions were the value it was given");
    }

    std::filesystem::remove(tmp);

    /* --- the GA over the same vectors --- */

    if (plugins.find("breed") == plugins.end())
    {
        fail("the 'breed' module is missing; build the plugins first");
        return;
    }

    tmp = thUtil::tempFile("gencheck-breed-");

    if (tmp.empty())
    {
        fail("could not make a breed scratch file");
        return;
    }

    {
        /* spread = 0, so the corridor is exactly the interval the two
           presets span and the bound below is an equality rather than an
           estimate. `hum' is named by one preset only: it has nowhere to
           travel and must still be emitted, held at the value it was
           given. */
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        out <<
            "seed 91;\n"
            "preset shut { res = 0.9; fmin = 0.05; hum = 0.4; };\n"
            "preset wide { res = 0.3; fmin = 0.25; };\n"
            "chain search {\n"
            "    stage g gen::breed {\n"
            "        from = shut; toward = wide;\n"
            "        population = 12; mutation = 0.2; elites = 2;\n"
            "        spread = 0; aim = 1; drift = 0.5; reach = 0.25;\n"
            "        period = 0.5 s;\n"
            "    };\n"
            "    sink { channel = 7; chanarg = \"*\"; };\n"
            "};\n";
    }

    thcScheduler bsched(synth);
    thcGenLoader bloader(plugins);

    if (!bloader.load(tmp, &bsched))
    {
        for (size_t i = 0; i < bloader.errors().size(); i++)
            fprintf(stderr, "gencheck: %s\n", bloader.errors()[i].c_str());

        fail("the breed piece did not load");
        std::filesystem::remove(tmp);
        return;
    }

    const std::string bfirst = render(bsched, 20.0, 0.02);

    bsched.reset();

    const std::string bsecond = render(bsched, 20.0, 0.02);

    /* A GA drifting off its seed would be the least debuggable
       corruption of the replay story, which is why evolve has this gate
       and why this one does too. */
    if (bfirst != bsecond)
        fail("a breed replayed differently; same file, same seed");

    if (bfirst.find("C ") == std::string::npos)
        fail("the breed emitted nothing at all");

    /* The first thing played is `from', exactly.
     *
     * `from' is documented as where the population starts, and a piece
     * that names a starting timbre should hear it before it hears what
     * became of it. This was the corridor's midpoint, which whenever a
     * target is named is neither `from' nor near it -- and nothing said
     * so, because nothing looked at the first event. */
    {
        std::istringstream lines(bfirst);
        std::string line;
        bool checked = false, wrong = false;

        while (std::getline(lines, line) && !checked)
        {
            if (line.empty() || line[0] != 'C')
                continue;

            std::istringstream f(line);
            std::string kind, name;
            double at = 0, value = 0;
            int chan = 0;

            f >> kind >> at >> chan >> name >> value;

            if (name != "res")
                continue;

            checked = true;

            /* shut sets res = 0.9; wide sets it to 0.3. The midpoint
               this used to play is 0.6. */
            if (fabs(value - 0.9) > 1e-5)
                wrong = true;
        }

        if (!checked)
            fail("the breed never emitted the component to check");
        else if (wrong)
            fail("the breed did not start at the preset `from' names");
    }

    /* The corridor, which is the whole "declared surface is consent"
     * argument stated as arithmetic: a gene may travel between what the
     * two presets give it and no further, and no component neither
     * preset names can appear. This is the property that stops a search
     * reaching past what an instrument was offered for. */
    {
        bool strayed = false, unknown = false, sawHum = false, humMoved = false;

        std::istringstream lines(bfirst);
        std::string line;

        while (std::getline(lines, line))
        {
            if (line.empty() || line[0] != 'C')
                continue;

            std::istringstream f(line);
            std::string kind, name;
            double at = 0, value = 0;
            int chan = 0;

            f >> kind >> at >> chan >> name >> value;

            double lo = 0, hi = 0;

            if (name == "res")       { lo = 0.3;  hi = 0.9;  }
            else if (name == "fmin") { lo = 0.05; hi = 0.25; }
            else if (name == "hum")
            {
                sawHum = true;

                if (fabs(value - 0.4) > 1e-6)
                    humMoved = true;

                continue;
            }
            else { unknown = true; continue; }

            if (value < lo - 1e-6 || value > hi + 1e-6)
                strayed = true;
        }

        if (unknown)
            fail("the breed emitted a component neither preset names");

        if (strayed)
            fail("a gene travelled outside the corridor the presets "
                 "declared");

        if (!sawHum)
            fail("a component only one preset names was never emitted");

        if (humMoved)
            fail("a component with nowhere to travel moved anyway");
    }

    std::filesystem::remove(tmp);
}

/* ---- 7. every shipped piece still loads -------------------------------- */

/* The corpus instinct, applied to .gen.
 *
 * Everything above builds its own files or leans on the one piece passed
 * in, so the other shipped pieces were gated by nothing at all: a param
 * renamed in a plugin, a unit tightened in the loader, a knob whose
 * metadata stopped being accepted, and fern.gen or loom.gen would have
 * quietly stopped loading with no test anywhere to say so. dspcheck has
 * swept dsp/ for exactly this reason since long before any of this.
 *
 * Loading only, not rendering: what these files can prove cheaply is
 * that they still parse, still name plugins that exist, and still pass
 * every validation the loader applies. Replay determinism needs a pinned
 * seed and three minutes, and one piece carrying that is enough.
 */
static void
checkCorpus (const std::map<std::string, thcPlugin *> &plugins,
             thSynth *synth, const std::string &genFile)
{
    const std::filesystem::path dir =
        std::filesystem::path(genFile).parent_path();

    std::error_code ec;

    if (dir.empty() || !std::filesystem::is_directory(dir, ec))
        return;                 /* nothing to sweep; not a failure       */

    std::vector<std::filesystem::path> files;

    for (const auto &e : std::filesystem::directory_iterator(dir, ec))
    {
        if (ec)
            break;

        if (e.path().extension() == ".gen")
            files.push_back(e.path());
    }

    /* Sorted so a failure names the same file on every machine; the
       directory order is the filesystem's business, not the test's. */
    std::sort(files.begin(), files.end());

    if (files.empty())
    {
        fail("no .gen files beside " + genFile + " -- the sweep swept "
             "nothing");
        return;
    }

    for (size_t i = 0; i < files.size(); i++)
    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (loader.load(files[i].string(), &sched))
            continue;

        for (size_t k = 0; k < loader.errors().size(); k++)
            fprintf(stderr, "gencheck: %s\n", loader.errors()[k].c_str());

        fail(files[i].filename().string() + " no longer loads");
    }
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
    checkPlanners(plugins, &synth);
    checkLiveInput(plugins, &synth);
    checkEdits(plugins, &synth, genFile);
    checkPresets(plugins, &synth);
    checkCorpus(plugins, &synth, genFile);

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
