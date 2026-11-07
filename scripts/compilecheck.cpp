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
 * that voice to nothing, so its output is the voice's. And a patch's
 * controller block, compiled with it, plays as a profile does.
 */

#include <cstdio>
#include <cstring>
#include <string>

#include "think.h"

#include "thhBox.h"
#include "thhMapper.h"
#include "thvDxbell.h"
#include "thvGate.h"
#include "thvNames.h"

#define WINDOW 128

static int failed = 0;

template <class V>
static void
check (const char *name, const std::string &file, const std::string &plugins,
       int releaseAt, int windows, bool ends = false)
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

    int done = -1;              /* the window render said it was over */

    for (int w = 0; w < windows && !why[0]; w++)
    {
        int channels = 0;

        if (w == releaseAt)
        {
            synth.delNote(0, 60);
            voice.release();
        }
        synth.process();
        if (!voice.render(knobs, outs, WINDOW) && done < 0)
            done = w;

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
                else if (done >= 0 && w > done && want != 0)
                    snprintf(why, sizeof(why), "done in window %d, and the engine "
                             "still sounding in window %d", done, w);
            }
    }
    if (!why[0] && ends && done < 0)
        snprintf(why, sizeof(why), "never done in %d windows", windows);

    /* Started again once done, as a firmware reuses its voices: as a new
       one starts. */
    if (!why[0] && done >= 0)
    {
        V fresh;
        float again[2][WINDOW];
        float *agains[2] = { again[0], again[1] };

        voice.start(64, 100);
        fresh.start(64, 100);
        for (int w = 0; w < 50 && !why[0]; w++)
        {
            voice.render(knobs, outs, WINDOW);
            fresh.render(knobs, agains, WINDOW);
            if (memcmp(out[0], again[0], sizeof(again[0])) != 0)
                snprintf(why, sizeof(why), "started again, window %d isn't a new "
                         "voice's", w);
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

    check<thvDxbell>("dsp/dxbell.dsp, held then let go, to its end", dsp + "/dxbell.dsp",
                     plugins, 300, 1300, true);
    check<thvGate>("a gated envelope through its release, done as the engine is",
                   dsp + "/../scripts/compiled/gate.dsp", plugins, 100, 200, true);
    check<thvNames>("knobs named for a word of C++'s and for set()'s own",
                    dsp + "/../scripts/compiled/names.dsp", plugins, 100, 200, true);

    /* Each by its name, whatever that is in C++. */
    {
        thvNames::Knobs k;
        float lo, hi;

        const bool ok = k.set("value", 0.25f) && k.set("or", 2) &&
                        k.k_value == 0.25f && k.k_or == 2 &&
                        thvNames::Knobs::range("value", lo, hi);

        printf("%s  knobs named value and or are set by those names\n",
               ok ? "ok  " : "FAIL");
        failed += !ok;
    }

    /* dxbell's block for the BOX: a touch mid-screen is C5, the scale's
       fifth degree up from C4, at the velocity its height gives. */
    {
        thhMapper m;
        thhState st;
        std::vector<thhAction> acts;
        std::string err;
        int line;
        const bool ok = m.configure(thvDxbell::profile(), thhBoxControls(), acts, err, line);

        st.connected = true;
        st.axis[THH_BOX_X] = 0.5f;
        st.axis[THH_BOX_Y] = 1;
        m.update(st, 0, acts);
        acts.clear();
        st.buttons = 1u << THH_BOX_TOUCH;
        st.time += 0.01;
        m.update(st, 0, acts);

        bool on = false;

        for (const thhAction &a : acts)
            on = on || (a.kind == thhAction::NOTE_ON && a.note == 72 && a.vel == 127);
        if (ok && on)
            printf("ok    dxbell's BOX block, compiled, plays C5 mid-screen\n");
        else
        {
            printf("FAIL  dxbell's BOX block, compiled, plays C5 mid-screen\n  %s\n",
                   ok ? "no such note" : err.c_str());
            failed++;
        }
    }

    printf("%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed ? 1 : 0;
}
