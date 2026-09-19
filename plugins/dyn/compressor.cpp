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

/* A compressor: a threshold, a ratio, and an attack and a release.
 *
 * The sound of the decade this was written for, and the one thing the tree
 * had no node for. There is a limiter -- `fx/limiter.dsp', a peak follower
 * into a gain, on the mix -- and it is this node's special case: a ratio of
 * infinity, no attack to speak of, and a threshold it is not allowed past.
 * What a limiter cannot do is the part that is musical. An eighties snare is
 * not a snare that was stopped from clipping; it is a snare whose first four
 * milliseconds got through at full size and whose body was then held down for
 * a hundred, and that shape is an attack, a release and a ratio.
 *
 * The arithmetic is the textbook feed-forward compressor, in decibels
 * throughout:
 *
 *     xL = 20*log10(|key|)                     the level, in dB from full
 *     cG = the gain computer's answer for xL   how much to turn down, <= 0
 *     yL = yL + (cG - yL) * (attack or release coefficient)
 *     out = in * 10^((yL + makeup)/20)
 *
 * SMOOTHED IN THE GAIN, NOT IN THE LEVEL, which is what makes `attack' mean
 * what a person setting it expects. The alternative -- a peak follower on the
 * level, then the gain computer -- has the same two knobs and a different
 * meaning for both: the time constant is the level's, and the gain reaches
 * 63% of its step at some other moment that depends on how far over the
 * threshold the signal went. Smoothing the gain makes `attack' exactly the
 * 63% time of the gain reduction, whatever the level did, which is the number
 * on the front panel of every compressor anybody has ever used. statecheck
 * measures it as such.
 *
 * The price is ripple. A sine's |x| falls to zero twice a cycle, so the
 * instantaneous target falls with it and the release drags the gain back up a
 * little between peaks -- a real analogue compressor does the same thing, it
 * is why "release" and "pumping" are the same knob, and it is inaudible above
 * a few hundred hertz. A release shorter than a couple of cycles of the
 * lowest note is the setting that makes it audible, and that is a sound
 * people reach for on purpose.
 *
 * THE KEY IS `side' WHERE ONE IS WIRED AND `in' WHERE ONE IS NOT. That is
 * the sidechain: `side = ionode->side0' on a channel effect, with the piece
 * naming the kick's channel, is the pump under every dance record since 1982
 * -- and it is the thing `gen::pump' was drawing by hand, a step at a time,
 * from the composer, where it can only know about the beats it wrote rather
 * than about the kick.
 *
 * "Wired" is a question about the graph and not about the samples, so it is
 * asked of the arg rather than of its contents: an arg that points at another
 * node is a key, an arg holding a number is not. Reading it as "the side is
 * not all zeros" would be worse than useless -- it would make a key that
 * happens to be silent for a window into no key at all, which is exactly the
 * window where a sidechain is supposed to be letting go.
 *
 * `gain' is the reduction, in dB, for a meter to draw -- the smoothed yL
 * above, without the makeup, because what a meter says is how hard the
 * compressor is working and the makeup is not work. It is never positive:
 * the gain computer only ever turns things down, which is also why `ratio'
 * has a floor of 1 rather than becoming an expander underneath it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

static const char desc[] = "Compressor with a threshold, ratio, attack and release";
thPlugin::State    mystate = thPlugin::ACTIVE;

enum { OUT_ARG, OUT_GAIN, INOUT_STATE, IN_ARG, IN_SIDE, IN_THRESHOLD,
       IN_RATIO, IN_ATTACK, IN_RELEASE, IN_KNEE, IN_MAKEUP };

int args[IN_MAKEUP + 1];

/* log10 and its inverse, as the multiplies everything here actually uses.
   20*log10(x) is 8.6858896*ln(x), and 10^(x/20) is exp(0.11512925*x) -- the
   constant decibel.cpp has used for twenty years. */
#define COMP_TO_DB      8.6858896f
#define COMP_FROM_DB    0.11512925f

/* Where the level detector stops caring. -140 dB is below the floor of the
   sixteen-bit world this plugin's numbers are written for, and it is what
   stops log(0) from being anybody's problem. */
#define COMP_FLOOR      1e-7f

