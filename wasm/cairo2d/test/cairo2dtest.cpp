/*
 * cairo2d's own test: what a cairo call turns into, and whether the list
 * can be read back with nothing but the arity table.
 *
 * Nothing here draws a picture, because the stand-in draws no pictures --
 * it records. So what is checked is the record: that each call appends the
 * op it says it does with the operands it was given, that the two faces
 * (cairo.h and cairomm/context.h) record the same list for the same
 * drawing, that a variable-length op can be walked past, that text is laid
 * out with the metrics the host supplied and drawn at the current point,
 * and that a surface's pixels outlive the caller's reference to them.
 *
 * With --dump it writes the tables and one recorded list as JSON, which is
 * what test/replaytest.mjs reads to hold replay.js against this side.
 *
 * Public domain, or CC0 where that is not a thing. Take it.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "cairo2d.h"
#include "cairomm/context.h"

static int failures = 0;

static void
fail (const std::string &what)
{
    printf("cairo2dtest: FAIL: %s\n", what.c_str());
    failures++;
}

/* ---- reading a list back ---------------------------------------------- */

struct Record {
    int op;
    std::vector<float> args;
};

/* The whole claim of the encoding: an opcode followed by exactly
   cairo2d_op_arity() operands is enough to walk the list, and a list that
   does not end on a record boundary is malformed. */
static bool
decode (const cairo_t *cr, std::vector<Record> &out)
{
    const float *ops = cairo2d_ops(cr);
    const int words = cairo2d_op_words(cr);

    out.clear();

    for (int i = 0; i < words; )
    {
        Record r;

        r.op = (int)ops[i++];

        int arity = cairo2d_op_arity(r.op);

        if (arity == -2 || cairo2d_op_name(r.op) == NULL)
        {
            fail("an op the table does not know: " + std::to_string(r.op));
            return false;
        }

        if (arity < 0)          /* SET_DASH: a count, the dashes, an offset */
        {
            if (i >= words)
            {
                fail("a variable-length op with no count");
                return false;
            }

            arity = (int)ops[i] + 2;
        }

        if (i + arity > words)
        {
            fail(std::string(cairo2d_op_name(r.op)) +
                 " runs off the end of the list");
            return false;
        }

        for (int k = 0; k < arity; k++)
            r.args.push_back(ops[i + k]);

        i += arity;
        out.push_back(r);
    }

    return true;
}

static std::string
spell (const std::vector<Record> &recs)
{
    std::string s;

    for (size_t i = 0; i < recs.size(); i++)
    {
        if (i > 0)
            s += " ";

        s += cairo2d_op_name(recs[i].op);
    }

    return s;
}

/* The op names in order, which is what an expectation is written as. */
static void
expect (const cairo_t *cr, const std::string &what, const std::string &want)
{
    std::vector<Record> recs;

    if (!decode(cr, recs))
        return;

    const std::string got = spell(recs);

    if (got != want)
        fail(what + ": recorded `" + got + "', wanted `" + want + "'");
}

static void
expectArg (const cairo_t *cr, const std::string &what, int index,
           int argIndex, double want)
{
    std::vector<Record> recs;

    if (!decode(cr, recs))
        return;

    if (index >= (int)recs.size() ||
        argIndex >= (int)recs[index].args.size())
    {
        fail(what + ": no operand " + std::to_string(argIndex) +
             " of record " + std::to_string(index));
        return;
    }

    const double got = recs[index].args[argIndex];

    if (fabs(got - want) > 1e-4)
        fail(what + ": operand " + std::to_string(argIndex) + " of " +
             cairo2d_op_name(recs[index].op) + " is " +
             std::to_string(got) + ", wanted " + std::to_string(want));
}

/* ---- a measurer with numbers a test can recognise ---------------------- */

static std::string lastFont;

static int
fakeMeasure (const char *font, const char *utf8, double *out)
{
    lastFont = font;

    const double advance = 7.0 * (double)strlen(utf8);

    out[0] = 0.0;
    out[1] = -9.0;
    out[2] = advance - 1.0;
    out[3] = 9.0;
    out[4] = advance;
    out[5] = 0.0;

    return 1;
}

/* ---- the drawing both faces make --------------------------------------- */

