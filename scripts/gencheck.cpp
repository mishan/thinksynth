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
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <glibmm.h>

#include "think.h"

#include "libthink/thDynLib.h"
#include "libthink/thMidiChan.h"
#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"
#include "thcGenEdit.h"
#include "thcNodeHost.h"

static int failures = 0;

static void
fail (const std::string &what)
{
    fprintf(stderr, "gencheck: FAIL: %s\n", what.c_str());
    failures++;
}

/* The synth every check shares, and the one thing this harness has to do
 * with it besides hand it to a scheduler: empty its command queue.
 *
 * Every delivered note posts a command for the audio thread, and nothing
 * here is an audio thread -- so the ring filled after the first few
 * thousand events and stayed full, printing "command queue full" for the
 * rest of the run and dropping everything queued after it.
 *
 * That was noise while the only thing in the ring was notes nobody was
 * listening to. It stopped being noise when a piece began carrying its
 * own instruments: loading one queues a SET_CHANNEL, and a SET_CHANNEL
 * dropped on the floor is loadTree returning NULL -- an instrument that
 * fails to load because the *previous* check played too many notes,
 * which is a beautifully confusing way to fail.
 *
 * Once per render and once before a load that might carry an instrument,
 * not once per step. drainCommands empties the whole ring in one call,
 * so a single window leaves it clean for whatever comes next. A window
 * per step was tried first and is what a real audio thread does; it also
 * turned a 0.07-second gate into a 28-second one, which is a fine way to
 * teach people to stop running it. */
static thSynth *tapeSynth = NULL;

static void
drainSynth (void)
{
    if (tapeSynth != NULL)
        tapeSynth->process();
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

    /* A file that declares an instrument queues a SET_CHANNEL, and a
       full command ring would drop it -- so a rejection case would
       "fail" on the wrong error. See drainSynth. */
    drainSynth();

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
        " sink { channel = 1; }; };",
        "write a unit");

    /* A plugin that does not exist, by name. */
    expectReject(plugins, synth, "no-such-plugin",
        "chain c { stage s gen::no_such_thing { };"
        " sink { channel = 1; }; };",
        "no_such_thing");

    /* A param the plugin never registered, by name. */
    expectReject(plugins, synth, "no-such-param",
        "chain c { stage s gen::eno_line { frobnicate = 3; };"
        " sink { channel = 1; }; };",
        "frobnicate");

    /* gen:: asked of a transformer. */
    expectReject(plugins, synth, "wrong-role",
        "chain c { stage s gen::quantize { };"
        " sink { channel = 1; }; };",
        "cannot be a gen:: stage");

    /* A knob used before it is declared. */
    expectReject(plugins, synth, "undeclared-knob",
        "chain c { stage s gen::eno_line { prob = @nope; };"
        " sink { channel = 1; }; };",
        "@nope");

    /* A chain with no sink has nowhere to deliver. */
    expectReject(plugins, synth, "no-sink",
        "chain c { stage s gen::eno_line { }; };",
        "has no sink");

    /* Textual order is execution order; a stage after a sink is a
       contradiction, not a style choice. */
    expectReject(plugins, synth, "stage-after-sink",
        "chain c { stage s gen::eno_line { }; sink { channel = 1; };"
        " stage t xform::quantize { }; };",
        "stage after sink");

    /* All transformers and no input: nothing would ever flow. */
    expectReject(plugins, synth, "no-source",
        "chain c { stage s xform::quantize { };"
        " sink { channel = 1; }; };",
        "no generator");

    /* A scale nobody declared, by name. */
    expectReject(plugins, synth, "no-such-scale",
        "chain c { stage s gen::eno_line { notes = ghost; };"
        " sink { channel = 1; }; };",
        "ghost");

    /* A seed after a chain cannot mean what it says. */
    expectReject(plugins, synth, "late-seed",
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 1; }; };\n"
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
            /* Structure edits are on the tape for the same reason notes
               are: they are what the piece did. A replay gate that
               diffed only the notes would call a piece identical while
               it rebuilt its instrument at different times. */
            else if (ev.type == THC_EV_PATCH)
                snprintf(buf, sizeof(buf), "P %.17g %d %s\n",
                         ev.at, ev.channel,
                         ev.u.patch.name ? ev.u.patch.name : "");
            else if (ev.type == THC_EV_NODEARG)
                snprintf(buf, sizeof(buf), "E %.17g %d %s %s %.17g\n",
                         ev.at, ev.channel,
                         ev.u.nodearg.node ? ev.u.nodearg.node : "",
                         ev.u.nodearg.arg ? ev.u.nodearg.arg : "",
                         (double)ev.u.nodearg.value);
            else if (ev.type == THC_EV_CHANARG)
                snprintf(buf, sizeof(buf), "C %.17g %d %s %.17g\n",
                         ev.at, ev.channel,
                         ev.u.chanarg.name ? ev.u.chanarg.name : "",
                         (double)ev.u.chanarg.value);
            /* Named, not fallen through to. The chanarg case used to be
               the `else', which read u.chanarg.name out of whatever
               arrived -- a THC_EV_NOTEOFF landing there hands printf two
               ints as a char *. Unreachable in a headless run with no
               MIDI in it, and one live-input piece away from not
               being. */
            else
                snprintf(buf, sizeof(buf), "? %.17g %d %d\n",
                         ev.at, ev.channel, (int)ev.type);

            tape += buf;
        });

    sched.start();

    while (sched.now() < seconds)
        sched.stepTransport(step);

    sched.stop();
    conn.disconnect();

    /* After stop(), so the note-offs it flushes are in the ring this
       empties too. */
    drainSynth();

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

    if (first.find("fmin") == std::string::npos)
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
            "    sink { channel = 1; };\n"
            "};\n"
            "chain roots {\n"
            "    stage src gen::evolve {\n"
            "        length = 8; population = 8;\n"
            "        step = 0.2 s; hold = 0.2 s;\n"
            "    };\n"
            "    sink { channel = 2; };\n"
            "};\n"
            "chain dream {\n"
            "    stage teacher gen::lsystem {\n"
            "        axiom = \"X\"; rules = \"X=F[+X]F[-X]\";\n"
            "        depth = 3; step = 0.15 s; hold = 0.2 s;\n"
            "    };\n"
            "    stage student gen::markov {\n"
            "        pass = 0; period = 0.2 s; hold = 0.2 s;\n"
            "    };\n"
            "    sink { channel = 3; };\n"
            "};\n"
            "chain grid {\n"
            "    stage src gen::ca {\n"
            "        rule = 110; width = 8;\n"
            "        period = 0.2 s; hold = 0.1 s;\n"
            "    };\n"
            "    sink { channel = 4; };\n"
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
       so anything on the third channel is the student's own), and the ca's
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
       Group the evolve chain's notes into cycle-length windows and
       count distinct phrases across ~18 cycles. The comparisons below
       are against *delivered* channels, which the engine counts from
       zero -- the file's `channel = 2' is this stream's channel 1. */
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
            "    sink { channel = 1; };\n"
            "};\n"
            "chain thru {\n"
            "    input midi;\n"
            "    sink { channel = 2; };\n"
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
       chains' sink channels, and these are the *engine's* numbers: the
       file writes 1 and 2, the loader hands over 0 and 1, and injection
       and delivery both speak the latter. Channel 0 reaches `hands'
       (the arp), channel 1 reaches `thru'. */
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

    /* The instrument block, and the sinks that bind to it by name. */
    if (doc.instruments.size() != 1 || doc.instruments[0].name != "pad" ||
        doc.instruments[0].dsp != "amb01.dsp")
        fail("describe missed the pad instrument");
    else
    {
        bool sawAttack = false;

        for (size_t i = 0; i < doc.instruments[0].values.size(); i++)
            if (doc.instruments[0].values[i].name == "a")
            {
                sawAttack = true;

                /* Authored, not folded -- the same promise the stage
                   params get, and the one that makes an instrument
                   block readable at all. */
                if (doc.instruments[0].values[i].valueText != "900 ms")
                    fail("describe did not keep the authored '900 ms': " +
                         doc.instruments[0].values[i].valueText);
            }

        if (!sawAttack)
            fail("describe missed the pad's attack");
    }

    if (doc.chains[0].sinks.size() != 1 ||
        doc.chains[0].sinks[0].instrument != "pad" ||
        doc.chains[0].sinks[0].channel != 0)
        fail("describe did not read the sink's instrument binding");

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

    editOk(thcGenEdit::addChain(path, "pulse", 2, "", "src", "gen",
                                "eno_line", params, why), why, "addChain");

    /* And one whose sink binds to the piece's own instrument, which is
       what a new chain in an instrument-carrying piece should do -- a
       `channel = 1' here would take the pad's channel and move it.
       Its own params, naming no scale, so that the reference count
       removeScale checks below stays about the chain above. */
    {
        std::vector<std::pair<std::string, std::string> > bparams;

        bparams.push_back(std::make_pair(std::string("notes"),
                                         std::string("\"C4\"")));

        editOk(thcGenEdit::addChain(path, "bound", 1, "pad", "src", "gen",
                                    "eno_line", bparams, why), why,
               "addChain onto an instrument");
    }

    {
        thcGenEdit::Doc mid;

        thcGenEdit::describe(path, mid, why);

        const thcGenEdit::Chain *bound = NULL;

        for (size_t i = 0; i < mid.chains.size(); i++)
            if (mid.chains[i].name == "bound")
                bound = &mid.chains[i];

        if (bound == NULL || bound->sinks.size() != 1 ||
            bound->sinks[0].instrument != "pad")
            fail("addChain did not write the instrument it was given");
    }

    std::vector<std::pair<std::string, std::string> > qparams;

    qparams.push_back(std::make_pair(std::string("scale"),
                                     std::string("pent")));

    editOk(thcGenEdit::addStage(path, "pulse", "q", "xform", "quantize",
                                qparams, why), why, "addStage");
    editOk(thcGenEdit::addSink(path, "pulse", 5, "", "cutoff", why), why,
           "addSink");
    editOk(thcGenEdit::setSink(path, "pulse", 1, 6, "", "", why), why,
           "setSink to note sink");
    editOk(thcGenEdit::setSink(path, "pulse", 0, 2, "", "bright", why), why,
           "setSink add chanarg");

    /* Off the piece's own instrument onto a bare channel and back --
       the two spellings of a target, and the one edit that replaces a
       statement rather than a value inside one. */
    editOk(thcGenEdit::setSink(path, "loop_f3", 0, 9, "", "", why), why,
           "setSink instrument -> channel");

    {
        thcGenEdit::Doc mid;

        thcGenEdit::describe(path, mid, why);

        if (mid.chains.empty() || mid.chains[0].sinks.empty() ||
            !mid.chains[0].sinks[0].instrument.empty() ||
            mid.chains[0].sinks[0].channel != 9)
            fail("setSink left the instrument behind when it wrote a "
                 "channel");
    }

    editOk(thcGenEdit::setSink(path, "loop_f3", 0, 9, "pad", "", why), why,
           "setSink channel -> instrument");

    {
        thcGenEdit::Doc mid;

        thcGenEdit::describe(path, mid, why);

        if (mid.chains.empty() || mid.chains[0].sinks.empty() ||
            mid.chains[0].sinks[0].instrument != "pad" ||
            mid.chains[0].sinks[0].channel != 0)
            fail("setSink left the channel behind when it wrote an "
                 "instrument");
    }

    /* A sink cannot be pointed at an instrument nobody declared: every
       state this editor writes has to load. */
    if (thcGenEdit::setSink(path, "loop_f3", 0, 1, "ghost", "", why) ==
        thcGenEdit::OK)
        fail("a sink was pointed at an undeclared instrument");

    /* Nor at one declared *below* it. The loader resolves names in file
       order, so a chain above its instrument cannot name it -- and a
       file is perfectly free to be written that way round. Checking
       only that the name exists somewhere is how this wrote a file it
       could not then load. Its own scratch, because the shape the bug
       needs is not the shape the shipped piece has. */
    {
        std::string below = thUtil::tempFile("gencheck-below-");

        if (below.empty())
            fail("could not make a scratch file for the ordering check");
        else
        {
            {
                std::ofstream out(below.c_str(), std::ios::trunc);

                out << "chain c { stage s gen::eno_line { };"
                       " sink { channel = 9; }; };\n"
                       "instrument late { dsp \"amb01.dsp\"; };\n";
            }

            std::string was = slurp(below);

            if (thcGenEdit::setSink(below, "c", 0, 9, "late", "", why) ==
                thcGenEdit::OK)
                fail("a sink was pointed at an instrument declared below "
                     "it, which is a file that will not load");

            if (slurp(below) != was)
                fail("the refused ordering edit wrote to the file anyway");

            std::filesystem::remove(below);
        }
    }
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

    /* A knob read from inside an instrument is a binding too, and a
       removal that walked only the stages left a dangling `@name' and a
       file that no longer loaded -- the exact failure the rewriting is
       for, in the half of the language that grew after it. The unit
       comes along, because a bare number on a folded chanarg is refused.
       Its own scratch: the shipped piece has no such binding, which is
       the point. */
    {
        std::string bound = thUtil::tempFile("gencheck-knobinst-");

        if (bound.empty())
            fail("could not make a scratch file for the knob removal");
        else
        {
            {
                std::ofstream out(bound.c_str(), std::ios::trunc);

                out << "@tail = 1800;\n@tail.min = 200;\n@tail.max = 5000;\n"
                       "instrument pad {\n"
                       "    dsp \"amb01.dsp\";\n"
                       "    r = @tail ms;\n"
                       "    fmin = @tail;\n"
                       "};\n"
                       "chain c { stage s gen::eno_line { };"
                       " sink { instrument = pad; }; };\n";
            }

            /* Before removing it: a sink cannot be pointed at an arg
               that knob already drives, because the loader refuses that
               and every state this editor writes has to load. Both
               writers, since addSink and setSink share the check but
               not the call site. */
            if (thcGenEdit::addSink(bound, "c", 1, "pad", "fmin", why) ==
                thcGenEdit::OK)
                fail("addSink wrote a sink that fights a knob");

            if (thcGenEdit::setSink(bound, "c", 0, 1, "pad", "r", why) ==
                thcGenEdit::OK)
                fail("setSink wrote a sink that fights a knob");

            /* An arg the instrument leaves alone is still fair game. */
            editOk(thcGenEdit::addSink(bound, "c", 1, "pad", "res", why),
                   why, "addSink onto an arg no knob drives");

            int n = 0;

            editOk(thcGenEdit::removeKnob(bound, "tail", 1800, n, why), why,
                   "removeKnob bound into an instrument");

            if (n != 2)
            {
                std::ostringstream s;

                s << "removeKnob rewrote " << n
                  << " instrument bindings, not 2";
                fail(s.str());
            }

            const std::string after = slurp(bound);

            if (after.find("@tail") != std::string::npos)
                fail("removeKnob left a dangling knob reference behind");

            if (after.find("r = 1800 ms;") == std::string::npos)
                fail("removeKnob dropped the unit off a binding it "
                     "rewrote: " + after);

            thcGenEdit::Doc back;

            if (thcGenEdit::describe(bound, back, why) != thcGenEdit::OK)
                fail("the file after removeKnob no longer reads");

            std::filesystem::remove(bound);
        }
    }

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

    /* A legal channel, so what this refuses is the duplicate name and
       not the target -- the check used to pass channel 0 and was
       answered by the range check before it ever reached the name. */
    if (thcGenEdit::addChain(path, "loop_f3", 1, "", "s", "gen", "eno_line",
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

        editOk(thcGenEdit::addChain(path, "sweep", 4, "", "m", "gen", "morph",
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
        " sink { channel = 1; chanarg = \"*\"; }; };",
        "no preset called 'ghost'");

    expectReject(plugins, synth, "duplicate-preset",
        "preset a { x = 1; };\npreset a { x = 2; };\n"
        "chain c { stage s gen::morph { }; sink { channel = 1; }; };",
        "already declared");

    expectReject(plugins, synth, "empty-preset",
        "preset a { };\n"
        "chain c { stage s gen::morph { }; sink { channel = 1; }; };",
        "sets nothing");

    /* A preset is a vector or it is nothing: interpolating towards a
       component whose value depends on where a slider happens to be is
       not a preset, it is an expression with a value right now. */
    expectReject(plugins, synth, "knob-in-preset",
        "@k = 1;\npreset a { x = @k; };\n"
        "chain c { stage s gen::morph { }; sink { channel = 1; }; };",
        "cannot be a knob");

    expectReject(plugins, synth, "preset-set-twice",
        "preset a { x = 1; x = 2; };\n"
        "chain c { stage s gen::morph { }; sink { channel = 1; }; };",
        "sets 'x' twice");

    /* A preset param takes a name, not the resolved text: a vector
       spelled inline cannot be morphed towards or saved under a name,
       which is the whole reason the noun exists. */
    expectReject(plugins, synth, "preset-as-string",
        "chain c { stage s gen::morph { from = \"x=1\"; };"
        " sink { channel = 1; }; };",
        "declare it with `preset'");

    expectReject(plugins, synth, "preset-as-number",
        "chain c { stage s gen::morph { from = 3; };"
        " sink { channel = 1; }; };",
        "wants a preset name");

    /* A sink pointed at a name no .dsp could declare would fail silently
       at delivery, which is a long way from the typo. */
    /* The bottom of the old range. A file written when channels counted
       from zero is indistinguishable from one written for 1-16 *except*
       here, so this is the only place the change can be caught rather
       than silently transposing a piece -- and the message says what
       happened rather than only that a number was out of range. */
    expectReject(plugins, synth, "channel-zero",
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 0; }; };",
        "channel is 1-16 now");

    expectReject(plugins, synth, "channel-seventeen",
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 17; }; };",
        "whole number, 1-16");

    expectReject(plugins, synth, "bad-sink-name",
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 1; chanarg = \"cut off\"; }; };",
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
            "    sink { channel = 6; chanarg = \"*\"; };\n"
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
            "    sink { channel = 8; chanarg = \"*\"; };\n"
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

/* ---- 6b. a picture that is also a control ------------------------------ */

/* composer_input, and what it costs.
 *
 * `gen::life' is the first module whose draw is touchable, and the
 * property worth pinning is the one the canvas cannot show: a click goes
 * to the plugin's own state, changes what is played from the next
 * generation on, and does *not* change the file. Everything else here
 * follows from that -- a board nobody clicked replays from the piece
 * alone, and a board somebody clicked replays given the same clicks,
 * which is the boundary live MIDI already has.
 *
 * The clicks are scripted rather than real, for exactly the reason §7
 * gives about learned composers: a gate that needed a mouse would not be
 * a gate. What is driven is the ABI, not the widget -- composercheck is
 * where the widget gets pressed.
 */
static void
checkInput (const std::map<std::string, thcPlugin *> &plugins,
            thSynth *synth)
{
    std::map<std::string, thcPlugin *>::const_iterator it =
        plugins.find("life");

    if (it == plugins.end())
    {
        fail("the 'life' module is missing; build the plugins first");
        return;
    }

    thcPlugin *life = it->second;

    if (!life->hasInput())
        fail("gen::life does not export composer_input");

    if (!life->hasCapture())
        fail("gen::life does not export composer_capture");

    const std::string tmp = thUtil::tempFile("gencheck-life-");

    if (tmp.empty())
    {
        fail("could not make a life scratch file");
        return;
    }

    /* A blinker: three in a row, which oscillates with period two. Small
       enough that every assertion below can be reasoned about by hand. */
    {
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        out <<
            "chain c {\n"
            "    stage b gen::life {\n"
            "        board = \"...../.OOO./...../...../.....\";\n"
            "        width = 5; height = 5;\n"
            "        scatter = 0; trigger = 1; wrap = 1;\n"
            "        notes = \"C4 D4 E4 F4 G4\";\n"
            "        period = 0.5 s; hold = 0.2 s; vel = 90;\n"
            "    };\n"
            "    sink { channel = 1; };\n"
            "};\n";
    }

    /* A click, in the coordinate space composer_draw is given. The board
       is square and centred, so a cell's middle is arithmetic the test
       can do as well as the plugin can -- deliberately, because a test
       that asked the plugin where its cells were would be checking the
       plugin against itself. */
    struct Clicker {
        thcPlugin *plugin;
        void      *state;
        double     w, h;

        void at (int col, int row, thcInputType type) const
        {
            at(col, row, type, 1);
        }

        void at (int col, int row, thcInputType type, int button) const
        {
            const double cell = (w / 5 < h / 5) ? w / 5 : h / 5;
            const double ox = (w - cell * 5) / 2;
            const double oy = (h - cell * 5) / 2;

            thcInputEvent ev;

            ev.type = type;
            ev.x = ox + (col + 0.5) * cell;
            ev.y = oy + (row + 0.5) * cell;
            ev.w = w;
            ev.h = h;
            ev.button = button;

            plugin->input(state, &ev);
        }
    };

    /* --- untouched, the piece replays from itself --- */
    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(tmp, &sched))
        {
            for (size_t i = 0; i < loader.errors().size(); i++)
                fprintf(stderr, "gencheck: %s\n",
                        loader.errors()[i].c_str());

            fail("the life piece did not load");
            std::filesystem::remove(tmp);
            return;
        }

        const std::string first = render(sched, 6.0, 0.02);

        sched.reset();

        const std::string second = render(sched, 6.0, 0.02);

        if (first.empty())
            fail("gen::life delivered nothing");

        if (first != second)
            fail("gen::life replayed differently with nobody touching it");
    }

    /* --- the same clicks give the same music --- */
    std::string tapes[2];

    for (int pass = 0; pass < 2; pass++)
    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(tmp, &sched))
        {
            fail("the life piece did not load on pass two");
            std::filesystem::remove(tmp);
            return;
        }

        thcChain *c = sched.chain(0);

        if (c == NULL || c->stages.empty())
        {
            fail("the life piece has no stage to click");
            std::filesystem::remove(tmp);
            return;
        }

        thcStage *st = c->stages[0].get();
        Clicker click = { st->plugin, st->state, 100.0, 100.0 };

        /* Kill one end of the blinker and add a cell elsewhere. Pressed
           and released, because that is the pair the canvas sends and a
           plugin is entitled to keep state between them. */
        click.at(1, 1, THC_IN_PRESS);
        click.at(1, 1, THC_IN_RELEASE);
        click.at(4, 4, THC_IN_PRESS);
        click.at(4, 4, THC_IN_RELEASE);

        tapes[pass] = render(sched, 6.0, 0.02);
    }

    if (tapes[0].empty())
        fail("a clicked board delivered nothing");

    if (tapes[0] != tapes[1])
        fail("the same clicks gave a different piece -- input is the "
             "only thing that changed");

    /* --- and the clicks actually did something --- */
    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        loader.load(tmp, &sched);

        const std::string untouched = render(sched, 6.0, 0.02);

        if (untouched == tapes[0])
            fail("clicking the board changed nothing about what it plays");
    }

    /* --- capture hands back what was clicked, as text --- */
    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        loader.load(tmp, &sched);

        thcChain *c = sched.chain(0);
        thcStage *st = c->stages[0].get();

        const int idx = st->plugin->paramIndex("board");

        if (idx < 0)
            fail("gen::life has no 'board' param to capture");
        else
        {
            const std::string before = st->plugin->capture(st->state, idx);

            Clicker click = { st->plugin, st->state, 100.0, 100.0 };

            click.at(0, 0, THC_IN_PRESS);
            click.at(0, 0, THC_IN_RELEASE);

            const std::string after = st->plugin->capture(st->state, idx);

            if (before.empty() || after.empty())
                fail("capture returned nothing for the board param");
            else if (before == after)
                fail("capture did not see the click");
            else if (after.find('O') == std::string::npos ||
                     after.find('.') == std::string::npos ||
                     after.find('/') == std::string::npos)
                fail("captured board is not in the format the file "
                     "writes: " + after);

            /* The round trip, which is the whole reason a board is a
               string: capture, write it back the way the host does, and
               capture again. Anything but equality means the writer and
               the reader of this format disagree, and a piece saved
               through the panel would come back as a different board. */
            st->params.setString(idx, after);

            if (st->plugin->capture(st->state, idx) != after)
                fail("a captured board did not survive a round trip "
                     "through the param");

            /* ...but a board stated in the file or typed in the panel
               outranks a click, or a pattern nobody could correct would
               be one click away. */
            st->params.setString(idx, "OOOOO/...../...../...../.....");

            const std::string typed = st->plugin->capture(st->state, idx);

            if (typed.compare(0, 5, "OOOOO") != 0)
                fail("a board written after a click was ignored: " + typed);

            /* The secondary button erases rather than toggling, which
               is the whole reason thcInputEvent carries a button. Two
               right-clicks on one cell must leave it dead; two left
               clicks put it back where it started. */
            st->params.setString(idx, "OOOOO/OOOOO/OOOOO/OOOOO/OOOOO");

            Clicker erase = { st->plugin, st->state, 100.0, 100.0 };

            erase.at(2, 2, THC_IN_PRESS, 3);
            erase.at(2, 2, THC_IN_RELEASE, 3);

            const std::string once = st->plugin->capture(st->state, idx);

            erase.at(2, 2, THC_IN_PRESS, 3);
            erase.at(2, 2, THC_IN_RELEASE, 3);

            const std::string twice = st->plugin->capture(st->state, idx);

            if (once == "OOOOO/OOOOO/OOOOO/OOOOO/OOOOO")
                fail("a secondary click did not erase a cell");
            else if (once != twice)
                fail("a secondary click toggled rather than erasing");

            erase.at(2, 2, THC_IN_PRESS, 1);
            erase.at(2, 2, THC_IN_RELEASE, 1);

            if (st->plugin->capture(st->state, idx) != 
                "OOOOO/OOOOO/OOOOO/OOOOO/OOOOO")
                fail("the primary button did not put the cell back");

            /* A param the plugin cannot capture says so, rather than
               handing back something a host would then write. */
            const int other = st->plugin->paramIndex("period");

            if (other >= 0 && !st->plugin->capture(st->state, other).empty())
                fail("capture answered for a param it has nothing to say "
                     "about");
        }
    }

    std::filesystem::remove(tmp);
}

