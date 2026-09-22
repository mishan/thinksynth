/*
 * dspcapture -- does a live input survive the device's block size?
 *
 * dspblock's mirror image. That one asks whether the stream a device
 * *receives* is the same however it is chopped up; this asks the same of the
 * stream a graph *hears*, which is the other half of gthSynthSource's job now
 * that a channel effect can declare live<N> (think.h, LIVEPREFIX).
 *
 * The property is not the same property, and the difference is the reason this
 * is a second harness rather than three more block sizes in the first one:
 *
 *   the samples are the same.        Whatever the period, the graph is handed
 *      the frames the host captured, in order, with none inserted and none
 *      lost. That is what the accumulator is for and it is checked here.
 *
 *   the alignment is not.            Which window boundary the accumulation
 *      completes on is a function of the period, so the same capture lands
 *      against different windows of the same piece at two block sizes and the
 *      render is *not* bit-identical. dspblock's invariant is therefore false
 *      for a graph with a live input, and a graph with one is not in its list.
 *
 * What else is checked, each of which was a decision rather than a discovery:
 *
 *   silence is the default.          A host that never captures leaves
 *      thSynth's buffer at the zeros it was allocated with, so genwav,
 *      gencheck and dspcheck render a graph with a live input reproducibly --
 *      and a graph *without* one renders bit for bit the same whether or not
 *      the host is capturing.
 *
 *   a gap is silence.                A host that was capturing and stops has
 *      to hear silence, not the last window over and over, which is the
 *      failure thChanEffect describes for a side channel that goes away.
 *
 *   the delay is one window, or two.  One because produce() hands out the
 *      window it has and renders the next; a second where the device period is
 *      smaller than the window, because then the first ask for a window of
 *      capture comes with only a period in hand. Both numbers are pinned here,
 *      because between them they are the whole argument for running a window
 *      the size of the device period -- which in a browser is what asking for
 *      128 rather than 256 buys.
 *
 * Its .dsp files are written here rather than shipped, for fxcheck's reason:
 * what is being tested is the engine's handling of a declaration, so the
 * declarations belong next to the assertions about them.
 *
 * Exit status is the number of failures.
 *
 * Copyright (C) 2004-2026 Metaphonic Labs. GPL 2 or later.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "think.h"

#include "gthSynthSource.h"

using std::string;
using std::vector;

static const unsigned kChannels = 2;
static const int      kWindow   = 1024;
static const int      kRate     = 44100;

/* Enough to cross several window boundaries at every block size. */
static const unsigned kFrames = 1024 * 16;

static int failed = 0;

static void ok (const string &what)
{
    printf("ok    %s\n", what.c_str());
}

static void fail (const string &what, const string &detail)
{
    printf("FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : " -- ",
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

    snprintf(buf, sizeof(buf), "%g", v);

    return buf;
}

/* Every other harness here spells this the same way, and it has to be spelled
 * that way: TMPDIR and a /tmp fallback is a Unix habit, and on the MinGW runner
 * there is no /tmp -- the whole harness got as far as "could not write its
 * scratch files" and stopped. temp_directory_path() is the portable answer and
 * was already in the tree four times over.
 *
 * A function rather than a static, for argtype's reason: temp_directory_path()
 * consults the environment, and doing that before main() is a habit worth not
 * forming. */
static string scratchPath (const char *leaf)
{
    std::error_code ec;

    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);

    if (ec)
        dir = ".";      /* the build tree; ctest runs us in it */

    return (dir / leaf).string();
}

static bool writeFile (const string &path, const string &text)
{
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);

    if (!out)
        return false;

    out << text;

    return out.good();
}

/* An instrument that makes no sound at all: the channel a live effect goes on
   needs something loaded, and anything it produced itself would be added to
   what is being measured. */
static string silentInstrument (void)
{
    return
        "name \"dspcapture-inst\";\n\n"
        "node ionode {\n"
        "    channels = 2;\n"
        "    out0 = 0;\n"
        "    out1 = 0;\n"
        "};\n\n"
        "io ionode;\n";
}