/* Deliberately one of everything with an operand worth checking. */
static void
drawThroughC (cairo_t *cr)
{
    cairo_save(cr);
    cairo_translate(cr, 10.0, 20.0);
    cairo_scale(cr, 2.0, 2.0);
    cairo_set_source_rgba(cr, 0.25, 0.5, 0.75, 0.5);
    cairo_set_line_width(cr, 1.5);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_new_path(cr);
    cairo_move_to(cr, 1.0, 2.0);
    cairo_line_to(cr, 3.0, 4.0);
    cairo_curve_to(cr, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0);
    cairo_new_sub_path(cr);
    cairo_arc(cr, 11.0, 12.0, 3.0, 0.0, M_PI);
    cairo_rectangle(cr, 0.0, 0.0, 20.0, 30.0);
    cairo_close_path(cr);
    cairo_fill_preserve(cr);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void
drawThroughCairomm (const Cairo::RefPtr<Cairo::Context> &cr)
{
    cr->save();
    cr->translate(10.0, 20.0);
    cr->scale(2.0, 2.0);
    cr->set_source_rgba(0.25, 0.5, 0.75, 0.5);
    cr->set_line_width(1.5);
    cr->set_line_cap(Cairo::Context::LineCap::ROUND);
    cr->begin_new_path();
    cr->move_to(1.0, 2.0);
    cr->line_to(3.0, 4.0);
    cr->curve_to(5.0, 6.0, 7.0, 8.0, 9.0, 10.0);
    cr->begin_new_sub_path();
    cr->arc(11.0, 12.0, 3.0, 0.0, M_PI);
    cr->rectangle(0.0, 0.0, 20.0, 30.0);
    cr->close_path();
    cr->fill_preserve();
    cr->stroke();
    cr->restore();
}

static const char *const FIGURE =
    "SAVE TRANSLATE SCALE SET_SOURCE_RGBA SET_LINE_WIDTH SET_LINE_CAP "
    "NEW_PATH MOVE_TO LINE_TO CURVE_TO NEW_SUB_PATH ARC RECTANGLE "
    "CLOSE_PATH FILL_PRESERVE STROKE RESTORE";

/* ---- the checks --------------------------------------------------------- */

static void
checkTable (void)
{
    for (int op = 1; op < CAIRO2D_OP_MAX; op++)
    {
        if (cairo2d_op_name(op) == NULL)
            fail("opcode " + std::to_string(op) + " has no name");

        if (cairo2d_op_arity(op) < -1)
            fail("opcode " + std::to_string(op) + " has no arity");
    }

    if (cairo2d_op_name(0) != NULL || cairo2d_op_name(CAIRO2D_OP_MAX) != NULL)
        fail("an opcode outside the table has a name");
}

static void
checkFigure (cairo_t *cr)
{
    cairo2d_begin(cr);
    drawThroughC(cr);
    expect(cr, "the C face", FIGURE);

    /* The operands, spot-checked where getting them wrong would look like
       a picture rather than a crash. */
    expectArg(cr, "the C face", 3, 3, 0.5);      /* the alpha           */
    expectArg(cr, "the C face", 9, 5, 10.0);     /* curve_to's y3       */
    expectArg(cr, "the C face", 11, 4, M_PI);    /* the arc's end angle */
    expectArg(cr, "the C face", 12, 3, 30.0);    /* the rectangle's h   */

    std::vector<Record> viaC;

    decode(cr, viaC);

    cairo2d_begin(cr);
    drawThroughCairomm(Cairo::Context::create(cr));

    std::vector<Record> viaCairomm;

    decode(cr, viaCairomm);

    if (viaC.size() != viaCairomm.size())
    {
        fail("the two faces record different lists");
        return;
    }

    for (size_t i = 0; i < viaC.size(); i++)
        if (viaC[i].op != viaCairomm[i].op ||
            viaC[i].args != viaCairomm[i].args)
        {
            fail(std::string("the two faces part at ") +
                 cairo2d_op_name(viaC[i].op));
            return;
        }
}

/* A variable-length op is the one thing a reader can get lost in. */
static void
checkDash (cairo_t *cr)
{
    const double dashes[3] = { 4.0, 3.0, 2.0 };

    cairo2d_begin(cr);
    cairo_set_dash(cr, dashes, 3, 1.5);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0.0);
    cairo_fill(cr);

    expect(cr, "dashes", "SET_DASH STROKE SET_DASH FILL");
    expectArg(cr, "dashes", 0, 0, 3.0);
    expectArg(cr, "dashes", 0, 3, 2.0);
    expectArg(cr, "dashes", 0, 4, 1.5);         /* the offset, last */
    expectArg(cr, "dashes", 2, 0, 0.0);         /* unset: no dashes */

    Cairo::RefPtr<Cairo::Context> ctx = Cairo::Context::create(cr);

    cairo2d_begin(cr);
    ctx->unset_dash();
    expectArg(cr, "unset_dash", 0, 0, 0.0);
}

