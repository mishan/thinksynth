/*
 * Copyright (C) 2026 The thinksynth authors
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
 * tuningcheck -- thTuning against Scala's own examples and hand-worked
 * pitches.
 *
 * Expected notes are written out, worked from the spec by hand (to 15-17
 * digits where they are not whole, checked to 1e-9), never computed by the
 * code under test.
 * After every build that succeeds, and every refusal, the invariants are
 * checked whatever the case meant to test: a refusal leaves its object as
 * it was; every mapped note is finite and within float range; keys one
 * repeat of the map apart differ by that repeat's pitch, to rounding; the
 * reference key sits exactly on its note. Last, a seeded mutation loop
 * runs the same checks on thousands of damaged files.
 */

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "thTuning.h"

static int failures = 0;

static void fail (const std::string &what)
{
    fprintf(stderr, "FAIL %s\n", what.c_str());
    failures++;
}

/* A repeat of the map: from a key to the key one repeat on, in notes. */
static double repeatNotes (const thScale &s, const thKeymap &m)
{
    const int n = (int)s.cents.size();
    const int d = m.size == 0 ? n : m.periodDegree;
    const long long q = d >= 0 ? d / n : -((-(long long)d + n - 1) / n);
    const int r = (int)(d - q * n);

    return ((double)q * s.cents.back() + (r ? s.cents[r - 1] : 0.0)) / 100.0;
}

static void invariants (const std::string &name, const thScale &s,
                        const thKeymap &m, const thTuning &t)
{
    const int step = m.size == 0 ? (int)s.cents.size() : m.size;
    const double repeat = repeatNotes(s, m);

    /* Rounding is relative to the largest quantity summed into a note, not
       to the note: a degree of 1e14 cents leaves a small note carrying
       its rounding. Bound that quantity by the largest degree times the
       most periods a key can reach. */
    double degreeMax = 0, reach = thTuning::KEYS + fabs((double)m.middle) +
                                  fabs((double)m.periodDegree);

    for (double c : s.cents)
        degreeMax = std::max(degreeMax, fabs(c));
    for (int d : m.map)
        if (d != thKeymap::UNMAPPED)
            reach = std::max(reach, thTuning::KEYS + fabs((double)d));

    const double scale = 1 + fabs(m.referenceNote) +
                         degreeMax * (2 + reach / (double)s.cents.size()) / 100;

    for (int k = 0; k < thTuning::KEYS; k++)
    {
        if (!t.mapped(k))
            continue;

        const double n = t.note(k);

        if (!std::isfinite(n) || fabs(n) > FLT_MAX)
            fail(name + ": key " + std::to_string(k) + " is not a float");

        if (k + step < thTuning::KEYS && t.mapped(k + step) &&
            fabs(t.note(k + step) - n - repeat) > 1e-12 * scale)
            fail(name + ": keys " + std::to_string(k) + " and " +
                 std::to_string(k + step) + " are not a repeat apart");
    }

    if (m.first <= m.reference && m.reference <= m.last &&
        t.mapped(m.reference) && t.note(m.reference) != m.referenceNote)
        fail(name + ": the reference is off its note");
}

/* Parse both and build; on success check the invariants. */
static bool load (const std::string &name, const std::string &scl,
                  const std::string *kbm, thScale &s, thKeymap &m,
                  thTuning &t, std::string &err, int &line)
{
    if (!s.parse(scl, err, line))
        return false;

    if (kbm && !m.parse(*kbm, err, line))
        return false;

    line = 0;

    if (!t.build(s, m, err))
        return false;

    invariants(name, s, m, t);
    return true;
}

/* ---------------------------------------------------------------- files */

static const char *const TET12 =
    "! 12-TET\n"
    "Twelve-tone equal temperament\n"
    " 12\n"
    "!\n"
    " 100.0\n 200.0\n 300.0\n 400.0\n 500.0\n 600.0\n"
    " 700.0\n 800.0\n 900.0\n 1000.0\n 1100.0\n 2/1\n";

static std::string edo (int n)
{
    std::string text = "! equal\n" + std::to_string(n) + "-EDO\n" +
                       std::to_string(n) + "\n";

    for (int i = 1; i < n; i++)
    {
        char buf[64];

        snprintf(buf, sizeof buf, "%.12f\n", 1200.0 * i / n);
        text += buf;
    }

    return text + "2/1\n";
}

