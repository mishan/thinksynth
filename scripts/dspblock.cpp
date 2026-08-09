/*
 * dspblock -- does the audio path survive a device block size that is not
 * the synth's window length?
 *
 * This is the regression test for the bug PORTING.md section 1 describes.
 * The old JACK callback did:
 *
 *     int copy = ((int)nframes < l) ? (int)nframes : l;
 *     for (int k = 0; k < copy; k++) buf[k] = thClampSample(synthbuffer[k]);
 *     if ((int)nframes > copy) memset(buf + copy, 0, ...);
 *     process_synth();
 *
 * so with a 512-frame period and a 1024-frame window it discarded half of
 * every window, and with a 2048-frame period it emitted 1024 frames of audio
 * followed by 1024 of silence. Only nframes == windowlen was correct, and
 * nothing enforced that. JACK's default period happens to be 1024, which is
 * why it was never noticed; CoreAudio and WASAPI pick their own.
 *
 * The property that has to hold: the stream of samples a device receives is
 * the same regardless of how it is chopped up. So render the same patch
 * through gthSynthSource at a range of block sizes -- including sizes that
 * are not divisors or multiples of the window, and one that is prime -- and
 * compare every stream bit-for-bit against the 1024-frame reference.
 *
 * Exit status is the number of block sizes that disagreed.
 *
 * Copyright (C) 2004-2014 Metaphonic Labs. GPL 2 or later.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>
#include <string>

#include "think.h"

#include "gthSynthSource.h"

static const unsigned kChannels = 2;

/* Enough to cross several window boundaries at every block size. */
static const unsigned kFrames = 1024 * 24;

static void usage (const char *argv0)
{
    printf("usage: %s [-p PATH] [-q] file.dsp ...\n"
           "  -p PATH   plugin root\n"
           "  -q        only report failures\n", argv0);
}

/* Render kFrames frames of one note through a freshly built synth, in
   blocks of `block' frames. */
static bool renderAt (const string &pluginPath, const string &dsp,
                      unsigned block, vector<float> &out)
{
    thSynth synth(pluginPath, 1024, 44100);

    if (synth.loadTree(dsp.c_str(), 0, TH_MAX) == NULL)
        return false;

    /* osc::static and friends call rand(); reseed so two runs of the same
       patch are comparable, exactly as dspcheck does. */
    srand(1);

    gthSynthSource source(&synth);

    source.prepare(block, kChannels);

    synth.addNote(0, 60, 100);

    out.assign((size_t)kFrames * kChannels, 0.0f);

    vector<float> chunk((size_t)block * kChannels, 0.0f);

    unsigned done = 0;

    while (done < kFrames)
    {
        unsigned want = block;

        if (want > kFrames - done)
            want = kFrames - done;

        source.render(&chunk[0], want, kChannels);

        memcpy(&out[(size_t)done * kChannels], &chunk[0],
               (size_t)want * kChannels * sizeof(float));

        done += want;
    }

    return true;
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;
    bool quiet = false;
    int firstFile = -1;

    for (int i = 1; i < argc; i++)
    {
        const string a = argv[i];

        if (a == "-p" || a == "--plugin-path")
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            pluginPath = argv[i];
        }
        else if (a == "-q")
            quiet = true;
        else if (a == "-h" || a == "--help")
        {
            usage(argv[0]);
            return 0;
        }
        else { firstFile = i; break; }
    }

    if (firstFile < 0) { usage(argv[0]); return 2; }

    /* 1024 is the reference: it is the window length, the one case the old
       code got right. 512 and 256 are the CoreAudio-shaped ones that used to
       drop samples; 2048 is the case that used to insert silence; 333 is
       prime and divides nothing. 1 is the pathological limit. */
    const unsigned blocks[] = { 512, 256, 2048, 333, 64, 1, 4096 };
    const unsigned nblocks = sizeof(blocks) / sizeof(blocks[0]);

    int failures = 0;

    for (int f = firstFile; f < argc; f++)
    {
        const string dsp = argv[f];

        vector<float> reference;

        if (!renderAt(pluginPath, dsp, 1024, reference))
        {
            printf("FAIL  %s (did not load)\n", dsp.c_str());
            failures++;
            continue;
        }

        bool ok = true;

        for (unsigned b = 0; b < nblocks; b++)
        {
            vector<float> got;

            if (!renderAt(pluginPath, dsp, blocks[b], got))
            {
                printf("FAIL  %s (did not load at block %u)\n",
                       dsp.c_str(), blocks[b]);
                ok = false;
                continue;
            }

            if (got.size() != reference.size())
            {
                printf("FAIL  %s: block %u produced %zu samples, "
                       "expected %zu\n", dsp.c_str(), blocks[b],
                       got.size(), reference.size());
                ok = false;
                continue;
            }

            size_t at = reference.size();

            for (size_t i = 0; i < reference.size(); i++)
            {
                if (memcmp(&got[i], &reference[i], sizeof(float)) != 0)
                {
                    at = i;
                    break;
                }
            }

            if (at != reference.size())
            {
                printf("FAIL  %s: block %u differs from the 1024-frame "
                       "reference at frame %zu channel %zu (%g vs %g)\n",
                       dsp.c_str(), blocks[b], at / kChannels,
                       at % kChannels, (double)got[at], (double)reference[at]);
                ok = false;
            }
        }

        if (!ok)
            failures++;
        else if (!quiet)
            printf("ok    %s (%u block sizes agree)\n", dsp.c_str(), nblocks);
    }

    if (!quiet || failures)
        printf("\n%d file(s) failed\n", failures);

    return failures;
}
