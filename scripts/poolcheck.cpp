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
 * poolcheck -- a restarted voice sounds like a new one.
 *
 *   scripts/poolcheck -p plugins/ $(find dsp -name '*.dsp')
 *
 * A channel keeps the voices the audio thread has finished with and starts
 * them over for later notes (thMidiChan::recycle), resetting each arg in the
 * buffer it already has rather than copying the prototype again. The claim
 * is that nothing can tell: every plugin keeps its state in args, and a
 * restored arg is what the copy constructor would have made.
 *
 * So each file plays the same events twice, on fresh synths, once with the
 * pool and once with every note a fresh copy (thSynth::setVoicePool), and
 * the two renders must be bitwise equal. Two sequences:
 *
 * - scale: a note every three windows, up and down the keyboard in fifths,
 *   each held for six -- long enough for voices to finish and be started
 *   over.
 *
 * - crowd: a note every two windows held for thirty, so more are held than
 *   a default `poly' allows and voices are stolen and faded; pitches off the
 *   semitone; the sustain pedal down and up again, so released voices are
 *   held and then let go; and the channel's `amp' swapped for a three-value
 *   array and back, which re-points the prototype's chanargs.
 *
 * A file fails if it restarted no voice in either sequence, which would
 * show nothing.
 *
 * Then, once, the fallback: a voice whose args no longer match its
 * prototype's -- one invented on the voice by name -- is not restarted, and
 * the note it would have served gets a fresh copy without that arg.
 *
 * Exit status is the number of files that fail, plus one if the fallback
 * does.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "think.h"

#define WINDOWS 160

enum Sequence { SCALE, CROWD };

/* The k-th note of a sequence: up and down three octaves in fifths, and in
   the crowd a quarter tone off every fifth note. */
static float noteAt (Sequence seq, int k)
{
    const int span = 36 / 7 + 1;
    const int up = k % (2 * span);
    const float note = 36 + 7 * (up < span ? up : 2 * span - 1 - up);

    return (seq == CROWD && k % 5 == 0) ? note + 0.5f : note;
}

static void setAmp (thSynth &synth, const float *values, int len)
{
    synth.setChanArg(0, new thArg(string("amp"), values, len));
}

static void setPedal (thSynth &synth, float value)
{
    synth.setChanArg(0, new thArg(string("SusPedal"), value));
}

static bool render (const string &plugins, const char *file, Sequence seq,
                    bool pool, vector<float> &out, unsigned long *restarts)
{
    thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    thSynthTree *tree = synth.loadTree(file, 0, 100);
    const int step = (seq == SCALE) ? 3 : 2;
    const int hold = (seq == SCALE) ? 6 : 30;

    out.clear();
    *restarts = 0;

    if (tree == NULL || tree->takesInput() || synth.getChannel(0) == NULL)
        return false;

    synth.setVoicePool(pool);

    const thArg *amp = synth.getChanArg(0, "amp");
    const float level = amp ? (*amp)[0] : 1;
    const float swell[3] = { level, level * 0.5f, level * 0.75f };
    const int frame = synth.audioChannelCount() * synth.getWindowlen();

    for (int w = 0; w < WINDOWS; w++)
    {
        if (w % step == 0)
        {
            const int k = w / step;

            synth.addNote(0, noteAt(seq, k), 40 + 20 * (k % 4));
        }

        if (w >= hold && (w - hold) % step == 0)
            synth.delNote(0, noteAt(seq, (w - hold) / step));

        if (seq == CROWD)
        {
            if (w == 40)
                setPedal(synth, 127);
            else if (w == 80)
                setPedal(synth, 0);
            else if (w == 100)
                setAmp(synth, swell, 3);
            else if (w == 130)
                setAmp(synth, swell, 1);
        }

        synth.process();

        const float *buf = synth.getOutput();

        out.insert(out.end(), buf, buf + frame);
    }

    *restarts = synth.getChannel(0)->restarts();

    return true;
}

