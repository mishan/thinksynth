/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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
 * voicecheck -- how a channel hands notes to voices.
 *
 * `poly' on the io node is what a .dsp can say about that, and what it claims
 * is about a *sound*: how many voices are audible, and at what pitch. So this
 * harness listens rather than counting. It plays notes into a real thSynth,
 * reads the channel's output back, and measures two things from it -- the
 * level, which says how many voices are sounding, and the rate of the zero
 * crossings, which for a graph that is one sine says which pitch. Nothing
 * here reaches into thMidiChan to ask.
 *
 * What is claimed: `poly' caps the voices. Three notes at once on a
 * `poly = 1' channel are one voice, and the one left is the newest; a channel
 * that asks for no limit plays all three.
 *
 * Its .dsp files are written here rather than taken from the corpus, for the
 * reason argtype gives about its own: the cases that matter are ones the
 * corpus cannot contain, since every shipped graph takes the default.
 *
 *     scripts/voicecheck -p build/plugins/
 *
 * Exit status is the number of failures.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "think.h"

using std::string;
using std::vector;

static int failed = 0;

static void ok (const string &what)
{
    printf("ok    %s\n", what.c_str());
}

static void fail (const string &what, const string &detail)
{
    printf("FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : ": ",
           detail.c_str());
    failed++;
}

static void okOrFail (bool good, const string &what, const string &detail)
{
    if (good)
        ok(what);
    else
        fail(what, detail);
}

static string num (double v)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%.6g", v);

    return buf;
}

/* Not "/tmp/...", for the reason argtype spells out: this is a CTest gate and
   the Windows runner has no such directory. */
static string scratchPath (const char *leaf)
{
    std::error_code ec;

    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);

    if (ec)
        dir = ".";

    return (dir / leaf).string();
}

static bool writeFile (const string &path, const string &text)
{
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);

    out << text;
    out.close();

    if (out.good())
        return true;

    fail("could not write", path);

    return false;
}

/* ---- the graphs --------------------------------------------------------
 *
 * One sine at the note's pitch, at the note's velocity, through an envelope
 * that reaches its sustain in a millisecond and holds there. The level is
 * then a count of voices and the zero crossings are a pitch, which is all
 * either measurement below needs.
 *
 * `io' is whatever the case under test wants on the io node -- `poly = 1',
 * or nothing at all.
 */
static string graph (const string &io)
{
    return
        string("name \"voicecheck\";\n\n") +
        "node ionode {\n"
        "    channels = 1;\n"
        "    out0 = vca->out;\n"
        "    play = env->play;\n" + io +
        "};\n\n"
        "node freq misc::midi2freq {\n"
        "    note = ionode->note;\n"
        "};\n\n"
        "node osc osc::simple {\n"
        "    freq = freq->out;\n"
        "    waveform = 0;\n"
        "    amp = ionode->velocity;\n"
        "};\n\n"
        "node env env::adsr {\n"
        "    a = 1 ms;\n"
        "    d = 1 ms;\n"
        "    s = th_max;\n"
        "    r = 200 ms;\n"
        "    trigger = ionode->trigger;\n"
        "};\n\n"
        "node vca mixer::mul {\n"
        "    in0 = osc->out;\n"
        "    in1 = env->out;\n"
        "};\n\n"
        "io ionode;\n";
}

/* ---- measuring ---------------------------------------------------------- */

static double rms (const vector<float> &v)
{
    double sum = 0;

    for (size_t i = 0; i < v.size(); i++)
        sum += (double)v[i] * v[i];

    return v.empty() ? 0 : sqrt(sum / v.size());
}

/* Hertz, from the upward zero crossings.
 *
 * Exact for one sine and meaningless for two, which is deliberate: where a
 * case is about how many voices are sounding the level is what is read, and
 * where it is about which pitch there is only ever one voice to hear.
 *
 * Measured from the first crossing to the last rather than over the whole
 * buffer. Crossings are whole numbers, so dividing a count by the buffer
 * length quantises the answer to one cycle in it -- eleven hertz over a
 * window, which is most of a semitone at middle C and enough to fail a pitch
 * that is exactly right. Between two crossings there is a whole number of
 * cycles by construction, and what is divided is a sample count.
 *
 * The threshold keeps a run of samples sitting on zero -- a released voice --
 * from counting as a crossing per sample. */
