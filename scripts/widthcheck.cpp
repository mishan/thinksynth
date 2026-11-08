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
 * widthcheck -- a synth as many channels wide as it is made, and its
 * limiter turned off.
 *
 * Each row makes a synth of some width, plays an instrument of some width
 * whose outputs are constants, and reads the first sample of each of the
 * synth's planes back. A row with no `want' has the limiter on, and wants
 * no plane past full scale.
 */

#include "config.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "think.h"
#include "thSynth.h"

using std::string;

/* An instrument `channels' wide whose out<N> is `level' * (N + 1). */
static string
instrument (int channels, float level)
{
    string text = "node ionode { channels = " + std::to_string(channels) + "; play = 1;";

    for (int c = 0; c < channels; c++)
        text += " out" + std::to_string(c) + " = o" + std::to_string(c) + "->out;";
    text += " };\n";
    for (int c = 0; c < channels; c++)
        text += "node o" + std::to_string(c) + " math::add { in0 = " +
                std::to_string(level * (c + 1)) + "; in1 = 0; };\n";

    return text + "io ionode;\n";
}

int
main (int argc, char *argv[])
{
    string plugins = PLUGIN_PATH;

    if (argc > 2 && !strcmp(argv[1], "-p"))
        plugins = argv[2];
    if (plugins.back() != '/')
        plugins += '/';

    static const struct {
        const char *name;
        int width, instrument;
        float level;
        bool limit;
        const char *want;
    } rows[] = {
        { "a 3-wide instrument fills a 3-wide synth", 3, 3, 0.25f, false, "0.25 0.5 0.75" },
        { "a mono instrument fills the first plane", 3, 1, 0.25f, false, "0.25 0 0" },
        { "a synth drops what is wider than it", 2, 3, 0.25f, false, "0.25 0.5" },
        { "a mono synth", 1, 2, 0.25f, false, "0.25" },
        { "a width of 0 is the default 2", 0, 2, 0.25f, false, "0.25 0.5" },
        { "a width past TH_MAX_CHANNELS is the default 2", TH_MAX_CHANNELS + 1, 2, 0.25f, false,
          "0.25 0.5" },
        { "without the limiter, past full scale passes", 3, 3, 1, false, "1 2 3" },
        { "with it, nothing passes full scale", 3, 3, 1, true, NULL },
    };
    int failed = 0;

    for (const auto &r : rows)
    {
        thSynth synth(plugins, 64, TH_DEFAULT_SAMPLES, r.width);

        synth.setMasterGain(1);
        synth.setLimiter(r.limit);
        if (synth.loadTreeText(r.name, instrument(r.instrument, r.level), 0,
                               MIDIVALMAX) == NULL)
        {
            printf("FAIL  %s\n  does not load\n", r.name);
            failed++;
            continue;
        }
        synth.addNote(0, 60, MIDIVALMAX);
        synth.process();

        string got;
        bool under = true;

        for (int c = 0; c < synth.audioChannelCount(); c++)
        {
            const float v = synth.getOutput()[c * 64];
            char text[16];

            snprintf(text, sizeof(text), "%s%.2g", c ? " " : "", v);
            got += text;
            under = under && v <= 1;
        }
        if (r.want ? got == r.want : under)
            printf("ok    %s: %s\n", r.name, got.c_str());
        else
        {
            printf("FAIL  %s\n  got  %s\n  want %s\n", r.name, got.c_str(),
                   r.want ? r.want : "each at most 1");
            failed++;
        }
    }

    return failed ? 1 : 0;
}
