/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#ifndef DSP_CATEGORIES_H
#define DSP_CATEGORIES_H 1

#include <stddef.h>

/* Where a shipped graph may be filed.
 *
 * The groups mostly fall out of the description lines, and the arguable ones
 * are arguable in one direction each: a plucked string is its own thing
 * rather than a lead, and a graph promoted out of the old drawers because it
 * was interesting rather than because it was useful is an experiment and
 * says so. Effects is dsp/fx/, which is the one place a directory already
 * carries meaning the format relies on.
 *
 * A list and not an enum. `category' is free text in the grammar; this is the
 * discipline the shipped corpus is held to (scripts/dspcheck) and what the
 * node editor offers, and nothing else, which is the difference between a
 * category and a schema. See docs/DSP_FORMAT.md "Choosing a file". */
static const char *const thDspCategories[] = {
    "Bass", "Drums", "Effects", "Experiments", "Keys", "Leads and stabs",
    "Plucked", "Strings and pads", "Synths", NULL
};

#endif /* DSP_CATEGORIES_H */
