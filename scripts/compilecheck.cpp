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
#include <type_traits>
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
#include "thvTwice.h"
#include "thvNames.h"
#include "thvNoise.h"
#include "thvGrab.h"
#include "thvIndex.h"
#include "thvAurora.h"
#include "thvConstellation.h"
#include "thvMoire.h"
#include "thvPond.h"
#include "thvRings.h"
#include "thvWells.h"
#include "thvFeedback.h"
#include "thvPalette.h"
#include "thvSoften.h"
#include "thvFxdelays.h"
#include "../esp32/box/main/pool.h"

#define WINDOW 128

static int failed = 0;

typedef std::vector<std::pair<const char *, float>> Turned;

/* The knobs in turned are set alike on the engine and the voice before the
   note, so nothing glides. A rate other than the compiled one is bound;
   a knob in time keeps its compiled default, so it is turned too. */
template <class V>
static void
check (const char *name, const std::string &file, const std::string &plugins,
       int releaseAt, int windows, bool ends = false, const Turned &turned = {},
       long rate = (long)V::rate)
{
    thSynth synth(plugins, WINDOW, (int)rate);
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
    voice.bind(rate);
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
        fresh.bind(rate);
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

/* thinkbox's pool of a patch's voices mixes them as the engine's channel
   does: each note at its own level, the channel's amp over them all, and
   a step in amp glided as the engine glides it. Two notes, at `levels',
   the second struck as the amp steps from `amp' to `ampTo', then both
   let go. To a float's rounding, since the two sum in either order. */
template <class V>
static void
checkPool (const char *name, const std::string &file, const std::string &plugins,
           const float levels[2], float amp, float ampTo)
{
    thSynth synth(plugins, BLOCK, BOX_RATE);
    PatchOf<V> pool("pool", 1);
    char why[200] = "";
    const int notes[2] = { 60, 67 };

    if (!synth.loadTree(file, 0, MIDIVALMAX))
    {
        printf("FAIL  %s\n  %s didn't load\n", name, file.c_str());
        failed++;
        return;
    }
    synth.setChanArg(0, new thArg("amp", amp));
    pool.amp = amp;

    for (int w = 0; w < 400 && !why[0]; w++)
    {
        float l[BLOCK] = { 0 }, r[BLOCK] = { 0 };
        int channels = 0;
        thhAction a;

        a.note = notes[w == 100];
        a.vel = 100;
        if (w == 0 || w == 100)
        {
            if (w == 100)
            {
                synth.setChanArg(0, new thArg("amp", ampTo));
                pool.amp = ampTo;
            }
            synth.addNote(0, a.note, 100, levels[w == 100]);
            a.kind = thhAction::NOTE_ON;
            pool.play(a, 0, w, true, levels[w == 100]);
        }
        for (int n = 0; n < 2 && w == 250; n++)
        {
            a.note = notes[n];
            synth.delNote(0, a.note);
            a.kind = thhAction::NOTE_OFF;
            pool.play(a, 0, w, true, 1);
        }
        synth.process();
        pool.glide();
        pool.render(l, r, 0);
        pool.render(l, r, 1);

        const float *live = synth.getChannelOutput(0, &channels);

        for (int i = 0; i < BLOCK && !why[0]; i++)
            for (int c = 0; c < channels && c < 2; c++)
            {
                const float want = live ? live[i * channels + c] : 0;
                const float got = c ? r[i] : l[i];

                if (!(fabsf(got - want) <= 1e-6f * fmaxf(1, fabsf(want))))
                    snprintf(why, sizeof(why), "window %d, sample %d, channel %d: "
                             "%g, the engine %g", w, i, c, got, want);
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

/* A video patch on a raster `w' by `h' at 60 frames a second: compiled at
   160x120's pixel rate, bound to this one's. Half a second of a
   note, released a quarter of a second in, in windows that start partway
   along a line, and the music's chanargs at levels that light it. Every
   pixel of every channel as the engine's, and some of them lit. */
template <class V>
static void
checkRaster (const char *name, const std::string &file, const std::string &plugins,
             int w, int h)
{
    static const std::pair<const char *, float> MUSIC[] = {
        { "in_level", 0.2f }, { "in_bass", 0.3f }, { "in_mid", 0.2f }, { "in_high", 0.1f },
    };
    const int window = 1000, rate = w * h * 60, windows = 30 * w * h / window;
    thSynth synth(plugins, window, rate, 3);
    V voice;
    typename V::Knobs knobs;
    std::vector<float> out(3 * window);
    float *outs[3] = { &out[0], &out[window], &out[2 * window] };
    char why[200] = "";
    float most = 0;

    synth.setRaster(w, h);
    synth.setLimiter(false);
    if (!synth.loadTree(file, 0, MIDIVALMAX))
        snprintf(why, sizeof(why), "%s didn't load", file.c_str());
    for (const auto &m : MUSIC)
    {
        synth.setChanArg(0, new thArg(std::string(m.first), m.second));
        knobs.set(m.first, m.second);
    }
    synth.addNote(0, 60, 100);
    voice.bind(rate);
    voice.raster(w, h);
    voice.start(60, 100);
    for (int k = 0; k < windows && !why[0]; k++)
    {
        int channels = 0;

        if (k == windows / 2)
        {
            synth.delNote(0, 60);
            voice.release();
        }
        synth.process();
        /* The window just rendered begins at the synth's position. */
        voice.render(knobs, outs, window, synth.position());

        const float *live = synth.getChannelOutput(0, &channels);

        if (channels != 3)
            snprintf(why, sizeof(why), "the engine's channel is %d wide, not 3", channels);
        for (int i = 0; i < window && !why[0]; i++)
            for (int c = 0; c < 3 && !why[0]; c++)
            {
                const float want = live ? live[i * channels + c] : 0;
                const float got = 0.0f + outs[c][i];

                most = std::max(most, std::fabs(want));
                if (memcmp(&want, &got, sizeof(float)) != 0)
                    snprintf(why, sizeof(why), "window %d, sample %d, channel %d: "
                             "%a, the engine %a", k, i, c, got, want);
            }
    }
    if (!why[0] && most < 0.05f)
        snprintf(why, sizeof(why), "dark: the brightest pixel is %g", most);
    if (!why[0])
        printf("ok    %s, %dx%d\n", name, w, h);
    else
    {
        printf("FAIL  %s, %dx%d\n  %s\n", name, w, h, why);
        failed++;
    }
}

/* Whether a compiled graph reads misc::scan, and so is on a raster. */
template <class V, class = void> struct OnRaster : std::false_type {};
template <class V>
struct OnRaster<V, std::void_t<decltype(&V::raster)>> : std::true_type {};

/* An effect on the whole picture, after a compiled patch, against the
   engine's master effect after the engine's: a note of rings, so the
   picture moves, through the effect at its defaults for a quarter of a
   second, the master's output to the bit. */
template <class F>
static void
checkEffect (const char *name, const std::string &patch, const std::string &fx,
             const std::string &plugins, int w, int h)
{
    const int window = 1000, rate = w * h * 60, windows = 15 * w * h / window;
    thSynth synth(plugins, window, rate, 3);
    thvRings voice;
    thvRings::Knobs vk;
    F effect;
    typename F::Knobs fk;
    std::vector<float> mid(3 * window), out(3 * window);
    float *mids[3] = { &mid[0], &mid[window], &mid[2 * window] };
    float *outs[3] = { &out[0], &out[window], &out[2 * window] };
    char why[200] = "";
    float most = 0;

    synth.setRaster(w, h);
    synth.setLimiter(false);
    synth.setMasterGain(1);
    if (!synth.loadTree(patch, 0, MIDIVALMAX) || !synth.loadMasterEffect(fx))
        snprintf(why, sizeof(why), "%s or %s didn't load", patch.c_str(), fx.c_str());
    synth.setChanArg(0, new thArg(std::string("in_level"), 0.2f));
    vk.set("in_level", 0.2f);
    synth.addNote(0, 60, 100);
    voice.bind(rate);
    voice.raster(w, h);
    voice.start(60, 100);
    effect.bind(rate);
    if constexpr (OnRaster<F>::value)
        effect.raster(w, h);
    for (int k = 0; k < windows && !why[0]; k++)
    {
        synth.process();
        voice.render(vk, mids, window, synth.position());
        bool on;

        if constexpr (OnRaster<F>::value)
            on = effect.render(fk, mids, outs, window, synth.position());
        else
            on = effect.render(fk, mids, outs, window);
        /* An effect has no note to end. */
        if (!on)
            snprintf(why, sizeof(why), "window %d: done, as no effect is", k);

        const float *live = synth.getOutput();

        for (int c = 0; c < 3 && !why[0]; c++)
            for (int i = 0; i < window && !why[0]; i++)
            {
                const float want = live[c * window + i], got = 0.0f + outs[c][i];

                most = std::max(most, std::fabs(want));
                if (memcmp(&want, &got, sizeof(float)) != 0)
                    snprintf(why, sizeof(why), "window %d, sample %d, channel %d: "
                             "%a, the engine %a", k, i, c, got, want);
            }
    }
    if (!why[0] && most < 0.05f)
        snprintf(why, sizeof(why), "dark: the brightest pixel is %g", most);
    if (!why[0])
        printf("ok    %s, %dx%d\n", name, w, h);
    else
    {
        printf("FAIL  %s, %dx%d\n  %s\n", name, w, h, why);
        failed++;
    }
}

/* Knobs::declared, what a host checks a piece's chanargs against with no
   graph loaded: every knob the voice has, as the loaded patch has it --
   its value, range and unit -- and nothing it doesn't have. */
template <class V>
static void
checkDeclared (const char *name, const std::string &file, const std::string &plugins)
{
    thSynth synth(plugins, WINDOW, (int)V::rate);
    typename V::Knobs knobs;
    std::string why;
    size_t settable = 0;

    if (!synth.loadTree(file, 0, MIDIVALMAX))
        why = file + " didn't load";

    const thArgMap args = synth.getChanArgs(0);

    for (const auto &kv : args)
        settable += knobs.set(kv.first.c_str(), (*kv.second)[0]);
    for (const auto &d : V::Knobs::declared)
    {
        const auto at = args.find(d.name);
        const thArg *a = at == args.end() ? NULL : at->second;

        if (!why.empty())
            break;
        if (a == NULL)
            why = std::string(d.name) + " is declared and the patch has no such chanarg";
        else if ((*a)[0] != d.value || a->min() != d.min || a->max() != d.max ||
                 a->units() != d.units)
            why = std::string(d.name) + " is declared " + std::to_string(d.value) + " in " +
                  std::to_string(d.min) + ".." + std::to_string(d.max) + " " + d.units +
                  "; the patch has " + std::to_string((*a)[0]) + " in " +
                  std::to_string(a->min()) + ".." + std::to_string(a->max()) + " " +
                  a->units();
    }
    if (why.empty() && settable != V::Knobs::declared.size())
        why = std::to_string(settable) + " of the patch's chanargs are the voice's knobs, " +
              std::to_string(V::Knobs::declared.size()) +
              " declared";

    printf("%s  %s\n", why.empty() ? "ok  " : "FAIL", name);
    if (!why.empty())
    {
        printf("  %s\n", why.c_str());
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
    check<thvGate>("compiled at 32 kHz and bound to 48, as the engine at 48",
                   dsp + "/../scripts/compiled/gate.dsp", plugins, 100, 200, true,
                   { { "a", 240 }, { "r", 2400 } }, 48000);
    check<thvTwice>("values written twice, each as its second, bound to 48 kHz",
                    dsp + "/../scripts/compiled/twice.dsp", plugins, 100, 200, true, {},
                    48000);
    check<thvDxbell>("dsp/dxbell.dsp bound to 44.1 kHz, to its end", dsp + "/dxbell.dsp",
                     plugins, 300, 1800, true,
                     { { "id", 61740 }, { "ia", 44.1f }, { "d", 198450 } }, 44100);
    check<thvNames>("knobs named for a word of C++'s and for set()'s own",
                    dsp + "/../scripts/compiled/names.dsp", plugins, 100, 200, true);
    checkNoise(dsp + "/../scripts/compiled/noise.dsp", plugins);

    static const struct { const char *name; float levels[2], amp, ampTo; } POOLS[] = {
        { "thinkbox's pool: notes at full level, the channel's amp full",
          { 1, 1 }, 127, 127 },
        { "thinkbox's pool: notes at their own levels, the channel's amp half",
          { 0.3f, 0.8f }, 64, 64 },
        { "thinkbox's pool: the channel's amp stepped down, glided",
          { 1, 0.6f }, 127, 30 },
    };

    for (const auto &t : POOLS)
        checkPool<thvDxbell>(t.name, dsp + "/dxbell.dsp", plugins, t.levels, t.amp,
                             t.ampTo);
    checkDeclared<thvDxbell>("dxbell's knobs, declared as the patch has them",
                             dsp + "/dxbell.dsp", plugins);
    checkDeclared<thvGrab>("grab's knobs, declared as the patch has them",
                           dsp + "/grab.dsp", plugins);
    checkDeclared<thvIndex>("index's knobs, declared as the patch has them",
                            dsp + "/index.dsp", plugins);

    const std::string video = dsp + "/../video/";

    /* A video voice given no raster renders nothing, rather than dividing
       by its width. */
    {
        thvRings v;
        thvRings::Knobs k;
        float out[3][WINDOW];
        float *outs[3] = { out[0], out[1], out[2] };
        bool dark = true;

        memset(out, 0x7f, sizeof(out));
        v.start(60, 100);

        const bool sounding = v.render(k, outs, WINDOW, 0);

        for (int c = 0; c < 3; c++)
            for (int i = 0; i < WINDOW; i++)
                dark = dark && out[c][i] == 0;
        printf("%s  a video voice with no raster renders nothing\n",
               !sounding && dark ? "ok  " : "FAIL");
        failed += sounding || !dark;
    }

    for (const auto &r : { std::make_pair(160, 120), std::make_pair(80, 60) })
    {
        checkRaster<thvAurora>("video/aurora.dsp", video + "aurora.dsp", plugins, r.first, r.second);
        checkRaster<thvConstellation>("video/constellation.dsp", video + "constellation.dsp",
                                      plugins, r.first, r.second);
        checkRaster<thvMoire>("video/moire.dsp", video + "moire.dsp", plugins, r.first, r.second);
        checkRaster<thvPond>("video/pond.dsp", video + "pond.dsp", plugins, r.first, r.second);
        checkRaster<thvRings>("video/rings.dsp", video + "rings.dsp", plugins, r.first, r.second);
        checkRaster<thvWells>("video/wells.dsp", video + "wells.dsp", plugins, r.first, r.second);
        checkEffect<thvFeedback>("video/fx/feedback.dsp after rings", video + "rings.dsp",
                                 video + "fx/feedback.dsp", plugins, r.first, r.second);
        checkEffect<thvPalette>("video/fx/palette.dsp after rings", video + "rings.dsp",
                                video + "fx/palette.dsp", plugins, r.first, r.second);
        checkEffect<thvSoften>("video/fx/soften.dsp after rings", video + "rings.dsp",
                               video + "fx/soften.dsp", plugins, r.first, r.second);
        checkEffect<thvFxdelays>("delays whose lengths follow the picture, after rings",
                                 video + "rings.dsp",
                                 dsp + "/../scripts/compiled/fxdelays.dsp", plugins,
                                 r.first, r.second);
    }

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
        const bool ok = m.configure(thvDxbell::profile_box(), thhBoxControls(), acts, err, line);

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