/* ---- 6c. two things the window asks the scheduler ---------------------- */

/* Does the tempo mean anything to this piece, and can a dead automaton
 * be brought back?
 *
 * Both are here rather than in composercheck because both are questions
 * about the *engine*, and the window only asks them. The tempo control
 * was offered on every piece and did nothing on nine of eleven, because
 * it scales beat-valued durations and most pieces are written in
 * seconds -- and nudging it wrote a `tempo' line into a file that never
 * had one. An empty CA ring is a fixed point for every rule that maps
 * 000 to 0, so the knob that emptied it cannot refill it.
 */
static void
checkTempoAndRevival (const std::map<std::string, thcPlugin *> &plugins,
                      thSynth *synth)
{
    const std::string tmp = thUtil::tempFile("gencheck-beats-");

    if (tmp.empty())
    {
        fail("could not make a beats scratch file");
        return;
    }

    /* Seconds only: the tempo has nothing to scale. */
    {
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        out << "chain c { stage s gen::eno_line { period = 2 s; };"
               " sink { channel = 1; }; };\n";
    }

    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(tmp, &sched))
            fail("the seconds-only piece did not load");
        else if (sched.usesBeats())
            fail("a piece written in seconds claimed the tempo moves it");
    }

    /* The same piece with one duration in beats. */
    {
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        out << "tempo 90;\n"
               "chain c { stage s gen::eno_line { period = 2 beats; };"
               " sink { channel = 1; }; };\n";
    }

    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(tmp, &sched))
            fail("the beats piece did not load");
        else if (!sched.usesBeats())
            fail("a piece written in beats claimed the tempo does not "
                 "move it");
    }

    /* --- a CA emptied by its rule, and clicked back --- */
    if (plugins.find("ca") == plugins.end())
    {
        fail("the 'ca' module is missing; build the plugins first");
        std::filesystem::remove(tmp);
        return;
    }

    {
        /* Rule 0 sends every neighbourhood to 0, so the ring is empty
           after one row and stays empty however the rule is changed --
           which is exactly the state a knob can reach and not leave. */
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        out <<
            "chain c {\n"
            "    stage s gen::ca {\n"
            "        rule = 0; width = 8; scatter = 0; trigger = 1;\n"
            "        notes = \"C4 D4 E4 G4\";\n"
            "        period = 0.25 s; hold = 0.2 s; vel = 90;\n"
            "    };\n"
            "    sink { channel = 1; };\n"
            "};\n";
    }

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(tmp, &sched))
    {
        fail("the dead-ca piece did not load");
        std::filesystem::remove(tmp);
        return;
    }

    thcChain *c = sched.chain(0);

    if (c == NULL || c->stages.empty())
    {
        fail("the dead-ca piece has no stage");
        std::filesystem::remove(tmp);
        return;
    }

    thcStage *st = c->stages[0].get();

    if (!st->plugin->hasInput())
        fail("gen::ca does not export composer_input");

    /* render() runs the transport *to* a time rather than for one, so
       each leg names a later mark than the last. Passing the same bound
       twice renders nothing at all, silently, which is a way to write a
       test that always passes. */

    render(sched, 4.0, 0.02);                    /* run it dead         */

    const std::string silent = render(sched, 8.0, 0.02);

    if (!silent.empty())
        fail("rule 0 did not empty the ring");

    /* Turning the knob does not help, which is the trap.
     *
       The index is checked before it is used. It cannot be -1 today --
       the piece above declares `rule' and the loader accepted it -- but
       paramIndex answers -1 for a name the plugin does not have, and a
       -1 handed to params.set is an out-of-range write that a renamed
       param would introduce silently. The rest of this file guards its
       lookups; this one had been the exception. */
    const int ruleIdx = st->plugin->paramIndex("rule");

    if (ruleIdx < 0)
    {
        fail("gen::ca has no `rule' param to turn");
        std::filesystem::remove(tmp);
        return;
    }

    st->params.set(ruleIdx, 110);

    if (!render(sched, 12.0, 0.02).empty())
        fail("an empty ring came back from a rule change alone -- the "
             "trap this is about does not exist");

    /* A click in the history does not, which is the other half of the
       bargain: the rows above the present one are what happened, and
       what happened is not editable. Checked before the click that
       works, so that "a click revived it" cannot be satisfied by a
       plugin that takes every click anywhere. */
    {
        thcInputEvent ev;

        ev.type = THC_IN_PRESS;
        ev.x = 50;
        ev.y = 50;                  /* halfway up: history              */
        ev.w = 100;
        ev.h = 100;
        ev.button = 1;

        st->plugin->input(st->state, &ev);
    }

    if (!render(sched, 14.0, 0.02).empty())
        fail("a click in the history edited the present row");

    /* A click does. The present row is the bottom band of the draw, and
       the coordinates are the ones composer_draw is given. */
    {
        thcInputEvent ev;

        ev.type = THC_IN_PRESS;
        ev.x = 50;                  /* somewhere along the ring          */
        ev.y = 99;                  /* the bottom row's band             */
        ev.w = 100;
        ev.h = 100;
        ev.button = 1;

        st->plugin->input(st->state, &ev);
    }

    if (render(sched, 18.0, 0.02).empty())
        fail("a clicked cell did not bring the automaton back");

    std::filesystem::remove(tmp);
}

