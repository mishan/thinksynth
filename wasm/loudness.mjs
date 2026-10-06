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

/* scripts/Loudness.h, line for line, so wasm/genwav.mjs reports the same
   LUFS genwav does: compare.mjs holds the two reports to the character. */

const SHELF = [1681.974450955533, 3.999843853973347, 0.7071752369554196];
const HIGHPASS = [38.13547087602444, 0.5003270373238773];

function design (rate, f0, gainDb, q, shelf)
{
    const k = Math.tan(Math.PI * f0 / rate);
    const a0 = 1 + k / q + k * k;
    const f = { a1: 2 * (k * k - 1) / a0, a2: (1 - k / q + k * k) / a0,
                b0: 1, b1: -2, b2: 1 };

    if (shelf)
    {
        const vh = Math.pow(10, gainDb / 20);
        const vb = Math.pow(vh, 0.4996667741545416);

        f.b0 = (vh + vb * k / q + k * k) / a0;
        f.b1 = 2 * (k * k - vh) / a0;
        f.b2 = (vh - vb * k / q + k * k) / a0;
    }

    return f;
}

function filter (f, t, x)
{
    const y = f.b0 * x + f.b1 * t.x1 + f.b2 * t.x2 - f.a1 * t.y1 -
              f.a2 * t.y2;

    t.x2 = t.x1;
    t.x1 = x;
    t.y2 = t.y1;
    t.y1 = y;

    return y;
}

const taps = () => ({ x1: 0, x2: 0, y1: 0, y2: 0 });

export const lufs = (meanSquare) =>
    meanSquare > 0 ? -0.691 + 10 * Math.log10(meanSquare) : -Infinity;

export class Loudness
{
    constructor (channels, rate)
    {
        this.chans = channels;
        this.state = Array.from({ length: channels },
                                () => ({ sh: taps(), hp: taps() }));
        this.blockLen = Math.floor(rate * 0.1 + 0.5);
        this.shelf = design(rate, SHELF[0], SHELF[1], SHELF[2], true);
        this.highpass = design(rate, HIGHPASS[0], 0, HIGHPASS[1], false);
        this.fill = 0;
        this.sum = 0;
        this.quarters = [];
    }

    /* One frame, the channels of it from `at' in `samples'. */
    add (samples, at)
    {
        for (let c = 0; c < this.chans; c++)
        {
            const s = this.state[c];
            const y = filter(this.highpass, s.hp,
                             filter(this.shelf, s.sh, samples[at + c]));

            this.sum += y * y;
        }

        if (++this.fill === this.blockLen)
        {
            this.quarters.push(this.sum / this.blockLen);
            this.sum = 0;
            this.fill = 0;
        }
    }

    integrated ()
    {
        const q = this.quarters, blocks = [];

        for (let i = 3; i < q.length; i++)
            blocks.push((q[i - 3] + q[i - 2] + q[i - 1] + q[i]) / 4);

        const gated = (floor) =>
        {
            let sum = 0, n = 0;

            for (const z of blocks)
                if (z >= floor)
                {
                    sum += z;
                    n++;
                }

            return n ? sum / n : 0;
        };

        const gate = gated(Math.pow(10, (-70 + 0.691) / 10));

        return gate > 0 ? lufs(gated(gate / 10)) : -Infinity;
    }
}
