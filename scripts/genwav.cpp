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
 * genwav -- render a .gen to a file, with no window and no sound card.
 *
 *   scripts/genwav -p plugins/ -s 180 -o ebb.wav gen/ebb.gen
 *   scripts/genwav -p plugins/ -t - gen/round.gen | head
 *
 * gencheck drives the scheduler through its virtual clock and keeps the
 * delivered events as a tape, which is how it proves a piece replays. This
 * does the same thing and keeps the audio as well: after every step of the
 * clock the synth renders one window, and the windows are written out as
 * 16-bit PCM. Nothing here is real time, so a three-minute piece takes a
 * few seconds, and a piece that would clip does so in a file rather than
 * in your ears.
 *
 * What it is for: hearing a piece without opening the Composer window,
 * checking a new instrument block's level against the others before
 * pressing Play, and reading the tape -- `-t' -- to see what a chain
 * actually delivered when the piano roll is not to hand. The summary
 * printed at the end (peak, RMS, clipped samples, notes delivered) is the
 * number a level question wants.
 *
 * The clock steps one audio window at a time -- 1024 samples, about
 * twenty-three milliseconds at the default rate -- so an event lands on the
 * window boundary after its scheduled time. The Composer window delivers on
 * a twenty-millisecond timer and has the same granularity; the file sounds
 * the way the window does, and the tape's times are the scheduled ones.
 *
 * Instruments load the way they do everywhere else: the `dsp' name is
 * searched for under THINK_DSP_PATH, the current directory's dsp/, and the
 * install prefix, in that order. Run it from the source tree and the tree's
 * dsp/ is what it finds. A piece whose sinks name channels rather than
 * instruments renders those channels silent, since nothing is loaded on
 * them -- that is what such a piece does before somebody aims the channel,
 * and this tool has no somebody.
 *
 * After the transport stops the render continues until the tails have rung
 * out, up to a few seconds, so a long release is in the file rather than
 * cut off at the length you asked for.
 *
 * Exit status is 0; 3 if any sample reached full scale, which is the thing
 * this tool exists to catch; and 4, which wins, if the engine's per-voice
 * guard dropped a voice for going non-finite. Both are in the summary too.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <glibmm.h>

#include "think.h"

#include "libthink/thDynLib.h"
#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"

/* Enough silence to call a tail finished, and the longest we will wait
   for one. amb01's release tops out at five seconds; anything longer than
   TAIL_MAX is a drone, not a tail. */
#define TAIL_SILENT   1e-4f
#define TAIL_MAX      8.0

static void usage (const char *argv0)
{
    printf("usage: %s [-p PATH] [-s SECONDS] [-o FILE.wav] [-t FILE] file.gen\n"
           "\n"
           "  -p, --plugin-path PATH  where to find plugin .so files\n"
           "  -s, --seconds N         how long to run the transport (default 120)\n"
           "  -o, --output FILE       write the audio here, 16-bit PCM WAV\n"
           "  -t, --tape FILE         write the delivered events here (- for stdout)\n"
           "  -q, --quiet             no summary\n",
           argv0);
}

static void loadComposers (const std::string &pluginDir,
                           std::map<std::string, thcPlugin *> &out)
{
    std::filesystem::path root =
        std::filesystem::path(pluginDir) / "composer";
    std::error_code ec;

    if (!std::filesystem::is_directory(root, ec))
        return;

    for (const auto &f : std::filesystem::directory_iterator(root, ec))
    {
        if (ec)
            break;

        if (f.path().extension() != PLUGIN_SUFFIX)
            continue;

        thcPlugin *p = new thcPlugin(f.path().string());

        if (p->state() != thcPlugin::LOADED)
        {
            delete p;
            continue;
        }

        out[p->name()] = p;

        /* Pin the module's mapping, and its dependency closure, for the life
           of the process: a second dlopen bumps the loader's reference count
           and this handle is never closed. Without it the dlclose in
           ~thcPlugin below can drop the last reference to the glib stack --
           which some cairo builds pull in -- and unmap the once-per-process
           init heap glib documents as never freed, turning it into
           LeakSanitizer reports naming "<unknown module>". gencheck makes the
           same arrangement and says more about why. */
        thDynLib::open(f.path().string());
    }
}

/* Frees the composer modules on every path out of main().
 *
 * They used to be left for exit() to deal with, on the grounds that this is a
 * tool rather than a test. It is a test now -- the ctest that renders
 * scripts/guard/nonfinite.gen through it -- and under the address sanitizer
 * that shortcut is twenty-five leak reports. */
struct HeldComposers {
    std::map<std::string, thcPlugin *> &plugins;

