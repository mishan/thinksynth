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

#ifndef THINK_REALFFT_H
#define THINK_REALFFT_H

/*
 * The radix-2 decimation-in-frequency FFT osc::pad builds its tables with,
 * and osc::wavetable its levels: Embree and Kimble's (plugins/fft/dsp.c),
 * the twiddle factors the caller's.
 *
 * `wr' and `wi' are cos and -sin of pi j / (M / 2) for a transform of M
 * points, j < M / 2; a transform of n = 2^m <= M points reads every
 * (M / n)-th, `stride'. A forward transform, in place, natural order out.
 *
 * Only <vector> and <utility>, which every plugin here already has: the
 * browser build compiles each plugin inside a namespace of its own, and a
 * standard header first met inside one does not survive it.
 */

#include <stddef.h>

#include <utility>
#include <vector>

static inline void thRealFft (std::vector<double> &re, std::vector<double> &im,
                              int m, const std::vector<double> &wr,
                              const std::vector<double> &wi, size_t stride)
{
    const size_t n = (size_t)1 << m;

    size_t le = n, windex = stride;

    for (int l = 0; l < m; l++)
    {
        const size_t increment = le;

        le >>= 1;

        /* Block by block, each run of butterflies along the arrays rather
           than across them: the same arithmetic in another order, three
           times as fast for the cache. */
        for (size_t b = 0; b < n; b += increment)
            for (size_t j = 0; j < le; j++)
            {
                const double ur = wr[j * windex], ui = wi[j * windex];
                const size_t i = b + j, ip = i + le;
                const double tr = re[i] - re[ip], ti = im[i] - im[ip];

                re[i] += re[ip];
                im[i] += im[ip];
                re[ip] = tr * ur - ti * ui;
                im[ip] = tr * ui + ti * ur;
            }

        windex <<= 1;
    }

    /* Bit-reversed order back to natural, as the original does after. */
    for (size_t i = 1, j = 0; i < n - 1; i++)
    {
        size_t k = n >> 1;

        while (k <= j)
        {
            j -= k;
            k >>= 1;
        }

        j += k;

        if (i < j)
        {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
}

#endif /* THINK_REALFFT_H */