/* What the sliders offer. Neither is a law -- the threshold is arithmetic all
   the way down and a ratio of 200 is a limiter -- but a compressor whose
   threshold slider ran to -300 dB would be a slider nobody could use. */
#define COMP_THRESH_MIN -60.0f
#define COMP_RATIO_MAX  20.0f

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The input, turned down where it was "
                       "over the threshold");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    args[OUT_GAIN] = plugin->regArg("gain", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_GAIN],
                       "How far it is turning the signal down right now, in "
                       "dB and never above zero -- a meter, or a control");
    plugin->setArgRange(args[OUT_GAIN], COMP_THRESH_MIN, 0);
    plugin->setArgUnits(args[OUT_GAIN], "dB");

    /* [0] the smoothed reduction in dB, [1] whether [0] means anything yet.
       Zero is a perfectly good reduction -- it is the one a compressor sits
       at -- so there is no sentinel available in [0] itself. See misc::slew,
       which keeps its state the same way and for the same reason. */
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in; this is what is turned down");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");

    args[IN_SIDE] = plugin->regArg("side", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SIDE],
                       "The key: what the level is measured from. Wire "
                       "another signal here for a sidechain; leave it alone "
                       "and `in' keys itself");
    plugin->setArgRange(args[IN_SIDE], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_SIDE], "full scale");

    args[IN_THRESHOLD] = plugin->regArg("threshold", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_THRESHOLD],
                       "The level above which the key is turned down, in dB "
                       "from full scale");
    plugin->setArgRange(args[IN_THRESHOLD], COMP_THRESH_MIN, 0);
    plugin->setArgUnits(args[IN_THRESHOLD], "dB");
    plugin->setArgDefault(args[IN_THRESHOLD], -20);

    args[IN_RATIO] = plugin->regArg("ratio", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RATIO],
                       "How much of each dB over the threshold gets through: "
                       "4 means 4 dB in for 1 dB out. Under 1 reads as 1");
    plugin->setArgRange(args[IN_RATIO], 1, COMP_RATIO_MAX);
    plugin->setArgUnits(args[IN_RATIO], "ratio");
    plugin->setArgDefault(args[IN_RATIO], 4);

    args[IN_ATTACK] = plugin->regArg("attack", thPlugin::ARG_IN);
    /* Samples, so a .dsp writes `10 ms' and the unit fold turns it into the
       rate's worth -- misc::slew's rule, and the same arithmetic underneath. */
    plugin->setArgDesc(args[IN_ATTACK],
                       "How long the reduction takes to cover 63% of its way "
                       "down. Under one sample is instantaneous");
    plugin->setArgUnits(args[IN_ATTACK], "samples");

    args[IN_RELEASE] = plugin->regArg("release", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RELEASE],
                       "And 63% of its way back up, once the key has fallen "
                       "under the threshold again");
    plugin->setArgUnits(args[IN_RELEASE], "samples");

    args[IN_KNEE] = plugin->regArg("knee", thPlugin::ARG_IN);
    /* The soft knee: a parabola joining "not compressing" to "compressing",
       spanning `knee' dB centered on the threshold, with the slope matched at
       both ends. 0 is the corner, which is what a drum wants; a voice or a
       bus wants a few dB, because the corner is audible as the moment the
       compressor starts working. */
    plugin->setArgDesc(args[IN_KNEE],
                       "How many dB either side of the threshold the corner "
                       "is rounded over: 0 is a corner");
    plugin->setArgRange(args[IN_KNEE], 0, 24);
    plugin->setArgUnits(args[IN_KNEE], "dB");

    args[IN_MAKEUP] = plugin->regArg("makeup", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_MAKEUP],
                       "Added to the output afterwards, to put back what the "
                       "compression took off");
    plugin->setArgRange(args[IN_MAKEUP], 0, 24);
    plugin->setArgUnits(args[IN_MAKEUP], "dB");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out, *gain, *out_state;
    thArg *in_arg, *in_side, *in_threshold, *in_ratio, *in_attack;
    thArg *in_release, *in_knee, *in_makeup;
    thArg *out_arg, *gain_arg, *inout_state;
    thArg *raw_side;
    unsigned int i;
    float reduction;
    bool primed, keyed;

    /* The two coefficients, and the times they were worked out from. Both
       are buffers like every other arg and may move every sample, and both
       usually do not, so exp() is called when a number changes rather than
       twice a sample. A NaN never compares equal to itself, which sends it
       down the recompute path, where the floor below answers it. */
    float lastAttack = 0, lastRelease = 0;
    float ka = 1, kr = 1;
    bool haveKa = false, haveKr = false;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    gain_arg = mod->getArg(node, args[OUT_GAIN]);
    inout_state = mod->getArg(node, args[INOUT_STATE]);

    reduction = (*inout_state)[0];
    primed = (*inout_state)[1] != 0;

    out_state = inout_state->allocate(2);

    out = out_arg->allocate(windowlen);
    gain = gain_arg->allocate(windowlen);

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_side = mod->getArg(node, args[IN_SIDE]);
    in_threshold = mod->getArg(node, args[IN_THRESHOLD]);
    in_ratio = mod->getArg(node, args[IN_RATIO]);
    in_attack = mod->getArg(node, args[IN_ATTACK]);
    in_release = mod->getArg(node, args[IN_RELEASE]);
    in_knee = mod->getArg(node, args[IN_KNEE]);
    in_makeup = mod->getArg(node, args[IN_MAKEUP]);

    /* The graph's own arg rather than what it resolves to: a `side' that
       points at another node is a key, and a `side' holding a number is a
       `side' nobody wired. See the head. */
    raw_side = node->getArg(args[IN_SIDE]);
    keyed = raw_side != NULL && raw_side->type() == thArg::ARG_POINTER;

    for (i = 0; i < windowlen; i++)
    {
        const float x = (*in_arg)[i];
        const float key = keyed ? (*in_side)[i] : x;
        const float threshold = (*in_threshold)[i];
        const float knee = (*in_knee)[i] > 0 ? (*in_knee)[i] : 0;
        const float ratio = (*in_ratio)[i] > 1 ? (*in_ratio)[i] : 1;
        const float attack = (*in_attack)[i];
        const float release = (*in_release)[i];

        float level = fabsf(key);
        float over, target;

        if (!thIsFinite(level) || level < COMP_FLOOR)
            level = COMP_FLOOR;

        /* How far over the threshold the key is, in dB. */
        over = COMP_TO_DB * logf(level) - threshold;

        /* The gain computer. Below the knee nothing happens; above it the
           slope is 1/ratio, so what is kept of each dB over is `over/ratio'
           and what is taken off is the rest. Inside the knee the same
           quantity, weighted by how far into the corner we are -- which is
           the parabola whose slope matches 0 at one end and 1/ratio - 1 at
           the other. */
        if (knee > 0 && 2 * over > -knee && 2 * over < knee)
        {
            const float into = over + knee * 0.5f;

            target = (1.0f / ratio - 1.0f) * into * into / (2.0f * knee);
        }
        else if (2 * over >= knee)
            target = (1.0f / ratio - 1.0f) * over;
        else
            target = 0;

        /* Down fast, up slowly -- or however the two knobs say. Under one
           sample is no smoothing at all, which is where a coefficient of 1
           comes from; above it, exp(-1/t) is in (0, 1) and so is 1 - it,
           which is the stability condition for the whole plugin. */
        if (!haveKa || attack != lastAttack)
        {
            ka = (attack >= 1.0f)
                 ? (float)(1.0 - exp(-1.0 / (double)attack)) : 1.0f;
            lastAttack = attack;
            haveKa = true;
        }

        if (!haveKr || release != lastRelease)
        {
            kr = (release >= 1.0f)
                 ? (float)(1.0 - exp(-1.0 / (double)release)) : 1.0f;
            lastRelease = release;
            haveKr = true;
        }

        /* The first sample of this voice, and the restart after a bad one:
           both are a compressor that has not done anything yet, which is a
           reduction of nothing. Without this one non-finite sample would be
           read back for ever, since here the state is the gain. */
        if (!primed || !thIsFinite(reduction))
        {
            reduction = 0;
            primed = true;
        }

        if (target < reduction)
            reduction += (target - reduction) * ka;
        else
            reduction += (target - reduction) * kr;

        gain[i] = reduction;
        out[i] = x * expf(COMP_FROM_DB * (reduction + (*in_makeup)[i]));
    }

    out_state[0] = reduction;
    out_state[1] = 1;

    return 0;
}
