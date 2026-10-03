#!/usr/bin/env python3
#
# Copyright (C) 2004-2026 The thinksynth authors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.

"""screenshots.py -- the desktop pictures the README shows.

    scripts/screenshots.py [BUILD_DIR] [OUT_DIR]

BUILD_DIR is a native build (build/), OUT_DIR where the pictures go
(docs/screenshots/). The browser's are wasm/web/screenshots.mjs's.

Taken with shotbox (https://github.com/mishan/shotbox) in a sealed
session: its own X display, D-Bus and scratch home, so the window comes
up at a first run's size and layout, with GTK's own defaults rather than
whoever runs this. Its Python module has to be importable: a checkout's
root on PYTHONPATH.

The audio is a real stream, into scripts/headless.sh's null sink, since a
piece's transport is clocked by the device: -d none would draw a piano
roll with nothing on it. This runs itself under headless.sh for that.

  desktop-patch.png   patch mode on a first run: the channels, SuperRes's
                      graph, its parameters and the keys
  desktop-piece.png   piece mode, with mirrorball.gen half a minute in:
                      the canvas, the piece's settings and the roll
  desktop-sequence.png
                      sequence mode, with scratch.gen's tracks playing
                      and the roll under them
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(HERE)

# Under headless.sh unless already there: its PipeWire is what the piece
# plays into.
if not os.environ.get("THINK_HEADLESS"):
    os.execv(os.path.join(HERE, "headless.sh"),
             [os.path.join(HERE, "headless.sh"), sys.executable,
              os.path.abspath(__file__)] + sys.argv[1:])

try:
    import shotbox
except ImportError:
    sys.exit("screenshots.py: no shotbox module; put a checkout of "
             "https://github.com/mishan/shotbox on PYTHONPATH")

BUILD = os.path.abspath(sys.argv[1] if len(sys.argv) > 1
                        else os.path.join(TOP, "build"))
OUT = os.path.abspath(sys.argv[2] if len(sys.argv) > 2
                      else os.path.join(TOP, "docs", "screenshots"))

WINDOW = "thinksynth"
BROWSER = "Open piece"

# A screen with room around a first run's 1440x900.
SCREEN = (1600, 1000)

# Long enough for the roll to have a few bars of every part on it, in
# either piece.
PLAY_SECONDS = 30

# Play: the first of the transport's buttons, after the three modes'.
PLAY = (275, 27)


def session():
    return shotbox.Session(size=SCREEN, passthrough=("PULSE_SERVER",),
                           failed=os.path.join(OUT, "failed.png"))


def start(s, log):
    s.spawn([os.path.join(BUILD, "src", "thinksynth"), "-d", "pulse"],
            log=os.path.join(OUT, log))
    s.wait_window(WINDOW)
    s.wait_stable(window=WINDOW)


def patch():
    with session() as s:
        start(s, "desktop-patch.log")
        s.capture(os.path.join(OUT, "desktop-patch.png"), window=WINDOW,
                  park=True)


def piece(name):
    with session() as s:
        start(s, "desktop-piece.log")

        # Piece mode, then the menu's Open Piece... by its mnemonic.
        s.key("ctrl+2")
        s.wait_stable(window=WINDOW)
        s.key("F10")
        s.wait_stable()
        s.key("o")
        s.wait_window(BROWSER)
        s.wait_stable(window=BROWSER)

        # The filter, then the one piece it leaves. A click first: there is
        # no window manager to give the dialog the keyboard.
        s.click(230, 22, window=BROWSER)
        s.type(name)
        s.wait_stable(window=BROWSER)
        s.click(50, 80, window=BROWSER, double=True)
        s.wait_stable(window=WINDOW)

        s.click(*PLAY, window=WINDOW)
        time.sleep(PLAY_SECONDS)
        s.capture(os.path.join(OUT, "desktop-piece.png"), window=WINDOW,
                  park=True)


def sequence():
    with session() as s:
        start(s, "desktop-sequence.log")

        # Sequence mode opens on its own copy of scratch.gen.
        s.key("ctrl+3")
        s.wait_stable(window=WINDOW)

        s.click(*PLAY, window=WINDOW)
        time.sleep(PLAY_SECONDS)
        s.capture(os.path.join(OUT, "desktop-sequence.png"), window=WINDOW,
                  park=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    patch()
    piece("mirrorball")
    sequence()

    for log in ("desktop-patch.log", "desktop-piece.log",
                "desktop-sequence.log"):
        try:
            os.remove(os.path.join(OUT, log))
        except OSError:
            pass


if __name__ == "__main__":
    main()
