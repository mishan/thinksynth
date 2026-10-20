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
 */

/*
 * parsecheck -- the .gen and .dsp readers, on texts nobody wrote.
 *
 *     scripts/parsecheck -p PLUGINDIR [-n ITERATIONS] [-s SEED]
 *                        GENDIR DSPDIR SCRATCHDIR
 *
 * A room takes both from its peers: a piece and its edits as .gen, an
 * instrument as .dsp. Here every shipped file is changed a few times over
 * -- bytes, spans cut or doubled, a token or an extreme number put in --
 * and taken the way a room takes it:
 *
 *   a .dsp through thDspCheck, then loaded as a channel and played;
 *   a .gen loaded and played, or sent as an edit to the piece it came
 *   from while that plays.
 *
 * A text that is refused has to say why. Under the sanitizers (the asan
 * job) this is also where a read past a buffer shows. The case being run
 * is SCRATCHDIR/case.gen or case.dsp, so a crash or a hang leaves it there,
 * and SCRATCHDIR/case.txt says how to run it again (-r, and -e for an
 * edit).
 *
 * Exit status is 0 when nothing failed.
 */

#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "think.h"

#include "libthink/thDynLib.h"
#include "libthink/thDspParser.h"
#include "libthink/thLexer.h"
#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"
#include "thcGenDiff.h"

namespace fs = std::filesystem;

static int failures = 0;

static void
fail (const std::string &what)
{
    printf("FAIL  %s\n", what.c_str());
    failures++;
}

static uint64_t state = 0x9e3779b97f4a7c15ull;

static uint64_t
next (void)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

static std::string
slurp (const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;

    s << in.rdbuf();
    return s.str();
}

static bool
spill (const fs::path &path, const std::string &text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);

    out << text;
    return out.good();
}

/* What either language is made of; then numbers at the edges of what a
   control or a duration can hold. */
static const char *const TOKENS[] = {
    "{", "}", "(", ")", "[", "]", ";", ",", "=", "->", "@", "\"", ".",
    "/", "*", "+", "-", "/*", "//", "\n", " ", "0.5 beats", "0 ms", "node",
    "io", "name", "chain", "section", "knob", "loop", "dsp", "tempo",
    "C4", "x", "_", "#",
};

static const char *const NUMBERS[] = {
    "0", "-1", "1e38", "-1e38", "1e-38", "nan", "inf", "999999999",
};

/* A few changes to `text'. */
static std::string
mutate (std::string text)
{
    const int edits = 1 + (int)(next() % 4);

    for (int e = 0; e < edits && !text.empty(); e++)
    {
        const size_t at = next() % text.size();
        const size_t len = std::min<size_t>(1 + next() % 24,
                                            text.size() - at);

        switch (next() % 6)
        {
        case 0:
            text[at] = (char)(next() % 256);
            break;
        case 1:
            text.erase(at, len);
            break;
        case 2:
            text.insert(at, text.substr(at, len));
            break;
        case 3:
            text.insert(at, TOKENS[next() % (sizeof(TOKENS) /
                                             sizeof(TOKENS[0]))]);
            break;
        case 4:
        {
            /* A number, wherever one starts, for another. */
            size_t d = text.find_first_of("0123456789", at);

            if (d == std::string::npos)
                break;

            const size_t end = text.find_first_not_of("0123456789.", d);

            text.replace(d, (end == std::string::npos ? text.size() : end) -
                         d, NUMBERS[next() % 8]);
            break;
        }
        case 5:
            text.resize(at);
            break;
        }
    }

    return text;
}

/* As gencheck loads them, and pinned for its reason: see loadComposers
   there. */
static void
loadComposers (const std::string &pluginDir,
               std::map<std::string, thcPlugin *> &out)
{
    std::error_code ec;

    for (const auto &f :
         fs::directory_iterator(fs::path(pluginDir) / "composer", ec))
    {
        if (f.path().extension() != PLUGIN_SUFFIX)
            continue;

        thcPlugin *p = new thcPlugin(f.path().string());

        if (p->state() != thcPlugin::LOADED)
        {
            delete p;
            continue;
        }

        out[p->name()] = p;
        thDynLib::open(f.path().string());
    }
}

/* Each case on a synth of its own, so -r runs it as it ran here. */
struct Run
{
    std::map<std::string, thcPlugin *> plugins;
    std::string pluginDir;
    thSynth *synth;
    fs::path scratch;
};

/* Play `sched' to `seconds', emptying the synth's ring as the room's
   audio thread would. */
static void
play (Run &run, thcScheduler &sched, double seconds)
{
    sched.start();

    for (int step = 0; sched.now() < seconds && sched.running(); step++)
    {
        sched.stepTransport(0.02);

        if (step % 4 == 0)
            run.synth->process();
    }
}

static void
dspCase (Run &run, const std::string &text, const std::string &label)
{
    thSynth synth(run.pluginDir, TH_DEFAULT_WINDOW_LENGTH,
                  TH_DEFAULT_SAMPLES);

    run.synth = &synth;

    std::vector<thLexToken> tokens;
    bool said = false;

    thLexString(text, tokens);

    if (!thDspCheck(tokens, [&said] (int, const std::string &) { said = true; })
        && !said)
        fail(label + ": thDspCheck refused it without a reason");

    const fs::path file = run.scratch / "case.dsp";

    if (!spill(file, text))
    {
        fail(label + ": could not write " + file.string());
        return;
    }

    if (run.synth->loadTree(file.string(), 0, 100) != NULL)
    {
        run.synth->addNote(0, 60, 100, 1);

        for (int i = 0; i < 4; i++)
            run.synth->process();
    }
}

