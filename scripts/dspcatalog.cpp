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
 * dspcatalog -- what a chooser would show, checked against the real corpus.
 *
 * src/DspCatalog.h reads a .dsp's header without building its graph, which is
 * the only way a menu over seventy-odd files can be drawn at all: the
 * alternative costs a dlopen per graph. The risk in reading a format twice is
 * the one docs/DSP_FORMAT.md keeps pointing at -- two readings drift -- so
 * what this harness does is put the two side by side. Every shipped file is
 * scanned *and* loaded, and the answers have to agree.
 *
 * Four things are checked.
 *
 *   The corpus scans. Every .dsp under the corpus directory turns into an
 *   entry, named the way a .patch or a .gen names it, and every entry has a
 *   title to draw.
 *
 *   The scan agrees with the parser. For each file: the title the scan read
 *   is thSynthTree::name(), the description is desc(), and the effect flag is
 *   takesInput(). That is the claim the chooser rests on -- that a row drawn
 *   from a header says what loading the file would have said.
 *
 *   Grouping, and what a chooser offers. Every entry lands in exactly one
 *   group, the groups come back sorted with Uncategorized last, and
 *   inGroup() accounts for every entry. The kind split and the filter are
 *   DspCatalog::matches rather than the browser's own, which is the reason
 *   what a chooser shows can be held still with no display anywhere near.
 *
 *   The fixtures. A file per awkward header, asserting the documented answer
 *   rather than whatever the code happens to do: no header at all, an `io'
 *   statement before the node it names, `in0' assigned on a node that is not
 *   the io node, a body that does not parse, and a file whose text is empty.
 *   Several of those are cases the shipped corpus cannot contain.
 *
 *     scripts/dspcatalog [-p plugindir] [--json] [corpus-dir]
 *
 * With no corpus argument it checks the fixtures alone, which is how it runs
 * where there is no source tree beside the build. The plugins are needed only
 * for the half that loads; with no corpus they are not needed at all. Exit
 * status is the number of failures.
 *
 * --json prints the catalog and checks nothing. That dump is the other half
 * of a parity gate: the browser's module scans its own MEMFS copy of the same
 * tree through the same class and prints the same bytes, and
 * wasm/web/dspcatalogcheck.mjs diffs the two. A menu the page draws and a
 * list the desktop draws are then the same list by construction rather than
 * by inspection.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "think.h"
#include "thSynth.h"
#include "thSynthTree.h"
#include "DspCatalog.h"

namespace fs = std::filesystem;

static int failed = 0;

static void ok (const string &what)
{
    printf("ok    %s\n", what.c_str());
}

static void fail (const string &what, const string &detail)
{
    printf("FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : ": ",
           detail.c_str());
    failed++;
}

static void check (bool good, const string &what, const string &detail = "")
{
    if (good)
        ok(what);
    else
        fail(what, detail);
}

static void checkEq (const string &got, const string &want, const string &what)
{
    check(got == want, what, "got `" + got + "', wanted `" + want + "'");
}

/* ------------------------------------------------------------------ */
/* The fixtures                                                        */
/* ------------------------------------------------------------------ */

static void one (const string &what, const string &file, const string &text,
                 const string &name, const string &desc, bool isEffect)
{
    DspCatalog cat;

    cat.take(file, text);

    const DspCatalog::Entry *e = cat.find(file);

    if (e == NULL)
    {
        fail(what, "no entry");
        return;
    }

    checkEq(e->name, name, what + ": title");
    checkEq(e->desc, desc, what + ": description");
    check(e->isEffect == isEffect, what + ": effect",
          e->isEffect ? "said effect" : "said instrument");
}

