/*
 * cairo2d -- the recorder.
 *
 * Every cairo call in cairo.h lands here and appends to a list. Nothing
 * is rasterised, nothing is measured except text, and the only state kept
 * is the state a recorder needs to be honest about: where the current
 * point is, and what font the next string will be drawn in.
 *
 * Why so little state: the replayer is a CanvasRenderingContext2D, which
 * has cairo's state model already -- a transform stack, a clip, a source,
 * a line width, a dash, a font. So the ops go through almost one for one
 * and the browser keeps the state. The two exceptions are here because
 * Canvas2D cannot answer them:
 *
 *   - the current point, because cairo's show_text draws at it and
 *     Canvas2D's fillText takes coordinates;
 *   - the font, because a label's width has to be known while the list is
 *     being built (cairo_text_extents), and the font it is measured in has
 *     to be the font it is drawn in, so the recorder composes one CSS font
 *     string and both the measurement and the list get that same string.
 *
 * Public domain, or CC0 where that is not a thing. Take it.
 */

#include "cairo2d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* ---- the objects ------------------------------------------------------ */

struct _cairo_surface {
    int refs;
    int width, height, stride;
    unsigned char *data;        /* NULL when the create failed */
};

/* What cairo_get_source() hands back. It names the context's source rather
   than being a thing of its own: the one property anybody sets through it
   is the filter, which is a property of how the source is drawn. */
struct _cairo_pattern {
    cairo_t *cr;
};

namespace {

/* The recorder's half of the graphics state -- the half Canvas2D cannot be
   asked about. cairo_save/cairo_restore push and pop it, as they do the
   rest, which the replayer's own save/restore handles. */
struct FontState {
    std::string family;
    cairo_font_slant_t slant;
    cairo_font_weight_t weight;
    double size;

    FontState (void)
        : family("sans"), slant(CAIRO_FONT_SLANT_NORMAL),
          weight(CAIRO_FONT_WEIGHT_NORMAL), size(10.0) {}
};

} /* namespace */

struct _cairo {
    std::vector<float> ops;
    std::vector<std::string> strings;
    std::map<std::string, int> interned;
    std::vector<cairo_surface_t *> surfaces;

    _cairo_pattern source;

    FontState font;
    std::vector<FontState> fontStack;

    double curX, curY;
};

/* ---- the op table ----------------------------------------------------- */

