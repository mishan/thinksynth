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

/* A granular player: a wav, or what a graph has just heard, as a cloud of
 * short windowed reads.
 *
 * `density' times a second a grain starts: `size' samples long, reading
 * the source from near `position' at `pitch' times its own speed, shaped
 * by a window so that it starts and ends at nothing. Enough of them
 * overlapping and there is no grain to hear, only the sound -- held for as
 * long as the voice lasts, whatever the file's own length, which is the
 * whole reason a texture that has to sustain for ten minutes is made this
 * way rather than looped.
 *
 *   position  where in the source a grain starts, 0 to 1: through the
 *             file from its beginning, or back into the live ring from
 *             the newest sample
 *   spread    how far from `position' either way a grain may start, as
 *             a fraction of the same span; 0 is every grain in one place
 *   size      each grain's length, in samples, so a .dsp writes `80 ms'
 *   density   grains a second
 *   pitch     each grain's speed through the source; 2 is an octave up
 *   jitter    semitones of random detune either way, drawn per grain
 *   window    0 a Hann window, 1 a trapezoid a quarter up and a quarter
 *             down, anything between a blend of the two
 *   freq and root, as osc::sample has them: a grain's speed is also
 *             freq / root, so a keyboard plays the cloud. `freq' left at 0
 *             is no keyboard at all, which is what an effect graph has.
 *
 * A GRAIN IS FIXED WHEN IT STARTS. Its place, its speed and its gain are
 * drawn and worked out at launch and never read again, so moving
 * `position' or `pitch' moves the grains that start after it and leaves
 * the ones sounding alone. That is why a position swept smoothly does not
 * click: nothing already heard jumps.
 *
 * THE LAUNCHES ARE EVEN -- `density' of them a second, one every
 * 1/density -- and what is random is where each one reads and how it is
 * detuned. A cloud whose launches were random as well would be lumpy in
 * level at low densities for no gain: the texture is in the reads.
 *
 * LEVEL. Grains reading uncorrelated places add in power, so each is
 * scaled by 1/sqrt(overlap * E[w^2]), for the number of grains a side
 * overlaps and the mean square of the window (3/8 for Hann, 2/3 for the
 * trapezoid). A cloud reading a file comes out at about the file's level
 * at any density and size; below one grain at a time nothing is boosted.
 * A cloud with no spread reads correlated copies and comes out a little
 * louder, which is what a graph's own amp is for.
 *
 * TWO SIDES. The grains alternate between `out' and `out2', so the pair
 * is a stereo image built from the cloud itself, and each side is scaled
 * for its own half of the density.
 *
 * DETERMINISTIC, from `seed', for misc::drift's reason and by the same
 * generator (plugins/dice.h): two voices started with the same seed make
 * the same cloud, bit for bit. A graph that wants each note its own
 * writes `seed = ionode->note'.
 *
 * `source = 1' READS A LIVE RING instead of a file: `in' is written into
 * eight seconds of it every sample, and `position' is how far back into
 * it a grain starts. `freeze' at 1 stops the writing, and the newest
 * sample stays where it was, so the grains go on reading what the ring
 * last held from where they were reading it; that is how a chord becomes
 * a texture. A grain that reads faster than the ring is written starts
 * far enough back not to overtake the write head within its length, and
 * one that reads slower starts near enough not to fall off the far end.
 *
 * The file is found and read the way osc::sample finds and reads one, and
 * through the same code (osc/sampleslot.h): once per synth, shared by
 * every voice, mono, at the synth's rate.
 *
 * At most GRAIN_MAX grains sound at once; a launch that finds none free is
 * skipped. That is sixty-four, which is a second of grains at 64 a second
 * or an eighth of a second at 500, and past it a cloud is a sound no
 * denser than it was.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "config.h"

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thSynthTree.h"
#include "thSynth.h"

#include "sampleslot.h"
#include "plugins/dice.h"

enum {IN_FILE, IN_POSITION, IN_SPREAD, IN_SIZE, IN_DENSITY, IN_PITCH,
      IN_JITTER, IN_WINDOW, IN_FREQ, IN_ROOT, IN_SEED, IN_SOURCE, IN_ARG,
      IN_FREEZE, OUT_ARG, OUT_ARG2, INOUT_RING, INOUT_STATE};

int args[INOUT_STATE + 1];

static const char desc[] = "Granular player (a file or a live ring)";
thPlugin::State    mystate = thPlugin::ACTIVE;

#define GRAIN_MAX 64
#define GRAIN_DENSITY_MAX 2000.0f
#define GRAIN_RATIO_MAX 16.0f
#define GRAIN_JITTER_MAX 24.0f
#define GRAIN_ROOT_DEFAULT 261.63f

/* Seconds of live ring, and the shortest and longest grain, as fractions
   of the rate. */
