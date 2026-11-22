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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * hideditcheck -- a player's profile edited by splicing
 * (hid/thhProfileEdit.h).
 *
 * Each edit changes the statement it means and nothing else: a binding
 * replaced, added to a block written over lines or on one, or to a profile
 * with no block yet, taken out, reset, turned off; the player's comments
 * and layout kept; a block for one hand left alone; and text that doesn't
 * read, or is another device's, refused and untouched. And any binding,
 * written as the language writes it, reads back as itself: thousands of
 * them, of every kind with the options each takes, from a seed.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "thHidBlock.h"
#include "thhProfileEdit.h"

using std::string;

static int failed = 0;

static void
check (bool good, const string &what, const string &detail = "")
{
    if (good)
        printf("ok    %s\n", what.c_str());
    else
    {
        printf("FAIL  %s\n%s%s", what.c_str(), detail.empty() ? "" : detail.c_str(),
               detail.empty() ? "" : "\n");
        failed++;
    }
}

static thHidBinding
note (const char *control, int semis)
{
    thHidBinding b;

    b.kind = thHidBinding::NOTE;
    b.control = control;
    b.semis = semis;
    return b;
}

static void
checkSplices ()
{
    const string mine =
        "# my phone, my way\n"
        "device phone {\n"
        "    base = \"C3\";      # low\n"
        "    touch_1 = note 0;  # the first finger\n"
        "    touch_1_x = \"cutoff\";\n"
        "    touch_1_x = bend { range = 12; };\n"
        "\n"
        "    layer up {\n"
        "        touch_1 = note 12;\n"
        "    };\n"
        "};\n"
        "device phone left {\n"
        "    touch_1 = note 5;\n"
        "};\n";
    string err;

    const struct
    {
        const char *what;
        string (*edit) (string text, string &err);
        string want;
    } rows[] = {
        { "a binding replaced is that statement only, its comment kept",
          [](string t, string &e)
          {
              thhProfileSet(t, "phone", note("touch_1", 7), 0, e);
              return t;
          },
          "# my phone, my way\n"
          "device phone {\n"
          "    base = \"C3\";      # low\n"
          "    touch_1 = note 7;  # the first finger\n"
          "    touch_1_x = \"cutoff\";\n"
          "    touch_1_x = bend { range = 12; };\n"
          "\n"
          "    layer up {\n"
          "        touch_1 = note 12;\n"
          "    };\n"
          "};\n"
          "device phone left {\n"
          "    touch_1 = note 5;\n"
          "};\n" },
        { "the second of an axis's bindings is the second",
          [](string t, string &e)
          {
              thHidBinding b;

              b.kind = thHidBinding::AUX;
              b.control = "touch_1_x";
              b.semis = 3;
              thhProfileSet(t, "phone", b, 1, e);
              return t;
          },
          "# my phone, my way\n"
          "device phone {\n"
          "    base = \"C3\";      # low\n"
          "    touch_1 = note 0;  # the first finger\n"
          "    touch_1_x = \"cutoff\";\n"
          "    touch_1_x = aux 3;\n"
          "\n"
          "    layer up {\n"
          "        touch_1 = note 12;\n"
          "    };\n"
          "};\n"
          "device phone left {\n"
          "    touch_1 = note 5;\n"
          "};\n" },
        { "one added goes last in the block for either hand, indented as its others",
          [](string t, string &e)
          {
              thhProfileSet(t, "phone", note("touch_2", 12), 0, e);
              return t;
          },
          "# my phone, my way\n"
          "device phone {\n"
          "    base = \"C3\";      # low\n"
          "    touch_1 = note 0;  # the first finger\n"
          "    touch_1_x = \"cutoff\";\n"
          "    touch_1_x = bend { range = 12; };\n"
          "\n"
          "    layer up {\n"
          "        touch_1 = note 12;\n"
          "    };\n"
          "    touch_2 = note 12;\n"
          "};\n"
          "device phone left {\n"
          "    touch_1 = note 5;\n"
          "};\n" },
        { "one taken out goes with its line, the others and the layer's kept",
          [](string t, string &e)
          {
              thhProfileRemove(t, "phone", "touch_1_x", 0, e);
              return t;
          },
          "# my phone, my way\n"
          "device phone {\n"
          "    base = \"C3\";      # low\n"
          "    touch_1 = note 0;  # the first finger\n"
          "    touch_1_x = bend { range = 12; };\n"
          "\n"
          "    layer up {\n"
          "        touch_1 = note 12;\n"
          "    };\n"
          "};\n"
          "device phone left {\n"
          "    touch_1 = note 5;\n"
          "};\n" },
        { "off takes out every binding of it, and ignores it; a trailing comment stays",
          [](string t, string &e)
          {
              thhProfileOff(t, "phone", "touch_1", e);
              return t;
          },
          "# my phone, my way\n"
          "device phone {\n"
          "    base = \"C3\";      # low\n"
          "    # the first finger\n"
          "    touch_1_x = \"cutoff\";\n"
          "    touch_1_x = bend { range = 12; };\n"
          "\n"
          "    layer up {\n"
          "        touch_1 = note 12;\n"
          "    };\n"
          "    ignore touch_1;\n"
          "};\n"
          "device phone left {\n"
          "    touch_1 = note 5;\n"
          "};\n" },
    };

    for (const auto &r : rows)
    {
        err.clear();

        const string got = r.edit(mine, err);

        check(got == r.want && err.empty(), r.what,
              err.empty() ? "got:\n" + got : "err: " + err);
    }

    {
        string t = mine;

        thhProfileOff(t, "phone", "touch_1", err);
        thhProfileReset(t, "phone", "touch_1", err);
        check(t.find("ignore touch_1") == string::npos &&
              t.find("touch_1 = note 12") != string::npos &&
              t.find("touch_1 = note 5") != string::npos,
              "reset takes out the ignore too, and leaves the layer's and the "
              "left hand's");
    }

    const struct
    {
        const char *what;
        string text, want;
    } adds[] = {
        { "into a profile with nothing in it yet, a block of its own", "",
          "device phone {\n    touch_2 = note 12;\n};\n" },
        { "and after a comment, below it", "# mine\n",
          "# mine\ndevice phone {\n    touch_2 = note 12;\n};\n" },
        { "into a block on one line, on that line",
          "device phone { touch_1 = note 0; };\n",
          "device phone { touch_1 = note 0; touch_2 = note 12; };\n" },
        { "indented as the block's own statements are, not by a rule",
          "device phone {\n  touch_1 = note 0;\n};\n",
          "device phone {\n  touch_1 = note 0;\n  touch_2 = note 12;\n};\n" },
        { "into an empty block written over lines, indented",
          "device phone {\n};\n", "device phone {\n    touch_2 = note 12;\n};\n" },
        { "beside a block for one hand only, a block for either",
          "device phone right {\n    touch_1 = note 5;\n};\n",
          "device phone right {\n    touch_1 = note 5;\n};\n"
          "device phone {\n    touch_2 = note 12;\n};\n" },
    };

    for (const auto &a : adds)
    {
        string t = a.text;

        err.clear();
        thhProfileSet(t, "phone", note("touch_2", 12), 0, err);
        check(t == a.want && err.empty(), a.what, "got:\n" + t + err);
    }

    for (const auto &[what, text] : std::vector<std::pair<string, string>>{
             { "text that doesn't read is refused, and left as it was",
               "device phone { touch_1 = ; };\n" },
             { "and so is another device's", "device index { a = note 0; };\n" } })
    {
        string t = text;

        err.clear();
        check(!thhProfileSet(t, "phone", note("touch_2", 12), 0, err) &&
              t == text && !err.empty(), what, err);
    }

    thHidBinding scaled, quoted;

    scaled.kind = thHidBinding::BEND;
    scaled.control = "touch_1_x";
    scaled.scale = { 0, 1 };
    quoted.kind = thHidBinding::VALUE;
    quoted.control = "touch_1_y";
    quoted.arg = "cut\"off";
    for (const auto &[what, b] : std::vector<std::pair<string, thHidBinding>>{
             { "a second note of a button is refused, the text untouched",
               note("touch_1", 3) },
             { "and a scale the language has no name for", scaled },
             { "and an arg the language can't quote", quoted } })
    {
        string t = mine;

        err.clear();
        check(!thhProfileSet(t, "phone", b, 1, err) && t == mine && !err.empty(),
              what, err);
    }
}