static const char *const JUST =
    "Just major\n"
    "7\n"
    "9/8\n5/4\n4/3\n3/2\n5/3\n15/8\n2/1\n";

static const char *const BOHLEN_PIERCE =
    "Bohlen-Pierce, 13 equal steps of 3/1\n"
    "13\n"
    "146.304230835800\n292.608461671600\n438.912692507400\n"
    "585.216923343200\n731.521154179000\n877.825385014800\n"
    "1024.129615850599\n1170.433846686399\n1316.738077522199\n"
    "1463.042308357999\n1609.346539193799\n1755.650770029599\n"
    "3/1\n";

/* ---------------------------------------------------------------- cases */

struct Expect {
    int key;
    bool mapped;
    double note;
    bool exact;
};

struct Known {
    const char *name;
    std::string scl;
    const char *kbm;                /* NULL: none */
    int degrees;                    /* parsed, as a no-op guard */
    std::vector<Expect> expect;
};

static void known (void)
{
    const std::vector<Known> cases = {
        { "12-TET in cents, no keymap", TET12, NULL, 12,
          { { 0, true, 0, true }, { 59, true, 59, true },
            { 60, true, 60, true }, { 61, true, 61, true },
            { 127, true, 127, true } } },
        { "12-TET, keymap at 69 = 440",
          TET12,
          "! the standard keymap, with A as the reference\n"
          "0\n0\n127\n60\n69\n440.0\n12\n", 12,
          { { 0, true, 0, true }, { 61, true, 61, true },
            { 69, true, 69, true }, { 127, true, 127, true } } },
        { "19-EDO", edo(19), NULL, 19,
          { { 60, true, 60, true },
            { 61, true, 60.63157894736842, false },
            { 59, true, 59.36842105263158, false },
            { 79, true, 72, false }, { 41, true, 48, false } } },
        { "5-limit just major, ratios", JUST, NULL, 7,
          { { 60, true, 60, true },
            { 61, true, 62.039100017307746, false },
            { 64, true, 67.01955000865388, false },
            { 59, true, 58.88268714730222, false },
            { 67, true, 72, false } } },
        { "Bohlen-Pierce, 3/1 period", BOHLEN_PIERCE, NULL, 13,
          { { 61, true, 61.46304230835799, false },
            { 73, true, 79.01955000865388, false },
            { 47, true, 40.98044999134613, false } } },
        { "gaps in the map",
          TET12,
          "12\n0\n127\n60\n69\n440\n12\n"
          "0\nx\n2\nx\n4\n5\nx\n7\nx\n9\nx\n11\n", 12,
          { { 60, true, 60, true }, { 61, false, 0, true },
            { 62, true, 62, true }, { 69, true, 69, true },
            { 73, false, 0, true }, { 74, true, 74, true } } },
        { "reference at 432 Hz",
          TET12, "0\n0\n127\n60\n69\n432\n12\n", 12,
          { { 69, true, 68.6823334636657072, false },
            { 60, true, 59.6823334636657072, false } } },
        { "a map entry past the scale",
          TET12, "2\n0\n127\n60\n60\n261.6255653005986\n24\n0\n14\n", 12,
          { { 60, true, 60, false }, { 61, true, 74, false },
            { 62, true, 84, false }, { 59, true, 50, false } } },
        { "negative map entries",
          TET12, "2\n0\n127\n60\n60\n261.6255653005986\n2\n0\n-1\n", 12,
          { { 60, true, 60, false }, { 61, true, 59, false },
            { 62, true, 62, false }, { 63, true, 61, false } } },
        { "a negative formal octave",
          TET12, "1\n0\n127\n60\n69\n440\n-1\n0\n", 12,
          /* The keyboard reversed about the reference: 138 - key. */
          { { 59, true, 79, true }, { 60, true, 78, true },
            { 61, true, 77, true }, { 69, true, 69, true } } },
        { "a reference below the retuned keys",
          TET12, "0\n60\n127\n60\n57\n220\n12\n", 12,
          { { 57, false, 0, true }, { 60, true, 60, true },
            { 72, true, 72, true } } },
        { "formal octave of degree 0",
          TET12, "2\n0\n127\n60\n69\n440\n0\n0\n4\n", 12,
          { { 60, true, 65, true }, { 61, true, 69, true },
            { 62, true, 65, true }, { 63, true, 69, true } } },
        { "reference outside the retuned keys",
          TET12, "0\n60\n72\n60\n81\n880\n12\n", 12,
          { { 60, true, 60, true }, { 72, true, 72, true },
            { 59, false, 0, true }, { 73, false, 0, true },
            { 81, false, 0, true } } },
        { "a map shorter than its size",
          TET12, "12\n0\n127\n60\n60\n261.6255653005986\n12\n0\n1\n2\n", 12,
          { { 60, true, 60, false }, { 62, true, 62, false },
            { 63, false, 0, true }, { 71, false, 0, true },
            { 72, true, 72, false } } },
    };

    for (const Known &c : cases)
    {
        thScale s;
        thKeymap m;
        thTuning t;
        std::string err, kbm = c.kbm ? c.kbm : "";
        int line = 0;

        if (!load(c.name, c.scl, c.kbm ? &kbm : NULL, s, m, t, err, line))
        {
            fail(std::string(c.name) + ": refused, line " +
                 std::to_string(line) + ": " + err);
            continue;
        }

        if ((int)s.cents.size() != c.degrees)
            fail(std::string(c.name) + ": " + std::to_string(s.cents.size()) +
                 " degrees parsed");

        for (const Expect &e : c.expect)
        {
            const std::string at = std::string(c.name) + ", key " +
                                   std::to_string(e.key);

            if (t.mapped(e.key) != e.mapped)
                fail(at + (e.mapped ? " unmapped" : " mapped"));
            else if (e.mapped && e.exact && t.note(e.key) != e.note)
                fail(at + ": " + std::to_string(t.note(e.key)) + ", not exactly " +
                     std::to_string(e.note));
            else if (e.mapped && fabs(t.note(e.key) - e.note) > 1e-9)
                fail(at + ": " + std::to_string(t.note(e.key)) + ", not " +
                     std::to_string(e.note));
        }
    }

    /* Exactness over every key, not the ones the tables pick. */
    thScale s;
    thKeymap m;
    thTuning t;
    std::string err;
    int line;

    const std::string atA = "0\n0\n127\n60\n69\n440\n12\n";

    for (const std::string *kbm : { (const std::string *)NULL, &atA })
    {
        thKeymap fresh;

        m = fresh;

        if (load("12-TET identity", TET12, kbm, s, m, t, err, line))
            for (int k = 0; k < thTuning::KEYS; k++)
                if (!t.mapped(k) || t.note(k) != k)
                    fail(std::string("12-TET identity") + (kbm ? ", keymap" : "") +
                         ": key " + std::to_string(k));
    }
}

