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

#ifndef TH_KERNEL_FFT_H
#define TH_KERNEL_FFT_H 1

/* A radix-2 FFT of real input in float, 256 to 4096 points, for the fft/
 * plugins' audio path.
 *
 * The tree has three others and none is this. thv::FFTR (libthink/thFFT.h)
 * is a display's: double, forward and magnitude only. osc::pad's is double
 * and builds a table once. plugins/fft/dsp.c is not built. A spectral node
 * runs a transform a hop per voice, in float as KERNELS.md asks, on real
 * input, so the n-point real transform is an n/2-point complex one with a
 * pass either side, half the work of a complex transform of the samples.
 *
 * The twiddles are one table at THK_FFT_MAX, strided for the smaller sizes,
 * each (float)sin(double) and (float)cos(double) as osc::pad's are. It is a
 * function-local static const: built once, on first use, by whichever thread
 * gets there, which C++ makes safe. A plugin's module_init asks for it so
 * that the first use is not on the audio thread; a table rebuilt there
 * instead would race, since module_init runs again for every synth that
 * loads the plugin. */

#include <math.h>

/* The largest transform, and so the twiddle table's size. */
#define THK_FFT_MAX 4096

struct thkFftTable
{
    /* e^(-2 pi i j / THK_FFT_MAX), j from 0 to half way round. */
    float c[THK_FFT_MAX / 2 + 1], s[THK_FFT_MAX / 2 + 1];

    thkFftTable ()
    {
        for (int j = 0; j <= THK_FFT_MAX / 2; j++)
        {
            const double a = 2 * 3.14159265358979323846 * j / THK_FFT_MAX;

            c[j] = (float)cos(a);
            s[j] = (float)-sin(a);
        }
    }
};

static inline const thkFftTable &thkFftTwiddles ()
{
    static const thkFftTable t;

    return t;
}

/* In place, n complex points, n a power of two up to THK_FFT_MAX / 2;
   inverse turns the other way and does not scale. */
static inline void thkFftComplex (float *re, float *im, int n, bool inverse)
{
    const thkFftTable &t = thkFftTwiddles();

    for (int i = 1, j = 0; i < n; i++)
    {
        int bit = n >> 1;

        for (; j & bit; bit >>= 1)
            j ^= bit;
        j |= bit;
        if (i < j)
        {
            const float r = re[i], m = im[i];

            re[i] = re[j]; im[i] = im[j];
            re[j] = r; im[j] = m;
        }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const int stride = THK_FFT_MAX / len, half = len >> 1;

        for (int i = 0; i < n; i += len)
            for (int k = 0; k < half; k++)
            {
                const float wr = t.c[k * stride], wi = inverse ? -t.s[k * stride] : t.s[k * stride];
                const int a = i + k, b = a + half;
                const float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;

                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr; im[a] += xi;
            }
    }
}

/* n real samples to n/2 + 1 bins, re and im each n/2 + 1 long, unscaled:
   the even samples the real part and the odd the imaginary of an n/2-point
   transform, then the two pulled apart. re and im need n/2 + 1 floats. */
static inline void thkFftReal (const float *x, float *re, float *im, int n)
{
    const thkFftTable &t = thkFftTwiddles();
    const int h = n / 2, stride = THK_FFT_MAX / n;

    for (int m = 0; m < h; m++)
    {
        re[m] = x[2 * m];
        im[m] = x[2 * m + 1];
    }
    thkFftComplex(re, im, h, false);
    re[h] = re[0];
    im[h] = im[0];

    /* X[k] = E[k] + W^k O[k], from Z[k] and conj(Z[h - k]), for k and h - k
       together so that each pair is read before either is written. */
    for (int k = 0; k <= h / 2; k++)
    {
        const int q = h - k;
        const float ar = re[k], ai = im[k], br = re[q], bi = im[q];
        const float er = 0.5f * (ar + br), ei = 0.5f * (ai - bi);
        const float orr = 0.5f * (ai + bi), oi = 0.5f * (br - ar);
        const float wr = t.c[k * stride], wi = t.s[k * stride];
        const float vr = orr * wr - oi * wi, vi = orr * wi + oi * wr;

        re[k] = er + vr;
        im[k] = ei + vi;

        /* h - k: E is conj(E[k]) and W^(h-k) O is -conj(W^k O[k]). */
        re[q] = er - vr;
        im[q] = vi - ei;
    }
}

/* The inverse: n/2 + 1 bins to n real samples, scaled so that it undoes
   thkFftReal. re and im are worked in place. */
static inline void thkFftRealInverse (float *re, float *im, float *x, int n)
{
    const thkFftTable &t = thkFftTwiddles();
    const int h = n / 2, stride = THK_FFT_MAX / n;

    for (int k = 0; k <= h / 2; k++)
    {
        const int q = h - k;
        const float ar = re[k], ai = im[k], br = re[q], bi = im[q];
        const float er = 0.5f * (ar + br), ei = 0.5f * (ai - bi);
        const float dr = 0.5f * (ar - br), di = 0.5f * (ai + bi);
        /* O[k] = (X[k] - conj(X[h-k])) / 2 * W^-k, and Z = E + i O. */
        const float wr = t.c[k * stride], wi = -t.s[k * stride];
        const float orr = dr * wr - di * wi, oi = dr * wi + di * wr;

        re[k] = er - oi;
        im[k] = ei + orr;
        re[q] = er + oi;
        im[q] = orr - ei;
    }
    thkFftComplex(re, im, h, true);

    const float scale = 1.0f / h;

    for (int m = 0; m < h; m++)
    {
        x[2 * m] = re[m] * scale;
        x[2 * m + 1] = im[m] * scale;
    }
}

#endif /* TH_KERNEL_FFT_H */
