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

    thSynth *synth = new thSynth(pluginPath, TH_DEFAULT_WINDOW_LENGTH,
                                 TH_DEFAULT_SAMPLES);
    gthPatchManager *patchMgr = new gthPatchManager;

    /* ---- resolution ------------------------------------------------- */

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

            if (p != NULL && !p->dspFile.empty())
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

            if (p != NULL && !p->dspFile.empty())
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

    delete patchMgr;
    delete synth;

    std::error_code ec;
    fs::remove_all(tmp, ec);

    if (failures)
        printf("\n%d check(s) failed\n", failures);
    else
        printf("\na first run makes a sound and the file it writes travels\n");

    return failures;
}