#define GRAIN_RING_SECONDS 8
#define GRAIN_SIZE_MIN 0.001f
#define GRAIN_SIZE_MAX 1.0f

/* The state: the dice, whether they are seeded, the launch clock, which
   side the next grain goes to, the live ring's write head, and then each
   grain's six numbers. A grain of length 0 is a free slot. */
enum { S_DICE0, S_DICE1, S_SEEDED, S_CLOCK, S_SIDE, S_HEAD, S_GRAINS };
enum { G_POS, G_STEP, G_AGE, G_LEN, G_SIDE, G_GAIN, G_COUNT };
#define S_COUNT (S_GRAINS + GRAIN_MAX * G_COUNT)

void module_cleanup (thPlugin *plugin)
{
    thSampleRelease(plugin);
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    thSampleClaim(plugin);

    args[IN_FILE] = plugin->regArg("file", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FILE],
                       "The wav to read, found under samples/ on "
                       "THINK_DSP_PATH");
    args[IN_POSITION] = plugin->regArg("position", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_POSITION],
                       "Where grains start: through the file, or back into "
                       "the live ring");
    plugin->setArgRange(args[IN_POSITION], 0, 1);
    args[IN_SPREAD] = plugin->regArg("spread", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SPREAD],
                       "How far either way from `position' a grain may "
                       "start");
    plugin->setArgRange(args[IN_SPREAD], 0, 1);
    args[IN_SIZE] = plugin->regArg("size", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SIZE], "Each grain's length");
    plugin->setArgUnits(args[IN_SIZE], "samples");
    plugin->setArgRange(args[IN_SIZE], 44, 44100);
    args[IN_DENSITY] = plugin->regArg("density", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DENSITY], "How many grains start a second");
    plugin->setArgUnits(args[IN_DENSITY], "Hz");
    plugin->setArgRange(args[IN_DENSITY], 0, GRAIN_DENSITY_MAX);
    args[IN_PITCH] = plugin->regArg("pitch", thPlugin::ARG_IN);
    /* A zero is the arg left unwritten; a grain that does not move
       through its source is a click, not a pitch. */
    plugin->setArgDesc(args[IN_PITCH],
                       "Each grain's speed through the source: 2 is an "
                       "octave up");
    plugin->setArgRange(args[IN_PITCH], 0.25f, 4);
    plugin->setArgDefault(args[IN_PITCH], 1);
    args[IN_JITTER] = plugin->regArg("jitter", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_JITTER],
                       "Random detune either way, drawn per grain");
    plugin->setArgUnits(args[IN_JITTER], "semitones");
    plugin->setArgRange(args[IN_JITTER], 0, 12);
    args[IN_WINDOW] = plugin->regArg("window", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_WINDOW],
                       "Each grain's shape: 0 Hann, 1 trapezoid");
    plugin->setArgRange(args[IN_WINDOW], 0, 1);
    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FREQ],
                       "The note to play the cloud at; 0 is no keyboard");
    plugin->setArgUnits(args[IN_FREQ], "Hz");
    args[IN_ROOT] = plugin->regArg("root", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ROOT],
                       "The frequency the source is at; `root = freq' "
                       "plays it unpitched");
    plugin->setArgUnits(args[IN_ROOT], "Hz");
    plugin->setArgDefault(args[IN_ROOT], GRAIN_ROOT_DEFAULT);
    args[IN_SEED] = plugin->regArg("seed", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SEED],
                       "Which cloud; read on the voice's first sample");
    args[IN_SOURCE] = plugin->regArg("source", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SOURCE],
                       "0 reads `file', 1 reads a ring of `in'");
    plugin->setArgRange(args[IN_SOURCE], 0, 1);
    plugin->setArgStep(args[IN_SOURCE], 1);
    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "What the live ring records");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_FREEZE] = plugin->regArg("freeze", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FREEZE],
                       "1 stops the live ring recording, so the grains read "
                       "what it last held");
    plugin->setArgRange(args[IN_FREEZE], 0, 1);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "Every other grain");
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_ARG2] = plugin->regArg("out2", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG2], "The grains between them");
    plugin->setArgUnits(args[OUT_ARG2], "full scale");

    args[INOUT_RING] = plugin->regArg("ring", thPlugin::ARG_STATE);
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

/* The source at `at', between frames by a straight line, as osc::sample
   reads one. A file past either end is silence; the ring wraps. */