/* ---- 6d. instruments: a piece that carries what it is played on -------- */

/* UNIFICATION.md phase 1. Four claims, each of which fails silently if
 * nothing watches it:
 *
 * 1. The block parses and the graph actually arrives on a channel. This
 *    is the one that needs a real thSynth with a real plugin path, which
 *    is why main() builds one -- an instrument that "loaded" because
 *    nothing tried is not a test.
 * 2. Channels are allocated in declaration order, lowest free first,
 *    around whatever `channel = N' claimed. That assignment is stable
 *    across loads and a great deal keys off it.
 * 3. Sinks bound by name get the number, so the events go somewhere.
 * 4. The rejections, by name and line. The unit rules especially: a
 *    chanarg written in the wrong unit is a value silently a thousand
 *    times wrong, which is the failure this format exists to refuse.
 */
/* Every channel empty, and the queue that empties them drained.
 *
 * The checks below ask what is on a channel, and a scheduler going out
 * of scope does not unload what its piece loaded -- a patch outlives the
 * file that asked for it, deliberately. So the sub-tests would be
 * answering with the previous one's leftovers. Nothing else in this
 * harness cares, because the harness installs no channelTaken hook and
 * allocation therefore ignores what is loaded. */
static void
clearChannels (thSynth *synth)
{
    for (int i = 0; i < TH_MIDI_CHANNELS; i++)
        synth->removeChan(i);

    drainSynth();
}

/* A scratch .dsp declaring `name' with no unit at all.
 *
 * amb01 is used throughout above because it is a real instrument with
 * real chanargs; what it cannot be is a *second* graph declaring one of
 * those names differently, and that shape is the one a knob binding has
 * to survive being swapped onto. So it is written here, the way argtype
 * writes the .dsp files whose cases the corpus cannot contain. Built
 * from amb01 rather than from nothing, so it stays a graph that loads
 * and sounds; only the one declaration is rewritten. Returns the path,
 * or "" if it could not be written. */
static std::string
writeUnitlessArg (const std::string &name)
{
    const std::string src =
        thUtil::findDataFile("amb01.dsp", "dsp", "THINK_DSP_PATH", DSP_PATH);

    if (src.empty())
        return "";

    std::ifstream in(src.c_str());
    std::string line, out;

    if (!in)
        return "";

    /* `@r = 100 ms;' -> `@r = 0.5;', and the same for its .max, which
       would otherwise put the unit back: a .dsp takes the unit from
       whichever value site states one first. */
    while (std::getline(in, line))
    {
        const std::string decl = "@" + name + " = ";
        const std::string maxd = "@" + name + ".max = ";
        size_t at = line.find(decl);

        if (at != std::string::npos)
            line = line.substr(0, at) + decl + "0.5;";
        else if ((at = line.find(maxd)) != std::string::npos)
            line = line.substr(0, at) + maxd + "1;";

        out += line + "\n";
    }

    std::string path = thUtil::tempFile("gencheck-plain-");

    if (path.empty())
        return "";

    {
        std::ofstream o(path.c_str(), std::ios::trunc);

        o << out;

        if (!o.good())
        {
            std::filesystem::remove(path);
            return "";
        }
    }

    return path;
}

