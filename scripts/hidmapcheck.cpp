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
 * hidmapcheck -- what a HID profile does with a controller, held still.
 *
 * Each row is a profile and a script of steps, and what the mapper asked
 * for at each step. The steps:
 *
 *   ch N            the host's current channel is N (1-16)
 *   down B, up B    a button
 *   axis A V        an axis moves to V
 *   unplug, plug    the controller goes and comes back
 *   release         the host asks the mapper to let go
 *
 * Every step but `release' is one update(). What each step asked for is
 * written out by describe() and the steps are joined with " | ", so a row
 * says when as well as what: an off one step late is a stuck note.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#include "thhMapper.h"

using std::string;

static int failed = 0;

static void
report (const char *name, const string &why)
{
    if (why.empty())
        printf("ok    %s\n", name);
    else
    {
        printf("FAIL  %s\n  %s\n", name, why.c_str());
        failed++;
    }
}

static string
describe (const std::vector<thhAction> &acts)
{
    std::ostringstream s;

    for (size_t i = 0; i < acts.size(); i++)
    {
        const thhAction &a = acts[i];
        char t[16];

        s << (i ? ", " : "");
        if (a.kind == thhAction::NOTE_ON)
            s << "on " << a.channel + 1 << " " << a.note << " " << a.vel;
        else if (a.kind == thhAction::NOTE_OFF)
            s << "off " << a.channel + 1 << " " << a.note;
        else
        {
            snprintf(t, sizeof(t), "%.3f", a.t);
            s << a.arg << "@" << a.channel + 1 << " " << t;
        }
    }

    return s.str();
}

/* Runs a script; on a step it can't read, says so in place of the result. */
static string
play (thhMapper &m, const thhControls &c, const string &script)
{
    std::istringstream steps(script);
    string step, out;
    thhState s;
    int ch = 0;

    s.connected = true;

    for (int n = 0; std::getline(steps, step, ';'); n++)
    {
        std::istringstream w(step);
        string verb, name;
        std::vector<thhAction> acts;

        w >> verb >> name;

        if (verb == "ch")
            ch = atoi(name.c_str()) - 1;
        else if ((verb == "down" || verb == "up") && c.button(name) >= 0)
        {
            const uint32_t bit = 1u << c.button(name);

            s.buttons = verb == "down" ? (s.buttons | bit) : (s.buttons & ~bit);
        }
        else if (verb == "axis" && c.axis(name) >= 0)
            w >> s.axis[c.axis(name)];
        else if (verb == "unplug" || verb == "plug")
            s.connected = verb == "plug";
        else if (verb != "release")
            return "a step this harness can't read: '" + step + "'";

        if (verb == "release")
            m.release(acts);
        else
            m.update(s, ch, acts);

        out += (n ? " | " : "") + describe(acts);
    }

    return out;
}

struct Row
{
    const char *name;
    const char *profile;
    const char *script;
    const char *want;       /* what each step asked for, or NULL if refused */
    int         errLine;
    const char *errHas;
};

#define NOTES "device index { a = note 0; b = note 2; };"
#define OCTAVES \
    "device index { a = note 0; system = octave 1; b = octave -1; };"
#define STICK \
    "device index { a = note 0; stick_y = pitch { step = octave; range = 2; }; };"

static const Row ROWS[] = {
    { "a note on the current channel", NOTES,
      "ch 3; down a; up a", " | on 3 60 100 | off 3 60", 0, NULL },
    { "the device's channel over the current one",
      "device index { channel = 2; a = note 0; };",
      "ch 3; down a; up a", " | on 2 60 100 | off 2 60", 0, NULL },
    { "a binding's channel over the device's",
      "device index { channel = 2; a = note 0 { channel = 5; }; };",
      "down a; up a", "on 5 60 100 | off 5 60", 0, NULL },
    { "notes above and below the base",
      "device index { base = \"A3\"; a = note -12; b = note 7; };",
      "down a; down b; up a; up b",
      "on 1 45 100 | on 1 64 100 | off 1 45 | off 1 64", 0, NULL },
    { "an octave step moves the next note", OCTAVES,
      "down system; up system; down a; up a; down b; up b; down b; up b; "
      "down a; up a",
      " |  | on 1 72 100 | off 1 72 |  |  |  |  | on 1 48 100 | off 1 48", 0, NULL },
    { "a release undoes its press after the octave moves", OCTAVES,
      "down a; down system; up a", "on 1 60 100 |  | off 1 60", 0, NULL },
    { "the pitch axis in octaves", STICK,
      "axis stick_y 1; down a; up a; axis stick_y 0.4; down a; up a; "
      "axis stick_y -1; down a; up a",
      " | on 1 84 100 | off 1 84 |  | on 1 72 100 | off 1 72 |  | "
      "on 1 36 100 | off 1 36", 0, NULL },
    { "the pitch axis in semitones, on a finger",
      "device index { a = note 0; middle = pitch { range = 12; }; };",
      "axis middle 0.5; down a; up a", " | on 1 66 100 | off 1 66", 0, NULL },
    { "a held note ignores the pitch axis moving", STICK,
      "down a; axis stick_y 1; up a", "on 1 60 100 |  | off 1 60", 0, NULL },
    { "a velocity source",
      "device index { a = note 0; trigger = velocity { min = 20; max = 120; }; };",
      "axis trigger 0; down a; up a; axis trigger 1; down a; up a; "
      "axis trigger 0.5; down a; up a",
      " | on 1 60 20 | off 1 60 |  | on 1 60 120 | off 1 60 |  | "
      "on 1 60 70 | off 1 60", 0, NULL },
    { "a fixed velocity with no source",
      "device index { velocity = 64; a = note 0; };",
      "down a; up a", "on 1 60 64 | off 1 60", 0, NULL },
    { "two buttons on one note",
      "device index { a = note 0; b = note 0; };",
      "down a; down b; up a; up b", "on 1 60 100 |  |  | off 1 60", 0, NULL },
    { "a note past the top sounds nothing",
      "device index { base = \"G9\"; a = note 12; };",
      "down a; up a", " | ", 0, NULL },
    { "a release on the channel the note started on", NOTES,
      "down a; ch 4; up a", "on 1 60 100 |  | off 1 60", 0, NULL },
    { "a value along a finger, past a deadband",
      "device index { middle = \"cutoff\" { min = 200; max = 8000; }; };",
      "axis middle 0.5; axis middle 0.5; axis middle 0.501; axis middle 1",
      "cutoff@1 0.500 |  |  | cutoff@1 1.000", 0, NULL },
    { "a bipolar value from the middle",
      "device index { stick_x = \"pan\"; };",
      "axis stick_x 0; axis stick_x -1", "pan@1 0.500 | pan@1 0.000", 0, NULL },
    { "a value on the current channel",
      "device index { middle = \"cutoff\"; };",
      "ch 2; axis middle 1", "cutoff@2 0.000 | cutoff@2 1.000", 0, NULL },
    { "an unplug ends held notes", NOTES,
      "down a; unplug; plug; up a",
      "on 1 60 100 | off 1 60 | on 1 60 100 | off 1 60", 0, NULL },
    { "a release ends held notes, and they don't sound again", NOTES,
      "down a; release; up a; down a",
      "on 1 60 100 | off 1 60 |  | on 1 60 100", 0, NULL },

    { "a channel past 16 is channel 1", NOTES,
      "ch 17; down a; up a", " | on 1 60 100 | off 1 60", 0, NULL },
    { "a slow creep reaches the end",
      "device index { middle = \"cutoff\"; };",
      "axis middle 0.997; axis middle 0.999; axis middle 1",
      "cutoff@1 0.997 |  | cutoff@1 1.000", 0, NULL },
    { "a note past the bottom sounds nothing",
      "device index { base = \"C-1\"; a = note -1; };",
      "down a; up a", " | ", 0, NULL },
    { "octave steps stop where notes do", OCTAVES,
      "down system; up system; down system; up system; down system; "
      "up system; down system; up system; down system; up system; "
      "down system; up system; down b; up b; down a; up a",
      " |  |  |  |  |  |  |  |  |  |  |  |  |  | on 1 108 100 | off 1 108",
      0, NULL },
    { "an octave and the pitch axis past the top",
      "device index { base = \"C7\"; a = note 0; system = octave 1; "
      "stick_y = pitch { step = octave; range = 2; }; };",
      "down system; up system; axis stick_y 1; down a; up a",
      " |  |  |  | ", 0, NULL },

    { "a control the device doesn't have",
      "device index {\n  zz = note 0;\n};", "", NULL, 2, "'zz'" },
    { "a button bound to a value",
      "device index { a = \"cutoff\"; };", "", NULL, 1, "is a button" },
    { "an axis bound to a note",
      "device index { middle = note 0; };", "", NULL, 1, "is an axis" },
    { "two pitch axes",
      "device index {\n  stick_y = pitch { range = 1; };\n"
      "  pad_y = pitch { range = 1; };\n};", "", NULL, 3, "one pitch axis" },
    { "a profile for another device",
      "device knuckles { };", "", NULL, 1, "index" },
};

static void
checkRows (void)
{
    for (size_t i = 0; i < sizeof(ROWS) / sizeof(ROWS[0]); i++)
    {
        const Row &r = ROWS[i];
        thcHidBlock blk;
        thhMapper m;
        string err, why;
        int errLine = -1;

        if (!thcHidParseProfile(r.profile, blk, err, errLine))
        {
            report(r.name, "the profile doesn't parse: " + err);
            continue;
        }

        std::vector<thhAction> none;
        const bool ok = m.configure(blk, thhIndexControls(), none, err,
                                    errLine);

        if (r.want && !ok)
            why = "refused: " + std::to_string(errLine) + ": " + err;
        else if (r.want)
        {
            const string got = play(m, thhIndexControls(), r.script);

            if (got != r.want)
                why = "asked for\n    " + got + "\n  wanted\n    " + r.want;
        }
        else if (ok)
            why = "accepted";
        else if (errLine != r.errLine || err.find(r.errHas) == string::npos)
            why = "refused at " + std::to_string(errLine) + " with \"" + err +
                  "\"; wanted line " + std::to_string(r.errLine) + " and \"" +
                  r.errHas + "\"";

        report(r.name, why);
    }
}

/* configure() on a mapper already playing: a new profile lets go of what
   the old one held, and a refused one leaves the old one playing. */
static void
checkReconfigure (void)
{
    const thhControls &c = thhIndexControls();
    const char *profiles[] = {
        "device index { b = note 0; };",
        "device index { b = note 0; zz = note 1; };",
    };
    const char *want[] = {
        "off 1 60 | ",
        " | off 1 60",
    };
    const char *names[] = {
        "a new profile lets go of a held note",
        "a refused profile leaves the old one playing",
    };

    for (int i = 0; i < 2; i++)
    {
        thcHidBlock blk, next;
        thhMapper m;
        thhState s;
        std::vector<thhAction> acts;
        string err;
        int errLine;

        thcHidParseProfile(NOTES, blk, err, errLine);
        thcHidParseProfile(profiles[i], next, err, errLine);
        m.configure(blk, c, acts, err, errLine);

        s.connected = true;
        s.buttons = 1u << c.button("a");
        m.update(s, 0, acts);
        acts.clear();

        m.configure(next, c, acts, err, errLine);
        string got = describe(acts) + " | ";

        acts.clear();
        s.buttons = 0;
        m.update(s, 0, acts);
        got += describe(acts);

        report(names[i], got == want[i] ? "" :
               "asked for \"" + got + "\", wanted \"" + want[i] + "\"");
    }
}

static void
checkValues (void)
{
    struct V
    {
        const char *name;
        double      t;
        bool        hasMin, hasMax;
        double      min, max;
        bool        exp;
        double      argMin, argMax, want;
    };
    static const V VALUES[] = {
        { "the profile's range",      0.25, true,  true,  200, 8000, false,
          0, 1, 2150 },
        { "an exp shape",             0.5,  true,  true,  100, 10000, true,
          0, 1, 1000 },
        { "the arg's own range",      0.3,  false, false, 0, 0, false,
          0, 10, 3 },
        { "half of each",             0.5,  false, true,  0, 5, false,
          1, 10, 3 },
        { "exp through 0, as linear", 0.5,  false, false, 0, 0, true,
          0, 10, 5 },
    };

    for (size_t i = 0; i < sizeof(VALUES) / sizeof(VALUES[0]); i++)
    {
        const V &v = VALUES[i];
        thhAction a;

        a.kind = thhAction::VALUE;
        a.t = v.t;
        a.hasMin = v.hasMin;
        a.hasMax = v.hasMax;
        a.min = v.min;
        a.max = v.max;
        a.exp = v.exp;

        const double got = thhActionValue(a, v.argMin, v.argMax);

        report(v.name, std::fabs(got - v.want) < 1e-9 ? "" :
               "got " + std::to_string(got) + ", wanted " +
               std::to_string(v.want));
    }
}

int
main (void)
{
    checkRows();
    checkReconfigure();
    checkValues();

    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");

    return failed;
}
