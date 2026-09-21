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
 * patchcheck -- what a .patch means, held still.
 *
 * src/PatchFile.h is the format with no file and no synth around it, which is
 * what makes this possible at all: the rules used to live inside a loop over
 * fgets() that needed a real file on a real disk and a real channel to load
 * onto, so the only thing that ever exercised them was the application
 * starting up. They are an ordinary function over an ordinary string now, and
 * this runs everywhere gencheck does.
 *
 * Three things are checked.
 *
 *   The corpus. Every shipped .patch parses, names a graph, and says nothing
 *   the reader has to complain about. That is the claim "no corpus file
 *   changed meaning" -- a .patch that started complaining is a .patch this
 *   reader disagrees with the old one about.
 *
 *   The round trip. parse -> compose -> parse, and the second document equals
 *   the first, for every shipped file and every fixture. That is what lets a
 *   browser write a .patch the desktop reads back, and what stops compose
 *   from being judged by eye.
 *
 *   The first-run configuration, which both shells used to write down: the
 *   four patches a channel gets when nothing has aimed it, and the rule for
 *   a channel above the last of them.
 *
 *   The awkward lines. A fixture per documented answer, asserting the answer
 *   rather than whatever the code happens to do -- an `info' with no value, a
 *   line with no space in it, a line that starts with a NUL, CRLF endings,
 *   `side 99', `side' naming its own channel, an `fx.' name nothing declares,
 *   and `name 1,2,3'. Every one of those is a case the two old parsers
 *   answered differently, and several are cases the corpus cannot contain.
 *
 * What is not checked here is whether the graph exists, whether anything
 * declares `cutoff', or whether 99 is a channel. Those need a synth and are
 * PatchApply's; scripts/dspcheck --patch is what loads the corpus onto one.
 *
 *     scripts/patchcheck [corpus-dir]
 *
 * With no argument it checks the fixtures alone, which is how CI runs it on
 * the platforms that have no source tree beside the build. Exit status is the
 * number of failures.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "PatchFile.h"
#include "PatchSet.h"

using std::ifstream;
using std::ofstream;
using std::ostringstream;

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
/* Two documents are the same document                                  */
/* ------------------------------------------------------------------ */

/* Everything a file can say, compared field by field. Composing both and
 * comparing the text would be shorter and would also be circular -- it would
 * pass for any two documents compose happened to flatten together, which is
 * precisely the failure the round trip is looking for.
 *
 * complaints are not compared. A document that complained once must compose
 * to a file that does not, so the second pass having none is the property,
 * and it is asserted separately below.
 */
static bool sameDoc (const thPatchDoc &a, const thPatchDoc &b, string &why)
{
    if (a.dsp != b.dsp)
    {
        why = "dsp `" + a.dsp + "' became `" + b.dsp + "'";
        return false;
    }

    if (a.effect != b.effect)
    {
        why = "effect `" + a.effect + "' became `" + b.effect + "'";
        return false;
    }

    if (a.side != b.side)
    {
        char buf[64];

        snprintf(buf, sizeof(buf), "side %d became %d", a.side, b.side);
        why = buf;
        return false;
    }

    /* An info property with an empty value is not written, so it is not read
       back either -- parse refuses to make one, which is what makes this a
       plain comparison rather than one with an exception in it. */
    if (a.info != b.info)
    {
        why = "the info properties differ";
        return false;
    }

    if (a.args.size() != b.args.size())
    {
        why = "a different number of args came back";
        return false;
    }

    for (map<string, vector<float> >::const_iterator j = a.args.begin();
         j != a.args.end(); ++j)
    {
        map<string, vector<float> >::const_iterator k = b.args.find(j->first);

        if (k == b.args.end())
        {
            why = "`" + j->first + "' was lost";
            return false;
        }

        if (j->second.size() != k->second.size())
        {
            why = "`" + j->first + "' changed how many values it has";
            return false;
        }

        for (size_t i = 0; i < j->second.size(); i++)
            if (j->second[i] != k->second[i])
            {
                char buf[128];

                snprintf(buf, sizeof(buf), "%s[%u]: %f became %f",
                         j->first.c_str(), (unsigned)i, j->second[i],
                         k->second[i]);
                why = buf;
                return false;
            }
    }

    return true;
}

