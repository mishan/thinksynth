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

#ifndef GTH_CONSOLE_H
#define GTH_CONSOLE_H

/* Put stdout and stderr back where the user can see them, on Windows.
 *
 * thinksynth is linked as a GUI-subsystem program there, so Windows does not
 * give it a console -- which is the point: double-clicking a synth should not
 * open a terminal alongside it. The cost of that is the other half, because a
 * GUI-subsystem program run *from* a terminal prints into nothing, and every
 * diagnostic this program has is a printf.
 *
 * So: if a console launched us, attach to it. If one did not, do nothing and
 * stay silent. Call it first thing in main(), before anything prints.
 *
 * A no-op everywhere else. Linux and macOS have no subsystem distinction and
 * never lost their streams in the first place.
 */
void gthAttachConsole (void);

#endif /* GTH_CONSOLE_H */