/* ---- the round trip ----------------------------------------------------- */

static std::mt19937 rng(20261122);

static double
uniform (double lo, double hi)
{
    return std::uniform_real_distribution<double>(lo, hi)(rng);
}

static int
between (int lo, int hi)
{
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}

static bool
coin ()
{
    return between(0, 1) == 1;
}

/* A binding of kind `k' with options the parser takes for it, at random. */
static thHidBinding
generated (thHidBinding::Kind k, int n)
{
    static const std::vector<std::vector<int>> scales = {
        { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 4, 7, 9 }, { 0, 3, 5, 6, 7, 10 } };
    thHidBinding b;

    b.kind = k;
    b.control = "c" + std::to_string(n);
    switch (k)
    {
    case thHidBinding::NOTE:
        b.semis = between(-127, 127);
        b.channel = coin() ? 0 : between(1, 16);
        b.latch = coin();
        break;
    case thHidBinding::HOLD:
    case thHidBinding::TOGGLE:
        b.layer = "up";
        break;
    case thHidBinding::OCTAVE:
        b.semis = coin() ? between(1, 10) : between(-10, -1);
        break;
    case thHidBinding::VALUE:
        b.arg = "cutoff";
        b.channel = coin() ? 0 : between(1, 16);
        b.shape = coin() ? thHidBinding::EXP : thHidBinding::LINEAR;
        if ((b.hasMin = coin()))
            b.min = b.shape == thHidBinding::EXP ? uniform(0.001, 50) : uniform(-50, 50);
        if ((b.hasMax = coin()))
            b.max = b.shape == thHidBinding::EXP ? uniform(0.001, 50) : uniform(-50, 50);
        b.smooth = coin() ? 0 : uniform(0, 2);
        b.center = coin() ? 0 : uniform(-1, 1);
        break;
    case thHidBinding::PITCH:
        b.range = between(1, 127);
        if (coin())
            b.octaves = true;
        else if (coin())
            b.scale = scales[between(0, 2)];
        b.center = coin() ? 0 : uniform(-1, 1);
        break;
    case thHidBinding::VELOCITY:
    case thHidBinding::STRIKE:
        if ((b.hasMin = coin()))
            b.min = between(1, 64);
        if ((b.hasMax = coin()))
            b.max = between(64, 127);
        if (k == thHidBinding::STRIKE)
        {
            b.semis = between(-127, 127);
            b.channel = coin() ? 0 : between(1, 16);
            b.at = coin() ? 0.6 : uniform(0.01, 1);
            b.off = coin() ? std::max(0.0, b.at - 0.2) : uniform(0, b.at * 0.99);
        }
        break;
    case thHidBinding::BEND:
        b.range = between(1, 127);
        if (coin())
            b.scale = scales[between(0, 2)];
        b.smooth = coin() ? 0 : uniform(0, 2);
        b.center = coin() ? 0 : uniform(-1, 1);
        b.both = coin();
        break;
    case thHidBinding::AUX:
        b.semis = between(0, 3);
        if ((b.hasMin = coin()))
            b.min = uniform(-5, 5);
        if ((b.hasMax = coin()))
            b.max = uniform(-5, 5);
        b.smooth = coin() ? 0 : uniform(0, 2);
        b.center = coin() ? 0 : uniform(-1, 1);
        b.both = coin();
        break;
    case thHidBinding::LOOP:
        b.arg = between(0, 2) == 0 ? "record" : coin() ? "undo" : "clear";
        b.channel = coin() ? 0 : between(1, 16);
        b.at = coin() ? 0.6 : uniform(0.01, 1);
        b.off = coin() ? std::max(0.0, b.at - 0.2) : uniform(0, b.at * 0.99);
        break;
    }
    return b;
}

