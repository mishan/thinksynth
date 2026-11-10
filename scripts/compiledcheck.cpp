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
 * compiledcheck -- the engine running compiled graphs (thCompiled.h).
 *
 * The fingerprint: two loads of a graph match where nothing but the rate,
 * a knob or the loader differs, and not where one number, one wire or
 * one unit does.
 *
 * The frames: every shipped video patch, rings under each shipped effect,
 * and a patch here that reads what none of those do (its velocity, an aux
 * and a choke), each played through one script on a thVideoSynth before
 * anything is compiled and again after thCompiledInstall(), at 160x120
 * and 80x60. The script plays a chord, lets a note go mid-frame, holds one
 * on the pedal, bends a note and moves its aux, moves the music's knobs,
 * plays past the voice pool, stops, and plays again. Every frame is the
 * same bits both ways, and the second way runs no tree on its nodes.
 *
 * The same for random graphs (dsprandom.cpp), at 80x60.
 *
 * The instruments in dsp/ that compile, each a chord, a release, a bend,
 * an aux, the pedal and a run of notes on an audio synth at 48 kHz: the
 * same samples compiled as on the nodes, no window after on the nodes,
 * and none of them silent.
 *
 * And which graphs fall back to their nodes: an edited one, one on a synth
 * with no raster, an effect narrower than it was compiled, one changed in
 * place after it loaded, and one a probe reads inside.
 */

#include "config.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "think.h"
#include "thCompiled.h"
#include "thMidiChan.h"
#include "thSynth.h"
#include "thSynthTree.h"
#include "thVideoSynth.h"

#include "thvExpression.h"

/* thRandomInstall.cpp, which the build writes: how many random graphs
   there are, and them added. */
extern const int thRandomGraphs;
void thRandomInstall (void);

using std::string;

static int failed = 0;

static void
report (const string &name, const string &why)
{
    if (why.empty())
        printf("ok    %s\n", name.c_str());
    else
    {
        printf("FAIL  %s\n  %s\n", name.c_str(), why.c_str());
        failed++;
    }
}

static string
slurp (const string &path)
{
    std::ifstream in(path);
    std::stringstream s;

    s << in.rdbuf();
    return s.str();
}

/* `text' with `from' replaced by `to', which must be in it. */
static string
edit (string text, const string &from, const string &to)
{
    const size_t at = text.find(from);

    if (at == string::npos)
        return "";
    return text.replace(at, from.size(), to);
}

/* The fingerprint of `text' loaded at `rate', as an instrument or an
   effect, or "" for one that didn't load. */
static string
print (const string &plugins, const string &text, int rate, bool effect)
{
    thSynth synth(plugins, 1000, rate, 3);
    thSynthTree *tree = effect ? synth.loadMasterEffectText("fx", text)
                               : synth.loadTreeText("dsp", text, 0, MIDIVALMAX);

    return tree ? tree->fingerprint() : "";
}

/* One run of the script: a patch, maybe an effect, a raster. */
struct Run
{
    string name, dsp, fx;
    int w, h;
    std::vector<uint64_t> frames;   /* each frame's hash, as interpreted */
};

