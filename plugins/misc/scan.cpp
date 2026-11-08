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

/* Where on a video synth's raster each sample lands.
 *
 * A video synth runs at the pixel clock, one sample a pixel, row by row
 * (thSynth::setRaster). A voice's own state starts at its note-on, so it
 * cannot know where on the screen it is; this reads the synth's count of
 * samples instead, so every voice draws onto the one raster whenever it
 * began. `x' and `y' run 0 to 1 across and down: a ring is a sine of
 * (x - 0.5)^2 + (y - 0.5)^2, and a bar a sine of y. `t' is seconds since
 * the synth started, for what moves.
 *
 * Worked out from the count in 64-bit integers, so the picture is as
 * steady after an hour as in the first frame. On a synth with no raster
 * every output is 0.
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

enum {OUT_X, OUT_Y, OUT_HSYNC, OUT_VSYNC, OUT_FRAME, OUT_T, INOUT_LAST};

std::atomic<int> args[INOUT_LAST];

static const char desc[] = "Where on the raster a sample lands";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[OUT_X] = plugin->regArg("x", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_X], "Across, 0 at the left edge to 1 at the right");
    plugin->setArgRange(args[OUT_X], 0, 1);
    args[OUT_Y] = plugin->regArg("y", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_Y], "Down, 0 at the top to 1 at the bottom");
    plugin->setArgRange(args[OUT_Y], 0, 1);
    args[OUT_HSYNC] = plugin->regArg("hsync", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_HSYNC], "1 on a line's first pixel, else 0");
    plugin->setArgRange(args[OUT_HSYNC], 0, 1);
    args[OUT_VSYNC] = plugin->regArg("vsync", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_VSYNC], "1 on a frame's first pixel, else 0");
    plugin->setArgRange(args[OUT_VSYNC], 0, 1);
    args[OUT_FRAME] = plugin->regArg("frame", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_FRAME], "Which frame, from 0");
    args[OUT_T] = plugin->regArg("t", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_T], "Seconds since the synth started");
    plugin->setArgUnits(args[OUT_T], "seconds");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    const thSynth *synth = mod->synth();
    const int64_t w = synth ? synth->rasterWidth() : 0;
    const int64_t h = synth ? synth->rasterHeight() : 0;
    const int64_t at = synth ? synth->position() : 0;

    float *x = mod->getArg(node, args[OUT_X])->allocate(windowlen);
    float *y = mod->getArg(node, args[OUT_Y])->allocate(windowlen);
    float *hsync = mod->getArg(node, args[OUT_HSYNC])->allocate(windowlen);
    float *vsync = mod->getArg(node, args[OUT_VSYNC])->allocate(windowlen);
    float *frame = mod->getArg(node, args[OUT_FRAME])->allocate(windowlen);
    float *t = mod->getArg(node, args[OUT_T])->allocate(windowlen);

    for (unsigned int i = 0; i < windowlen; i++)
    {
        if (w == 0)
        {
            x[i] = y[i] = hsync[i] = vsync[i] = frame[i] = t[i] = 0;
            continue;
        }

        const int64_t n = at + i, column = n % w, row = n / w % h;

        x[i] = (float)column / w;
        y[i] = (float)row / h;
        hsync[i] = column == 0;
        vsync[i] = column == 0 && row == 0;
        frame[i] = (float)(n / (w * h));
        t[i] = (float)((double)n / samples);
    }

    return 0;
}
