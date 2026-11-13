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

#ifndef TH_KERNEL_STFT_H
#define TH_KERNEL_STFT_H 1

/* A running short-time Fourier transform, for the fft/ nodes that change a
 * sound's spectrum as it plays: one sample in and one out, and every hop
 * of a quarter frame the last frame's spectrum handed to the node to
 * change, then overlap-added back.
 *
 * The output is the frames' sum for the sample a whole frame ago: every
 * frame that overlaps a sample has been added by the time it comes out.
 * A node that changes nothing therefore gives back its input exactly a
 * frame late, to float error -- a periodic Hann window, applied going in
 * and coming out, sums squared to 3/2 at a hop of a quarter frame, hence
 * the 2/3. The input that frame ago is the ring's oldest sample, read
 * before it is overwritten, so a node's dry signal delayed to match costs
 * nothing.
 *
 * The ring and the accumulator are the caller's, a frame long each, in a
 * plugin's state; this keeps only pointers and positions. The caller
 * answers for what it hands over: n a size thkStftSize gives, pos in [0,
 * n), hop in [0, n/4), and a finite input -- a state restored out of
 * range reads off the accumulator, and a NaN let in comes back as the dry
 * signal a frame later and spoils every frame it is in. */

#include <string.h>

#include "thKernel.h"
#include "fft.h"

/* `size' to the power of two nearest it in octaves, 256 to THK_FFT_MAX;
   0 or not a number is 2048. */
static inline int thkStftSize (float size)
{
    if (!thkFinite(size) || size == 0)
        return 2048;

    int n = 256;

    while (n < THK_FFT_MAX && size > (float)n * 1.41421356f)
        n *= 2;
    return n;
}

/* The periodic Hann window at j of n, from the FFT's own table. */
static inline float thkStftHann (const thkFftTable &t, int j, int n)
{
    const int at = (j <= n / 2 ? j : n - j) * (THK_FFT_MAX / n);

    return 0.5f - 0.5f * t.c[at];
}

struct thkStft
{
    float *ring, *acc;          /* n each */
    int n, pos, hop;            /* the frame; where the ring writes next; samples into the hop */

    /* One sample: `dry' is the input a frame ago, the return the
       spectrum's sound for the same moment. `process(re, im, n)' changes
       a frame's n/2 + 1 bins in place. */
    template <class F>
    float tick (float in, float &dry, F process)
    {
        const int h = n / 4;

        dry = ring[pos];
        ring[pos] = in;
        pos = (pos + 1) & (n - 1);

        const float out = acc[hop];

        if (++hop == h)
        {
            const thkFftTable &t = thkFftTwiddles();
            float x[THK_FFT_MAX], re[THK_FFT_MAX / 2 + 1], im[THK_FFT_MAX / 2 + 1];

            for (int j = 0; j < n; j++)
                x[j] = thkStftHann(t, j, n) * ring[(pos + j) & (n - 1)];
            thkFftReal(x, re, im, n);
            process(re, im, n);
            thkFftRealInverse(re, im, x, n);

            memmove(acc, acc + h, (n - h) * sizeof(float));
            memset(acc + n - h, 0, h * sizeof(float));
            for (int j = 0; j < n; j++)
                acc[j] += (2.0f / 3) * thkStftHann(t, j, n) * x[j];
            hop = 0;
        }
        return out;
    }
};

#endif /* TH_KERNEL_STFT_H */
