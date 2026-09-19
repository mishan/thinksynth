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

/* A wav, played at a voice's pitch.
 *
 *     out = the file, read at (freq / root) frames per sample
 *
 * and that is the node. What it is for is the half of the decade a graph
 * cannot reach: a LinnDrum, a DMX, a Fairlight, an Emulator. An
 * orchestra hit is a recording of an orchestra and no amount of
 * oscillator is going to be one.
 *
 * `freq' AND `root' RATHER THAN A RATE, because a patch has to survive
 * being played at a pitch nobody thought of. `root' is the frequency the
 * file was recorded at -- middle C unless the graph says otherwise --
 * and the playback rate is the ratio of the two, so a note an octave up
 * reads at twice the speed and comes out an octave up and half as long,
 * which is what a sampler of this period did and is half of why they
 * sound the way they do. A drum writes `root = freq' (both reading the
 * same node) and plays at exactly one frame a sample whatever key it is
 * on, which is the other half.
 *
 * LINEAR INTERPOLATION between frames, for delay::chorus's reason: the
 * playhead lands between samples and the fraction is where the pitch
 * lives. A cubic would cost four reads a frame to fix a treble loss that
 * a drum machine's 8-bit converters never had in the first place.
 *
 * `play' IS THE FILE'S OWN LENGTH. A one-shot's io node writes
 * `play = smp->play' and the note is over when the sample is, with no
 * envelope involved and nothing to tune -- which is exactly how a drum
 * machine behaves and is the reason `play' is an output here rather than
 * something a graph has to work out from a decay time.
 *
 * `loop' IS HOW MANY FRAMES AT THE END REPEAT, and 0 is a one-shot. A
 * string or a choir is trimmed so its last half-second is the sustain,
 * and `loop = 22050' plays that forever; a drum names nothing and stops.
 * The frames-from-the-end spelling rather than a loop point is what lets
 * a file be re-trimmed without the number in the graph going stale.
 *
 * THE FILE IS READ ONCE PER SYNTH, not once per voice -- sixteen voices
 * of a kit share one copy of the kick. See osc/sampleslot.h, which also
 * says why the read happens on the first window that asks for it rather
 * than at load.
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

enum {IN_FILE, IN_FREQ, IN_ROOT, IN_START, IN_LOOP, IN_TRIGGER,
      OUT_ARG, OUT_PLAY, INOUT_STATE};

int args[INOUT_STATE + 1];

static const char desc[] = "Sample Player (a wav at a voice's pitch)";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* Middle C. What a sampler calls a file's root note when nobody has said
   -- and the number a graph overrides with `root = freq' to play a file
   at the speed it was recorded at whatever the key. */
#define SAMPLE_ROOT_DEFAULT 261.63f

/* Sixteen times up and down. Past that a file is being used as a noise
   source rather than as a recording, and the arithmetic stops being
   worth defending: at 64x a drum is four hundred samples long and the
   interpolation is reading every fifth frame. */
#define SAMPLE_RATIO_MAX 16.0f