namespace {

struct OpInfo {
    const char *name;
    int arity;
};

/* Indexed by opcode. Order is cairo2d.h's enum; the empty first row is
   opcode 0, which is not one. */
const OpInfo opInfo[] = {
    { NULL,                  -2 },
    { "SAVE",                 0 },
    { "RESTORE",              0 },
    { "TRANSLATE",            2 },
    { "SCALE",                2 },
    { "NEW_PATH",             0 },
    { "NEW_SUB_PATH",         0 },
    { "MOVE_TO",              2 },
    { "LINE_TO",              2 },
    { "CURVE_TO",             6 },
    { "ARC",                  5 },
    { "RECTANGLE",            4 },
    { "CLOSE_PATH",           0 },
    { "SET_SOURCE_RGBA",      4 },
    { "SET_SOURCE_SURFACE",   3 },
    { "SET_LINE_WIDTH",       1 },
    { "SET_LINE_CAP",         1 },
    { "SET_DASH",            -1 },
    { "SET_FILTER",           1 },
    { "FILL",                 0 },
    { "FILL_PRESERVE",        0 },
    { "STROKE",               0 },
    { "STROKE_PRESERVE",      0 },
    { "PAINT",                0 },
    { "CLIP",                 0 },
    { "SET_FONT",             1 },
    { "SHOW_TEXT",            3 },
};

const int opInfoCount = (int)(sizeof(opInfo) / sizeof(opInfo[0]));

/* The table above is written by hand and the enum it indexes is in the
   header; this is what says they are still the same length. */
static_assert(opInfoCount == CAIRO2D_OP_MAX,
              "cairo2d.cpp's op table and cairo2d.h's opcodes have parted");

void emit (cairo_t *cr, int op)
{
    cr->ops.push_back((float)op);
}

void arg (cairo_t *cr, double v)
{
    cr->ops.push_back((float)v);
}

int intern (cairo_t *cr, const std::string &s)
{
    std::map<std::string, int>::const_iterator it = cr->interned.find(s);

    if (it != cr->interned.end())
        return it->second;

    const int index = (int)cr->strings.size();

    cr->strings.push_back(s);
    cr->interned[s] = index;

    return index;
}

/* The toy API's font families, as CSS names them. A family this does not
   know goes through as it is: a browser that does not know it either
   falls back, which is what cairo's toy API does too. */
const char *cssFamily (const std::string &family)
{
    if (family == "sans" || family == "Sans")
        return "sans-serif";

    if (family == "mono" || family == "Mono")
        return "monospace";

    return family.c_str();
}

/* One string, used to measure with and to draw with. The order is CSS's:
   style, weight, size, family. */
std::string cssFont (const FontState &f)
{
    char buf[128];

    const char *slant = f.slant == CAIRO_FONT_SLANT_ITALIC  ? "italic "
                      : f.slant == CAIRO_FONT_SLANT_OBLIQUE ? "oblique "
                                                            : "";
    const char *weight = f.weight == CAIRO_FONT_WEIGHT_BOLD ? "bold " : "";

    snprintf(buf, sizeof(buf), "%s%s%gpx ", slant, weight, f.size);

    return std::string(buf) + cssFamily(f.family);
}

/* ---- text measurement ------------------------------------------------- */

#ifdef __EMSCRIPTEN__

/* measureText on an offscreen context, synchronously. Available on the
   main thread and in a worker; in an AudioWorklet, and in Node, there is
   no canvas of any kind and this says so by returning 0. */
EM_JS(int, cairo2dMeasureJS, (const char *font, const char *text,
                              double *out), {
    var ctx = globalThis.__cairo2dMeasureCtx;

    if (ctx === undefined) {
        ctx = null;

        try {
            if (typeof OffscreenCanvas !== "undefined")
                ctx = new OffscreenCanvas(8, 8).getContext("2d");
            else if (typeof document !== "undefined")
                ctx = document.createElement("canvas").getContext("2d");
        } catch (e) {
            ctx = null;
        }

        globalThis.__cairo2dMeasureCtx = ctx;
    }

    if (!ctx)
        return 0;

    ctx.font = UTF8ToString(font);

    var m = ctx.measureText(UTF8ToString(text));

    /* The ink extents are the bounding box; the advance is what the pen
       moves. Some of the bounding box is optional in older browsers, so
       fall back to the advance for the width and to the size for the
       height rather than writing NaN into a layout. */
    var left = m.actualBoundingBoxLeft;
    var right = m.actualBoundingBoxRight;
    var asc = m.actualBoundingBoxAscent;
    var desc = m.actualBoundingBoxDescent;

    var haveInk = (typeof right === "number" && typeof asc === "number");

    var i = out >> 3;

    HEAPF64[i + 0] = haveInk ? -left : 0;            /* x_bearing */
    HEAPF64[i + 1] = haveInk ? -asc : 0;             /* y_bearing */
    HEAPF64[i + 2] = haveInk ? left + right : m.width;
    HEAPF64[i + 3] = haveInk ? asc + desc : 0;
    HEAPF64[i + 4] = m.width;                        /* x_advance */
    HEAPF64[i + 5] = 0;                              /* y_advance */

    return 1;
});

#endif /* __EMSCRIPTEN__ */

/* What there is when nothing can be asked. Sans at size s runs a little
   under half its size per character averaged over identifiers, which is
   what everything measured here is; the cap height is about 0.72 of it.
   A label laid out from this is centred a pixel or two off and truncated
   a character early or late, and nothing else in the drawing depends on
   it. */
void estimate (const FontState &f, const char *utf8, double *out)
{
    size_t chars = 0;

    for (const unsigned char *p = (const unsigned char *)utf8; *p; p++)
        if ((*p & 0xc0) != 0x80)
            chars++;

    const double bold = f.weight == CAIRO_FONT_WEIGHT_BOLD ? 0.53 : 0.5;
    const double advance = f.size * bold * (double)chars;

    out[0] = 0.0;
    out[1] = -f.size * 0.72;
    out[2] = advance;
    out[3] = f.size * 0.72;
    out[4] = advance;
    out[5] = 0.0;
}

cairo2d_measure_fn measureFn = NULL;

void measure (cairo_t *cr, const char *utf8, double *out)
{
    const std::string font = cssFont(cr->font);

    if (measureFn != NULL && measureFn(font.c_str(), utf8, out))
        return;

    estimate(cr->font, utf8, out);
}

/* ---- surfaces --------------------------------------------------------- */

void surfaceUnref (cairo_surface_t *s)
{
    if (s == NULL || --s->refs > 0)
        return;

    free(s->data);
    free(s);
}

} /* namespace */

