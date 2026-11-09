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
 * that voice to nothing, so its output is the voice's. A noise, whose
 * stream isn't the engine's, is held to it by its statistics. And a
 * patch's controller block, compiled with it, plays as a profile does.
 */

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "think.h"

#include "thhBox.h"
#include "thhMapper.h"
#include "thvDxbell.h"
#include "thvGate.h"
#include "thvNames.h"
#include "thvNoise.h"
#include "thvGrab.h"
#include "thvIndex.h"

#define WINDOW 128

static int failed = 0;

typedef std::vector<std::pair<const char *, float>> Turned;

/* The knobs in turned are set alike on the engine and the voice before the
   note, so nothing glides. */
template <class V>
static void
check (const char *name, const std::string &file, const std::string &plugins,
       int releaseAt, int windows, bool ends = false, const Turned &turned = {})
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
    for (const auto &t : turned)
    {
        synth.setChanArg(0, new thArg(std::string(t.first), t.second));
        if (!knobs.set(t.first, t.second))
            snprintf(why, sizeof(why), "no knob %s", t.first);
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

/* A noise's level, and the level of its steps over that, which is its
   slope: sqrt(2) for a white, less the more it leans to the low end. */
static void
levels (const float *y, int n, double &rms, double &slope)
{
    double e = 0, d = 0;

    for (int i = 1; i < n; i++)
    {
        e += (double)y[i] * y[i];
        d += ((double)y[i] - y[i - 1]) * ((double)y[i] - y[i - 1]);
    }
    rms = std::sqrt(e / (n - 1));
    slope = std::sqrt(d / e);
}

/* The engine's noise and a compiled voice's are seeded apart (start()'s
   seed is the firmware's to give), so what is held to the engine's is
   the streams' statistics, over 32 seconds of each color after its
   first 100 ms: a level or a slope 1% off is a different noise. */
static void
checkNoise (const std::string &file, const std::string &plugins)
{
    static const char *const COLORS[] = { "white", "pink", "brown" };
    const int skip = 3200, len = 32 * 32000;
    std::vector<float> live(len), compiled(len);
    char why[200] = "";

    for (int color = 0; color < 3 && !why[0]; color++)
    {
        thSynth synth(plugins, WINDOW, (int)thvNoise::rate);
        thvNoise voice;
        thvNoise::Knobs knobs;
        float out[WINDOW];
        float *outs[1] = { out };

        if (!synth.loadTree(file, 0, MIDIVALMAX))
        {
            snprintf(why, sizeof(why), "%s didn't load", file.c_str());
            break;
        }
        synth.setChanArg(0, new thArg(std::string("color"), (float)color));
        knobs.k_color = (float)color;
        synth.addNote(0, 60, 127);
        voice.start(60, 127, 7);
        for (int at = 0; at < skip + len; at += WINDOW)
        {
            int channels = 0;

            synth.process();
            voice.render(knobs, outs, WINDOW);

            const float *ch = synth.getChannelOutput(0, &channels);

            for (int i = 0; i < WINDOW; i++)
                if (at + i >= skip && at + i - skip < len)
                {
                    live[at + i - skip] = ch ? ch[i * channels] : 0;
                    compiled[at + i - skip] = out[i];
                }
        }

        double rmsLive, slopeLive, rms, slope;

        levels(live.data(), len, rmsLive, slopeLive);
        levels(compiled.data(), len, rms, slope);
        if (!(std::fabs(rms / rmsLive - 1) < 0.01 && std::fabs(slope / slopeLive - 1) < 0.01))
            snprintf(why, sizeof(why), "%s: level %.4f, the engine %.4f; slope %.4f, "
                     "the engine %.4f", COLORS[color], rms, rmsLive, slope, slopeLive);
    }
    if (!why[0])
        printf("ok    the compiled noise is the engine's, by level and slope\n");
    else
    {
        printf("FAIL  the compiled noise is the engine's, by level and slope\n  %s\n", why);
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
    check<thvGrab>("dsp/grab.dsp, its gated envelope released", dsp + "/grab.dsp",
                   plugins, 300, 600);
    check<thvGrab>("dsp/grab.dsp, every knob turned", dsp + "/grab.dsp", plugins, 300, 600,
                   false, { { "bright", 0.8f }, { "ring", 20000 }, { "level", 0.6f },
                            { "pan", 0.5f } });
    /* index.dsp's breath is its noise, and its Air at 0 takes all of it:
       the compiled voice's noise isn't the engine's stream. */
    check<thvIndex>("dsp/index.dsp, filter, saturation and all, Air at 0",
                    dsp + "/index.dsp", plugins, 300, 600);
    check<thvIndex>("dsp/index.dsp, every knob but Air turned", dsp + "/index.dsp", plugins,
                    300, 600, false,
                    { { "bright", 0.7f }, { "wobble", 0.6f }, { "growl", 0.5f },
                      { "sub", 0.8f }, { "bend", 0.3f }, { "level", 0.9f },
                      { "pan", -0.4f }, { "r", 5000 } });
    check<thvGate>("a gated envelope through its release, done as the engine is",
                   dsp + "/../scripts/compiled/gate.dsp", plugins, 100, 200, true);
    check<thvNames>("knobs named for a word of C++'s and for set()'s own",
                    dsp + "/../scripts/compiled/names.dsp", plugins, 100, 200, true);
    checkNoise(dsp + "/../scripts/compiled/noise.dsp", plugins);

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
