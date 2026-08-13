#!/usr/bin/env python3
#
# Copyright (C) 2004-2026 Metaphonic Labs
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

"""Render src/thinksynth.ico from the application icon.

There is one icon in this tree, data/org.thinksynth.thinksynth.svg.  Linux
installs it as-is into hicolor/scalable and the Flatpak picks it up from
there; Windows cannot use an SVG, so this renders that same file down into the
.ico that src/thinksynth.rc.in embeds in the executable.  One source, two
outputs.

    python3 scripts/make-windows-icon.py

Two things about how it does that are worth knowing.

Rasteriser.  gdk-pixbuf's SVG loader, which is librsvg, reached through
PyGObject -- so the icon is rendered by the same library GTK itself would use
to draw it, and nothing new has to be installed to run this.  ImageMagick is
not an option even though it is more likely to be on hand: its built-in SVG
renderer ignores the `url(#bg)' gradient and fills the background flat black,
which is wrong in a way nobody notices until the icon is on someone's desktop.

Scaling.  The SVG is rendered once at 1024px and each frame is reduced from
that, rather than rendering the vector separately at 16, 24, 32 and so on.
Asking librsvg for a 16px render of a 128-unit drawing gives a soft halo around
the rounded corners and a waveform so pale it nearly disappears; reducing a
large render keeps both crisp.  Compared side by side before choosing.

The .ico is committed rather than built, so a Windows build needs no
rasteriser on the machine doing it.  That leaves the two able to drift, which
is what the stamp file is for: it records the hash of the SVG this icon came
from, and ctest fails if the SVG has moved on without the icon being
regenerated.  See cmake/CheckIconStamp.cmake.

The stamp deliberately hashes the input rather than the output.  Byte-comparing
a freshly rendered .ico against the committed one would be a stricter check and
an unusable one -- librsvg's antialiasing differs between versions, so it would
fail on whichever machine happened to have a different one, which is not the
mistake anybody is trying to catch.
"""

import hashlib
import io
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("this needs Pillow: pip install Pillow")

# Relative to the repository root, which is this script's parent.
SVG = os.path.join("data", "org.thinksynth.thinksynth.svg")
ICO = os.path.join("src", "thinksynth.ico")
STAMP = os.path.join("src", "thinksynth-icon.stamp")

# What Windows picks between: 16 and 32 in lists and the taskbar, 48 in
# Explorer, 256 for the large-icon view.  The sizes in between cost a few
# kilobytes and stop Windows scaling one of these itself.
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]

# Big enough that every size above is an integer-friendly reduction of it, and
# big enough that the reduction is doing the antialiasing rather than librsvg.
RENDER = 1024


def render (path, size):
    """The SVG at `size' square, as an RGBA image, by way of librsvg."""

    try:
        import gi
        gi.require_version("GdkPixbuf", "2.0")
        from gi.repository import GdkPixbuf
    except (ImportError, ValueError) as e:
        sys.exit("this needs PyGObject and gdk-pixbuf's SVG loader "
                 "(Debian: python3-gi, librsvg2-common): %s" % e)

    pb = GdkPixbuf.Pixbuf.new_from_file_at_size(path, size, size)

    if pb is None:
        sys.exit("could not render %s" % path)

    # An SVG loader that silently declined the gradient would still hand back a
    # pixbuf, so check the one property that distinguishes a real render:
    # transparent outside the rounded rectangle.
    if not pb.get_has_alpha():
        sys.exit("%s rendered without an alpha channel -- the SVG loader in "
                 "use is not librsvg" % path)

    return Image.frombytes("RGBA", (pb.get_width(), pb.get_height()),
                           bytes(pb.get_pixels()), "raw", "RGBA",
                           pb.get_rowstride(), 1)


def main ():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir)
    os.chdir(os.path.normpath(root))

    if not os.path.isfile(SVG):
        sys.exit("no %s -- run this from anywhere, but the tree has to be "
                 "intact" % SVG)

    big = render(SVG, RENDER)

    frames = [big.resize((s, s), Image.LANCZOS) for s in ICO_SIZES]

    # Pillow's ICO writer will happily reduce a single image to every size
    # asked for, but it will not enlarge one: handed the 16x16 and a sizes=
    # list up to 256 it writes a file containing only the 16x16, without
    # complaint.  Passing the frames explicitly means each one is the frame
    # that gets written.
    frames[-1].save(ICO, format="ICO",
                    sizes=[(s, s) for s in ICO_SIZES],
                    append_images=frames[:-1])

    with open(SVG, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()

    with open(STAMP, "w") as f:
        f.write("# sha256 of %s, the source %s was rendered from.\n"
                "#\n"
                "# If ctest reports this as stale, the icon has not been\n"
                "# regenerated since the SVG changed:\n"
                "#\n"
                "#     python3 scripts/make-windows-icon.py\n"
                "#\n"
                "# and commit both files together.\n"
                "%s\n" % (SVG, ICO, digest))

    print("wrote %s (%s)" % (ICO, ", ".join("%dx%d" % (s, s)
                                            for s in ICO_SIZES)))
    print("wrote %s (%s)" % (STAMP, digest))


if __name__ == "__main__":
    main()