/* Loaded as a piece, or with `from' the piece it came from, sent to that
   as an edit while it plays. */
static void
genCase (Run &run, const fs::path &from, const std::string &text,
         const std::string &label)
{
    thSynth synth(run.pluginDir, TH_DEFAULT_WINDOW_LENGTH,
                  TH_DEFAULT_SAMPLES);

    run.synth = &synth;

    const fs::path file = run.scratch / "case.gen";

    if (!spill(file, text))
    {
        fail(label + ": could not write " + file.string());
        return;
    }

    thcScheduler sched(run.synth);

    sched.setAuditionSynchronous(true);

    /* A piece without a seed line would otherwise take a random one. */
    sched.setMasterSeed(1);

    std::vector<std::string> errors;
    bool ok;

    if (from.empty())
    {
        thcGenLoader loader(run.plugins);

        ok = loader.load(file.string(), &sched);
        errors = loader.errors();
    }
    else
    {
        thcGenLoader loader(run.plugins);

        if (!loader.load(from.string(), &sched))
            return;

        play(run, sched, 0.5);
        ok = thcGenDiff::apply(sched, run.plugins, from.string(),
                               file.string(), std::set<std::string>(),
                               errors);
    }

    if (!ok && errors.empty())
        fail(label + ": refused without a reason");

    if (ok)
        play(run, sched, sched.now() + 1);

    sched.stop();
    run.synth->process();
}

/* How to run case `i' again, in case it is the one that does not come
   back. */
static void
note (Run &run, int i, const std::string &how, const fs::path &seed)
{
    spill(run.scratch / "case.txt",
          "case " + std::to_string(i) + " from " + seed.string() +
          "\nparsecheck -p PLUGINDIR " + how + "\n");
}

/* What the room takes from a peer. RunHarness's own reasons for leaving
   a graph out apply: a device, a network, a test node. */
static std::vector<fs::path>
seeds (const fs::path &dir, const char *ext)
{
    std::vector<fs::path> out;
    std::error_code ec;

    for (const auto &f : fs::directory_iterator(dir, ec))
    {
        if (f.path().extension() != ext)
            continue;

        const std::string text = slurp(f.path());

        if (text.find("input::") != std::string::npos ||
            text.find("misc::wlan") != std::string::npos ||
            text.find("fft::") != std::string::npos ||
            text.find("test::") != std::string::npos)
            continue;

        out.push_back(f.path());
    }

    std::sort(out.begin(), out.end());
    return out;
}

int
main (int argc, char *argv[])
{
    std::string pluginDir;
    std::vector<std::string> dirs;
    std::string replay, from;
    int iterations = 600;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            pluginDir = argv[++i];
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            iterations = atoi(argv[++i]);
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
            state ^= strtoull(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc)
            replay = argv[++i];
        else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc)
            from = argv[++i];
        else
            dirs.push_back(argv[i]);
    }

    if (pluginDir.empty() || (replay.empty() && dirs.size() != 3))
    {
        fprintf(stderr, "usage: parsecheck -p PLUGINDIR [-n ITERATIONS] "
                "[-s SEED] GENDIR DSPDIR SCRATCHDIR\n"
                "       parsecheck -p PLUGINDIR -r CASE [-e FROM]\n");
        return 2;
    }

    if (!replay.empty())
    {
        Run run;

        run.pluginDir = pluginDir;
        run.scratch = fs::path(replay).parent_path() / "replay";
        fs::create_directories(run.scratch);
        loadComposers(pluginDir, run.plugins);

        if (fs::path(replay).extension() == ".dsp")
            dspCase(run, slurp(replay), replay);
        else
            genCase(run, from, slurp(replay), replay);

        for (auto &p : run.plugins)
            delete p.second;

        return failures == 0 ? 0 : 1;
    }

    const std::vector<fs::path> gens = seeds(dirs[0], ".gen");
    const std::vector<fs::path> dsps = seeds(dirs[1], ".dsp");
    std::error_code ec;

    fs::create_directories(dirs[2], ec);

    if (gens.empty() || dsps.empty())
    {
        fprintf(stderr, "parsecheck: nothing to change in %s or %s\n",
                dirs[0].c_str(), dirs[1].c_str());
        return 2;
    }

    Run run;

    run.pluginDir = pluginDir;
    run.scratch = dirs[2];
    loadComposers(pluginDir, run.plugins);

    const int before = failures;

    for (int i = 0; i < iterations && failures - before < 10; i++)
    {
        if (next() % 3 == 0)
        {
            const fs::path &seed = dsps[next() % dsps.size()];

            note(run, i, "-r case.dsp", seed);
            dspCase(run, mutate(slurp(seed)),
                    "case " + std::to_string(i) + " from " +
                    seed.filename().string());
        }
        else
        {
            const fs::path &seed = gens[next() % gens.size()];
            const bool edit = next() % 2;

            note(run, i, edit ? "-r case.gen -e " + seed.string()
                              : std::string("-r case.gen"), seed);
            genCase(run, edit ? seed : fs::path(), mutate(slurp(seed)),
                    "case " + std::to_string(i) + " from " +
                    seed.filename().string());
        }
    }

    for (auto &p : run.plugins)
        delete p.second;

    if (failures == 0)
        printf("ok    %d changed texts through the room's readers\n"
               "parsecheck: OK\n", iterations);
    else
        printf("parsecheck: %d failure%s\n", failures,
               failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
