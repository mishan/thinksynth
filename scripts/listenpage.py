#!/usr/bin/env python3
"""listenpage -- pieces as a page to listen to: a player and a piano roll each.

    listenpage.py [-o DIR] [--title TEXT] [-s SECONDS] PIECE.gen...

Renders each piece with genwav, encodes it as an MP3 with ffmpeg, and draws
its tape -- every note it played, colored by instrument -- as a piano roll.
DIR (default `listen') holds the whole page, index.html and audio/, for any
static server or for opening from disk: it loads nothing from anywhere else.
The roll's columns are bars at the piece's opening tempo (120 if it writes
none).

An instrument that plays notes and renders silent is warned about rather
than refused: it is most often a sampled instrument whose pack is not
installed (`scripts/packs.py install').

    --genwav PATH    the genwav to render with (default build/scripts/genwav)
    --plugins PATH   its plugins (default: beside genwav, ../plugins)

genwav finds a piece's .dsp files as it always does; run from the tree or set
THINK_DSP_PATH.
"""

import argparse
import html
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile

PX_BEAT = 16
PX_SEMITONE = 6
COLORS = 8          # instruments past the eighth reuse the palette

CSS = """
:root { color-scheme: light dark; --ink: #1d1d1f; --quiet: #5f6368;
  --rule: #d9d9de; --bg: #fbfbfa; --card: #fff;
  --c0: #2563c9; --c1: #d9733f; --c2: #2e9a5b; --c3: #9b4fc4;
  --c4: #c23b5a; --c5: #1f9bb0; --c6: #a3881c; --c7: #6b7280; }
@media (prefers-color-scheme: dark) { :root { --ink: #ececec;
  --quiet: #a0a4aa; --rule: #3a3b3f; --bg: #161618; --card: #1f2023;
  --c0: #6ea2ff; --c1: #f0a27a; --c2: #5cc98a; --c3: #c58cf0;
  --c4: #f07a94; --c5: #5fd0e0; --c6: #d9bf52; --c7: #a8adb5; } }
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink);
  font: 17px/1.55 Georgia, "Iowan Old Style", serif; }
main { max-width: 820px; margin: 0 auto; padding: 48px 20px 80px; }
h1 { font-size: 2em; margin: 0 0 8px; }
h2 { font-size: 1.35em; margin: 0 0 4px; }
section { background: var(--card); border: 1px solid var(--rule);
  border-radius: 10px; padding: 24px; margin: 28px 0; }
.roll { overflow-x: auto; margin: 12px 0 4px; }
svg { display: block; font: 11px system-ui, sans-serif; }
svg .grid { stroke: var(--rule); } svg .bar { stroke: var(--quiet); }
svg .axis { fill: var(--quiet); }
.legend { font: 13px system-ui, sans-serif; color: var(--quiet); }
.legend span { display: inline-block; margin-right: 14px; }
.legend i { display: inline-block; width: 14px; height: 6px;
  border-radius: 2px; margin-right: 6px; vertical-align: middle; }
audio { width: 100%; margin-top: 12px; }
"""
CSS += "".join(f"svg .c{i} {{ fill: var(--c{i}); }} .legend .c{i} "
               f"{{ background: var(--c{i}); }}\n" for i in range(COLORS))

NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def note_name(n):
    return f"{NAMES[n % 12]}{n // 12 - 1}"


def read_tape(path):
    """The channels the tape names, and its notes as (time, channel,
    note, velocity, duration)."""
    channels, notes = {}, []
    with open(path) as f:
        for line in f:
            m = re.match(r"# channel (\d+) = (.*)", line)
            if m:
                channels[int(m.group(1))] = m.group(2).strip()
            elif line.startswith("N "):
                w = line.split()
                notes.append((float(w[1]), int(w[2]), int(w[3]), int(w[4]),
                              float(w[5])))
    return channels, notes