/* An instrument that does, for the graph-without-a-live-input case. */
static string toneInstrument (void)
{
    return
        "name \"dspcapture-tone\";\n\n"
        "node ionode {\n"
        "    channels = 2;\n"
        "    out0 = vca->out;\n"
        "    out1 = vca->out;\n"
        "    play = env->play;\n"
        "};\n\n"
        "node osc osc::simple {\n"
        "    freq = 220;\n"
        "    waveform = 0;\n"
        "    amp = 0.4;\n"
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

/* The effect that hands the capture straight back: whatever comes out of the
   channel is what the graph was fed, which is what makes the stream
   recoverable from the render. */
static string echoEffect (void)
{
    return
        "name \"dspcapture-fx\";\n\n"
        "node ionode {\n"
        "    channels = 2;\n"
        "    in0 = 0;\n"
        "    in1 = 0;\n"
        "    live0 = 0;\n"
        "    out0 = ionode->live0;\n"
        "    out1 = ionode->live0;\n"
        "};\n\n"
        "io ionode;\n";
}

/* And one that asks for nothing: the whole corpus's shape. */
static string plainEffect (void)
{
    return
        "name \"dspcapture-dry\";\n\n"
        "node ionode {\n"
        "    channels = 2;\n"
        "    in0 = 0;\n"
        "    in1 = 0;\n"
        "    out0 = ionode->in0;\n"
        "    out1 = ionode->in1;\n"
        "};\n\n"
        "io ionode;\n";
}

/* ---- the capture the host feeds ---------------------------------------- */

/* A sequence with no two frames alike and no period, so a run that reordered
 * or repeated a window could not come out looking right. A small fixed LCG
 * rather than rand(): what is compared is two runs of this program against
 * each other, and a sequence that depends on the C library's generator is one
 * more thing that could differ between them.
 *
 * Held well inside the rails so nothing downstream is limited: what the master
 * limiter does to a signal is not what is being measured, and it does nothing
 * at all below its knee.
 */
static void makeCapture (vector<float> &out, unsigned frames)
{
    unsigned long state = 12345;

    out.assign(frames, 0.0f);

    for (unsigned i = 0; i < frames; i++)
    {
        state = state * 1103515245UL + 12345UL;

        out[i] = (float)(((state >> 16) & 0xffff) / 65535.0 * 0.4 - 0.2);
    }
}

/* ---- a run ------------------------------------------------------------- */

struct Run
{
    vector<float> out;        /* channel 0's left side, frame by frame */
    unsigned long dropped;
    unsigned long starved;

    Run (void) : dropped(0), starved(0) { }
};

/* Render `kFrames' frames in blocks of `block', feeding `capture' one block at
 * a time -- which is what a duplex callback does: the period it was handed and
 * the period it must fill, in one call.
 *
 * `feedFor' is how many frames of capture to feed before going quiet, so that
 * a gap can be asked for. `capture' empty is a host that captures nothing.
 */
static bool render (const string &pluginPath, const string &inst,
                    const string &fx, const vector<float> &capture,
                    unsigned block, unsigned feedFor, Run &run)
{
    thSynth synth(pluginPath, kWindow, kRate);

    if (synth.loadTree(inst.c_str(), 0, TH_MAX) == NULL)
        return false;

    if (!fx.empty() && synth.loadEffect(fx, 0, -1) == NULL)
        return false;

    /* dspblock's reason: two runs of one patch are only comparable if the
       noise plugins start where they started last time. */
    srand(1);

    gthSynthSource source(&synth);

    source.prepare(block, kChannels);

    synth.addNote(0, 60, 100);

    run.out.assign(kFrames, 0.0f);

    vector<float> chunk((size_t)block * kChannels, 0.0f);
    vector<float> feed(block, 0.0f);

    unsigned done = 0;

    while (done < kFrames)
    {
        unsigned want = block;

        if (want > kFrames - done)
            want = kFrames - done;

        /* The capture for exactly this period, mono, the way a device hands
           one over -- and before render(), the way a duplex callback has it. */
        if (!capture.empty() && done < feedFor)
        {
            unsigned n = want;

            if (done + n > feedFor)
                n = feedFor - done;

            for (unsigned i = 0; i < n; i++)
                feed[i] = (done + i < capture.size())
                              ? capture[done + i] : 0.0f;

            source.feedInput(&feed[0], n, 1);
        }

        source.render(&chunk[0], want, kChannels);

        for (unsigned i = 0; i < want; i++)
            run.out[done + i] = chunk[(size_t)i * kChannels];

        done += want;
    }

    run.dropped = source.inputDropped();
    run.starved = source.inputStarved();

    return true;
}

/* Leading exact zeros. What the graph was fed is behind them, and how many
   there are is the alignment -- which is the thing that legitimately differs
   between block sizes. */
static size_t leadingZeros (const vector<float> &v)
{
    size_t i = 0;

    while (i < v.size() && v[i] == 0.0f)
        i++;

    return i;
}

static double peak (const vector<float> &v, size_t from, size_t to)
{
    double top = 0;

    for (size_t i = from; i < to && i < v.size(); i++)
        if (fabs(v[i]) > top)
            top = fabs(v[i]);

    return top;
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;

    for (int i = 1; i < argc; i++)
    {
        const string a = argv[i];

        if ((a == "-p" || a == "--plugin-path") && i + 1 < argc)
            pluginPath = argv[++i];
        else if (a == "-h" || a == "--help")
        {
            printf("usage: %s [-p PATH]\n", argv[0]);
            return 0;
        }
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    const string instFile  = scratchPath("dspcapture-inst.dsp");
    const string toneFile  = scratchPath("dspcapture-tone.dsp");
    const string echoFile  = scratchPath("dspcapture-fx.dsp");
    const string plainFile = scratchPath("dspcapture-dry.dsp");

    if (!writeFile(instFile, silentInstrument()) ||
        !writeFile(toneFile, toneInstrument()) ||
        !writeFile(echoFile, echoEffect()) ||
        !writeFile(plainFile, plainEffect()))
    {
        fprintf(stderr, "dspcapture: could not write its scratch files\n");
        return 1;
    }

    vector<float> capture;

    makeCapture(capture, kFrames);

    /* 1024 is the window and the reference. 512 and 256 are the ones a
       CoreAudio or WASAPI device picks; 2048 is a period *larger* than a
       window, which is the case the accumulator is sized for; 333 is prime and
       divides nothing. */
    const unsigned blocks[] = { 1024, 512, 256, 2048, 333, 64 };
    const unsigned nblocks  = sizeof(blocks) / sizeof(blocks[0]);

    /* ---- the samples are the same, whatever the period ------------------ */

    {
        Run ref;

        if (!render(pluginPath, instFile, echoFile, capture, kWindow,
                    kFrames, ref))
            fail("the reference run loads", "");
        else
        {
            const size_t refLead = leadingZeros(ref.out);
            bool same = true;
            string why;

            for (unsigned b = 0; b < nblocks && same; b++)
            {
                Run got;

                if (!render(pluginPath, instFile, echoFile, capture,
                            blocks[b], kFrames, got))
                {
                    same = false;
                    why = "block " + num(blocks[b]) + " did not load";
                    break;
                }

                const size_t lead = leadingZeros(got.out);

                /* The overlap, which is everything past the later of the two
                   alignments. Compared bit for bit: the scaling either run
                   went through is the same scaling, so anything but equality
                   is a sample that moved. */
                const size_t n = (ref.out.size() - refLead <
                                  got.out.size() - lead)
                                     ? ref.out.size() - refLead
                                     : got.out.size() - lead;

                for (size_t i = 0; i < n; i++)
                {
                    if (memcmp(&ref.out[refLead + i], &got.out[lead + i],
                               sizeof(float)) != 0)
                    {
                        same = false;
                        why = "block " + num(blocks[b]) +
                              " differs from the reference at capture frame " +
                              num((double)i);
                        break;
                    }
                }

                if (same && got.dropped != 0)
                {
                    same = false;
                    why = "block " + num(blocks[b]) + " dropped " +
                          num((double)got.dropped) + " captured frames";
                }

                if (same && got.starved != 0)
                {
                    same = false;
                    why = "block " + num(blocks[b]) + " starved " +
                          num((double)got.starved) + " windows";
                }
            }

            okOrFail(same && refLead < ref.out.size(),
                     "a live graph hears the frames the host captured, in "
                     "order, at every device block size",
                     why);
        }
    }

    /* ---- and the delay is one window, or two -------------------------- */

    /* With the period equal to the window there is nothing to accumulate: a
     * single feedInput completes the window before the render that wants it, so
     * the only thing in the way is produce() being one window ahead.
     */
    {
        Run run;

        if (!render(pluginPath, instFile, echoFile, capture, kWindow,
                    kFrames, run))
            fail("the one-window run loads", "");
        else
        {
            const size_t lead = leadingZeros(run.out);

            okOrFail(lead == (size_t)kWindow,
                     "with the device period equal to the window, a live graph "
                     "hears one window late",
                     "the capture arrived " + num((double)lead) +
                     " frames in, not " + num(kWindow));
        }
    }

    /* And with a period smaller than the window it is two, steadily. The first
     * ask comes with a period in hand rather than a window, so that window is
     * rendered with silence and the capture lands in the one after it -- and
     * because the ask and the arrival keep the same relationship from then on,
     * the second window is not a startup cost that gets absorbed.
     *
     * Pinned rather than merely noted: it is what makes a window the size of
     * the device period worth asking for, and a change that quietly turned two
     * into three would be a change nobody could hear the difference of and
     * everybody would feel.
     */
    {
        Run run;

        if (!render(pluginPath, instFile, echoFile, capture, kWindow / 4,
                    kFrames, run))
            fail("the two-window run loads", "");
        else
        {
            const size_t lead = leadingZeros(run.out);

            okOrFail(lead == (size_t)kWindow * 2,
                     "and with a period smaller than the window, two -- which "
                     "is the whole argument for matching them",
                     "the capture arrived " + num((double)lead) +
                     " frames in, not " + num(kWindow * 2));
        }
    }

    /* ---- a host that captures nothing changes nothing ------------------- */

    /* The property everything else in the tree rests on. A graph that asks for
     * no live input has to render bit for bit the same on a machine with a
     * microphone open as on one without, and an offline path that feeds
     * nothing has to render what it rendered before live<N> existed.
     */
    {
        Run dry, wet;

        const bool loaded =
            render(pluginPath, toneFile, plainFile, vector<float>(), 512,
                   0, dry) &&
            render(pluginPath, toneFile, plainFile, capture, 512, kFrames,
                   wet);

        if (!loaded)
            fail("the dry runs load", "");
        else
        {
            bool same = dry.out.size() == wet.out.size();
            size_t at = 0;

            for (size_t i = 0; i < dry.out.size() && same; i++)
                if (memcmp(&dry.out[i], &wet.out[i], sizeof(float)) != 0)
                {
                    same = false;
                    at = i;
                }

            okOrFail(same && peak(dry.out, 0, dry.out.size()) > 0,
                     "a graph that asks for no live input renders bit for bit "
                     "the same while the host is capturing",
                     same ? "it rendered silence"
                          : "they differ at frame " + num((double)at));
        }
    }

    /* ---- and a gap is silence, not the last window again ---------------- */

    /* A microphone revoked, a stream closed, a host that simply stops. What
     * the graph must hear is nothing; what it must not hear is the last window
     * it was given, for ever, which is what a buffer nobody writes any more
     * would give it.
     *
     * Fed for the first quarter of the run, then quiet. Past a window of slack
     * for the one in flight, everything after it has to be exactly zero.
     */
    {
        const unsigned feedFor = kFrames / 4;
        Run run;

        if (!render(pluginPath, instFile, echoFile, capture, 512, feedFor,
                    run))
            fail("the gap run loads", "");
        else
        {
            const double before = peak(run.out, 0, feedFor);
            const double after  = peak(run.out, feedFor + 2 * kWindow,
                                       run.out.size());

            okOrFail(before > 0 && after == 0,
                     "a host that stops capturing leaves the graph hearing "
                     "silence rather than the last window again",
                     "while capturing " + num(before) + ", after " +
                     num(after));
        }
    }

    printf("\n%d failure(s)\n", failed);

    return failed;
}