static uint64_t
frameHash (const float *p, size_t n, uint64_t h = 1469598103934665603ull)
{
    const unsigned char *b = (const unsigned char *)p;

    for (size_t i = 0; i < n * sizeof(float); i++)
        h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

static const int FRAMES = 36, LINES = 10;

/* `run' played through the script, each frame's hash into `frames', or a
   sentence; `restarts', how many voices came from the pool. */
static string
play (const string &plugins, const Run &run, std::vector<uint64_t> &frames,
      unsigned long &restarts)
{
    float most = 0;
    thVideoSynth video;
    string why;

    if (!video.open(plugins, run.w, run.h, 60, LINES, why) ||
        !video.followText(0, run.name, run.dsp, why) ||
        (!run.fx.empty() && !video.effectText("fx", run.fx, why)))
        return why;

    thSynth *synth = video.synth();

    /* The engine's polyphony, not a video channel's two: the script's
       chord holds note 60 to bend it and keep it on the pedal. */
    if (synth->getChannel(0) != NULL)
        synth->getChannel(0)->setPolyMax(TH_DEFAULT_POLY);
    const int perFrame = run.h / LINES;
    const float aux[TH_NOTE_AUX] = { 0.3f, 0, 0, 0 };
    const auto at = [&](int frame, int window) { return frame * perFrame + window; };
    const thVideoSynth::Frame keep = [&](int64_t, const float *r, const float *, const float *) {
        frames.push_back(frameHash(r, 3 * (size_t)run.w * run.h));
        for (size_t i = 0; i < 3 * (size_t)run.w * run.h; i++)
            most = std::max(most, r[i]);
        return true;
    };

    frames.clear();
    for (int k = 0; k < FRAMES * perFrame; k++)
    {
        const int frame = k / perFrame;

        if (k % perFrame == 0)
        {
            video.set("in_level", 0.1f + 0.03f * (frame % 7));
            video.set("in_bass", 0.5f - 0.02f * (frame % 5));
            video.set("in_mid", 0.2f + 0.01f * frame);
            video.set("in_high", 0.05f * (frame % 3));
        }
        if (k == at(0, 0))
        {
            video.noteOn(0, 60, 100, 1, aux);
            video.noteOn(0, 64, 80, 1, aux);
            video.noteOn(0, 67, 60, 1, aux);
        }
        if (k == at(3, 5))
            video.noteOff(0, 64);
        if (k == at(4, 0))
            synth->handleMidiController(0, TH_MIDI_CC_SUSTAIN, 127);
        if (k == at(5, 3))
            video.noteOff(0, 67);
        if (k == at(6, 2))
        {
            synth->bendNote(0, 60, 2);
            synth->setNoteAux(0, 60, 0, 0.8f);
        }
        if (k == at(8, 7))
            synth->handleMidiController(0, TH_MIDI_CC_SUSTAIN, 0);
        if (k == at(9, 0))
            video.noteOff(0, 60);
        /* A note a frame, each let go two frames on: more than the pool
           holds, and each voice recycled for one to come. */
        if (frame >= 10 && frame < 24 && k % perFrame == 1)
            video.noteOn(0, 70 + frame % 6, 90, 1, aux);
        if (frame >= 12 && frame < 26 && k % perFrame == 4)
            video.noteOff(0, 70 + (frame - 2) % 6);
        if (k == at(27, 6))
            synth->silence();
        if (k == at(31, 0))
        {
            video.noteOn(0, 62, 110, 1, aux);
            video.noteOn(0, 69, 70, 1, aux);
        }
        if (!video.render(keep))
            return "the render stopped";
    }
    restarts = synth->getChannel(0) ? synth->getChannel(0)->restarts() : 0;
    return most < 0.05f ? "dark: the brightest pixel is " + std::to_string(most) : "";
}

static const int AUDIO_WINDOW = 256, AUDIO_WINDOWS = 400;

/* `file' played on an audio synth, each window's hash into `windows', or a
   sentence: one if it never sounded. */
static string
playAudio (const string &plugins, const string &file, std::vector<uint64_t> &windows)
{
    thSynth synth(plugins, AUDIO_WINDOW, 48000);

    if (synth.loadTree(file.c_str(), 0, MIDIVALMAX) == NULL)
        return file + " didn't load";
    float most = 0;

    windows.clear();
    for (int w = 0; w < AUDIO_WINDOWS; w++)
    {
        if (w == 0)
            for (float note : { 60, 64, 67 })
                synth.addNote(0, note, 100);
        if (w == 20)
        {
            synth.bendNote(0, 60, 2);
            synth.setNoteAux(0, 67, 0, 0.8f);
        }
        if (w == 30)
            synth.handleMidiController(0, TH_MIDI_CC_SUSTAIN, 127);
        if (w == 40)
            synth.delNote(0, 64);
        if (w == 60)
            synth.handleMidiController(0, TH_MIDI_CC_SUSTAIN, 0);
        /* A note every ten windows, each let go five on, below the chord. */
        if (w >= 80 && w < 280 && w % 10 == 0)
            synth.addNote(0, 36 + w / 10 % 12, 90);
        if (w >= 80 && w < 280 && w % 10 == 5)
            synth.delNote(0, 36 + w / 10 % 12);
        if (w == 300)
            for (float note : { 60, 67 })
                synth.delNote(0, note);
        synth.process();

        const float *out = synth.getOutput();

        for (size_t i = 0; i < thOutputSamples(2, AUDIO_WINDOW); i++)
            most = std::max(most, fabsf(out[i]));
        windows.push_back(frameHash(out, thOutputSamples(2, AUDIO_WINDOW)));
    }
    return most < 1e-3f ? "silent: the loudest sample is " + std::to_string(most) : "";
}

/* How `dsp' on a fresh thSynth of `w' x `h' (0 x 0, none) runs, or `fx'
   as its effect does: "compiled", "on its nodes" or "not at all", so that a
   check of a fallback fails when what it edits stops loading. */
static string
ranAs (const string &plugins, const string &dsp, int w, int h, const string &fx = "")
{
    thSynth synth(plugins, 1000, 1152000, 3);

    synth.setRaster(w, h);
    if (synth.loadTreeText("dsp", dsp, 0, MIDIVALMAX) == NULL)
        return "not at all";

    thSynthTree *t = fx.empty() ? synth.getChannel(0)->modnode()
                                : synth.loadEffectText("fx", fx, 0);

    return t == NULL ? "not at all" : t->compiled() ? "compiled" : "on its nodes";
}

static void
expect (const string &name, const string &want, const string &got)
{
    report(name, got == want ? "" : "it ran " + got);
}

int
main (int argc, char **argv)
{
    string plugins = "plugins/", video = "video/", dsp = "dsp/", compiled = "scripts/compiled/",
           random = "scripts/random/";

    for (int i = 1; i + 1 < argc; i += 2)
        if (string(argv[i]) == "-p")
            plugins = argv[i + 1];
        else if (string(argv[i]) == "-v")
            video = argv[i + 1];
        else if (string(argv[i]) == "-c")
            compiled = argv[i + 1];
        else if (string(argv[i]) == "-d")
            dsp = argv[i + 1];
        else if (string(argv[i]) == "-g")
            random = argv[i + 1];

    const string rings = slurp(video + "rings.dsp"), palette = slurp(video + "fx/palette.dsp");
    const int R = 1152000;
    const string a = print(plugins, rings, R, false);
    const struct
    {
        const char *name;
        string other;
        bool same;
    } PAIRS[] = {
        { "at another rate", print(plugins, rings, 48000, false), true },
        { "with another knob", print(plugins, edit(rings, "@speed = 4;", "@speed = 9;"), R,
                                     false), true },
        { "with one number changed", print(plugins, edit(rings, "in1 = 0.5", "in1 = 0.6"), R,
                                           false), false },
        { "rewired", print(plugins, edit(rings, "out1 = g->out", "out1 = b->out"), R, false),
          false },
        { "with 500 ms as 0.5 s", print(plugins, edit(rings, "r = 500 ms", "r = 0.5 s"), R,
                                        false), false },
        { "with midi2freq's note a number", print(plugins, edit(rings, "note = ionode->note",
                                                                "note = 60"), R, false),
          false },
    };

    report("rings.dsp has a fingerprint", a.empty() ? "it's empty" : "");
    for (const auto &p : PAIRS)
        report(string("rings.dsp ") + (p.same ? "matches itself " : "doesn't match itself ") +
                   p.name,
               p.other.empty() ? "it didn't load"
               : (p.other == a) != p.same ? (p.same ? "they differ" : "they match") : "");

    const string loaded = print(plugins, palette, R, false), fx = print(plugins, palette, R, true);

    report("palette.dsp matches itself loaded as an instrument and as an effect",
           loaded.empty() || fx.empty() ? "it didn't load"
           : loaded != fx ? "they differ" : "");

    std::vector<Run> runs;
    static const char *const PATCHES[] = { "aurora", "constellation", "moire", "pond",
                                           "rings", "wells" };
    static const char *const FX[] = { "feedback", "palette", "soften" };

    for (const auto &r : { std::make_pair(160, 120), std::make_pair(80, 60) })
    {
        for (const char *p : PATCHES)
            runs.push_back({ string(p) + ".dsp", slurp(video + p + ".dsp"), "", r.first,
                             r.second, {} });
        for (const char *f : FX)
            runs.push_back({ string("rings.dsp under ") + f + ".dsp", rings,
                             slurp(video + "fx/" + f + ".dsp"), r.first, r.second, {} });
        runs.push_back({ "expression.dsp", slurp(compiled + "expression.dsp"), "", r.first,
                         r.second, {} });
    }
    for (int seed = 1; seed <= thRandomGraphs; seed++)
    {
        const string name = "random" + std::to_string(seed) + ".dsp";

        runs.push_back({ name, slurp(random + name), "", 80, 60, {} });
    }

    static const char *const INSTRUMENTS[] = { "dxbell", "glass", "rhodes" };
    std::vector<std::vector<uint64_t> > sounded(sizeof INSTRUMENTS / sizeof INSTRUMENTS[0]);

    for (size_t i = 0; i < sounded.size(); i++)
    {
        const string why = playAudio(plugins, dsp + INSTRUMENTS[i] + ".dsp", sounded[i]);

        if (!why.empty())
            report(string(INSTRUMENTS[i]) + ".dsp plays", why);
    }

    /* Nothing compiled yet: every frame on the nodes. */
    expect("rings.dsp runs on its nodes before anything is installed", "on its nodes",
           ranAs(plugins, rings, 160, 120));
    for (Run &r : runs)
    {
        unsigned long restarts = 0;
        const string why = play(plugins, r, r.frames, restarts);

        if (!why.empty())
            report(r.name + " plays", why);
    }

    thCompiledInstall();
    thCompiledAdd(thvExpressionEngine::fingerprint(), thvExpressionEngine::make);
    thRandomInstall();

    for (size_t i = 0; i < sounded.size(); i++)
    {
        std::vector<uint64_t> windows;
        const unsigned long nodes = thSynthTree::windowsRun(false);
        const unsigned long compiled = thSynthTree::windowsRun(true);
        string why = playAudio(plugins, dsp + INSTRUMENTS[i] + ".dsp", windows);

        if (why.empty() && thSynthTree::windowsRun(false) != nodes)
            why = std::to_string(thSynthTree::windowsRun(false) - nodes) +
                  " windows ran on the nodes";
        if (why.empty() && thSynthTree::windowsRun(true) == compiled)
            why = "no window ran compiled";
        for (size_t w = 0; why.empty() && w < windows.size(); w++)
            if (w >= sounded[i].size() || windows[w] != sounded[i][w])
                why = "window " + std::to_string(w) + " differs";
        report(string(INSTRUMENTS[i]) + ".dsp sounds the same compiled", why);
    }

    for (Run &r : runs)
    {
        std::vector<uint64_t> frames;
        unsigned long restarts = 0;
        const unsigned long nodes = thSynthTree::windowsRun(false);
        string why = play(plugins, r, frames, restarts);
        const string name = r.name + ", " + std::to_string(r.w) + "x" + std::to_string(r.h) +
                            ", draws the same frames compiled";

        if (why.empty() && thSynthTree::windowsRun(false) != nodes)
            why = std::to_string(thSynthTree::windowsRun(false) - nodes) +
                  " windows ran on the nodes";
        for (size_t f = 0; why.empty() && f < frames.size(); f++)
            if (f >= r.frames.size() || frames[f] != r.frames[f])
                why = "frame " + std::to_string(f) + " differs";
        if (why.empty() && frames.size() != r.frames.size())
            why = std::to_string(frames.size()) + " frames, not " +
                  std::to_string(r.frames.size());
        if (why.empty() && r.name == "expression.dsp" && restarts == 0)
            why = "no voice came from the pool";
        report(name, why);
    }

    /* What falls back. */
    expect("rings.dsp compiles on a raster", "compiled", ranAs(plugins, rings, 160, 120));
    expect("rings.dsp with one number changed runs on its nodes", "on its nodes",
           ranAs(plugins, edit(rings, "in1 = 0.5", "in1 = 0.6"), 160, 120));
    expect("rings.dsp runs on its nodes with no raster", "on its nodes",
           ranAs(plugins, rings, 0, 0));
    expect("palette.dsp compiles on a 3-channel instrument", "compiled",
           ranAs(plugins, rings, 160, 120, palette));
    expect("palette.dsp on a 1-channel instrument runs on its nodes", "on its nodes",
           ranAs(plugins, edit(rings, "channels = 3;", "channels = 1;"), 160, 120, palette));
    {
        thSynth synth(plugins, 1000, 1152000, 3);

        synth.setRaster(160, 120);
        synth.loadTreeText("dsp", rings, 0, MIDIVALMAX);

        thSynthTree *tree = synth.getChannel(0)->modnode();
        const bool before = tree->compiled();

        tree->edited();
        report("rings.dsp changed in place runs on its nodes",
               !before ? "it didn't compile to begin with"
               : tree->compiled() ? "it's still compiled" : "");
    }
    {
        thSynth synth(plugins, 1000, 1152000, 3);
        string why;

        synth.setRaster(160, 120);
        synth.loadTreeText("dsp", rings, 0, MIDIVALMAX);

        thSynthTree *tree = synth.getChannel(0)->modnode();
        const bool before = tree->compiled();

        report("rings.dsp with a probe on it runs on its nodes",
               !before ? "it didn't compile to begin with"
               : synth.armProbe(0, "ring", "out", why) < 0 ? why
               : tree->compiled() ? "it's still compiled" : "");
    }

    return failed ? 1 : 0;
}
