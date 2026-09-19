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
 * fxcheck -- the graph that runs on a channel's summed voices.
 *
 * A channel effect is the one graph in the engine that is not anybody's note,
 * and everything worth checking about it follows from that:
 *
 *   it runs when nothing is playing.   A delay's tail is exactly the part
 *      that comes out after the last note-off. An effect that only ran while
 *      a voice sounded would cut off the one thing it exists for.
 *
 *   it is fed the channel's sum.       An impulse in comes out delayed by
 *      the number of samples the graph asked for, which is the whole claim
 *      about in0 -- that it carries the audio, at the right time, in the
 *      right channel.
 *
 *   it replaces what it was fed.       The dry signal is the graph's to mix,
 *      so a graph that passes in0 straight to out0 changes nothing and a
 *      graph that halves it halves the channel.
 *
 *   its chanargs are its own.          `fx.delay' is the effect's, `delay' is
 *      the instrument's, and an instrument and an effect that both declare
 *      `@a' do not collide.
 *
 *   it comes and goes.                 Loading one, replacing it, taking it
 *      off, and loading an instrument over the top -- the last of which has
 *      to take the effect with it, since the channel owns it.
 *
 *   it is refused when it is not one.  An instrument loaded as an effect
 *      would run every window with nothing driving it; a graph with no in0 is
 *      not an effect and is told so.
 *
 *   it can hear a second channel.     side0..side<N-1> carry another
 *      channel's output -- a vocoder's carrier, a compressor's key -- and
 *      carry *this* window of it, which is what the engine running that
 *      channel first is for. A channel that would end up waiting on itself
 *      is refused at load.
 *
 * Plus the guard: an effect that goes non-finite hands the channel the dry
 * signal rather than a window of NaN, because by then the voices are summed
 * and there is no bad one left to drop.
 *
 * Its .dsp files are written here, for the reason argtype gives about its
 * own, and it also loads the shipped effect graphs in dsp/fx/ -- which the
 * note-playing corpus gates skip, because an effect has no note to play.
 *
 *     scripts/fxcheck -p build/plugins/ dsp/fx/echo.dsp
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
 * The instrument is an impulse: one window of a square at full scale through
 * an envelope with no attack and no decay, so the channel's sum is something
 * whose arrival time can be read off a buffer.
 */
static string instrument (const string &extra)
{
    return
        string("name \"fxcheck-inst\";\n\n") + extra +
        "node ionode {\n"
        "    channels = 2;\n"
        "    out0 = vca->out;\n"
        "    out1 = vca->out;\n"
        "    play = env->play;\n"
        "};\n\n"
        "node osc osc::simple {\n"
        "    freq = 40;\n"
        "    waveform = 0;\n"
        "    amp = 0.5;\n"
        "};\n\n"
        "node env env::adsr {\n"
        "    a = 1 ms;\n"
        "    d = 1 ms;\n"
        "    s = th_max;\n"
        "    r = 1 ms;\n"
        "    trigger = ionode->trigger;\n"
        "};\n\n"
        "node vca mixer::mul {\n"
        "    in0 = osc->out;\n"
        "    in1 = env->out;\n"
        "};\n\n"
        "io ionode;\n";
}

/* An effect, with whatever `body' wires out0 and out1 to. */
static string effect (const string &controls, const string &nodes,
                      const string &out0, const string &out1)
{
    return
        string("name \"fxcheck-fx\";\n\n") + controls +
        "node ionode {\n"
        "    channels = 2;\n"
        "    in0 = 0;\n"
        "    in1 = 0;\n"
        "    out0 = " + out0 + ";\n"
        "    out1 = " + out1 + ";\n"
        "};\n\n" + nodes +
        "io ionode;\n";
}

/* The same, declaring side0 and side1 as well: an effect that listens to a
 * second channel. A file asks for them the way it asks for in<N>, and the
 * engine writes them only where they were asked for.
 */