def roll(title, info, channels, notes, playing):
    """The tape as an SVG piano roll, columns in bars at the piece's
    opening tempo."""
    # To the last note's end: the render runs on through the release tail,
    # which would add a bar with nothing in it.
    length = max([t + d for t, _, _, _, d in notes] or [info["rendered"]])
    tempo, meter = info["tempo"], info["meter"]
    x_of = lambda t: 48 + t * tempo / 60 * PX_BEAT
    bar = 60 / tempo * meter
    pitches = [n for _, _, n, _, _ in notes] or [60]
    lo, hi = min(pitches) - 2, max(pitches) + 2
    y_of = lambda p: 12 + (hi - p) * PX_SEMITONE
    width = int(x_of(math.ceil(length / bar) * bar)) + 16
    height = y_of(lo) + 32
    order = sorted(channels)
    color = {ch: i % COLORS for i, ch in enumerate(order)}

    s = [(f'<svg width="{width}" height="{height}" '
          f'viewBox="0 0 {width} {height}" role="img" '
          f'aria-label="Piano roll of {title}">')]
    for p in range(lo, hi + 1):
        if p % 12 == 0:
            s.append(f'<line x1="48" x2="{width - 8}" y1="{y_of(p)}" '
                     f'y2="{y_of(p)}" class="grid"/><text x="40" '
                     f'y="{y_of(p) + 4}" text-anchor="end" class="axis">'
                     f'{note_name(p)}</text>')
    count = math.ceil(length / bar)
    every = 1 if count <= 64 else 4
    for i in range(count + 1):
        x = x_of(i * bar)
        s.append(f'<line x1="{x:.1f}" x2="{x:.1f}" y1="12" y2="{y_of(lo)}" '
                 f'class="{"bar" if i % every == 0 else "grid"}"/>')
        if i % every == 0 and i < count:
            s.append(f'<text x="{x + 3:.1f}" y="{y_of(lo) + 16}" '
                     f'class="axis">{i + 1}</text>')
    for t, ch, n, v, d in notes:
        x0, x1 = x_of(t), x_of(t + d)
        name = html.escape(channels.get(ch, f"channel {ch}"))
        s.append(f'<rect x="{x0:.1f}" y="{y_of(n) - 3}" '
                 f'width="{max(x1 - x0 - 1, 2):.1f}" height="6" rx="2" '
                 f'class="note c{color.get(ch, 0)}" '
                 f'fill-opacity="{0.45 + 0.55 * v / 127:.2f}"><title>{name}, '
                 f'{note_name(n)}, {t:.2f} s</title></rect>')
    s.append("</svg>")
    legend = "".join(f'<span><i class="c{color[ch]}"></i>'
                     f'{html.escape(channels[ch])}</span>' for ch in order
                     if ch in playing)
    return ('<div class="roll">' + "".join(s) + "</div>"
            f'<div class="legend">{legend}<span>columns in bars</span></div>')


def render(piece, genwav, plugins, seconds, work):
    """genwav's WAV, tape and summary for one piece."""
    wav, tape, summary = (os.path.join(work, x)
                          for x in ("out.wav", "out.tape", "out.json"))
    subprocess.run([genwav, "-q", "-p", plugins, "-s", str(seconds),
                    "--json", summary, "-t", tape, "-o", wav,
                    piece], check=True, stdout=subprocess.DEVNULL)
    with open(summary) as f:
        return wav, tape, json.load(f)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pieces", nargs="+", metavar="PIECE.gen")
    ap.add_argument("-o", "--output", default="listen")
    ap.add_argument("--title", default="Listen")
    ap.add_argument("-s", "--seconds", type=float, default=120)
    ap.add_argument("--genwav", default=os.path.join("build", "scripts",
                                                     "genwav"))
    ap.add_argument("--plugins")
    opt = ap.parse_args()
    if shutil.which("ffmpeg") is None:
        sys.exit("listenpage: the MP3s need ffmpeg, which is not on PATH")
    plugins = opt.plugins or os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(opt.genwav))),
        "plugins")
    os.makedirs(os.path.join(opt.output, "audio"), exist_ok=True)

    sections, used = [], set()
    for piece in opt.pieces:
        # The slug names the MP3 and the section; kept to characters a URL
        # and a case-folding filesystem both take as they are.
        stem = re.sub(r"[^a-z0-9_-]+", "-",
                      os.path.splitext(os.path.basename(piece))[0].lower())
        slug, k = stem, 2
        while slug in used:
            slug, k = f"{stem}-{k}", k + 1
        used.add(slug)
        with tempfile.TemporaryDirectory() as work:
            wav, tape, info = render(piece, opt.genwav, plugins,
                                     opt.seconds, work)
            channels, notes = read_tape(tape)
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", wav,
                            "-map_metadata", "-1", "-c:a", "libmp3lame",
                            "-b:a", "192k",
                            os.path.join(opt.output, "audio", slug + ".mp3")],
                           check=True)
        playing = {c for _, c, _, _, _ in notes}
        for ch in info.get("channels", []):
            if ch.get("engine") in playing and not ch.get("peak"):
                print(f"listenpage: {piece}: {ch['instrument']} plays notes "
                      "and rendered silent (a sample pack not installed?)",
                      file=sys.stderr)
        title = html.escape(info.get("name") or stem)
        about = info.get("description")
        sections.append(
            f'<section id="{slug}"><h2>{title}</h2>'
            + (f"<p>{html.escape(about)}</p>" if about else "")
            + roll(title, info, channels, notes, playing)
            + f'<audio controls preload="none" src="audio/{slug}.mp3">'
            "</audio></section>")

    title = html.escape(opt.title)
    with open(os.path.join(opt.output, "index.html"), "w") as f:
        f.write('<!doctype html><html lang="en"><head><meta charset="utf-8">'
                '<meta name="viewport" content="width=device-width, '
                f'initial-scale=1"><title>{title}</title><style>{CSS}</style>'
                f"</head><body><main><h1>{title}</h1>\n"
                + "\n".join(sections) + "\n</main></body></html>\n")
    print(f"listenpage: {len(sections)} pieces in {opt.output}")


if __name__ == "__main__":
    main()