static void fixtures (void)
{
    /* The smallest thing that loads, per docs/DSP_FORMAT.md. */
    one("an instrument", "ts1.dsp",
        "name \"TS-1\";\n"
        "description \"A saw and a filter.\";\n"
        "node ionode { channels = 2; };\n"
        "io ionode;\n",
        "TS-1", "A saw and a filter.", false);

    /* An effect: the io node declares in0. `in0 = 0' is how a file writes
       down an arg the engine fills, and it is the whole of the difference
       between these two fixtures. */
    one("an effect", "fx/echo.dsp",
        "name \"Echo\";\n"
        "description \"A stereo delay.\";\n"
        "node ionode { channels = 2; in0 = 0; out0 = ionode->in0; };\n"
        "io ionode;\n",
        "Echo", "A stereo delay.", true);

    /* in0 on a node that is not the io node -- which is every instrument
       with a math::mul in it, and the reason the flag cannot be `does the
       text contain in0'. */
    one("in0 elsewhere", "mul.dsp",
        "name \"Mul\";\n"
        "node gain math::mul { in0 = osc->out; in1 = 0.5; };\n"
        "node ionode { channels = 2; out0 = gain->out; };\n"
        "io ionode;\n",
        "Mul", "", false);

    /* The io statement ahead of the node it names. No shipped file is
       written this way and nothing says one may not be. */
    one("io first", "early.dsp",
        "io ionode;\n"
        "name \"Early\";\n"
        "node ionode { channels = 2; in0 = 0; };\n",
        "Early", "", true);

    /* No header at all: the row is drawn from the filename, because a
       chooser has to draw something and that is what is left. */
    one("no header", "fx/nameless.dsp",
        "node ionode { channels = 2; in0 = 0; };\nio ionode;\n",
        "nameless", "", true);

    one("empty text", "empty.dsp", "", "empty", "", false);

    /* A body that does not parse. The header is above the damage, so the row
       is still drawn -- which is the other half of not building the menu by
       loading files. */
    {
        DspCatalog cat;

        const bool lexed =
            cat.take("broken.dsp",
                     "name \"Broken\";\n"
                     "description \"Has a bad byte in it.\";\n"
                     "node osc osc::simple { freq = \"unterminated\n");

        check(!lexed, "a bad file lexes false");

        const DspCatalog::Entry *e = cat.find("broken.dsp");

        check(e != NULL && e->name == "Broken",
              "a bad file still has a row");
    }

    /* What a chooser offers: the kind split and the filter. Both are
       DspCatalog::matches and neither is the widget's, which is what makes
       them checkable here. */
    {
        DspCatalog cat;

        cat.take("bd10.dsp", "name \"BD-10\";\n"
                             "description \"A tuned sine and a click.\";\n"
                             "node ionode { channels = 2; };\nio ionode;\n");
        cat.take("fx/echo.dsp", "name \"Echo\";\n"
                                "description \"A stereo delay.\";\n"
                                "node ionode { in0 = 0; };\nio ionode;\n");

        const DspCatalog::Entry &drum = *cat.find("bd10.dsp");
        const DspCatalog::Entry &echo = *cat.find("fx/echo.dsp");

        check(DspCatalog::matches(drum, false, ""),
              "an instrument is in the instrument list");
        check(!DspCatalog::matches(drum, true, ""),
              "an instrument is not in the effect list");
        check(DspCatalog::matches(echo, true, ""),
              "an effect is in the effect list");
        check(!DspCatalog::matches(echo, false, ""),
              "an effect is not in the instrument list");

        check(DspCatalog::matches(drum, false, "bd-10"),
              "the filter reads the title");
        check(DspCatalog::matches(drum, false, "BD-10"),
              "the filter ignores case");
        check(DspCatalog::matches(drum, false, "bd10.dsp"),
              "the filter reads the filename");
        check(DspCatalog::matches(drum, false, "click"),
              "the filter reads the description");
        check(!DspCatalog::matches(drum, false, "reverb"),
              "the filter excludes what it does not match");
    }

    /* Where a file with no category of its own is filed. */
    {
        DspCatalog cat;

        cat.take("ts1.dsp", "name \"TS-1\";\nio ionode;\n");
        cat.take("fx/echo.dsp", "name \"Echo\";\n"
                                "node ionode { in0 = 0; };\nio ionode;\n");

        checkEq(DspCatalog::groupOf(*cat.find("ts1.dsp")),
                DspCatalog::UNCATEGORIZED, "no directory, no category");
        checkEq(DspCatalog::groupOf(*cat.find("fx/echo.dsp")),
                "Effects", "fx/ is Effects");

        check(cat.groups().size() == 2 && cat.groups().back() ==
              DspCatalog::UNCATEGORIZED, "Uncategorized sorts last");
    }
}

