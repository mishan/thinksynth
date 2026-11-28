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

/* A wavefolder: the West Coast's way to brighten a sound, where the East's
 * filters one down. A wave pushed past the top folds back down instead of
 * clipping, and every fold is more harmonics: `drive' is how many.
 *
 *     u = drive * (in + symmetry) * pi / 2,   out = sin(u)
 *
 * At drive 1 a full-scale sine is shaped, not folded; at 8 it is folded
 * four times each way, bright and hollow. `symmetry' moves the fold off
 * centre, which brings even harmonics and DC; a 5 Hz high pass takes the
 * DC out after. A note off centre starts with its DC and loses it over
 * about 30 ms: how much DC there is depends on how loud the input is, which
 * its first sample does not say (seeded from it, the step was larger).
 *
 * ANTIALIASED, by second-order ADAA: the output is the mean of sin over
 * the triangle the last three samples' u span -- what 2 F[u0, u1, u2], the
 * second divided difference of F = -sin, comes to -- which takes 10 to
 * 25 dB off what a fold aliases (first order takes 5 to 14), at the cost of
 * a sample of delay and a gentle top-end roll-off.
 *
 * Written two ways, so nothing near-equal is subtracted and divided by a
 * small step, which would turn sin's last bit into a different output
 * between builds. Around the middle sample, with a = u0 - u1, b = u2 - u1
 * and D = t0 a + t2 b over the triangle,
 *
 *     out = sin(u1) E[cos D] + cos(u1) E[sin D],
 *     E[D^k] = 2 (a^k + a^(k-1) b + ... + b^k) / ((k + 1)(k + 2))
 *
 * a series in the steps, where u0 and u2 are close; elsewhere the divided
 * differences themselves, each of them sin's difference as cos(mid)
 * sinc(half), divided by no less than 0.01. A drive moved while a note
 * plays is folded as it stands, so a sweep is continuous.
 */

#include <math.h>

#include "think.h"

#include "fold.h"

static const char desc[] = "Wavefolder (a sine fold, antialiased)";
thPlugin::State    mystate = thPlugin::ACTIVE;

enum { IN_ARG, IN_DRIVE, IN_SYMMETRY, OUT_ARG, INOUT_STATE };

std::atomic<int> args[INOUT_STATE + 1];

/* The cutoff of the high pass that takes the DC a fold off centre makes. */
#define FOLD_DC_HZ 5.0

void module_cleanup (thPlugin *plugin)
{
    (void)plugin;
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc(desc);
    plugin->setState(mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_DRIVE] = plugin->regArg("drive", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DRIVE],
                       "How far past the top it is pushed: 1 a gentle "
                       "shaping, 8 folded four times each way; 0 silence");
    plugin->setArgRange(args[IN_DRIVE], 0, 16);
    plugin->setArgDefault(args[IN_DRIVE], 1);
    args[IN_SYMMETRY] = plugin->regArg("symmetry", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SYMMETRY],
                       "Moves the fold off centre, for even harmonics");
    plugin->setArgRange(args[IN_SYMMETRY], -1, 1);
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The folded signal, its DC taken out");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    /* [0] the last u, [1] and [2] the high pass's last in and out, [3] 1
       once a sample has been folded, [4] the u before the last. */
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg = mod->getArg(node, args[IN_ARG]);
    thArg *in_drive = mod->getArg(node, args[IN_DRIVE]);
    thArg *in_symmetry = mod->getArg(node, args[IN_SYMMETRY]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);

    /* In float between samples as between windows, so a window boundary is
       not an event. */
    float last = (*inout_state)[0];
    float dcIn = (*inout_state)[1];
    float dcOut = (*inout_state)[2];
    bool started = (*inout_state)[3] > 0;
    float before = (*inout_state)[4];
    float *state = inout_state->allocate(5);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    const float keep = (float)(1 - 2 * M_PI * FOLD_DC_HZ / samples);

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float x = (*in_arg)[i] / TH_MAX;
        const float drive = (*in_drive)[i];
        const float symmetry = (*in_symmetry)[i];

        /* A sample that is not a number is skipped: nothing of it kept. */
        if (!thIsFinite(x) || !thIsFinite(drive) || !thIsFinite(symmetry))
        {
            out[i] = 0;
            continue;
        }

        const float u = (drive > 0 ? drive : 0) * (x + symmetry) *
                        (float)(M_PI / 2);

        if (!started)
            last = before = u;

        const float folded = (float)foldAdaa(u, last, before);

        started = true;

        dcOut = folded - dcIn + keep * dcOut;
        dcIn = folded;
        before = last;
        last = u;
        out[i] = TH_MAX * dcOut;
    }

    state[0] = last;
    state[1] = dcIn;
    state[2] = dcOut;
    state[3] = started ? 1 : 0;
    state[4] = before;

    return 0;
}
