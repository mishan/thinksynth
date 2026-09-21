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
 * configcheck -- does a first run leave someone with a synth that makes a
 *                sound, and a config file that survives being moved?
 *
 * thinksynth used to ship etc/thinkrc, configure_file'd with the absolute
 * dsp and patch directories of whichever machine ran cmake. That is correct
 * on exactly one kind of installation and wrong on every relocatable one: a
 * tarball unpacked somewhere else, a .app, a Windows zip, a Flatpak. The
 * defaults are built into the binary now, named relatively, and resolved
 * through the same search the rest of the program uses.
 *
 * Which moves the risk rather than removing it, so this covers where it went:
 *
 *   - the default patches resolve, and to something that opens;
 *   - they still resolve after the working directory has moved, which is the
 *     bug pathcheck exists for, in the one resolver it did not cover;
 *   - a first run with an empty config directory populates channels and
 *     writes a file;
 *   - that file names its patches *relatively*. This is the property the
 *     whole change is for, and the easiest one to lose by accident: have
 *     Save() write patch->filename after something has helpfully replaced it
 *     with a resolved absolute path, and every generated config goes back to
 *     being valid only on the machine that wrote it. Nothing else would
 *     notice, because it keeps working until the install moves.
 *   - reading that file back produces the same channels, and does not run the
 *     defaults a second time.
 *
 * No display and no audio device. Needs the plugins, because a channel is
 * only really loaded if its DSP parsed.
 */

#include "config.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "think.h"
#include "thUtil.h"

#include "gthPrefs.h"
#include "gthPatchfile.h"

namespace fs = std::filesystem;

static int failures = 0;

static void ok (bool cond, const char *fmt, ...)
{
    va_list ap;
    char what[512];

    va_start(ap, fmt);
    vsnprintf(what, sizeof(what), fmt, ap);
    va_end(ap);

    if (cond)
    {
        printf("ok    %s\n", what);
        return;
    }

    printf("FAIL  %s\n", what);
    failures++;
}

/* The same list gthPrefs builds a first run from. Spelled out again on
   purpose: a test that imported the table would agree with it however it
   changed, including into something empty. */
static const char *expected[] = {
    "leads/SuperRes.patch",
    "bass/FunkMachine.patch",
    "organs/Organ1.patch",
    "pads/SynString.patch",
};

static const size_t expectedCount = sizeof(expected) / sizeof(expected[0]);

/* Every line of a file, in order. The effect check below cares which line
   comes before which, not only that both are there. */
static std::vector<string> fileLines (const string &path)
{
    std::vector<string> out;
    std::ifstream in(path.c_str());
    string line;

    while (std::getline(in, line))
        out.push_back(line);

    return out;
}

