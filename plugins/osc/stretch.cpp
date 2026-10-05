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

/* A recording played at one speed and heard at another pitch.
 *
 * osc::sample reads a file at `freq / root', which is a tape machine:
 * twice as fast is an octave up. This one takes the two apart. A
 * playhead moves through the file at `speed' -- 1 as recorded, 0.5 half
 * as fast, 0 stopped on one moment -- and what is heard there is read in
 * short overlapping grains at `pitch', so a drum loop can follow a
 * piece's tempo without the drums going up, and a voice can be slowed to
 * a crawl and stay a voice.
 *
 * FOUR GRAINS OVERLAP, each `size' samples long under a Hann window, one
 * starting every quarter of that. Four Hann windows a quarter apart sum
 * to a constant 2, so the output is divided by 2 and a steady sound comes
 * out at the level it went in -- provided the grains agree. Started
 * exactly at the playhead they do not: at half speed two grains overlap
 * reading a tone at positions a fraction of a cycle apart, and they
 * cancel. So each grain starts within an eighth of `size' of the
 * playhead, at whichever offset best matches what the grain before it
 * would have read next (WSOLA, Verhelst and Roelands 1993): the overlap
 * is then the same waveform twice, in phase, and only the playhead's
 * long-run speed is `speed'. The match is a cross-correlation at every
 * fourth sample over a quarter of `size'.
 *
 * The price is the smear: a transient is heard once per grain that
 * covers it, which is a flam on a drum at a large `size' and a reason to
 * keep it small for beats and large for pads.
 *
 * TO FOLLOW A TEMPO, a graph sets `speed' to the piece's tempo over the
 * one the loop was played at -- `tempo->bpm / 100' for a loop at 100 --
 * through misc::tempo; this node knows nothing about beats.
 *
 * `file' is osc::sample's: a wav under samples/ on THINK_DSP_PATH, read
 * once per synth (osc/sampleslot.h), or zones -- "a.wav@60 b.wav@62" --
 * with `note' choosing one, so one graph can hold several loops and the
 * note a piece plays says which.
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

enum {IN_FILE, IN_NOTE, IN_SPEED, IN_PITCH, IN_SIZE, IN_START, IN_LOOP,
      IN_TRIGGER, OUT_ARG, OUT_PLAY, INOUT_STATE};

std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "Time-stretch (a wav at one speed, another pitch)";
thPlugin::State    mystate = thPlugin::ACTIVE;

#define GRAINS 4

/* Grain lengths, in seconds: under 10 ms a grain is a buzz at its own
   rate, and over half a second it is an echo. */
#define GRAIN_MIN 0.01f
#define GRAIN_MAX 0.5f

/* The state: the playhead, samples until the next grain starts, the slot
   it starts in, the last trigger, whether the voice has started, and per
   grain where it reads and how far into its window it is. */
enum { S_HEAD, S_UNTIL, S_NEXT, S_TRIG, S_STARTED, S_GRAIN,
       S_LEN = S_GRAIN + 2 * GRAINS };

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
                       "The wav to play, found under samples/ on "
                       "THINK_DSP_PATH; or zones, `a.wav@60 b.wav@62', "
                       "chosen by `note'");
    args[IN_NOTE] = plugin->regArg("note", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_NOTE],
                       "Which zone of a zoned `file': the one recorded "
                       "nearest this MIDI note");
    args[IN_SPEED] = plugin->regArg("speed", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SPEED],
                       "How fast the playhead moves through the file: 1 as "
                       "recorded, 0 holds one moment, negative goes back");
    plugin->setArgRange(args[IN_SPEED], -4, 4);
    plugin->setArgDefault(args[IN_SPEED], 1);
    plugin->setArgUnits(args[IN_SPEED], "ratio");
    args[IN_PITCH] = plugin->regArg("pitch", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_PITCH],
                       "How fast each grain reads: 1 as recorded, 2 an "
                       "octave up, whatever `speed' is");
    plugin->setArgRange(args[IN_PITCH], 0.25f, 4);
    plugin->setArgDefault(args[IN_PITCH], 1);
    plugin->setArgUnits(args[IN_PITCH], "ratio");
    args[IN_SIZE] = plugin->regArg("size", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SIZE],
                       "Each grain's length: small keeps a drum's attack, "
                       "large smooths a pad");
    plugin->setArgRange(args[IN_SIZE], 441, 22050);
    plugin->setArgUnits(args[IN_SIZE], "samples");
    args[IN_START] = plugin->regArg("start", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_START],
                       "Where the playhead starts, as a fraction of the file");
    plugin->setArgRange(args[IN_START], 0, 1);
    args[IN_LOOP] = plugin->regArg("loop", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_LOOP],
                       "1 wraps the playhead round the file; 0 stops at its "
                       "end");
    plugin->setArgRange(args[IN_LOOP], 0, 1);
    args[IN_TRIGGER] = plugin->regArg("trigger", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_TRIGGER],
                       "Back to `start' when this rises above 0");
    plugin->setArgRange(args[IN_TRIGGER], 0, 1);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The file");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_PLAY] = plugin->regArg("play", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_PLAY],
                       "1 while there is file to play; a loop always has");
    plugin->setArgRange(args[OUT_PLAY], 0, 1);

    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

/* Frame `at' of `f', between samples, wrapped round `len' when `wrap'
   and silent outside it otherwise. */
