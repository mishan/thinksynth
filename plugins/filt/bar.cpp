/*
 * Copyright (C) 2026 The thinksynth authors
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

/* A struck bar: a glockenspiel's key, a wind chime, the nearest thing here
 * to a triangle. See modal.h for the bank.
 *
 * The ratios are a free-free uniform bar's (Euler-Bernoulli): (bn / b1)^2,
 * b the roots of cos(b) cosh(b) = 1 -- 4.730040745, 7.853204624,
 * 10.995607838, 14.137165491, 17.278759657, and (2n + 1) pi/2 after, to
 * better than a part in a million. Nothing here is tuned by ear. A table
 * and not a loop in module_init: every synth that loads the plugin runs
 * that, and one would rewrite it under another reading it. A real
 * glockenspiel key is cut under its middle to pull the second mode toward a
 * harmonic; this is the bar before the cut, which is what wind chimes and
 * struck rods are.
 */

#include "think.h"

#include "modal.h"

static const double ratios[] = { 1, 2.756539, 5.403918, 8.932950, 13.344287,
                                 18.637888, 24.813756, 31.871891, 39.812293,
                                 48.634962, 58.339898, 68.927100, 80.396570,
                                 92.748306, 105.982309, 120.098579 };

static const thkModal::Material bar = {
    ratios, (int)(sizeof ratios / sizeof ratios[0]), 8, 1, 1, -6,
    "The pitch heard: the bar's lowest mode"
};

std::atomic<int> args[MODAL_ARGS];

static const char desc[] = "A struck bar: modes at a free bar's ratios";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);
    thModalInit(plugin, args, bar);

    return 0;
}

void module_prepare (thNode *node, thSynthTree *mod, unsigned int samples,
                     int voice)
{
    thModalPrepare(node, mod, voice, args);
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    return thModalCallback(node, mod, windowlen, samples, args, bar);
}
