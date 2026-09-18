/*
 * cairo2d -- the host's side of the stand-in: making a recorder, and
 * reading the list back out of it.
 *
 * cairo.h is what the drawing code sees, and it sees nothing of this. A
 * host makes a cairo_t with cairo2d_create(), hands it to whatever draws,
 * and then reads three tables out of it: the ops, the strings the ops
 * refer to by index, and the image surfaces they refer to by index. Then
 * cairo2d_begin() again for the next frame, which keeps the buffers and
 * their capacity.
 *
 * THE LIST. One flat array of float. A record is an opcode followed by
 * exactly cairo2d_op_arity() operands -- no length prefix, because the
 * arity table is the contract and a replayer that disagrees with it is
 * wrong about more than the length. The one variable-length op,
 * CAIRO2D_SET_DASH, has an arity of -1 and says its own count first:
 * [op, n, d0 .. dn-1, offset].
 *
 * Indices, not pointers, for strings and surfaces. A float holds integers
 * exactly only to 2^24, and a heap pointer above sixteen megabytes would
 * land silently on the wrong byte.
 *
 * Public domain, or CC0 where that is not a thing. Take it.
 */

#ifndef CAIRO2D_H
#define CAIRO2D_H

#include "cairo.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The opcodes. replay.js has this list too, and cairo2d_op_name() is how
   a test holds the two against each other. */
enum {
    CAIRO2D_SAVE = 1,
    CAIRO2D_RESTORE,
    CAIRO2D_TRANSLATE,          /* tx ty                                 */
    CAIRO2D_SCALE,              /* sx sy                                 */

    CAIRO2D_NEW_PATH,
    CAIRO2D_NEW_SUB_PATH,
    CAIRO2D_MOVE_TO,            /* x y                                   */
    CAIRO2D_LINE_TO,            /* x y                                   */
    CAIRO2D_CURVE_TO,           /* x1 y1 x2 y2 x3 y3                     */
    CAIRO2D_ARC,                /* xc yc r a1 a2                         */
    CAIRO2D_RECTANGLE,          /* x y w h                               */
    CAIRO2D_CLOSE_PATH,

    CAIRO2D_SET_SOURCE_RGBA,    /* r g b a                               */
    CAIRO2D_SET_SOURCE_SURFACE, /* surface index, x, y                   */
    CAIRO2D_SET_LINE_WIDTH,     /* width                                 */
    CAIRO2D_SET_LINE_CAP,       /* cairo_line_cap_t                      */
    CAIRO2D_SET_DASH,           /* n, d0 .. dn-1, offset                 */
    CAIRO2D_SET_FILTER,         /* 1 to smooth, 0 not                    */

    CAIRO2D_FILL,
    CAIRO2D_FILL_PRESERVE,
    CAIRO2D_STROKE,
    CAIRO2D_STROKE_PRESERVE,
    CAIRO2D_PAINT,
    CAIRO2D_CLIP,

    CAIRO2D_SET_FONT,           /* string index: a CSS font shorthand    */
    CAIRO2D_SHOW_TEXT,          /* string index, x, y                    */

    CAIRO2D_OP_MAX
};

/* ---- a recorder ------------------------------------------------------ */

cairo_t *cairo2d_create (void);
void cairo2d_destroy (cairo_t *cr);

/* Start a list. Empties the three tables and returns the context to its
   initial state -- identity transform, black, line width 1, no dash, the
   default font -- as a fresh cairo_t would be. */
void cairo2d_begin (cairo_t *cr);

/* ---- reading it back ------------------------------------------------- */

const float *cairo2d_ops (const cairo_t *cr);
int cairo2d_op_words (const cairo_t *cr);      /* floats, not records     */

int cairo2d_string_count (const cairo_t *cr);
const char *cairo2d_string (const cairo_t *cr, int index);

/* A surface referred to by CAIRO2D_SET_SOURCE_SURFACE. Its pixels are
   where the drawing code wrote them -- the list blits by reference -- and
   they stay valid until the next cairo2d_begin(), whatever the drawing
   code did with its own reference. */
int cairo2d_surface_count (const cairo_t *cr);
const unsigned char *cairo2d_surface_data (const cairo_t *cr, int index);
int cairo2d_surface_width (const cairo_t *cr, int index);
int cairo2d_surface_height (const cairo_t *cr, int index);
int cairo2d_surface_stride (const cairo_t *cr, int index);

/* ---- the op table, for a replayer or a test -------------------------- */

/* NULL for an opcode this build does not have. */
const char *cairo2d_op_name (int op);

/* How many operands follow the opcode; -1 for CAIRO2D_SET_DASH, which
   says its own length, and -2 for an opcode this build does not have. */
int cairo2d_op_arity (int op);

/* ---- text measurement ------------------------------------------------ */

/* What answers cairo_text_extents(). `font' is the CSS font shorthand the
   recorder will also put in the list, so that a label is laid out from the
   metrics of the font it is then drawn with. Fill in six doubles --
   x_bearing, y_bearing, width, height, x_advance, y_advance -- and return
   non-zero; return zero to say the host cannot measure, and the built-in
   estimate is used instead.
 *
 * Under Emscripten the default asks the browser for measureText() on an
 * offscreen 2D context, synchronously, which works on the main thread and
 * in a worker and is exactly what the replayer will draw with. Anywhere
 * else -- Node, a native test -- there is no canvas to ask, and the
 * estimate is what there is.
 */
typedef int (*cairo2d_measure_fn) (const char *font, const char *utf8,
                                   double *extents);

void cairo2d_set_measure (cairo2d_measure_fn fn);

#ifdef __cplusplus
}
#endif

#endif /* CAIRO2D_H */
