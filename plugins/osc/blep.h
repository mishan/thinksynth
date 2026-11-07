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

#ifndef TH_KERNEL_BLEP_H
#define TH_KERNEL_BLEP_H 1

/* osc::blep's kernel (libthink/thKernel.h): one sample of the band-limited
 * saw, pulse or triangle, whether a cycle began on it, and how far past it
 * the wrap fell. See blep.cpp for the corrections and the hard sync. */

#include <math.h>

#include "thKernel.h"

struct thkBlep
{
    /* What the frequency, waveform and pulse width make of a sample. */
    struct Ctl
    {
        float dt, perDt;                /* the cycle's step, and 1 / it */
        int   wave;                     /* 0 saw, 1 pulse, 2 triangle */
        float pw;                       /* held a step from either end */
        float pwFirst;                  /* as the first sample takes it */
    };

    static Ctl ctl (float freq, float waveform, float pw, float rate)
    {
        Ctl c;

        c.dt = thkBoundFreq(freq, rate) / rate;
        c.perDt = 1 / c.dt;
        /* A selector: the whole part picks, and anything else is the saw. */
        c.wave = thkFinite(waveform) && waveform >= 0 && waveform < 3 ? (int)waveform : 0;
        c.pwFirst = !thkFinite(pw) || pw <= 0 || pw >= 1 ? 0.5f : pw;
        pw = !thkFinite(pw) || pw <= 0 ? 0.5f : pw;
        /* Closer to an end than a step and the corrections overlap. */
        c.pw = pw < c.dt ? c.dt : pw > 1 - c.dt ? 1 - c.dt : pw;
        return c;
    }

    float phase = 0, tri = 0, owed = 0;
    bool  started = false, owing = false;

    /* The correction for a step at 0 of the cycle, nonzero only within a
       sample of it. */
    static float polyBlep (float t, const Ctl &c)
    {
        if (t < c.dt)
        {
            const float x = t * c.perDt;

            return x + x - x * x - 1;
        }
        if (t > 1 - c.dt)
        {
            const float x = (t - 1) * c.perDt;

            return x * x + x + x + 1;
        }
        return 0;
    }

    static float stepBefore (float h, float d) { return h / 2 * d * d; }
    static float stepAfter (float h, float d) { return h / 2 * (2 * d - d * d - 1); }

    static float triAt (float p, float pw)
    {
        return p < pw ? -pw * (1 - pw) + 2 * (1 - pw) * p
                      : pw * (1 - pw) - 2 * pw * (p - pw);
    }

    /* start: where the cycle starts, read on the first sample alone. */
    float tick (const Ctl &c, float start, float r, float &sync, float &edge)
    {
        const float dt = c.dt, pw = c.pw;
        const bool reset = thkFinite(r) && r > 0;
        const bool pulse = c.wave != 0;
        float y, jump = 0;

        if (!started)
        {
            phase = thkFinite(start) ? start - floorf(start) : 0;
            tri = triAt(phase, c.pwFirst);
            started = true;
        }

        if (!reset && !owing)
        {
            if (!pulse)
                y = 2 * phase - 1 - polyBlep(phase, c);
            else
            {
                float t2 = phase + 1 - pw;

                t2 -= floorf(t2);
                y = (phase < pw ? 1.0f : -1.0f) + polyBlep(phase, c) - polyBlep(t2, c);
            }
        }
        else
        {
            float t2 = phase + 1 - pw;
            float next = 0;

            t2 -= floorf(t2);
            y = pulse ? (phase < pw ? 1.0f : -1.0f) : 2 * phase - 1;
            if (owing)
                y += owed;
            else if (pulse)
                y += (phase < dt ? polyBlep(phase, c) : 0) - (t2 < dt ? polyBlep(t2, c) : 0);
            else
                y -= phase < dt ? polyBlep(phase, c) : 0;

            if (!reset)
            {
                if (pulse)
                    y += (phase > 1 - dt ? polyBlep(phase, c) : 0) -
                         (t2 > 1 - dt ? polyBlep(t2, c) : 0);
                else
                    y -= phase > 1 - dt ? polyBlep(phase, c) : 0;
            }
            else
            {
                const float until = r < 1 ? r : 1;
                const float d = 1 - until;
                float at = phase + until * dt;

                if (pulse && phase < pw && at >= pw)
                {
                    const float dw = 1 - (pw - phase) * c.perDt;

                    y += stepBefore(-2, dw);
                    next += stepAfter(-2, dw);
                }
                if (at >= 1)
                {
                    const float dw = 1 - (1 - phase) * c.perDt;
                    const float h = pulse ? 2.0f : -2.0f;

                    y += stepBefore(h, dw);
                    next += stepAfter(h, dw);
                    at -= 1;
                }

                const float h = pulse ? 1 - (at < pw ? 1.0f : -1.0f) : -2 * at;

                jump = triAt(0, pw) - triAt(at, pw);
                y += stepBefore(h, d);
                next += stepAfter(h, d);
            }
            owed = next;
            owing = reset;
        }

        if (c.wave == 2)
        {
            /* The restart's jump on whichever side of this sample it is
               nearer; then the pulse integrated, leaking slowly. */
            if (reset && r < 0.5f)
            {
                tri += jump;
                jump = 0;
            }
            tri = dt * (y - (2 * pw - 1)) + (1 - dt / 64) * tri;
            y = tri / (pw * (1 - pw));
            tri += jump;
        }

        if (reset)
        {
            const float until = r < 1 ? r : 1;

            phase = (1 - until) * dt;
            sync = 1;
            edge = until;
        }
        else
        {
            edge = phase + dt >= 1 ? (1 - phase) * c.perDt : 0;
            phase += dt;
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

#endif /* TH_KERNEL_BLEP_H */
