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

#ifndef THINK_WAVETABLE_H
#define THINK_WAVETABLE_H

/*
 * A wavetable, band-limited: what osc::wavetable plays.
 *
 * A TABLE IS FRAMES, single cycles of one length laid end to end, read from
 * a wav as recorded (osc/sampleslot.h, rate 0). Their length is what the
 * file's chunks say (`clm ', `srge', what wavetable editors write); with
 * none, a file of 4096 samples or fewer is one cycle, and a longer one is
 * cycles of 2048.
 *
 * EACH FRAME IS KEPT AT HALF-OCTAVE LEVELS. Level k holds the frame's
 * harmonics 1 to H_k = floor(H * 2^(-k/2)), H the most the frame can say
 * (half its length, less one, and 511 at the most), and DC. A note plays
 * the levels whose harmonics all fit under Nyquist at its pitch, so it
 * never aliases, and half an octave apart they cost little of its top.
 * Each level is stored at eight samples a cycle of its own top harmonic, a
 * power of two and at least 64, so a 4-point Hermite read between them
 * puts its images 80 dB under the note: a frame of 2048 at every level is
 * eight of its own lengths.
 *
 * Built from each frame's spectrum: an FFT when the length is a power of
 * two, a DFT otherwise (a 600-sample single cycle), kept to tables whose
 * frames x length^2 stays under 64M.
 */

#include <math.h>
#include <stdint.h>

#include <algorithm>
#include <string>
#include <vector>

#include "realfft.h"

#define THINK_WT_FRAMES_MAX 256
#define THINK_WT_SIZE_MIN 16
#define THINK_WT_SIZE_MAX 4096

/* Samples a cycle of a level's top harmonic, and the fewest a level has:
   what puts the 4-point read's images 80 dB under a note's fundamental
   everywhere from 100 Hz to 10 kHz (scripts/wavetablecheck measures it);
   at 4 they were 40 dB at the top of the keyboard. */
#define THINK_WT_OVERSAMPLE 8
#define THINK_WT_LEN_MIN 64

/* The most harmonics a level keeps: past it a frame's higher ones are
   for notes under 43 Hz, above 15 kHz, and would double the storage. */
#define THINK_WT_HARMONICS_MAX 511

struct thWaveLevel
{
    unsigned harmonics;         /* H_k */
    unsigned len;               /* samples a frame, a power of two */
    std::vector<float> data;    /* frames x len */
};

struct thWaveTable
{
    unsigned size = 0;          /* a frame's length as read */
    unsigned frames = 0;
    std::vector<thWaveLevel> levels;
};

/* The twiddles for transforms up to 2^14 points, the longest a level is. */
struct thWaveTwiddles
{
    std::vector<double> wr, wi;
};

#define THINK_WT_FFT_BITS 14

static inline const thWaveTwiddles &thWaveTwiddleTable (void)
{
    static const thWaveTwiddles t = []
    {
        thWaveTwiddles made;
        const size_t n = (size_t)1 << THINK_WT_FFT_BITS;

        made.wr.resize(n / 2);
        made.wi.resize(n / 2);

        for (size_t j = 0; j < n / 2; j++)
        {
            made.wr[j] = cos(M_PI * (double)j / (double)(n / 2));
            made.wi[j] = -sin(M_PI * (double)j / (double)(n / 2));
        }

        return made;
    }();

    return t;
}

/* log2 of a power of two. */
static inline int thWaveBits (size_t n)
{
    int m = 0;

    while (((size_t)1 << m) < n)
        m++;

    return m;
}

/* H_k: the frame's top harmonic H at k half-octaves down. */
static inline unsigned thWaveHarmonics (unsigned top, unsigned k)
{
    const double down = (double)(1u << (k / 2));

    return (unsigned)floor((k % 2 ? top * 0.70710678118654752 : (double)top) /
                           down);
}

/* `x', `n' samples as recorded, into `out' as a table whose frames are
   `cycle' long, 0 for what its length says. False, with `why', for a
   table this does not play. */
