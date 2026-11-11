#!/usr/bin/env python3
"""The composer screen in boxsim, headless: thinkbox built for the linux
target (README.md) stepped to its fourth screen and each piece played in
turn, a press of the side button apart, the sound written to a WAV. Fails
where a piece didn't load, the firmware's pieces aren't pieces/'s, no
chanarg a step moved reached the voices, a sample out wasn't finite, or a
piece made no sound.

    ./composercheck.py build-sim/thinkbox.elf [SECONDS] [OUT.wav]

SECONDS is each piece's, 60 by default.
"""

import array
import glob
import math
import os
import re
import subprocess
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
# Held, the side button steps to the next screen: the keys, the marbles,
# Life, the composer. The first piece starts as its screen shows, and a
# press starts the next, the last one's voices faded out over a block.
SHOWN_MS = 5000
# Life's notes, played on the way, rung out before the first piece is
# measured; a press's piece is measured from a second after it.
RUNG_OUT_MS = 6000
SETTLED_MS = 1000
# Played on past what is measured until the composer has said its
# counters again, every 5 s, so they cover every sample measured.
SAID_MS = 6000


def main():
    elf = sys.argv[1]
    seconds = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    if seconds * 1000 <= RUNG_OUT_MS:
        sys.exit(f"composercheck: SECONDS must be past {RUNG_OUT_MS // 1000}, "
                 "where the first piece is first measured")
    out = sys.argv[3] if len(sys.argv) > 3 else os.path.join(tempfile.mkdtemp(), "composer.wav")
    pieces = len(glob.glob(os.path.join(HERE, "pieces", "*.gen")))
    starts = [SHOWN_MS + i * seconds * 1000 for i in range(pieces)]
    steps = [f"{1000 + 1500 * i} hold side; {1900 + 1500 * i} release side" for i in range(3)]
    steps += [f"{t} hold side; {t + 100} release side" for t in starts[1:]]
    run = subprocess.run([elf, "--headless", "--script",
                          "; ".join(steps) +
                          f"; {SHOWN_MS + pieces * seconds * 1000 + SAID_MS} quit"],
                         env=dict(os.environ, BOXSIM_AUDIO="wav:" + out),
                         capture_output=True, text=True, timeout=pieces * seconds + 70)
    lines = [l for l in (run.stdout + run.stderr).splitlines() if l.startswith("composer:")]
    print("\n".join(lines))

    why = []
    if run.returncode:
        why.append(f"thinkbox exited {run.returncode}")
    why += [l for l in lines if "did not load" in l]
    played = [l for l in lines if l.startswith("composer: playing")]
    if len(played) != pieces:
        why.append(f"{len(played)} pieces played, of pieces/'s {pieces}")
    # Each piece's file, from its playing line or its error, and its name
    # by where it is in the firmware's list.
    files = set(re.findall(r"composer: (?:playing .*\()?([a-z0-9_]+\.gen)", "\n".join(lines)))
    ours = {os.path.basename(f) for f in glob.glob(os.path.join(HERE, "pieces", "*.gen"))}
    if files != ours:
        why.append(f"the firmware's pieces, {sorted(files)}, aren't pieces/'s, {sorted(ours)}")
    names, counts = {}, set()
    for l in played:
        at = re.search(r", (\d+) of (\d+)\)", l)
        names[int(at[1]) - 1] = l.split('"')[1]
        counts.add(int(at[2]))
    if counts - {pieces}:
        why.append(f"the firmware has {max(counts - {pieces})} pieces, pieces/ {pieces}")
    stats = [l for l in lines if " late " in l]
    if not stats:
        why.append("the composer said nothing as it played")
    else:
        last = stats[-1]
        # Reported, not failed: the simulator's tasks are threads on one
        # core, scheduled by the desktop's, so its timing isn't the BOX's.
        print(f"composercheck: {re.search(r' late (\d+)', last)[1]} events played late")
        if not int(re.search(r" knobs (\d+)", last)[1]):
            why.append("no chanarg a step moved reached the voices")
        nonfinite = int(re.search(r" non-finite (\d+)", last)[1])
        if nonfinite:
            why.append(f"{nonfinite} samples out weren't finite")

    try:
        w = wave.open(out)
        rate, channels = w.getframerate(), w.getnchannels()
        pcm = array.array("h", w.readframes(w.getnframes()))
    except (OSError, EOFError, wave.Error) as e:
        why.append(f"no sound written: {e}")
        starts = []
    for i, start in enumerate(starts):
        a = (start + (RUNG_OUT_MS if i == 0 else SETTLED_MS)) * rate // 1000 * channels
        b = (start + seconds * 1000) * rate // 1000 * channels
        piece = pcm[a:b]
        rms = math.sqrt(sum(x * x for x in piece) / max(1, len(piece)))
        peak = max((abs(x) for x in piece), default=0)
        name = names.get(i, f"piece {i + 1}")
        print(f"composercheck: {name}: RMS {20 * math.log10(rms / 32768 + 1e-12):.1f} dBFS, "
              f"peak {20 * math.log10(peak / 32768 + 1e-12):.1f} dBFS")
        if rms / 32768 < 10 ** (-50 / 20):
            why.append(f"{name} made no sound")

    for w in why:
        print("FAIL  " + w)
    return 1 if why else 0


if __name__ == "__main__":
    sys.exit(main())