/* Lines the spec gives as valid, each alone as the scale's one degree, and
   the cents it must read as. */
static void accepted (void)
{
    static const struct { const char *line; double cents; } degrees[] = {
        { "81/64", 407.82000346154886 }, { "408.0", 408 }, { "408.", 408 },
        { "5", 2786.3137138648344 }, { "1200.0 cents", 1200 },
        { "5/4 E", 386.31371386483477 }, { "3/2!fifth", 701.95500086538754 },
        { ".5", 0.5 }, { "+1200.0", 1200 }, { "\t1200.0\t", 1200 },
        { "2147483647/1073741824", 1199.9999991938312 },
        { "3/2 .fifth", 701.95500086538754 }, { "5/4 /x", 386.31371386483477 },
        { "100.0 .5", 100 },
    };

    for (const auto &d : degrees)
    {
        thScale s;
        std::string err;
        int line;
        const std::string text = std::string("one\n1\n") + d.line + "\n";

        if (!s.parse(text, err, line))
            fail(std::string("degree '") + d.line + "' refused: " + err);
        else if (s.cents.size() != 1 || fabs(s.cents[0] - d.cents) > 1e-9)
            fail(std::string("degree '") + d.line + "' read as " +
                 (s.cents.empty() ? "nothing" : std::to_string(s.cents[0])));
    }

    /* Below unison is a degree, not an error; only the period has a sign
       rule. */
    {
        thScale s;
        std::string err;
        int line;

        if (!s.parse("down\n3\n10/20\n-5.0\n2/1\n", err, line))
            fail("degrees below unison refused: " + err);
        else if (fabs(s.cents[0] + 1200) > 1e-9 || s.cents[1] != -5)
            fail("degrees below unison misread");
    }

    /* Layout: an empty description, comments between degrees, blank
       lines, CRLF, CR alone, a byte-order mark, no final newline, lines
       past the count. */
    static const struct { const char *text, *description; } layouts[] = {
        { "!a\n\n 3\n!c\n100.0\n\n200.0\n2/1\n", "" },
        { "\xEF\xBB\xBF! BOM\r\nname\r\n3\r\n100.0\r\n200.0\r\n2/1\r\n", "name" },
        { "\xEF\xBB\xBFname\n3\n100.0\n200.0\n2/1\n", "name" },
        { "name\r3\r100.0\r200.0\r2/1\r", "name" },
        { "name\n3 ! three\n100.0\n200.0\n2/1", "name" },
        { "name\n3\n100.0\n200.0\n2/1\n300.0\nnot a degree\n", "name" },
        { "\n3\n100.0\n200.0\n2/1\n", "" },
        { "\r\n\r\n3\n100.0\n\r200.0\n2/1\n", "" },
        { "  !not a comment\n3\n100.0\n200.0\n2/1\n", "  !not a comment" },
    };

    for (const auto &l : layouts)
    {
        thScale s;
        std::string err;
        int line;

        if (!s.parse(l.text, err, line))
            fail("layout refused at line " + std::to_string(line) + ": " + err);
        else if (s.cents.size() != 3 || s.cents[0] != 100 || s.cents[1] != 200 ||
                 s.cents[2] != 1200 || s.description != l.description)
            fail("layout misread: " + std::to_string(s.cents.size()) +
                 " degrees, description '" + s.description + "'");
    }

    /* A keymap with comments between its header values; success leaves the
       error line alone. */
    thKeymap m;
    std::string err;
    int line = -1;

    if (!m.parse("! map\n0\n!\n0\n127\n60\n69\n! ref\n440\n12\n", err, line))
        fail("keymap with comments refused: " + err);
    else if (m.reference != 69 || m.frequency != 440 || m.referenceNote != 69 ||
             line != -1)
        fail("keymap with comments misread, or the error line written");
}