/* parse, compose, parse: the same document, and a file that has stopped
   complaining. Returns false having already said what went wrong. */
static bool roundTrips (const string &text, const string &what)
{
    thPatchDoc first, second;
    string why;

    if (!thPatchParse(text, first, why))
    {
        fail(what + ": parses", why);
        return false;
    }

    const string written = thPatchCompose(first);

    if (!thPatchParse(written, second, why))
    {
        fail(what + ": what it writes parses", why);
        return false;
    }

    if (!sameDoc(first, second, why))
    {
        fail(what + ": round trips", why);
        return false;
    }

    /* Whatever the reader had to skip on the way in, it wrote nothing that
       has to be skipped on the way out. A compose that preserved a bad line
       would round-trip and still be wrong. */
    if (!second.complaints.empty())
    {
        fail(what + ": what it writes is clean", second.complaints[0]);
        return false;
    }

    /* And again, so that "the bytes are stable" is a claim about bytes and
       not only about what they mean. This is the property a page needs to
       offer a download that a desktop will not rewrite on its first save. */
    if (thPatchCompose(second) != written)
    {
        fail(what + ": writes the same bytes twice", "");
        return false;
    }

    ok(what + ": round trips");

    return true;
}

/* ------------------------------------------------------------------ */
/* The corpus                                                           */
/* ------------------------------------------------------------------ */

static bool readFile (const std::filesystem::path &p, string &out)
{
    ifstream in(p, std::ios::binary);

    if (!in)
        return false;

    ostringstream buf;

    buf << in.rdbuf();
    out = buf.str();

    return true;
}

static void checkCorpus (const string &dir)
{
    std::error_code ec;
    int seen = 0;

    std::filesystem::recursive_directory_iterator walk(dir, ec);

    if (ec)
    {
        fail("the corpus is there", dir + ": " + ec.message());
        return;
    }

    /* Sorted, so a failure names the same file on every machine: the
       directory iterator's order is the filesystem's. */
    vector<std::filesystem::path> files;

    for (const std::filesystem::directory_entry &e : walk)
        if (e.is_regular_file() && e.path().extension() == ".patch")
            files.push_back(e.path());

    std::sort(files.begin(), files.end());

    for (const std::filesystem::path &p : files)
    {
        const string name = p.filename().string();
        string text;

        seen++;

        if (!readFile(p, text))
        {
            fail(name + ": reads", "could not be opened");
            continue;
        }

        thPatchDoc doc;
        string why;

        if (!thPatchParse(text, doc, why))
        {
            fail(name + ": parses", why);
            continue;
        }

        check(!doc.dsp.empty(), name + ": names a graph");

        /* The one that matters for "no shipped file changed meaning": this
           reader understood every line of it. */
        check(doc.complaints.empty(), name + ": has nothing to complain about",
              doc.complaints.empty() ? "" : doc.complaints[0]);

        roundTrips(text, name);
    }

    check(seen > 0, "the corpus has files in it");
}

/* ------------------------------------------------------------------ */
/* The awkward lines                                                    */
/* ------------------------------------------------------------------ */

/* Every fixture, named, in one table.
 *
 * Named rather than written where they are asserted about because they are
 * read twice: the checks below, and `-j', which dumps each one's document for
 * wasm/web/patchcheck.mjs to hold the module's reading of the same bytes
 * against. A fixture the two gates disagreed about the text of would be a
 * parity gate that proved nothing. */
struct Fixture
{
    const char *name;
    const char *text;
};