    ~HeldComposers (void)
    {
        for (std::map<std::string, thcPlugin *>::iterator i = plugins.begin();
             i != plugins.end(); ++i)
        {
            delete i->second;
        }
    }
};

/* One line per delivered event, in gencheck's spelling minus the
   seventeen digits: N time channel note velocity duration, C for a
   chanarg, P for a swap, E for a node-arg edit. Channels are the
   engine's, counted from zero. */
static void writeEvent (FILE *tape, const thcEvent &ev)
{
    switch (ev.type)
    {
        case THC_EV_NOTE:
            fprintf(tape, "N %.3f %d %d %d %.3f\n", ev.at, ev.channel,
                    ev.u.note.note, ev.u.note.velocity,
                    ev.u.note.duration);
            break;
        case THC_EV_CHANARG:
            fprintf(tape, "C %.3f %d %s %.4f\n", ev.at, ev.channel,
                    ev.u.chanarg.name ? ev.u.chanarg.name : "",
                    (double)ev.u.chanarg.value);
            break;
        case THC_EV_PATCH:
            fprintf(tape, "P %.3f %d %s\n", ev.at, ev.channel,
                    ev.u.patch.name ? ev.u.patch.name : "");
            break;
        case THC_EV_NODEARG:
            fprintf(tape, "E %.3f %d %s.%s %.4f\n", ev.at, ev.channel,
                    ev.u.nodearg.node ? ev.u.nodearg.node : "",
                    ev.u.nodearg.arg ? ev.u.nodearg.arg : "",
                    (double)ev.u.nodearg.value);
            break;
        default:
            fprintf(tape, "? %.3f %d %d\n", ev.at, ev.channel,
                    (int)ev.type);
            break;
    }
}

static bool writeWav (const std::string &path, const std::vector<float> &pcm,
                      int channels, int rate)
{
    FILE *w = fopen(path.c_str(), "wb");

    if (w == NULL)
        return false;

    const uint32_t dataBytes = (uint32_t)pcm.size() * 2;
    const uint16_t fmt = 1, ch = (uint16_t)channels, bits = 16;
    const uint16_t align = ch * 2;
    const uint32_t byteRate = (uint32_t)rate * align;
    const uint32_t fmtLen = 16, riffLen = 36 + dataBytes;
    const uint32_t rate32 = (uint32_t)rate;

    fwrite("RIFF", 1, 4, w);  fwrite(&riffLen, 4, 1, w);
    fwrite("WAVE", 1, 4, w);
    fwrite("fmt ", 1, 4, w);  fwrite(&fmtLen, 4, 1, w);
    fwrite(&fmt, 2, 1, w);    fwrite(&ch, 2, 1, w);
    fwrite(&rate32, 4, 1, w); fwrite(&byteRate, 4, 1, w);
    fwrite(&align, 2, 1, w);  fwrite(&bits, 2, 1, w);
    fwrite("data", 1, 4, w);  fwrite(&dataBytes, 4, 1, w);

    /* Clamped here as well as in the engine, because the engine's clamp is
       what thClampSample promises the output stage and this is a second
       output stage. */
    for (size_t i = 0; i < pcm.size(); i++)
    {
        float v = pcm[i];

        if (v > 1)  v = 1;
        if (v < -1) v = -1;

        const int16_t s = (int16_t)lrintf(v * 32767);

        fwrite(&s, 2, 1, w);
    }

    return fclose(w) == 0;
}