/* A malformed file, the line it must be refused at, and a word its reason
   must contain. */
struct Refusal {
    const char *text;
    int line;
    const char *word;
};

/* Each refused, and each leaving the object holding `valid' as it was. */
template <class T, size_t N>
static void refusals (const char *what, const char *valid, const Refusal (&cases)[N])
{
    for (const Refusal &c : cases)
    {
        T obj, before;
        std::string err;
        int line = 0;

        if (!obj.parse(valid, err, line))
            fail(std::string(what) + ": the valid file was refused: " + err);

        before = obj;

        if (obj.parse(c.text, err, line))
            fail(std::string(what) + " accepted: " + c.text);
        else
        {
            if (line != c.line || err.find(c.word) == std::string::npos)
                fail(std::string(what) + " '" + c.text + "': line " +
                     std::to_string(line) + " '" + err + "', not line " +
                     std::to_string(c.line) + " with '" + c.word + "'");

            if (!(obj == before))
                fail(std::string(what) + " '" + c.text + "': it changed");
        }
    }
}

static void refused (void)
{
    static const Refusal scl[] = {
        { "", 1, "description" },
        { "name\n", 2, "count" },
        { "name\nmany\n", 2, "count" },
        { "name\n0\n", 2, "no degrees" },
        { "name\n-1\n", 2, "count" },
        { "name\n3\n100.0\n2/1\n", 5, "degree" },
        { "name\n2\nabc\n2/1\n", 3, "or a ratio" },
        { "name\n2\n0/1\n2/1\n", 3, "positive" },
        { "name\n2\n3/0\n2/1\n", 3, "positive" },
        { "name\n2\n-3/2\n2/1\n", 3, "positive" },
        { "name\n2\n1e3\n2/1\n", 3, "or a ratio" },
        { "name\n2\ninf\n2/1\n", 3, "or a ratio" },
        { "name\n2\n0x10\n2/1\n", 3, "or a ratio" },
        { "name\n2\n.\n2/1\n", 3, "cents" },
        { "name\n2\n+.\n2/1\n", 3, "cents" },
        { "name\n2\n1/\n2/1\n", 3, "or a ratio" },
        { "name\n2\n/2\n2/1\n", 3, "or a ratio" },
        { "name\n2\n3 / 2\n2/1\n", 3, "blank inside" },
        { "name\n2\n100 .0\n2/1\n", 3, "blank inside" },
        { "name\n2\n3/ 2\n2/1\n", 3, "blank inside" },
        { "name\n2\n100.0\n-1200.0\n", 4, "period" },
        { "name\n1\n0.0\n", 3, "period" },
        { "name\n1\n-0.0\n", 3, "period" },
        { "name\n1\n4294967296/1\n", 3, "at most" },
        { "name\n1\n100.0c\n", 3, "cents" },
        { "name\n1\n1""0000000000000000000000000000000000000000000000000000000000"
          "000000000000000000000000000000000000000000000000000000000000000000000"
          "000000000000000000000000000000000000000000000000000000000000000000000"
          "000000000000000000000000000000000000000000000000000000000000000000000"
          "000000000000000000000000000000000000000000000000.0\n", 3, "cents" },
        { "name\n99999999999\n", 2, "count" },
    };

    static const Refusal kbm[] = {
        { "", 1, "map size" },
        { "0\n0\n127\n60\n69\n", 6, "frequency" },
        { "0\n0\n128\n60\n69\n440\n12\n", 3, "key" },
        { "0\n-1\n127\n60\n69\n440\n12\n", 2, "key" },
        { "0\n70\n60\n60\n69\n440\n12\n", 3, "first" },
        { "0\n0\n127\n60\n69\n0\n12\n", 6, "frequency" },
        { "0\n0\n127\n60\n69\ninf\n12\n", 6, "frequency" },
        { "-1\n0\n127\n60\n69\n440\n12\n", 1, "map size" },
        { "1.5\n0\n127\n60\n69\n440\n12\n", 1, "map size" },
        { "99999999999\n0\n127\n60\n69\n440\n12\n", 1, "map size" },
        { "2\n0\n127\n60\n69\n440\n12\n0\ny\n", 9, "map" },
        { "2\n0\n127\n60\n69\n440\n12\n0\n4\n7\n", 10, "map" },
        { "x\n0\n127\n60\n69\n440\n12\n", 1, "map size" },
        { "1\n0\n127\n60\n69\n440\n12\n-2147483648\n", 8, "reserved" },
    };

    refusals<thScale>("scale", "kept\n2\n100.0\n2/1\n", scl);
    refusals<thKeymap>("keymap", "0\n0\n127\n60\n69\n440\n12\n", kbm);

    /* Refused by build: an x on the reference, and a scale never parsed. */
    {
        thScale s;
        thKeymap m;
        thTuning t, before;
        std::string err;
        int line;

        s.parse(TET12, err, line);
        thKeymap plain;
        t.build(s, plain, err);
        before = t;

        if (!m.parse("2\n0\n127\n60\n61\n440\n12\n0\nx\n", err, line))
            fail("keymap with an x refused: " + err);
        else if (t.build(s, m, err))
            fail("an x on the reference built");
        else if (err.find("reference") == std::string::npos || !(t == before))
            fail("an x on the reference: '" + err + "', or the table changed");

        /* A keymap's fields are public: one never parsed can say anything. */
        thKeymap handmade;

        handmade.size = -3;
        handmade.map = { 0, 1, 2 };

        if (t.build(s, handmade, err))
            fail("a keymap of negative size built");
        else if (!(t == before))
            fail("a keymap of negative size changed the table");

        thScale empty;

        if (t.build(empty, plain, err))
            fail("an empty scale built");
        else if (!(t == before))
            fail("an empty scale changed the table");
    }

    /* A tuning past float range. */
    {
        thScale s;
        thTuning t;
        thKeymap plain;
        std::string err;
        int line;

        /* A period of 1e40 cents: key 127 is 67 periods up, past float. */
        if (!s.parse("huge\n1\n10000000000000000000000000000000000000000.0\n",
                     err, line))
            fail("a huge period refused by the parser: " + err);
        else if (t.build(s, plain, err))
            fail("a tuning past float range built");
        else if (!(t == thTuning()))
            fail("a tuning past float range changed the table");
    }

    /* No build, no keys. */
    thTuning fresh;

    for (int k = 0; k < thTuning::KEYS; k++)
        if (fresh.mapped(k))
            fail("a fresh tuning maps key " + std::to_string(k));
}

