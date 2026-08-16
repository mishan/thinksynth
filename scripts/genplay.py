#!/usr/bin/env python3
"""genplay -- Eno-style generative MIDI player.

Reads a .gen file describing independent lines, each firing a note on its
own period, and sends the result to a MIDI output port (e.g. thinksynth's
RtMidi input).

Usage:
    pip install mido python-rtmidi
    ./genplay.py airports.gen --port thinksynth
    ./genplay.py airports.gen --list-ports

.gen format (line-based, '#' comments):

    # optional default pitch pool for lines that don't name their own
    scale F3 Ab3 C4 Db4 Eb4

    # one 'line' per independent voice; key=value pairs
    #   notes    note name or comma-separated pool (default: the scale)
    #   period   seconds between firings
    #   jitter   +/- seconds of random spread on the period (default 0)
    #   prob     chance a firing actually sounds, 0..1 (default 1.0)
    #   hold     seconds before note-off; the patch's release does the tail
    #   vel      base velocity (default 80)
    #   vel_jitter  +/- velocity spread (default 0)
    #   channel  MIDI channel 0-15 (default 0)
    line notes=Ab3 period=21.3 jitter=1.5 prob=0.9 hold=4 channel=3
"""

import argparse
import random
import re
import sys
import time

import mido

NOTE_RE = re.compile(r"^([A-Ga-g])([#b]?)(-?\d+)$")
SEMIS = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def note_number(name):
    m = NOTE_RE.match(name.strip())
    if not m:
        raise ValueError(f"bad note name: {name!r}")
    letter, acc, octave = m.groups()
    n = SEMIS[letter.upper()] + (1 if acc == "#" else -1 if acc == "b" else 0)
    return 12 * (int(octave) + 1) + n


def parse_gen(path):
    scale = []
    lines = []
    with open(path) as f:
        for lineno, raw in enumerate(f, 1):
            text = raw.split("#", 1)[0].strip()
            if not text:
                continue
            tokens = text.split()
            kind, rest = tokens[0], tokens[1:]
            if kind == "scale":
                scale = [note_number(t) for t in rest]
            elif kind == "line":
                spec = {
                    "notes": None,
                    "period": None,
                    "jitter": 0.0,
                    "prob": 1.0,
                    "hold": 2.0,
                    "vel": 80,
                    "vel_jitter": 0,
                    "channel": 0,
                }
                for tok in rest:
                    if "=" not in tok:
                        sys.exit(f"{path}:{lineno}: expected key=value, got {tok!r}")
                    key, val = tok.split("=", 1)
                    if key not in spec:
                        sys.exit(f"{path}:{lineno}: unknown key {key!r}")
                    if key == "notes":
                        spec[key] = [note_number(n) for n in val.split(",")]
                    elif key in ("vel", "vel_jitter", "channel"):
                        spec[key] = int(val)
                    else:
                        spec[key] = float(val)
                if spec["period"] is None:
                    sys.exit(f"{path}:{lineno}: line needs period=")
                if spec["notes"] is None:
                    if not scale:
                        sys.exit(f"{path}:{lineno}: no notes= and no scale defined")
                    spec["notes"] = list(scale)
                lines.append(spec)
            else:
                sys.exit(f"{path}:{lineno}: unknown directive {kind!r}")
    if not lines:
        sys.exit(f"{path}: no lines defined")
    return lines


def pick_port(name_fragment):
    names = mido.get_output_names()
    if name_fragment:
        for n in names:
            if name_fragment.lower() in n.lower():
                return n
        sys.exit(f"no MIDI output matching {name_fragment!r}; "
                 f"available: {names}")
    if not names:
        sys.exit("no MIDI output ports found")
    return names[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("genfile")
    ap.add_argument("--port", help="substring of the MIDI output port name")
    ap.add_argument("--list-ports", action="store_true")
    ap.add_argument("--seed", type=int, help="RNG seed, for a repeatable run")
    args = ap.parse_args()

    if args.list_ports:
        for n in mido.get_output_names():
            print(n)
        return

    if args.seed is not None:
        random.seed(args.seed)

    lines = parse_gen(args.genfile)
    port_name = pick_port(args.port)
    out = mido.open_output(port_name)
    print(f"sending to: {port_name}")
    print(f"{len(lines)} lines; ctrl-C to stop")

    now = time.monotonic()
    # Stagger the first firings randomly across each line's own period,
    # like dropping the tape loops in at arbitrary points.
    for ln in lines:
        ln["next_at"] = now + random.uniform(0, ln["period"])
    pending_offs = []  # (when, channel, note)

    try:
        while True:
            now = time.monotonic()

            for off in [o for o in pending_offs if o[0] <= now]:
                pending_offs.remove(off)
                _, ch, note = off
                out.send(mido.Message("note_off", channel=ch, note=note))

            for ln in lines:
                if ln["next_at"] > now:
                    continue
                ln["next_at"] = now + ln["period"] + random.uniform(
                    -ln["jitter"], ln["jitter"])
                if random.random() >= ln["prob"]:
                    continue  # this firing sits one out
                note = random.choice(ln["notes"])
                vel = ln["vel"] + random.randint(-ln["vel_jitter"],
                                                 ln["vel_jitter"])
                vel = max(1, min(127, vel))
                out.send(mido.Message("note_on", channel=ln["channel"],
                                      note=note, velocity=vel))
                pending_offs.append((now + ln["hold"], ln["channel"], note))

            time.sleep(0.02)
    except KeyboardInterrupt:
        print("\nstopping")
    finally:
        for _, ch, note in pending_offs:
            out.send(mido.Message("note_off", channel=ch, note=note))
        for ch in {ln["channel"] for ln in lines}:
            out.send(mido.Message("control_change", channel=ch,
                                  control=123, value=0))  # all notes off
        out.close()


if __name__ == "__main__":
    main()
