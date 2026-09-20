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
 * `file2' AND `file3' ARE VELOCITY LAYERS. `select' chooses one on the
 * trigger edge and that file stays under the voice until its next edge.
 * Omitted layers fall back to the one below them. With `alternate = 1',
 * a counter shared by this synth's voices chooses 1, 2, 3, 1 instead.
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

enum {IN_FILE, IN_FILE2, IN_FILE3, IN_FREQ, IN_ROOT, IN_START, IN_LOOP,
      IN_TRIGGER, IN_SELECT, IN_SPLIT1, IN_SPLIT2, IN_ALTERNATE,
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
    args[IN_FILE2] = plugin->regArg("file2", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FILE2],
                       "Middle layer wav; an empty slot uses file");
    args[IN_FILE3] = plugin->regArg("file3", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FILE3],
                       "Upper layer wav; an empty slot uses file2 or file");
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
    args[IN_SELECT] = plugin->regArg("select", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SELECT],
                       "Layer choice at trigger: below split1 is file, "
                       "below split2 is file2, above is file3");
    plugin->setArgRange(args[IN_SELECT], 0, 1);
    args[IN_SPLIT1] = plugin->regArg("split1", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SPLIT1], "Boundary between file and file2");
    plugin->setArgRange(args[IN_SPLIT1], 0, 1);
    plugin->setArgDefault(args[IN_SPLIT1], 1.0f / 3.0f);
    args[IN_SPLIT2] = plugin->regArg("split2", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SPLIT2], "Boundary between file2 and file3");
    plugin->setArgRange(args[IN_SPLIT2], 0, 1);
    plugin->setArgDefault(args[IN_SPLIT2], 2.0f / 3.0f);
    args[IN_ALTERNATE] = plugin->regArg("alternate", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ALTERNATE],
                       "Cycle layers 1, 2, 3 across this synth's triggers");
    plugin->setArgRange(args[IN_ALTERNATE], 0, 1);
    plugin->setArgStep(args[IN_ALTERNATE], 1);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The file");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_PLAY] = plugin->regArg("play", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_PLAY],
                       "1 while there is file left, so a one-shot's note "
                       "can be the sample's own length");
    plugin->setArgRange(args[OUT_PLAY], 0, 1);

    /* [0] the playhead, [1] the last trigger, [2] the chosen layer. */
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out, *play;
    float *state;
    thArg *in_file, *in_file2, *in_file3, *in_freq, *in_root;
    thArg *in_start, *in_loop, *in_trigger, *in_select, *in_split1;
    thArg *in_split2, *in_alternate;
    thArg *out_arg, *out_play;
    thArg *inout_state;
    unsigned int i;
    float at, lastTrigger;
    unsigned layer;

    in_file = mod->getArg(node, args[IN_FILE]);
    in_file2 = mod->getArg(node, args[IN_FILE2]);
    in_file3 = mod->getArg(node, args[IN_FILE3]);
    in_freq = mod->getArg(node, args[IN_FREQ]);
    in_root = mod->getArg(node, args[IN_ROOT]);
    in_start = mod->getArg(node, args[IN_START]);
    in_loop = mod->getArg(node, args[IN_LOOP]);
    in_trigger = mod->getArg(node, args[IN_TRIGGER]);
    in_select = mod->getArg(node, args[IN_SELECT]);
    in_split1 = mod->getArg(node, args[IN_SPLIT1]);
    in_split2 = mod->getArg(node, args[IN_SPLIT2]);
    in_alternate = mod->getArg(node, args[IN_ALTERNATE]);

    inout_state = mod->getArg(node, args[INOUT_STATE]);

    /* In float, and stepped in float, so that a window boundary is not
       an event -- delay::chorus's rule, and the same arithmetic. It also
       sets the ceiling on a file's length: past about six minutes a
       float's mantissa can no longer hold a frame index and its
       fraction, and the interpolation goes coarse. A sampler of the
       period held eight seconds. */
    at = (*inout_state)[0];
    lastTrigger = (*inout_state)[1];
    layer = (unsigned)thClampArg((*inout_state)[2], 0, 2);
    state = inout_state->allocate(3);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);
    out_play = mod->getArg(node, args[OUT_PLAY]);
    play = out_play->allocate(windowlen);

    /* Resolve at most once per window, and once more if a retrigger changes
       layer inside it. The counter lives in the synth's shared sample slot;
       the chosen layer and playhead live in this voice's state arg. */
    const thSampleData *smp = NULL;
    size_t len = 0;
    unsigned loadedLayer = 3;

    for (i = 0; i < windowlen; i++)
    {
        const float trigger = (*in_trigger)[i];
        const bool edge = trigger > 0 && !(lastTrigger > 0);
        float step;

        if (edge)
        {
            if ((*in_alternate)[i] > 0)
                layer = thSampleNextLayer(node->plugin(), in_file->text());
            else
            {
                const float select = thClampArg((*in_select)[i], 0, 1);
                const float split1 = thClampArg((*in_split1)[i], 0, 1);
                const float split2 = thClampArg((*in_split2)[i], split1, 1);

                layer = select < split1 ? 0 : select < split2 ? 1 : 2;
            }
        }

        lastTrigger = trigger;

        if (loadedLayer != layer)
        {
            const std::string &name =
                (layer == 2 && !in_file3->text().empty()) ? in_file3->text() :
                (layer >= 1 && !in_file2->text().empty()) ? in_file2->text() :
                in_file->text();

            smp = thSampleGet(node->plugin(), name, samples);
            len = (smp != NULL) ? smp->frames.size() : 0;
            loadedLayer = layer;
        }

        if (edge)
            at = len ? thClampArg((*in_start)[i], 0, (float)(len - 1)) : 0;

        if (len == 0)
        {
            out[i] = 0;
            play[i] = 0;
            continue;
        }

        const float loop = thClampArg((*in_loop)[i], 0, (float)len);
        const float root = (*in_root)[i];

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
            const float a = smp->frames[k];
            const float b = smp->frames[(k + 1 < len) ? k + 1 : k];

            out[i] = TH_MAX * (a + (b - a) * frac);
        }

        play[i] = 1;
        at += step;
    }

    state[0] = at;
    state[1] = lastTrigger;
    state[2] = (float)layer;

    return 0;
}
