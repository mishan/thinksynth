/*
 * cairo2d -- cairo's API, recorded and replayed on a Canvas2D.
 *
 * This is not cairo. It is a header that answers to cairo's name for the
 * corner of cairo that a program which draws diagrams uses: paths, fills,
 * strokes, the toy text API, a transform, a clip, and one image surface.
 * A cairo_t here is a display list under construction; every call below
 * appends to it, and the host reads the list out and replays it on a
 * CanvasRenderingContext2D, whose vocabulary is the same one.
 *
 * What it is for: C and C++ that draws through cairo on the desktop, and
 * is compiled to WebAssembly to draw the same picture in a browser. Real
 * cairo compiles to wasm and brings pixman and freetype with it, which for
 * this vocabulary is a megabyte to draw rectangles; drawing the same
 * picture a second time in JavaScript is two versions of everything a
 * person looks at.
 *
 * What is deliberately missing: everything else. A program that reaches
 * for cairo_set_line_join or a PDF surface does not compile, in the line
 * that reached, rather than linking and drawing nothing. The list below is
 * the contract, and it grows when a caller needs it to and never by
 * accident.
 *
 * See README.md for the shape of the list and how the replayer reads it.
 *
 * Public domain, or CC0 where that is not a thing. Take it.
 */

#ifndef CAIRO_H
#define CAIRO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The tag names are cairo's, so that a header which forward-declares
   `struct _cairo' the way cairo's users do agrees with this one. */
typedef struct _cairo cairo_t;
typedef struct _cairo_surface cairo_surface_t;
typedef struct _cairo_pattern cairo_pattern_t;

typedef enum _cairo_status {
    CAIRO_STATUS_SUCCESS = 0,
    CAIRO_STATUS_NO_MEMORY = 1
} cairo_status_t;

typedef enum _cairo_format {
    CAIRO_FORMAT_ARGB32 = 0,
    CAIRO_FORMAT_RGB24  = 1
} cairo_format_t;

typedef enum _cairo_font_slant {
    CAIRO_FONT_SLANT_NORMAL  = 0,
    CAIRO_FONT_SLANT_ITALIC  = 1,
    CAIRO_FONT_SLANT_OBLIQUE = 2
} cairo_font_slant_t;

typedef enum _cairo_font_weight {
    CAIRO_FONT_WEIGHT_NORMAL = 0,
    CAIRO_FONT_WEIGHT_BOLD   = 1
} cairo_font_weight_t;

typedef enum _cairo_line_cap {
    CAIRO_LINE_CAP_BUTT   = 0,
    CAIRO_LINE_CAP_ROUND  = 1,
    CAIRO_LINE_CAP_SQUARE = 2
} cairo_line_cap_t;

/* Canvas2D has one knob for this -- imageSmoothingEnabled -- so the
   filters divide into the two answers it has. */
typedef enum _cairo_filter {
    CAIRO_FILTER_FAST     = 0,
    CAIRO_FILTER_GOOD     = 1,
    CAIRO_FILTER_BEST     = 2,
    CAIRO_FILTER_NEAREST  = 3,
    CAIRO_FILTER_BILINEAR = 4
} cairo_filter_t;

typedef struct {
    double x_bearing, y_bearing;
    double width, height;
    double x_advance, y_advance;
} cairo_text_extents_t;

/* ---- state ---------------------------------------------------------- */

void cairo_save (cairo_t *cr);
void cairo_restore (cairo_t *cr);
void cairo_translate (cairo_t *cr, double tx, double ty);
void cairo_scale (cairo_t *cr, double sx, double sy);

/* ---- paint ---------------------------------------------------------- */

void cairo_set_source_rgb (cairo_t *cr, double r, double g, double b);
void cairo_set_source_rgba (cairo_t *cr, double r, double g, double b,
                            double a);
void cairo_set_line_width (cairo_t *cr, double width);
void cairo_set_line_cap (cairo_t *cr, cairo_line_cap_t cap);
void cairo_set_dash (cairo_t *cr, const double *dashes, int num_dashes,
                     double offset);

void cairo_fill (cairo_t *cr);
void cairo_fill_preserve (cairo_t *cr);
void cairo_stroke (cairo_t *cr);
void cairo_stroke_preserve (cairo_t *cr);
void cairo_paint (cairo_t *cr);
void cairo_clip (cairo_t *cr);

/* ---- path ----------------------------------------------------------- */

void cairo_new_path (cairo_t *cr);
void cairo_new_sub_path (cairo_t *cr);
void cairo_move_to (cairo_t *cr, double x, double y);
void cairo_line_to (cairo_t *cr, double x, double y);
void cairo_curve_to (cairo_t *cr, double x1, double y1, double x2, double y2,
                     double x3, double y3);
void cairo_arc (cairo_t *cr, double xc, double yc, double radius,
                double angle1, double angle2);
void cairo_rectangle (cairo_t *cr, double x, double y, double width,
                      double height);
void cairo_close_path (cairo_t *cr);

/* ---- the toy text API ------------------------------------------------ */

void cairo_select_font_face (cairo_t *cr, const char *family,
                             cairo_font_slant_t slant,
                             cairo_font_weight_t weight);
void cairo_set_font_size (cairo_t *cr, double size);
void cairo_show_text (cairo_t *cr, const char *utf8);

/* The one call that needs an answer back while the list is being built.
   See cairo2d_set_measure() in cairo2d.h for where the answer comes
   from. */
void cairo_text_extents (cairo_t *cr, const char *utf8,
                         cairo_text_extents_t *extents);

/* ---- an image surface, for blitting pixels a caller computed --------- */

cairo_surface_t *cairo_image_surface_create (cairo_format_t format,
                                             int width, int height);
cairo_status_t cairo_surface_status (cairo_surface_t *surface);
unsigned char *cairo_image_surface_get_data (cairo_surface_t *surface);
int cairo_image_surface_get_stride (cairo_surface_t *surface);
int cairo_image_surface_get_width (cairo_surface_t *surface);
int cairo_image_surface_get_height (cairo_surface_t *surface);
void cairo_surface_mark_dirty (cairo_surface_t *surface);
void cairo_surface_destroy (cairo_surface_t *surface);

void cairo_set_source_surface (cairo_t *cr, cairo_surface_t *surface,
                               double x, double y);

/* The source as a pattern, for the one property of it a caller can set.
   The pattern is the context's own and is not reference-counted: it names
   whatever the source is now. */
cairo_pattern_t *cairo_get_source (cairo_t *cr);
void cairo_pattern_set_filter (cairo_pattern_t *pattern,
                               cairo_filter_t filter);

#ifdef __cplusplus
}
#endif

#endif /* CAIRO_H */
