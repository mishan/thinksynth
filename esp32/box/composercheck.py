#!/usr/bin/env python3
"""The composer screen in boxsim, headless: thinkbox built for the linux
target (README.md) stepped to its fourth screen, the piece played for a
while and its sound written to a WAV. Fails where the piece didn't load,
an instrument was refused, no chanarg reached the voices, a sample out
wasn't finite, or the piece made no sound.

    ./composercheck.py build-sim/thinkbox.elf [SECONDS] [OUT.wav]
"""

import array
import math
import os
import re
import subprocess
import sys
import tempfile
import wave

# Held, the side button steps to the next screen: the keys, the marbles,
# Life, the composer. The piece starts as its screen shows.
SHOWN_MS = 5000
RUNG_OUT_MS = 6000
# Played on past what is measured until the composer has said its
# counters again, every 5 s, so they cover every sample measured.
SAID_MS = 6000


def main():
    elf = sys.argv[1]
    seconds = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    if seconds * 1000 <= RUNG_OUT_MS:
        sys.exit(f"composercheck: SECONDS must be past {RUNG_OUT_MS // 1000}, "
                 "where the piece is first measured")
    out = sys.argv[3] if len(sys.argv) > 3 else os.path.join(tempfile.mkdtemp(), "composer.wav")
    holds = "; ".join(f"{1000 + 1500 * i} hold side; {1900 + 1500 * i} release side"
                      for i in range(3))
    run = subprocess.run([elf, "--headless", "--script",
                          f"{holds}; {SHOWN_MS + seconds * 1000 + SAID_MS} quit"],
                         env=dict(os.environ, BOXSIM_AUDIO="wav:" + out),
                         capture_output=True, text=True, timeout=seconds + 70)
    log = run.stdout + run.stderr
    lines = [l for l in log.splitlines() if l.startswith("composer:")]
    print("\n".join(lines))

    why = []
    if run.returncode:
        why.append(f"thinkbox exited {run.returncode}")
    if not any(l.startswith("composer: playing") for l in lines):
        why.append("no piece played")
    why += [l for l in lines if "did not load" in l]
    last = [l for l in lines if " late " in l]
    if not last:
        why.append("the composer said nothing as it played")
    else:
        late = int(re.search(r" late (\d+)", last[-1])[1])
        nonfinite = int(re.search(r" non-finite (\d+)", last[-1])[1])
        if not int(re.search(r" knobs (\d+)", last[-1])[1]):
            why.append("no chanarg reached the voices")
        # Reported, not failed: the simulator's tasks are threads on one
        # core, scheduled by the desktop's, so its timing isn't the BOX's.
        print(f"composercheck: {late} events played late")
        if nonfinite:
            why.append(f"{nonfinite} samples out weren't finite")

    # The piece's own sound: from after Life's notes, played on the way
    # to its screen, have rung out.
    try:
        w = wave.open(out)
    except (OSError, EOFError, wave.Error) as e:
        why.append(f"no sound written: {e}")
    else:
        rate, channels = w.getframerate(), w.getnchannels()
        pcm = array.array("h", w.readframes(w.getnframes()))
        piece = pcm[(SHOWN_MS + RUNG_OUT_MS) * rate // 1000 * channels:
                    (SHOWN_MS + seconds * 1000) * rate // 1000 * channels]
        rms = math.sqrt(sum(x * x for x in piece) / max(1, len(piece)))
        print(f"composercheck: {len(piece) / channels / rate:.0f} s of the piece, "
              f"RMS {20 * math.log10(rms / 32768 + 1e-12):.1f} dBFS")
        if rms / 32768 < 10 ** (-50 / 20):
            why.append("the piece made no sound")

    for w in why:
        print("FAIL  " + w)
    return 1 if why else 0


if __name__ == "__main__":
    sys.exit(main())