static const Fixture FIXTURES[] = {
    { "no-dsp",        "cutoff 1.0\n" },
    { "empty",         "" },
    { "dsp-only",      " \t dsp ts1.dsp\n" },
    { "info-no-value", "dsp ts1.dsp\ninfo foo\ncutoff 2.5\n" },
    { "lone-word",     "dsp ts1.dsp\ncutoff\n" },
    { "crlf",          "dsp ts1.dsp\r\ninfo title Super Res\r\n"
                       "cutoff 2.5\r\n" },
    { "side-99",       "dsp ts1.dsp\nside 99\neffect fx/delay.dsp\n" },
    { "side-4",        "dsp ts1.dsp\nside 4\neffect fx/delay.dsp\n" },
    { "side-0",        "dsp ts1.dsp\nside 0\neffect fx/delay.dsp\n" },
    { "side-negative", "dsp ts1.dsp\nside -1\neffect fx/delay.dsp\n" },
    { "effect",        "dsp ts1.dsp\neffect fx/delay.dsp\nfx.wet 0.5\n" },
    { "fx-orphan",     "dsp ts1.dsp\nfx.nosuchthing 0.5\n" },
    { "multi-value",   "dsp ts1.dsp\nwave 1,2,3\n" },
    { "not-a-number",  "dsp ts1.dsp\ncutoff abc\nres 4abc\nq 1,,3\n" },
    { "not-finite",    "dsp ts1.dsp\ncutoff nan\nres inf\nq -Infinity\n" },
    { "blanks",        "dsp ts1.dsp\nside 4 \neffect fx/delay.dsp\n"
                       "cutoff 1.04 \nwave 1, 2 ,3\n" },
    { "side-alone",    "dsp ts1.dsp\nside 4\n" },
    { "comments",      "# a comment\n\n   # an indented one\ndsp ts1.dsp\n"
                       "info comments one\\ntwo\n" },
    { "no-final-nl",   "dsp ts1.dsp\ncutoff 2.5" },
    { "everything",    "dsp ts1.dsp\nside 4\neffect fx/delay.dsp\n"
                       "info title Super Res\ninfo comments one\\ntwo\n"
                       "cutoff 2.5\nwave 1,2,3\nfx.wet 0.5\n" },
};

static const size_t FIXTURE_COUNT = sizeof(FIXTURES) / sizeof(FIXTURES[0]);

/* The one named, or a loud empty string: a typo in a name below would
   otherwise quietly assert things about nothing at all. */
static string fixture (const char *name)
{
    for (size_t i = 0; i < FIXTURE_COUNT; i++)
        if (strcmp(FIXTURES[i].name, name) == 0)
            return FIXTURES[i].text;

    fail(string("there is a fixture called `") + name + "'", "");

    return string();
}

/* Parses, and the caller gets the document. Fails the named check and
   returns false otherwise, so each fixture below reads as its assertion. */
static bool parses (const string &text, thPatchDoc &doc, const string &what)
{
    string why;

    if (thPatchParse(text, doc, why))
        return true;

    fail(what, why);

    return false;
}