static void
checkText (cairo_t *cr)
{
    cairo2d_set_measure(fakeMeasure);

    cairo2d_begin(cr);

    Cairo::RefPtr<Cairo::Context> ctx = Cairo::Context::create(cr);

    ctx->select_font_face("sans", Cairo::ToyFontFace::Slant::ITALIC,
                          Cairo::ToyFontFace::Weight::BOLD);
    ctx->set_font_size(12.0);

    Cairo::TextExtents ext;

    ctx->get_text_extents("abc", ext);

    if (fabs(ext.x_advance - 21.0) > 1e-6 || fabs(ext.width - 20.0) > 1e-6)
        fail("get_text_extents did not hand back what the host measured");

    if (lastFont != "italic bold 12px sans-serif")
        fail("the font measured with was `" + lastFont + "'");

    /* Two strings, one move_to: the second starts where the first ended,
       which is what cairo's current point does and what a caller that
       draws a run of words relies on. */
    ctx->move_to(30.0, 40.0);
    ctx->show_text("abc");
    ctx->show_text("de");

    expect(cr, "text", "MOVE_TO SET_FONT SHOW_TEXT SET_FONT SHOW_TEXT");
    expectArg(cr, "text", 2, 1, 30.0);
    expectArg(cr, "text", 2, 2, 40.0);
    expectArg(cr, "text", 4, 1, 51.0);          /* 30 + three at seven */

    /* The font string is in the table, and it is the string the
       measurement used: the whole point of composing it in one place. */
    std::vector<Record> recs;

    decode(cr, recs);

    const char *font = cairo2d_string(cr, (int)recs[1].args[0]);
    const char *text = cairo2d_string(cr, (int)recs[2].args[0]);

    if (font == NULL || lastFont != font)
        fail("the list's font is not the one the text was measured in");

    if (text == NULL || strcmp(text, "abc") != 0)
        fail("the list's string table lost the string");

    /* The same string twice is one entry: SET_FONT before every SHOW_TEXT
       is cheap only if it is. */
    if (recs[1].args[0] != recs[3].args[0])
        fail("the string table did not intern the font");

    if (cairo2d_string_count(cr) != 3)
        fail("the string table has " +
             std::to_string(cairo2d_string_count(cr)) +
             " entries, wanted 3");

    /* And with nobody to ask, the estimate: not the right answer, but an
       answer that grows with the string and the size. */
    cairo2d_set_measure(NULL);

    Cairo::TextExtents small, large, longer;

    ctx->set_font_size(8.0);
    ctx->get_text_extents("mm", small);
    ctx->set_font_size(24.0);
    ctx->get_text_extents("mm", large);
    ctx->get_text_extents("mmmm", longer);

    if (!(small.x_advance > 0.0 && large.x_advance > small.x_advance &&
          longer.x_advance > large.x_advance))
        fail("the fallback estimate is not monotonic in size and length");
}

/* A spectrogram writes pixels, paints them and destroys the surface, all
   before the frame is replayed. The list blits by reference, so the
   pixels have to still be there. */
static void
checkSurface (cairo_t *cr)
{
    cairo2d_begin(cr);

    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                                      4, 2);

    if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS)
    {
        fail("a 4x2 image surface could not be made");
        return;
    }

    unsigned char *data = cairo_image_surface_get_data(img);
    const int stride = cairo_image_surface_get_stride(img);

    data[0] = 0x10;             /* blue, green, red, alpha: cairo's order */
    data[1] = 0x20;
    data[2] = 0x30;
    data[3] = 0xff;

    cairo_surface_mark_dirty(img);
    cairo_set_source_surface(cr, img, 5.0, 6.0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);
    cairo_surface_destroy(img);

    expect(cr, "a surface", "SET_SOURCE_SURFACE SET_FILTER PAINT");
    expectArg(cr, "a surface", 0, 0, 0.0);      /* the first surface   */
    expectArg(cr, "a surface", 0, 1, 5.0);
    expectArg(cr, "a surface", 1, 0, 1.0);      /* bilinear is smooth  */

    if (cairo2d_surface_count(cr) != 1)
        fail("the surface table did not take the surface");

    const unsigned char *kept = cairo2d_surface_data(cr, 0);

    if (kept == NULL || kept[0] != 0x10 || kept[2] != 0x30)
        fail("the surface's pixels did not outlive the caller's reference");

    if (cairo2d_surface_width(cr, 0) != 4 ||
        cairo2d_surface_height(cr, 0) != 2 ||
        cairo2d_surface_stride(cr, 0) != stride)
        fail("the surface table lost the surface's shape");

    /* The same surface twice is one entry and one reference. */
    cairo_surface_t *again = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                                        4, 2);

    cairo_set_source_surface(cr, again, 0.0, 0.0);
    cairo_set_source_surface(cr, again, 1.0, 1.0);
    cairo_surface_destroy(again);

    if (cairo2d_surface_count(cr) != 2)
        fail("the surface table did not reuse an entry");

    /* And a surface nobody can make is refused rather than recorded. */
    cairo_surface_t *none = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                                       0, 0);

    if (cairo_surface_status(none) == CAIRO_STATUS_SUCCESS)
        fail("a surface with no pixels came back successful");

    cairo_surface_destroy(none);
}