/* ---- the recorder ----------------------------------------------------- */

cairo_t *cairo2d_create (void)
{
    cairo_t *cr = new cairo_t();

    cr->source.cr = cr;
    cr->curX = cr->curY = 0.0;

#ifdef __EMSCRIPTEN__
    if (measureFn == NULL)
        measureFn = cairo2dMeasureJS;
#endif

    return cr;
}

void cairo2d_destroy (cairo_t *cr)
{
    if (cr == NULL)
        return;

    cairo2d_begin(cr);          /* which is where the surfaces go */
    delete cr;
}

void cairo2d_begin (cairo_t *cr)
{
    for (size_t i = 0; i < cr->surfaces.size(); i++)
        surfaceUnref(cr->surfaces[i]);

    cr->surfaces.clear();
    cr->ops.clear();
    cr->strings.clear();
    cr->interned.clear();
    cr->fontStack.clear();
    cr->font = FontState();
    cr->curX = cr->curY = 0.0;
}

void cairo2d_set_measure (cairo2d_measure_fn fn)
{
    measureFn = fn;
}

const float *cairo2d_ops (const cairo_t *cr)
{
    return cr->ops.empty() ? NULL : &cr->ops[0];
}

int cairo2d_op_words (const cairo_t *cr)
{
    return (int)cr->ops.size();
}

int cairo2d_string_count (const cairo_t *cr)
{
    return (int)cr->strings.size();
}

const char *cairo2d_string (const cairo_t *cr, int index)
{
    if (index < 0 || index >= (int)cr->strings.size())
        return NULL;

    return cr->strings[index].c_str();
}

int cairo2d_surface_count (const cairo_t *cr)
{
    return (int)cr->surfaces.size();
}

const unsigned char *cairo2d_surface_data (const cairo_t *cr, int index)
{
    if (index < 0 || index >= (int)cr->surfaces.size())
        return NULL;

    return cr->surfaces[index]->data;
}

int cairo2d_surface_width (const cairo_t *cr, int index)
{
    if (index < 0 || index >= (int)cr->surfaces.size())
        return 0;

    return cr->surfaces[index]->width;
}

int cairo2d_surface_height (const cairo_t *cr, int index)
{
    if (index < 0 || index >= (int)cr->surfaces.size())
        return 0;

    return cr->surfaces[index]->height;
}

int cairo2d_surface_stride (const cairo_t *cr, int index)
{
    if (index < 0 || index >= (int)cr->surfaces.size())
        return 0;

    return cr->surfaces[index]->stride;
}

const char *cairo2d_op_name (int op)
{
    if (op < 0 || op >= opInfoCount)
        return NULL;

    return opInfo[op].name;
}

int cairo2d_op_arity (int op)
{
    if (op < 0 || op >= opInfoCount)
        return -2;

    return opInfo[op].arity;
}

/* ---- state ------------------------------------------------------------ */

void cairo_save (cairo_t *cr)
{
    cr->fontStack.push_back(cr->font);
    emit(cr, CAIRO2D_SAVE);
}

void cairo_restore (cairo_t *cr)
{
    if (!cr->fontStack.empty())
    {
        cr->font = cr->fontStack.back();
        cr->fontStack.pop_back();
    }

    emit(cr, CAIRO2D_RESTORE);
}

void cairo_translate (cairo_t *cr, double tx, double ty)
{
    emit(cr, CAIRO2D_TRANSLATE);
    arg(cr, tx);
    arg(cr, ty);
}

void cairo_scale (cairo_t *cr, double sx, double sy)
{
    emit(cr, CAIRO2D_SCALE);
    arg(cr, sx);
    arg(cr, sy);
}

/* ---- paint ------------------------------------------------------------ */

void cairo_set_source_rgb (cairo_t *cr, double r, double g, double b)
{
    cairo_set_source_rgba(cr, r, g, b, 1.0);
}

