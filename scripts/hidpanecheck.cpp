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
 * hidpanecheck -- what the controller pane says about a profile, held still.
 *
 * Each row is a profile, a controller state and the keyboard's channel, and
 * the rows the pane shows for them, one per line as describe() writes them:
 * layer, control, what it does, where, its value now, and a warning.
 * Channel 2's patch is the only one with knobs: cutoff and resonance.
 */

#include <cstdio>
#include <string>

#include "thhIndex.h"
#include "thhPane.h"

using std::string;

static int failed = 0;

static string
describe (const std::vector<thhPaneRow> &rows)
{
    string s;

    for (size_t i = 0; i < rows.size(); i++)
    {
        const thhPaneRow &r = rows[i];

        s += (r.patch ? "patch " : r.yours ? "" : "own ") +
             (r.layer.empty() ? "" : r.layer + ": ") + r.control + " | " +
             r.action + " | " + r.target + " | " + r.value +
             (r.warning.empty() ? "" : " | ! " + r.warning) + "\n";
    }
    return s;
}

struct Row
{
    const char *name, *profile;
    const char *down;       /* a button held, or NULL */
    const char *axis;       /* an axis moved, or NULL */
    float       to;
    int         channel;    /* the keyboard's, 0-based */
    const char *want;
};

static const Row ROWS[] = {
    { "notes, on the keyboard's channel and their own",
      "device index { a = note 0 { latch; }; b = note -12 { channel = 2; }; };",
      "a", NULL, 0, 3,
      "a | note +0, latched | the keyboard's channel (4) | down\n"
      "b | note -12 | channel 2 | up\n" },
    { "the profile's channel over the keyboard's",
      "device index { channel = 2; a = note 4; };", NULL, NULL, 0, 0,
      "a | note +4 | channel 2 | up\n" },
    { "the controller's own bindings, not the player's",
      "device index { a = note 4; };", "own", NULL, 0, 0,
      "own a | note +4 | the keyboard's channel (1) | up\n" },
    { "a patch's bindings, not the player's",
      "device index { a = note 4; };", "patch", NULL, 0, 0,
      "patch a | note +4 | the keyboard's channel (1) | up\n" },
    { "modifiers and octaves",
      "device index { system = toggle drone; thumbstick = hold mod; "
      "b = octave -1; layer mod { }; layer drone { }; };", NULL, NULL, 0, 0,
      "system | toggle layer drone |  | up\n"
      "thumbstick | hold layer mod |  | up\n"
      "b | octave -1 |  | up\n" },
    { "a knob, there and not",
      "device index { trigger = \"cutoff\" { min = 200; max = 8000; "
      "shape = exp; smooth = 0.05; }; grip_mid = \"wobble\" { channel = 2; }; };",
      NULL, "trigger", 0.5f, 1,
      "trigger | knob cutoff, 200 to 8000, exp, smoothed over 0.05 s | "
      "the keyboard's channel (2) "
      "| 0.50\n"
      "grip_mid | knob wobble | channel 2 | 0.00 | ! channel 2's patch has "
      "no knob 'wobble'\n" },
    { "pitch and velocity",
      "device index { stick_y = pitch { step = octave; range = 2; }; "
      "pad_y = velocity { min = 30; max = 127; }; };",
      NULL, "stick_y", -1, 0,
      "stick_y | pitch, 2 octaves either way |  | -1.00\n"
      "pad_y | velocity, 30 to 127 |  | 0.00\n" },
    { "a pitch axis on a grip zone only goes up",
      "device index { grip_top = pitch { range = 7; }; };", NULL, NULL, 0, 0,
      "grip_top | pitch, 7 semitones up |  | 0.00\n" },
    { "a pitch axis on a scale counts its degrees",
      "device index { tilt = pitch { scale = blues; range = 1; }; };", NULL,
      NULL, 0, 0, "tilt | pitch, 1 degree of a scale either way |  | 0.00\n" },
    { "loops, and where a record plays",
      "device index { a = loop record { channel = 2; }; "
      "twist = loop clear { at = 0.7; }; };", NULL, NULL, 0, 0,
      "a | loop record | channel 2 | up\n"
      "twist | loop clear at 0.7 |  | 0.00\n" },
    { "a layer's rows, with its channel",
      "device index { thumbstick = hold mod; a = note 0; "
      "layer mod { channel = 2; a = note 7; grip_mid = \"resonance\"; }; };",
      NULL, NULL, 0, 0,
      "thumbstick | hold layer mod |  | up\n"
      "a | note +0 | the keyboard's channel (1) | up\n"
      "mod: a | note +7 | channel 2 | up\n"
      "mod: grip_mid | knob resonance | channel 2 | 0.00\n" },
};

int
main (void)
{
    const thhControls &c = thhIndexControls();
    const auto hasArg = [](int ch, const string &arg) {
        return ch == 1 && (arg == "cutoff" || arg == "resonance");
    };

    for (size_t i = 0; i < sizeof(ROWS) / sizeof(ROWS[0]); i++)
    {
        const Row &r = ROWS[i];
        thcHidBlock blk;
        thhState s;
        string err;
        int line;

        if (!thcHidParseProfile(r.profile, blk, err, line))
        {
            printf("FAIL  %s\n  doesn't parse: %s\n", r.name, err.c_str());
            failed++;
            continue;
        }

        s.connected = true;
        if (r.down && c.button(r.down) >= 0)
            s.buttons = 1u << c.button(r.down);
        if (r.axis)
            s.axis[c.axis(r.axis)] = r.to;

        if (r.down && string(r.down) == "own")
            for (size_t k = 0; k < blk.bindings.size(); k++)
                blk.bindings[k].line = -blk.bindings[k].line;
        if (r.down && string(r.down) == "patch")
            for (size_t k = 0; k < blk.bindings.size(); k++)
                blk.bindings[k].patch = true;

        const string got = describe(thhPaneRows(blk, c, s, r.channel, hasArg));

        if (got == r.want)
            printf("ok    %s\n", r.name);
        else
        {
            printf("FAIL  %s\n  shows\n%s  wanted\n%s", r.name, got.c_str(),
                   r.want);
            failed++;
        }
    }

    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");

    return failed;
}