static void
checkBegin (cairo_t *cr)
{
    cairo2d_begin(cr);
    cairo_move_to(cr, 1.0, 1.0);
    cairo_show_text(cr, "x");

    cairo2d_begin(cr);

    if (cairo2d_op_words(cr) != 0 || cairo2d_string_count(cr) != 0 ||
        cairo2d_surface_count(cr) != 0)
        fail("begin() did not empty the tables");

    if (cairo2d_ops(cr) != NULL)
        fail("an empty list still has ops");

    /* The font goes back to its default too: a frame must not be laid out
       in whatever the last one happened to end in. */
    cairo2d_set_measure(fakeMeasure);
    cairo_show_text(cr, "x");
    cairo2d_set_measure(NULL);

    if (lastFont != "10px sans-serif")
        fail("begin() left the font at `" + lastFont + "'");
}

/* save and restore carry the font, as cairo's do -- and the recorder has
   to keep its own copy, since it is the one laying text out. */
static void
checkSaveRestore (cairo_t *cr)
{
    cairo2d_set_measure(fakeMeasure);
    cairo2d_begin(cr);

    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11.0);
    cairo_save(cr);
    cairo_set_font_size(cr, 30.0);
    cairo_show_text(cr, "x");

    if (lastFont != "30px sans-serif")
        fail("inside save, the font is `" + lastFont + "'");

    cairo_restore(cr);
    cairo_show_text(cr, "x");

    if (lastFont != "11px sans-serif")
        fail("after restore, the font is `" + lastFont + "'");

    cairo2d_set_measure(NULL);
}

/* ---- the JSON replaytest.mjs reads ------------------------------------- */

static void
dump (cairo_t *cr)
{
    cairo2d_set_measure(fakeMeasure);
    cairo2d_begin(cr);

    drawThroughC(cr);

    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12.0);
    cairo_move_to(cr, 4.0, 8.0);
    cairo_show_text(cr, "hi");

    const double dashes[2] = { 4.0, 3.0 };

    cairo_set_dash(cr, dashes, 2, 0.0);
    cairo_move_to(cr, 0.0, 0.0);
    cairo_line_to(cr, 10.0, 10.0);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
    cairo_paint(cr);

    printf("{\n  \"ops\": [");

    const float *ops = cairo2d_ops(cr);

    for (int i = 0; i < cairo2d_op_words(cr); i++)
        printf("%s%g", i > 0 ? ", " : "", (double)ops[i]);

    printf("],\n  \"strings\": [");

    for (int i = 0; i < cairo2d_string_count(cr); i++)
        printf("%s\"%s\"", i > 0 ? ", " : "", cairo2d_string(cr, i));

    printf("],\n  \"table\": {");

    for (int op = 1; op < CAIRO2D_OP_MAX; op++)
        printf("%s\n    \"%s\": { \"op\": %d, \"arity\": %d }",
               op > 1 ? "," : "", cairo2d_op_name(op), op,
               cairo2d_op_arity(op));

    printf("\n  }\n}\n");

    cairo2d_set_measure(NULL);
}

int
main (int argc, char **argv)
{
    cairo_t *cr = cairo2d_create();

    if (argc > 1 && strcmp(argv[1], "--dump") == 0)
    {
        dump(cr);
        cairo2d_destroy(cr);
        return 0;
    }

    checkTable();
    checkFigure(cr);
    checkDash(cr);
    checkText(cr);
    checkSurface(cr);
    checkBegin(cr);
    checkSaveRestore(cr);

    cairo2d_destroy(cr);

    if (failures == 0)
        printf("cairo2dtest: OK\n");
    else
        printf("cairo2dtest: %d failure%s\n", failures,
               failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
