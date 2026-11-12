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
 * b the roots of cos(b) cosh(b) = 1, which past the fifth are (2n + 1) pi/2
 * to better than a part in a million. Nothing here is tuned by ear. A real
 * glockenspiel key is cut under its middle to pull the second mode toward a
 * harmonic; this is the bar before the cut, which is what wind chimes and
 * struck rods are.
 */

#include "think.h"

#include "modal.h"

static double ratios[thkModal::MODES_MAX];

static const thkModal::Material bar = {
    ratios, thkModal::MODES_MAX, 8, 1, 1, -6,
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
    const double roots[] = { 4.730040745, 7.853204624, 10.995607838,
                             14.137165491, 17.278759657 };

    for (int k = 0; k < thkModal::MODES_MAX; k++)
    {
        const double b = k < 5 ? roots[k] : (2 * k + 3) * M_PI / 2;

        ratios[k] = (b / roots[0]) * (b / roots[0]);
    }

    plugin->setDesc (desc);
    plugin->setState (mystate);
    thModalInit(plugin, args, bar);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    return thModalCallback(node, mod, windowlen, samples, args, bar);
}
