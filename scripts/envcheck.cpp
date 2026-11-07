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
 * envcheck -- envelopes whose segments outlast a float's count.
 *
 * A float counts samples one by one only to 2^24: 16.8 s at a 1 MHz pixel
 * clock, 6.3 minutes at 44.1 kHz. Each row is a 20 s decay at 1 MHz, which
 * must keep falling past that and then settle: hold, or for env::adsfr
 * fall off from its sustain. Windows of two sizes must give the same
 * samples, since a window's end is where the count is put away; one
 * of them odd, which a float past 2^24 cannot hold.
 */

#include "config.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "think.h"
#include "thSynth.h"

using std::string;

static const int RATE = 1000000;

int
main (int argc, char *argv[])
{
    string plugins = PLUGIN_PATH;

    if (argc > 2 && !strcmp(argv[1], "-p"))
        plugins = argv[2];
    if (plugins.back() != '/')
        plugins += '/';

    static const struct { const char *name, *env; bool sustains; } rows[] = {
        { "env::adsr, free running",
          "env::adsr { a = 0; d = 20 s; s = 0.25; r = 0; p = 1; trigger = 1; }", true },
        { "env::adsr, gated",
          "env::adsr { a = 0; d = 20 s; s = 0.25; r = 0; p = 1; trigger = ionode->trigger; }", true },
        { "env::ad",
          "env::ad { a = 0; d = 20 s; p = 1; }", false },
        { "env::adsfr",
          "env::adsfr { a = 0; d = 20 s; s = 0.25; f = 1000 s; r = 0; p = 1; trigger = ionode->trigger; }", true },
    };
    int failed = 0;

    /* The level at the end of each second, in windows of `window'. */
    const auto levels = [&](const char *env, int window, float *at) {
        thSynth synth(plugins, window, RATE);
        const string text = string("node ionode { channels = 1; out0 = e->out; play = 1; };\n"
                                   "node e ") + env + ";\nio ionode;\n";

        if (synth.loadTreeText("envcheck", text, 0, MIDIVALMAX) == NULL)
            return false;
        synth.addNote(0, 60, MIDIVALMAX);
        for (int s = 1; s < 24; s++)
        {
            for (int w = 0; w < RATE / window; w++)
                synth.process();
            at[s] = synth.getOutput()[window - 1];
        }
        return true;
    };

    for (const auto &r : rows)
    {
        float at[24], other[24];

        if (!levels(r.env, 10000, at) || !levels(r.env, 15625, other))
        {
            printf("FAIL  %s\n  does not load\n", r.name);
            failed++;
            continue;
        }

        bool falls = true;

        for (int s = 2; s < 20; s++)
            falls = falls && at[s] < at[s - 1];

        const bool settles = r.sustains ? at[22] >= at[23] && at[22] > 0 && at[22] < at[19]
                                     : at[22] == 0 && at[23] == 0;
        const bool alike = !memcmp(at + 1, other + 1, 23 * sizeof(float));
        char got[160];

        snprintf(got, sizeof(got), "1 s %.4f, 14 s %.4f, 17 s %.4f, 19 s %.4f, 22 s %.4f",
                 at[1], at[14], at[17], at[19], at[22]);
        if (falls && settles && alike)
            printf("ok    %s keeps falling for 20 s, then %s: %s\n", r.name,
                   r.sustains ? "sustains" : "ends", got);
        else
        {
            printf("FAIL  %s:%s%s%s\n  %s\n", r.name, falls ? "" : " stops falling",
                   settles ? "" : " does not settle",
                   alike ? "" : " differs by window size", got);
            failed++;
        }
    }

    return failed ? 1 : 0;
}