static void
checkInstruments (const std::map<std::string, thcPlugin *> &plugins,
                  thSynth *synth)
{
    clearChannels(synth);

    /* Two instruments and a sink that claimed a channel out from under
       them: `pad' takes 1, `bell' skips the claimed 2 and takes 3. */
    const std::string body =
        "instrument pad {\n"
        "    dsp \"amb01.dsp\";\n"
        "    a = 900 ms;\n"
        "    fmin = 0.2;\n"
        "};\n"
        "instrument bell {\n"
        "    dsp \"amb01.dsp\";\n"
        "    r = 40 ms;\n"
        "};\n"
        "chain other { stage s gen::eno_line { };"
        " sink { channel = 2; }; };\n"
        "chain a { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };\n"
        "chain b { stage s gen::walk { };"
        " sink { instrument = bell; chanarg = \"res\"; }; };\n";

    std::string path = thUtil::tempFile("gencheck-instr-");

    if (path.empty())
    {
        fail("could not make a scratch file for the instrument check");
        return;
    }

    {
        std::ofstream out(path.c_str(), std::ios::trunc);

        out << body;
    }

    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        drainSynth();

        if (!loader.load(path, &sched))
        {
            for (size_t i = 0; i < loader.errors().size(); i++)
                fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

            fail("a piece with instruments did not load");
        }
        else
        {
            if (sched.instruments().size() != 2)
                fail("the instrument table is the wrong size");
            else
            {
                /* Engine numbering here: file 1 and 3 are 0 and 2. */
                if (sched.instruments()[0].channel != 0)
                    fail("pad did not land on the first free channel");

                if (sched.instruments()[1].channel != 2)
                    fail("bell did not skip the channel a sink claimed");

                thArg *a = synth->getChanArg(0, "a");
                thArg *f = synth->getChanArg(0, "fmin");

                if (a == NULL || f == NULL)
                    fail("the instrument's graph did not reach its channel");
                else
                {
                    /* 900 ms folded at the rate the synth was built
                       with -- not the compile-time one, which is the
                       bug thUnits exists to have fixed. */
                    const float want =
                        (float)(900.0 * synth->getSampleRate() / 1000.0);

                    if (fabs((*a)[0] - want) > 1.0)
                        fail("the attack was not folded through its unit");

                    if (fabs((*f)[0] - 0.2) > 1e-5)
                        fail("a unitless value did not arrive as written");
                }
            }

            /* The sinks got the numbers behind the names. */
            const thcChain *ca = sched.chain(1);
            const thcChain *cb = sched.chain(2);

            if (ca == NULL || ca->sinks.size() != 1 ||
                ca->sinks[0].channel != 0)
                fail("the sink bound to pad never got its channel");

            if (cb == NULL || cb->sinks.size() != 1 ||
                cb->sinks[0].channel != 2 || cb->sinks[0].chanarg != "res")
                fail("the chanarg sink bound to bell never got its channel");
        }
    }

    std::filesystem::remove(path);

    /* `%' folds too, and it is the half nothing else here touches --
       every other instrument in this file is amb01, which declares no
       percentages. A fraction of TH_MAX rather than a sample count,
       which is exactly why the diagnostic for a *missing* unit cannot
       talk about samples. */
    {
        std::string path = thUtil::tempFile("gencheck-instr-pct-");

        if (path.empty())
            fail("could not make a scratch file for the percent check");
        else
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << "instrument fizz { dsp \"aspect2.dsp\"; os = 50%; };\n"
                       "chain c { stage s gen::eno_line { };"
                       " sink { instrument = fizz; }; };\n";
            }

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            clearChannels(synth);

            if (!loader.load(path, &sched))
            {
                for (size_t i = 0; i < loader.errors().size(); i++)
                    fprintf(stderr, "gencheck: %s\n",
                            loader.errors()[i].c_str());

                fail("a percentage instrument value did not load");
            }
            else
            {
                thArg *os = synth->getChanArg(0, "os");

                if (os == NULL)
                    fail("the percentage instrument did not reach its "
                         "channel");
                else if (fabs((*os)[0] - TH_MAX / 2.0) > 1.0)
                    fail("50% did not fold to half of TH_MAX");
            }

            std::filesystem::remove(path);
        }
    }

    /* ---- knobs reaching into an instrument -------------------------- */

    /* UNIFICATION.md phase 2, and the whole of it: one knob, both sides
     * of the boundary. A stage param bound to a knob is *read* through
     * it; a chanarg cannot be, because what reads a chanarg is the audio
     * graph and the only value it will ever see is the one in its thArg.
     * So this binding is a push, and the thing to hold down is that the
     * push happens -- at load, and again on every move, through the
     * unit the binding was written with. */
    {
        std::string path = thUtil::tempFile("gencheck-instr-knob-");

        if (path.empty())
            fail("could not make a scratch file for the knob-binding check");
        else
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << "@warmth = 0.4;\n"
                       "@warmth.min = 0;\n@warmth.max = 1;\n"
                       "@tail = 1800;\n"
                       "@tail.min = 200;\n@tail.max = 5000;\n"
                       "instrument pad {\n"
                       "    dsp \"amb01.dsp\";\n"
                       "    fmin = @warmth;\n"
                       "    r = @tail ms;\n"
                       "};\n"
                       "chain c { stage s gen::eno_line { prob = @warmth; };"
                       " sink { instrument = pad; }; };\n";
            }

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            clearChannels(synth);

            if (!loader.load(path, &sched))
            {
                for (size_t i = 0; i < loader.errors().size(); i++)
                    fprintf(stderr, "gencheck: %s\n",
                            loader.errors()[i].c_str());

                fail("a knob bound to an instrument chanarg did not load");
            }
            else
            {
                const long rate = synth->getSampleRate();
                thArg *fmin = synth->getChanArg(0, "fmin");
                thArg *r    = synth->getChanArg(0, "r");
                thArg *warmth = sched.knob("warmth");
                thArg *tail = sched.knob("tail");

                if (fmin == NULL || r == NULL || warmth == NULL ||
                    tail == NULL)
                    fail("the knob-bound instrument did not arrive whole");
                else
                {
                    /* Where the knob is, before anybody touches it: a
                       piece has to sound like its file the moment it
                       loads, not one knob-move later. */
                    if (fabs((*fmin)[0] - 0.4) > 1e-5)
                        fail("a knob-bound chanarg did not take the knob's "
                             "value at load");

                    if (fabs((*r)[0] - 1800.0 * rate / 1000.0) > 1.0)
                        fail("a knob binding with a unit was not folded "
                             "through it at load");

                    /* And on every move. */
                    warmth->setValue(0.8f);

                    if (fabs((*fmin)[0] - 0.8) > 1e-5)
                        fail("moving the knob did not move the chanarg");

                    tail->setValue(600.0f);

                    if (fabs((*r)[0] - 600.0 * rate / 1000.0) > 1.0)
                        fail("moving a unit-carrying knob did not fold "
                             "through the unit");

                    /* The same knob, still driving the composer's side.
                       That is the sentence phase 2 is about. */
                    thcChain *c = sched.chain(0);

                    if (c == NULL || c->stages.empty())
                        fail("the chain did not survive");
                    else
                    {
                        thcStage *st = c->stages[0].get();
                        const int pi = st->plugin->paramIndex("prob");

                        if (pi < 0 || fabs(st->params.get(pi) - 0.8) > 1e-5)
                            fail("one knob did not reach both worlds");
                    }

                    /* And a channel replaced from under the binding.
                     *
                     * The push looks its target up by name every time,
                     * which stops it writing through a freed pointer --
                     * and does nothing at all about that name resolving
                     * to a *different* arg. The Patch Selector replaces
                     * a channel on request, so `r' folded from ms here
                     * beside `r' running 0 to 1 on the next graph is a
                     * knob nudge writing six figures into an arg whose
                     * top is 1. Nothing in the corpus has that shape, so
                     * the .dsp is written here, the way argtype writes
                     * the ones it needs.
                     *
                     * Not hypothetical: without the fold re-check in the
                     * push, `r' below lands at 176400. */
                    const std::string plain = writeUnitlessArg("r");

                    if (plain.empty())
                        fail("could not write a scratch .dsp for the "
                             "channel-replacement check");
                    else
                    {
                        drainSynth();

                        if (synth->loadTree(plain.c_str(), 0,
                                            TH_DEFAULT_CHAN_AMP) == NULL)
                            fail("the scratch .dsp did not load");
                        else
                        {
                            drainSynth();

                            thArg *now = synth->getChanArg(0, "r");

                            if (now == NULL || !now->units().empty())
                                fail("the scratch .dsp did not give 'r' a "
                                     "different shape");
                            else
                            {
                                const float was = (*now)[0];

                                tail->setValue(4000.0f);

                                if (fabs((*now)[0] - was) > 1e-5)
                                    fail("a knob went on driving an arg "
                                         "whose fold had changed under it");
                            }
                        }

                        std::filesystem::remove(plain);
                    }
                }
            }

            std::filesystem::remove(path);
        }
    }

    /* ---- the rejections ---- */

    expectReject(plugins, synth, "instr-no-dsp",
        "instrument pad { fmin = 0.2; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "names no dsp");

    expectReject(plugins, synth, "instr-missing-file",
        "instrument pad { dsp \"no_such_graph.dsp\"; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "did not load");

    expectReject(plugins, synth, "instr-twice",
        "instrument pad { dsp \"amb01.dsp\"; };\n"
        "instrument pad { dsp \"amb01.dsp\"; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "already declared");

    expectReject(plugins, synth, "instr-unknown-arg",
        "instrument pad { dsp \"amb01.dsp\"; frobnicate = 1; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "frobnicate");

    /* The two halves of the unit rule. A duration written bare is a
       sample count nobody meant; a unit on something that has none is
       a fold that would land a thousand times off. */
    expectReject(plugins, synth, "instr-bare-duration",
        "instrument pad { dsp \"amb01.dsp\"; a = 900; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "written in ms");

    /* The other fold, and the reason that message cannot say "samples":
       a bare number on a percentage arg is a raw fraction of TH_MAX, not
       a sample count. aspect2.dsp is the one graph in the corpus that
       declares a chanarg in `%'. */
    expectReject(plugins, synth, "instr-bare-percent",
        "instrument fizz { dsp \"aspect2.dsp\"; os = 50; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = fizz; }; };",
        "written in %");

    expectReject(plugins, synth, "instr-spurious-unit",
        "instrument pad { dsp \"amb01.dsp\"; fmin = 50 ms; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "means nothing to it");

    /* A refused instrument leaves no wiring behind either.
     *
     * Bindings are connected as their values are read, so this one wires
     * `fmin' up and is then refused for an arg amb01 does not declare.
     * The graph comes off the channel; the connections have to go with
     * it, or they push into whatever is loaded onto that channel next --
     * the bug the fold re-check in the push exists for, arriving by a
     * second door. Checked by putting a graph there afterwards and
     * moving the knob, since a surviving connection still names
     * channel 0. */
    {
        const std::string dsp = thUtil::findDataFile(
            "amb01.dsp", "dsp", "THINK_DSP_PATH", DSP_PATH);

        thcScheduler sched(synth);
        thcInstrument inst;
        thcInstrumentArg a;

        clearChannels(synth);

        thArg *knob = sched.addKnob("warmth", 0.4f);

        inst.name = "pad";
        inst.dsp = "amb01.dsp";
        inst.channel = 0;

        a.name = "fmin";      a.value = 0; a.knob = "warmth";
        inst.args.push_back(a);
        a.name = "nosucharg"; a.value = 1; a.knob.clear();
        inst.args.push_back(a);

        sched.addInstrument(inst);

        std::string why;

        /* Driven through applyInstrument rather than through a .gen,
           because the loader calls clearChains on a failed load and
           that disconnects everything anyway -- which would make this
           pass whether or not the contract holds. The contract is
           stated on applyInstrument, so it is asked there. */
        if (sched.applyInstrument(0, why))
            fail("an instrument naming an arg its graph lacks applied "
                 "anyway");

        drainSynth();

        if (dsp.empty() ||
            synth->loadTree(dsp.c_str(), 0, TH_DEFAULT_CHAN_AMP) == NULL)
            fail("could not put a graph back for the wiring check");
        else
        {
            drainSynth();

            thArg *fmin = synth->getChanArg(0, "fmin");

            knob->setValue(0.9f);

            if (fmin != NULL && fabs((*fmin)[0] - 0.9) < 1e-5)
                fail("a refused instrument's knob binding survived and "
                     "drove the next patch on its channel");
        }
    }

    expectReject(plugins, synth, "instr-undeclared-knob",
        "instrument pad { dsp \"amb01.dsp\"; fmin = @nope; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "@nope");

    /* A knob binding obeys the unit rule a literal obeys, both ways
       round. The number in a knob is as unitless as the number in a
       file, so an envelope on a bare binding would be a slider running
       in samples. */
    expectReject(plugins, synth, "instr-knob-bare-duration",
        "@attack = 500;\n"
        "instrument pad { dsp \"amb01.dsp\"; a = @attack; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "written in ms");

    expectReject(plugins, synth, "instr-knob-spurious-unit",
        "@warmth = 0.5;\n"
        "instrument pad { dsp \"amb01.dsp\"; fmin = @warmth ms; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "means nothing to it");

    /* A knob and a sink over one arg are both pushes, so the walk wins
       every time it fires and the slider looks dead a second after you
       let go of it. Invisible from either end, and no reading of the
       file where it was the intention. */
    expectReject(plugins, synth, "knob-and-sink-fight",
        "@warmth = 0.5;\n"
        "instrument pad { dsp \"amb01.dsp\"; fmin = @warmth; };\n"
        "chain c { stage s gen::walk { };"
        " sink { instrument = pad; chanarg = \"fmin\"; }; };",
        "would fight over it");

    expectReject(plugins, synth, "sink-unknown-instrument",
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = ghost; }; };",
        "ghost");

    expectReject(plugins, synth, "sink-both-targets",
        "instrument pad { dsp \"amb01.dsp\"; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; channel = 4; }; };",
        "one or the other");

    expectReject(plugins, synth, "sink-no-target",
        "chain c { stage s gen::eno_line { }; sink { }; };",
        "no instrument and no channel");

    /* A sink bound to an instrument cannot name a knob that instrument
       does not have. Unlike a `channel = N' sink, whose patch is
       somebody else's business, this one is checkable -- and a sink
       that is not checked delivers into getChanArg's NULL forever, in
       silence, a long way from the typo. */
    expectReject(plugins, synth, "sink-unknown-chanarg",
        "instrument pad { dsp \"amb01.dsp\"; };\n"
        "chain c { stage s gen::walk { };"
        " sink { instrument = pad; chanarg = \"nosuchknob\"; }; };",
        "nosuchknob");

    /* One bad instrument stops the rest, and takes back the one that
       already made it.
     *
     * The file is not going to load once the first fails, so every
     * instrument after it would be another graph put on another channel
     * for a piece nobody is going to hear -- and the one before it is a
     * graph on a channel for the same piece, which is the half of "a
     * file with any error loads nothing" that used to be false. */
    {
        std::string path = thUtil::tempFile("gencheck-instr-stop-");

        if (!path.empty())
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << "instrument first { dsp \"amb01.dsp\"; };\n"
                       "instrument bad { dsp \"amb01.dsp\";"
                       " nosucharg = 1; };\n"
                       "instrument after { dsp \"amb01.dsp\"; };\n"
                       "chain c { stage s gen::eno_line { };"
                       " sink { instrument = bad; }; };\n";
            }

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            clearChannels(synth);

            if (loader.load(path, &sched))
                fail("an instrument naming an arg its graph does not "
                     "declare loaded anyway");
            else if (loader.errors().size() != 1)
            {
                std::ostringstream s;

                s << "one bad instrument produced "
                  << loader.errors().size() << " errors, not one";
                fail(s.str());
            }

            /* `first' came up before `bad' failed, and must not still be
               there. Drained first, because what unapplyInstrument does
               is queue a command like everything else. */
            drainSynth();

            if (synth->getChanArg(0, "fmin") != NULL)
                fail("a piece that failed to load left an instrument on a "
                     "channel");

            if (synth->getChanArg(2, "fmin") != NULL)
                fail("the instruments after the failure were loaded too");

            /* And `bad' itself, which is the one the first version of
               this check quietly skipped. Its .dsp loads fine and it is
               refused for an arg the graph does not declare -- so its
               graph is on the channel at the moment it fails, and a
               rollback that counted only successes walked straight past
               it. applyInstrument takes it back itself now, which is
               what makes the count above exact. */
            if (synth->getChanArg(1, "fmin") != NULL)
                fail("the instrument that failed was left on its channel");

            std::filesystem::remove(path);
        }
    }

    /* A channel that would not go.
     *
     * Taking an instrument off means telling the audio thread, and the
     * command ring can be full -- it is wedged, or nothing is draining
     * it, which is a harness's normal state. The graph is then still
     * loaded and still sounding while the piece that asked for it is
     * being thrown away, so the scheduler keeps the instrument and tries
     * again on its own clock. Dropping the record instead would leave a
     * graph nothing in the program could name, and headless there is no
     * window keeping a second copy.
     *
     * Driven through apply/unapply rather than a .gen, because a ring
     * full enough to block the unload blocks the *load* too and there
     * would be nothing to strand. */
    {
        thcScheduler sched(synth);
        thcInstrument inst;

        clearChannels(synth);

        inst.name = "pad";
        inst.dsp = "amb01.dsp";
        inst.channel = 0;

        sched.addInstrument(inst);

        std::string why;

        if (!sched.applyInstrument(0, why))
            fail("the stranding check could not get its instrument up: " +
                 why);
        else
        {
            drainSynth();

            /* Fill the ring, without draining. TH_COMMAND_QUEUE_SIZE
               notes would do it exactly; twice that is slack against the
               size ever changing. */
            for (int i = 0; i < TH_COMMAND_QUEUE_SIZE * 2; i++)
                synth->addNote(0, 60, 100);

            if (sched.unapplyInstrument(0))
                fail("an unload succeeded with the command ring full");

            if (sched.strandedCount() != 1)
                fail("a channel that would not go was not remembered");

            /* Asking twice must not remember it twice. */
            sched.unapplyInstrument(0);

            if (sched.strandedCount() != 1)
                fail("the same stranded channel was recorded twice");

            /* And the retry, on the clock the harness has. */
            drainSynth();
            sched.stepTransport(0.0);

            if (sched.strandedCount() != 0)
                fail("a stranded channel was never retried");

            drainSynth();

            if (synth->getChanArg(0, "fmin") != NULL)
                fail("the retried channel is still loaded");
        }
    }
}

/* ---- 6e. embedded nodes: dsp plugins as chain stages ------------------- */

/* UNIFICATION.md phase 3. Four claims:
 *
 * 1. A node's output reaches a composer param, and moves it. The whole
 *    deliverable is "an LFO breathing a chain's density", and a binding
 *    that resolved but never changed anything would look identical from
 *    outside.
 * 2. It replays. Nodes step on transport time, so the same file and the
 *    same seed deliver the same stream twice with a reset between --
 *    which is the promise every composer already keeps and the reason a
 *    node had to be as repeatable as one before it was let in.
 * 3. The refusals: families that mean nothing one sample at a time, the
 *    one plugin that cannot replay, and arrows pointing at nothing.
 * 4. Pause freezes them, because transport time is what they read.
 */
static void
checkNodes (const std::map<std::string, thcPlugin *> &plugins,
            thSynth *synth, const std::string &genFile)
{
    const std::string breath =
        (std::filesystem::path(genFile).parent_path() / "breath.gen").string();

    if (!std::filesystem::exists(breath))
    {
        fail("breath.gen is not beside " + genFile + "; the node checks "
             "have nothing to run");
        return;
    }

    clearChannels(synth);

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(breath, &sched))
    {
        for (size_t i = 0; i < loader.errors().size(); i++)
            fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

        fail("breath.gen does not load");
        return;
    }

    /* The binding is live: the param a node drives has to *move*. Read
       straight off the store, because that is where a composer reads it
       and the arithmetic in between is the piece's business. */
    thcChain *c = sched.chain(0);

    if (c == NULL || c->stages.empty() || !c->nodes)
        fail("breath.gen's first chain has no nodes in it");
    else
    {
        thcStage *src = c->stages.back().get();
        const int pi = src->plugin->paramIndex("prob");

        if (pi < 0)
            fail("the node-driven stage has no prob param");
        else
        {
            double lo = 2, hi = -1;

            sched.start();

            for (int i = 0; i < 1200; i++)      /* twenty-four seconds  */
            {
                sched.stepTransport(0.02);

                const double v = src->params.get(pi);

                lo = v < lo ? v : lo;
                hi = v > hi ? v : hi;
            }

            sched.stop();

            /* The piece asks for 0.5 +- 0.45 over a twenty-second
               cycle, so a full swing is most of 0.05..0.95. Loose
               bounds: what is being asked is "did the LFO drive it",
               not "is osc::simple accurate", which hostcheck owns. */
            if (hi - lo < 0.5)
            {
                std::ostringstream s;

                s << "the LFO did not breathe the density: prob stayed "
                  << "between " << lo << " and " << hi;
                fail(s.str());
            }

            /* And it is the *node* doing it, not a stored value that
               happens to vary: nothing else in that chain touches
               prob. */
            if (src->params.nodeBinding(pi) == NULL)
                fail("prob is not bound to a node at all");
        }
    }

    drainSynth();

    /* Replay. Same shape as the gate airports.gen passes, pointed at
       the piece whose values come out of a second interpreter. */
    {
        thcScheduler a(synth);
        thcGenLoader la(plugins);

        clearChannels(synth);

        if (!la.load(breath, &a))
            fail("breath.gen did not load for the replay check");
        else
        {
            const std::string first = render(a, 90.0, 0.02);

            a.reset();

            const std::string second = render(a, 90.0, 0.02);

            if (first.empty())
                fail("breath.gen delivered nothing at all");
            else if (first != second)
            {
                fail("a piece with dsp nodes in it did not replay");

                size_t n = 0;

                while (n < first.size() && n < second.size() &&
                       first[n] == second[n])
                    n++;

                size_t line0 = first.rfind('\n', n);

                line0 = line0 == std::string::npos ? 0 : line0 + 1;

                fprintf(stderr, "  first : %.60s\n", first.c_str() + line0);
                fprintf(stderr, "  second: %.60s\n", second.c_str() + line0);
            }
        }
    }

    drainSynth();

    /* Pause freezes them. The nodes read transport time, and a stopped
       transport does not advance -- so the value a param sees must be
       the same before and after a stretch of wall clock spent stopped.
       Stepping a stopped scheduler is exactly what the app's timer does
       while paused. */
    {
        thcScheduler p(synth);
        thcGenLoader lp(plugins);

        clearChannels(synth);

        if (lp.load(breath, &p))
        {
            thcChain *pc = p.chain(0);
            thcStage *src = pc != NULL && !pc->stages.empty()
                ? pc->stages.back().get() : NULL;
            const int pi = src != NULL
                ? src->plugin->paramIndex("prob") : -1;

            if (src != NULL && pi >= 0)
            {
                p.start();

                for (int i = 0; i < 150; i++)
                    p.stepTransport(0.02);

                p.stop();

                const double held = src->params.get(pi);

                for (int i = 0; i < 500; i++)
                    p.stepTransport(0.02);

                if (src->params.get(pi) != held)
                    fail("a paused transport did not freeze the nodes");
            }
        }
    }

    drainSynth();

    /* A pure function of transport time, and nothing else.
     *
     * The replay above is necessary and not sufficient: the harness
     * drives both renders with the same call pattern, so a host firing
     * one window per *call* rather than per fiftieth of a second would
     * replay perfectly and still be wrong -- wrong in the way that
     * matters, since the application's dt jitters with the frame and a
     * piece would breathe at whatever rate the machine felt like. That
     * mistake was made while writing this, and the replay gate did not
     * notice.
     *
     * So it is asked directly, which is the only place the question is
     * separable from what the composers around it are doing: one graph,
     * one span of transport, reached in a hundred and fifty steps and in
     * a single jump. Same answer, or the clock is not the clock. Three
     * seconds because that is inside the catch-up guard -- a longer jump
     * is deliberately allowed to skip, the way a pause is.
     */
    {
        /* The host's own control-rate synth, made here the way the
           scheduler makes one: a rate and a plugin manager. */
        thSynth control(synth->getPluginManager()->pluginPath(), 1, 50);
        thcNodeHost a(&control, 50);
        thcNodeHost b(&control, 50);
        std::string why;
        bool built = true;

        for (int which = 0; which < 2; which++)
        {
            thcNodeHost &h = which == 0 ? a : b;

            built = built && h.addNode("lfo", "osc/simple", why);
            h.setValue("lfo", "freq", 0.37, why);
            h.setValue("lfo", "waveform", 0, why);
            h.setValue("lfo", "amp", 1, why);
            built = built && h.build(why);
        }

        if (!built)
            fail("the clock check could not build its graph: " + why);
        else
        {
            thArg *ao = a.output("lfo", "out", why);
            thArg *bo = b.output("lfo", "out", why);

            for (int i = 0; i < 150; i++)
                a.stepTo((i + 1) * 0.02);

            b.stepTo(3.0);

            if (ao == NULL || bo == NULL)
                fail("the clock check lost its outputs: " + why);
            else if ((*ao)[0] != (*bo)[0])
            {
                std::ostringstream s;

                s << "a node's value depends on how the transport was "
                  << "reached rather than on where it got to: stepped "
                  << (*ao)[0] << ", jumped " << (*bo)[0];
                fail(s.str());
            }
            else if ((*ao)[0] == 0)
                fail("the clock check watched a signal that never moved");
        }
    }

    /* A literal typed over a node-driven param actually takes.
     *
     * The panel's undo path: somebody selects a stage whose `prob' reads
     * an LFO, types 0.9, and the file, the canvas and the panel all say
     * 0.9 from that moment. Releasing only the knob left the node still
     * shadowing the stored value, so the piece went on breathing while
     * every surface that could show a number showed the new one -- and
     * saving and reopening sounded different from what had just been
     * heard, which is the part that makes it worth a gate rather than a
     * bug report. Driven through the scheduler primitive the panel
     * calls, since gencheck links no widgets.
     */
    {
        clearChannels(synth);

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(breath, &sched))
            fail("breath.gen did not load for the unbind check");
        else
        {
            thcChain *c = sched.chain(0);
            thcStage *src = c != NULL && !c->stages.empty()
                ? c->stages.back().get() : NULL;
            const int idx = src != NULL
                ? src->plugin->paramIndex("prob") : -1;

            if (src == NULL || idx < 0)
                fail("breath.gen's first chain no longer ends in a stage "
                     "with a 'prob' param");
            else if (src->params.nodeBinding(idx) == NULL)
                fail("breath.gen's 'prob' is not node-driven any more, so "
                     "the unbind check is testing nothing");
            else
            {
                /* Somewhere the LFO is not, so "it took" cannot be read
                   off a value the node might have produced anyway. */
                sched.start();

                for (int i = 0; i < 40; i++)
                    sched.stepTransport(0.02);

                sched.unbindParam(src, idx);
                src->params.set(idx, 0.9);

                if (src->params.nodeBinding(idx) != NULL)
                    fail("unbindParam left the node binding in place");

                bool moved = false;

                for (int i = 0; i < 40; i++)
                {
                    sched.stepTransport(0.02);

                    if (fabs(src->params.get(idx) - 0.9) > 1e-6)
                        moved = true;
                }

                if (moved)
                    fail("a value written over a node-driven param was "
                         "still shadowed by the node");

                sched.stop();
            }
        }

        clearChannels(synth);
    }

    /* ---- the refusals ---- */

    expectReject(plugins, synth, "node-bad-family",
        "chain c { stage d dist::clip { };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "not a family that means anything at control rate");

    expectReject(plugins, synth, "node-samples-family",
        "chain c { stage e delay::echo { };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "counts in samples");

    expectReject(plugins, synth, "node-not-replayable",
        "chain c { stage n osc::static { };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "would not replay");

    expectReject(plugins, synth, "node-no-such-module",
        "chain c { stage n osc::nosuchosc { };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "nosuchosc");

    /* An arrow to a node nobody declared, and to an arg it does not
       have. Both are the composer-side spelling, which is where a
       reader is most likely to get it wrong. */
    expectReject(plugins, synth, "arrow-no-node",
        "chain c { stage s gen::eno_line { prob = ghost->out; };"
        " sink { channel = 1; }; };",
        "no dsp stages");

    expectReject(plugins, synth, "arrow-no-arg",
        "chain c { stage lfo osc::simple { freq = 1; };"
        " stage s gen::eno_line { prob = lfo->nosucharg; };"
        " sink { channel = 1; }; };",
        "nosucharg");

    /* An arg the module does not declare. thNode::setArg invents one,
       which is right for a .dsp and silent here: `frq' for `freq' gave
       an oscillator running at zero and a piece that simply did not
       breathe, with no error anywhere. */
    expectReject(plugins, synth, "node-bad-arg",
        "chain c { stage lfo osc::simple { frq = 0.05; };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "no arg called 'frq'");

    /* Both ends of a wire, not just the near one. setPointers would
       otherwise create the missing far arg as a permanent zero. */
    expectReject(plugins, synth, "wire-bad-far-arg",
        "chain c { stage lfo osc::simple { freq = 1; };"
        " stage m math::mul { in0 = lfo->nosuch; };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "no arg called 'nosuch'");

    /* A module's own scratch is not a port. */
    expectReject(plugins, synth, "node-state-arg",
        "chain c { stage lfo osc::simple { freq = 1; };"
        " stage s gen::eno_line { prob = lfo->last; };"
        " sink { channel = 1; }; };",
        "own scratch");

    /* Nor is an input something to read out of. */
    expectReject(plugins, synth, "arrow-at-an-input",
        "chain c { stage lfo osc::simple { freq = 1; };"
        " stage s gen::eno_line { prob = lfo->freq; };"
        " sink { channel = 1; }; };",
        "is an input, not an output");

    /* And an output is not something to write into -- the other end of
       the same arrow, in its three spellings.
     *
       The third one is why these are worth having. A node pointing its
       own output at itself built a thArg whose pointer resolves to
       itself, and thSynthTree::getArg follows a pointer in a loop with
       no exit: the file loaded without a word of complaint and the
       first window hung the GUI thread. The other two were quiet
       instead of fatal -- a value the plugin overwrote every window,
       and a module handed somebody else's buffer to write into -- which
       is the same silence a mistyped arg name used to produce, and the
       reason checkArg exists at all. */
    expectReject(plugins, synth, "value-at-an-output",
        "chain c { stage lfo osc::simple { freq = 1; out = 0.5; };"
        " stage s gen::eno_line { prob = lfo->out; };"
        " sink { channel = 1; }; };",
        "cannot write it");

    expectReject(plugins, synth, "wire-into-an-output",
        "chain c { stage lfo osc::simple { freq = 1; };"
        " stage m math::mul { in0 = lfo->out; in1 = 2; out = lfo->out; };"
        " stage s gen::eno_line { prob = m->out; };"
        " sink { channel = 1; }; };",
        "cannot write it");

    expectReject(plugins, synth, "node-wired-to-itself",
        "chain c { stage m math::mul { in0 = 1; in1 = 2; out = m->out; };"
        " stage s gen::eno_line { prob = m->out; };"
        " sink { channel = 1; }; };",
        "cannot write it");

    /* A knob cannot write one either -- the same end of the arrow,
       reached from the namespace phase 2 unified. */
    expectReject(plugins, synth, "knob-at-an-output",
        "@depth = 0.5;\n@depth.min = 0;\n@depth.max = 1;\n"
        "chain c { stage lfo osc::simple { freq = 1; out = @depth; };"
        " stage s gen::eno_line { prob = lfo->out; };"
        " sink { channel = 1; }; };",
        "cannot write it");

    /* In an allowed family and refused by name: filt:: is admitted
       because a filter is a shape over time, and comb is a delay line
       whose `size' is a raw sample count. Same criterion delay:: and
       fft:: are refused by, arriving one category later. */
    expectReject(plugins, synth, "node-comb-counts-samples",
        "chain c { stage k filt::comb { in = 0.5; size = 4410; };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "delay line");

    /* The name this host gives the node it invents. */
    expectReject(plugins, synth, "node-called-ionode",
        "chain c { stage ionode osc::simple { freq = 1; };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "call it something else");

    /* And a wire between nodes pointing at nothing, which is the same
       mistake one level down. */
    expectReject(plugins, synth, "wire-no-node",
        "chain c { stage m math::mul { in0 = ghost->out; };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "there is no node called 'ghost'");

    /* A node cannot drive something that is not a number, for the same
       reason a knob cannot. */
    expectReject(plugins, synth, "arrow-on-noteset",
        "chain c { stage lfo osc::simple { freq = 1; };"
        " stage s gen::eno_line { notes = lfo->out; };"
        " sink { channel = 1; }; };",
        "a node cannot drive it");

    clearChannels(synth);
}

/* ---- 6f. structure edits: composers reshaping instruments -------------- */

/* UNIFICATION.md phase 4. What has to be true:
 *
 * 1. A swap actually swaps -- the channel is playing a different graph
 *    afterwards, not merely told to.
 * 2. A node-arg edit reaches a constant the patch never declared, which
 *    is the whole of what makes it a different mechanism from a chanarg
 *    rather than a wider one.
 * 3. Both replay. They are events, so they should; a piece full of them
 *    rendered twice with a reset between must be byte-identical.
 * 4. A rewind puts the instruments back as the file declared them,
 *    because after a swap the channels no longer say what the file says.
 * 5. The refusals -- an instrument nobody declared, an arg no node has,
 *    a node arg that is wired rather than constant.
 */
static void
checkStructureEdits (const std::map<std::string, thcPlugin *> &plugins,
                     thSynth *synth, const std::string &genFile)
{
    const std::string piece =
        (std::filesystem::path(genFile).parent_path() / "reshape.gen").string();

    if (!std::filesystem::exists(piece))
    {
        fail("reshape.gen is not beside " + genFile);
        return;
    }

    clearChannels(synth);

    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(piece, &sched))
        {
            for (size_t i = 0; i < loader.errors().size(); i++)
                fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

            fail("reshape.gen does not load");
            return;
        }

        /* voice is amb01 and glass is ts1, and only one of them declares
           `cutoff'. So which graph is on the channel is a question with
           an answer, rather than a thing to take on trust. */
        const thcInstrument *voice = sched.instrument("voice");
        const thcInstrument *glass = sched.instrument("glass");

        if (voice == NULL || glass == NULL)
            fail("reshape.gen no longer declares voice and glass");
        else
        {
            const int ch = voice->channel;

            drainSynth();

            if (synth->getChanArg(ch, "fmin") == NULL)
                fail("the piece did not open on the instrument it declares");

            std::string why;

            if (!sched.swapInstrument(ch, "glass", why))
                fail("a swap to glass was refused: " + why);

            drainSynth();

            /* ts1 has a cutoff and amb01 does not: the graph changed. */
            if (synth->getChanArg(ch, "cutoff") == NULL)
                fail("after the swap the channel is not playing glass");

            if (synth->getChanArg(ch, "fmin") != NULL)
                fail("after the swap the old graph is still there");

            /* And the values came with it, not just the graph. */
            thArg *res = synth->getChanArg(ch, "res");

            if (res == NULL || fabs((*res)[0] - 0.5) > 1e-4)
                fail("the swapped-in instrument did not bring its values");

            /* Back, and then the fine edit -- which needs amb01, since
               `fmap' is one of its nodes. */
            if (!sched.swapInstrument(ch, "voice", why))
                fail("a swap back to voice was refused: " + why);

            drainSynth();

            if (!sched.setNodeArg(ch, "fmap", "inmax", 0.25f, why))
                fail("a node arg the patch never declared was refused: " +
                     why);
            else
            {
                /* Read back off the prototype tree, which is where a
                   new voice would read it. */
                thMidiChan *c = synth->getChannel(ch);
                thSynthTree *tree = c != NULL ? c->modnode() : NULL;
                thNode *n = tree != NULL ? tree->findNode("fmap") : NULL;
                thArg *a = n != NULL ? n->getArg("inmax") : NULL;

                if (a == NULL || fabs((*a)[0] - 0.25) > 1e-5)
                    fail("the node arg did not take");

                /* And it really is not a chanarg -- if it were, this
                   whole mechanism would be a chanarg with extra steps. */
                if (synth->getChanArg(ch, "inmax") != NULL)
                    fail("'inmax' is a chanarg after all, which would make "
                         "this the wrong test entirely");
            }

            /* A rewind puts the file back. */
            sched.reset();
            drainSynth();

            thNode *n2 = NULL;
            thMidiChan *c2 = synth->getChannel(ch);
            thSynthTree *t2 = c2 != NULL ? c2->modnode() : NULL;

            if (t2 != NULL)
                n2 = t2->findNode("fmap");

            thArg *a2 = n2 != NULL ? n2->getArg("inmax") : NULL;

            if (a2 != NULL && fabs((*a2)[0] - 0.25) < 1e-5)
                fail("a rewind left a structure edit in place");
        }
    }

    drainSynth();

    /* Replay, the gate every piece passes and the one a structure edit
       had to earn before it was allowed to exist. */
    {
        clearChannels(synth);

        thcScheduler a(synth);
        thcGenLoader la(plugins);

        if (!la.load(piece, &a))
            fail("reshape.gen did not load for the replay check");
        else
        {
            const std::string first = render(a, 130.0, 0.02);

            a.reset();

            const std::string second = render(a, 130.0, 0.02);

            if (first.empty())
                fail("reshape.gen delivered nothing at all");
            else if (first.find("P ") == std::string::npos)
                fail("no structure edits in reshape.gen's stream");
            else if (first != second)
                fail("a piece with structure edits in it did not replay");

            /* And the sweep moves on every step it takes.
             *
               A ping-pong that reflects *at* its walls rather than one
               short of them emits its top value twice running, and its
               bottom twice running -- a cycle two ticks long than the
               file asks for that stalls at each extreme. Nothing about
               the replay gate can see that: both renders stall
               identically. Consecutive equal values on one arg is the
               shape of the bug, said directly. */
            std::map<std::string, std::string> last;
            std::istringstream lines(first);
            std::string line;

            while (std::getline(lines, line))
            {
                if (line.compare(0, 2, "E ") != 0)
                    continue;

                /* "E <at> <ch> <node> <arg> <value>": node and arg
                   together are which constant this is, and the value is
                   what has to have moved since last time. */
                std::istringstream f(line);
                std::string tag, at, ch, node, arg, val;

                if (!(f >> tag >> at >> ch >> node >> arg >> val))
                    continue;

                const std::string key = ch + " " + node + "." + arg;

                if (last.count(key) && last[key] == val)
                {
                    fail("a node-arg sweep emitted '" + val +
                         "' twice running for " + key);
                    break;
                }

                last[key] = val;
            }

            if (last.empty())
                fail("no node-arg edits in reshape.gen's stream");
        }
    }

    drainSynth();
    clearChannels(synth);

    /* ---- what `pass = 0' is allowed to eat ---- */

    /* A transformer told not to pass notes eats the notes, and nothing
     * else.
     *
     * Two halves of one rule. A stage consuming what it is *for* is a
     * design; a stage consuming everything that happens to flow through
     * it is a hole in a pipeline -- a gen::reshape in front of a
     * `pass = 0' arp delivered nothing at all, silently, and the piece
     * simply never changed. And the off goes with its on: forwarding a
     * release whose press this stage ate is an off for a note it did not
     * play, which the scheduler hands to delNote and which then silences
     * whatever else is sounding at that pitch.
     */
    {
        clearChannels(synth);

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);
        std::string tmp = thUtil::tempFile("gencheck-passrule-");

        if (tmp.empty())
            fail("the pass-rule check could not make a scratch file");
        else
        {
            {
                std::ofstream out(tmp.c_str(), std::ios::trunc);

                out << "seed 7;\n"
                    << "instrument pad { dsp \"amb01.dsp\"; };\n"
                    << "chain edits {\n"
                    << "  stage r gen::reshape { node = \"fmap\";"
                    << " arg = \"inmax\"; from = 1; to = 0.25;"
                    << " every = 1 s; steps = 4; };\n"
                    << "  stage a xform::arp { pass = 0; };\n"
                    << "  sink { instrument = pad; };\n"
                    << "};\n"
                    << "chain keys {\n"
                    << "  input midi;\n"
                    << "  stage m xform::markov { pass = 0; };\n"
                    << "  sink { instrument = pad; };\n"
                    << "};\n"
                    /* All three transformers that carry the rule, since
                       the fix landed on one of them first and the other
                       two kept the bug for a release. */
                    << "chain keys2 {\n"
                    << "  input midi;\n"
                    << "  stage l xform::life { pass = 0; listen = 1; };\n"
                    << "  sink { instrument = pad; };\n"
                    << "};\n"
                    << "chain keys3 {\n"
                    << "  input midi;\n"
                    << "  stage p xform::arp { pass = 0; };\n"
                    << "  sink { instrument = pad; };\n"
                    << "};\n";

                if (!out.good())
                    fail("the pass-rule check could not write " + tmp);
            }

            if (!loader.load(tmp, &sched))
            {
                for (size_t i = 0; i < loader.errors().size(); i++)
                    fprintf(stderr, "gencheck: %s\n",
                            loader.errors()[i].c_str());

                fail("the pass-rule piece did not load");
            }
            else
            {
                int edits = 0, notes = 0;

                sigc::connection conn = sched.sigDelivered.connect(
                    [&edits, &notes](const thcEvent &ev)
                    {
                        if (ev.type == THC_EV_NODEARG)
                            edits++;
                        else if (ev.type == THC_EV_NOTE ||
                                 ev.type == THC_EV_NOTEOFF)
                            notes++;
                    });

                sched.start();

                /* The press and its release, both of which the markov
                   stage is told to eat. */
                thcEvent key = {};

                key.type = THC_EV_NOTE;
                key.channel = 0;
                key.u.note.note = 60;
                key.u.note.velocity = 90;
                key.u.note.duration = 0;
                key.at = sched.now();
                sched.injectMidiEvent(key);

                sched.stepTransport(0.1);

                thcEvent up = {};

                up.type = THC_EV_NOTEOFF;
                up.channel = 0;
                up.u.note.note = 60;
                up.at = sched.now();
                sched.injectMidiEvent(up);

                for (int i = 0; i < 200; i++)
                    sched.stepTransport(0.02);

                sched.stop();
                conn.disconnect();
                drainSynth();

                if (edits == 0)
                    fail("a 'pass = 0' transformer swallowed the structure "
                         "edits of the stage in front of it");

                if (notes != 0)
                    fail("a 'pass = 0' transformer ate a press and "
                         "forwarded its release anyway");
            }

            std::filesystem::remove(tmp);
        }

        clearChannels(synth);
    }

    /* ---- two events at one instant come out the way they went in ---- */

    /* A heap does not order equal keys, and a re-pressed held root makes
     * a pile of exactly equal keys: xform::harmonize releases the old
     * chord at `at + spread*v' and presses the new one at `at + spread*v'
     * -- the same instant, deliberately, because that is what replacing a
     * chord means. Popped in heap order an on could be delivered ahead of
     * the off emitted before it, and deliver() then ran addNote followed
     * by delNote on the same pitch: the voice was created and immediately
     * killed, and held_ kept an entry for a note nothing was playing.
     *
     * Asserted as the harm rather than as the ordering, because the
     * ordering is only interesting for what it does: walk the delivered
     * stream in order, and every pitch of a chord nobody released has to
     * end up sounding.
     */
    {
        clearChannels(synth);

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);
        std::string tmp = thUtil::tempFile("gencheck-tieorder-");

        if (tmp.empty())
            fail("the tie-order check could not make a scratch file");
        else
        {
            {
                std::ofstream out(tmp.c_str(), std::ios::trunc);

                out << "seed 3;\n"
                    << "instrument pad { dsp \"amb01.dsp\"; };\n"
                    << "chain keys {\n"
                    << "  input midi;\n"
                    /* spread 0 so every voice collides, which is the
                       case the heap was free to reorder. */
                    << "  stage h xform::harmonize { voices = 3;"
                    << " spread = 0 s; root = 1; };\n"
                    << "  sink { instrument = pad; };\n"
                    << "};\n";

                if (!out.good())
                    fail("the tie-order check could not write " + tmp);
            }

            if (!loader.load(tmp, &sched))
                fail("the tie-order piece did not load");
            else
            {
                std::map<int, bool> sounding;
                int offs = 0;

                sigc::connection conn = sched.sigDelivered.connect(
                    [&sounding, &offs](const thcEvent &ev)
                    {
                        if (ev.type == THC_EV_NOTE)
                            sounding[ev.u.note.note] = true;
                        else if (ev.type == THC_EV_NOTEOFF)
                        {
                            sounding[ev.u.note.note] = false;
                            offs++;
                        }
                    });

                sched.start();

                thcEvent key = {};

                key.type = THC_EV_NOTE;
                key.channel = 0;
                key.u.note.note = 60;
                key.u.note.velocity = 90;
                key.u.note.duration = 0;    /* held */
                key.at = sched.now();
                sched.injectMidiEvent(key);

                for (int i = 0; i < 10; i++)
                    sched.stepTransport(0.02);

                /* The re-press: one key, one entry, so the old chord is
                   released and the new one pressed at the same instant. */
                key.at = sched.now();
                sched.injectMidiEvent(key);

                for (int i = 0; i < 20; i++)
                    sched.stepTransport(0.02);

                sched.stop();
                conn.disconnect();
                drainSynth();

                if (offs == 0)
                    fail("the tie-order check saw no releases, so the "
                         "re-press it is about did not happen");

                if (sounding.empty())
                    fail("the tie-order check heard nothing at all");

                for (std::map<int, bool>::const_iterator i = sounding.begin();
                     i != sounding.end(); ++i)
                    if (!i->second)
                    {
                        std::ostringstream s;

                        s << "a re-pressed chord ended with pitch "
                          << i->first << " released: an on was delivered "
                          << "before the off emitted ahead of it";
                        fail(s.str());
                        break;
                    }
            }

            std::filesystem::remove(tmp);
        }

        clearChannels(synth);
    }

    /* ---- what a rewind reaches, and what it leaves alone ---- */

    /* A rewind restores the channels a structure edit touched and no
     * others.
     *
     * applyInstrument goes through the host's patch loader, which drops
     * the channel and re-parses the .dsp -- so re-applying every
     * declaration because *one* of them moved threw away hand-tuned
     * values and disarmed probes on channels nothing had been near, and
     * did it only once a swap had happened to fire. Rewind behaving
     * differently depending on how far the piece had got is the part
     * worth a gate: the cheap way to see it is that an untouched
     * channel's graph is the same object afterwards, where a restored
     * one is not.
     */
    {
        clearChannels(synth);

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(piece, &sched))
            fail("reshape.gen did not load for the rewind-scope check");
        else
        {
            const thcInstrument *voice = sched.instrument("voice");
            const thcInstrument *other = sched.instrument("bell");

            if (voice == NULL || other == NULL ||
                voice->channel == other->channel)
                fail("reshape.gen no longer declares voice and bell on "
                     "channels of their own");
            else
            {
                drainSynth();

                thMidiChan *before = synth->getChannel(other->channel);
                std::string why;

                /* One edit, on voice's channel only. */
                if (!sched.setNodeArg(voice->channel, "fmap", "inmax",
                                      0.25f, why))
                    fail("the rewind-scope check could not make its edit: " +
                         why);

                sched.reset();
                drainSynth();

                if (synth->getChannel(other->channel) != before)
                    fail("a rewind rebuilt a channel no structure edit had "
                         "touched");

                thMidiChan *c = synth->getChannel(voice->channel);
                thSynthTree *t = c != NULL ? c->modnode() : NULL;
                thNode *n = t != NULL ? t->findNode("fmap") : NULL;
                thArg *a = n != NULL ? n->getArg("inmax") : NULL;

                if (n == NULL || a == NULL)
                    fail("a rewind left the edited channel without the "
                         "graph its declaration names");
                else if (fabs((*a)[0] - 0.25) < 1e-5)
                    fail("a rewind left a structure edit in place");
            }
        }

        clearChannels(synth);
    }

    /* ---- a list the panel wrote rather than the loader ---- */

    /* The loader normalises an instrument list to "voice,bell,glass" and
     * checks every name in it, so a piece read off disk never exercises
     * what a composer does with the separators. The param panel is the
     * other writer, and it stores what was typed: a list edited in the
     * window to "voice, bell, glass" reaches the plugin with the spaces
     * still in it. Splitting on commas alone made that a name with a
     * space welded to the front, and every swap to it was refused by a
     * service that had never heard of " bell" -- a piece that quietly
     * stopped swapping, with nothing in the log tying it to the edit
     * that did it.
     *
     * Asserted on the event rather than on the tape, because the tape is
     * whitespace-separated and reading a name back out of it would eat
     * the very space this is about.
     */
    {
        clearChannels(synth);

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(piece, &sched))
            fail("reshape.gen did not load for the spaced-list check");
        else
        {
            thcStage *swap = NULL;

            for (size_t i = 0; i < sched.chainCount() && swap == NULL; i++)
            {
                thcChain *c = sched.chain(i);

                for (size_t j = 0; c != NULL && j < c->stages.size(); j++)
                    if (c->stages[j]->plugin->name() == "swap")
                    {
                        swap = c->stages[j].get();
                        break;
                    }
            }

            if (swap == NULL)
                fail("reshape.gen no longer has a gen::swap stage");
            else
            {
                const int idx = swap->plugin->paramIndex("instruments");

                /* Exactly what ComposerWindow::applyParam does to a live
                   stage: the typed text, stored as typed, then the
                   changed notification the panel sends after it. */
                if (idx < 0 ||
                    !swap->params.setString("instruments",
                                            "voice, bell, glass"))
                    fail("gen::swap has no 'instruments' param");
                else
                {
                    swap->params.notifyChanged(idx);

                    std::vector<std::string> swapped;
                    sigc::connection conn = sched.sigDelivered.connect(
                        [&swapped](const thcEvent &ev)
                        {
                            if (ev.type == THC_EV_PATCH)
                                swapped.push_back(ev.u.patch.name
                                                  ? ev.u.patch.name : "");
                        });

                    sched.start();

                    while (sched.now() < 130.0)
                        sched.stepTransport(0.02);

                    sched.stop();
                    conn.disconnect();
                    drainSynth();

                    for (size_t i = 0; i < swapped.size(); i++)
                        if (sched.instrument(swapped[i]) == NULL)
                            fail("a spaced instrument list swapped to '" +
                                 swapped[i] + "', which the piece does "
                                 "not declare");

                    if (swapped.empty())
                        fail("a spaced instrument list produced no swaps");
                }
            }
        }
    }

    drainSynth();
    clearChannels(synth);

    /* ---- the refusals ---- */

    expectReject(plugins, synth, "swap-unknown-instrument",
        "instrument pad { dsp \"amb01.dsp\"; };\n"
        "chain c { stage m gen::swap { instruments = ghost; };"
        " sink { instrument = pad; }; };",
        "no instrument called 'ghost'");

    expectReject(plugins, synth, "swap-unknown-in-list",
        "instrument pad { dsp \"amb01.dsp\"; };\n"
        "chain c { stage m gen::swap { instruments = \"pad,ghost\"; };"
        " sink { instrument = pad; }; };",
        "no instrument called 'ghost'");

    /* The services say no by name, which is what a piece hitting one
       mid-play has to read in the log. */
    {
        clearChannels(synth);

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        /* Said rather than skipped. A block of refusals inside an
           `if (loaded)' with no else is a block that reports success the
           day the piece stops loading, which is the day you want to hear
           about it most. */
        if (!loader.load(piece, &sched))
            fail("the structure-edit piece did not load");
        else
        {
            const thcInstrument *voice = sched.instrument("voice");
            const int ch = voice != NULL ? voice->channel : 0;
            std::string why;

            drainSynth();

            if (sched.swapInstrument(ch, "nosuchinstrument", why))
                fail("a swap to an undeclared instrument succeeded");

            /* A channel the piece declares no instrument for. A swap
               rebuilds a whole graph, so reaching one is reaching into
               somebody's loaded patch and throwing it away -- and a
               rewind would not put it back, because it is in no
               declaration to be restored from. */
            if (sched.swapInstrument(15, "bell", why))
                fail("a swap onto a channel the piece does not own "
                     "succeeded");

            /* And the fine edit onto the same channel, which had no such
               refusal and needed it more. A swap into somebody's
               hand-loaded patch is loud; rewriting one constant inside
               their graph is silent, and reset() restores by re-applying
               *declarations*, so there is nothing that would ever put it
               back.
             *
               With a graph actually on that channel, which is the whole
               point: written against an empty one this passed on
               "nothing is loaded there" and would have gone on passing
               with the refusal deleted. So put the patch a person would
               have had loaded onto it first, and then ask -- and read
               the message, because there are two ways to say no here and
               only one of them is the one under test. */
            const std::string handLoaded = thUtil::findDataFile(
                "amb01.dsp", "dsp", "THINK_DSP_PATH", DSP_PATH);

            if (handLoaded.empty() ||
                synth->loadTree(handLoaded.c_str(), 15,
                                TH_DEFAULT_CHAN_AMP) == NULL)
                fail("could not put a patch on channel 16 by hand");
            else
            {
                drainSynth();

                why.clear();

                if (sched.setNodeArg(15, "fmap", "inmax", 0.25f, why))
                    fail("a node arg on a channel the piece does not own "
                         "succeeded");
                else if (why.find("does not") == std::string::npos &&
                         why.find("not one this piece declares") ==
                             std::string::npos)
                    fail("a node arg on an undeclared channel was refused, "
                         "but for the wrong reason: " + why);
            }

            if (sched.setNodeArg(ch, "nosuchnode", "x", 1, why))
                fail("a node arg on a node that is not there succeeded");

            if (sched.setNodeArg(ch, "fmap", "nosucharg", 1, why))
                fail("a node arg the module never declared succeeded");

            /* `fmap.in' is wired to ionode->velocity. Writing a number
               over it would silently unwire the graph, which is an
               add/remove/rewire edit wearing a value edit's clothes. */
            if (sched.setNodeArg(ch, "fmap", "in", 1, why))
                fail("a wired node arg was overwritten with a constant");

            /* And the other kind of wire, which is the one the first
               draft let through: `fmap.outmin' is `@fmin'. thNode::setArg
               retypes an arg to ARG_VALUE whatever it was, and
               assignChanArgPointers only re-points args still typed
               ARG_CHANNEL -- so a number written here kills that
               channel's fmin for the rest of the session, silently. */
            if (sched.setNodeArg(ch, "fmap", "outmin", 0.5, why))
                fail("a node arg wired to a chanarg was overwritten with "
                     "a constant");

            /* And a swap to what is already there does nothing rather
               than rebuilding the graph into a copy of itself, because a
               gen::swap cannot see what its sink's channel holds: point
               one at a list whose first name is what the sink already
               plays and the opening tick would cut every sounding voice
               for no change. "Did nothing" is the prototype tree still
               being the same object -- a rebuild goes through loadTree,
               which makes a new one. */
            thMidiChan *mc = synth->getChannel(ch);
            const void *before = mc != NULL ? (void *)mc->modnode() : NULL;

            if (!sched.swapInstrument(ch, sched.holding(ch), why))
                fail("a swap to the instrument already there was refused");

            mc = synth->getChannel(ch);

            if (before == NULL ||
                before != (const void *)(mc != NULL ? mc->modnode() : NULL))
                fail("a swap to the instrument already there rebuilt the "
                     "graph anyway");

            /* A swapped-away instrument stops driving the channel it
               was on.
             *
               A knob bound into an instrument's chanarg is a *push*: the
               knob moves, the chanarg is written. The connections used
               to be appended and never removed, so swapping `quiet' onto
               `loud's channel left loud's binding pushing into it
               alongside quiet's own values -- one knob nudge and the
               instrument the file says is playing is not the one you
               hear. Both instruments are the same .dsp on purpose, so
               the stale binding finds a real arg to write through
               instead of failing to find one and looking fixed. */
            {
                const std::string body =
                    "@k = 0.4;\n@k.min = 0;\n@k.max = 1;\n"
                    "instrument loud {\n"
                    "    dsp \"amb01.dsp\";\n"
                    "    fmin = @k;\n"
                    "};\n"
                    "instrument quiet {\n"
                    "    dsp \"amb01.dsp\";\n"
                    "    fmin = 0.2;\n"
                    "};\n"
                    "chain c { stage s gen::eno_line { };"
                    " sink { instrument = loud; }; };\n";

                const std::string path = thUtil::tempFile("gencheck-swapkn-");

                if (path.empty())
                    fail("could not write the swapped-knob piece");
                else
                {
                    FILE *f = fopen(path.c_str(), "w");

                    if (f == NULL)
                        fail("could not write the swapped-knob piece");
                    else
                    {
                        fputs(body.c_str(), f);
                        fclose(f);
                    }

                    clearChannels(synth);

                    thcScheduler s2(synth);
                    thcGenLoader l2(plugins);

                    if (!l2.load(path, &s2))
                        fail("the swapped-knob piece did not load");
                    else
                    {
                        const thcInstrument *loud = s2.instrument("loud");
                        const int lch = loud != NULL ? loud->channel : 0;
                        std::string w2;

                        drainSynth();

                        if (!s2.swapInstrument(lch, "quiet", w2))
                            fail("swapping quiet in failed: " + w2);
                        else
                        {
                            drainSynth();

                            thArg *k = s2.knob("k");
                            thArg *fmin = synth->getChanArg(lch, "fmin");

                            if (k == NULL || fmin == NULL)
                                fail("the swapped-in instrument did not "
                                     "arrive whole");
                            else
                            {
                                k->setValue(0.9f);

                                if (fabs((*fmin)[0] - 0.2) > 1e-5)
                                    fail("a swapped-away instrument's knob "
                                         "binding still drives the channel");
                            }
                        }
                    }

                    remove(path.c_str());
                }

                clearChannels(synth);
            }

        }
    }

    clearChannels(synth);
}

/* How many notes a scratch piece delivers in `seconds'. The two things
 * a board fed notes can be asked -- "did it draw" and "did it refuse to
 * draw" -- are both this number against zero. */
static int
notesFrom (const std::map<std::string, thcPlugin *> &plugins,
           thSynth *synth, const std::string &what,
           const std::string &body, double seconds)
{
    const std::string path = thUtil::tempFile("gencheck-plays-");

    if (path.empty())
    {
        fail("could not write the piece for " + what);
        return -1;
    }

    {
        std::ofstream out(path.c_str(), std::ios::trunc);

        out << body;
    }

    clearChannels(synth);
    drainSynth();

    int n = -1;

    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(path, &sched))
        {
            for (size_t i = 0; i < loader.errors().size(); i++)
                fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

            fail(what + " did not load");
        }
        else
        {
            std::istringstream lines(render(sched, seconds, 0.02));
            std::string line;

            n = 0;

            while (std::getline(lines, line))
                if (line.compare(0, 2, "N ") == 0)
                    n++;
        }
    }

    remove(path.c_str());
    clearChannels(synth);

    return n;
}

/* ---- 7. a chain that is a pipeline ------------------------------------- */

/* colony.gen, and the two things it needed that did not exist.
 *
 * gen::life gained a receive, so an upstream stage can draw on the board
 * the way a mouse already could; xform::harmonize turns a note into a
 * chord counted in scale degrees. Both are easy to write in a way that
 * looks right and does nothing, so both are asked directly.
 *
 * 1. Feeding the board CHANGES WHAT IT PLAYS. This is the claim the
 *    piece's header makes and the one worth defending: the same board,
 *    rendered with the line and without it, must not produce the same
 *    stream -- and with it must reach rows the blinker alone never
 *    touches. A receive that quietly dropped every note would pass a
 *    "does it still load" gate forever.
 * 2. A pitch the ladder cannot spell draws nothing, rather than landing
 *    on the nearest row it can find.
 * 3. `listen = 0' is the pure generator glider.gen still wants.
 * 4. The chord is DIATONIC. Two degrees above the first pentatonic
 *    degree and two above the second are different numbers of
 *    semitones; a harmonizer that added a fixed interval would give the
 *    same gap everywhere and is the thing this must not be.
 * 5. `voices = 1' passes the note through untouched, `root = 0' drops
 *    it, and `below = 1' puts the harmony underneath.
 * 6. The whole piece replays.
 */
static void
checkColony (const std::map<std::string, thcPlugin *> &plugins,
             thSynth *synth, const std::string &genFile)
{
    const std::string piece =
        (std::filesystem::path(genFile).parent_path() / "colony.gen").string();

    if (!std::filesystem::exists(piece))
    {
        fail("colony.gen is not beside " + genFile);
        return;
    }

    clearChannels(synth);

    /* ---- 1. the line makes a difference ---- */

    /* Two renders of the same file, one with the line's onsets turned
       off. Editing the piece rather than writing a fresh one on purpose:
       what is being gated is the shipped configuration, and a scratch
       file tuned until it passed would gate nothing about it. */
    {
        const std::string text = slurp(piece);

        if (text.find("fills = 7;") == std::string::npos)
            fail("colony.gen no longer spells its euclid's fills the way "
                 "the gate looks for");
        else
        {
            std::string silent = text;
            const size_t at = silent.find("fills = 7;");

            silent.replace(at, strlen("fills = 7;"), "fills = 0;");

            const std::string sp = thUtil::tempFile("gencheck-colony-");

            if (sp.empty())
                fail("could not write the line-off variant");
            else
            {
                {
                    std::ofstream out(sp.c_str(), std::ios::trunc);

                    out << silent;
                }

                std::string withLine, without;
                std::set<int> pitchesWith, pitchesWithout;

                for (int pass = 0; pass < 2; pass++)
                {
                    clearChannels(synth);
                    drainSynth();

                    thcScheduler sched(synth);
                    thcGenLoader loader(plugins);

                    if (!loader.load(pass ? sp : piece, &sched))
                    {
                        fail(pass ? "the line-off variant did not load"
                                  : "colony.gen did not load");
                        break;
                    }

                    const std::string tape = render(sched, 90.0, 0.04);

                    (pass ? without : withLine) = tape;

                    /* Which rows of the board were reached. Only the
                       colony's channel counts: the floor chain plays its
                       own pitches on its own instrument and would mask
                       a board that never left its blinker. */
                    std::istringstream lines(tape);
                    std::string line;

                    while (std::getline(lines, line))
                    {
                        std::istringstream f(line);
                        std::string tag, at2, ch, note;

                        if (!(f >> tag >> at2 >> ch >> note) || tag != "N")
                            continue;

                        if (atoi(ch.c_str()) != 0)
                            continue;

                        (pass ? pitchesWithout : pitchesWith)
                            .insert(atoi(note.c_str()));
                    }
                }

                if (withLine.empty())
                    fail("colony.gen delivered nothing at all");
                else if (withLine == without)
                    fail("feeding gen::life a line changed nothing about "
                         "what it played");
                else if (pitchesWith.size() <= pitchesWithout.size())
                    fail("the line did not spread the colony past the "
                         "pitches the blinker reaches alone");

                remove(sp.c_str());
            }
        }
    }

    clearChannels(synth);
    drainSynth();

    /* ---- 2, 3. what the board does and does not accept ---- */

    /* One generation of a board fed four notes: two the ladder can spell
       and two it cannot. Only the spellable ones may draw. */
    /* A 4x2 board, empty, and a rhythm on the two pitches its ladder
       spells. `trigger = 1' so a drawn cell is heard the generation it
       is drawn rather than having to survive into a birth -- what is
       being asked here is whether the cell arrived at all. */
    {
        const char *shape =
            "instrument pad { dsp \"amb01.dsp\"; };\n"
            "chain c {\n"
            "  stage src gen::euclid { steps = 4; fills = 4; notes = \"%s\";"
            " period = 0.25 s; hold = 0.2 s; };\n"
            "  stage b gen::life { width = 4; height = 2; wrap = 0;"
            " scatter = 0; board = \"..../....\"; notes = \"C4 D4\";"
            " trigger = 1; period = 1 s; hold = 0.5 s; listen = %d;"
            " pass = 0; };\n"
            "  sink { instrument = pad; };\n"
            "};\n";

        char body[1024];

        snprintf(body, sizeof(body), shape, "C4 D4", 1);

        if (notesFrom(plugins, synth, "life-listens", body, 6.0) <= 0)
            fail("a line played into gen::life drew nothing on the board");

        /* C#4 and D#4 are not on a ladder of C4 and D4. A receive that
           snapped to the nearest row would fill this board just as fast
           as the one above, and a piece would have no way to tell a
           mistyped note from a meant one. */
        snprintf(body, sizeof(body), shape, "C#4 D#4", 1);

        if (notesFrom(plugins, synth, "life-ignores-unspellable",
                      body, 6.0) != 0)
            fail("gen::life drew cells for pitches its ladder cannot spell");

        /* And `listen = 0' is the plugin glider.gen has always had. */
        snprintf(body, sizeof(body), shape, "C4 D4", 0);

        if (notesFrom(plugins, synth, "life-listen-0", body, 6.0) != 0)
            fail("gen::life drew on its board with listen = 0");
    }

    /* `pass' is about notes, and a stage that ate anything else would
       be a hole in the pipeline rather than a stage in it.
     *
       The first draft gated the emit on `pass' for every event type, so
       a `pass = 0' board silently swallowed the note-offs, chanarg
       writes and structure edits of every stage upstream of it -- a
       gen::reshape in front of one delivered nothing at all, and the
       piece it was in had a chain that quietly did nothing. Nothing a
       note tape can see, which is why it is asked here directly. */
    {
        const std::string body =
            "instrument pad { dsp \"amb01.dsp\"; };\n"
            "chain c {\n"
            "  stage r gen::reshape { node = \"fmap\"; arg = \"inmax\";"
            " from = 1; to = 0.5; every = 0.5 s; steps = 4; };\n"
            "  stage b gen::life { width = 4; height = 2; wrap = 0;"
            " scatter = 0; board = \"..../....\"; notes = \"C4 D4\";"
            " period = 1 s; hold = 0.5 s; listen = 1; pass = 0; };\n"
            "  sink { instrument = pad; };\n"
            "};\n";

        const std::string path = thUtil::tempFile("gencheck-passthru-");

        if (path.empty())
            fail("could not write the pass-through piece");
        else
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << body;
            }

            clearChannels(synth);
            drainSynth();

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            if (!loader.load(path, &sched))
                fail("the pass-through piece did not load");
            else if (render(sched, 6.0, 0.02).find("E ") == std::string::npos)
                fail("a gen::life with pass = 0 swallowed the structure "
                     "edits of the stage in front of it");

            remove(path.c_str());
            clearChannels(synth);
        }
    }

    clearChannels(synth);
    drainSynth();

    /* ---- 4, 5. the chord is counted in degrees ---- */

    {
        clearChannels(synth);
        drainSynth();

        /* A minor pentatonic on A: 45 48 50 52 55, then 57. Two degrees
           above 45 is 50 -- five semitones -- and two above 48 is 52,
           which is four. A fixed-interval shifter cannot tell those
           apart, and telling them apart is the whole plugin. */
        const std::string body =
            "instrument pad { dsp \"amb01.dsp\"; };\n"
            "chain c {\n"
            "  stage src gen::euclid { steps = 2; fills = 2;"
            "    notes = \"A2 C3\"; period = 1 s; hold = 0.5 s; };\n"
            "  stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
            "    voices = 2; step = 2; spread = 0 s; taper = 1; };\n"
            "  sink { instrument = pad; };\n"
            "};\n";

        const std::string path = thUtil::tempFile("gencheck-harm-");

        if (path.empty())
            fail("could not write the harmonize piece");
        else
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << body;
            }

            clearChannels(synth);

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            if (!loader.load(path, &sched))
                fail("the harmonize piece did not load");
            else
            {
                std::set<int> heard;
                std::istringstream lines(render(sched, 6.0, 0.02));
                std::string line;

                while (std::getline(lines, line))
                {
                    std::istringstream f(line);
                    std::string tag, at, ch, note;

                    if ((f >> tag >> at >> ch >> note) && tag == "N")
                        heard.insert(atoi(note.c_str()));
                }

                /* Roots, and the second degree above each. */
                if (!heard.count(45) || !heard.count(48))
                    fail("harmonize dropped the roots it was given");
                else if (!heard.count(50))
                    fail("harmonize did not stack two degrees above 45");
                else if (!heard.count(52))
                    fail("harmonize did not stack two degrees above 48");
                else if (heard.count(47) || heard.count(51) ||
                         heard.count(53))
                    fail("harmonize emitted a pitch outside its scale");

                /* The two gaps differ, measured rather than asserted.
                 *
                   The first draft of this compared two literals -- 50-45
                   against 52-48 -- which the compiler can answer without
                   running anything, so it was a comment with a shape
                   like a test. What has to be measured is the *gap this
                   plugin produced*, which means finding what it stacked
                   over each root in the stream rather than naming it. */
                int over45 = -1, over48 = -1;

                for (std::set<int>::iterator i = heard.begin();
                     i != heard.end(); ++i)
                {
                    if (*i > 45 && *i < 48 + 12 && over45 < 0 && *i != 48)
                        over45 = *i;

                    if (*i > 48 && over48 < 0)
                        over48 = *i;
                }

                if (over45 < 0 || over48 < 0)
                    fail("harmonize stacked nothing over one of its roots");
                else if (over45 - 45 == over48 - 48)
                    fail("harmonize put the same number of semitones over "
                         "both roots; it is counting semitones, not "
                         "degrees");
            }

            remove(path.c_str());
        }
    }

    clearChannels(synth);
    drainSynth();

    /* `voices = 1' is a passthrough -- the identity every transformer
       should have and the one worth pinning, because it is what a piece
       reaches for when it wants the stage present and doing nothing. */
    {
        const char *shape =
            "instrument pad { dsp \"amb01.dsp\"; };\n"
            "chain c {\n"
            "  stage src gen::euclid { steps = 2; fills = 2;"
            "    notes = \"A2 C3\"; period = 1 s; hold = 0.5 s; };\n"
            "  %s"
            "  sink { instrument = pad; };\n"
            "};\n";

        struct { const char *what; const char *stage; bool wantRoot;
                 int wantOther; size_t exact; } cases[] = {
            { "voices = 1 was not a passthrough",
              "stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
              " voices = 1; };\n", true, -1, 2 },
            /* Not just "45 is there" -- euclid supplies that whatever
               harmonize does. The identity claim is that *nothing else*
               is there, which `exact' below is what checks. */
            { "root = 0 still sounded the note it was given",
              "stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
              " voices = 2; step = 2; root = 0; };\n", false, 50, 2 },
            { "below = 1 did not stack downward",
              "stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
              " voices = 2; step = 2; below = 1; };\n", true, 40, 4 },
        };

        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
        {
            char body[2048];

            snprintf(body, sizeof(body), shape, cases[c].stage);

            const std::string path = thUtil::tempFile("gencheck-harm2-");

            if (path.empty())
            {
                fail("could not write a harmonize variant");
                continue;
            }

            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << body;
            }

            clearChannels(synth);
            drainSynth();

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            if (!loader.load(path, &sched))
                fail(std::string("a harmonize variant did not load: ") +
                     cases[c].what);
            else
            {
                std::set<int> heard;
                std::istringstream lines(render(sched, 6.0, 0.02));
                std::string line;

                while (std::getline(lines, line))
                {
                    std::istringstream f(line);
                    std::string tag, at, ch, note;

                    if ((f >> tag >> at >> ch >> note) && tag == "N")
                        heard.insert(atoi(note.c_str()));
                }

                if (heard.count(45) != (cases[c].wantRoot ? 1u : 0u))
                    fail(cases[c].what);
                else if (cases[c].wantOther >= 0 &&
                         !heard.count(cases[c].wantOther))
                    fail(cases[c].what);
                else if (heard.size() != cases[c].exact)
                    fail(std::string(cases[c].what) + " (heard " +
                         std::to_string(heard.size()) + " distinct "
                         "pitches, wanted " +
                         std::to_string(cases[c].exact) + ")");
            }

            remove(path.c_str());
        }
    }

    clearChannels(synth);
    drainSynth();

    /* ---- 5b. every on gets exactly one off ---- */

    /* The half of a transformer that no rendered tape can see.
     *
     * A generator's notes carry their own duration, so the scheduler
     * derives each off from the event it delivered and a transformer
     * that never thinks about offs at all still works. Live input does
     * not: a key arrives as a THC_EV_NOTE with duration 0 and a
     * THC_EV_NOTEOFF whenever the hand lets go, and a transformer that
     * turned one note into five owes five offs, at the right pitches
     * and at the right times. Three ways to get that wrong, all of
     * which this plugin did, all of which leave a note ringing until
     * the program is closed:
     *
     *   - a pitch pressed twice before either release;
     *   - `spread', where each voice has to be released as late as it
     *     was pressed, or the offs arrive before their own ons;
     *   - a press that emitted nothing, whose release must emit nothing
     *     rather than forwarding the root it deliberately dropped.
     *
     * So this drives the chain by hand and counts. Pitch by pitch, and
     * with the times compared, because "the same number of ons and
     * offs" is a gate that a chord released at the wrong pitches walks
     * straight through.
     */
    {
        struct Case
        {
            const char *what;
            const char *stage;
            bool        twice;      /* press it again before releasing */
        };

        static const Case cases[] = {
            { "a held chord",
              "stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
              " voices = 3; step = 2; };\n", false },
            { "a held chord pressed twice",
              "stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
              " voices = 3; step = 2; };\n", true },
            { "a rolled chord released early",
              "stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
              " voices = 3; step = 2; spread = 0.5 s; };\n", false },
            { "a chord whose root was dropped",
              "stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
              " voices = 1; root = 0; };\n", false },
        };

        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
        {
            char body[1024];

            snprintf(body, sizeof(body),
                     "chain hands {\n"
                     "  input midi;\n"
                     "  %s"
                     "  sink { channel = 1; };\n"
                     "};\n", cases[c].stage);

            const std::string path = thUtil::tempFile("gencheck-held-");

            if (path.empty())
            {
                fail("could not write the held-note piece");
                continue;
            }

            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << body;
            }

            clearChannels(synth);
            drainSynth();

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            if (!loader.load(path, &sched))
                fail(std::string("the held-note piece did not load: ") +
                     cases[c].what);
            else
            {
                /* pitch -> (ons, offs), and the time of each. */
                std::map<int, std::vector<double> > ons, offs;

                sigc::connection conn = sched.sigDelivered.connect(
                    [&ons, &offs](const thcEvent &ev)
                    {
                        if (ev.type == THC_EV_NOTE)
                            ons[ev.u.note.note].push_back(ev.at);
                        else if (ev.type == THC_EV_NOTEOFF)
                            offs[ev.u.note.note].push_back(ev.at);
                    });

                sched.start();

                thcEvent key = {};

                key.type = THC_EV_NOTE;
                key.channel = 0;
                key.u.note.note = 45;
                key.u.note.velocity = 90;
                key.u.note.duration = 0;
                key.at = sched.now();

                sched.injectMidiEvent(key);

                if (cases[c].twice)
                {
                    sched.stepTransport(0.1);

                    key.at = sched.now();
                    sched.injectMidiEvent(key);
                }

                /* Let go well inside the roll, which is what makes the
                   spread case a question at all. */
                sched.stepTransport(0.15);

                thcEvent up = {};

                up.type = THC_EV_NOTEOFF;
                up.channel = 0;
                up.u.note.note = 45;
                up.at = sched.now();

                sched.injectMidiEvent(up);

                if (cases[c].twice)
                    sched.injectMidiEvent(up);

                /* Long enough for a 0.5s roll's last voice and its off. */
                for (int i = 0; i < 200; i++)
                    sched.stepTransport(0.02);

                conn.disconnect();

                for (std::map<int, std::vector<double> >::iterator
                         i = ons.begin(); i != ons.end(); ++i)
                {
                    const size_t up2 = i->second.size();
                    const size_t down = offs.count(i->first)
                        ? offs[i->first].size() : 0;

                    if (up2 != down)
                    {
                        fail(std::string(cases[c].what) + ": pitch " +
                             std::to_string(i->first) + " sounded " +
                             std::to_string(up2) + " times and was "
                             "released " + std::to_string(down));
                        break;
                    }

                    /* And each release after the press it answers. An
                       off ahead of its own on is a note that never
                       stops. */
                    bool bad = false;

                    for (size_t k = 0; k < up2; k++)
                        if (offs[i->first][k] < i->second[k])
                            bad = true;

                    if (bad)
                    {
                        fail(std::string(cases[c].what) + ": pitch " +
                             std::to_string(i->first) + " was released "
                             "before it sounded");
                        break;
                    }
                }

                /* And nothing released that never sounded. */
                for (std::map<int, std::vector<double> >::iterator
                         i = offs.begin(); i != offs.end(); ++i)
                    if (!ons.count(i->first))
                    {
                        fail(std::string(cases[c].what) + ": pitch " +
                             std::to_string(i->first) + " was released "
                             "but never sounded");
                        break;
                    }
            }

            remove(path.c_str());
        }
    }

    clearChannels(synth);
    drainSynth();

    /* ---- 6. and the whole thing replays ---- */

    {
        clearChannels(synth);

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(piece, &sched))
            fail("colony.gen did not load for the replay check");
        else
        {
            const std::string first = render(sched, 100.0, 0.04);

            sched.reset();

            const std::string second = render(sched, 100.0, 0.04);

            if (first.empty())
                fail("colony.gen delivered nothing");
            else if (first != second)
                fail("colony.gen did not replay; a learner or a board is "
                     "carrying state across a rewind");
        }
    }

    clearChannels(synth);
}

