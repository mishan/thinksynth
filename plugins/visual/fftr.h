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

#ifndef TH_VISUAL_FFTR_H
#define TH_VISUAL_FFTR_H 1

/*
 * A radix-2 FFT for the visual modules, and the window that goes with it.
 *
 * WHY NOT THE ONE ALREADY IN THE TREE
 *
 * plugins/fft/dsp.c has a working radix-2 -- Embree & Kimble's, from C
 * Language Algorithms for DSP. It is not built, nothing references it, and
 * VISUALIZERS.md said finding out whether it could be reused was a
 * twenty-minute question. The answer is no, for two reasons that are about the
 * shape of it rather than the arithmetic:
 *
 *   Its twiddle table is a function-static keyed on the last size it was asked
 *   for, so two instances at different sizes free and rebuild each other's
 *   table on every call, and none of it is safe to call from two places.
 *   Every probe here is its own instance and there may be eight.
 *
 *   It calls exit(1) when a calloc fails -- a library killing the host
 *   process, which is exactly the pattern that was removed from the parser
 *   years ago.
 *
 * So: this. About eighty lines, one instance's worth of state, no globals, and
 * nothing to fail after construction.
 *
 * WHY NOT A DEPENDENCY
 *
 * kissfft or FFTW would both do, and both are more than a display needs. A
 * spectrum at 1024 bins thirty times a second is 15k butterflies per frame,
 * which is nothing; the reason to take a dependency would be speed, and there
 * is no speed problem to solve. VISUALIZERS.md called it and this is that
 * call carried out.
 *
 * Everything here runs on the GUI thread, like the rest of a visual module.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <new>

namespace thv {

/* Not M_PI: it is not in C++ at all, glibc provides it from <math.h> and
   MinGW's UCRT does not without _USE_MATH_DEFINES. The same reason
   scripts/visualcheck carries its own. */
const double FFT_PI = 3.14159265358979323846;

class FFTR {
public:
    /* order 10 is 1024 points. Everything is allocated here and nothing after,
       so a module that constructs one has either got it or not. */
    explicit FFTR (unsigned int order)
        : order_(order < 2 ? 2 : (order > 15 ? 15 : order)),
          n_(1u << (order < 2 ? 2 : (order > 15 ? 15 : order))),
          re_(NULL), im_(NULL), cosT_(NULL), sinT_(NULL), win_(NULL),
          rev_(NULL), ok_(false)
    {
        re_ = new (std::nothrow) double[n_];
        im_ = new (std::nothrow) double[n_];
        cosT_ = new (std::nothrow) double[n_ / 2];
        sinT_ = new (std::nothrow) double[n_ / 2];
        win_ = new (std::nothrow) double[n_];
        rev_ = new (std::nothrow) unsigned int[n_];

        if (!re_ || !im_ || !cosT_ || !sinT_ || !win_ || !rev_)
            return;     /* ok_ stays false; magnitude() then does nothing */

        for (unsigned int i = 0; i < n_ / 2; i++)
        {
            const double a = -2.0 * FFT_PI * (double)i / (double)n_;

            cosT_[i] = cos(a);
            sinT_[i] = sin(a);
        }

        /* Hann. Flat-top would read levels better and Blackman would separate
           close partials better; Hann is the one that does neither badly, and
           a display is not a measurement. Baked in rather than passed by the
           caller because both modules want the same one and a window is not
           the interesting decision here. */
        for (unsigned int i = 0; i < n_; i++)
            win_[i] = 0.5 - 0.5 * cos(2.0 * FFT_PI * (double)i /
                                      (double)(n_ - 1));

        for (unsigned int i = 0; i < n_; i++)
        {
            unsigned int r = 0;

            for (unsigned int b = 0; b < order_; b++)
                if (i & (1u << b))
                    r |= 1u << (order_ - 1 - b);

            rev_[i] = r;
        }

        ok_ = true;
    }

    ~FFTR (void)
    {
        delete [] re_;
        delete [] im_;
        delete [] cosT_;
        delete [] sinT_;
        delete [] win_;
        delete [] rev_;
    }

    unsigned int size (void) const { return n_; }
    unsigned int bins (void) const { return n_ / 2; }
    bool ok (void) const { return ok_; }

    /* n_/2 magnitudes from n_ real samples, windowed.
     *
     * `in' is read through `at', which lets a caller hand over a ring buffer
     * without copying it straight first: at(i) is the i'th sample of the
     * frame. Scaled so a full-scale sine reads 1.0 in its bin, which is what
     * makes a dB scale mean what it says.
     */
    template <typename At>
    void magnitude (const At &at, float *out)
    {
        if (!ok_)
            return;

        /* Bit-reversed on the way in, so the butterflies below run in
           natural order and nothing has to be permuted afterwards. */
        for (unsigned int i = 0; i < n_; i++)
        {
            const double x = (double)at(i) * win_[i];

            re_[rev_[i]] = x;
            im_[rev_[i]] = 0.0;
        }

        for (unsigned int len = 2; len <= n_; len <<= 1)
        {
            const unsigned int half = len >> 1;
            const unsigned int step = n_ / len;

            for (unsigned int i = 0; i < n_; i += len)
                for (unsigned int j = 0; j < half; j++)
                {
                    const unsigned int t = j * step;
                    const double wr = cosT_[t];
                    const double wi = sinT_[t];

                    const unsigned int a = i + j;
                    const unsigned int b = a + half;

                    const double tr = re_[b] * wr - im_[b] * wi;
                    const double ti = re_[b] * wi + im_[b] * wr;

                    re_[b] = re_[a] - tr;
                    im_[b] = im_[a] - ti;
                    re_[a] += tr;
                    im_[a] += ti;
                }
        }

        /* 2/(n * coherent gain). Hann's coherent gain is 0.5, so 4/n -- which
           puts a full-scale sine at 1.0 in the bin it lands in. Without this
           every reading would be off by a constant that changes with the
           transform size, and a dB axis would be decorative. */
        const double scale = 4.0 / (double)n_;

        for (unsigned int i = 0; i < n_ / 2; i++)
            out[i] = (float)(sqrt(re_[i] * re_[i] + im_[i] * im_[i]) * scale);
    }

private:
    FFTR (const FFTR &);
    FFTR &operator= (const FFTR &);

    unsigned int order_, n_;

    double *re_, *im_;
    double *cosT_, *sinT_;
    double *win_;
    unsigned int *rev_;

    bool ok_;
};

} /* namespace thv */

#endif /* TH_VISUAL_FFTR_H */