void cairo_set_source_rgba (cairo_t *cr, double r, double g, double b,
                            double a)
{
    emit(cr, CAIRO2D_SET_SOURCE_RGBA);
    arg(cr, r);
    arg(cr, g);
    arg(cr, b);
    arg(cr, a);
}

void cairo_set_line_width (cairo_t *cr, double width)
{
    emit(cr, CAIRO2D_SET_LINE_WIDTH);
    arg(cr, width);
}

void cairo_set_line_cap (cairo_t *cr, cairo_line_cap_t cap)
{
    emit(cr, CAIRO2D_SET_LINE_CAP);
    arg(cr, (double)cap);
}

void cairo_set_dash (cairo_t *cr, const double *dashes, int num_dashes,
                     double offset)
{
    if (num_dashes < 0)
        num_dashes = 0;

    emit(cr, CAIRO2D_SET_DASH);
    arg(cr, (double)num_dashes);

    for (int i = 0; i < num_dashes; i++)
        arg(cr, dashes[i]);

    arg(cr, offset);
}

void cairo_fill (cairo_t *cr)
{
    emit(cr, CAIRO2D_FILL);
}

void cairo_fill_preserve (cairo_t *cr)
{
    emit(cr, CAIRO2D_FILL_PRESERVE);
}

void cairo_stroke (cairo_t *cr)
{
    emit(cr, CAIRO2D_STROKE);
}

void cairo_stroke_preserve (cairo_t *cr)
{
    emit(cr, CAIRO2D_STROKE_PRESERVE);
}

void cairo_paint (cairo_t *cr)
{
    emit(cr, CAIRO2D_PAINT);
}

void cairo_clip (cairo_t *cr)
{
    emit(cr, CAIRO2D_CLIP);
}

/* ---- path ------------------------------------------------------------- */

void cairo_new_path (cairo_t *cr)
{
    emit(cr, CAIRO2D_NEW_PATH);
}

void cairo_new_sub_path (cairo_t *cr)
{
    emit(cr, CAIRO2D_NEW_SUB_PATH);
}

void cairo_move_to (cairo_t *cr, double x, double y)
{
    cr->curX = x;
    cr->curY = y;

    emit(cr, CAIRO2D_MOVE_TO);
    arg(cr, x);
    arg(cr, y);
}

void cairo_line_to (cairo_t *cr, double x, double y)
{
    cr->curX = x;
    cr->curY = y;

    emit(cr, CAIRO2D_LINE_TO);
    arg(cr, x);
    arg(cr, y);
}

void cairo_curve_to (cairo_t *cr, double x1, double y1, double x2, double y2,
                     double x3, double y3)
{
    cr->curX = x3;
    cr->curY = y3;

    emit(cr, CAIRO2D_CURVE_TO);
    arg(cr, x1);
    arg(cr, y1);
    arg(cr, x2);
    arg(cr, y2);
    arg(cr, x3);
    arg(cr, y3);
}

void cairo_arc (cairo_t *cr, double xc, double yc, double radius,
                double angle1, double angle2)
{
    cr->curX = xc + radius * cos(angle2);
    cr->curY = yc + radius * sin(angle2);

    emit(cr, CAIRO2D_ARC);
    arg(cr, xc);
    arg(cr, yc);
    arg(cr, radius);
    arg(cr, angle1);
    arg(cr, angle2);
}

void cairo_rectangle (cairo_t *cr, double x, double y, double width,
                      double height)
{
    cr->curX = x;
    cr->curY = y;

    emit(cr, CAIRO2D_RECTANGLE);
    arg(cr, x);
    arg(cr, y);
    arg(cr, width);
    arg(cr, height);
}

void cairo_close_path (cairo_t *cr)
{
    emit(cr, CAIRO2D_CLOSE_PATH);
}

/* ---- the toy text API -------------------------------------------------- */

void cairo_select_font_face (cairo_t *cr, const char *family,
                             cairo_font_slant_t slant,
                             cairo_font_weight_t weight)
{
    cr->font.family = family != NULL ? family : "sans";
    cr->font.slant = slant;
    cr->font.weight = weight;
}

void cairo_set_font_size (cairo_t *cr, double size)
{
    cr->font.size = size;
}

/* The font goes in ahead of every string rather than when it changes.
   One op per label costs nothing next to the label, and it means the
   recorder never has to reason about what the replayer's state is after a
   restore -- which is the kind of reasoning that draws the second half of
   a canvas in the wrong weight. */
