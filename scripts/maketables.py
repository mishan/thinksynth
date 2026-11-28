#!/usr/bin/env python3
#
# Copyright (C) 2004-2026 The thinksynth authors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
# Public License for more details.
#
# You should have received a copy of the GNU General
# Public License along with this program; if not, write to the
# Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

"""The wavetables osc::wavetable ships: dsp/samples/wt_*.wav.

Each frame is a single cycle of 2048 samples summed from its harmonics in
double, so a table is exactly the amplitudes written here and nothing a
tool did to it; every frame is scaled to the same peak, so a sweep through
a table changes its colour and not its level. Written as 16-bit mono with
the `clm ' chunk wavetable editors write, which osc::wavetable reads its
frame length from.

    scripts/maketables.py [DIR]     # dsp/samples by default
"""

import math
import os
import struct
import sys

SIZE = 2048
PEAK = 0.8


def frame(amps, phase=0.0):
    """One cycle from {harmonic: amplitude}, each a sine from `phase'."""
    out = [0.0] * SIZE

    for h, a in amps.items():
        if a == 0 or h >= SIZE // 2:
            continue

        for t in range(SIZE):
            out[t] += a * math.sin(2 * math.pi * ((h * t) % SIZE) / SIZE +
                                   phase * h)

    peak = max(abs(v) for v in out) or 1.0

    return [v * PEAK / peak for v in out]


def saw_square(n=64):
    """A saw that loses its even harmonics: a square at the end."""
    return [frame({h: (1 if h % 2 else 1 - f / (n - 1)) / h
                   for h in range(1, 257)}) for f in range(n)]


def pwm(n=64):
    """A pulse from 50% wide to 5%: the classic PWM sweep, band-limited."""
    frames = []

    for f in range(n):
        w = 0.5 - 0.45 * f / (n - 1)
        # sin(pi h w) / h, each a cosine: sines a quarter cycle on.
        frames.append(frame({h: math.sin(math.pi * h * w) / h
                             for h in range(1, 257)}, math.pi / 2))

    return frames


def harmonics(n=64):
    """The partials one at a time: frame f is 1 to f + 1, a saw's 1/h."""
    return [frame({h: 1 / h for h in range(1, f + 2)}) for f in range(n)]


# Three formants of each vowel, Hz, a man's voice (Peterson and Barney).
VOWELS = [(730, 1090, 2440), (530, 1840, 2480), (270, 2290, 3010),
          (570, 840, 2410), (300, 870, 2240)]


def vowels(n=64):
    """A E I O U on a 110 Hz voice, each formant a resonance over its
    harmonics, the formants moving linearly from one vowel to the next."""
    frames = []

    for f in range(n):
        at = f / (n - 1) * (len(VOWELS) - 1)
        i = min(int(at), len(VOWELS) - 2)
        u = at - i
        formants = [a + (b - a) * u for a, b in zip(VOWELS[i], VOWELS[i + 1])]
        amps = {}

        for h in range(1, 49):
            hz = 110 * h
            level = sum(g / (1 + ((hz - fc) / bw) ** 2)
                        for fc, bw, g in zip(formants, (80, 90, 120),
                                             (1, 0.5, 0.25)))
            amps[h] = level / math.sqrt(h)

        frames.append(frame(amps))

    return frames


def glass(n=32):
    """Sparse, high partials whose weights turn with the frame: a bright,
    bell-ish colour (a single cycle is periodic, so not a bell's
    inharmonic partials)."""
    parts = (1, 5, 9, 13, 21, 27, 34)

    return [frame({h: (0.5 + 0.5 * math.cos(2 * math.pi * (f / n + k / 7))) /
                      math.sqrt(h)
                   for k, h in enumerate(parts)}) for f in range(n)]


def write(path, frames):
    data = b''.join(struct.pack('<h', max(-32768, min(32767,
                                                     round(v * 32767))))
                    for fr in frames for v in fr)
    clm = b'<!>2048 00000000 wavetable (thinksynth)\0'
    clm += b'\0' * (len(clm) & 1)
    fmt = struct.pack('<HHIIHH', 1, 1, 44100, 44100 * 2, 2, 16)
    body = (b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt +
            b'clm ' + struct.pack('<I', len(clm)) + clm +
            b'data' + struct.pack('<I', len(data)) + data)

    with open(path, 'wb') as out:
        out.write(b'RIFF' + struct.pack('<I', len(body)) + body)


TABLES = {'wt_sawsquare.wav': saw_square, 'wt_pwm.wav': pwm,
          'wt_harmonics.wav': harmonics, 'wt_vowels.wav': vowels,
          'wt_glass.wav': glass}


def main():
    where = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', 'dsp', 'samples')

    for name, make in TABLES.items():
        write(os.path.join(where, name), make())


if __name__ == '__main__':
    main()
