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
 * layouts.js -- a pane layout kept across a rename of its store.
 *
 * The tiler keeps each mode's layout in localStorage under
 * `STORE:MODE'. Renaming a store would cost everybody the layout they
 * left and leave the old entry behind for good, so before the tiler
 * reads anything, a mode with nothing under the new name takes what is
 * under the old one, and the old one is deleted.
 */

export function moveLayouts (from, to, modes)
{
    try
    {
        for (const mode of modes)
        {
            const old = `${from}:${mode}`;
            const text = localStorage.getItem(old);

            if (text === null)
                continue;

            if (localStorage.getItem(`${to}:${mode}`) === null)
                localStorage.setItem(`${to}:${mode}`, text);

            localStorage.removeItem(old);
        }
    }
    catch
    {
        /* No storage, or none to spare: the defaults, as with no
           layout saved at all. */
    }
}