static void checkFixtures (void)
{
    /* A patch is its `dsp' line. Nothing else is required and nothing else
       can fail it. */
    {
        thPatchDoc doc;
        string why;

        check(!thPatchParse(fixture("no-dsp"), doc, why),
              "a file with no dsp line is not a patch");
        check(why == "names no dsp", "and says so", why);

        check(!thPatchParse(fixture("empty"), doc, why), "nor is an empty one");

        if (parses(fixture("dsp-only"), doc, "a dsp line alone is a patch"))
        {
            ok("a dsp line alone is a patch");
            checkEq(doc.dsp, "ts1.dsp", "leading whitespace is not the name");
        }
    }

    /* `info foo' -- a property named and given nothing. The desktop refused
       the whole file over this, sharing its exit with a DSP that would not
       load, so a typo in a comment cost the instrument. A complaint, and the
       rest of the file is read. */
    {
        thPatchDoc doc;

        if (parses(fixture("info-no-value"), doc,
                   "an info line with no value does not fail the patch"))
        {
            ok("an info line with no value does not fail the patch");
            check(doc.info.empty(), "and invents no property");
            check(doc.args.count("cutoff") == 1,
                  "and the line after it is still read");
            check(doc.complaints.size() == 1, "and it is complained about");
        }
    }

    /* A word on a line by itself. Both old parsers skipped it in silence;
       silence is what made a misspelling impossible to find. */
    {
        thPatchDoc doc;

        if (parses(fixture("lone-word"), doc,
                   "a line with no space in it is not an arg"))
        {
            ok("a line with no space in it is not an arg");
            check(doc.args.empty(), "and sets nothing");
            check(doc.complaints.size() == 1, "and is complained about");
        }
    }

    /* A line whose first byte is a NUL. strlen() said 0, so the desktop's
       `buffer[len - 1]' was `buffer[-1]' -- a write one byte in front of the
       buffer, which is why the length is checked there to this day. A string
       has a length that does not end at a NUL, so this is now only a line
       with a strange character at the front of it. */
    {
        const string text = string("dsp ts1.dsp\n") + string(1, '\0') +
                            "cutoff 2.5\n";
        thPatchDoc doc;

        if (parses(text, doc, "a line starting with a NUL is survived"))
        {
            ok("a line starting with a NUL is survived");
            checkEq(doc.dsp, "ts1.dsp", "and the patch is still a patch");
        }
    }

    /* CRLF. The desktop kept the CR, so `dsp ts1.dsp\r' named a file with a
       control character in it -- a load that failed reporting a name that
       looked exactly right. */
    {
        thPatchDoc doc;

        if (parses(fixture("crlf"), doc, "a file with CRLF endings parses"))
        {
            ok("a file with CRLF endings parses");
            checkEq(doc.dsp, "ts1.dsp", "with no CR in the graph's name");
            checkEq(doc.info["title"], "Super Res",
                    "nor at the end of a property");
            check(doc.args["cutoff"].size() == 1 &&
                  doc.args["cutoff"][0] == 2.5f, "nor after a value");
        }
    }

    /* `side' is 1-based in the file and engine numbering in the document, and
     * that is the whole of what happens to it here. Whether 99 is a channel,
     * and whether a channel may name itself, need to know how many channels
     * there are and which one this is -- so they are PatchApply's, and
     * keeping the number is what makes the round trip exact. */
    {
        thPatchDoc doc;

        if (parses(fixture("side-99"), doc,
                   "side is read 1-based"))
        {
            ok("side is read 1-based");
            check(doc.side == 98, "and kept even where no such channel is");
        }

        if (parses(fixture("side-4"), doc,
                   "a side names a channel"))
            check(doc.side == 3, "a side is one less than what is written");

        if (parses(fixture("side-0"), doc,
                   "side 0 is no side"))
            check(doc.side == -1, "side 0 is no side");

        if (parses(fixture("side-negative"), doc,
                   "a negative side is no side"))
            check(doc.side == -1, "a negative side is no side");
    }

    /* The effect line itself -- the divergence that cost the page every
       channel effect. Its value is a filename, so a reader that tries it as a
       number gets NaN and drops the line, which is exactly what patch.js
       did. */
    {
        thPatchDoc doc;

        if (parses(fixture("effect"), doc,
                   "an effect line is read"))
        {
            ok("an effect line is read");
            checkEq(doc.effect, "fx/delay.dsp", "by the name it was given");
            check(doc.args.count("fx.wet") == 1,
                  "and its parameters are args like any other");
            check(doc.args.count("effect") == 0,
                  "and the effect itself is not an arg");
            check(doc.args.count("dsp") == 0, "nor is the graph");
            check(doc.args.count("side") == 0, "nor is the side");
        }
    }

    /* An `fx.' name nothing declares is a document like any other: the
     * document does not know what an effect declares, and refusing one here
     * would mean refusing every `fx.' line in a file read without a synth.
     * Where it is refused is PatchApply, by name and with a message.
     *
     * What this does assert is that the values follow their effect out of
     * compose: an `fx.' line with no `effect' above it is a line the reader
     * has nowhere to put, so writing one would be writing a patch that
     * complains at itself on every load.
     */
    {
        thPatchDoc doc;

        if (parses(fixture("fx-orphan"), doc,
                   "an fx. name with no effect parses"))
        {
            ok("an fx. name with no effect parses");
            check(doc.args.count("fx.nosuchthing") == 1,
                  "and is kept as an arg");
            check(thPatchCompose(doc).find("fx.nosuchthing") == string::npos,
                  "and is not written back without an effect to hold it");
        }
    }

    /* `name 1,2,3'. The one divergence resolved toward the page: a thArg has
       always held a list and the desktop kept the first value only. */
    {
        thPatchDoc doc;

        if (parses(fixture("multi-value"), doc,
                   "a multi-value arg keeps every value"))
        {
            ok("a multi-value arg keeps every value");
            check(doc.args["wave"].size() == 3, "all three of them");

            if (doc.args["wave"].size() == 3)
                check(doc.args["wave"][0] == 1 && doc.args["wave"][1] == 2 &&
                      doc.args["wave"][2] == 3, "in the order written");

            check(thPatchCompose(doc).find("wave 1.000000,2.000000,3.000000") !=
                  string::npos, "and writes them back as a list");
        }
    }

    /* A value that is not a number. strtof took what it could and shrugged at
       the rest, so `cutoff abc' was a silent zero -- a parameter set to a
       number nobody typed, indistinguishable from one somebody meant. */
    {
        thPatchDoc doc;

        if (parses(fixture("not-a-number"), doc,
                   "a value that is not a number is not a zero"))
        {
            ok("a value that is not a number is not a zero");
            check(doc.args.empty(), "and sets nothing");
            check(doc.complaints.size() == 3, "and each line is complained about");
        }
    }

    /* Nor is a value arithmetic cannot use. strtof spells `nan' and `inf'
       and consumes either whole, so both got past "the field was consumed"
       and onto a chanarg -- where one NaN poisons everything downstream of
       it, and where the page was told it was a 0, JSON having no spelling
       for one. */
    {
        thPatchDoc doc;

        if (parses(fixture("not-finite"), doc,
                   "a value that is not finite is not a value"))
        {
            ok("a value that is not finite is not a value");
            check(doc.args.empty(), "and sets nothing");
            check(doc.complaints.size() == 3,
                  "and each line is complained about");
        }
    }

    /* And the blanks around a value are not part of it. Both readings this
       replaces skipped them -- strtof does and Number() does -- so a file
       with a space after a value, or spaces around the commas in a list,
       loaded in both and would have been a complaint in one reading. */
    {
        thPatchDoc doc;

        if (parses(fixture("blanks"), doc,
                   "a value with blanks around it is a value"))
        {
            ok("a value with blanks around it is a value");
            check(doc.complaints.empty(), "with nothing to complain about",
                  doc.complaints.empty() ? "" : doc.complaints[0]);
            check(doc.args["wave"].size() == 3,
                  "and a list keeps every one of its values");
            check(doc.side == 3, "and a side with a space after it is a side");
        }
    }

    /* A side with no effect to hear it. Nothing puts it anywhere -- there is
       no effect to put it on -- but it is what the file said, and a document
       that quietly drops it is a file that does not come back the way it
       went in. */
    {
        thPatchDoc doc;

        if (parses(fixture("side-alone"), doc,
                   "a side with no effect line is read"))
        {
            ok("a side with no effect line is read");
            check(doc.side == 3, "and kept");
            check(thPatchCompose(doc).find("side 4\n") != string::npos,
                  "and written back out, effect or no effect");
        }
    }

    /* Comments, blank lines and the escape. */
    {
        thPatchDoc doc;

        if (parses(fixture("comments"), doc,
                   "comments and blank lines are skipped"))
        {
            ok("comments and blank lines are skipped");
            check(doc.complaints.empty(), "in silence",
                  doc.complaints.empty() ? "" : doc.complaints[0]);
            checkEq(doc.info["comments"], "one\ntwo",
                    "and an escaped newline is a newline");
        }
    }

    /* A file with no trailing newline: the last line is still a line. */
    {
        thPatchDoc doc;

        if (parses(fixture("no-final-nl"), doc,
                   "a file with no trailing newline is whole"))
        {
            ok("a file with no trailing newline is whole");
            check(doc.args.count("cutoff") == 1, "and its last line counts");
        }
    }

    /* And every fixture that is a patch round-trips, which is the property
       the corpus cannot test: no shipped file has an effect, a side, a
       multi-value arg or an escaped newline in it. */
    roundTrips(fixture("everything"),
               "a patch using every line the format has");
    roundTrips(fixture("blanks"), "a patch with blanks around its values");
    roundTrips(fixture("side-alone"), "a patch with a side and no effect");
}

