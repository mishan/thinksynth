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
 * sessioncheck -- one controller or a pair, played as the desktop and the
 * browser play them: hands given, a pair made and undone, its hands
 * swapped and remembered, bends of one note from both hands added, and
 * everything held let go of. A session fed states, and what it asks to
 * play.
 */

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "thhIndex.h"
#include "thhSession.h"

using std::string;

static int failed = 0;

static void
report (const string &name, const string &why)
{
    if (why.empty())
        printf("ok    %s\n", name.c_str());
    else
    {
        printf("FAIL  %s\n  %s\n", name.c_str(), why.c_str());
        failed++;
    }
}

/* A session and what it played, as "on 60", "off 48", "bend 60 4.00". */
struct Rig
{
    std::vector<string>               played;
    std::map<string, string>          kept;
    double                            t = 100;
    thhSession                        session;

    Rig ()
        : session([this](const std::vector<thhAction> &acts) {
                      char s[64];

                      for (const thhAction &a : acts)
                      {
                          if (a.kind == thhAction::NOTE_ON)
                              snprintf(s, sizeof(s), "on %d", a.note);
                          else if (a.kind == thhAction::NOTE_OFF)
                              snprintf(s, sizeof(s), "off %d", a.note);
                          else if (a.kind == thhAction::NOTE_BEND)
                              snprintf(s, sizeof(s), "bend %d %.2f", a.note, a.t);
                          else
                              continue;
                          played.push_back(s);
                      }
                  },
                  [this]() { return t; },
                  [this](const std::map<string, string> &k) { kept = k; })
    {
        string err;

        session.load("", "", "", "", err);
    }

    /* Slot i's controller as it is now: connected, these buttons down,
       these axes moved. */
    void be (int i, const string &said, uint32_t buttons,
             const std::map<int, float> &axes = {})
    {
        thhState s;

        s.connected = true;
        s.buttons = buttons;
        for (const auto &a : axes)
            s.axis[a.first] = a.second;
        t += 0.01;
        s.time = t;
        session.update(i, {}, s, said, 0);
    }

    void gone (int i)
    {
        thhState s;

        t += 0.01;
        s.time = t;
        session.update(i, { s }, s, "", 0);
    }

    string take ()
    {
        string all;

        for (const string &p : played)
            all += (all.empty() ? "" : ", ") + p;
        played.clear();
        return all;
    }
};

#define A (1u << THH_INDEX_A)

int
main (void)
{
    {
        Rig r;
        const int right = r.session.add("LHR-R");

        r.be(right, "right", 0);
        r.be(right, "right", A);

        string played = r.take();

        report("one controller alone plays the profile for either hand",
               r.session.handOf(right) == "" && played == "on 60" ? "" :
               "hand \"" + r.session.handOf(right) + "\", played " + played);

        const int left = r.session.add("LHR-L");

        r.be(left, "left", 0);
        report("a second one makes a pair, each its own hand",
               r.session.handOf(right) == "right" &&
               r.session.handOf(left) == "left" ? "" :
               r.session.handOf(right) + ", " + r.session.handOf(left));

        r.take();
        r.be(left, "left", A);
        played = r.take();
        report("in a pair the left hand plays its own block",
               played == "on 48" ? "" : "played " + played);

        r.gone(left);
        played = r.take();
        report("a hand going lets go of its notes, and the other plays alone",
               played == "off 48" && r.session.handOf(right) == "" ? "" :
               "played " + played + ", the other's hand \"" +
               r.session.handOf(right) + "\"");
    }

    {
        Rig r;
        const int one = r.session.add("LHR-1"), two = r.session.add("LHR-2");

        r.be(one, "", 0);
        r.be(two, "", 0);
        report("a pair that says no hands can be swapped",
               r.session.canSwapHands() ? "" : "it can't");

        const string before = r.session.handOf(one);

        r.session.swapHands();
        report("swapped, each plays the other's hand, remembered by serial",
               r.session.handOf(one) != before && !before.empty() &&
               r.kept["LHR-1"] == r.session.handOf(one) &&
               r.kept["LHR-2"] == r.session.handOf(two) ? "" :
               "was " + before + ", now " + r.session.handOf(one) +
               ", kept " + r.kept["LHR-1"] + "/" + r.kept["LHR-2"]);

        Rig again;

        again.session.remembered(r.kept);

        const int a = again.session.add("LHR-1"), b = again.session.add("LHR-2");

        again.be(a, "", 0);
        again.be(b, "", 0);
        report("hands remembered are given again",
               !again.session.handOf(a).empty() &&
               again.session.handOf(a) == r.session.handOf(one) ? "" :
               again.session.handOf(a) + " not " + r.session.handOf(one));
    }

    {
        /* The right hand holds a note and bends it; the left bends both
           hands' notes; the note's bend is the two added. */
        Rig r;
        string err;
        const bool loaded = r.session.load(
            "device index right { stick_x = bend { range = 2; }; };\n"
            "device index left { stick_x = bend { range = 2; of = both; }; };\n",
            "hid/index", "", "", err);
        const int right = r.session.add("LHR-R"), left = r.session.add("LHR-L");

        r.be(right, "right", 0);
        r.be(left, "left", 0);
        r.be(right, "right", A);
        r.be(right, "right", A, { { THH_INDEX_STICK_X, 1 } });
        r.be(left, "left", 0, { { THH_INDEX_STICK_X, 1 } });

        const string played = r.take();

        report("both hands' bends of one note add",
               loaded && played.find("bend 60 4.00") != string::npos ? "" :
               (loaded ? "played " + played : err));
    }

    {
        Rig r;
        const int right = r.session.add("LHR-R");

        r.be(right, "right", 0);
        r.be(right, "right", A);
        r.take();
        r.session.release();

        const string played = r.take();

        report("released, every held note ends", played == "off 60" ? "" :
               "played " + played);
    }

    {
        /* Another patch with the same block plays on, and is named. */
        Rig r;
        string err;

        r.session.load("", "", "device index { b = note 3; };\n", "rhodes",
                       err);

        const unsigned before = r.session.generation();

        r.session.rename("glass");
        report("another patch with the same block is named",
               r.session.patch().find("glass") != string::npos &&
               r.session.generation() != before ? "" :
               "said \"" + r.session.patch() + "\"");
    }

    {
        /* B starts a loop and ends it; what A played between plays again,
           on its own, once its time comes round. */
        Rig r;
        string err;

        r.session.load("device index { b = loop record; };\n", "hid/index",
                       "", "", err);

        const int right = r.session.add("LHR-R");

        /* A tenth of a second a step: a loop shorter than a twentieth is
           dropped. */
        for (uint32_t b : { 0u, 1u << THH_INDEX_B, 0u, A, 0u,
                            1u << THH_INDEX_B, 0u })
        {
            r.t += 0.1;
            r.be(right, "right", b);
        }
        r.take();
        r.t += 1;
        r.session.tick();

        const string again = r.take();

        report("a loop recorded plays again",
               r.session.looping() && again.find("on 60") != string::npos ? "" :
               "looping " + std::to_string(r.session.looping()) +
               ", played " + again);
    }

    {
        Rig r;
        string err;

        report("a profile that doesn't fit says where",
               !r.session.load("device index {\n    nonsense = note 0;\n};\n",
                               "hid/index", "", "", err) &&
               err.find("hid/index:2") == 0 ? "" : "said \"" + err + "\"");
    }

    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed;
}
