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

#ifndef TH_MATH_H
#define TH_MATH_H 1

#include <cmath>

/* M_PI, and the one place that has to know it might not be there.
 *
 * It is not in C++ at all. glibc provides it from <math.h> and g++ always
 * defines _GNU_SOURCE, so on Linux it simply appears and every file that
 * uses it looks fine. MinGW's UCRT hides it behind _USE_MATH_DEFINES, so
 * on Windows the same file does not compile -- and the failure lands on
 * whoever happened to add the newest cairo_arc, months after the rule was
 * learned and in a file that has nothing to do with it.
 *
 * That has now happened four times. euclid.cpp, lsystem.cpp and
 * ComposerCanvas.cpp each carry an identical three-line shim with an
 * identical comment above it, morph.cpp did not and broke the Windows
 * build, and NodeCanvas.cpp gets away with no shim only because gtkmm
 * drags a definition in behind it -- which is luck, not a decision.
 *
 * Two answers, deliberately both:
 *
 *   - The build defines _USE_MATH_DEFINES for every target (see the top
 *     level CMakeLists.txt). That is the root cause, and it fixes every
 *     file in the tree at once, including the ones nobody has written
 *     yet. Nothing has to remember anything.
 *   - This header, for the belt. Plugins are meant to be buildable out of
 *     tree -- that is the whole point of them linking libthink like any
 *     other consumer -- and a plugin built outside this project's CMake
 *     gets none of its compile definitions. Including this is how a file
 *     says "I need pi" once, instead of copying the shim a fifth time.
 *
 * Deliberately not a TH_PI constant to convert every call site to: there
 * are around twenty uses of M_PI across the oscillators, the canvases and
 * the composer draws, they all read correctly, and a rename touching all
 * of them would be a bigger change than the bug. What was missing was a
 * place to put the fallback, not a different spelling.
 */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#endif /* TH_MATH_H */
