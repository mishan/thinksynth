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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * dspstress -- concurrency harness for thSynth.
 *
 * dspcheck is single-threaded, so it cannot see any of the audio-thread /
 * GUI-thread races. This runs a synthetic audio thread calling thSynth::process
 * in a loop while the main thread does what the GUI thread does -- notes,
 * parameter changes, patch reloads -- and lets ThreadSanitizer watch.
 *
 * That mirrors the real thread split. There are only two threads in thinksynth:
 * JACK's RT callback (or the ALSA polling thread), and everything else. MIDI
 * events arrive through Glib::signal_io, so they land on the GUI thread along
 * with the keyboard, the sliders and the patch manager.
 *
 * Build (libthink and the plugins must be instrumented too -- and note that
 * ThreadSanitizer and AddressSanitizer cannot be combined, so this needs its
 * own build tree):
 *
 *   cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DTHINK_SANITIZE=thread
 *   cmake --build build-tsan
 *   build-tsan/scripts/dspstress -p build-tsan/plugins/ dsp/ts1.dsp
 *
 * Work is split into levels so a report can be attributed to one kind of
 * operation rather than to "something concurrent". Each level runs in a forked
 * child, so a level that segfaults does not stop the ones after it:
 *
 *   1 notes     addNote / delNote
 *   2 clear     + clearAll
 *   3 chanargs  + setChanArg and slider-style setValue
 *   4 reload    + loadTree onto a live channel, removeChan
 *   5 probes    + armProbe / disarmProbe, and draining the rings
 *   6 parse     + four threads parsing the same .dsp over and over
 *
 * Level 5 is where the visualizer tap gets looked at, and it deliberately sits
 * on top of level 4 rather than beside it: arming and disarming on a quiet
 * synth proves very little, and the interesting collision is a probe being
 * armed or drained while the channel it points at is being replaced. Draining
 * is part of it because the ring's consumer side is the GUI thread's, and a
 * ring that is written but never read exercises half of what it is for.
 *
 * Level 6 is the one that checks a claim rather than a mechanism. thinklang
 * was made pure -- a scanner and a context per parse -- so that parseTree
 * could run without the synth mutex and a background evaluator could parse
 * candidate instruments while the live synth plays. Purity of the parser is
 * necessary and was not sufficient: a parse resolves its nodes' plugins
 * through thPluginManager, whose map is the synth's and not the parse's, and
 * whose lookup was `plugins_[name]' -- an insert on every miss, from a call
 * that reads. Two parses of a file naming a plugin neither had loaded raced
 * on that insert. This level is what says so, and what would say so again.
 *
 * Confirmed to fail before it was trusted to pass, both of them. Taking the
 * manager's lock back out makes level 6 report a data race inside the map,
 * from two of the four parser threads, within three seconds. And relaxing
 * thSampleRing's release store to memory_order_relaxed makes level 5 report
 * a data race at the memcpy in read() within a second and a half. That is the
 * edge the whole handoff rests on, and this is what watches it -- ringcheck
 * can only find an ordering bug by volume, on hardware that will not reorder
 * the stores itself.
 *
 * Exit status is the number of levels that failed (crashed, or reported a race
 * when TSAN_OPTIONS sets exitcode/halt_on_error).
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "think.h"

/* Picked up automatically by ThreadSanitizer, so a race fails the run without
   the caller having to remember the environment variable. halt_on_error=0 so a
   level reports everything it finds rather than stopping at the first race;
   exitcode=66 is what the parent below looks for. */
extern "C" const char *__tsan_default_options (void)
{
    return "halt_on_error=0 exitcode=66 history_size=7";
}

enum {
    LVL_NOTES = 1,
    LVL_CLEAR,
    LVL_CHANARG,
    LVL_RELOAD,
    LVL_PROBE,
    LVL_PARSE,
    LVL_MAX = LVL_PARSE
};

static const char *levelName (int level)
{
    switch (level)
    {
        case LVL_NOTES:   return "notes    (addNote/delNote)";
        case LVL_CLEAR:   return "clear    (+ clearAll)";
        case LVL_CHANARG: return "chanargs (+ setChanArg/setValue)";
        case LVL_RELOAD:  return "reload   (+ loadTree/removeChan)";
        case LVL_PROBE:   return "probes   (+ arm/disarm/drain)";
        case LVL_PARSE:   return "parse    (+ concurrent parseTree)";
        default:          return "?";
    }
}

/* Deterministic so a failing run can be repeated. */
static unsigned int rngState = 0x1234567u;

static unsigned int nextRand (void)
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

struct StressCounters {
    std::atomic<unsigned long> windows;
    std::atomic<unsigned long> ops;
    std::atomic<unsigned long> parses;

    StressCounters () : windows(0), ops(0), parses(0) {}
};

/* Stands in for the JACK process callback. */
static void audioThread (thSynth *synth, std::atomic<bool> *running,
                         StressCounters *counters)
{
    while (running->load(std::memory_order_relaxed))
    {
        synth->process();
        counters->windows.fetch_add(1, std::memory_order_relaxed);

        /* A real callback is woken by the audio device rather than spinning;
           yielding here widens the interleaving rather than starving the
           other thread on a single core. */
        std::this_thread::yield();
    }
}

/* Stands in for whatever wants a tree without wanting the synth: the node
 * editor's own parse, a patch being previewed, and -- the reason the parser
 * was made pure -- a background evaluator scoring candidate instruments
 * while the live one plays.
 *
 * parseTree registers nothing, so the tree is this thread's to delete. The
 * deletion is the point as much as the parse is: it is what makes a leak or
 * a double free show up here rather than as growth in a long session. */
static void parserThread (thSynth *synth, const char *file,
                          std::atomic<bool> *running, StressCounters *counters)
{
    while (running->load(std::memory_order_relaxed))
    {
        thSynthTree *tree = synth->parseTree(file);

        if (tree == NULL)
            continue;

        delete tree;

        counters->parses.fetch_add(1, std::memory_order_relaxed);

        std::this_thread::yield();
    }
}

struct ProbePoint { string node; string arg; };

/* Every arg a plugin declared as an output, off an unowned parse.
 *
 * Read straight from the plugin tables rather than through NodeGraph: this
 * harness is not linked against the node model, and what is wanted here is
 * somewhere legitimate to point a probe, not the editor's opinion of which
 * ports are worth showing. */
static void collectProbePoints (thSynth &synth, const char *file,
                                vector<ProbePoint> &out)
{
    thSynthTree *tree = synth.parseTree(file);

    if (tree == NULL)
        return;

    const thSynthTree::NodeMap &nodes = tree->nodes();

    for (thSynthTree::NodeMap::const_iterator i = nodes.begin();
         i != nodes.end(); ++i)
    {
        thNode *n = i->second;

        if (n == NULL || n->plugin() == NULL)
            continue;

        for (int k = 0; k < n->plugin()->argCount(); k++)
        {
            if (n->plugin()->getArgDir(k) != thPlugin::ARG_OUT)
                continue;

            ProbePoint p;

            p.node = n->name();
            p.arg = n->plugin()->getArgName(k);

            out.push_back(p);
        }
    }

    delete tree;
}

/* Runs one level to completion. Called in a forked child. */
static int runLevel (const string &pluginPath, const char *file, int level,
                     int milliseconds)
{
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    if (synth.loadTree(file, 0, 100) == NULL)
    {
        fprintf(stderr, "dspstress: could not load %s\n", file);
        return 2;
    }

    /* A second channel so the reload level has something to swap against
       without the first channel going quiet. */
    synth.loadTree(file, 1, 100);

    vector<ProbePoint> points;
    vector<float> drain;

    if (level >= LVL_PROBE)
    {
        collectProbePoints(synth, file, points);
        drain.resize(synth.getWindowlen() * 2);

        if (points.empty())
        {
            fprintf(stderr, "dspstress: %s declares no output ports to probe\n",
                    file);
            return 2;
        }
    }

    StressCounters counters;
    std::atomic<bool> running(true);

    std::thread audio(audioThread, &synth, &running, &counters);

    /* Four rather than two: the race this level exists for is on an insert,
       and an insert is over quickly. More threads than cores is deliberate
       -- the scheduler's preemptions are half of what widens the window on a
       machine that is otherwise too fast to lose. */
    vector<std::thread> parsers;

    if (level >= LVL_PARSE)
        for (int i = 0; i < 4; i++)
            parsers.push_back(std::thread(parserThread, &synth, file, &running,
                                          &counters));

    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(milliseconds);

    while (std::chrono::steady_clock::now() < deadline)
    {
        unsigned int r = nextRand();
        int chan = (int)(r & 1);
        int note = 40 + (int)((r >> 1) % 40);

        /* Weighted towards notes: that is what actually happens most, and the
           rarer operations still come round often in a few seconds. */
        unsigned int pick = (r >> 8) % 100;

        /* Probe work runs alongside the pick below rather than as another
           slice of it, for two reasons. Taking a share would thin out the
           reloads, and reloading while a probe is armed is the collision worth
           having. And draining wants to happen constantly -- a ring that is
           read once in a hundred windows spends its life full, so the wrap
           that the release/acquire pair exists to protect never happens.
         *
         * The drain is not decoration. Between the audio thread's write and
         * this read there is nothing but thSampleRing's release/acquire pair,
         * and the whole reason a probe hands over a window at a time instead
         * of a pointer is that this read has to be safe while the callback is
         * inside the same buffer. */
        if (level >= LVL_PROBE)
        {
            for (int s = 0; s < synth.probeCount(); s++)
            {
                thProbe *probe = synth.probe(s);

                if (probe)
                    probe->read(&drain[0], (unsigned int)drain.size());
            }

            if ((pick & 7) == 0)
            {
                const ProbePoint &pt = points[(r >> 20) % points.size()];

                synth.armProbe(chan, pt.node, pt.arg);
            }

            if ((pick & 15) == 3)
                synth.disarmProbe((int)((r >> 24) % TH_MAX_PROBES));
        }

        if (pick < 55)
        {
            synth.addNote(chan, (float)note, 100);
        }
        else if (pick < 85)
        {
            synth.delNote(chan, (float)note);
        }
        else if (level >= LVL_CLEAR && pick < 88)
        {
            synth.clearAll();
        }
        else if (level >= LVL_CHANARG && pick < 94)
        {
            /* A slider drag: the GUI writes an arg the graph is reading. */
            thArg *amp = synth.getChanArg(chan, "amp");

            if (amp)
                amp->setValue((float)((r >> 16) % 128));
        }
        else if (level >= LVL_CHANARG && pick < 96)
        {
            /* Replacing an arg outright, as loading a patch does. */
            synth.setChanArg(chan, new thArg(string("amp"),
                                             (float)((r >> 16) % 128)));
        }
        else if (level >= LVL_RELOAD && pick < 99)
        {
            /* Patch switch on a channel the audio thread is inside. */
            synth.loadTree(file, chan, 100);
        }
        else if (level >= LVL_RELOAD)
        {
            synth.removeChan(chan);
            synth.loadTree(file, chan, 100);
        }
        else
        {
            continue;
        }

        counters.ops.fetch_add(1, std::memory_order_relaxed);
    }

    running.store(false, std::memory_order_relaxed);
    audio.join();

    for (size_t i = 0; i < parsers.size(); i++)
        parsers[i].join();

    if (parsers.empty())
        printf("      %lu windows, %lu ops\n",
               counters.windows.load(), counters.ops.load());
    else
        printf("      %lu windows, %lu ops, %lu parses\n",
               counters.windows.load(), counters.ops.load(),
               counters.parses.load());

    return 0;
}

static void usage (const char *argv0)
{
    /* PLUGIN_PATH is an argument rather than part of the format. It is a
       compile-time install path, so it is data: a prefix with a `%' in it
       would otherwise be read as a conversion and eat an argument that was
       never passed. */
    printf("usage: %s [-p PATH] [-t MS] [-l LEVEL] [-s SEED] file.dsp\n"
           "\n"
           "  -p, --plugin-path PATH  where to find plugin .so files\n"
           "                          (default: %s)\n"
           "  -t, --time MS           milliseconds per level (default 2000)\n"
           "  -l, --level N           run only level N (default: 1..%d)\n"
           "  -s, --seed N            RNG seed (default 0x1234567)\n",
           argv0, PLUGIN_PATH, LVL_MAX);
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;
    int milliseconds = 2000;
    int onlyLevel = 0;
    const char *file = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--plugin-path"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            pluginPath = argv[i];
        }
        else if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--time"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            milliseconds = atoi(argv[i]);
        }
        else if (!strcmp(argv[i], "-l") || !strcmp(argv[i], "--level"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            onlyLevel = atoi(argv[i]);
        }
        else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--seed"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            rngState = (unsigned int)strtoul(argv[i], NULL, 0);
        }
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
        {
            usage(argv[0]);
            return 0;
        }
        else
        {
            file = argv[i];
            break;
        }
    }

    if (file == NULL)
    {
        usage(argv[0]);
        return 2;
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    int first = (onlyLevel > 0) ? onlyLevel : 1;
    int last = (onlyLevel > 0) ? onlyLevel : LVL_MAX;
    int failed = 0;

    for (int level = first; level <= last; level++)
    {
        printf("level %d %-34s ", level, levelName(level));
        fflush(stdout);

        /* Fork per level: level 4 against the unfixed code tends to segfault
           outright, and that should not stop the earlier levels from being
           reported. */
        pid_t pid = fork();

        if (pid < 0)
        {
            perror("fork");
            return 2;
        }

        if (pid == 0)
        {
            printf("\n");
            fflush(stdout);

            /* Watchdog. A level can hang rather than finish: the unfixed code
               corrupts containers badly enough to spin, and ThreadSanitizer's
               own DEADLYSIGNAL handler can wedge after a SEGV. Without this the
               parent waits forever. */
            alarm((unsigned int)(milliseconds / 1000) + 20);

            _exit(runLevel(pluginPath, file, level, milliseconds));
        }

        int status = 0;
        waitpid(pid, &status, 0);

        if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM)
        {
            printf("      HUNG (watchdog fired)\n");
            failed++;
        }
        else if (WIFSIGNALED(status))
        {
            printf("      CRASHED (signal %d)\n", WTERMSIG(status));
            failed++;
        }
        else if (WEXITSTATUS(status) == 66)
        {
            /* See __tsan_default_options above. */
            printf("      RACES REPORTED (see output above)\n");
            failed++;
        }
        else if (WEXITSTATUS(status) != 0)
        {
            printf("      FAILED (exit %d)\n", WEXITSTATUS(status));
            failed++;
        }
        else
        {
            printf("      ok\n");
        }
    }

    printf("\n%d/%d levels clean\n", (last - first + 1) - failed,
           last - first + 1);

    return failed;
}