static inline bool thWaveTableBuild (const float *x, size_t n, unsigned cycle,
                                     thWaveTable &out, std::string &why)
{
    out = thWaveTable();

    const size_t size = cycle > 0 ? cycle
                      : n <= THINK_WT_SIZE_MAX ? n : 2048;

    if (size < THINK_WT_SIZE_MIN || size > THINK_WT_SIZE_MAX)
    {
        why = "has frames of " + std::to_string(size) + " samples, outside " +
              std::to_string(THINK_WT_SIZE_MIN) + " to " +
              std::to_string(THINK_WT_SIZE_MAX);
        return false;
    }

    size_t frames = n / size;

    if (frames == 0)
    {
        why = "is shorter than one frame";
        return false;
    }

    if (frames > THINK_WT_FRAMES_MAX)
        frames = THINK_WT_FRAMES_MAX;

    const bool pow2 = (size & (size - 1)) == 0;

    if (!pow2 && (double)frames * size * size > 64e6)
    {
        why = "has frames of " + std::to_string(size) + " samples, not a "
              "power of two, too many of them to analyse";
        return false;
    }

    const unsigned top = std::min<unsigned>((unsigned)((size - 1) / 2),
                                            THINK_WT_HARMONICS_MAX);

    out.size = (unsigned)size;
    out.frames = (unsigned)frames;

    for (unsigned k = 0; thWaveHarmonics(top, k) >= 1; k++)
    {
        thWaveLevel level;

        level.harmonics = thWaveHarmonics(top, k);
        level.len = THINK_WT_LEN_MIN;

        while (level.len < THINK_WT_OVERSAMPLE * level.harmonics)
            level.len <<= 1;

        level.data.resize(frames * level.len);
        out.levels.push_back(std::move(level));
    }

    const thWaveTwiddles &tw = thWaveTwiddleTable();
    const size_t most = (size_t)1 << THINK_WT_FFT_BITS;
    std::vector<double> sr(top + 1), si(top + 1), re, im;

    for (size_t f = 0; f < frames; f++)
    {
        const float *frame = x + f * size;

        /* The frame's harmonics 0 .. top, as X_h = sum x[t] e^(-2 pi i h t
           / size). */
        if (pow2)
        {
            re.assign(frame, frame + size);
            im.assign(size, 0.0);
            thRealFft(re, im, thWaveBits(size), tw.wr, tw.wi, most / size);

            for (unsigned h = 0; h <= top; h++)
            {
                sr[h] = re[h];
                si[h] = im[h];
            }
        }
        else
            for (unsigned h = 0; h <= top; h++)
            {
                double a = 0, b = 0;

                for (size_t t = 0; t < size; t++)
                {
                    /* h t reduced first, so the angle stays small. */
                    const double angle = 2 * M_PI *
                        (double)((h * t) % size) / (double)size;

                    a += frame[t] * cos(angle);
                    b -= frame[t] * sin(angle);
                }

                sr[h] = a;
                si[h] = b;
            }

        /* Each level: its harmonics at `len' points, by the forward
           transform of their conjugate, so y = Re / size. */
        for (thWaveLevel &level : out.levels)
        {
            const size_t len = level.len;

            re.assign(len, 0.0);
            im.assign(len, 0.0);
            re[0] = sr[0];

            for (unsigned h = 1; h <= level.harmonics; h++)
            {
                re[h] = sr[h];
                im[h] = -si[h];
                re[len - h] = sr[h];
                im[len - h] = si[h];
            }

            thRealFft(re, im, thWaveBits(len), tw.wr, tw.wi, most / len);

            for (size_t s = 0; s < len; s++)
                level.data[f * len + s] = (float)(re[s] / (double)size);
        }
    }

    return true;
}

/* A frame of a level at `phase' (0..1): a 4-point Hermite between its
   samples, wrapping. */
static inline float thWaveRead (const thWaveLevel &level, unsigned frame,
                                double phase)
{
    const unsigned mask = level.len - 1;
    const float *d = level.data.data() + (size_t)frame * level.len;
    const double at = phase * level.len;
    const long i = (long)floor(at);
    const float t = (float)(at - (double)i);
    const float y0 = d[(i - 1) & mask], y1 = d[i & mask];
    const float y2 = d[(i + 1) & mask], y3 = d[(i + 2) & mask];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2 * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    return ((c3 * t + c2) * t + c1) * t + y1;
}

#endif /* THINK_WAVETABLE_H */
