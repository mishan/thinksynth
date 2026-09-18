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
 * Everything a visual module includes, included once, at file scope, ahead
 * of the module. thinkstatic.h and thcstatic.h, for the other two ABIs.
 *
 * The visuals are compiled into the one module the same way the DSP
 * plugins and the composers are, each inside a namespace of its own, so a
 * header first seen inside that namespace would drag std:: in with it.
 * Included here first, every one is behind its guard by the time the
 * module's own #include lines are reached.
 *
 * Their exports are `extern "C"', which is a linkage and not a scope, so
 * each is renamed with a #define ahead of the include exactly as a
 * composer's is; wasm/web/cmake/ThinkPlugin.cmake writes the renames and
 * the table looks the module up under them.
 *
 * VISUAL_PLUGIN_BUILD is deliberately not defined, for the same reason
 * COMPOSER_PLUGIN_BUILD is not in thcstatic.h: it declares the exports and
 * defines the version byte, and a rename would have to reach those
 * declarations too. The definitions in the module stand on their own, and
 * the version byte a compiled-in visual is gated on is the table's.
 */

#ifndef TH_WEB_STATIC_VISUAL_H
#define TH_WEB_STATIC_VISUAL_H 1

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <new>

/* The cairo a visual draws through is wasm/cairo2d, which records rather
   than rasterises (JAM_M6.md, section 3). */
#include <cairo.h>

#include "thVisual.h"

#endif /* TH_WEB_STATIC_VISUAL_H */