/* What `a' and `b' differ in, or nothing. */
static string
differ (const thHidBinding &a, const thHidBinding &b)
{
    string d;
    const auto field = [&](const char *name, bool same)
    {
        if (!same)
            d += string(d.empty() ? "" : ", ") + name;
    };
    const bool atOff = a.kind == thHidBinding::STRIKE || a.kind == thHidBinding::LOOP;

    field("kind", a.kind == b.kind);
    field("control", a.control == b.control);
    field("semis", a.semis == b.semis);
    field("layer", a.layer == b.layer);
    field("arg", a.arg == b.arg);
    field("channel", a.channel == b.channel);
    field("min", a.hasMin == b.hasMin && (!a.hasMin || a.min == b.min));
    field("max", a.hasMax == b.hasMax && (!a.hasMax || a.max == b.max));
    field("at", !atOff || a.at == b.at);
    field("off", !atOff || a.off == b.off);
    field("center", a.center == b.center);
    field("smooth", a.smooth == b.smooth);
    field("latch", a.latch == b.latch);
    field("both", a.both == b.both);
    field("shape", a.shape == b.shape);
    field("octaves", a.octaves == b.octaves);
    field("range", a.range == b.range);
    field("scale", a.scale == b.scale);
    return d;
}

static void
checkRoundTrip ()
{
    const int each = 400;
    int wrong = 0;
    string first;

    for (int k = thHidBinding::NOTE; k <= thHidBinding::LOOP; k++)
        for (int n = 0; n < each; n++)
        {
            const thHidBinding b = generated((thHidBinding::Kind)k, n);
            const string stmt = thhBindingText(b);
            const string text = "device d {\n    " + stmt +
                                "\n    layer up {\n    };\n};\n";
            thHidBlock back;
            string err, d;
            int line = 0;

            if (!thHidParseProfile(text, back, err, line))
                d = "doesn't read: " + err;
            else if (back.bindings.size() != 1)
                d = "reads as " + std::to_string(back.bindings.size()) + " bindings";
            else
                d = differ(b, back.bindings[0]);
            if (!d.empty() && wrong++ == 0)
                first = stmt + " -- " + d;
        }
    check(wrong == 0,
          "every binding written reads back as itself: " +
          std::to_string(each * (thHidBinding::LOOP + 1)) + " of every kind",
          std::to_string(wrong) + " wrong, the first " + first);
}

