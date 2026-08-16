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
    if (plugins.find("lsystem") == plugins.end() ||
        plugins.find("evolve") == plugins.end())
    {
        fail("lsystem/evolve modules missing; build the plugins first");
        return;
    }

    std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "gencheck-planners.gen";

    {
        std::ofstream out(tmp);

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
            "};\n";
    }

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(tmp.string(), &sched))
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

    std::filesystem::remove(tmp);
}

/* ---- 5. the editor's splices ------------------------------------------ */

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

    std::filesystem::remove(path);
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
    checkEdits(plugins, &synth, genFile);

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