static inline float frameAt (const float *f, size_t len, float at, bool wrap)
{
    if (wrap)
    {
        at = fmodf(at, (float)len);
        if (at < 0)
            at += len;
    }
    else if (!(at >= 0 && at <= (float)(len - 1)))
        return 0;

    const size_t i = (size_t)at;
    const size_t j = i + 1 < len ? i + 1 : (wrap ? 0 : i);
    const float frac = at - (float)i;

    return f[i] + (f[j] - f[i]) * frac;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_file = mod->getArg(node, args[IN_FILE]);
    thArg *in_note = mod->getArg(node, args[IN_NOTE]);
    thArg *in_speed = mod->getArg(node, args[IN_SPEED]);
    thArg *in_pitch = mod->getArg(node, args[IN_PITCH]);
    thArg *in_size = mod->getArg(node, args[IN_SIZE]);
    thArg *in_start = mod->getArg(node, args[IN_START]);
    thArg *in_loop = mod->getArg(node, args[IN_LOOP]);
    thArg *in_trigger = mod->getArg(node, args[IN_TRIGGER]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);

    /* Float throughout, as it is kept between windows: a running value
       held wider inside a window than across one comes out differently at
       one sample a window than at five hundred. A float playhead is a
       sixteenth of a sample out at a minute in, which no ear can hear. */
    float st[S_LEN];
    const unsigned had = inout_state->len();

    for (int k = 0; k < S_LEN; k++)
        st[k] = (unsigned)k < had ? (*inout_state)[k] : 0;

    float *state = inout_state->allocate(S_LEN);

    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    float *play = mod->getArg(node, args[OUT_PLAY])->allocate(windowlen);

    const std::string &text = in_file->text();
    const thSampleZone *zone = thSampleIsZoned(text)
        ? thSampleZoneFor(node->plugin(), text, (*in_note)[0]) : NULL;
    const thSampleData *smp = thSampleGet(node->plugin(),
                                          zone ? zone->file : text, samples);
    const size_t len = smp ? smp->frames.size() : 0;
    const float *frames = len ? &smp->frames[0] : NULL;

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float trig = (*in_trigger)[i];
        const bool wrap = (*in_loop)[i] >= 0.5f;

        if (len == 0)
        {
            out[i] = 0;
            play[i] = 0;
            continue;
        }

        if (st[S_STARTED] == 0 || (trig > 0 && st[S_TRIG] <= 0))
        {
            st[S_HEAD] = thClampArg((*in_start)[i], 0, 1) * (float)(len - 1);
            st[S_UNTIL] = 0;
            st[S_NEXT] = 0;
            st[S_STARTED] = 1;

            for (int g = 0; g < GRAINS; g++)
                st[S_GRAIN + 2 * g + 1] = 1;     /* every window done */
        }
        st[S_TRIG] = trig;

        const float speed = thClampArg((*in_speed)[i], -4, 4);
        const float pitch = thClampArg((*in_pitch)[i], 0.25f, 4);
        const float size = thClampArg((*in_size)[i], GRAIN_MIN * samples,
                                      GRAIN_MAX * samples);

        /* A grain starts every quarter of `size', near the playhead, where
           it best continues the grain started before it. */
        if (st[S_UNTIL] <= 0)
        {
            const int g = (int)st[S_NEXT] % GRAINS;
            const int prev = (g + GRAINS - 1) % GRAINS;
            float start = st[S_HEAD];

            if (st[S_GRAIN + 2 * prev + 1] < 1)
            {
                const float next = st[S_GRAIN + 2 * prev];
                const int reach = (int)(size / 8);
                const int span = (int)(size / 4);
                float best = -1e30f;

                for (int c = -reach; c <= reach; c += 2)
                {
                    const float at = st[S_HEAD] + (float)c;
                    float score = 0;

                    for (int k = 0; k < span; k += 4)
                        score += frameAt(frames, len, next + k * pitch, wrap) *
                                 frameAt(frames, len, at + k * pitch, wrap);

                    if (score > best)
                    {
                        best = score;
                        start = at;
                    }
                }
            }

            st[S_GRAIN + 2 * g] = start;
            st[S_GRAIN + 2 * g + 1] = 0;
            st[S_NEXT] = (g + 1) % GRAINS;
            st[S_UNTIL] += size / GRAINS;
        }
        st[S_UNTIL] -= 1;

        float y = 0;

        for (int g = 0; g < GRAINS; g++)
        {
            float &at = st[S_GRAIN + 2 * g];
            float &phase = st[S_GRAIN + 2 * g + 1];

            if (phase >= 1)
                continue;

            const float w = 0.5f - 0.5f * cosf(2 * (float)M_PI * phase);

            y += w * frameAt(frames, len, at, wrap);
            at += pitch;
            phase += 1 / size;
        }

        out[i] = y * 0.5f;

        st[S_HEAD] += speed;

        bool more = true;

        if (wrap)
        {
            st[S_HEAD] = fmodf(st[S_HEAD], (float)len);
            if (st[S_HEAD] < 0)
                st[S_HEAD] += len;
        }
        else
            more = st[S_HEAD] >= 0 && st[S_HEAD] < (float)len;

        play[i] = more ? 1 : 0;
    }

    memcpy(state, st, sizeof(st));

    return 0;
}
