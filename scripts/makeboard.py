#!/usr/bin/env python3
#
# Copyright (C) 2004-2026 Metaphonic Labs
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.

"""makeboard -- writes dsp/samples/piano_board.wav, a piano soundboard's
impulse response, for dsp/grand.dsp to excite its strings with.

COMMUTED SYNTHESIS. A piano's hammer, strings and soundboard are in
series, and as far as the strings are linear the order does not matter:
hammer, then strings, then board is the same sound as board, then
hammer, then strings (Smith and Van Duyne, "Commuted piano synthesis",
ICMC 1995). So the board's response need not be a filter after every
voice. It can be the thing the strings are struck with -- one wav, read
by osc::sample at the top of each note -- and the most expensive part
of the instrument costs a sample read.

THE BOARD is a plate, and a plate's modes are about evenly spaced in
frequency: here one every BOARD_SPACING hertz from BOARD_LOW up, each
nudged off the grid so no two are in a simple ratio. Each mode:

- rings for a T60 of 2.2 / (loss * f), a constant loss factor, so the
  low modes last most of a second and the top ones a few milliseconds
  (spruce's is about 0.02; Conklin, "Design and tone in the
  mechanoacoustic piano", JASA 1996);
- starts at a random phase with a Rayleigh-distributed amplitude, which
  is what a sum of many reflections at one driving point looks like;
- and falls above BOARD_KNEE at 6 dB an octave, where the bridge gets
  too heavy for the string to move.

Nothing here is a measurement of a particular piano; it is a board with
the right statistics, and the seed fixes which one. The file is
generated, not recorded, so it regenerates from this script and carries
no question about whose piano it was.

    scripts/makeboard.py [OUT.wav]      # default dsp/samples/piano_board.wav

Standard library only.
"""

import math
import random
import struct
import sys
import wave

RATE = 44100
SECONDS = 1.2
SEED = 1995

BOARD_LOW = 45.0         # the lowest mode, Hz
BOARD_HIGH = 12000.0     # the highest, Hz
BOARD_SPACING = 18.0     # mean distance between modes, Hz
BOARD_LOSS = 0.02        # loss factor: T60 = 2.2 / (loss * f)
BOARD_T60_MAX = 1.0      # seconds; the lowest modes are held to this
BOARD_KNEE = 2000.0      # Hz, above which the response falls 6 dB/octave

FADE = 0.03              # seconds of fade at the end of the file
PEAK = 0.9               # the file's peak, full scale


def modes(rng):
    """(frequency, T60, amplitude, phase) for every mode on the board."""
    out = []
    count = int((BOARD_HIGH - BOARD_LOW) / BOARD_SPACING)

    for i in range(count):
        f = BOARD_LOW + (i + rng.uniform(-0.4, 0.4)) * BOARD_SPACING
        t60 = min(BOARD_T60_MAX, 2.2 / (BOARD_LOSS * f))
        # Rayleigh: the magnitude of a complex Gaussian.
        amp = math.hypot(rng.gauss(0, 1), rng.gauss(0, 1))
        amp /= math.sqrt(1 + (f / BOARD_KNEE) ** 2)
        out.append((f, t60, amp, rng.uniform(0, 2 * math.pi)))

    return out


def render(board):
    n = int(SECONDS * RATE)
    buf = [0.0] * n

    for f, t60, amp, phase in board:
        # A decaying phasor, rotated a sample at a time, until it is
        # 80 dB down.
        r = 10 ** (-3.0 / (t60 * RATE))
        w = 2 * math.pi * f / RATE
        cw, sw = r * math.cos(w), r * math.sin(w)
        re, im = amp * math.cos(phase), amp * math.sin(phase)
        last = min(n, int(t60 * RATE * 80 / 60))

        for i in range(last):
            buf[i] += im
            re, im = re * cw - im * sw, re * sw + im * cw

    fade = int(FADE * RATE)

    for i in range(fade):
        buf[n - fade + i] *= 0.5 * (1 + math.cos(math.pi * i / fade))

    top = max(abs(x) for x in buf)

    return [x * PEAK / top for x in buf]


def write(path, samples):
    with wave.open(path, "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b"".join(
            struct.pack("<h", int(round(x * 32767))) for x in samples))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "dsp/samples/piano_board.wav"
    board = modes(random.Random(SEED))

    write(path, render(board))
    print("makeboard: %s, %d modes, %.1f s" % (path, len(board), SECONDS))


if __name__ == "__main__":
    main()