static std::vector<string> channelLines (const string &path)
{
    std::vector<string> out;
    std::ifstream in(path.c_str());
    string line;

    while (std::getline(in, line))
    {
        if (line.compare(0, 8, "channel ") == 0)
            out.push_back(line);
    }

    return out;
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;

    for (int i = 1; i < argc; i++)
    {
        if ((!strcmp(argv[i], "-p") || !strcmp(argv[i], "--plugin-path")) &&
            i + 1 < argc)
            pluginPath = argv[++i];
    }

    /* A config directory of our own, so this neither reads nor writes the
       preferences of whoever is running it. HOME goes with it: Load() checks
       the pre-XDG ~/.thinkrc before deciding a run is a first run, and a
       developer with one would otherwise see this behave differently from
       CI. */
    const string tmp = thUtil::tempFile("configcheck");

    if (tmp.empty())
    {
        printf("FAIL  could not make a temporary directory\n");
        return 1;
    }

    fs::remove(tmp);
    fs::create_directories(tmp);

#ifndef _WIN32
    setenv("HOME", tmp.c_str(), 1);
#endif

    const string cfg = (fs::path(tmp) / "thinkrc").string();

    /* ---- no synth yet ------------------------------------------------ */

    /* Before anything is constructed. loadPatch reaches
       gthPatchManager::parse, which calls thSynth::loadTree without checking
       it has one, so a first run that happened before the synth did would
       take the crash on the one run where the user has nothing to fall back
       to. Ordering makes that unreachable today; this is here so it stays
       unreachable. */
    {
        const string early = (fs::path(tmp) / "early-thinkrc").string();

        gthPrefs prefs(early);

        ok(!prefs.LoadDefaults(), "defaults decline to run with no synth");

        prefs.Load();

        ok(!fs::exists(early),
           "and a first run with no synth writes no configuration file");
    }

    thSynth *synth = new thSynth(pluginPath, TH_DEFAULT_WINDOW_LENGTH,
                                 TH_DEFAULT_SAMPLES);

    /* instance(), not `new'. gthPatchManager's constructor only claims the
       singleton slot if it is empty, and the block above already filled it:
       LoadDefaults asks for instance(), which creates one on demand. A second
       manager here would be a different object from the one Load() goes on to
       populate, and every later check would read an empty one. */
    gthPatchManager *patchMgr = gthPatchManager::instance();

    /* ---- resolution ------------------------------------------------- */

    /* Say where we are looking before saying what we could not find.
     *
     * Registered without THINK_PATCH_PATH this passed on a machine with a
     * stale /usr/local/share/thinksynth answering the compiled-in fallback,
     * and failed on every clean runner with fourteen failures that named the
     * patches and not the reason. One line up front is the difference between
     * "the corpus is missing" and "the defaults are wrong". */
    {
        const char *env = getenv("THINK_PATCH_PATH");

        printf("patches: THINK_PATCH_PATH=%s, fallback %s\n",
               (env && *env) ? env : "(unset)", PATCH_PATH);
    }

    for (size_t i = 0; i < expectedCount; i++)
    {
        const string got = gthPatchManager::resolvePatch(expected[i]);

        ok(fs::path(got).is_absolute(),
           "%s resolves to an absolute path", expected[i]);
        ok(fs::exists(got), "%s resolves to a file that exists", expected[i]);
    }

    /* The pathcheck lesson, in the resolver pathcheck did not cover: an
       answer that is only true from the directory it was asked in is not an
       answer. */
    {
        const string before = gthPatchManager::resolvePatch(expected[0]);

        const fs::path cwd = fs::current_path();
        fs::current_path(fs::temp_directory_path());

        const bool stillThere = fs::exists(before);

        fs::current_path(cwd);

        ok(stillThere, "a resolved patch path still opens from elsewhere");
    }

    /* ---- a first run ------------------------------------------------ */

    {
        gthPrefs prefs(cfg);

        prefs.Load();

        ok(fs::exists(cfg), "a first run writes a configuration file");

        size_t loaded = 0;

        for (size_t i = 0; i < expectedCount; i++)
        {
            gthPatchManager::PatchFile *p = patchMgr->getPatch((int)i);

            if (p != NULL && !p->doc.dsp.empty())
                loaded++;
        }

        ok(loaded == expectedCount,
           "a first run puts a patch on %zu channels (%zu did)",
           expectedCount, loaded);

        const std::vector<string> lines = channelLines(cfg);

        ok(lines.size() == expectedCount,
           "the file it wrote has %zu channel lines (%zu)",
           expectedCount, lines.size());

        /* The property the whole change exists for. */
        bool allRelative = !lines.empty();

        for (size_t i = 0; i < lines.size(); i++)
        {
            /* channel N,<path>,<amp> */
            const size_t a = lines[i].find(',');
            const size_t b = lines[i].rfind(',');

            if (a == string::npos || b == string::npos || b <= a)
            {
                allRelative = false;
                break;
            }

            const string named = lines[i].substr(a + 1, b - a - 1);

            if (fs::path(named).is_absolute())
            {
                printf("      %s\n", lines[i].c_str());
                allRelative = false;
            }
        }

        ok(allRelative,
           "every patch it wrote is named relatively, so the file travels");
    }

    /* ---- reading it back -------------------------------------------- */

    {
        /* Everything off the channels first, so "the file put them back" is
           a claim about the file rather than about what was already there. */
        for (size_t i = 0; i < expectedCount; i++)
            patchMgr->unloadPatch((int)i);

        gthPrefs prefs(cfg);

        prefs.Load();

        size_t loaded = 0;

        for (size_t i = 0; i < expectedCount; i++)
        {
            gthPatchManager::PatchFile *p = patchMgr->getPatch((int)i);

            if (p != NULL && !p->doc.dsp.empty())
                loaded++;
        }

        ok(loaded == expectedCount,
           "reading the file back restores %zu channels (%zu)",
           expectedCount, loaded);

        /* A second run is not a first run: the defaults must not be applied
           over the top of a configuration someone may have edited down. */
        const std::vector<string> lines = channelLines(cfg);

        ok(lines.size() == expectedCount,
           "the file still has %zu channel lines after a second load (%zu)",
           expectedCount, lines.size());
    }

    /* ---- a channel effect survives a .patch --------------------------- */

    /* The chooser in the patch page puts a graph on a channel's sum, and the
     * only record that it happened is the .patch. So: put one on, move one of
     * its parameters, write the file, take everything off, read it back, and
     * see both again.
     *
     * The ordering inside the file is the part that can quietly break. An
     * effect's parameters do not exist until the effect is on the channel, so
     * `effect' has to be written above them -- and a reader that tolerated
     * either order would hide a writer that had stopped doing it.
     */
    {
        const int chan = 0;

        patchMgr->unloadPatch(chan);

        const string dsp =
            thUtil::findDataFile("ts1.dsp", "dsp", "THINK_DSP_PATH", DSP_PATH);
        const string fx =
            thUtil::findDataFile("fx/echo.dsp", "dsp", "THINK_DSP_PATH",
                                 DSP_PATH);

        if (dsp.empty() || fx.empty())
        {
            ok(false, "the effect round trip can find ts1.dsp and "
                      "fx/echo.dsp");
        }
        else if (!patchMgr->newPatch(dsp, chan))
        {
            ok(false, "the effect round trip can load an instrument");
        }
        else
        {
            ok(patchMgr->setEffect(chan, fx),
               "an effect goes onto a channel that has a patch");

            thArg *mix = synth->getChanArg(chan, "fx.mix");

            ok(mix != NULL, "its parameters are reachable under `fx.'");

            if (mix != NULL)
                mix->setValue(0.75);

            const string file = tmp + "/effect.patch";

            ok(patchMgr->savePatch(file, chan), "the patch writes");

            /* The line, and where it is. */
            {
                const std::vector<string> lines = fileLines(file);
                long effectAt = -1, valueAt = -1;

                for (size_t i = 0; i < lines.size(); i++)
                {
                    if (lines[i].compare(0, 7, "effect ") == 0)
                        effectAt = (long)i;

                    if (lines[i].compare(0, 7, "fx.mix ") == 0)
                        valueAt = (long)i;
                }

                ok(effectAt >= 0, "the patch names its effect");
                ok(valueAt >= 0, "and carries the effect's values");
                ok(effectAt >= 0 && valueAt > effectAt,
                   "with the effect above them, which is the order that "
                   "loads");
            }

            patchMgr->unloadPatch(chan);

            ok(synth->getEffect(chan) == NULL,
               "unloading the patch took the effect with it");

            ok(patchMgr->loadPatch(file, chan), "the patch reads back");

            thArg *back = synth->getChanArg(chan, "fx.mix");

            ok(synth->getEffect(chan) != NULL,
               "and the effect is on the channel again");
            ok(back != NULL && fabs((*back)[0] - 0.75) < 1e-6,
               "with the value that was saved (%f)",
               back ? (double)(*back)[0] : -1.0);

            /* ---- and what a reload must not do ----------------------- */

            /* Asking for the effect that is already there is not a
             * request to build another one. A new thChanEffect is a new
             * delay line, and a new delay line is an empty one -- so a
             * piece reapplied because somebody renamed a knob would cut
             * the tail off every repeat. The object has to be the same
             * object. */
            {
                thChanEffect *was = synth->getEffect(chan);

                ok(patchMgr->setEffect(chan, fx),
                   "putting on the effect that is already there succeeds");
                ok(synth->getEffect(chan) == was,
                   "and leaves the graph that is running alone, so its "
                   "tail carries");
            }

            /* An instrument arriving on the channel takes the effect
             * with it, and the record of it has to go at the same time:
             * a patch saved after that would otherwise carry the
             * effect's values with no `effect' line to attach them to,
             * and the reader refuses those one by one. */
            {
                ok(patchMgr->newPatch(dsp, chan),
                   "an instrument loads over the patch that had an effect");

                const gthPatchManager::PatchFile *p = patchMgr->getPatch(chan);

                ok(synth->getEffect(chan) == NULL &&
                   p != NULL && p->doc.effect.empty(),
                   "which takes the effect off and forgets its name");

                const string orphan = tmp + "/orphan.patch";

                ok(patchMgr->savePatch(orphan, chan),
                   "the patch writes again");

                const std::vector<string> lines = fileLines(orphan);
                bool sawFx = false;

                for (size_t i = 0; i < lines.size(); i++)
                    if (lines[i].compare(0, 3, "fx.") == 0)
                        sawFx = true;

                ok(!sawFx, "with no effect values in it");
            }

            /* An effect the patch manager was never told about -- which
             * is what putting one on through the synth alone leaves
             * behind. Its values must not be written: there is no
             * `effect' line for them to belong to, so the reader has
             * nothing to look their names up in and refuses every one.
             * A patch that complains at itself on every load is worse
             * than a patch that forgot a delay. */
            {
                ok(synth->loadEffect(fx.c_str(), chan) != NULL,
                   "an effect goes on behind the patch manager's back");

                const string behind = tmp + "/behind.patch";

                ok(patchMgr->savePatch(behind, chan),
                   "and the patch still writes");

                const std::vector<string> lines = fileLines(behind);
                bool sawFx = false;

                for (size_t i = 0; i < lines.size(); i++)
                    if (lines[i].compare(0, 3, "fx.") == 0)
                        sawFx = true;

                ok(!sawFx,
                   "without values the file has no `effect' line to hang "
                   "them on");
            }

            /* Nothing on the channel at all: asking for no effect is
               already true of it, which is what every instrument that
               declares none asks for. */
            patchMgr->unloadPatch(chan);
            ok(patchMgr->setEffect(chan, ""),
               "and `no effect' on an empty channel is not a failure");

            /* A `side' naming the channel the patch is being read onto.
             * Nothing the program writes says that -- savePatch only ever
             * writes back a side the engine accepted -- but a hand-edited
             * file or one moved to another channel can, and loadEffect
             * answers a channel waiting on itself with NULL. Taken at its
             * word that would drop the effect out of a patch that is
             * otherwise fine, and the next save would write the file back
             * without it: a bad number on one line, and the delay is gone
             * from disk. The side is what is wrong, so the side is what is
             * dropped. */
            {
                patchMgr->unloadPatch(chan);

                const string self = tmp + "/selfside.patch";
                std::ofstream out(self.c_str(), std::ios::trunc);

                /* The order savePatch writes: the dsp, then the side, then
                   the effect the side belongs to. 1-based in the file, so
                   channel 0 names itself as 1. */
                out << "dsp " << dsp << "\n"
                    << "side " << (chan + 1) << "\n"
                    << "effect " << fx << "\n";
                out.close();

                ok(patchMgr->loadPatch(self, chan),
                   "a patch whose `side' names its own channel loads");
                ok(synth->getEffect(chan) != NULL,
                   "with the effect on the channel rather than dropped");

                const gthPatchManager::PatchFile *p = patchMgr->getPatch(chan);

                ok(p != NULL && p->doc.side == -1,
                   "and no side, which is where an unusable one lands");

                /* And so it survives the round trip that would have lost
                   it: written back out, the file still names the effect. */
                const string again = tmp + "/selfside-again.patch";

                ok(patchMgr->savePatch(again, chan), "it writes back");

                const std::vector<string> lines = fileLines(again);
                bool sawEffect = false, sawSide = false;

                for (size_t i = 0; i < lines.size(); i++)
                {
                    if (lines[i].compare(0, 7, "effect ") == 0)
                        sawEffect = true;

                    if (lines[i].compare(0, 5, "side ") == 0)
                        sawSide = true;
                }

                ok(sawEffect, "still naming its effect");
                ok(!sawSide, "and without the side it could not honour");
            }
        }
    }

    delete synth;

    std::error_code ec;
    fs::remove_all(tmp, ec);

    if (failures)
        printf("\n%d check(s) failed\n", failures);
    else
        printf("\na first run makes a sound and the file it writes travels\n");

    return failures;
}
