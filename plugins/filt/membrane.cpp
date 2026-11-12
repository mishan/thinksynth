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

/* A struck membrane: a drumhead's ring. See modal.h for the bank.
 *
 * The ratios are an ideal circular membrane's: the zeros of the Bessel
 * functions, each over the first. An ideal membrane and not a timpani:
 * the air a kettle holds pulls a timpani's modes nearly harmonic, which
 * this does not model. What it is near is a tom's or a frame drum's ring,
 * dense and low, and gone in under a second.
 */

#include "think.h"

#include "modal.h"

static const double ratios[] = { 1, 1.5933, 2.1355, 2.2954, 2.6531, 2.9173,
                                 3.1555, 3.5001, 3.5985, 3.6475, 4.0589,
                                 4.1317, 4.2304, 4.6010, 4.6101, 4.8319 };

static const thkModal::Material membrane = {
    ratios, (int)(sizeof ratios / sizeof ratios[0]), 10, 0.4f, 1, -6,
    "The pitch heard: the membrane's lowest mode"
};

std::atomic<int> args[MODAL_ARGS];

static const char desc[] = "A struck membrane: a drumhead's modes";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);
    thModalInit(plugin, args, membrane);

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
    return thModalCallback(node, mod, windowlen, samples, args, membrane);
}