/* ------------------------------------------------------------------ */
/* The dump the parity gate compares                                    */
/* ------------------------------------------------------------------ */

/* Every fixture, written out beside its document: `<dir>/<name>.patch' and
 * `<dir>/<name>.json', and an index naming them in order.
 *
 * The bytes go out with the dump so that the wasm side reads exactly the text
 * this one did -- the same thing scripts/panelcheck -j does with its fixture
 * .dsp, and for the same reason: a gate that compared two readings of two
 * different files would pass for the wrong reason. A fixture that is not a
 * patch has an empty document and its refusal in `why', because that answer
 * has to match across the two builds too.
 */
static int dumpFixtures (const string &dir)
{
    std::error_code ec;

    std::filesystem::create_directories(dir, ec);

    string index;

    for (size_t i = 0; i < FIXTURE_COUNT; i++)
    {
        const string name = FIXTURES[i].name;
        const string text = FIXTURES[i].text;

        thPatchDoc doc;
        string why;

        const bool good = thPatchParse(text, doc, why);

        ofstream patch((std::filesystem::path(dir) / (name + ".patch")),
                            std::ios::binary | std::ios::trunc);
        ofstream json((std::filesystem::path(dir) / (name + ".json")),
                           std::ios::binary | std::ios::trunc);

        if (!patch || !json)
        {
            fprintf(stderr, "patchcheck: could not write into %s\n",
                    dir.c_str());
            return 1;
        }

        patch << text;
        json << (good ? thPatchDocToJson(doc) : string()) << "\n" << why
             << "\n";

        index += name + "\n";
    }

    ofstream list((std::filesystem::path(dir) / "index.txt"),
                  std::ios::binary | std::ios::trunc);

    list << index;

    /* And the first-run configuration, one line per channel, past the end of
     * the table on purpose: what a channel above the last entry gets is a
     * rule and not a row, and it is the half of this that a list of four
     * names would not catch. The page had its own copy of both. */
    ofstream defaults((std::filesystem::path(dir) / "defaults.txt"),
                      std::ios::binary | std::ios::trunc);

    for (int c = 0; c < thPatchDefaultCount() * 2 + 1; c++)
        defaults << thPatchDefaultFor(c) << "\n";

    return 0;
}

int main (int argc, char *argv[])
{
    /* `-j <dir>' prints nothing and checks nothing: it writes the fixtures
       and their documents for wasm/web/patchcheck.mjs, which is the gate that
       keeps one reading of the format from becoming two again. */
    if (argc > 2 && strcmp(argv[1], "-j") == 0)
        return dumpFixtures(argv[2]);

    checkFixtures();

    if (argc > 1)
        checkCorpus(argv[1]);
    else
        printf("(no corpus directory given; fixtures only)\n");

    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");

    return failed;
}