/* Control characters made visible, for a damaged file in a report. */
static std::string escape (const std::string &text)
{
    std::string out;

    for (unsigned char c : text)
    {
        char buf[8];

        if (c == '\n')
            out += "\\n\n";
        else if (c < 32 || c > 126)
        {
            snprintf(buf, sizeof buf, "\\x%02x", c);
            out += buf;
        }
        else
            out += (char)c;
    }

    return out;
}

/* An object used twice keeps nothing of its first use. */
static void reuse (void)
{
    thScale s;
    thKeymap m;
    thTuning t, fresh;
    std::string err;
    int line;

    s.parse(TET12, err, line);
    t.build(s, thKeymap(), err);

    if (!m.parse("12\n0\n127\n60\n69\n440\n12\n0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n",
                 err, line) ||
        !m.parse("2\n0\n127\n60\n60\n261.6255653005986\n12\n0\n", err, line))
        fail("reuse: a keymap refused: " + err);
    else if (m.map.size() != 1)
        fail("reuse: the second keymap kept " + std::to_string(m.map.size()) +
             " entries");
    else if (!t.build(s, m, err) || !fresh.build(s, m, err) || !(t == fresh))
        fail("reuse: a second build kept keys of the first");
}

/* What build accepts, parse must too. Objects filled in at random by hand,
   valid or not, through build; each one it takes is written out as .scl
   and .kbm text, parsed back, built again, and must come out the same. A
   rule build forgets shows here whichever rule it is. */