/* ------------------------------------------------------------------ */
/* The corpus                                                          */
/* ------------------------------------------------------------------ */

/* What the parser says about the same file, for the comparison this harness
 * exists to make. Loading is what a chooser must not do and what the catalog
 * has to agree with, so it happens here and nowhere near a menu. */
static void againstTheParser (thSynth *synth, const string &dir,
                              const DspCatalog::Entry &e)
{
    const string path = (fs::path(dir) / e.file).string();

    /* parseTree and not loadTree: loadTree registers the tree under its
       `name' statement and deletes whatever was registered before, and two
       shipped graphs do share a title. Looking at a file must not be able to
       drop one. */
    thSynthTree *tree = synth->parseTree(path);

    if (tree == NULL)
    {
        fail(e.file, "would not load");
        return;
    }

    /* The tree's name is the `name' statement's, set by the grammar's
       nameset rule; a file with no statement leaves it empty, where the
       catalog puts the stem. So the comparison is made against what the file
       said, not against the fallback. */
    const string treeName = tree->name();
    const string treeDesc = tree->desc();

    if (!treeName.empty())
        checkEq(e.name, treeName, e.file + ": title");
    else
        ok(e.file + ": title (no statement; row says `" + e.name + "')");

    checkEq(e.desc, treeDesc, e.file + ": description");

    check(e.isEffect == tree->takesInput(), e.file + ": effect",
          e.isEffect ? "scan said effect, parser said instrument"
                     : "scan said instrument, parser said effect");

    delete tree;
}

static void corpus (const string &dir, thSynth *synth)
{
    DspCatalog cat;

    const int found = cat.scan(dir);

    check(found > 0, "the corpus scans",
          "nothing under " + dir);

    if (found <= 0)
        return;

    printf("#     %d files, %d groups\n", found, (int)cat.groups().size());

    /* Every file on disk has a row. The catalog walks the tree itself, so a
       second walk here is what says the walk is the right one. */
    int onDisk = 0;

    for (const auto &top : fs::recursive_directory_iterator(dir))
        if (top.path().extension() == ".dsp")
            onDisk++;

    check(found == onDisk, "every .dsp has a row",
          "scanned " + std::to_string(found) + ", on disk " +
          std::to_string(onDisk));

    size_t grouped = 0;

    for (size_t g = 0; g < cat.groups().size(); g++)
        grouped += cat.inGroup(cat.groups()[g]).size();

    check(grouped == (size_t)found, "every row is in exactly one group",
          "grouped " + std::to_string(grouped));

    for (size_t i = 0; i < cat.entries().size(); i++)
    {
        const DspCatalog::Entry &e = cat.entries()[i];

        if (e.name.empty())
            fail(e.file, "no title to draw");

        if (synth != NULL)
            againstTheParser(synth, dir, e);
    }
}

int main (int argc, char **argv)
{
    string plugindir = PLUGIN_PATH;
    string dir;
    bool json = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            plugindir = argv[++i];
        else if (strcmp(argv[i], "--json") == 0)
            json = true;
        else
            dir = argv[i];
    }

    if (json)
    {
        DspCatalog cat;

        cat.scan(dir);

        printf("%s\n", dspCatalogToJson(cat).c_str());

        return 0;
    }

    fixtures();

    if (!dir.empty())
    {
        thSynth synth(plugindir, TH_DEFAULT_WINDOW_LENGTH,
                      TH_DEFAULT_SAMPLES);

        corpus(dir, &synth);
    }

    printf("%s\n", failed == 0 ? "all ok" : "FAILURES");

    return failed;
}