static inline float readFile (const float *frames, size_t len, double at)
{
    if (!(at >= 0) || at >= (double)(len - 1))
        return 0;

    const size_t k = (size_t)at;
    const float f = (float)(at - (double)k);

    return frames[k] + (frames[k + 1] - frames[k]) * f;
}

static inline float readRing (const float *ring, size_t len, double at)
{
    at -= floor(at / (double)len) * (double)len;

    size_t k = (size_t)at;

    if (k >= len)
        k = 0;

    const float f = (float)(at - (double)k);
    const float a = ring[k], b = ring[(k + 1) % len];

    return a + (b - a) * f;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_file = mod->getArg(node, args[IN_FILE]);
    thArg *in_position = mod->getArg(node, args[IN_POSITION]);
    thArg *in_spread = mod->getArg(node, args[IN_SPREAD]);
    thArg *in_size = mod->getArg(node, args[IN_SIZE]);
    thArg *in_density = mod->getArg(node, args[IN_DENSITY]);
    thArg *in_pitch = mod->getArg(node, args[IN_PITCH]);
    thArg *in_jitter = mod->getArg(node, args[IN_JITTER]);
    thArg *in_window = mod->getArg(node, args[IN_WINDOW]);
    thArg *in_freq = mod->getArg(node, args[IN_FREQ]);
    thArg *in_root = mod->getArg(node, args[IN_ROOT]);
    thArg *in_seed = mod->getArg(node, args[IN_SEED]);
    thArg *in_source = mod->getArg(node, args[IN_SOURCE]);
    thArg *in_arg = mod->getArg(node, args[IN_ARG]);
    thArg *in_freeze = mod->getArg(node, args[IN_FREEZE]);
    thArg *out_arg = mod->getArg(node, args[OUT_ARG]);
    thArg *out_arg2 = mod->getArg(node, args[OUT_ARG2]);
    thArg *inout_ring = mod->getArg(node, args[INOUT_RING]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);

    float *out = out_arg->allocate(windowlen);
    float *out2 = out_arg2->allocate(windowlen);
    float *state = inout_state->allocate(S_COUNT);

    /* Which source, once a window: the ring is eight seconds and a graph
       reading a file should not pay for it. Switching mid-note hands the
       grains a fresh, silent ring, which is what a ring that was not
       recording holds anyway. */
    const bool live = (*in_source)[0] >= 0.5f;
    const size_t ringLen = live ? (size_t)GRAIN_RING_SECONDS * samples : 0;
    float *ring = live ? inout_ring->allocate((unsigned)ringLen) : NULL;

    const thSampleData *smp = live ? NULL :
        thSampleGet(node->plugin(), in_file->text(), samples);
    const size_t fileLen = (smp != NULL) ? smp->frames.size() : 0;
    const float *frames = fileLen ? &smp->frames[0] : NULL;

    const float shortest = GRAIN_SIZE_MIN * samples;
    const float longest = GRAIN_SIZE_MAX * samples;

    size_t head = (size_t)state[S_HEAD];

    if (head >= ringLen)
        head = 0;

    for (unsigned int i = 0; i < windowlen; i++)
    {
        if (state[S_SEEDED] == 0)
        {
            thDiceSeed(&state[S_DICE0], (*in_seed)[i]);
            /* A grain on the voice's first sample, rather than a gap of
               1/density before the first sound. */
            state[S_CLOCK] = 1;
            state[S_SEEDED] = 1;
        }

        /* Frozen, the ring neither records nor moves on: `position' is
           how far back from the newest sample a grain starts, and a head
           that went on advancing over a ring that had stopped recording
           would walk the reads off the end of what it holds. */
        const bool recording = live && !((*in_freeze)[i] >= 0.5f);

        if (recording)
        {
            const float x = (*in_arg)[i];

            ring[head] = thIsFinite(x) ? x : 0;
        }

        /* ---- launches ---- */
        const float density = thClampArg((*in_density)[i], 0,
                                         GRAIN_DENSITY_MAX);

        state[S_CLOCK] += density / (float)samples;

        while (state[S_CLOCK] >= 1.0f)
        {
            state[S_CLOCK] -= 1.0f;

            /* Two draws a grain, always, whether or not it finds a slot
               or has anything to read -- so a knob turned or a slot
               filled does not reseed every grain after it. */
            const double uPlace = thDiceNext(&state[S_DICE0]);
            const double uTune = thDiceNext(&state[S_DICE0]);
            int slot = -1;

            for (int g = 0; g < GRAIN_MAX && slot < 0; g++)
                if (state[S_GRAINS + g * G_COUNT + G_LEN] == 0)
                    slot = g;

            if (slot < 0 || (!live && fileLen < 2))
                continue;

            const float size = thClampArg((*in_size)[i], shortest, longest);
            const float pitch = (*in_pitch)[i] == 0 ? 1 :
                thClampArg((*in_pitch)[i], 1.0f / GRAIN_RATIO_MAX,
                           GRAIN_RATIO_MAX);
            const float jitter = thClampArg((*in_jitter)[i], 0,
                                            GRAIN_JITTER_MAX);
            const float freq = (*in_freq)[i];
            const float root = (*in_root)[i];
            double ratio = pitch * exp2(jitter * (2 * uTune - 1) / 12);

            if (freq > 0)
                ratio *= thBoundFreq(freq, samples) /
                         thBoundFreq(root > 0 ? root : GRAIN_ROOT_DEFAULT,
                                     samples);

            if (ratio > GRAIN_RATIO_MAX)
                ratio = GRAIN_RATIO_MAX;
            else if (!(ratio >= 1.0 / GRAIN_RATIO_MAX))
                ratio = 1.0 / GRAIN_RATIO_MAX;

            const double position = thClampArg((*in_position)[i], 0, 1);
            const double spread = thClampArg((*in_spread)[i], 0, 1);
            const double span = size * ratio;       /* source it reads */
            double start;

            if (live)
            {
                /* Back from the newest sample, and kept where the grain
                   neither overtakes the write head nor falls off the far
                   end within its length. */
                const double far = (double)ringLen - 2;
                double back = (position + spread * (2 * uPlace - 1)) *
                              (double)ringLen;
                const double nearest = (ratio > 1 ? size * (ratio - 1) : 0)
                                       + 2;
                const double farthest = far -
                                        (ratio < 1 ? size * (1 - ratio) : 0);

                if (back > farthest)
                    back = farthest;

                if (back < nearest)
                    back = nearest;

                start = (double)head - back;
            }
            else
            {
                const double room = (double)fileLen - 2 - span;

                start = (position + spread * (2 * uPlace - 1)) *
                        (double)fileLen;

                if (start > room)
                    start = room;

                if (start < 0)
                    start = 0;
            }

            /* The level for this grain's side: half the density, over
               its length, times the window's mean square. */
            const float window = thClampArg((*in_window)[i], 0, 1);
            const double meanSquare = 0.375 + (2.0 / 3.0 - 0.375) * window;
            const double overlap = density / 2 * size / samples;
            const double crowd = overlap * meanSquare;
            float *gr = &state[S_GRAINS + slot * G_COUNT];

            gr[G_POS] = (float)start;
            gr[G_STEP] = (float)ratio;
            gr[G_AGE] = 0;
            gr[G_LEN] = floorf(size);
            gr[G_SIDE] = state[S_SIDE];
            gr[G_GAIN] = (float)(crowd > 1 ? 1 / sqrt(crowd) : 1);

            state[S_SIDE] = state[S_SIDE] == 0 ? 1.0f : 0.0f;
        }

        /* ---- the grains ---- */
        const float window = thClampArg((*in_window)[i], 0, 1);
        float l = 0, r = 0;

        for (int g = 0; g < GRAIN_MAX; g++)
        {
            float *gr = &state[S_GRAINS + g * G_COUNT];

            if (gr[G_LEN] == 0)
                continue;

            const float t = gr[G_AGE] / gr[G_LEN];
            const float hann = 0.5f - 0.5f * (float)cos(2.0 * M_PI * t);
            const float edge = 4 * (t < 0.5f ? t : 1 - t);
            const float trap = edge < 1 ? edge : 1;
            const float w = hann + (trap - hann) * window;
            const float x = live ? readRing(ring, ringLen, gr[G_POS])
                                 : readFile(frames, fileLen, gr[G_POS]);
            const float v = gr[G_GAIN] * w * x;

            if (gr[G_SIDE] == 0)
                l += v;
            else
                r += v;

            gr[G_POS] += gr[G_STEP];
            gr[G_AGE] += 1;

            /* The ring's grains are kept inside the ring's numbers, so a
               long note does not walk a float past where it can count. */
            if (live && gr[G_POS] >= (float)ringLen)
                gr[G_POS] -= (float)ringLen;

            if (gr[G_AGE] >= gr[G_LEN])
                gr[G_LEN] = 0;
        }

        out[i] = TH_MAX * l;
        out2[i] = TH_MAX * r;

        if (recording)
            head = (head + 1) % ringLen;
    }

    state[S_HEAD] = (float)head;

    return 0;
}