/* What the writer says is only what the parser wouldn't assume. */
static void
checkPlain ()
{
    thHidBinding strike, loop, latched = note("a", 7);

    strike.kind = thHidBinding::STRIKE;
    strike.control = "grip";
    loop.kind = thHidBinding::LOOP;
    loop.control = "b";
    loop.arg = "record";
    loop.at = 0.5;
    loop.off = 0.3;
    latched.latch = true;

    const string got = thhBindingText(strike) + " | " + thhBindingText(loop) +
                       " | " + thhBindingText(latched);

    check(got == "grip = strike 0; | b = loop record { at = 0.5; }; | "
                 "a = note 7 { latch; };",
          "a binding is written with what it says and no more", got);

    thHidBinding minned = note("a", 7);

    minned.hasMin = true;
    minned.min = 3;
    check(thhBindingText(minned) == "a = note 7;",
          "an option the kind doesn't take isn't written", thhBindingText(minned));

    thHidBinding far;
    thHidBlock back;
    string err;
    int line = 0;

    far.kind = thHidBinding::VALUE;
    far.control = "c";
    far.arg = "cutoff";
    far.hasMin = far.hasMax = true;
    far.min = 1e300;
    far.max = 4.9e-324;
    const string text = "device d { " + thhBindingText(far) + " };\n";
    const bool reads = thHidParseProfile(text, back, err, line);

    check(reads && back.bindings.size() == 1 && differ(far, back.bindings[0]).empty(),
          "the largest and smallest numbers read back as themselves",
          reads ? text : err);
}

int
main (void)
{
    checkSplices();
    checkPlain();
    checkRoundTrip();

    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed;
}