static string sideEffect (const string &nodes, const string &out0,
                          const string &out1)
{
    return
        string("name \"fxcheck-side\";\n\n") +
        "node ionode {\n"
        "    channels = 2;\n"
        "    in0 = 0;\n"
        "    in1 = 0;\n"
        "    side0 = 0;\n"
        "    side1 = 0;\n"
        "    out0 = " + out0 + ";\n"
        "    out1 = " + out1 + ";\n"
        "};\n\n" + nodes +
        "io ionode;\n";
}

/* ---- measuring ---------------------------------------------------------- */

static double peak (const vector<float> &v)
{
    double top = 0;

    for (size_t i = 0; i < v.size(); i++)
        if (fabs(v[i]) > top)
            top = fabs(v[i]);

    return top;
}

/* The first sample past `floorAt', or -1. */
static long firstAbove (const vector<float> &v, double floorAt)
{
    for (size_t i = 0; i < v.size(); i++)
        if (fabs(v[i]) > floorAt)
            return (long)i;

    return -1;
}

static bool allFinite (const vector<float> &v)
{
    for (size_t i = 0; i < v.size(); i++)
        if (!thIsFinite(v[i]))
            return false;

    return true;
}

/* Does this graph ask for a second channel? A file that declares side0 is
   one whose point is the side -- and the shipped-graph loop below has to
   give it one, since a vocoder with no carrier is a vocoder doing nothing.
   Read off the text rather than off the loaded tree: what is wanted is the
   author's declaration, and the engine invents nothing here.

   A comment is not a declaration, though, and a header explaining what
   side0 is for is the likeliest place in the file to write it down. `#'
   runs to the end of its line in this grammar, so dropping what follows one
   leaves exactly the lines the parser would have read. */
static bool declaresSide (const string &path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    const string want = string(SIDEPREFIX) + "0";
    string line;

    while (std::getline(in, line))
    {
        const string::size_type hash = line.find('#');

        if (hash != string::npos)
            line.erase(hash);

        if (line.find(want) != string::npos)
            return true;
    }

    return false;
}

/* ---- a session ---------------------------------------------------------- */

struct Session
{
    thSynth synth;
    vector<float> left;

