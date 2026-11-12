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

/* A struck bell: a church bell's partials, tuned. See modal.h for the
 * bank.
 *
 * A bell's strike note is heard at its prime, an octave under the
 * nominal; so `freq' is the prime, and the hum rings an octave under it. The ratios are the tuned
 * principal partials (Rossing, The Science of Percussion Instruments):
 * hum, prime, tierce (a minor third), quint, nominal, deciem, undeciem,
 * duodeciem, double octave, upper undeciem, upper duodeciem, triple
 * octave. The tierce is what makes it a church bell and not a chime.
 *
 * Its own d is 0.7 rather than a bar's 1: a bell's upper partials ring
 * on longer against its hum than a bar's do against its fundamental. At
 * the default 8 s, the hum rings for about 13.
 */

#include "think.h"

#include "modal.h"

static const double ratios[] = { 0.5, 1, 1.2, 1.5, 2, 2.5, 8.0 / 3, 3, 4,
                                 16.0 / 3, 6, 8 };

static const thkModal::Material bell = {
    ratios, (int)(sizeof ratios / sizeof ratios[0]), 12, 8, 0.7f, -3,
    "The strike note: the bell's prime, an octave under its nominal"
};

std::atomic<int> args[MODAL_ARGS];

static const char desc[] = "A struck bell: a church bell's tuned partials";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);
    thModalInit(plugin, args, bell);

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
    return thModalCallback(node, mod, windowlen, samples, args, bell);
}
