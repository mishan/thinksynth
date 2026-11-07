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
 * hidloopcheck -- loops of the notes the hands play (hid/thhLooper.h).
 *
 * Each row is a script of steps, each at a time in seconds, and what the
 * looper played at each step:
 *
 *   T on N, T off N     the hands play note N on channel 1, or let it go
 *   T bend N V          and bend it V semitones
 *   T rec C             a loop record, to play on channel C
 *   T undo, T clear     a loop undo or clear
 *   T tick              the looper plays what is due by T
 *
 * The steps are joined with " | ", so a row says when as well as what.
 * The rows of what reaches the synth say what the hands play too, and
 * what of the two thhVoices lets through.
 */

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#include "thhLooper.h"

using std::string;

static int failed = 0;

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
            snprintf(t, sizeof(t), "%+.2f", a.t);
            s << "bend " << a.channel + 1 << " " << a.note << " " << t;
        }
    }

    return s.str();
}

static string
play (const string &script, bool synth)
{
    thhLooper looper;
    thhVoices voices;
    std::istringstream steps(script);
    string step, out;

    for (int n = 0; std::getline(steps, step, ';'); n++)
    {
        std::istringstream w(step);
        double now, v = 0;
        string verb;
        int arg = 0;
        thhAction a = thhAction();
        std::vector<thhAction> acts;

        w >> now >> verb >> arg >> v;
        a.channel = 0;
        a.note = arg;
        a.vel = 100;
        a.t = v;
        if (verb == "on" || verb == "off" || verb == "bend")
        {
            a.kind = verb == "on" ? thhAction::NOTE_ON
                     : verb == "off" ? thhAction::NOTE_OFF
                                     : thhAction::NOTE_BEND;
            looper.heard(std::vector<thhAction>(1, a), now);
            if (synth)
                acts.push_back(a);
        }
        else if (verb == "rec" || verb == "undo" || verb == "clear")
        {
            a.kind = thhAction::LOOP;
            a.arg = verb == "rec" ? "record" : verb;
            a.channel = arg - 1;
            looper.act(a, now, acts);
        }
        else if (verb == "tick")
            looper.tick(now, acts);
        else
            return "a step this harness can't read: '" + step + "'";

        std::vector<thhAction> heard;

        if (synth)
            voices.pass(acts, heard);
        out += (n ? " | " : "") + describe(synth ? heard : acts);
    }

    return out;
}

struct Row
{
    const char *name;
    const char *script;
    const char *want;
    bool        synth;
};

static const Row ROWS[] = {
    { "a recording plays from its end, over and over, on its channel",
      "0 rec 2; 0.1 on 60; 0.3 off 60; 1.0 rec 2; 1.05 tick; 1.1 tick; "
      "1.3 tick; 2.1 tick; 2.3 tick",
      " |  |  |  |  | on 2 60 100 | off 2 60 | on 2 60 100 | off 2 60" },
    { "a note started as the recording starts is in its first lap",
      "0 rec 1; 0 on 60; 0.1 off 60; 0.2 rec 1; 0.25 tick",
      " |  |  |  | on 1 60 100" },
    { "a note still held at the end ends with each lap",
      "0 rec 1; 0.2 on 62; 0.5 rec 1; 0.6 tick; 0.75 tick; 1.0 tick; 1.2 tick",
      " |  |  |  | on 1 62 100 | off 1 62 | on 1 62 100" },
    { "a note started before the recording isn't in it",
      "0 on 60; 0.1 rec 1; 0.2 off 60; 0.3 on 64; 0.4 off 64; 0.6 rec 1; "
      "1.2 tick",
      " |  |  |  |  |  | on 1 64 100, off 1 64" },
    { "a recorded note's bend goes round with it",
      "0 rec 1; 0.1 on 60; 0.2 bend 60 1.5; 0.3 off 60; 0.4 rec 1; 0.75 tick",
      " |  |  |  |  | on 1 60 100, bend 1 60 +1.50, off 1 60" },
    { "undo stops the newest loop, clear the rest",
      "0 rec 1; 0.1 on 60; 0.5 rec 1; 0.5 rec 1; 0.6 on 64; 0.8 tick; "
      "1.0 rec 1; 1.1 tick; 1.15 undo; 1.2 clear",
      " |  |  |  |  | on 1 60 100 |  | off 1 60, on 1 60 100, on 1 64 100 | "
      "off 1 64 | off 1 60" },
    { "a recording too short to loop is dropped",
      "0 rec 1; 0.005 on 60; 0.01 rec 1; 0.5 tick", " |  |  | " },
    { "undo while recording drops the recording",
      "0 rec 1; 0.1 on 60; 0.2 undo; 0.3 off 60; 0.4 rec 1; 0.5 rec 1; 1.0 tick",
      " |  |  |  |  |  | " },
    { "a late tick plays the last lap, not every one missed",
      "0 rec 1; 0.01 on 60; 0.05 off 60; 0.1 rec 1; 100.0 tick",
      " |  |  |  | on 1 60 100, off 1 60" },

    /* What reaches the synth. */
    { "a note the hands play while a loop sounds it doesn't cut the loop's",
      "0 rec 1; 0.1 on 60; 0.9 off 60; 1.0 rec 1; 1.05 tick; 1.1 tick; "
      "1.3 on 60; 1.4 off 60; 1.95 tick",
      " | on 1 60 100 | off 1 60 |  |  | on 1 60 100 |  |  | off 1 60", true },
    { "a note both hands hold is recorded ending with the last of them",
      "0 rec 1; 0.1 on 60; 0.2 on 60; 0.3 off 60; 0.4 off 60; 0.5 rec 1; "
      "1.0 tick; 1.05 tick",
      " | on 1 60 100 |  |  | off 1 60 |  | on 1 60 100, off 1 60 | ", true },
    { "undoing a loop leaves another loop's same note sounding",
      "0 rec 1; 0.1 on 60; 0.9 off 60; 1.0 rec 1; 1.1 tick; 1.2 on 60; "
      "1.5 off 60; 1.95 tick; 2.0 rec 1; 2.3 tick; 2.3 undo; 2.95 tick",
      " | on 1 60 100 | off 1 60 |  | on 1 60 100 |  |  | off 1 60 |  | "
      "on 1 60 100 |  | off 1 60", true },
};

int
main ()
{
    for (const Row &r : ROWS)
    {
        const string got = play(r.script, r.synth);

        if (got == r.want)
            printf("ok    %s\n", r.name);
        else
        {
            printf("FAIL  %s\n  got  %s\n  want %s\n", r.name, got.c_str(),
                   r.want);
            failed++;
        }
    }

    printf("%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed ? 1 : 0;
}
