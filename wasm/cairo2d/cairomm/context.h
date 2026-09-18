/*
 * cairo2d -- cairomm's face, over the same list.
 *
 * C++ that draws through cairomm says cr->move_to(x, y) and takes a
 * Cairo::RefPtr<Cairo::Context> around. This header is that vocabulary --
 * the part of it a diagram uses -- over cairo2d's recorder, so such code
 * compiles for the browser with its include path changed and nothing else.
 *
 * It is not a binding: every method here is one inline call into cairo.h.
 * What it buys is that the C++ which draws on the desktop and the C++
 * which draws in the browser are the same C++.
 *
 * The same rule as cairo.h: what is missing is missing on purpose. A call
 * that is not here does not compile, in the line that made it.
 *
 * Public domain, or CC0 where that is not a thing. Take it.
 */

#ifndef CAIROMM_CONTEXT_H
#define CAIROMM_CONTEXT_H

#include <memory>
#include <string>
#include <vector>

#include "cairo.h"

namespace Cairo {

/* cairomm 1.16's own RefPtr is this alias; a Context here is held by the
   shared_ptr that made it and by nobody else. */
template <typename T>
using RefPtr = std::shared_ptr<T>;

typedef cairo_text_extents_t TextExtents;

/* The toy font face is not an object here -- there is nothing to hold --
   but its two enumerations are how select_font_face is spelled. */
class ToyFontFace {
public:
    enum class Slant {
        NORMAL  = CAIRO_FONT_SLANT_NORMAL,
        ITALIC  = CAIRO_FONT_SLANT_ITALIC,
        OBLIQUE = CAIRO_FONT_SLANT_OBLIQUE
    };

    enum class Weight {
        NORMAL = CAIRO_FONT_WEIGHT_NORMAL,
        BOLD   = CAIRO_FONT_WEIGHT_BOLD
    };
};

class Context {
public:
    enum class LineCap {
        BUTT   = CAIRO_LINE_CAP_BUTT,
        ROUND  = CAIRO_LINE_CAP_ROUND,
        SQUARE = CAIRO_LINE_CAP_SQUARE
    };

    /* Around a recorder the caller made and keeps. cairomm's own create()
       takes a surface, which is a thing this has no equivalent of: here
       the surface is the page's canvas element and the list is what
       reaches it. */
    static RefPtr<Context> create (cairo_t *cobj)
    {
        return RefPtr<Context>(new Context(cobj));
    }

    cairo_t *cobj (void) { return cr_; }
    const cairo_t *cobj (void) const { return cr_; }

    /* ---- state ---- */

    void save (void) { cairo_save(cr_); }
    void restore (void) { cairo_restore(cr_); }
    void translate (double tx, double ty) { cairo_translate(cr_, tx, ty); }
    void scale (double sx, double sy) { cairo_scale(cr_, sx, sy); }

    /* ---- paint ---- */

    void set_source_rgb (double r, double g, double b)
    {
        cairo_set_source_rgb(cr_, r, g, b);
    }

    void set_source_rgba (double r, double g, double b, double a)
    {
        cairo_set_source_rgba(cr_, r, g, b, a);
    }

    void set_line_width (double width) { cairo_set_line_width(cr_, width); }

    void set_line_cap (LineCap cap)
    {
        cairo_set_line_cap(cr_, (cairo_line_cap_t)cap);
    }

    void set_dash (const std::vector<double> &dashes, double offset)
    {
        cairo_set_dash(cr_, dashes.empty() ? NULL : &dashes[0],
                       (int)dashes.size(), offset);
    }

    void unset_dash (void) { cairo_set_dash(cr_, NULL, 0, 0.0); }

    void fill (void) { cairo_fill(cr_); }
    void fill_preserve (void) { cairo_fill_preserve(cr_); }
    void stroke (void) { cairo_stroke(cr_); }
    void stroke_preserve (void) { cairo_stroke_preserve(cr_); }
    void paint (void) { cairo_paint(cr_); }
    void clip (void) { cairo_clip(cr_); }

    /* ---- path ---- */

    void begin_new_path (void) { cairo_new_path(cr_); }
    void begin_new_sub_path (void) { cairo_new_sub_path(cr_); }
    void move_to (double x, double y) { cairo_move_to(cr_, x, y); }
    void line_to (double x, double y) { cairo_line_to(cr_, x, y); }

    void curve_to (double x1, double y1, double x2, double y2,
                   double x3, double y3)
    {
        cairo_curve_to(cr_, x1, y1, x2, y2, x3, y3);
    }

    void arc (double xc, double yc, double radius, double angle1,
              double angle2)
    {
        cairo_arc(cr_, xc, yc, radius, angle1, angle2);
    }

    void rectangle (double x, double y, double width, double height)
    {
        cairo_rectangle(cr_, x, y, width, height);
    }

    void close_path (void) { cairo_close_path(cr_); }

    /* ---- the toy text API ---- */

    void select_font_face (const std::string &family,
                           ToyFontFace::Slant slant,
                           ToyFontFace::Weight weight)
    {
        cairo_select_font_face(cr_, family.c_str(),
                               (cairo_font_slant_t)slant,
                               (cairo_font_weight_t)weight);
    }

    void set_font_size (double size) { cairo_set_font_size(cr_, size); }

    void show_text (const std::string &utf8)
    {
        cairo_show_text(cr_, utf8.c_str());
    }

    void get_text_extents (const std::string &utf8, TextExtents &extents)
    {
        cairo_text_extents(cr_, utf8.c_str(), &extents);
    }

private:
    explicit Context (cairo_t *cr) : cr_(cr) {}

    Context (const Context &);
    Context &operator= (const Context &);

    cairo_t *cr_;
};

} /* namespace Cairo */

#endif /* CAIROMM_CONTEXT_H */
