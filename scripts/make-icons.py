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

"""Render the platform icon files from the application icon.

There is one icon in this tree, data/org.thinksynth.thinksynth.svg.  Linux
installs it as-is into hicolor/scalable and the Flatpak picks it up from there.
Neither of the other two platforms can use an SVG, so this renders that same
file into the containers they want:

    src/thinksynth.ico    embedded in the executable by src/thinksynth.rc.in
    src/thinksynth.icns   copied into thinksynth.app/Contents/Resources

One source, three platforms.

    python3 scripts/make-icons.py

Four things about how it does that are worth knowing.

Rasteriser.  gdk-pixbuf's SVG loader, which is librsvg, reached through
PyGObject -- so the icon is rendered by the same library GTK itself would use
to draw it, and nothing new has to be installed to run this.  ImageMagick is
not an option even though it is more likely to be on hand: its built-in SVG
renderer ignores the `url(#bg)' gradient and fills the background flat black,
which is wrong in a way nobody notices until the icon is on someone's desktop.

Scaling.  The SVG is rendered once at 1024px and every frame is reduced from
that, rather than rendering the vector separately at each size.  Asking librsvg
for a 16px render of a 128-unit drawing gives a soft halo around the rounded
corners and a waveform so pale it nearly disappears; reducing a large render
keeps both crisp.  Compared side by side before choosing.

Committed, not built.  Both files are checked in, so building for Windows or
macOS needs no rasteriser on the machine doing it.  The price is that they can
go stale silently -- nothing about editing the SVG makes an out-of-date render
stop working -- so this also writes src/thinksynth-icon.stamp recording the
hash of the SVG they came from, and ctest fails if the SVG has moved on.  See
cmake/CheckIconStamp.cmake.

Checked, not assumed.  The render is sampled before anything is written, and
the script refuses to produce icons from a drawing whose corners are opaque or
whose background has no gradient -- see check() for why that particular pair.

The stamp deliberately hashes the input rather than the outputs.  Byte-comparing
a freshly rendered icon against the committed one would be a stricter check and
an unusable one -- librsvg's antialiasing differs between versions, so it would
fail on whichever machine happened to have a different one, which is not the
mistake anybody is trying to catch.
"""

import hashlib
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("this needs Pillow: pip install Pillow")

# Relative to the repository root, which is this script's parent.
SVG = os.path.join("data", "org.thinksynth.thinksynth.svg")
ICO = os.path.join("src", "thinksynth.ico")
ICNS = os.path.join("src", "thinksynth.icns")
STAMP = os.path.join("src", "thinksynth-icon.stamp")

# What Windows picks between: 16 and 32 in lists and the taskbar, 48 in
# Explorer, 256 for the large-icon view.  The sizes in between cost a few
# kilobytes and stop Windows scaling one of these itself.
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]

# What Pillow's ICNS writer emits, which is the modern PNG-based set:
# ic07/08/09/10 at 128/256/512/1024, and ic11/12/13/14 at 32/64/256/512 for the
# @2x slots.  Supplying them explicitly means every chunk is a reduction of the
# 1024px render rather than of whichever frame Pillow was handed.
#
# There is no 16x16 @1x chunk (icp4) -- Pillow does not write one.  For a 16pt
# slot on a non-Retina display macOS reduces the 32px ic11 itself, which is the
# same reduction this script would have done.
ICNS_SIZES = [32, 64, 128, 256, 512, 1024]

# Big enough that every size above is an integer-friendly reduction of it, and
# big enough that the reduction is doing the antialiasing rather than librsvg.
RENDER = 1024


def render(path, size):
    """The SVG at `size' square, as an RGBA image, by way of librsvg."""

    try:
        import gi
        gi.require_version("GdkPixbuf", "2.0")
        from gi.repository import GdkPixbuf, GLib
    except (ImportError, ValueError) as e:
        sys.exit("this needs PyGObject and gdk-pixbuf's SVG loader "
                 "(Debian: python3-gi, librsvg2-common): %s" % e)

    try:
        pb = GdkPixbuf.Pixbuf.new_from_file_at_size(path, size, size)
    except GLib.Error as e:
        sys.exit("could not render %s -- is gdk-pixbuf's SVG loader "
                 "installed? (Debian: librsvg2-common): %s" % (path, e))

    return Image.frombytes("RGBA", (pb.get_width(), pb.get_height()),
                           bytes(pb.get_pixels()), "raw", "RGBA",
                           pb.get_rowstride(), 1)