/* ---- 8. every shipped piece still loads -------------------------------- */

/* The corpus instinct, applied to .gen.
 *
 * Everything above builds its own files or leans on the one piece passed
 * in, so the other shipped pieces were gated by nothing at all: a param
 * renamed in a plugin, a unit tightened in the loader, a knob whose
 * metadata stopped being accepted, and fern.gen or loom.gen would have
 * quietly stopped loading with no test anywhere to say so. dspcheck has
 * swept dsp/ for exactly this reason since long before any of this.
 *
 * Loading is most of it, and one thing more: a piece that loads and then
 * says nothing is a piece with a typo in it, and the demos are meant to
 * be read as much as heard. So every piece with a generator in it has to
 * deliver something inside a minute of its own virtual time. A piece
 * whose chains are all `input midi' is exempt, because silence is
 * exactly what it should produce with nobody playing -- hands.gen is
 * that piece, and the exemption is why the check can be strict about
 * everything else.
 *
 * Replay determinism is not swept here: it needs a pinned seed and three
 * minutes, and one piece carrying that is enough.
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
        const std::string leaf = files[i].filename().string();

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(files[i].string(), &sched))
        {
            for (size_t k = 0; k < loader.errors().size(); k++)
                fprintf(stderr, "gencheck: %s\n",
                        loader.errors()[k].c_str());

            fail(leaf + " no longer loads");
            continue;
        }

        if (sched.chainCount() == 0)
        {
            fail(leaf + " loaded with no chains at all");
            continue;
        }

        bool anyGenerator = false;

        for (size_t ci = 0; ci < sched.chainCount(); ci++)
        {
            const thcChain *c = sched.chain(ci);

            if (c != NULL && !c->inputMidi)
                anyGenerator = true;
        }

        if (!anyGenerator)
            continue;           /* played by hand; see above            */

        if (render(sched, 60.0, 0.05).empty())
            fail(leaf + " loads but delivers nothing in a minute");
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

    /* Built with the plugin path rather than bare, because a piece can
       now carry its own instrument and "the instrument loaded" means a
       .dsp parsed and dlopen'd its nodes. A synth with nowhere to find
       them would make checkInstruments pass by failing to try. */
    thSynth synth(pluginDir, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    tapeSynth = &synth;

    checkValidation(plugins, &synth);
    checkReplay(plugins, &synth, genFile);
    checkPlanners(plugins, &synth);
    checkLiveInput(plugins, &synth);
    checkEdits(plugins, &synth, genFile);
    checkPresets(plugins, &synth);
    checkInput(plugins, &synth);
    checkTempoAndRevival(plugins, &synth);
    checkInstruments(plugins, &synth);
    checkNodes(plugins, &synth, genFile);
    checkStructureEdits(plugins, &synth, genFile);
    checkColony(plugins, &synth, genFile);
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