void module_cleanup (thPlugin *plugin)
{
    thSampleRelease(plugin);
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    /* A table of this synth's files, for the nodes built from this
       plugin to share. osc::noise claims a noise stream in the same
       place and for the same reason. */
    thSampleClaim(plugin);

    args[IN_FILE] = plugin->regArg("file", thPlugin::ARG_IN);
    /* A quoted name in the .dsp -- `file = "kick808.wav";' -- which is
       the one arg in the tree that is not a number. See thArg::ARG_TEXT
       and docs/DSP_FORMAT.md. It cannot be a control, an expression or a
       wire: a filename is not a thing a slider moves, so a kit is one
       node per drum rather than one node with the name swept. */
    plugin->setArgDesc(args[IN_FILE],
                       "The wav to play, found under samples/ on "
                       "THINK_DSP_PATH");
    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FREQ], "The note to play it at");
    plugin->setArgUnits(args[IN_FREQ], "Hz");
    args[IN_ROOT] = plugin->regArg("root", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ROOT],
                       "The frequency the file was recorded at; `root = "
                       "freq' plays it unpitched");
    plugin->setArgUnits(args[IN_ROOT], "Hz");
    plugin->setArgDefault(args[IN_ROOT], SAMPLE_ROOT_DEFAULT);
    args[IN_START] = plugin->regArg("start", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_START], "Where in the file a hit begins");
    plugin->setArgUnits(args[IN_START], "samples");
    args[IN_LOOP] = plugin->regArg("loop", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_LOOP],
                       "How many frames at the end repeat; 0 is a one-shot");
    plugin->setArgUnits(args[IN_LOOP], "samples");
    args[IN_TRIGGER] = plugin->regArg("trigger", thPlugin::ARG_IN);
    /* An edge and not a level, which is the difference between a
       retrigger and a file pinned to its first frame: a note's trigger
       is held at 1 for as long as the key is down. */
    plugin->setArgDesc(args[IN_TRIGGER],
                       "Start again from `start' when this rises above 0");
    plugin->setArgRange(args[IN_TRIGGER], 0, 1);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The file");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_PLAY] = plugin->regArg("play", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_PLAY],
                       "1 while there is file left, so a one-shot's note "
                       "can be the sample's own length");
    plugin->setArgRange(args[OUT_PLAY], 0, 1);

    /* [0] the playhead in frames, [1] the last trigger. */
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out, *play;
    float *state;
    thArg *in_file, *in_freq, *in_root, *in_start, *in_loop, *in_trigger;
    thArg *out_arg, *out_play;
    thArg *inout_state;
    unsigned int i;
    float at, lastTrigger;

    in_file = mod->getArg(node, args[IN_FILE]);
    in_freq = mod->getArg(node, args[IN_FREQ]);
    in_root = mod->getArg(node, args[IN_ROOT]);
    in_start = mod->getArg(node, args[IN_START]);
    in_loop = mod->getArg(node, args[IN_LOOP]);
    in_trigger = mod->getArg(node, args[IN_TRIGGER]);

    inout_state = mod->getArg(node, args[INOUT_STATE]);

    /* In float, and stepped in float, so that a window boundary is not
       an event -- delay::chorus's rule, and the same arithmetic. It also
       sets the ceiling on a file's length: past about six minutes a
       float's mantissa can no longer hold a frame index and its
       fraction, and the interpolation goes coarse. A sampler of the
       period held eight seconds. */
    at = (*inout_state)[0];
    lastTrigger = (*inout_state)[1];
    state = inout_state->allocate(2);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);
    out_play = mod->getArg(node, args[OUT_PLAY]);
    play = out_play->allocate(windowlen);

    /* The file, by the name the .dsp wrote. Looked up per window rather
       than cached on the node: a map lookup on a short string once a
       window is nothing beside the work below, and a node has nowhere to
       keep a pointer that survives the per-note tree copy. */
    const thSampleData *smp =
        thSampleGet(node->plugin(), in_file->text(), samples);
    const size_t len = (smp != NULL) ? smp->frames.size() : 0;

    if (len == 0)
    {
        for (i = 0; i < windowlen; i++)
        {
            out[i] = 0;
            play[i] = 0;
        }

        state[0] = at;
        state[1] = lastTrigger;

        return 0;
    }

    const float *frames = &smp->frames[0];

    for (i = 0; i < windowlen; i++)
    {
        const float trigger = (*in_trigger)[i];
        const float start = thClampArg((*in_start)[i], 0, (float)(len - 1));
        const float loop = thClampArg((*in_loop)[i], 0, (float)len);
        const float root = (*in_root)[i];
        float step;

        /* A rising edge. A note holds its trigger at 1 for as long as
           the key is down, so a level test would pin the playhead to
           `start' and the file would never move. */
        if (trigger > 0 && !(lastTrigger > 0))
            at = start;

        lastTrigger = trigger;

        /* Both bounded, so a root of 0 -- which is what an unwired
           `root' reads as before its default is applied -- is middle C
           rather than a division by nothing. */
        step = (float)(thBoundFreq((*in_freq)[i], samples) /
                       thBoundFreq(root > 0 ? root : SAMPLE_ROOT_DEFAULT,
                                   samples));
        step = thClampArg(step, 1.0f / SAMPLE_RATIO_MAX, SAMPLE_RATIO_MAX);

        if (at >= (float)len)
        {
            /* Past the end. A one-shot stops and says so; a loop takes
               the last `loop' frames again. */
            if (loop >= 1)
            {
                at -= floorf(loop);

                if (at < 0 || at >= (float)len)
                    at = (float)len - floorf(loop);
            }
            else
            {
                out[i] = 0;
                play[i] = 0;
                continue;
            }
        }

        {
            const size_t k = (size_t)at;
            const float frac = at - (float)k;
            const float a = frames[k];
            const float b = frames[(k + 1 < len) ? k + 1 : k];

            out[i] = TH_MAX * (a + (b - a) * frac);
        }

        play[i] = 1;
        at += step;
    }

    state[0] = at;
    state[1] = lastTrigger;

    return 0;
}
