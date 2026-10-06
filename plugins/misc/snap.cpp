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

/* Snap: a pitch moved to the nearest note of a key and a scale.
 *
 * `in' is a frequency, as analysis::yin measures it, and `out' the
 * nearest note the scale holds, nearest on a log scale -- in cents, the
 * way an ear hears near -- with a tie going down. `ratio' is out over in,
 * which is what a pitch shifter is set to to put the one on the other:
 * pitch correction. `key' is the tonic, 0 C to 11 B, and A4 is 440 Hz.
 *
 * Where there is no pitch, `in' 0 or not a number, `out' is 0 and
 * `ratio' is 1, so a shifter fed it leaves breath and silence alone.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

enum { IN_ARG, IN_KEY, IN_SCALE, OUT_ARG, OUT_RATIO };

std::atomic<int> args[OUT_RATIO + 1];

static const char desc[] = "Snap (a pitch to a key and a scale)";
thPlugin::State    mystate = thPlugin::PASSIVE;

static const char *const keyNames[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/* The notes each scale holds above its tonic, as a bit a semitone. */
static const char *const scaleNames[] = {
    "Chromatic", "Major", "Minor", "Harmonic minor", "Dorian",
    "Mixolydian", "Major pentatonic", "Minor pentatonic", "Blues"
};

static const unsigned scaleMasks[] = {
    0xFFF,
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) |
        (1 << 11),
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) |
        (1 << 10),
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) |
        (1 << 11),
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) |
        (1 << 10),
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) |
        (1 << 10),
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),
};

#define SCALES ((int)(sizeof(scaleMasks) / sizeof(scaleMasks[0])))

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "A pitch, or 0 for none");
    plugin->setArgUnits(args[IN_ARG], "Hz");
    args[IN_KEY] = plugin->regArg("key", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_KEY], "The tonic");
    plugin->setArgValues(args[IN_KEY], keyNames, 12);
    args[IN_SCALE] = plugin->regArg("scale", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SCALE], "The notes allowed above the tonic");
    plugin->setArgValues(args[IN_SCALE], scaleNames, SCALES);
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The nearest note of the scale, or 0");
    plugin->setArgUnits(args[OUT_ARG], "Hz");
    args[OUT_RATIO] = plugin->regArg("ratio", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_RATIO],
                       "out over in, or 1 where there is no pitch");
    plugin->setArgUnits(args[OUT_RATIO], "ratio");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg = mod->getArg(node, args[IN_ARG]);
    thArg *in_key = mod->getArg(node, args[IN_KEY]);
    thArg *in_scale = mod->getArg(node, args[IN_SCALE]);
    const unsigned n = thOutLen(windowlen, in_arg, in_key, in_scale);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(n);
    float *ratio = mod->getArg(node, args[OUT_RATIO])->allocate(n);

    for (unsigned i = 0; i < n; i++)
    {
        const float f = (*in_arg)[i];

        if (!thIsFinite(f) || f <= 0)
        {
            out[i] = 0;
            ratio[i] = 1;
            continue;
        }

        const int key = ((int)thClampArg((*in_key)[i], 0, 11) % 12 + 12) % 12;
        const unsigned mask =
            scaleMasks[(int)thClampArg((*in_scale)[i], 0, SCALES - 1)];
        const double note = 69 + 12 * log2((double)f / 440);
        const int near = (int)floor(note + 0.5);
        int best = near;

        /* Outward from the nearest semitone, lower first on a tie; a
           scale holds a note within six semitones of anything. */
        for (int k = 0; k <= 6; k++)
        {
            const int down = near - k, up = near + k;
            const bool d = mask >> (((down - key) % 12 + 12) % 12) & 1;
            const bool u = mask >> (((up - key) % 12 + 12) % 12) & 1;

            if (d && (!u || note - down <= up - note))
            {
                best = down;
                break;
            }

            if (u)
            {
                best = up;
                break;
            }
        }

        out[i] = (float)(440 * exp2((best - 69) / 12.0));
        ratio[i] = out[i] / f;
    }

    return 0;
}
