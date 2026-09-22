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

#ifndef THINK_DICE_H
#define THINK_DICE_H

/* A random stream a voice can keep in its own state arg.
 *
 * osc::noise draws from one generator per synth (osc/noiseslot.h), which is
 * right for noise: every voice's hiss is the same hiss and nobody can tell.
 * A grain cloud or a drift is not like that. What it draws decides where a
 * voice goes, and the promise is that a voice started the same way goes the
 * same way -- so the generator has to belong to the voice, start where the
 * voice says, and carry across windows in the state arg the rest of the
 * voice's memory lives in.
 *
 * A state arg is floats, and a float holds an integer exactly only to 2^24.
 * So the generator is drand48's -- x' = (0x5DEECE66D x + 0xB) mod 2^48, the
 * POSIX one, full period -- with its 48 bits kept as two 24-bit halves, each
 * of which a float holds exactly. Written out rather than called because
 * drand48() is one stream for the process and absent on Windows.
 */

#include <stdint.h>
#include <string.h>

/* The two floats at `s' become the stream for `seed'. Any float is a seed;
   its bits are what is used, so 1 and 1.5 are different streams. */
static inline void thDiceSeed (float *s, float seed)
{
    uint32_t bits;

    memcpy(&bits, &seed, sizeof(bits));

    /* drand48's seeding: the seed's 32 bits above a fixed 0x330E. */
    const uint64_t x = ((uint64_t)bits << 16) | 0x330Eu;

    s[0] = (float)(uint32_t)(x >> 24);
    s[1] = (float)(uint32_t)(x & 0xFFFFFFu);
}

/* The next draw, uniform on [0, 1). */
static inline double thDiceNext (float *s)
{
    const uint64_t mask = (((uint64_t)1) << 48) - 1;
    uint64_t x = ((uint64_t)(uint32_t)s[0] << 24) | (uint64_t)(uint32_t)s[1];

    x = (x * 0x5DEECE66Dull + 0xBull) & mask;

    s[0] = (float)(uint32_t)(x >> 24);
    s[1] = (float)(uint32_t)(x & 0xFFFFFFu);

    return (double)x / (double)(((uint64_t)1) << 48);
}

#endif /* THINK_DICE_H */