static bool decimalText (double v, std::string &out)
{
    char buf[64];

    snprintf(buf, sizeof buf, "%.17g", v);
    out = buf;

    if (out.find_first_of("eEn") != std::string::npos)
        return false;
    if (out.find('.') == std::string::npos)
        out += ".";

    return true;
}

static void byHand (void)
{
    std::mt19937 rng(1125);
    const auto pick = [&] (int lo, int hi) { return lo + (int)(rng() % (unsigned)(hi - lo + 1)); };
    int built = 0;

    for (int i = 0; i < 20000; i++)
    {
        thScale s;
        thKeymap m;
        std::string scl = "by hand\n", kbm, v;
        bool writable = true;

        const int n = pick(0, 5);

        for (int d = 0; d < n; d++)
            s.cents.push_back(pick(0, 9) ? pick(-2000, 3000) + pick(0, 999) / 1000.0
                                         : -pick(0, 100) / 10.0);

        scl += std::to_string(n) + "\n";
        for (double c : s.cents)
            writable &= decimalText(c, v), scl += v + "\n";

        m.size = pick(-1, 4) == 4 ? 12 : pick(-1, 3);
        m.first = pick(-2, 130);
        m.last = pick(-2, 130);
        m.middle = pick(-2, 130);
        m.reference = pick(-2, 130);
        m.periodDegree = pick(-14, 14);
        m.frequency = pick(0, 5) == 0 ? -pick(0, 2) : pick(1, 2000) + pick(0, 99) / 100.0;
        m.referenceNote = 69 + 12 * log2(m.frequency / 440) + (pick(0, 9) ? 0 : 0.5);

        for (int e = pick(0, std::max(0, m.size) + 1); e > 0; e--)
            m.map.push_back(pick(0, 4) ? pick(-20, 20) : thKeymap::UNMAPPED);

        kbm = std::to_string(m.size) + "\n" + std::to_string(m.first) + "\n" +
              std::to_string(m.last) + "\n" + std::to_string(m.middle) + "\n" +
              std::to_string(m.reference) + "\n";
        writable &= decimalText(m.frequency, v);
        kbm += v + "\n" + std::to_string(m.periodDegree) + "\n";
        for (int d : m.map)
            kbm += d == thKeymap::UNMAPPED ? std::string("x\n") : std::to_string(d) + "\n";

        thTuning t;
        std::string err;

        if (!t.build(s, m, err) || !writable)
            continue;

        built++;

        thScale s2;
        thKeymap m2;
        thTuning t2;
        int line;

        if (!s2.parse(scl, err, line) || !m2.parse(kbm, err, line))
            fail("by hand " + std::to_string(i) + ": built, but its text is refused: " +
                 err + "\n" + escape(scl) + "\n" + escape(kbm));
        else if (!t2.build(s2, m2, err) || !(t2 == t))
            fail("by hand " + std::to_string(i) + ": built, but its text builds " +
                 "another tuning\n" + escape(scl) + "\n" + escape(kbm));
    }

    if (built < 500)
        fail("by hand: only " + std::to_string(built) + " of 20000 built");
}

