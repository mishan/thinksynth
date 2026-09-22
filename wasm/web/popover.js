/*
 * Copyright (C) 2004-2026 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * popover.js -- a box beside a thing, and inside the window.
 *
 * Its own file rather than the tiler's, which is where it was: what it
 * knows about is the window's edges, and it was in panes.js because that
 * is where the bug it fixes was found. A layout that happens to have
 * discovered a clamp is not the owner of one.
 *
 * What the tiler does own is where a popover goes in the tree --
 * `createPanes(...).overlay()', over every pane rather than inside one,
 * because a pane is a box that scrolls and would clip it. That is a fact
 * about tiling and stays there. This is arithmetic.
 */

/*
 * A popover at a page coordinate, held inside the window.
 *
 * What asks for one of these is a canvas, and what it says is where in
 * its own pixels -- so the page adds where the canvas is and gets a page
 * coordinate, which is what these are positioned in and has not changed
 * with tiling. What has changed is what is around them: a pane can be
 * narrower than the popover's own maximum width and is a box that
 * scrolls, so one placed beside a handle near the right of a pane went
 * off the window rather than merely off the box. It is a bug that was
 * there before and that a 60em document rarely showed.
 *
 * Shown first and measured after, because a popover's size is what is in
 * it and what is in it was just written.
 */
export function placePopover (box, x, y)
{
    const pad = 8;

    box.hidden = false;

    const left = Math.max(scrollX + pad,
                          Math.min(x, scrollX + innerWidth -
                                      box.offsetWidth - pad));
    const top = Math.max(scrollY + pad,
                         Math.min(y, scrollY + innerHeight -
                                     box.offsetHeight - pad));

    box.style.left = `${left}px`;
    box.style.top = `${top}px`;
}
