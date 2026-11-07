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
 * hidcamcheck -- a webcam's motion as a controller (hid/thhCamera.h).
 *
 * Each row is a script of frames, 1/120 s apart unless one says when:
 *
 *   z7=0.3      zone 7's level
 *   m=0.1       the motion
 *   @25         this frame is 25 ms after the first
 *   gone        the camera went away
 *
 * and the states each frame gave, joined with ", " within a frame and
 * " | " between frames: the buttons down, joined with "+", or "-", and the
 * strike; "gone" for a camera that isn't there.
 */

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#include "thcHidBlock.h"
#include "thhCamera.h"
#include "thhMapper.h"

using std::string;

static int failed = 0;

static void
report (const char *name, const string &got, const string &want)
{
    if (got == want)
        printf("ok    %s\n", name);
    else
    {
        printf("FAIL  %s\n  got  %s\n  want %s\n", name, got.c_str(), want.c_str());
        failed++;
    }
}

/* Feeds a script's frames; each frame's states go to each(). */
template <typename Each>
static void
frames (const string &script, Each each)
{
    thhCamera cam;
    std::istringstream steps(script);
    string step;

    for (int n = 0; std::getline(steps, step, ';'); n++)
    {
        std::istringstream w(step);
        string tok;
        float axes[thMotion::AXES] = {};
        double seconds = n / 120.0;
        bool connected = true;

        while (w >> tok)
            if (tok == "gone")
                connected = false;
            else if (tok[0] == '@')
                seconds = atof(tok.c_str() + 1) / 1000;
            else if (tok[0] == 'z')
                axes[thMotion::ZONE1 + atoi(tok.c_str() + 1) - 1] = atof(tok.c_str() + 3);
            else if (tok[0] == 'm')
                axes[thMotion::MOTION] = atof(tok.c_str() + 2);

        std::vector<thhState> out;

        cam.update(axes, seconds, connected, out);
        each(n, out);
    }
}

static string
describe (const thhState &s)
{
    if (!s.connected)
        return "gone";

    const thhControls &c = thhCameraControls();
    string down;
    char strike[16];

    for (size_t b = 0; b < c.buttons.size(); b++)
        if (s.buttons & (1u << b))
            down += (down.empty() ? "" : "+") + c.buttons[b];
    snprintf(strike, sizeof(strike), " %.2f", s.axis[THH_CAMERA_STRIKE]);

    return (down.empty() ? "-" : down) + strike;
}

static void
states (void)
{
    static const struct { const char *name, *script, *want; } rows[] = {
        { "a strike presses at its peak",
          "z7=0.3; z7=0.45; z7=0.4; z7=0.05",
          "- 0.00 | - 0.00 | hit_7 0.80 | - 0.80" },
        { "a strike still rising presses when the wait is up",
          "@0 z7=0.3; @10 z7=0.35; @20 z7=0.4; @30 z7=0.45",
          "- 0.00 | - 0.00 | - 0.00 | hit_7 0.80" },
        { "a strike past full is full",
          "z7=0.3; z7=0.9; z7=0.8",
          "- 0.00 | - 0.00 | hit_7 1.00" },
        { "a strike that crosses at full presses at once",
          "z7=0.6; z7=0.9",
          "hit_7 1.00 | hit_7 1.00" },
        { "the wait is the same three frames wherever a 120 fps strike starts",
          "; ; z7=0.26; z7=0.27; z7=0.28; z7=0.29; z7=0.30",
          "- 0.00 | - 0.00 | - 0.00 | - 0.00 | - 0.00 | hit_7 0.16 | hit_7 0.16" },
        { "a stamp before the crossing ends the wait",
          "@1000 z7=0.3; @0 z7=0.35",
          "- 0.00 | hit_7 0.40" },
        { "a zone that falls away while rising still strikes, then lifts",
          "z7=0.3; z7=0.05; z7=0.05",
          "- 0.00 | hit_7 0.20 | - 0.20" },
        { "a lift and a strike in one frame",
          "z7=0.6; z7=0.6 z9=0.3; z7=0.05 z9=0.2",
          "hit_7 1.00 | hit_7 1.00 | hit_9 0.20" },
        { "two zones struck at once are each as loud as their own",
          "z1=0.3 z9=0.3; z1=0.35 z9=0.5; z1=0.3 z9=0.4",
          "- 0.00 | - 0.00 | hit_1 0.40, hit_1+hit_9 1.00" },
        { "a hit stays down until its zone is under HIT_OFF",
          "z7=0.3; z7=0.5; z7=0.4; z7=0.15; z7=0.2; z7=0.05",
          "- 0.00 | - 0.00 | hit_7 1.00 | hit_7 1.00 | hit_7 1.00 | - 1.00" },
        { "a zone under HIT_ON never strikes",
          "z5=0.2; z5=0.24; z5=0.2",
          "- 0.00 | - 0.00 | - 0.00" },
        { "moving is down over MOVING_ON and up under MOVING_OFF",
          "m=0.04; m=0.06; m=0.03; m=0.01",
          "- 0.00 | moving 0.00 | moving 0.00 | - 0.00" },
        { "a camera gone lets every button up, and one back starts afresh",
          "z7=0.3 m=0.2; z7=0.5 m=0.2; z7=0.4 m=0.2; gone; z7=0.4",
          "moving 0.00 | moving 0.00 | hit_7+moving 1.00 | gone | - 0.00" },
        { "a zone rising when the camera goes is forgotten",
          "z7=0.3; gone; z7=0.05; z7=0.05",
          "- 0.00 | gone | - 0.00 | - 0.00" },
    };

    for (const auto &r : rows)
    {
        string got;

        frames(r.script, [&] (int n, const std::vector<thhState> &out) {
            got += n ? " | " : "";
            for (size_t i = 0; i < out.size(); i++)
                got += (i ? ", " : "") + describe(out[i]);
        });
        report(r.name, got, r.want);
    }
}

/* The default profile, played through the mapper. */
static void
playing (void)
{
    thcHidBlock profile;
    thhMapper mapper;
    std::vector<thhAction> acts;
    string err;
    int line = 0;
    const bool parsed = thcHidParseProfile(THH_CAMERA_PROFILE, profile, err, line);
    const bool bound = parsed && mapper.configure(profile, thhCameraControls(), acts, err, line);

    report("the camera's own profile binds", bound ? "" : err, "");

    string got;

    frames("z7=0.3 z3=0.3; z7=0.5 z3=0.35; z7=0.4 z3=0.3; z7=0.05 z3=0.3; z3=0.05",
           [&] (int n, const std::vector<thhState> &out) {
        got += n ? " | " : "";
        acts.clear();
        for (const auto &s : out)
            mapper.update(s, 0, acts);
        for (size_t i = 0; i < acts.size(); i++)
        {
            const thhAction &a = acts[i];

            got += i ? ", " : "";
            got += (a.kind == thhAction::NOTE_ON ? "on " : "off ") + std::to_string(a.note);
            if (a.kind == thhAction::NOTE_ON)
                got += " " + std::to_string(a.vel);
        }
    });
    report("each zone plays its note as loud as it was struck", got,
           " |  | on 79 75, on 60 127 | off 60 | off 79");
}

int
main (void)
{
    states();
    playing();

    if (failed)
        printf("%d failed\n", failed);

    return failed ? 1 : 0;
}
