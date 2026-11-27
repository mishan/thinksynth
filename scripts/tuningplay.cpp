/*
 * Copyright (C) 2026 The thinksynth authors
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
 */

/*
 * tuningplay -- a channel's tuning, heard.
 *
 *   tuningplay PLUGINS
 *
 * A sine through misc::midi2freq on a tuned channel, its frequency measured
 * from the output by its upward zero crossings: a key, a fractional key, a
 * key the keymap leaves out, a bent key, a mono voice retuned to another
 * key, a release that must still find its voice, and a channel swapped
 * under a held key. Last, a channel with
 * 12-TET as an explicit tuning must render byte for byte what a channel
 * with none renders.
 */

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "think.h"
#include "thTuning.h"
#include "PatchApply.h"
#include "PatchFile.h"

static int failures = 0;

static void fail (const std::string &what)
{
    fprintf(stderr, "FAIL %s\n", what.c_str());
    failures++;
}

static std::string graph (bool mono)
{
    return std::string(
        "name \"tuningplay\";\n"
        "node ionode {\n"
        "    out0 = mix->out; out1 = mix->out; channels = 2;\n"
        "    play = env->play;\n") +
        (mono ? "    mono = 1;\n" : "") +
        "};\n"
        "node freq misc::midi2freq { note = ionode->note; };\n"
        "node osc osc::simple { freq = freq->out; waveform = 0; };\n"
        "node env env::adsr { a = 200; d = 200; s = 1; r = 2000; p = 0.8;\n"
        "    trigger = ionode->trigger; };\n"
        "node mix mixer::mul { in0 = osc->out; in1 = env->out; };\n"
        "io ionode;\n";
}

static std::shared_ptr<const thTuning> tuning (const std::string &scl,
                                               const std::string &kbm)
{
    thScale s;
    thKeymap m;
    auto t = std::make_shared<thTuning>();
    std::string err;
    int line;

    if (!s.parse(scl, err, line) || (!kbm.empty() && !m.parse(kbm, err, line)) ||
        !t->build(s, m, err))
    {
        fail("tuning refused: " + err);
        return nullptr;
    }

    return t;
}

static std::string edo19 (void)
{
    std::string text = "19-EDO\n19\n";

    for (int i = 1; i < 19; i++)
    {
        char buf[64];

        snprintf(buf, sizeof buf, "%.12f\n", 1200.0 * i / 19);
        text += buf;
    }

    return text + "2/1\n";
}

static const char *const TET12 =
    "12-TET\n12\n100.0\n200.0\n300.0\n400.0\n500.0\n600.0\n700.0\n800.0\n"
    "900.0\n1000.0\n1100.0\n2/1\n";

/* Windows of the synth's output, channel 0. */
static void run (thSynth &synth, int windows, std::vector<float> *out)
{
    const int len = synth.getWindowlen();

    for (int w = 0; w < windows; w++)
    {
        synth.process();

        if (out)
            out->insert(out->end(), synth.getOutput(), synth.getOutput() + len);
    }
}

/* Frequency from upward zero crossings, each placed by interpolation. */
static double frequency (const std::vector<float> &x, double rate)
{
    double first = -1, last = -1;
    int crossings = 0;

    for (size_t i = 1; i < x.size(); i++)
        if (x[i - 1] < 0 && x[i] >= 0)
        {
            const double at = (double)(i - 1) + x[i - 1] / (x[i - 1] - x[i]);

            if (first < 0)
                first = at;
            last = at;
            crossings++;
        }

    return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0;
}

static double rms (const std::vector<float> &x)
{
    double acc = 0;

    for (float v : x)
        acc += (double)v * v;

    return x.empty() ? 0 : sqrt(acc / x.size());
}

static const int SETTLE = 8;        /* windows before measuring */
static const int MEASURE = 24;      /* windows measured */

/* Whether the channel sounds at `note' (a fractional 12-TET note), to
   half a cent. */
