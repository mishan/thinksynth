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
 * The module's one drawing, and the tables the page reads it out of.
 *
 * Everything drawn here is recorded rather than rasterised (wasm/cairo2d):
 * a composer's picture, the composer canvas around it, the node canvas.
 * One recorder for the module, because nothing draws two things at once --
 * the list is read out before the next draw starts -- and because the page
 * then has one set of accessors to read any of them with (tw_draw_ops and
 * its neighbours, in twdraw.cpp).
 */

#ifndef TW_DRAW_H
#define TW_DRAW_H 1

#include "cairo.h"

/* A fresh list. The recorder is made on the first call, so an instance
   that never draws -- the worklet's -- carries the code and allocates
   nothing. */
cairo_t *twDrawingBegin (void);

/* The list as it stands, for the accessors and for whoever wants to know
   how long it is. NULL before the first draw. */
cairo_t *twDrawing (void);

#endif /* TW_DRAW_H */
