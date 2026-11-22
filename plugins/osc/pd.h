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

#ifndef TH_KERNEL_PD_H
#define TH_KERNEL_PD_H 1

/* osc::pd's kernel (libthink/thKernel.h): one sample of a cosine read
 * through a bent phase, whether a cycle began on it, and how far past it
 * the wrap fell. See pd.cpp for the waves and why they are drawn so. */

#include <math.h>

#include "thKernel.h"

struct thkPd
{
    enum { SAW, SQUARE, PULSE, DOUBLE, SAWPULSE, RESOSAW, RESOTRI, RESOTRAP,
           WAVES };

    /* What the frequency, waveform, amount and resonance make of a sample. */
    struct Ctl
    {
        float dt, perDt;                /* the cycle's step, and 1 / it */
        int   wave;
        float d, perD, perRest;         /* the knee, 1 / it, 1 / (1 - it) */
        float d2, perD2;                /* sawpulse's second half's knee */
        float mean, norm;               /* the bent waves' DC, 1 / (1 + |it|) */
        float r, m;                     /* the resonant waves' ratio and DC */
    };

    /* The knee at `amount' a, from d0, where the wave is a cosine, toward
       the sharpest a pitch allows: the steep stretch turns c / d of a turn
       a cycle, and held to 0.225 of the rate a cosine that short still
       spreads to under Nyquist. 1 / d is what is interpolated, as the
       harmonics go as 1 / d. */
    static float knee (float d0, float c, float a, float dt)
    {
        float least = dt * c / 0.225f;

        least = least < 0.01f ? 0.01f : least > d0 ? d0 : least;
        return 1 / (1 / d0 + a * (1 / least - 1 / d0));
    }

    static Ctl ctl (float freq, float waveform, float amount, float reso,
                    float rate)
    {
        const float pi = 3.14159265358979f;
        const float a = thkClamp(amount, 0, 1);
        Ctl c;

        c.dt = thkBoundFreq(freq, rate) / rate;
        c.perDt = 1 / c.dt;
        /* A selector: the whole part picks, and anything else is the saw. */
        c.wave = thkFinite(waveform) && waveform >= 0 && waveform < WAVES
                     ? (int)waveform : SAW;
        c.d = c.d2 = 0.5f;
        c.mean = 0;
        c.r = 1;
        c.m = 0;

        switch (c.wave)
        {
        case SAW:
        case SQUARE:
            c.d = knee(0.5f, 0.5f, a, c.dt);
            break;
        case PULSE:
            c.d = knee(1, 1, a, c.dt);
            c.mean = 1 - c.d;
            break;
        case DOUBLE:
            /* Its knees are corners, at a quarter turn, which fold as well:
               held twice as long as its steep stretch alone would need. */
            c.d = knee(0.5f, 1, a, c.dt);
            c.mean = 2 * (2 * c.d - 1) / pi;
            break;
        case SAWPULSE:
            c.d = knee(0.5f, 1, a, c.dt);       /* as the double's */
            c.d2 = knee(1, 1, a, c.dt);
            c.mean = (2 * c.d - 1) / pi + 0.5f * (1 - c.d2);
            break;
        default:
        {
            /* The inner cosine's sidebands sit about r f: held to 0.2 of
               the rate, and the floor of 1 winning over it. */
            const float most = 0.2f / c.dt;
            const float k2 = 4 * pi * pi;

            c.r = 1 + a * (thkClamp(reso, 1, 16) - 1);
            c.r = c.r > most ? most : c.r;
            c.r = c.r < 1 ? 1 : c.r;

            /* The mean of w (1 - cos 2 pi r phi): the window's own, less
               its integral against the cosine, which is closed form. */
            const float cosK = thkSin2pi(c.r + 0.25f);
            const float cosHalf = thkSin2pi(0.5f * c.r + 0.25f);
            const float perK2 = 1 / (k2 * c.r * c.r);

            c.m = c.wave == RESOSAW ? 0.5f - (1 - cosK) * perK2
                : c.wave == RESOTRI ? 0.5f - 2 * (2 * cosHalf - cosK - 1) * perK2
                                    : 0.75f - 2 * (cosHalf - cosK) * perK2;
        }
        }
        c.perD = 1 / c.d;
        c.perRest = c.d < 1 ? 1 / (1 - c.d) : 0;  /* the pulse has no rest */
        c.perD2 = 1 / c.d2;
        c.norm = 1 / (1 + fabsf(c.mean));
        return c;
    }