static void expect (thSynth &synth, const std::string &what, double note)
{
    std::vector<float> x;

    run(synth, MEASURE, &x);

    const double want = 440 * pow(2, (note - 69) / 12);
    const double got = frequency(x, TH_DEFAULT_SAMPLES);
    const double cents = got > 0 ? 1200 * log2(got / want) : 1e9;

    if (!(fabs(cents) < 0.5))
        fail(what + ": " + std::to_string(got) + " Hz, " + std::to_string(cents) +
             " cents from note " + std::to_string(note));
}

int main (int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s PLUGINS\n", argv[0]);
        return 2;
    }

    const std::string plugins = argv[1];
    const double step = 12.0 / 19;      /* one 19-EDO step, in 12-TET notes */

    /* 19-EDO, every key. */
    {
        thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        if (!synth.loadTreeText("tuned", graph(false), 0, 100, tuning(edo19(), "")))
            fail("the tuned graph would not load");

        synth.addNote(0, 61, 100);
        run(synth, SETTLE, NULL);
        expect(synth, "19-EDO key 61", 60 + step);

        synth.bendNote(0, 61, 1.0f);
        run(synth, 2, NULL);
        expect(synth, "19-EDO key 61 bent a semitone", 60 + step + 1);

        synth.delNote(0, 61);
        run(synth, SETTLE * 2, NULL);

        std::vector<float> after;

        run(synth, 4, &after);

        if (rms(after) > 1e-4)
            fail("key 61's release did not find its voice");

        synth.addNote(0, 60.5f, 100);
        run(synth, SETTLE, NULL);
        expect(synth, "19-EDO key 60.5, between its neighbors", 60 + step / 2);
        synth.delNote(0, 60.5f);
    }

    /* A keymap that leaves odd keys out: they play at their 12-TET pitch;
       a fraction beside one is added in 12-TET to its tuned neighbor. */
    {
        thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        synth.loadTreeText("gaps", graph(false), 0, 100,
                           tuning(edo19(), "2\n0\n127\n60\n60\n261.6255653005986\n1\n0\nx\n"));

        synth.addNote(0, 62, 100);
        run(synth, SETTLE, NULL);
        expect(synth, "gaps: key 62 is one step up", 60 + step);
        synth.delNote(0, 62);
        run(synth, SETTLE * 2, NULL);

        synth.addNote(0, 61, 100);
        run(synth, SETTLE, NULL);
        expect(synth, "gaps: unmapped key 61 plays at 12-TET", 61);
        synth.delNote(0, 61);
        run(synth, SETTLE * 2, NULL);

        /* 62 is tuned, 63 is not: the half is added in 12-TET to 62's
           pitch, which no tuning and no interpolation would give. */
        synth.addNote(0, 62.5f, 100);
        run(synth, SETTLE, NULL);
        expect(synth, "gaps: key 62.5 below an unmapped key", 60 + step + 0.5);
        synth.delNote(0, 62.5f);
    }

    /* Swapped while a key is held: the held voice finishes on the old
       channel and its tuning; the next note plays the new one's. */
    {
        thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        synth.loadTreeText("before", graph(false), 0, 100, tuning(edo19(), ""));
        synth.addNote(0, 61, 100);
        run(synth, SETTLE, NULL);

        if (!synth.loadTreeText("after", graph(false), 0, 100, nullptr))
            fail("the swapped-in graph would not load");

        run(synth, 2, NULL);
        expect(synth, "swap: the held key keeps the old tuning", 60 + step);

        synth.delNote(0, 61);
        run(synth, SETTLE * 2, NULL);
        synth.addNote(0, 61, 100);
        run(synth, SETTLE, NULL);
        expect(synth, "swap: the next key plays the new channel's", 61);
    }

    /* Retuned live: a held key goes on at the pitch it began at, the next
       key plays the new tuning, and the table the held key was built with
       is still there when it is let go (ASan sees to that). */
    {
        thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        synth.loadTreeText("live", graph(false), 0, 100, tuning(edo19(), ""));
        synth.addNote(0, 61, 100);
        run(synth, SETTLE, NULL);

        if (!synth.setTuning(0, nullptr))
            fail("a live retune was refused");

        run(synth, 2, NULL);
        expect(synth, "live: the held key keeps its tuning", 60 + step);

        /* Bent, it is pitched again, from the table it was built with. */
        synth.bendNote(0, 61, 0.5f);
        run(synth, 2, NULL);
        expect(synth, "live: and bent, still its own tuning", 60 + step + 0.5);

        synth.delNote(0, 61);
        run(synth, SETTLE * 2, NULL);
        synth.addNote(0, 61, 100);
        run(synth, SETTLE, NULL);
        expect(synth, "live: the next key plays the new tuning", 61);
        synth.delNote(0, 61);
        run(synth, SETTLE * 2, NULL);

        synth.setTuning(0, tuning(edo19(), ""));
        synth.addNote(0, 61, 100);
        run(synth, SETTLE, NULL);
        expect(synth, "live: and back again", 60 + step);
    }

    /* Mono, retuned with a key held: the next key slides the held voice
       to that key in the new tuning, the one it was pressed under. */
    {
        thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        synth.loadTreeText("monolive", graph(true), 0, 100, tuning(edo19(), ""));
        synth.addNote(0, 60, 100);
        run(synth, SETTLE, NULL);
        synth.setTuning(0, nullptr);
        synth.addNote(0, 62, 100);
        run(synth, SETTLE, NULL);
        expect(synth, "mono live: the slid key plays the new tuning", 62);
    }

    /* A mono voice moved to another key is moved to that key's pitch. */
    {
        thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        synth.loadTreeText("mono", graph(true), 0, 100, tuning(edo19(), ""));

        synth.addNote(0, 60, 100);
        run(synth, SETTLE, NULL);
        synth.addNote(0, 62, 100);
        run(synth, SETTLE, NULL);
        expect(synth, "mono: retuned to key 62", 60 + 2 * step);
    }

    /* A patch carrying a tuning tunes its channel; one whose tuning will
       not build still plays, untuned, and says why. */
    {
        thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
        thPatchDoc doc;
        std::string why;

        if (!thPatchParse("dsp pluck.dsp\ntuning name just\n"
                          "tuning scale 9/8 5/4 4/3 3/2 5/3 15/8 2/1\n", doc, why))
            fail("the tuned patch did not parse: " + why);
        else
        {
            const thPatchApplied a = thPatchApply(&synth, 0, doc);
            const thMidiChan *ch = synth.getChannel(0);

            if (!a.ok || ch == NULL || ch->tuning() == NULL ||
                fabs(ch->tuning()->note(61) - 62.039100017307746) > 1e-9)
                fail("a tuned patch did not tune its channel: " + a.why);
        }

        if (!thPatchParse("dsp pluck.dsp\ntuning scale 9/8 abc 2/1\n", doc, why))
            fail("the badly tuned patch did not parse: " + why);
        else
        {
            const thPatchApplied a = thPatchApply(&synth, 0, doc);
            const thMidiChan *ch = synth.getChannel(0);
            bool said = false;

            for (const std::string &c : a.complaints)
                said = said || c.find("'abc'") != std::string::npos;

            if (!a.ok || ch == NULL || ch->tuning() != NULL || !said)
                fail("a patch whose tuning will not build: ok " +
                     std::to_string(a.ok) + ", tuned " +
                     std::to_string(ch && ch->tuning() != NULL) +
                     ", complaint " + std::to_string(said));
        }
    }

    /* 12-TET as a tuning is no tuning, to the bit. */
    {
        std::vector<float> plain, tuned;

        for (int pass = 0; pass < 2; pass++)
        {
            thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

            if (!synth.loadTreeText("same", graph(false), 0, 100,
                                    pass ? tuning(TET12, "") : nullptr))
                fail("the identity graph would not load");

            synth.addNote(0, 61, 100);
            synth.addNote(0, 64.25f, 100);
            run(synth, SETTLE, pass ? &tuned : &plain);
            synth.bendNote(0, 61, 0.5f);
            run(synth, SETTLE, pass ? &tuned : &plain);
        }

        if (rms(plain) < 1e-3)
            fail("the identity renders are silent: nothing compared");
        else if (plain != tuned)
            fail("12-TET as a tuning rendered differently from none");
    }

    if (failures)
        fprintf(stderr, "tuningplay: %d failures\n", failures);
    else
        printf("tuningplay: ok\n");

    return failures ? 1 : 0;
}