void cairo_show_text (cairo_t *cr, const char *utf8)
{
    if (utf8 == NULL)
        return;

    emit(cr, CAIRO2D_SET_FONT);
    arg(cr, (double)intern(cr, cssFont(cr->font)));

    emit(cr, CAIRO2D_SHOW_TEXT);
    arg(cr, (double)intern(cr, utf8));
    arg(cr, cr->curX);
    arg(cr, cr->curY);

    double ext[6];

    measure(cr, utf8, ext);
    cr->curX += ext[4];
}

void cairo_text_extents (cairo_t *cr, const char *utf8,
                         cairo_text_extents_t *extents)
{
    double ext[6];

    if (extents == NULL)
        return;

    measure(cr, utf8 != NULL ? utf8 : "", ext);

    extents->x_bearing = ext[0];
    extents->y_bearing = ext[1];
    extents->width     = ext[2];
    extents->height    = ext[3];
    extents->x_advance = ext[4];
    extents->y_advance = ext[5];
}

/* ---- surfaces ---------------------------------------------------------- */

cairo_surface_t *cairo_image_surface_create (cairo_format_t format,
                                             int width, int height)
{
    cairo_surface_t *s = (cairo_surface_t *)calloc(1, sizeof(*s));

    (void)format;               /* both formats are four bytes a pixel */

    if (s == NULL)
        return NULL;

    s->refs = 1;

    if (width <= 0 || height <= 0)
        return s;               /* in error, as cairo's would be */

    s->width = width;
    s->height = height;
    s->stride = width * 4;
    s->data = (unsigned char *)calloc((size_t)s->stride * (size_t)height, 1);

    if (s->data == NULL)
        s->width = s->height = s->stride = 0;

    return s;
}

cairo_status_t cairo_surface_status (cairo_surface_t *surface)
{
    if (surface == NULL || surface->data == NULL)
        return CAIRO_STATUS_NO_MEMORY;

    return CAIRO_STATUS_SUCCESS;
}

unsigned char *cairo_image_surface_get_data (cairo_surface_t *surface)
{
    return surface != NULL ? surface->data : NULL;
}

int cairo_image_surface_get_stride (cairo_surface_t *surface)
{
    return surface != NULL ? surface->stride : 0;
}

int cairo_image_surface_get_width (cairo_surface_t *surface)
{
    return surface != NULL ? surface->width : 0;
}

int cairo_image_surface_get_height (cairo_surface_t *surface)
{
    return surface != NULL ? surface->height : 0;
}

void cairo_surface_mark_dirty (cairo_surface_t *surface)
{
    (void)surface;              /* nothing cached the pixels */
}

void cairo_surface_destroy (cairo_surface_t *surface)
{
    surfaceUnref(surface);
}

/* The list blits by reference, so the context takes a reference of its own
   here: a caller that draws into a surface, paints it and destroys it
   before the frame is replayed -- which is exactly what a spectrogram
   does -- must not leave the replayer reading freed pixels. The reference
   goes at the next cairo2d_begin(). */
void cairo_set_source_surface (cairo_t *cr, cairo_surface_t *surface,
                               double x, double y)
{
    if (surface == NULL || surface->data == NULL)
        return;

    int index = -1;

    for (size_t i = 0; i < cr->surfaces.size(); i++)
        if (cr->surfaces[i] == surface)
        {
            index = (int)i;
            break;
        }

    if (index < 0)
    {
        surface->refs++;
        index = (int)cr->surfaces.size();
        cr->surfaces.push_back(surface);
    }

    emit(cr, CAIRO2D_SET_SOURCE_SURFACE);
    arg(cr, (double)index);
    arg(cr, x);
    arg(cr, y);
}

cairo_pattern_t *cairo_get_source (cairo_t *cr)
{
    return &cr->source;
}

void cairo_pattern_set_filter (cairo_pattern_t *pattern,
                               cairo_filter_t filter)
{
    if (pattern == NULL || pattern->cr == NULL)
        return;

    const bool smooth = filter != CAIRO_FILTER_NEAREST &&
                        filter != CAIRO_FILTER_FAST;

    emit(pattern->cr, CAIRO2D_SET_FILTER);
    arg(pattern->cr, smooth ? 1.0 : 0.0);
}