static double pitch (const vector<float> &v, int rate)
{
    const double floorAt = 1e-4;

    int crossings = 0;
    size_t first = 0, last = 0;

    /* False, so that a buffer beginning part way up a cycle does not count
       its own first sample as a crossing. It would be a crossing three
       quarters of a cycle early, which over four windows of middle C reads as
       a pitch one and a third percent sharp -- close enough to look like a
       tuning constant and not like an off-by-one. */
    bool below = false;

    for (size_t i = 0; i < v.size(); i++)
    {
        if (below && v[i] > floorAt)
        {
            if (crossings == 0)
                first = i;

            last = i;
            crossings++;
            below = false;
        }
        else if (!below && v[i] < -floorAt)
        {
            below = true;
        }
    }

    if (crossings < 2 || last == first)
        return 0;

    return (double)(crossings - 1) * rate / (double)(last - first);
}

/* ---- a session ---------------------------------------------------------
 *
 * A synth with one channel loaded, notes played into it by hand, and the
 * channel's window read back after each step. Commands are queued and drained
 * by process(), so a note played here is sounding from the next window on --
 * the same path the audio thread takes.
 */
struct Session
{
    thSynth synth;
    vector<float> heard;

    Session (const string &pluginPath)
        : synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES) {}

    bool load (const string &file)
    {
        return synth.loadTree(file, 0, 100) != NULL;
    }

    /* `windows' windows of channel 0, appended to `heard'. */
    void run (int windows)
    {
        const int len = synth.getWindowlen();

        for (int w = 0; w < windows; w++)
        {
            synth.process();

            const float *out = synth.getOutput();

            heard.insert(heard.end(), out, out + len);
        }
    }

    /* Everything since the last look. */
    vector<float> take (void)
    {
        vector<float> got;

        got.swap(heard);

        return got;
    }

    /* Run, and answer only the second half: the first is where the pitch is
       still arriving and the envelope still moving. */
    vector<float> settled (int windows)
    {
        take();
        run(windows);

        vector<float> all = take();

        return vector<float>(all.begin() + all.size() / 2, all.end());
    }
};

/* Within `tol' of each other, proportionally. */
static bool near (double a, double b, double tol)
{
    if (b == 0)
        return fabs(a) < tol;

    return fabs(a - b) <= fabs(b) * tol;
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;

    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            pluginPath = argv[++i];

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    const string file = scratchPath("voicecheck-scratch.dsp");
    const int rate = TH_DEFAULT_SAMPLES;

    /* The C above middle C, which misc::midi2freq puts note 72 at. */
    const double C5 = 523.2511;

    /* ---- polyphony is what `poly' says -------------------------------- */

    /* Three keys at once through a channel that may play one voice. The
       polyphony check retires from the oldest held voice forward, so what is
       left is the newest -- and it is one voice, at one pitch, which is what
       both measurements below say together. */
    if (writeFile(file, graph("    poly = 1;\n")))
    {
        Session s(pluginPath);

        if (!s.load(file))
            fail("a `poly = 1' graph loads", "");
        else
        {
            s.synth.addNote(0, 60, 20);
            vector<float> one = s.settled(8);

            s.synth.addNote(0, 64, 20);
            s.synth.addNote(0, 72, 20);

            vector<float> three = s.settled(8);

            okOrFail(near(rms(three), rms(one), 0.02) &&
                     near(pitch(three, rate), C5, 0.01),
                     "poly: three notes on a one-voice channel are one voice, "
                     "and it is the newest",
                     "one " + num(rms(one)) + ", three " + num(rms(three)) +
                     " at " + num(pitch(three, rate)) + " Hz");
        }
    }

    /* And with no `poly' at all, the same three notes are three voices. This
       is the control: without it, the case above would pass on a channel that
       had simply stopped playing notes. */
    if (writeFile(file, graph("")))
    {
        Session s(pluginPath);

        if (!s.load(file))
            fail("a graph with no `poly' loads", "");
        else
        {
            s.synth.addNote(0, 60, 20);
            vector<float> one = s.settled(8);

            s.synth.addNote(0, 64, 20);
            s.synth.addNote(0, 72, 20);

            vector<float> three = s.settled(8);

            /* Three sines at three pitches sum incoherently, so the level
               goes up by about the root of three. */
            okOrFail(rms(three) > rms(one) * 1.5,
                     "poly: a channel that does not ask for a limit plays all "
                     "three", "one " + num(rms(one)) + ", three " +
                              num(rms(three)));
        }
    }

    remove(file.c_str());

    printf("\n%d failure(s)\n", failed);

    return failed;
}
