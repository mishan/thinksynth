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
 * ratecheck -- the synth follows the device's rate.
 *
 * A device may open at a rate other than the one asked for, and
 * gthFollowDeviceRate is what makes the synth follow it. This drives it
 * with a device that always runs at 48 kHz, whatever it is asked for, and
 * checks that a synth made at 44.1 kHz is replaced by one at 48 kHz, that
 * the device was reopened on the new source and is running, and that a
 * `10 ms' in a graph loaded afterwards is 480 frames and not 441. And that
 * a synth already at the device's rate is left alone.

 *
 *   scripts/ratecheck -p build/plugins/
 *
 * Exit status is the number of failures.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "think.h"

#include "gthAudio.h"
#include "gthAudioRate.h"
#include "gthSynthSource.h"

static int failed = 0;

static void check (bool ok, const char *what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);

    if (!ok)
        failed++;
}

/* A device with a mind of its own: it runs at `rate' whatever it is asked
   for, as a JACK server or a 48 kHz-only card does. */
class FixedRateAudio : public gthAudio {
public:
    explicit FixedRateAudio (int rate) : rate_(rate), running_(false),
                                         source_(NULL), opens_(0)
    {
        fmt_.rate = 0;
        fmt_.channels = 0;
        fmt_.frames = 0;
    }

    bool open (const gthAudioFmt &want, gthAudioSource *source)
    {
        fmt_ = want;
        fmt_.rate = rate_;
        fmt_.frames = 256;
        source_ = source;
        opens_++;

        source->prepare(fmt_.frames, (unsigned)fmt_.channels);

        return true;
    }

    bool start (void) { running_ = true; return true; }
    void stop (void) { running_ = false; }
    bool running (void) const { return running_; }
    const gthAudioFmt &format (void) const { return fmt_; }
    std::string deviceName (void) const { return "fixed"; }
    std::vector<gthAudioDevice> devices (void) const
    {
        return std::vector<gthAudioDevice>();
    }

    int rate_;
    bool running_;
    gthAudioSource *source_;
    int opens_;
    gthAudioFmt fmt_;
};

static const char graph[] =
    "name \"ratecheck\";\n"
    "node ionode {\n"
    "    channels = 2;\n"
    "    out0 = osc->out;\n"
    "    out1 = osc->out;\n"
    "    play = 1;\n"
    "};\n"
    "node osc osc::simple {\n"
    "    freq = 440;\n"
    "    waveform = 0;\n"
    "};\n"
    "node env env::ad {\n"
    "    a = 10 ms;\n"
    "    d = 10 ms;\n"
    "    trigger = ionode->trigger;\n"
    "};\n"
    "io ionode;\n";

int main (int argc, char **argv)
{
    string pluginPath;

    if (argc == 3 && !strcmp(argv[1], "-p"))
        pluginPath = argv[2];

    /* 44.1 kHz asked for, 48 kHz given. */
    {
        thSynth *synth = new thSynth(pluginPath, 256, 44100);
        gthSynthSource *source = new gthSynthSource(synth);
        FixedRateAudio audio(48000);
        gthAudioFmt want = { 44100, synth->audioChannelCount(), 0 };

        audio.open(want, source);
        audio.start();

        const bool followed = gthFollowDeviceRate(&audio, synth, source,
                                                  pluginPath);

        check(followed, "a device at another rate is followed");
        check(synth->getSampleRate() == 48000,
              "the new synth runs at the device's 48000");
        check(synth->getWindowlen() == 256, "and at the old window length");
        check(audio.opens_ == 2 && audio.source_ == source,
              "the device was reopened on the new source");
        check(audio.running(), "and is running again");

        thSynthTree *tree = synth->loadTreeText("ratecheck", graph, 0, 100);
        thArg *attack = tree ? tree->getArg("env", "a") : NULL;

        check(attack != NULL && (*attack)[0] == 480.0f,
              "`10 ms' in a graph loaded afterwards is 480 frames");

        float out[512];

        synth->addNote(0, 60, 100);
        source->render(out, 256, 2);

        audio.stop();
        delete source;
        delete synth;
    }

    /* Asked for what the device gives: nothing to do. */
    {
        thSynth *synth = new thSynth(pluginPath, 256, 48000);
        gthSynthSource *source = new gthSynthSource(synth);
        FixedRateAudio audio(48000);
        gthAudioFmt want = { 48000, synth->audioChannelCount(), 0 };

        audio.open(want, source);
        audio.start();

        const thSynth *before = synth;

        check(!gthFollowDeviceRate(&audio, synth, source, pluginPath) &&
              synth == before && audio.opens_ == 1 && audio.running(),
              "a synth already at the device's rate is left alone");

        audio.stop();
        delete source;
        delete synth;
    }

    printf("\n%d failure(s)\n", failed);

    return failed;
}
