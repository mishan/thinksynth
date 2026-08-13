#!/usr/bin/env python3
#
# Copyright (C) 2004-2014 Metaphonic Labs
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

"""Draw src/thinksynth.ico, the icon Windows Explorer and the taskbar show.

The shapes here are the same ones as data/org.thinksynth.thinksynth.svg --
same 128-unit grid, same coordinates, same colours -- because the two want to
look like the same program.  This does not read that file, and the honest
reason is that nothing available here renders it correctly: ImageMagick's
built-in SVG renderer ignores the `url(#bg)' gradient and fills the background
flat black, and librsvg is not installed.  Rather than ship an icon that is
wrong in a way nobody would notice until it was on someone's desktop, the
geometry is written out once more in a form that can actually be drawn.

If the SVG changes, change this too.  There is no build step tying them
together: .ico is a committed binary, because a Windows build should not need
a rasteriser on the machine doing it.

    python3 scripts/make-windows-icon.py
"""

import os
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("this needs Pillow: pip install Pillow")

# The 128-unit grid the SVG uses, drawn at 8x and reduced, which is what gives
# the curves and the rounded corners their antialiasing.
GRID = 128
SCALE = 8
SIZE = GRID * SCALE

BG_TOP = (0x3b, 0x4c, 0xa8)
BG_BOTTOM = (0x24, 0x1c, 0x3a)
WAVE = (0x7e, 0xe0, 0xc8)
CORD = (0xf2, 0xb5, 0x44)

# What Windows actually picks between: 16 and 32 in lists and the taskbar, 48
# in Explorer, 256 for the large-icon view.
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]


def u(v):
    """A coordinate on the SVG's grid, in pixels at the drawing scale."""
    return v * SCALE


def quadratic(p0, p1, p2, steps=160):
    """The points of a quadratic Bezier -- an SVG `Q' segment."""
    out = []

    for i in range(steps + 1):
        t = i / steps
        m = 1.0 - t

        out.append((m * m * p0[0] + 2 * m * t * p1[0] + t * t * p2[0],
                    m * m * p0[1] + 2 * m * t * p1[1] + t * t * p2[1]))

    return out


def reflect(control, about):
    """The implied control point of an SVG `T' segment."""
    return (2 * about[0] - control[0], 2 * about[1] - control[1])


def draw() -> Image.Image:
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))

    # The vertical gradient, a row at a time, on its own layer so the rounded
    # rectangle can mask it. Drawing the gradient and then rounding the
    # corners keeps the corners antialiased; rounding first and filling after
    # does not.
    grad = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    gd = ImageDraw.Draw(grad)

    for y in range(SIZE):
        t = y / (SIZE - 1)
        gd.line([(0, y), (SIZE, y)],
                fill=tuple(int(a + (b - a) * t) + 0
                           for a, b in zip(BG_TOP, BG_BOTTOM)) + (255,))

    mask = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [u(4), u(4), u(124), u(124)], radius=u(26), fill=255)

    img.paste(grad, (0, 0), mask)

    d = ImageDraw.Draw(img)

    # The waveform: M20 58 Q34 20 48 58 T76 58 T104 58
    pts = quadratic((u(20), u(58)), (u(34), u(20)), (u(48), u(58)))

    c = reflect((u(34), u(20)), (u(48), u(58)))
    pts += quadratic((u(48), u(58)), c, (u(76), u(58)))

    c = reflect(c, (u(76), u(58)))
    pts += quadratic((u(76), u(58)), c, (u(104), u(58)))

    d.line(pts, fill=WAVE + (255,), width=u(7), joint="curve")

    # Round caps, which ImageDraw.line does not do on its own.
    for end in (pts[0], pts[-1]):
        r = u(7) / 2.0
        d.ellipse([end[0] - r, end[1] - r, end[0] + r, end[1] + r],
                  fill=WAVE + (255,))

    # The patch cord: M34 98 Q64 76 94 98, at 85% like the SVG's opacity.
    cord = quadratic((u(34), u(98)), (u(64), u(76)), (u(94), u(98)))

    layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    ImageDraw.Draw(layer).line(cord, fill=CORD + (217,), width=u(5),
                               joint="curve")
    img.alpha_composite(layer)

    # And the three patch points it joins.
    for x in (34, 64, 94):
        d.ellipse([u(x - 9), u(98 - 9), u(x + 9), u(98 + 9)],
                  fill=CORD + (255,))

    return img


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, os.pardir, "src", "thinksynth.ico")

    img = draw()

    # Saved from the full-resolution drawing, not from a frame of it. Handing
    # Pillow the 16x16 and asking for sizes up to 256 does not fail -- it
    # silently writes a file containing only the 16x16, because it will not
    # scale an image up. The result looked like an icon right up until
    # Explorer wanted a large one.
    #
    # `sizes' lets Pillow do the reductions from the 1024px source, so every
    # frame comes from the drawing rather than from the frame above it.
    img.save(os.path.normpath(out), format="ICO",
             sizes=[(s, s) for s in ICO_SIZES])

    print("wrote %s (%s)" % (os.path.normpath(out),
                             ", ".join("%dx%d" % (s, s) for s in ICO_SIZES)))


if __name__ == "__main__":
    main()
