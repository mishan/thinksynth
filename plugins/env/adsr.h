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

#ifndef TH_KERNEL_ADSR_H
#define TH_KERNEL_ADSR_H 1

/* env::adsr's kernels (libthink/thKernel.h): one sample of the envelope,
 * and whether the note still sounds. Times are samples; levels full
 * scale. Two, as the plugin has two: free-running, for a trigger that is a
 * constant, and gated, for one that moves. See adsr.cpp. */

#include <math.h>

#include "thKernel.h"

#define THK_E 2.71828182845904523536f

/* Both kernels' times and levels, and a sample's share of each segment:
   worked out once while they hold still, since a division is the slowest
   thing a small FPU does. */
struct thkAdsrCtl
{
    float a, d, s, r, peak;
    float perA, perD, perR;     /* 1 / a, 1 / d, 1 / r, or 0 for none */
};

static inline thkAdsrCtl thkAdsr (float a, float d, float s, float r, float p)
{
    return { a, d, s, r, p == 0 ? 1 : p, a > 0 ? 1 / a : 0, d > 0 ? 1 / d : 0,
             r > 0 ? 1 / r : 0 };
}

/* The envelope as it always ran: attack, decay, sustain while the trigger
   is up, and the release once it is down; a trigger coming up again
   starts it over. */
struct thkAdsrFree
{
    enum { ATTACK, DECAY, SUSTAIN, RELEASE, DONE };

    float position = 0;
    int   phase = ATTACK;

    float tick (const thkAdsrCtl &c, float trigger, float reset, float &play)
    {
        const float a = c.a, d = c.d, s = c.s, r = c.r, peak = c.peak;
        float out = 0;

        if (reset > 0)
        {
            position = 0;
            phase = a == 0 ? DECAY : ATTACK;
        }
        /* A segment of no samples completes at once rather than dividing
           by its own length. */
        if (phase == ATTACK && a <= 0)
        {
            phase = DECAY;
            position = 0;
        }
        if (phase == DECAY && d <= 0)
        {
            phase = SUSTAIN;
            position = 0;
        }

        play = 1;
        switch (phase)
        {
        case ATTACK:
            out = position++ * c.perA * peak;
            if (position >= a)
            {
                phase = DECAY;
                position = 0;
            }
            break;
        case DECAY:
            out = s + logf(THK_E - (THK_E - 1) * (position++ * c.perD)) * (peak - s);
            if (position >= d)
            {
                phase = SUSTAIN;
                position = 0;
            }
            break;
        case SUSTAIN:
            /* A sustain of 0 is the end of a one-shot. */
            if (s == 0)
            {
                play = 0;
                phase = DONE;
            }
            out = s;
            if (trigger <= 0)
                phase = RELEASE;
            break;
        case RELEASE:
            if (r <= 0 || position >= r)
            {
                play = 0;
                phase = DONE;
            }
            else
                out = (1 - logf(1 + (THK_E - 1) * (position++ * c.perR))) * s;
            if (trigger > 0)
            {
                position = 0;
                phase = ATTACK;
            }
            break;
        default:
            play = 0;
            if (trigger > 0)
            {
                position = 0;
                phase = ATTACK;
            }
            break;
        }
        return out;
    }
};

/* On a trigger that moves: waiting for the first rise, each rise an
   attack from wherever the level is, each fall a release from there. */
struct thkAdsrGated
{
    enum { ATTACK, DECAY, SUSTAIN, RELEASE, DONE, WAITING };

    float position = 0;
    int   phase = WAITING;
    bool  was = false;          /* the trigger, a sample ago */
    float level = 0;
    float from = 0;             /* where the attack or release started */

    float tick (const thkAdsrCtl &c, float trigger, float reset, float &play)
    {
        const float a = c.a, d = c.d, s = c.s, r = c.r, peak = c.peak;
        const bool up = trigger > 0;
        /* A voice's first rise, and `reset', start the attack on this
           sample; that rise is then spent. */
        const bool first = up && !was && phase == WAITING;

        if (first || reset > 0)
        {
            phase = ATTACK;
            position = 0;
            from = level;
        }
        if (phase == ATTACK && a <= 0)
        {
            phase = DECAY;
            position = 0;
        }
        if (phase == DECAY && d <= 0)
        {
            phase = SUSTAIN;
            position = 0;
        }

        switch (phase)
        {
        case ATTACK:
            level = from + (peak - from) * (position++ * c.perA);
            if (position >= a)
            {
                phase = DECAY;
                position = 0;
            }
            break;
        case DECAY:
            level = s + logf(THK_E - (THK_E - 1) * (position++ * c.perD)) * (peak - s);
            if (position >= d)
            {
                phase = SUSTAIN;
                position = 0;
            }
            break;
        case SUSTAIN:
            level = s;
            if (s == 0)
                phase = DONE;
            break;
        case RELEASE:
            if (r <= 0 || position >= r)
            {
                level = 0;
                phase = DONE;
            }
            else
                level = (1 - logf(1 + (THK_E - 1) * (position++ * c.perR))) * from;
            break;
        default:
            level = 0;
            break;
        }

        /* Not sounding while it waits: a gate that never opens must not
           hold its voice for ever. */
        play = phase == DONE || phase == WAITING ? 0 : 1;

        /* Every other edge from the next sample, as the free-running
           envelope's release begins. */
        if (up && !was && !first)
        {
            phase = ATTACK;
            position = 0;
            from = level;
        }
        else if (!up && was && phase <= SUSTAIN)
        {
            phase = RELEASE;
            position = 0;
            from = level;
        }
        was = up;
        return level;
    }
};

#endif /* TH_KERNEL_ADSR_H */