int main (int argc, char **argv)
{
    Glib::init();

    std::string pluginPath = PLUGIN_PATH;
    std::string genFile, wavFile, tapeFile;
    double seconds = 120;
    bool quiet = false;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--plugin-path"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            pluginPath = argv[i];
        }
        else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--seconds"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            seconds = atof(argv[i]);
        }
        else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            wavFile = argv[i];
        }
        else if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--tape"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            tapeFile = argv[i];
        }
        else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet"))
            quiet = true;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
        {
            usage(argv[0]);
            return 0;
        }
        else if (genFile.empty())
            genFile = argv[i];
        else
        {
            usage(argv[0]);
            return 2;
        }
    }

    if (genFile.empty() || seconds <= 0)
    {
        usage(argv[0]);
        return 2;
    }

    if (wavFile.empty() && tapeFile.empty() && quiet)
    {
        fprintf(stderr, "%s: nothing to write and nothing to say\n", argv[0]);
        return 2;
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    std::map<std::string, thcPlugin *> plugins;

    loadComposers(pluginPath, plugins);

    const HeldComposers held = { plugins };

    if (plugins.empty())
    {
        fprintf(stderr, "%s: no composer modules under %s -- build the "
                "plugins first, or pass -p\n", argv[0], pluginPath.c_str());
        return 2;
    }

    /* Built with the plugin path, because a piece's instruments are .dsp
       graphs and their nodes have to come from somewhere. */
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    thcScheduler sched(&synth);
    thcGenLoader loader(plugins);

    if (!loader.load(genFile, &sched))
    {
        for (size_t k = 0; k < loader.errors().size(); k++)
            fprintf(stderr, "%s\n", loader.errors()[k].c_str());

        return 1;
    }

    FILE *tape = NULL;

    if (tapeFile == "-")
        tape = stdout;
    else if (!tapeFile.empty())
    {
        tape = fopen(tapeFile.c_str(), "w");

        if (tape == NULL)
        {
            fprintf(stderr, "%s: cannot write %s\n", argv[0],
                    tapeFile.c_str());
            return 1;
        }
    }

    size_t notes = 0;

    sigc::connection conn = sched.sigDelivered.connect(
        [&](const thcEvent &ev)
        {
            if (ev.type == THC_EV_NOTE)
                notes++;

            if (tape != NULL)
                writeEvent(tape, ev);
        });

    const int channels = synth.audioChannelCount();
    const int window = synth.getWindowlen();
    const double dt = (double)window / TH_DEFAULT_SAMPLES;
    const size_t frame = (size_t)channels * window;

    std::vector<float> pcm;

    pcm.reserve((size_t)((seconds + TAIL_MAX) / dt + 1) * frame);

    /* One window of audio per step of the clock. The scheduler enqueues
       what it delivers and process() applies it, exactly as the GUI's
       timer and the audio thread do between them. */
    auto renderWindow = [&]() -> float
    {
        synth.process();

        const float *buf = synth.getOutput();
        float peak = 0;

        for (size_t i = 0; i < frame; i++)
        {
            const float a = fabsf(buf[i]);

            if (a > peak)
                peak = a;
        }

        /* getOutput() is planar -- a window of channel 0, then a window of
           channel 1 -- and a WAV is interleaved. Copied as it stood, every
           window came out as channel 0 at double speed across both
           speakers and then channel 1 the same way: an octave up, chopped
           forty-three times a second. The tape was never affected, which
           is why gencheck and compare.mjs had nothing to say about it. */
        for (int i = 0; i < window; i++)
            for (int c = 0; c < channels; c++)
                pcm.push_back(buf[(size_t)c * window + i]);

        return peak;
    };

    sched.start();

    /* Until the time asked for, or until the piece is over: a piece
       whose arrangement closes with `section end;' stops its own
       transport, and a loop that only watched the clock would step a
       stopped scheduler for the rest of the render. */
    while (sched.now() < seconds && sched.running())
    {
        sched.stepTransport(dt);
        renderWindow();
    }

    /* stop() flushes the note-offs for whatever is still sounding; the
       releases that follow are part of the piece. */
    sched.stop();
    conn.disconnect();

    for (double tail = 0; tail < TAIL_MAX; tail += dt)
        if (renderWindow() < TAIL_SILENT)
            break;

    if (tape != NULL && tape != stdout)
        fclose(tape);

    float peak = 0;
    double sumsq = 0;
    size_t clipped = 0;

    for (size_t i = 0; i < pcm.size(); i++)
    {
        const float a = fabsf(pcm[i]);

        if (a > peak)
            peak = a;

        if (a >= 0.999f)
            clipped++;

        sumsq += (double)pcm[i] * pcm[i];
    }

    if (!wavFile.empty() &&
        !writeWav(wavFile, pcm, channels, TH_DEFAULT_SAMPLES))
    {
        fprintf(stderr, "%s: cannot write %s\n", argv[0], wavFile.c_str());
        return 1;
    }

    /* Voices thMidiChan::mixNote dropped for going non-finite. Unreported,
       the render is just quieter than it should be -- or silent, if the piece
       is one diverging instrument -- and the peak and RMS below are an honest
       report of the wrong signal. */
    const unsigned long badVoices = synth.nonFiniteVoices();

    if (!quiet)
    {
        fprintf(stderr, "%s: %.1f s rendered, %zu notes, peak %.3f, "
                "RMS %.4f, %zu clipped sample%s\n",
                genFile.c_str(), pcm.size() / (double)frame * dt, notes,
                peak, pcm.empty() ? 0.0 : sqrt(sumsq / pcm.size()),
                clipped, clipped == 1 ? "" : "s");

        if (badVoices > 0)
            fprintf(stderr, "%s: non-finite voices: %lu\n", genFile.c_str(),
                    badVoices);
    }

    /* 4 before 3: a piece that clips is loud, a piece with a non-finite
       voice in it is not the piece. */
    if (badVoices > 0)
        return 4;

    return clipped > 0 ? 3 : 0;
}
