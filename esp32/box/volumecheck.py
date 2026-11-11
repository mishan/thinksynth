#!/usr/bin/env python3
"""The volume strip in boxsim, headless: thinkbox built for the linux
target (README.md) with a key held on the keys screen, the volume set by
a finger in the top row between two presses of it. Fails where the second
note isn't as much quieter as the strip asked, or a finger in the strip
played a note itself. And again with a finger that started on a key slid
up into the strip, which is the key's and moves no volume.

    ./volumecheck.py build-sim/thinkbox.elf [OUT.wav]
"""

import array
import math
import os
import subprocess
import sys
import tempfile
import wave

W = 320
TOP = 8                 # in the top row, the strip's
KEY = (160, 200)        # a key, played the same both times
# The strip's level, left to right, as an amplitude squared.
HALF = 0.5 ** 2
# The volume the simulator starts at, with no NVS to keep one.
START = 0.8 ** 2


def play(elf, script, out):
    """thinkbox run headless to `script'; its sound as an RMS between two
    times, or the reason it has none."""
    run = subprocess.run([elf, "--headless", "--script", script],
                         env=dict(os.environ, BOXSIM_AUDIO="wav:" + out),
                         capture_output=True, text=True, timeout=60)
    if run.returncode:
        return None, f"thinkbox exited {run.returncode}"
    try:
        with wave.open(out) as w:
            rate, channels = w.getframerate(), w.getnchannels()
            pcm = array.array("h", w.readframes(w.getnframes()))
    except (OSError, EOFError, wave.Error) as e:
        return None, f"no sound written: {e}"

    def rms(t0, t1):
        seg = pcm[int(t0 * rate) * channels:int(t1 * rate) * channels]
        return math.sqrt(sum(x * x for x in seg) / max(1, len(seg)))
    return rms, None


def main():
    elf = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(tempfile.mkdtemp(), "volume.wav")
    x, y = KEY
    script = (f"200 down {W - 1} {TOP}; 300 up; "                    # full
              f"600 down {x} {y}; 1600 up; "                         # a note
              f"7600 down {W // 2} {TOP}; 7700 move {(W - 1) // 2} {TOP}; 7800 up; "  # half
              f"8000 down {x} {y}; 9000 up; "                        # the note again
              f"10000 quit")
    why = []
    rms, failed = play(elf, script, out)
    if failed:
        why.append(failed)
    else:
        # The strip touched before anything has played: silent, or a note.
        strip, first, second = rms(0.2, 0.6), rms(0.7, 1.5), rms(8.1, 8.9)
        print(f"volumecheck: the note at full {first:.0f}, at half {second:.0f}, "
              f"the strip touched {strip:.0f} (RMS)")
        if first < 100:
            why.append("the note at full made no sound")
        elif abs(second / first - HALF) > 0.1 * HALF:
            why.append(f"at half the strip, the note is {second / first:.3f} of its "
                       f"level at full, not {HALF:.3f}")
        if strip > 0.05 * first:
            why.append("a finger in the strip played a note")

        # A key at the left end slid up into the strip, then, rung out, the
        # note again: at the volume the BOX starts at, as the strip didn't
        # take it to the left end's silence.
        rms2, failed = play(elf, f"100 down 2 {y}; 200 move 2 {TOP}; 300 up; "
                                 f"6600 down {x} {y}; 7600 up; 8600 quit", out)
        if failed:
            why.append(failed)
        elif first >= 100:
            slid = rms2(6.7, 7.5)
            print(f"volumecheck: after a key slid into the strip, the note at {slid:.0f}")
            if abs(slid / first - START) > 0.1 * START:
                why.append(f"a key slid into the strip moved the volume: the note is "
                           f"{slid / first:.3f} of full, not the start's {START:.3f}")
    for w in why:
        print("FAIL  " + w)
    return 1 if why else 0


if __name__ == "__main__":
    sys.exit(main())
