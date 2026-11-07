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
 * compilecheck -- a compiled voice renders what the engine renders, to the
 * bit (scripts/dspcompile).
 *
 * Each patch is compiled at build time; here one note of it plays in the
 * engine and in the compiled voice, released partway, and every sample of
 * every channel is compared. A channel with one voice at full level adds
 * that voice to nothing, so its output is the voice's.
 */

#include <cstdio>
#include <cstring>
#include <string>

#include "think.h"

#include "thvDxbell.h"
#include "thvGate.h"

#define WINDOW 128

static int failed = 0;

template <class V>
static void
check (const char *name, const std::string &file, const std::string &plugins,
       int releaseAt, int windows)
{
    thSynth synth(plugins, WINDOW, (int)V::rate);
    V voice;
    typename V::Knobs knobs;
    float out[2][WINDOW];
    float *outs[2] = { out[0], out[1] };
    char why[200] = "";

    if (!synth.loadTree(file, 0, MIDIVALMAX))
    {
        printf("FAIL  %s\n  %s didn't load\n", name, file.c_str());
        failed++;
        return;
    }
    synth.addNote(0, 60, 100);
    voice.start(60, 100);

    for (int w = 0; w < windows && !why[0]; w++)
    {
        int channels = 0;

        if (w == releaseAt)
        {
            synth.delNote(0, 60);
            voice.release();
        }
        synth.process();
        voice.render(knobs, outs, WINDOW);

        const float *live = synth.getChannelOutput(0, &channels);

        for (int i = 0; i < WINDOW && !why[0]; i++)
            for (int c = 0; c < channels && c < 2; c++)
            {
                /* As the channel mixes it: onto nothing. */
                const float want = live ? live[i * channels + c] : 0;
                const float got = 0.0f + out[c][i];

                if (memcmp(&want, &got, sizeof(float)) != 0)
                    snprintf(why, sizeof(why), "window %d, sample %d, channel %d: "
                             "%a, the engine %a", w, i, c, got, want);
            }
    }

    if (!why[0])
        printf("ok    %s\n", name);
    else
    {
        printf("FAIL  %s\n  %s\n", name, why);
        failed++;
    }
}

int
main (int argc, char **argv)
{
    const std::string plugins = argc > 2 && !strcmp(argv[1], "-p") ? argv[2] : "plugins/";
    const std::string dsp = argc > 3 ? argv[3] : "dsp/";

    check<thvDxbell>("dsp/dxbell.dsp, held then let go", dsp + "/dxbell.dsp",
                     plugins, 300, 600);
    check<thvGate>("a gated envelope through its release",
                   dsp + "/../scripts/compiled/gate.dsp", plugins, 100, 200);

    printf("%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed ? 1 : 0;
}
