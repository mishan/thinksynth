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

#include "thhIndex.h"
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

/* Runs a script from where s and ch stand; on a step it can't read, says so
   in place of the result. */
static string
play (thhMapper &m, const thhControls &c, const string &script, thhState &s,
      int &ch)
{
    std::istringstream steps(script);
    string step, out;

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
#define LAYERS \
    "device index { a = note 0; b = note 2; grip_mid = \"cutoff\"; " \
    "thumbstick = hold mod; system = toggle drone; " \
    "layer mod { a = octave -1; b = note 7; grip_mid = \"resonance\"; }; " \
    "layer drone { channel = 2; base = \"C3\"; }; };"
#define BOTH \
    "device index { b = note 2; thumbstick = hold mod; system = toggle drone; " \
    "layer mod { b = note 7; }; layer drone { b = note 4; }; };"
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
    { "the pitch axis in semitones, on a grip zone",
      "device index { a = note 0; grip_mid = pitch { range = 12; }; };",
      "axis grip_mid 0.5; down a; up a", " | on 1 66 100 | off 1 66", 0, NULL },
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
    { "a value along a grip zone, past a deadband",
      "device index { grip_mid = \"cutoff\" { min = 200; max = 8000; }; };",
      "axis grip_mid 0.5; axis grip_mid 0.5; axis grip_mid 0.501; axis grip_mid 1",
      "cutoff@1 0.500 |  |  | cutoff@1 1.000", 0, NULL },
    { "a bipolar value from the middle",
      "device index { stick_x = \"pan\"; };",
      "axis stick_x 0; axis stick_x -1", "pan@1 0.500 | pan@1 0.000", 0, NULL },
    { "a value on the current channel",
      "device index { grip_mid = \"cutoff\"; };",
      "ch 2; axis grip_mid 1", "cutoff@2 0.000 | cutoff@2 1.000", 0, NULL },
    { "a value follows the current channel when it changes",
      "device index { grip_mid = \"cutoff\"; };",
      "axis grip_mid 0.5; ch 2", "cutoff@1 0.500 | cutoff@2 0.500", 0, NULL },
    { "an unplug ends held notes", NOTES,
      "down a; unplug; plug; up a",
      "on 1 60 100 | off 1 60 | on 1 60 100 | off 1 60", 0, NULL },
    { "a release ends held notes, and they don't sound again", NOTES,
      "down a; release; up a; down a",
      "on 1 60 100 | off 1 60 |  | on 1 60 100", 0, NULL },

    { "a channel past 16 is channel 1", NOTES,
      "ch 17; down a; up a", " | on 1 60 100 | off 1 60", 0, NULL },
    { "a slow creep reaches the end",
      "device index { grip_mid = \"cutoff\"; };",
      "axis grip_mid 0.997; axis grip_mid 0.999; axis grip_mid 1",
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

    { "a hold swaps a button's note", LAYERS,
      "down thumbstick; down b; up b; up thumbstick; down b; up b",
      "resonance@1 0.000 | on 1 67 100 | off 1 67 | cutoff@1 0.000 | "
      "on 1 62 100 | off 1 62",
      0, NULL },
    { "a layer's octave step", LAYERS,
      "down thumbstick; down a; up a; up thumbstick; down a; up a",
      "resonance@1 0.000 |  |  | cutoff@1 0.000 | on 1 48 100 | off 1 48",
      0, NULL },
    { "a release undoes its press when a layer comes between", LAYERS,
      "down b; down thumbstick; up b; up thumbstick",
      "on 1 62 100, cutoff@1 0.000 | resonance@1 0.000 | off 1 62 | "
      "cutoff@1 0.000", 0, NULL },
    { "and when one goes", LAYERS,
      "down thumbstick; down b; up thumbstick; up b",
      "resonance@1 0.000 | on 1 67 100 | cutoff@1 0.000 | off 1 67", 0, NULL },
    { "a toggle latches a layer's channel and base", LAYERS,
      "down system; up system; down a; up a; down system; up system; "
      "down a; up a",
      "cutoff@2 0.000 |  | on 2 48 100 | off 2 48 | cutoff@1 0.000 |  | "
      "on 1 60 100 | off 1 60", 0, NULL },
    { "the newest layer wins", BOTH,
      "down system; up system; down thumbstick; down b; up b; up thumbstick; "
      "down b; up b",
      " |  |  | on 1 67 100 | off 1 67 |  | on 1 64 100 | off 1 64",
      0, NULL },
    { "the newest layer wins, the other way round", BOTH,
      "down thumbstick; down system; up system; down b; up b",
      " |  |  | on 1 64 100 | off 1 64", 0, NULL },
    { "a value jumps to the layer's binding and back", LAYERS,
      "axis grip_mid 0.5; down thumbstick; up thumbstick",
      "cutoff@1 0.500 | resonance@1 0.500 | cutoff@1 0.500", 0, NULL },
    { "a hold and a toggle of one layer",
      "device index { b = note 2; thumbstick = hold mod; system = toggle mod; "
      "layer mod { b = note 7; }; };",
      "down system; up system; down thumbstick; up thumbstick; down b; up b",
      " |  |  |  | on 1 67 100 | off 1 67", 0, NULL },
    { "a release drops a hold", LAYERS,
      "down thumbstick; release; down b; up b",
      "resonance@1 0.000 |  | on 1 62 100, cutoff@1 0.000 | off 1 62",
      0, NULL },
    { "a release keeps a toggle", LAYERS,
      "down system; up system; release; down a; up a",
      "cutoff@2 0.000 |  |  | on 2 48 100 | off 2 48", 0, NULL },
    { "a hold comes back with the controller", LAYERS,
      "down thumbstick; unplug; plug; down b; up b",
      "resonance@1 0.000 |  |  | on 1 67 100 | off 1 67", 0, NULL },
    { "two holds let go in the order they were pressed",
      "device index { b = note 2; thumbstick = hold mod; trackpad_touch = hold drone; "
      "layer mod { b = note 7; }; layer drone { b = note 4; }; };",
      "down thumbstick; down trackpad_touch; up thumbstick; down b; up b",
      " |  |  | on 1 64 100 | off 1 64", 0, NULL },
    { "a toggle stays on through an unplug", LAYERS,
      "down system; up system; unplug; plug; down a; up a",
      "cutoff@2 0.000 |  |  |  | on 2 48 100 | off 2 48", 0, NULL },
    { "a note ends on its own channel when a layer changes it", LAYERS,
      "down a; down system; up a",
      "on 1 60 100, cutoff@1 0.000 | cutoff@2 0.000 | off 1 60", 0, NULL },
    { "a layer's velocity source",
      "device index { a = note 0; thumbstick = hold soft; "
      "layer soft { trigger = velocity { min = 10; max = 20; }; }; };",
      "axis trigger 1; down a; up a; down thumbstick; down a; up a",
      " | on 1 60 100 | off 1 60 |  | on 1 60 20 | off 1 60", 0, NULL },
    { "an octave step up never goes down",
      "device index { a = note 0; system = toggle low; b = octave 1; "
      "layer low { base = \"C1\"; }; };",
      "down system; up system; down b; up b; down b; up b; down b; up b; "
      "down b; up b; down b; up b; down b; up b; down b; up b; down b; up b; "
      "down system; up system; down b; up b; down a; up a",
      " |  |  |  |  |  |  |  |  |  |  |  |  |  "
      "|  |  |  |  |  |  |  |  |  | ", 0, NULL },
    { "a layer's pitch axis over the base's",
      "device index { a = note 0; thumbstick = hold mod; "
      "stick_y = pitch { step = octave; range = 1; }; "
      "layer mod { pad_y = pitch { step = octave; range = 1; }; }; };",
      "axis stick_y 1; down a; up a; down thumbstick; down a; up a",
      " | on 1 72 100 | off 1 72 |  | on 1 60 100 | off 1 60", 0, NULL },

    { "a control the device doesn't have",
      "device index {\n  zz = note 0;\n};", "", NULL, 2, "'zz'" },
    { "a button bound to a value",
      "device index { a = \"cutoff\"; };", "", NULL, 1, "is a button" },
    { "an axis bound to a note",
      "device index { grip_mid = note 0; };", "", NULL, 1, "is an axis" },
    { "two pitch axes",
      "device index {\n  stick_y = pitch { range = 1; };\n"
      "  pad_y = pitch { range = 1; };\n};", "", NULL, 3, "one pitch axis" },
    { "a layer's control the device doesn't have",
      "device index {\n  layer mod {\n    zz = note 0;\n  };\n};",
      "", NULL, 3, "'zz'" },
    { "two pitch axes in one layer",
      "device index {\n  layer mod {\n    stick_y = pitch { range = 1; };\n"
      "    pad_y = pitch { range = 1; };\n  };\n};", "", NULL, 4,
      "one pitch axis" },
    { "a layer rebinding a modifier",
      "device index {\n  system = toggle drone;\n  layer drone {\n"
      "    system = note 5;\n  };\n};", "", NULL, 4, "switches a layer" },
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
            thhState s;
            int ch = 0;

            s.connected = true;

            const string got = play(m, thhIndexControls(), r.script, s, ch);

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

/* configure() on a mapper already playing: a new profile lets go of the
   notes the old one held but keeps the layers the hand still holds, and a
   refused one leaves the old one playing. Each row plays `before', applies
   `next', then plays `after'; what configure asked for comes between the
   two, in `||'. */
static void
checkReconfigure (void)
{
    struct R
    {
        const char *name, *first, *before, *next, *after, *want;
    };
    static const R ROWS2[] = {
        { "a new profile lets go of a held note",
          NOTES, "down a", "device index { b = note 0; };", "up a",
          "on 1 60 100 || off 1 60 || " },
        { "a refused profile leaves the old one playing",
          NOTES, "down a", "device index { b = note 0; zz = note 1; };",
          "up a", "on 1 60 100 ||  || off 1 60" },
        { "a held hold survives an edit",
          LAYERS, "down thumbstick", LAYERS, "down b; up b",
          "resonance@1 0.000 ||  || on 1 67 100, resonance@1 0.000 | "
          "off 1 67" },
        { "a toggle whose button is gone doesn't survive an edit",
          LAYERS, "down system; up system",
          "device index { a = note 0; thumbstick = hold drone; "
          "layer drone { channel = 2; }; };",
          "down a; up a", "cutoff@2 0.000 |  ||  || on 1 60 100 | off 1 60" },
        { "an edit keeps the newest layer newest",
          BOTH, "down thumbstick; down system; up system", BOTH,
          "down b; up b", " |  |  ||  || on 1 64 100 | off 1 64" },
        { "a toggle survives an edit",
          LAYERS, "down system; up system", LAYERS, "down a; up a",
          "cutoff@2 0.000 |  ||  || on 2 48 100, cutoff@2 0.000 | off 2 48" },
    };
    const thhControls &c = thhIndexControls();

    for (size_t i = 0; i < sizeof(ROWS2) / sizeof(ROWS2[0]); i++)
    {
        const R &r = ROWS2[i];
        thcHidBlock blk, next;
        thhMapper m;
        thhState s;
        std::vector<thhAction> acts;
        string err;
        int errLine, ch = 0;

        s.connected = true;
        thcHidParseProfile(r.first, blk, err, errLine);
        thcHidParseProfile(r.next, next, err, errLine);
        m.configure(blk, c, acts, err, errLine);

        string got = play(m, c, r.before, s, ch);

        acts.clear();
        m.configure(next, c, acts, err, errLine);
        got += " || " + describe(acts) + " || " + play(m, c, r.after, s, ch);

        report(r.name, got == r.want ? "" :
               "asked for\n    " + got + "\n  wanted\n    " + r.want);
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
