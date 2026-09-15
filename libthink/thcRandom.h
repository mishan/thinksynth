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

/*
 * Randomness a replay can count on.
 *
 * std::mt19937 is specified to the bit: the same seed is the same stream
 * on every platform. The distributions that turn the stream into numbers
 * are not. uniform_int_distribution and normal_distribution are each
 * library's own algorithm, libstdc++ and libc++ draw differently from the
 * same engine, and a composer that used them wrote a piece that replayed
 * exactly on the machine it was written on and composed something else
 * under the other library -- which is macOS, and the wasm build. These are
 * the two a composer needs, spelled out, so the numbers are this file's
 * rather than the toolchain's.
 *
 * uniform_real_distribution is left as it is: both libraries implement it
 * with generate_canonical, which the standard gives as a formula, and the
 * two agree to the bit. Should one of them ever change how it computes
 * that, a third helper belongs here.
 *
 * The same goes for anything that orders ties: std::sort leaves equal
 * elements wherever its algorithm happens to, so a composer that sorts
 * something with ties in it sorts with std::stable_sort.
 */

#ifndef THC_RANDOM_H
#define THC_RANDOM_H 1

#include <stdint.h>
#include <stddef.h>

#include <cmath>
#include <random>

/* Uniform on [lo, hi], both ends included. One 32-bit draw per try: the
   largest multiple of the span that fits in 2^32 is accepted and the rest
   thrown back, so no value is likelier than another. The span may be at
   most 2^32, which for what a composer picks between -- an instrument, a
   genome, a cut point -- is not a limit anyone meets. */
inline size_t thcUniformIndex (std::mt19937 &rng, size_t lo, size_t hi)
{
    const uint64_t span  = (uint64_t)(hi - lo) + 1;
    const uint64_t limit = 0x100000000ull - 0x100000000ull % span;
    uint64_t x;

    do
        x = rng();
    while (x >= limit);

    return lo + (size_t)(x % span);
}

/* The standard normal, by the polar method. Each accepted pair makes two
   values and the second is kept for the next call, as the library's
   distribution does -- so keep one of these for as long as a
   std::normal_distribution would have been kept. */
struct thcNormal
{
    bool   have = false;
    double kept = 0;

    double operator() (std::mt19937 &rng)
    {
        if (have)
        {
            have = false;
            return kept;
        }

        std::uniform_real_distribution<double> u(-1.0, 1.0);
        double x, y, r2;

        do
        {
            x = u(rng);
            y = u(rng);
            r2 = x * x + y * y;
        }
        while (r2 > 1.0 || r2 == 0.0);

        const double m = std::sqrt(-2.0 * std::log(r2) / r2);

        kept = x * m;
        have = true;

        return y * m;
    }
};

#endif /* THC_RANDOM_H */