/* Damaged files, a seeded number of them: every outcome must be a refusal
   that changed nothing, or a build that passes the invariants. Seeds lean
   on what breaks parsers of numbers: huge counts and sizes, signs, x, and
   integers past ten digits. */
static void mutations (void)
{
    static const char *const SCL_SEEDS[] = { TET12, JUST, BOHLEN_PIERCE,
                                             "a\n1\n2/1\n" };
    static const char *const KBM_SEEDS[] = {
        "0\n0\n127\n60\n69\n440\n12\n",
        "12\n0\n127\n60\n69\n440\n12\n0\nx\n2\nx\n4\n5\nx\n7\nx\n9\nx\n11\n",
        "2\n0\n127\n60\n60\n261.6255653005986\n-24\n-3\n14\n",
    };
    static const char *const SPLICE[] = {
        "9999999999", "99999999999", "2147483648", "-2147483649", "-", "+",
        "x", "/", ".", "0", "!", "\n", "\r", " ", "1e308", "nan", "/0",
        "4294967295/1", "127", "128", "-1",
    };

    std::mt19937 rng(20261125);
    int built = 0;

    for (int i = 0; i < 20000; i++)
    {
        std::string scl = SCL_SEEDS[rng() % 4];
        std::string kbm = KBM_SEEDS[rng() % 3];
        const int edits = 1 + (int)(rng() % 4);

        for (int e = 0; e < edits; e++)
        {
            std::string &text = rng() % 2 ? scl : kbm;
            const size_t at = text.empty() ? 0 : rng() % text.size();

            switch (rng() % 3)
            {
            case 0: text.insert(at, SPLICE[rng() % (sizeof SPLICE / sizeof *SPLICE)]); break;
            case 1: if (!text.empty()) text.erase(at, 1 + rng() % 4); break;
            default: if (!text.empty()) text[at] = (char)(rng() % 256); break;
            }
        }

        thScale s, sBefore;
        thKeymap m, mBefore;
        thTuning t, tBefore;
        std::string err;
        int line;

        if (!s.parse("kept\n2\n100.0\n2/1\n", err, line))
            fail("the kept scale was refused: " + err);

        sBefore = s;

        if (!s.parse(scl, err, line))
        {
            if (!(s == sBefore))
                fail("mutation " + std::to_string(i) + ": a refused scale changed");
            continue;
        }

        if (!m.parse(kbm, err, line))
        {
            if (!(m == mBefore))
                fail("mutation " + std::to_string(i) + ": a refused keymap changed");
            continue;
        }

        const int before = failures;

        if (t.build(s, m, err))
        {
            invariants("mutation " + std::to_string(i), s, m, t);
            built++;
        }
        else if (!(t == tBefore))
            fail("mutation " + std::to_string(i) + ": a refused build changed");

        if (failures > before)
            fprintf(stderr, "  from:\n%s\n  and:\n%s\n", escape(scl).c_str(),
                    escape(kbm).c_str());
    }

    /* A loop whose damage always breaks the parse checks only refusals. */
    if (built < 1000)
        fail("only " + std::to_string(built) + " of 20000 damaged files built");
}

int main (void)
{
    known();
    accepted();
    refused();
    reuse();
    byHand();
    mutations();

    if (failures)
        fprintf(stderr, "tuningcheck: %d failures\n", failures);
    else
        printf("tuningcheck: ok\n");

    return failures ? 1 : 0;
}
