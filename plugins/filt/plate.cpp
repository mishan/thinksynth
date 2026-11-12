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

/* A struck plate: a gong's or a sheet's clang. See modal.h for the bank.
 *
 * The ratios are a square plate's with its edges supported: its modes go
 * as m^2 + n^2 for whole m, n from 1, here over the lowest's 2 and each
 * frequency once, since (1, 2) and (2, 1) ring at the same one. Dense and
 * unrelated above the first few, which is the clang. A real gong is
 * domed and free at its rim and pitches its own way; this is the plate
 * before any of that.
 */

#include "think.h"

#include "modal.h"

static const double ratios[] = { 1, 2.5, 4, 5, 6.5, 8.5, 9, 10, 12.5, 13,
                                 14.5, 16, 17, 18.5, 20, 20.5 };

static const thkModal::Material plate = {
    ratios, (int)(sizeof ratios / sizeof ratios[0]), 16, 3, 0.5f, -3,
    "The pitch heard: the plate's lowest mode"
};

std::atomic<int> args[MODAL_ARGS];

static const char desc[] = "A struck plate: a square plate's modes";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);
    thModalInit(plugin, args, plate);

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
    return thModalCallback(node, mod, windowlen, samples, args, plate);
}