def check(img):
    """Refuse to write icons from a render that is visibly not the drawing.

    Worth doing because the failure this guards against does not look like a
    failure.  ImageMagick renders this SVG at the right size, with an alpha
    channel, and every shape in the right place -- and fills the background
    flat black, because it ignores the `url(#bg)' gradient.  Nothing about the
    file says so; you have to look at it.  The icon was wrong for a while on
    that basis.

    So it samples three points and asks the two questions that separate a
    correct render from that one: are the corners outside the rounded
    rectangle transparent, and is the background a gradient rather than one
    flat colour.

    Points are given as fractions of the image, so the same three work at any
    render size, and they are read off the SVG's 128-unit grid: a corner
    outside the rounded rect, and two background points near the top and
    bottom of it, both on the centre line where neither the waveform nor the
    patch cord crosses.

    Deliberately not checked: the specific colours.  Testing against #3b4ca8
    and #241c3a would catch more, and would also mean that recolouring the
    artwork breaks this script for no reason.  What is caught is a renderer
    that flattens or drops the gradient, which is the mistake that actually
    happened.
    """

    w, h = img.size

    def at(fx, fy):
        return img.getpixel((int(w * fx), int(h * fy)))

    corner = at(0.02, 0.02)
    top = at(0.5, 0.078)
    bottom = at(0.5, 0.922)

    if corner[3] != 0:
        sys.exit("render is opaque at the corner %s -- the rounded rectangle "
                 "did not clip, so this is not the drawing" % (corner,))

    # One test rather than two: a renderer that dropped the gradient filled the
    # background with something flat, and whether that something was black is
    # beside the point.  An earlier draft also checked for near-black, which
    # only fired for a gradient that was itself almost black -- a branch with
    # no plausible way to reach it.
    spread = sum(abs(a - b) for a, b in zip(top[:3], bottom[:3]))

    if spread < 30:
        sys.exit("render has a flat background (top %s, bottom %s) -- the "
                 "renderer ignored the gradient"
                 % (top[:3], bottom[:3]))


def svg_digest(path):
    """The SVG's hash, with line endings normalised first.

    Normalised because the SVG is a text file and git is entitled to rewrite
    its line endings on checkout -- GitHub's Windows runners set
    core.autocrlf, so the file that arrives there is byte-for-byte different
    from the one committed while describing exactly the same drawing.  Hashing
    the raw bytes asks "have these bytes changed?"; the question worth asking
    is "has the artwork changed?", and the answer must not depend on which
    platform is asking.

    cmake/CheckIconStamp.cmake does the same replacement before hashing, and
    the two are checked against each other.
    """

    with open(path, "rb") as f:
        return hashlib.sha256(f.read().replace(b"\r\n", b"\n")).hexdigest()


def frames(big, sizes):
    return [big.resize((s, s), Image.LANCZOS) for s in sizes]


def write_ico(big):
    f = frames(big, ICO_SIZES)

    # Pillow's ICO writer will happily reduce a single image to every size
    # asked for, but it will not enlarge one: handed the 16x16 and a sizes=
    # list up to 256 it writes a file containing only the 16x16, without
    # complaint.  Passing the frames explicitly means each one is the frame
    # that gets written.
    f[-1].save(ICO, format="ICO", sizes=[(s, s) for s in ICO_SIZES],
               append_images=f[:-1])


def write_icns(big):
    # Pillow matches append_images to its chunk table by width, and resizes the
    # base image itself for any size it was not given.
    f = frames(big, ICNS_SIZES)

    f[-1].save(ICNS, format="ICNS", append_images=f[:-1])


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir)
    os.chdir(os.path.normpath(root))

    if not os.path.isfile(SVG):
        sys.exit("no %s -- run this from anywhere, but the tree has to be "
                 "intact" % SVG)

    big = render(SVG, RENDER)
    check(big)

    write_ico(big)
    write_icns(big)

    digest = svg_digest(SVG)

    with open(STAMP, "w") as f:
        f.write("# sha256 of %s, the source these were rendered from:\n"
                "#\n"
                "#     %s\n"
                "#     %s\n"
                "#\n"
                "# If ctest reports this as stale, they have not been\n"
                "# regenerated since the SVG changed:\n"
                "#\n"
                "#     python3 scripts/make-icons.py\n"
                "#\n"
                "# and commit all three files together.\n"
                "%s\n" % (SVG, ICO, ICNS, digest))

    print("wrote %s (%s)"
          % (ICO, ", ".join("%dx%d" % (s, s) for s in ICO_SIZES)))
    print("wrote %s (%s)"
          % (ICNS, ", ".join("%dx%d" % (s, s) for s in ICNS_SIZES)))
    print("wrote %s (%s)" % (STAMP, digest))


if __name__ == "__main__":
    main()