    Session (const string &pluginPath)
        : synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES) {}

    /* `windows' windows of channel 0's left side, appended. */
    void run (int windows)
    {
        const int len = synth.getWindowlen();

        for (int w = 0; w < windows; w++)
        {
            synth.process();

            const float *out = synth.getOutput();

            left.insert(left.end(), out, out + len);
        }
    }

    vector<float> take (void)
    {
        vector<float> got;

        got.swap(left);

        return got;
    }
};

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;
    vector<string> shipped;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            pluginPath = argv[++i];
        else
            shipped.push_back(argv[i]);
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    const string instFile = scratchPath("fxcheck-inst.dsp");
    const string fxFile = scratchPath("fxcheck-fx.dsp");
    const string fxFile2 = scratchPath("fxcheck-fx2.dsp");

    /* ---- an effect runs with nothing playing, and delays what it is fed -- */

    /* One note, one window, then silence -- and a delay whose tap is a known
     * number of samples back. What comes out is the note, and then the note
     * again `delay' samples later, out of a channel that has had no voice on
     * it since the first window. Both halves of the claim in one render.
     */
    {
        const int delay = 5000;

        /* The dry beside the wet, so one buffer holds both the note and its
           repeat and the distance between them is the measurement. An effect
           that passed only the wet would give two readings of the same
           event. */
        const string fx = effect(
            "", string(
            "node ring delay::echo {\n"
            "    in = ionode->in0;\n"
            "    size = 20000;\n"
            "    delay = ") + num(delay) + ";\n"
            "    feedback = 0;\n"
            "    dry = 0;\n"
            "};\n\n"
            "node both math::add {\n"
            "    in0 = ionode->in0;\n"
            "    in1 = ring->out;\n"
            "};\n\n", "both->out", "both->out");

        if (writeFile(instFile, instrument("")) && writeFile(fxFile, fx))
        {
            Session s(pluginPath);

            if (s.synth.loadTree(instFile, 0, 100) == NULL)
                fail("the instrument loads", "");
            else if (s.synth.loadEffect(fxFile, 0) == NULL)
                fail("the effect loads onto the channel", "");
            else
            {
                s.synth.addNote(0, 60, 100);
                s.run(2);
                s.synth.delNote(0, 60);

                /* Well past the note and past the tap. */
                s.run(10);

                vector<float> heard = s.take();

                const long dry = firstAbove(heard, 0.01);

                /* Where the wet starts, looked for after the dry has gone
                   quiet again. The note is a few milliseconds long and the
                   tap is a hundred and thirteen. */
                vector<float> tail(heard.begin() + delay / 2, heard.end());

                const long wet = firstAbove(tail, 0.01);

                okOrFail(dry >= 0 && wet >= 0 &&
                         labs((wet + delay / 2) - (dry + delay)) < 64,
                         "an effect is fed the channel's sum, and its output "
                         "arrives where the graph put it",
                         "note at " + num((double)dry) + ", repeat at " +
                         num((double)(wet + delay / 2)) + ", tap " +
                         num(delay));

                okOrFail(wet >= 0,
                         "an effect runs when no voice is sounding, which is "
                         "what a tail is", "nothing came out after the note");
            }
        }
    }

    /* ---- what it writes is what the channel plays --------------------- */

    /* A graph that hands back half of what it was given halves the channel,
     * and one that hands back what it was given changes nothing. Between
     * them they say the outputs *replace* the buffer rather than adding to
     * it, which is what makes a dry/wet mix the graph's business.
     */
    {
        const string same = effect("", "", "ionode->in0", "ionode->in1");
        const string half = effect("",
            "node cut math::mul {\n"
            "    in0 = ionode->in0;\n"
            "    in1 = 0.5;\n"
            "};\n\n", "cut->out", "cut->out");

        double bare = 0, through = 0, halved = 0;

        for (int pass = 0; pass < 3; pass++)
        {
            if (!writeFile(instFile, instrument("")))
                break;

            if (pass == 1 && !writeFile(fxFile, same))
                break;

            if (pass == 2 && !writeFile(fxFile, half))
                break;

            Session s(pluginPath);

            if (s.synth.loadTree(instFile, 0, 100) == NULL)
            {
                fail("the instrument loads", "");
                break;
            }

            if (pass > 0 && s.synth.loadEffect(fxFile, 0) == NULL)
            {
                fail("the effect loads", "");
                break;
            }

            s.synth.addNote(0, 60, 100);
            s.run(4);

            const double got = peak(s.take());

            if (pass == 0) bare = got;
            else if (pass == 1) through = got;
            else halved = got;
        }

        okOrFail(bare > 0 && fabs(through - bare) < bare * 0.001 &&
                 fabs(halved - bare * 0.5) < bare * 0.01,
                 "an effect's outputs replace the channel's buffer: a "
                 "pass-through changes nothing and a halving halves it",
                 "bare " + num(bare) + ", through " + num(through) +
                 ", halved " + num(halved));
    }

    /* ---- the chanargs are the effect's, under `fx.' -------------------- */

    /* Both graphs declare `@a'. The instrument's is a number its envelope
     * reads; the effect's is a gain. Setting one must not move the other,
     * which is the whole reason the two maps are kept apart.
     */
    {
        const string inst = instrument("    @a = 0.25;\n"
                                       "    @a.widget = 1;\n"
                                       "    @a.min = 0;\n"
                                       "    @a.max = 1;\n");
        const string fx = effect("    @a = 1;\n"
                                 "    @a.widget = 1;\n"
                                 "    @a.min = 0;\n"
                                 "    @a.max = 1;\n",
            "node gain math::mul {\n"
            "    in0 = ionode->in0;\n"
            "    in1 = @a;\n"
            "};\n\n", "gain->out", "gain->out");

        if (writeFile(instFile, inst) && writeFile(fxFile, fx))
        {
            Session s(pluginPath);

            if (s.synth.loadTree(instFile, 0, 100) == NULL ||
                s.synth.loadEffect(fxFile, 0) == NULL)
                fail("the pair loads", "");
            else
            {
                thArg *mine = s.synth.getChanArg(0, "a");
                thArg *theirs = s.synth.getChanArg(0, "fx.a");

                if (mine == NULL || theirs == NULL)
                    fail("both `a's are reachable",
                         string(mine ? "" : "the instrument's is not; ") +
                         (theirs ? "" : "the effect's is not"));
                else if (mine == theirs)
                    fail("the two `a's are two args", "they are one");
                else
                {
                    theirs->setValue(0.25);

                    s.synth.addNote(0, 60, 100);
                    s.run(4);

                    const double quiet = peak(s.take());

                    theirs->setValue(1);

                    Session s2(pluginPath);

                    if (s2.synth.loadTree(instFile, 0, 100) == NULL ||
                        s2.synth.loadEffect(fxFile, 0) == NULL)
                        fail("the pair loads again", "");
                    else
                    {
                        s2.synth.addNote(0, 60, 100);
                        s2.run(4);

                        const double loud = peak(s2.take());

                        okOrFail(loud > 0 &&
                                 fabs(quiet - loud * 0.25) < loud * 0.01 &&
                                 (*mine)[0] == 0.25,
                                 "`fx.a' is the effect's and `a' is the "
                                 "instrument's, and setting one leaves the "
                                 "other where it was",
                                 "gain 1 " + num(loud) + ", gain 0.25 " +
                                 num(quiet) + ", instrument's a " +
                                 num((*mine)[0]));
                    }
                }
            }
        }
    }

    /* ---- coming and going --------------------------------------------- */

    /* Replaced, taken off, and then the instrument reloaded over the top --
     * which has to take the effect with it, because the channel owns it and
     * loading an instrument builds a new channel.
     */
    {
        const string silent = effect("", "", "0", "0");
        const string through = effect("", "", "ionode->in0", "ionode->in1");

        if (writeFile(instFile, instrument("")) &&
            writeFile(fxFile, silent) && writeFile(fxFile2, through))
        {
            Session s(pluginPath);

            if (s.synth.loadTree(instFile, 0, 100) == NULL ||
                s.synth.loadEffect(fxFile, 0) == NULL)
                fail("the pair loads", "");
            else
            {
                s.synth.addNote(0, 60, 100);
                s.run(4);

                const double muted = peak(s.take());

                /* Replaced by one that passes the signal. */
                const bool swapped = s.synth.loadEffect(fxFile2, 0) != NULL;

                s.synth.addNote(0, 64, 100);
                s.run(4);

                const double passed = peak(s.take());

                /* Taken off. */
                const bool removed = s.synth.removeEffect(0);

                s.synth.addNote(0, 67, 100);
                s.run(4);

                const double bare = peak(s.take());

                okOrFail(swapped && removed && muted < 1e-6 && passed > 0 &&
                         bare > 0,
                         "an effect can be replaced and taken off again",
                         "silent " + num(muted) + ", through " +
                         num(passed) + ", none " + num(bare));

                /* And an instrument loaded over the top takes it with it. */
                if (s.synth.loadEffect(fxFile, 0) == NULL)
                    fail("the silent effect goes back on", "");
                else if (s.synth.loadTree(instFile, 0, 100) == NULL)
                    fail("the instrument reloads", "");
                else
                {
                    s.synth.addNote(0, 72, 100);
                    s.run(4);

                    okOrFail(peak(s.take()) > 0 &&
                             s.synth.getEffect(0) == NULL,
                             "loading an instrument takes the channel's "
                             "effect off with it", "");
                }
            }
        }
    }

    /* ---- a second channel, in side<N> ---------------------------------- */

    /* The whole claim in one number: nothing comes out.
     *
     * Channel 0 plays no notes and carries an effect that hands back the
     * *inverse* of what channel 1 is playing; channel 1 plays a note. The two
     * are summed by the engine, so if side0 is channel 1's output the mix
     * cancels to silence, and if it is anything else -- the wrong channel,
     * the right channel a window late, or zeros -- it does not.
     *
     * The window-late case is the one worth naming, because it is what the
     * engine's channel ordering exists to stop and it is invisible in a peak:
     * two copies of a note a window apart, one inverted, is not quieter than
     * one copy. It is a different signal, and this measures the difference.
     */
    {
        const string fx = sideEffect(
            "node flipl math::sub {\n"
            "    in0 = 0;\n"
            "    in1 = ionode->side0;\n"
            "};\n\n"
            "node flipr math::sub {\n"
            "    in0 = 0;\n"
            "    in1 = ionode->side1;\n"
            "};\n\n", "flipl->out", "flipr->out");

        if (writeFile(instFile, instrument("")) && writeFile(fxFile, fx))
        {
            double alone = 0, cancelled = 0;

            for (int pass = 0; pass < 2; pass++)
            {
                Session s(pluginPath);

                /* The listener on channel 0 and the thing it listens to on
                   channel 1 -- the way round that needs the reordering, since
                   the channels run in index order otherwise. */
                if (s.synth.loadTree(instFile, 0, 100) == NULL ||
                    s.synth.loadTree(instFile, 1, 100) == NULL)
                {
                    fail("two instruments load", "");
                    break;
                }

                /* Pass 0 is the control: the same graph with no side, so
                   side0 is zeros and channel 0 contributes nothing. */
                if (pass == 1 && s.synth.loadEffect(fxFile, 0, 1) == NULL)
                {
                    fail("an effect loads with a side channel", "");
                    break;
                }

                s.synth.addNote(1, 60, 100);
                s.run(4);

                const double got = peak(s.take());

                if (pass == 0) alone = got;
                else cancelled = got;
            }

            okOrFail(alone > 0 && cancelled < alone * 0.001,
                     "side0 carries the channel the effect was given, "
                     "sample for sample and window for window",
                     "channel 1 alone " + num(alone) + ", against its own "
                     "inverse " + num(cancelled));
        }
    }

    /* What is in side0 when a piece named no side: this channel.
     *
     * A graph reading side0 therefore always has a signal there, which is
     * what lets fx/comp.dsp be an ordinary compressor and a sidechain
     * compressor without a knob to say which. Measured with an effect that
     * is nothing but `out0 = side0': with nobody named it changes nothing,
     * and with an empty channel named it is silence -- because that is what
     * an empty channel is putting out, and the two are different questions.
     */
    {
        const string fx = sideEffect("", "ionode->side0", "ionode->side1");

        if (writeFile(instFile, instrument("")) && writeFile(fxFile, fx))
        {
            double bare = 0, mine = 0, empty = -1;

            for (int pass = 0; pass < 3; pass++)
            {
                Session s(pluginPath);

                if (s.synth.loadTree(instFile, 0, 100) == NULL)
                {
                    fail("the instrument loads", "");
                    break;
                }

                /* Pass 0 has no effect at all, which is the level the other
                   two are read against. */
                if (pass == 1 && s.synth.loadEffect(fxFile, 0) == NULL)
                {
                    fail("an effect loads with no side named", "");
                    break;
                }

                /* Channel 5 has nothing on it. */
                if (pass == 2 && s.synth.loadEffect(fxFile, 0, 5) == NULL)
                {
                    fail("an effect loads with a side on an empty channel",
                         "");
                    break;
                }

                s.synth.addNote(0, 60, 100);
                s.run(4);

                const double got = peak(s.take());

                if (pass == 0) bare = got;
                else if (pass == 1) mine = got;
                else empty = got;
            }

            okOrFail(bare > 0 && fabs(mine - bare) < bare * 0.001 &&
                     empty >= 0 && empty < 1e-6,
                     "an effect that names no side hears its own channel "
                     "there, and one that names an empty channel hears "
                     "silence",
                     "bare " + num(bare) + ", unnamed side " + num(mine) +
                     ", empty side " + num(empty));
        }
    }

    /* And the ring. A channel that hears itself -- directly, or around a
     * loop of channels that each hear the next -- cannot be run in an order
     * that satisfies it, so it is refused where it is asked for rather than
     * sorted out a window at a time on the audio thread.
     */
    {
        const string fx = sideEffect("", "ionode->side0", "ionode->side1");

        if (writeFile(instFile, instrument("")) && writeFile(fxFile, fx))
        {
            Session s(pluginPath);

            if (s.synth.loadTree(instFile, 0, 100) == NULL ||
                s.synth.loadTree(instFile, 1, 100) == NULL)
                fail("two instruments load", "");
            else
            {
                const bool refusedSelf =
                    s.synth.loadEffect(fxFile, 0, 0) == NULL &&
                    s.synth.getEffect(0) == NULL;

                /* 0 hears 1, and then 1 asked to hear 0: the two-channel
                   ring, which is the one a piece would write by accident. */
                const bool tookFirst =
                    s.synth.loadEffect(fxFile, 0, 1) != NULL;
                const bool refusedRing =
                    s.synth.loadEffect(fxFile, 1, 0) == NULL &&
                    s.synth.getEffect(1) == NULL;

                /* And a channel that does not exist. */
                const bool refusedRange =
                    s.synth.loadEffect(fxFile, 2, 99) == NULL;

                okOrFail(refusedSelf && tookFirst && refusedRing &&
                         refusedRange,
                         "a channel may not end up waiting on itself, and a "
                         "side that is not a channel is refused",
                         string(refusedSelf ? "" : "it heard itself; ") +
                         (tookFirst ? "" : "the first leg was refused; ") +
                         (refusedRing ? "" : "the ring closed; ") +
                         (refusedRange ? "" : "channel 99 was taken"));
            }
        }
    }

    /* ---- what is not an effect is refused ------------------------------ */

    {
        Session s(pluginPath);

        if (!writeFile(instFile, instrument("")))
            ;
        else if (s.synth.loadTree(instFile, 0, 100) == NULL)
            fail("the instrument loads", "");
        else
        {
            /* The instrument itself, as an effect: no in0. */
            const bool refusedNoInput =
                s.synth.loadEffect(instFile, 0) == NULL;

            /* And onto a channel with nothing on it. */
            const bool refusedNoChannel =
                writeFile(fxFile, effect("", "", "ionode->in0", "ionode->in1"))
                && s.synth.loadEffect(fxFile, 5) == NULL;

            okOrFail(refusedNoInput && refusedNoChannel,
                     "a graph with no in0 is not an effect, and an effect "
                     "needs a channel to run on",
                     string(refusedNoInput ? "" : "the instrument was taken; ")
                     + (refusedNoChannel ? "" : "the empty channel was too"));
        }
    }

    /* ---- and one that diverges hands back the dry signal ---------------- */

    {
        /* 1/0. The graph is finite until the engine reads it. */
        const string bad = effect("",
            "node blow math::div {\n"
            "    in0 = 1;\n"
            "    in1 = 0;\n"
            "};\n\n"
            "node mix math::add {\n"
            "    in0 = ionode->in0;\n"
            "    in1 = blow->out;\n"
            "};\n\n", "mix->out", "mix->out");

        if (writeFile(instFile, instrument("")) && writeFile(fxFile, bad))
        {
            Session s(pluginPath);

            if (s.synth.loadTree(instFile, 0, 100) == NULL ||
                s.synth.loadEffect(fxFile, 0) == NULL)
                fail("the pair loads", "");
            else
            {
                s.synth.addNote(0, 60, 100);
                s.run(4);

                vector<float> heard = s.take();

                okOrFail(allFinite(heard) && peak(heard) > 0,
                         "an effect that goes non-finite hands the channel "
                         "the dry signal rather than a window of NaN",
                         "peak " + num(peak(heard)));
            }
        }
    }

    /* ---- the same graph on the mix -------------------------------------- */

    /* A master effect is this object with no channel under it: it is handed
     * what every channel summed to, after the mix and before the master gain
     * and the limiter. What is asked here is the part that is not the
     * channel's -- that it is fed the *sum*, that its output replaces it,
     * that it runs with nothing playing, and that it comes off again.
     */
    {
        const int delay = 5000;

        const string fx = effect(
            "", string(
            "node ring delay::echo {\n"
            "    in = ionode->in0;\n"
            "    size = 20000;\n"
            "    delay = ") + num(delay) + ";\n"
            "    feedback = 0;\n"
            "    dry = 0;\n"
            "};\n\n"
            "node both math::add {\n"
            "    in0 = ionode->in0;\n"
            "    in1 = ring->out;\n"
            "};\n\n", "both->out", "both->out");

        if (writeFile(instFile, instrument("")) && writeFile(fxFile, fx))
        {
            Session s(pluginPath);

            /* Two channels, so "the sum" is a sum. A master effect that
               reached one channel's buffer instead of the mix would repeat
               one of these notes and not the other. */
            if (s.synth.loadTree(instFile, 0, 100) == NULL ||
                s.synth.loadTree(instFile, 1, 100) == NULL)
                fail("two instruments load", "");
            else if (s.synth.loadMasterEffect(fxFile) == NULL)
                fail("the effect loads onto the mix", "");
            else
            {
                s.synth.addNote(0, 60, 100);
                s.synth.addNote(1, 67, 100);
                s.run(2);
                s.synth.delNote(0, 60);
                s.synth.delNote(1, 67);
                s.run(10);

                vector<float> heard = s.take();

                const long dry = firstAbove(heard, 0.01);

                vector<float> tail(heard.begin() + delay / 2, heard.end());

                const long wet = firstAbove(tail, 0.01);
                const long at = wet < 0 ? -1 : wet + delay / 2;

                okOrFail(dry >= 0 && at > 0 && labs(at - dry - delay) < 64,
                         "a master effect is fed the sum of every channel, "
                         "and what it returns is what goes out",
                         "the mix arrived at " + num((double)dry) +
                         " and came back at " + num((double)at) +
                         ", wanted " + num((double)(dry + delay)));

                /* The repeat lands long after both notes were released,
                   which is the tail a master reverb is for. */
                okOrFail(at > 2 * TH_DEFAULT_WINDOW_LENGTH,
                         "a master effect runs when no voice is sounding",
                         "");

                /* And off again: what the audio thread hears after the
                   removal is the mix, undelayed and unrepeated. */
                if (!s.synth.removeMasterEffect())
                    fail("the master effect comes off", "");
                else
                {
                    s.run(1);            /* the swap is a command       */
                    s.take();

                    s.synth.addNote(0, 60, 100);
                    s.run(2);
                    s.synth.delNote(0, 60);
                    s.run(10);

                    vector<float> after = s.take();
                    vector<float> quiet(after.begin() + delay / 2,
                                        after.end());

                    okOrFail(peak(after) > 0 && firstAbove(quiet, 0.01) < 0,
                             "a master effect comes off again, and the mix "
                             "stops being delayed",
                             "peak " + num(peak(after)));
                }
            }
        }
    }

    /* An instrument is not an effect here either: the graph has no in0, so
       there is nowhere for the mix to go. */
    {
        if (writeFile(instFile, instrument("")))
        {
            Session s(pluginPath);

            okOrFail(s.synth.loadMasterEffect(instFile) == NULL,
                     "a graph with no in0 is not a master effect either",
                     "");
        }
    }

    /* And the guard: a master effect that diverges hands back the mix it was
       given. There is no bad voice to drop by then -- every channel is
       already summed -- so the choice is the dry mix or a window of NaN. */
    {
        const string bad = effect(
            "", string(
            "node blow math::div {\n"
            "    in0 = 1;\n"
            "    in1 = 0;\n"
            "};\n\n"
            "node mix math::add {\n"
            "    in0 = ionode->in0;\n"
            "    in1 = blow->out;\n"
            "};\n\n"), "mix->out", "mix->out");

        if (writeFile(instFile, instrument("")) && writeFile(fxFile, bad))
        {
            Session s(pluginPath);

            if (s.synth.loadTree(instFile, 0, 100) == NULL ||
                s.synth.loadMasterEffect(fxFile) == NULL)
                fail("the pair loads", "");
            else
            {
                s.synth.addNote(0, 60, 100);
                s.run(4);

                vector<float> heard = s.take();

                okOrFail(allFinite(heard) && peak(heard) > 0,
                         "a master effect that goes non-finite hands back "
                         "the mix rather than a window of NaN",
                         "peak " + num(peak(heard)));
            }
        }
    }

    /* ---- the shipped effect graphs ------------------------------------- */

    /* They are not in the corpus gates: those play notes, and an effect has
       no note to play. So they are loaded here, as effects, on a channel. */
    for (size_t i = 0; i < shipped.size(); i++)
    {
        Session s(pluginPath);

        if (!writeFile(instFile, instrument("")))
            break;

        if (s.synth.loadTree(instFile, 0, 100) == NULL)
        {
            fail("the instrument loads", "");
            break;
        }

        if (s.synth.loadEffect(shipped[i], 0) == NULL)
        {
            fail(shipped[i] + " loads as an effect", "");
            continue;
        }

        s.synth.addNote(0, 60, 100);
        s.run(2);
        s.synth.delNote(0, 60);
        s.run(30);

        vector<float> heard = s.take();

        okOrFail(allFinite(heard) && peak(heard) > 0,
                 shipped[i] + " runs a note through and stays finite",
                 "peak " + num(peak(heard)));

        /* And again with a carrier, for the ones that asked for one. A
         * graph that declares side0 is a graph whose whole job is the
         * second channel -- a vocoder with no carrier has nothing to put
         * the modulator's envelopes onto -- so loading it without one says
         * nothing about whether it works.
         *
         * Twice, against the same graph given a side that is an empty
         * channel rather than against no side at all: both renders carry
         * channel 1's own note in the mix, both run the same graph, and
         * the only thing that differs between them is what the effect was
         * handed. What is measured is therefore the side and nothing else.
         *
         * A difference rather than a level, because the effect's share of
         * the mix is not the mix: the carrier is in there at full size
         * either way, and a vocoder's output against a square wave is a
         * few per cent of it. Subtracting says exactly how much of what
         * came out came from the side.
         */
        if (!declaresSide(shipped[i]))
            continue;

        vector<float> both[2];
        bool rendered = true;

        for (int pass = 0; pass < 2; pass++)
        {
            Session c(pluginPath);

            if (c.synth.loadTree(instFile, 0, 100) == NULL ||
                c.synth.loadTree(instFile, 1, 100) == NULL)
            {
                fail("two instruments load", "");
                rendered = false;
                break;
            }

            /* Channel 5 has nothing on it, so pass 1 is the same effect
               fed a side of silence. */
            if (c.synth.loadEffect(shipped[i], 0, pass == 0 ? 1 : 5) == NULL)
            {
                fail(shipped[i] + " loads with a side channel", "");
                rendered = false;
                break;
            }

            c.synth.addNote(0, 60, 100);
            c.synth.addNote(1, 67, 100);
            c.run(4);
            c.synth.delNote(0, 60);
            c.synth.delNote(1, 67);
            c.run(8);

            both[pass] = c.take();

            if (!allFinite(both[pass]))
            {
                fail(shipped[i] + " stays finite with a side channel", "");
                rendered = false;
                break;
            }
        }

        /* One of the two renders never happened, and its failure has been
           counted already. Measuring a difference against the empty vector
           it left behind would only report a second, emptier way to say the
           same thing. */
        if (!rendered)
            continue;

        double came = 0;
        const double whole = peak(both[0]);

        for (size_t j = 0; j < both[0].size() && j < both[1].size(); j++)
            if (fabs(both[0][j] - both[1][j]) > came)
                came = fabs(both[0][j] - both[1][j]);

        okOrFail(whole > 0 && came > whole * 0.01,
                 shipped[i] + " puts the channel on `side' into what it "
                 "hands back",
                 "the side is worth " + num(came) + " against a mix of " +
                 num(whole));
    }

    remove(instFile.c_str());
    remove(fxFile.c_str());
    remove(fxFile2.c_str());

    printf("\n%d failure(s)\n", failed);

    return failed;
}