/* An instrument that loads: not an effect, which plays no notes. */
static bool playable (const string &plugins, const char *file)
{
    thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    thSynthTree *tree = synth.loadTree(file, 0, 100);

    return tree != NULL && !tree->takesInput() && synth.getChannel(0) != NULL;
}

/* Pooled and fresh for one sequence; true if they match. */
static bool compare (const string &plugins, const char *file, Sequence seq,
                     unsigned long *restarts, size_t *first)
{
    vector<float> pooled, fresh;
    unsigned long none;

    render(plugins, file, seq, true, pooled, restarts);
    render(plugins, file, seq, false, fresh, &none);

    if (pooled.size() == fresh.size() &&
        !memcmp(pooled.data(), fresh.data(), pooled.size() * sizeof(float)))
        return true;

    *first = 0;

    while (*first < pooled.size() && *first < fresh.size() &&
           !memcmp(&pooled[*first], &fresh[*first], sizeof(float)))
        (*first)++;

    return false;
}

/* See the top of the file. On the GUI thread's side only: the voice is
   built, changed and handed back without the audio thread ever seeing it,
   which is what collectRetired does with one it has finished. */
static bool fallback (const string &plugins, const char *file)
{
    thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    if (synth.loadTree(file, 0, 100) == NULL || synth.getChannel(0) == NULL)
    {
        printf("FAIL  fallback: %s did not load\n", file);
        return false;
    }

    thMidiChan *chan = synth.getChannel(0);
    thMidiNote *voice = chan->buildNote(60, 100);

    voice->setArg("poolcheck", 1);

    if (!chan->recycle(voice))
    {
        printf("FAIL  fallback: the channel would not take its own voice\n");
        delete voice;
        return false;
    }

    thMidiNote *next = chan->buildNote(64, 100);
    /* Not `next != voice': the copy may well be allocated where the voice
       it replaces was just freed. */
    const bool fresh = chan->restarts() == 0 && chan->pooled() == 0 &&
                       next->synthTree()->IONode()->getArg("poolcheck") == NULL;

    delete next;

    printf("%s  fallback: a voice with an arg its prototype lacks is %s\n",
           fresh ? "ok  " : "FAIL",
           fresh ? "replaced by a fresh copy" : "restarted");

    return fresh;
}

int main (int argc, char **argv)
{
    string plugins = "plugins/";
    const char *probe = NULL;
    int failed = 0, same = 0, skipped = 0;
    unsigned long total = 0;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
        {
            plugins = argv[++i];
            continue;
        }

        unsigned long restarts[2] = { 0, 0 };
        size_t first = 0;

        if (!playable(plugins, argv[i]))
        {
            printf("skip  %s\n", argv[i]);
            skipped++;
            continue;
        }

        if (probe == NULL)
            probe = argv[i];

        const Sequence seqs[2] = { SCALE, CROWD };
        const char *names[2] = { "scale", "crowd" };
        bool ok = true;

        for (int s = 0; s < 2 && ok; s++)
        {
            if (!compare(plugins, argv[i], seqs[s], &restarts[s], &first))
            {
                printf("DIFF  %-34s %s: %lu restarted, first differing "
                       "sample %zu\n", argv[i], names[s], restarts[s],
                       first);
                ok = false;
            }
        }

        if (ok && restarts[0] + restarts[1] == 0)
        {
            printf("FAIL  %-34s no voice was restarted, so nothing was "
                   "shown\n", argv[i]);
            ok = false;
        }

        if (!ok)
        {
            failed++;
            continue;
        }

        total += restarts[0] + restarts[1];
        printf("same  %-34s %lu + %lu restarted\n", argv[i], restarts[0],
               restarts[1]);
        same++;
    }

    printf("\n%d identical, %d failed, %d skipped; %lu voices restarted\n",
           same, failed, skipped, total);

    if (probe != NULL && !fallback(plugins, probe))
        failed++;

    return failed;
}
