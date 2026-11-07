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

/* The piece's tempo, for a graph to time things by.
 *
 * A dotted-eighth echo is `delay = tempo->beat * 0.75', and an LFO a bar
 * long in 4/4 is `rate = tempo->bpm / 240'. The value is the synth's,
 * which the scheduler sets from the piece's `tempo' line and its tempo
 * control; with no piece playing it is 120. It is read once a window,
 * so a tempo change reaches a graph within one.
 *
 * Rate only, not phase: the node knows how long a beat is and not where
 * the bar starts. An LFO in a voice is in phase with the grid because the
 * note that started it was.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thSynthTree.h"
#include "thSynth.h"

enum {OUT_BPM, OUT_BEAT, INOUT_LAST};

std::atomic<int> args[INOUT_LAST];

static const char desc[] = "The piece's tempo";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[OUT_BPM] = plugin->regArg("bpm", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_BPM], "Beats per minute; 120 with no piece");
    plugin->setArgUnits(args[OUT_BPM], "BPM");
    args[OUT_BEAT] = plugin->regArg("beat", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_BEAT], "How long a beat is");
    plugin->setArgUnits(args[OUT_BEAT], "samples");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    const thSynth *synth = mod->synth();
    const float bpm = synth ? synth->tempo() : 120;
    const float beat = (float)samples * 60 / bpm;

    float *out_bpm = mod->getArg(node, args[OUT_BPM])->allocate(windowlen);
    float *out_beat = mod->getArg(node, args[OUT_BEAT])->allocate(windowlen);

    for (unsigned int i = 0; i < windowlen; i++)
    {
        out_bpm[i] = bpm;
        out_beat[i] = beat;
    }

    return 0;
}
