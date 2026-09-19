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
 * twdraw -- the module's recorder, and the three tables a drawing is.
 *
 * What comes out of a draw is not pixels but a list of ops, its strings
 * and the image surfaces it blits (cairo-canvas2d). The page reads the three
 * out of the heap and replays them on a Canvas2D with replay.js. Every
 * drawing in this module -- a composer's picture, the composer canvas, the
 * node canvas -- goes through here, so the page reads them all the same
 * way.
 */

#include "twdraw.h"

#include <emscripten.h>

#include "cairo2d.h"

namespace {

cairo_t *drawing_ = NULL;

} /* namespace */

cairo_t *twDrawingBegin (void)
{
    if (drawing_ == NULL)
        drawing_ = cairo2d_create();

    cairo2d_begin(drawing_);

    return drawing_;
}

cairo_t *twDrawing (void)
{
    return drawing_;
}

extern "C" {

/* The list the last draw recorded, as a pointer into HEAPF32 and a length
   in floats. cairo-canvas2d's replay.js is what reads it. */
EMSCRIPTEN_KEEPALIVE const float *tw_draw_ops (void)
{
    return drawing_ != NULL ? cairo2d_ops(drawing_) : NULL;
}

EMSCRIPTEN_KEEPALIVE int tw_draw_words (void)
{
    return drawing_ != NULL ? cairo2d_op_words(drawing_) : 0;
}

EMSCRIPTEN_KEEPALIVE int tw_draw_string_count (void)
{
    return drawing_ != NULL ? cairo2d_string_count(drawing_) : 0;
}

EMSCRIPTEN_KEEPALIVE const char *tw_draw_string (int k)
{
    const char *s = drawing_ != NULL ? cairo2d_string(drawing_, k) : NULL;

    return s != NULL ? s : "";
}

/* The image surfaces the list blits by reference -- a spectrogram is what
   they are for. */
EMSCRIPTEN_KEEPALIVE int tw_draw_surface_count (void)
{
    return drawing_ != NULL ? cairo2d_surface_count(drawing_) : 0;
}

EMSCRIPTEN_KEEPALIVE const unsigned char *tw_draw_surface_data (int k)
{
    return drawing_ != NULL ? cairo2d_surface_data(drawing_, k) : NULL;
}

EMSCRIPTEN_KEEPALIVE int tw_draw_surface_width (int k)
{
    return drawing_ != NULL ? cairo2d_surface_width(drawing_, k) : 0;
}

EMSCRIPTEN_KEEPALIVE int tw_draw_surface_height (int k)
{
    return drawing_ != NULL ? cairo2d_surface_height(drawing_, k) : 0;
}

EMSCRIPTEN_KEEPALIVE int tw_draw_surface_stride (int k)
{
    return drawing_ != NULL ? cairo2d_surface_stride(drawing_, k) : 0;
}

} /* extern "C" */