    /* Half a turn fast over [0, d], the other half slowly over [d, 1]. */
    static float bend (float x, const Ctl &c)
    {
        return x < c.d ? 0.5f * x * c.perD : 0.5f + 0.5f * (x - c.d) * c.perRest;
    }

    /* The wave at `p' of its cycle, 0 to 1. */
    static float shape (float p, const Ctl &c)
    {
        float g;

        switch (c.wave)
        {
        case SAW:
            g = bend(p, c);
            break;
        case SQUARE:
            g = p < c.d ? 0.5f * p * c.perD
              : p < 0.5f ? 0.5f
              : p < 0.5f + c.d ? 0.5f + 0.5f * (p - 0.5f) * c.perD
                               : 1.0f;
            break;
        case PULSE:
            g = p < c.d ? p * c.perD : 1.0f;
            break;
        case DOUBLE:
            /* The second half the first's mirror in time, so the halves
               differ and the even harmonics are not cancelled. */
            g = p < 0.5f ? 0.5f * bend(2 * p, c)
                         : 1 - 0.5f * bend(2 - 2 * p, c);
            break;
        case SAWPULSE:
        {
            const float x = 2 * p - 1;

            g = p < 0.5f ? 0.5f * bend(2 * p, c)
              : x < c.d2 ? 0.5f + 0.5f * x * c.perD2
                         : 1.0f;
            break;
        }
        default:
        {
            const float w = c.wave == RESOSAW ? 1 - p
                          : c.wave == RESOTRI ? (p < 0.5f ? 2 * p : 2 - 2 * p)
                          : (p < 0.5f ? 1.0f : 2 - 2 * p);

            return 0.5f * (w * (1 - thkSin2pi(c.r * p + 0.25f)) - c.m);
        }
        }
        return (thkSin2pi(g + 0.25f) - c.mean) * c.norm;
    }

    static float stepBefore (float h, float d) { return h / 2 * d * d; }
    static float stepAfter (float h, float d) { return h / 2 * (2 * d - d * d - 1); }

    float phase = 0, owed = 0;
    bool  started = false;

    /* start: where the cycle starts, read on the first sample alone. */
    float tick (const Ctl &c, float start, float r, float &sync, float &edge)
    {
        if (!started)
        {
            phase = thkFinite(start) ? start - floorf(start) : 0;
            started = true;
        }

        float y = shape(phase, c) + owed;

        owed = 0;
        if (thkFinite(r) && r > 0)
        {
            /* A restart `until' past this sample cuts the cycle at `at'
               and jumps to its start: a step, given osc::blep's two-sample
               correction, half on this sample and half owed to the next.
               A natural wrap before it is continuous and wants none. */
            const float until = r < 1 ? r : 1;
            const float d = 1 - until;
            float at = phase + until * c.dt;

            at -= floorf(at);

            const float h = shape(0, c) - shape(at, c);

            y += stepBefore(h, d);
            owed = stepAfter(h, d);
            phase = d * c.dt;
            sync = 1;
            edge = until;
        }
        else
        {
            edge = phase + c.dt >= 1 ? (1 - phase) * c.perDt : 0;
            phase += c.dt;
            sync = 0;
            if (phase >= 1)
            {
                phase -= floorf(phase);
                sync = 1;
            }
        }
        return y;
    }
};

#endif /* TH_KERNEL_PD_H */
