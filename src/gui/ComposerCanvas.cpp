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
 */

#include "config.h"

#include <stdio.h>

#include <algorithm>
#include <cmath>

#include "think.h"

/* M_PI is not in C++ and UCRT hides it; thMath.h is the one place that
 * knows that. See its header for why there are two answers and not one. */
#include "thMath.h"

#include "thcPlugin.h"
#include "thcScheduler.h"
#include "ComposerCanvas.h"

#include "../gui-util.h"

/* Row and box geometry. A chain reads left to right at one size; the
 * canvas scrolls rather than shrinks, because a stage box exists to
 * hold a legible composer_draw and a 40-pixel euclid ring is not one. */
static const double M        = 8;    /* outer margin                    */
static const double ROW_GAP  = 16;
static const double LABEL_W  = 104;  /* the chain's name column         */
static const double STAGE_W  = 104;
static const double STAGE_H  = 88;
static const double SINK_W   = 76;
static const double GHOST_W  = 30;
static const double ARROW_W  = 22;
static const double TITLE_H  = 15;

/* The params handle in a stage box's title bar. */
static const double TWISTY   = 9;

/* The canvas background, named because the boxes have to repaint it: a
 * box is opaque, so that a wire running behind it is behind it. */
static const double BG_R = 0.09, BG_G = 0.09, BG_B = 0.11;

/* A knob node. Wider than it is tall because what it holds is a name and
 * a track, and shorter than a stage box because it has no picture. Six
 * to a row: past that the lane is wider than any chain and the wires all
 * come from off to the right. */
static const double KNOB_W   = 118;
static const double KNOB_H   = 38;
static const double KNOB_GAP = 10;
static const int    KNOB_COLS = 6;

/* How near a knob's port counts as on it. Four pixels of circle is what
 * is drawn; nine is what answers, because it is a drag handle and a
 * handle you have to be accurate about is one nobody finds twice. */
static const double PORT_GRAB = 9;

ComposerCanvas::ComposerCanvas (void)
    : doc_(NULL), sched_(NULL),
      feeding_(false), feedButton_(1), dragKnob_(-1), wireFrom_(-1),
      wireX_(0), wireY_(0), dragBox_(-1), dragDx_(0), dropAt_(-1)
{
    set_draw_func(sigc::mem_fun(*this, &ComposerCanvas::onDraw));

    auto click = Gtk::GestureClick::create();

    /* Through a lambda rather than straight to the handler, because
       which button was pressed is the *controller's* to answer and the
       signal does not carry it. A plugin that could not tell a primary
       click from a secondary one would have half an input API -- a Life
       board wants left to draw and right to erase. */
    click->signal_pressed().connect(
        [this, click](int n, double x, double y)
        { onPressed(n, x, y, (int)click->get_current_button()); });
    click->signal_released().connect(
        [this, click](int n, double x, double y)
        { onReleased(n, x, y, (int)click->get_current_button()); });
    add_controller(click);

    /* Motion, for painting a plugin's picture by dragging across it.
       Separate from the drag gesture below because that one exists to
       move stage boxes around and reports offsets; a plugin wants
       positions. */
    auto motion = Gtk::EventControllerMotion::create();

    motion->signal_motion().connect(
        sigc::mem_fun(*this, &ComposerCanvas::onMotion));
    add_controller(motion);

    /* Escape leaves the enlarged view. A canvas that fills itself with
       one stage and offers no way back is a trap. */
    auto keys = Gtk::EventControllerKey::create();

    keys->signal_key_pressed().connect(
        sigc::mem_fun(*this, &ComposerCanvas::onKey), false);
    add_controller(keys);

    set_focusable(true);

    auto drag = Gtk::GestureDrag::create();

    drag->signal_drag_begin().connect(
        sigc::mem_fun(*this, &ComposerCanvas::onDragBegin));
    drag->signal_drag_update().connect(
        sigc::mem_fun(*this, &ComposerCanvas::onDragUpdate));
    drag->signal_drag_end().connect(
        sigc::mem_fun(*this, &ComposerCanvas::onDragEnd));
    add_controller(drag);
}

/* The laid-out rows, plus a margin so the rightmost ghost box does not
   sit against the edge. Zero before the first rebuild, which the base
   reads as "nothing to size to" and leaves alone. */
void
ComposerCanvas::contentExtent (double &w, double &h) const
{
    w = h = 0;

    for (size_t i = 0; i < boxes_.size(); i++)
    {
        if (boxes_[i].x + boxes_[i].w > w)
            w = boxes_[i].x + boxes_[i].w;

        if (boxes_[i].y + boxes_[i].h > h)
            h = boxes_[i].y + boxes_[i].h;
    }

    if (w > 0) w += 12;
    if (h > 0) h += 12;
}

void
ComposerCanvas::SetPiece (const thcGenEdit::Doc *doc, thcScheduler *sched)
{
    doc_ = doc;
    sched_ = sched;
    dragBox_ = -1;
    dropAt_ = -1;
    feeding_ = false;
    dragKnob_ = -1;
    wireFrom_ = -1;

    rebuild();
    contentResized();

    /* A reload rebuilds every instance, so the enlarged stage is a new
       object at the same address in the piece -- or gone, if the edit
       removed it. Checked rather than assumed: enlargedStage() looks it
       up through the scheduler every time, and this is where a stage
       that no longer exists stops being shown. */
    if (enlarged_.kind == Selection::STAGE && enlargedStage() == NULL)
        setEnlarged(Selection());

    /* The selection may name things the new piece does not have. */
    if (sel_.kind != Selection::NONE && sel_.kind != Selection::ADD_CHAIN)
    {
        /* A knob is indexed into the piece's knobs, not into a chain --
           and it leaves `chain' at zero, which the chain test below
           would have read as "the first chain", quietly keeping a knob
           selected in a piece that no longer has it. */
        bool ok = doc_ != NULL;

        if (ok && sel_.kind == Selection::KNOB)
            ok = sel_.index < doc_->knobs.size();
        else if (ok)
            ok = sel_.chain < doc_->chains.size();

        if (ok && sel_.kind == Selection::STAGE)
            ok = sel_.index < doc_->chains[sel_.chain].stages.size();

        if (ok && sel_.kind == Selection::SINK)
            ok = sel_.index < doc_->chains[sel_.chain].sinks.size();

        if (!ok)
        {
            sel_ = Selection();
            sigSelection.emit(sel_);
        }
    }

    queue_draw();
}

void
ComposerCanvas::select (const Selection &sel)
{
    if (sel == sel_)
        return;

    sel_ = sel;
    sigSelection.emit(sel_);
    queue_draw();
}

/* Lay every clickable box out once per piece; drawing and hit testing
 * both read the result, so they cannot disagree about where things
 * are. */
void
ComposerCanvas::rebuild (void)
{
    boxes_.clear();
    rowY_.clear();

    if (doc_ == NULL)
        return;

    double y = M;
    double widest = 0;

    /* The knob lane, across the top.
     *
       Above the chains rather than beside them because a knob feeds
       stages anywhere in the piece, and a wire that drops down past the
       chain names reads as "this reaches all of these" -- a left-hand
       column would have every wire crossing every chain's label. */
    for (size_t ki = 0; ki < doc_->knobs.size(); ki++)
    {
        const thcGenEdit::Knob &k = doc_->knobs[ki];

        Box b;

        b.what.kind = Selection::KNOB;
        b.what.index = ki;
        b.x = M + (ki % KNOB_COLS) * (KNOB_W + KNOB_GAP);
        b.y = y + (ki / KNOB_COLS) * (KNOB_H + KNOB_GAP);
        b.w = KNOB_W;
        b.h = KNOB_H;
        b.title = k.label.empty() ? k.name : k.label;
        b.sub = "@" + k.name;
        b.live = NULL;
        b.channel = -1;
        b.ghost = false;

        /* The live value, because a knob that has been dragged is not
           where the file last saw it -- and the node is a control, so it
           has to show what the piece is actually hearing. */
        thArg *arg = sched_ ? sched_->knob(k.name) : NULL;

        b.kv  = arg != NULL ? (*arg)[0] : k.value;
        b.klo = arg != NULL ? arg->min() : (k.hasMin ? k.min : 0);
        b.khi = arg != NULL ? arg->max() : (k.hasMax ? k.max : 1);

        if (b.khi <= b.klo)
        {
            b.klo = 0;
            b.khi = 1;
        }

        boxes_.push_back(b);
        widest = std::max(widest, b.x + b.w);

        if (ki + 1 == doc_->knobs.size())
            y = b.y + KNOB_H + ROW_GAP;
    }

    for (size_t ci = 0; ci < doc_->chains.size(); ci++)
    {
        const thcGenEdit::Chain &chain = doc_->chains[ci];

        rowY_.push_back(y);

        double x = M;

        {
            Box b;

            b.what.kind = Selection::CHAIN;
            b.what.chain = ci;
            b.x = x; b.y = y; b.w = LABEL_W; b.h = STAGE_H;
            b.title = chain.name;
            b.sub = chain.inputMidi ? "midi in" : "";
            b.live = NULL;
            b.channel = -1;
            b.ghost = false;
            boxes_.push_back(b);
        }

        x += LABEL_W + ARROW_W;

        for (size_t si = 0; si < chain.stages.size(); si++)
        {
            Box b;

            b.what.kind = Selection::STAGE;
            b.what.chain = ci;
            b.what.index = si;
            b.x = x; b.y = y; b.w = STAGE_W; b.h = STAGE_H;
            b.title = chain.stages[si].category + "::" +
                      chain.stages[si].plugin;
            b.sub = chain.stages[si].name;
            b.channel = -1;
            b.ghost = false;

            thcChain *live = sched_ ? sched_->chain(ci) : NULL;

            b.live = live != NULL && si < live->stages.size()
                ? live->stages[si].get() : NULL;

            boxes_.push_back(b);
            x += STAGE_W + ARROW_W;
        }

        {
            Box b;

            b.what.kind = Selection::ADD_STAGE;
            b.what.chain = ci;
            b.what.index = chain.stages.size();
            b.x = x; b.y = y + (STAGE_H - GHOST_W) / 2;
            b.w = GHOST_W; b.h = GHOST_W;
            b.title = "+";
            b.live = NULL;
            b.channel = -1;
            b.ghost = true;
            boxes_.push_back(b);
            x += GHOST_W + ARROW_W;
        }

        for (size_t ki = 0; ki < chain.sinks.size(); ki++)
        {
            Box b;

            b.what.kind = Selection::SINK;
            b.what.chain = ci;
            b.what.index = ki;
            b.x = x; b.y = y; b.w = SINK_W; b.h = STAGE_H;

            char t[24];

            snprintf(t, sizeof(t), "ch %d", chain.sinks[ki].channel);
            b.title = t;
            b.sub = chain.sinks[ki].chanarg.empty()
                ? "notes" : "@" + chain.sinks[ki].chanarg;
            b.live = NULL;
            b.channel = chain.sinks[ki].channel;
            b.ghost = false;
            boxes_.push_back(b);
            x += SINK_W + 8;
        }

        {
            Box b;

            b.what.kind = Selection::ADD_SINK;
            b.what.chain = ci;
            b.what.index = chain.sinks.size();
            b.x = x; b.y = y + (STAGE_H - GHOST_W) / 2;
            b.w = GHOST_W; b.h = GHOST_W;
            b.title = "+";
            b.live = NULL;
            b.channel = -1;
            b.ghost = true;
            boxes_.push_back(b);
            x += GHOST_W;
        }

        widest = std::max(widest, x);
        y += STAGE_H + ROW_GAP;
    }

    {
        Box b;

        b.what.kind = Selection::ADD_CHAIN;
        b.x = M; b.y = y; b.w = LABEL_W + 36; b.h = 26;
        b.title = "+ chain";
        b.live = NULL;
        b.channel = -1;
        b.ghost = true;
        boxes_.push_back(b);
        y += 26;
        widest = std::max(widest, b.x + b.w);
    }

    set_content_width((int)(widest + M));
    set_content_height((int)(y + M));
}

const ComposerCanvas::Box *
ComposerCanvas::hit (double x, double y) const
{
    for (size_t i = boxes_.size(); i-- > 0; )
        if (x >= boxes_[i].x && x <= boxes_[i].x + boxes_[i].w &&
            y >= boxes_[i].y && y <= boxes_[i].y + boxes_[i].h)
            return &boxes_[i];

    return NULL;
}

static void
roundedRect (const Cairo::RefPtr<Cairo::Context> &cr, double x, double y,
             double w, double h, double r)
{
    cr->begin_new_sub_path();
    cr->arc(x + w - r, y + r, r, -M_PI / 2, 0);
    cr->arc(x + w - r, y + h - r, r, 0, M_PI / 2);
    cr->arc(x + r, y + h - r, r, M_PI / 2, M_PI);
    cr->arc(x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cr->close_path();
}

/* Toy-API text, truncated to fit -- a canvas box is not a place for
 * Pango machinery, and every string here is a short identifier. */
static void
fitText (const Cairo::RefPtr<Cairo::Context> &cr, std::string text,
         double x, double y, double maxW)
{
    Cairo::TextExtents ext;

    cr->get_text_extents(text, ext);

    while (!text.empty() && ext.width > maxW)
    {
        text.resize(text.size() - 1);
        cr->get_text_extents(text, ext);
    }

    cr->move_to(x, y);
    cr->show_text(text);
}

/* A knob node: the name, the value, and the track that sets it, with a
 * port on the bottom edge for wires to leave from. */
void
ComposerCanvas::drawKnob (const Cairo::RefPtr<Cairo::Context> &cr,
                          const Box &box, bool selected) const
{
    roundedRect(cr, box.x + 0.5, box.y + 0.5, box.w - 1, box.h - 1, 5);
    cr->set_source_rgb(0.16, 0.15, 0.10);
    cr->fill_preserve();
    cr->set_line_width(selected ? 2 : 1);
    cr->set_source_rgba(1.0, 0.85, 0.3, selected ? 0.95 : 0.4);
    cr->stroke();

    cr->set_font_size(10);
    cr->set_source_rgba(1, 1, 1, 0.85);
    fitText(cr, box.title, box.x + 8, box.y + 14, box.w - 52);

    char v[32];

    snprintf(v, sizeof(v), "%.3g", box.kv);

    Cairo::TextExtents ext;

    cr->get_text_extents(v, ext);
    cr->set_source_rgba(1.0, 0.9, 0.5, 0.9);
    cr->move_to(box.x + box.w - 8 - ext.width, box.y + 14);
    cr->show_text(v);

    double x0, x1, ty;

    knobTrackRect(box, x0, x1, ty);

    const double t = box.khi > box.klo
        ? std::clamp((box.kv - box.klo) / (box.khi - box.klo), 0.0, 1.0)
        : 0.0;

    cr->set_line_width(3.0);
    cr->set_line_cap(Cairo::Context::LineCap::ROUND);

    cr->set_source_rgba(1, 1, 1, 0.14);
    cr->move_to(x0, ty);
    cr->line_to(x1, ty);
    cr->stroke();

    cr->set_source_rgba(1.0, 0.85, 0.3, 0.75);
    cr->move_to(x0, ty);
    cr->line_to(x0 + (x1 - x0) * t, ty);
    cr->stroke();

    cr->set_line_cap(Cairo::Context::LineCap::BUTT);

    cr->set_source_rgba(1.0, 0.92, 0.6, 0.95);
    cr->arc(x0 + (x1 - x0) * t, ty, 3.5, 0, 2 * M_PI);
    cr->fill();

    /* The port. Bigger than it needs to look, because it is a drag
       target and a three-pixel drag target is a decoration. */
    double px, py;

    knobPortAt(box, px, py);

    cr->set_source_rgba(1.0, 0.85, 0.3, 0.9);
    cr->arc(px, py, 4, 0, 2 * M_PI);
    cr->fill();
}

/* One curve per binding, plus the one being dragged.
 *
 * Faint by default and bright at either end of the selection: a piece
 * with four knobs and twenty bound params has twenty wires, and twenty
 * bright wires is a ball of wool. Selecting a knob or a stage is how you
 * ask which of them are yours. */
void
ComposerCanvas::drawWires (const Cairo::RefPtr<Cairo::Context> &cr) const
{
    cr->set_line_width(1.4);

    eachWire([&](const Box &kb, const Box &st, const std::string &)
    {
        double x0, y0;

        knobPortAt(kb, x0, y0);

        const double x1 = st.x;
        const double y1 = st.y + st.h / 2;

        const bool lit = sel_.kind != Selection::NONE &&
            (sel_ == kb.what || sel_ == st.what);

        cr->set_source_rgba(1.0, 0.85, 0.3, lit ? 0.8 : 0.22);

        /* Down out of the port and in from the left, rather than down
           into the top.
         *
           Into the top looked tidier on one chain and was unreadable on
           seven: every wire ran vertically down the column the stage
           boxes are in, so a knob feeding the seventh chain drew a line
           through the six above it. The left edge is the side the flow
           arrives on, the boxes are opaque, and the space between the
           chain names and the stages is empty -- so the wires run in the
           gap and appear where they arrive. */
        const double bend = std::max(20.0, (y1 - y0) * 0.35);

        cr->move_to(x0, y0);
        cr->curve_to(x0, y0 + bend, x1 - 26, y1 - bend / 2, x1 - 4, y1);
        cr->stroke();

        cr->arc(x1 - 3, y1, 2.2, 0, 2 * M_PI);
        cr->fill();
    });

    /* The wire in flight, dashed so that it reads as a proposal. */
    if (wireFrom_ >= 0 && (size_t)wireFrom_ < boxes_.size())
    {
        double x0, y0;

        knobPortAt(boxes_[wireFrom_], x0, y0);

        std::vector<double> dashes = { 4.0, 3.0 };

        cr->set_dash(dashes, 0);
        cr->set_source_rgba(1.0, 0.9, 0.5, 0.9);
        cr->move_to(x0, y0);
        cr->line_to(wireX_, wireY_);
        cr->stroke();
        cr->unset_dash();
    }
}

void
ComposerCanvas::drawBox (const Cairo::RefPtr<Cairo::Context> &cr,
                         const Box &box, bool selected) const
{
    if (box.what.kind == Selection::KNOB)
    {
        drawKnob(cr, box, selected);
        return;
    }

    if (box.ghost)
    {
        std::vector<double> dashes = { 3.0, 3.0 };

        cr->set_source_rgba(1, 1, 1, selected ? 0.7 : 0.3);
        cr->set_line_width(1);
        cr->set_dash(dashes, 0);
        roundedRect(cr, box.x + 0.5, box.y + 0.5, box.w - 1, box.h - 1, 5);
        cr->stroke();
        cr->unset_dash();

        cr->set_font_size(14);
        Cairo::TextExtents ext;
        cr->get_text_extents(box.title, ext);
        cr->move_to(box.x + (box.w - ext.width) / 2 - ext.x_bearing,
                    box.y + (box.h + ext.height) / 2);
        cr->show_text(box.title);
        return;
    }

    if (box.what.kind == Selection::CHAIN)
    {
        bool muted = false;

        if (sched_ != NULL && sched_->chain(box.what.chain) != NULL)
            muted = sched_->chain(box.what.chain)->muted;

        cr->set_source_rgba(1, 1, 1, muted ? 0.35 : 0.85);
        cr->set_font_size(12);
        fitText(cr, box.title, box.x + 2, box.y + 16, box.w - 4);

        cr->set_font_size(9);
        cr->set_source_rgba(1, 1, 1, 0.4);

        if (muted)
            fitText(cr, "muted", box.x + 2, box.y + 30, box.w - 4);
        else if (!box.sub.empty())
            fitText(cr, box.sub, box.x + 2, box.y + 30, box.w - 4);

        if (selected)
        {
            cr->set_source_rgba(1.0, 0.85, 0.3, 0.9);
            cr->set_line_width(1.5);
            roundedRect(cr, box.x + 0.5, box.y + 0.5, box.w - 1,
                        box.h - 1, 5);
            cr->stroke();
        }

        return;
    }

    /* Stage and sink boxes share the body; sinks wear their channel. */
    double r = 1, g = 1, b = 1;
    bool isSink = box.what.kind == Selection::SINK;

    /* The box carries the file's number, 1-16; gthChannelColor speaks the
       engine's, 0-15, and the piano roll draws delivered notes with it.
       Off by one here would give every sink its neighbour's colour.
     *
       Ranged rather than merely non-negative, because this draws what
       `describe' read and describe reports a file as written -- and a
       file is only sometimes one the loader accepted. parseWork keeps
       the window up after a failed load so the error can be read and the
       piece edited, so a `channel = 0' left over from the old numbering
       reaches here, and `>= 0' let it through as channel -1: a hue no
       real channel has, on a sink that is not going to play. Out of
       range now draws in the neutral colour a stage does, which is the
       honest picture of a sink that names nothing. */
    if (isSink && box.channel >= 1 && box.channel <= TH_MIDI_CHANNELS)
        gthChannelColor(box.channel - 1, r, g, b);

    /* Opaque, in two coats: the background colour so that wires passing
       behind the box are hidden by it, then the tint that says what kind
       of box this is. Before, the tint was the only fill and every wire
       to a stage below showed straight through every stage above it. */
    roundedRect(cr, box.x, box.y, box.w, box.h, 5);
    cr->set_source_rgb(BG_R, BG_G, BG_B);
    cr->fill_preserve();
    cr->set_source_rgba(r, g, b, isSink ? 0.13 : 0.07);
    cr->fill();

    if (selected)
    {
        cr->set_source_rgba(1.0, 0.85, 0.3, 0.9);
        cr->set_line_width(1.5);
    }
    else
    {
        cr->set_source_rgba(r, g, b, isSink ? 0.6 : 0.25);
        cr->set_line_width(1);
    }

    roundedRect(cr, box.x + 0.5, box.y + 0.5, box.w - 1, box.h - 1, 5);
    cr->stroke();

    cr->set_source_rgba(1, 1, 1, 0.8);
    cr->set_font_size(10);
    fitText(cr, box.title, box.x + 4, box.y + 11, box.w - 8);

    cr->set_source_rgba(1, 1, 1, 0.45);
    cr->set_font_size(9);
    fitText(cr, box.sub, box.x + 4, box.y + box.h - 4, box.w - 8);

    /* The handle that opens the stage's params.
     *
       Three little sliders rather than a disclosure triangle, because
       nothing here expands: the params come up in a popover beside the
       box. A triangle would promise the box was about to grow, which is
       what the first version of this did and what made a chain of open
       boxes unreadable. */
    if (box.what.kind == Selection::STAGE && box.live != NULL)
    {
        double tx, ty, ts;

        twistyRect(box, tx, ty, ts);

        cr->set_line_width(1.0);
        cr->set_source_rgba(1, 1, 1, 0.45);

        for (int i = 0; i < 3; i++)
        {
            const double ly = ty + ts * (0.2 + 0.3 * i);

            cr->move_to(tx, ly);
            cr->line_to(tx + ts, ly);
            cr->stroke();

            cr->arc(tx + ts * (i == 1 ? 0.65 : 0.3), ly, 1.4, 0, 2 * M_PI);
            cr->fill();
        }
    }

    /* The algorithm's face: composer_draw inside the box, on instance
       state -- same thread, same story as the old draw strip, better
       address. Without one, the first authored params stand in. */
    double bodyY = box.y + TITLE_H;
    double bodyH = box.h - TITLE_H - 12;

    if (box.live != NULL && box.live->plugin->hasDraw())
    {
        cr->save();
        cr->rectangle(box.x + 2, bodyY, box.w - 4, bodyH);
        cr->clip();
        cr->translate(box.x + 2, bodyY);
        box.live->plugin->draw(box.live->state, cr->cobj(),
                               box.w - 4, bodyH);
        cr->restore();
    }
    else if (box.what.kind == Selection::STAGE && doc_ != NULL)
    {
        const thcGenEdit::Stage &st =
            doc_->chains[box.what.chain].stages[box.what.index];

        cr->set_source_rgba(1, 1, 1, 0.4);
        cr->set_font_size(8);

        for (size_t i = 0; i < st.params.size() && i < 4; i++)
            fitText(cr, st.params[i].name + " " + st.params[i].valueText,
                    box.x + 4, bodyY + 10 + i * 11, box.w - 8);
    }
}

/* The box for a stage, or NULL. Not `hit', which answers about a point;
 * this answers about a position in the piece. */
const ComposerCanvas::Box *
ComposerCanvas::boxFor (size_t chain, size_t stage) const
{
    for (size_t i = 0; i < boxes_.size(); i++)
        if (boxes_[i].what.kind == Selection::STAGE &&
            boxes_[i].what.chain == chain && boxes_[i].what.index == stage)
            return &boxes_[i];

    return NULL;
}

/* The knob node's value track: the full width under its name, inset so
 * the ends are reachable without hitting the box edge. */
void
ComposerCanvas::knobTrackRect (const Box &b, double &x0, double &x1,
                               double &y)
{
    x0 = b.x + 8;
    x1 = b.x + b.w - 8;
    y  = b.y + b.h - 11;
}

/* The output port, on the bottom edge, where wires leave from. */
void
ComposerCanvas::knobPortAt (const Box &b, double &x, double &y)
{
    x = b.x + b.w - 12;
    y = b.y + b.h;
}

/* Which knob node's port a point is on, or -1.
 *
 * A scan rather than a question about the box under the point, because
 * the port straddles the box's bottom edge and half of it is therefore
 * over whatever is below. Generous on purpose: it is a drag handle, and
 * PORT_GRAB is the radius that makes it one. */
int
ComposerCanvas::knobPortAt (double x, double y) const
{
    for (size_t i = 0; i < boxes_.size(); i++)
    {
        if (boxes_[i].what.kind != Selection::KNOB)
            continue;

        double px, py;

        knobPortAt(boxes_[i], px, py);

        if ((x - px) * (x - px) + (y - py) * (y - py) <=
            PORT_GRAB * PORT_GRAB)
            return (int)i;
    }

    return -1;
}

const ComposerCanvas::Box *
ComposerCanvas::knobBox (const std::string &name) const
{
    if (doc_ == NULL)
        return NULL;

    for (size_t i = 0; i < boxes_.size(); i++)
    {
        if (boxes_[i].what.kind != Selection::KNOB ||
            boxes_[i].what.index >= doc_->knobs.size())
            continue;

        if (doc_->knobs[boxes_[i].what.index].name == name)
            return &boxes_[i];
    }

    return NULL;
}

/* Every binding in the piece, as a pair of boxes.
 *
 * Read out of the live stages rather than out of the doc, because the
 * live stage is what a binding actually is: `params.knobBinding(i)' is
 * the pointer the scheduler will read through, and a doc that said
 * `@density' for a param the loader refused would draw a wire nothing
 * travels down. The doc is the spelling; this is the connection. */
void
ComposerCanvas::eachWire (const std::function<void (const Box &,
                                                    const Box &,
                                                    const std::string &)>
                          &fn) const
{
    for (size_t i = 0; i < boxes_.size(); i++)
    {
        const Box &st = boxes_[i];

        if (st.what.kind != Selection::STAGE || st.live == NULL)
            continue;

        for (int pi = 0; pi < st.live->plugin->paramCount(); pi++)
        {
            thArg *bound = st.live->params.knobBinding(pi);

            if (bound == NULL)
                continue;

            const Box *kb = knobBox(bound->name());

            if (kb == NULL)
                continue;

            const thcPlugin::ParamInfo *info =
                st.live->plugin->paramInfo(pi);

            fn(*kb, st, info != NULL ? info->name : std::string());
        }
    }
}

bool
ComposerCanvas::knobTrack (const std::string &name, double &x0, double &x1,
                           double &y) const
{
    const Box *b = knobBox(name);

    if (b == NULL)
        return false;

    knobTrackRect(*b, x0, x1, y);

    x0 *= zoom();
    x1 *= zoom();
    y  *= zoom();

    return true;
}

bool
ComposerCanvas::knobPort (const std::string &name, double &x,
                          double &y) const
{
    const Box *b = knobBox(name);

    if (b == NULL)
        return false;

    knobPortAt(*b, x, y);

    x *= zoom();
    y *= zoom();

    return true;
}

/* A box in widget pixels, which is what a popover wants to point at. */
Gdk::Rectangle
ComposerCanvas::boxRect (const Box &b) const
{
    return Gdk::Rectangle((int)(b.x * zoom()), (int)(b.y * zoom()),
                          (int)(b.w * zoom()), (int)(b.h * zoom()));
}

bool
ComposerCanvas::stageRect (size_t chain, size_t stage,
                           Gdk::Rectangle &at) const
{
    const Box *b = boxFor(chain, stage);

    if (b == NULL)
        return false;

    at = boxRect(*b);

    return true;
}

bool
ComposerCanvas::paramsHandle (size_t chain, size_t stage,
                              double &x, double &y) const
{
    const Box *b = boxFor(chain, stage);

    if (b == NULL || b->live == NULL)
        return false;

    double tx, ty, ts;

    twistyRect(*b, tx, ty, ts);

    x = (tx + ts / 2) * zoom();
    y = (ty + ts / 2) * zoom();

    return true;
}

void
ComposerCanvas::pressAt (double sx, double sy, int button, int nPress)
{
    onPressed(nPress, sx, sy, button);
}

void
ComposerCanvas::motionTo (double sx, double sy)
{
    onMotion(sx, sy);
}

void
ComposerCanvas::releaseAt (double sx, double sy, int button)
{
    onReleased(1, sx, sy, button);
}

void
ComposerCanvas::onDraw (const Cairo::RefPtr<Cairo::Context> &cr,
                        int width, int height)
{
    cr->set_source_rgb(BG_R, BG_G, BG_B);
    cr->paint();

    if (doc_ == NULL)
        return;

    /* Everything below is drawn in the coordinates rebuild() laid the
       boxes out in; the zoom is one transform at the top rather than a
       multiply on every number. Ctrl+wheel drives it, and the scrolled
       window this lives in does the scrolling. */
    cr->scale(zoom(), zoom());

    cr->select_font_face("sans", Cairo::ToyFontFace::Slant::NORMAL,
                         Cairo::ToyFontFace::Weight::NORMAL);

    /* One stage, filling everything. Drawn instead of the rows rather
       than over them: the point of enlarging is room, and a hundred-pixel
       box behind a Life board would only be something to misclick. */
    if (thcStage *big = enlargedStage())
    {
        double rx, ry, rw, rh;

        enlargedRect(rx, ry, rw, rh);

        std::string label = "stage " + std::to_string(enlarged_.index) +
            " of " + doc_->chains[enlarged_.chain].name;

        if (big->plugin->hasInput())
            label += "  --  click to change it, Escape to go back";
        else
            label += "  --  Escape to go back";

        cr->set_source_rgba(1, 1, 1, 0.55);
        cr->set_font_size(11);
        cr->move_to(rx, ry - 6);
        cr->show_text(label);

        cr->set_source_rgba(1, 1, 1, 0.06);
        cr->rectangle(rx, ry, rw, rh);
        cr->fill();

        cr->save();
        cr->rectangle(rx, ry, rw, rh);
        cr->clip();
        cr->translate(rx, ry);
        big->plugin->draw(big->state, cr->cobj(), rw, rh);
        cr->restore();

        return;
    }

    /* Wires under the arrows, and both under the boxes. */
    drawWires(cr);

    /* Arrows first, boxes over them. */
    cr->set_source_rgba(1, 1, 1, 0.25);
    cr->set_line_width(1);

    for (size_t i = 0; i + 1 < boxes_.size(); i++)
    {
        const Box &a = boxes_[i];
        const Box &b = boxes_[i + 1];

        if (a.what.kind == Selection::ADD_CHAIN ||
            b.what.kind == Selection::ADD_CHAIN)
            continue;

        /* Knob nodes are not in anyone's chain; their connections are
           wires, drawn separately. Skipped by name rather than by the
           chain test below, which they would pass by accident -- a knob
           box leaves `what.chain' at its default of zero, which is also
           the first chain. */
        if (a.what.kind == Selection::KNOB ||
            b.what.kind == Selection::KNOB)
            continue;

        if (a.what.chain != b.what.chain)
            continue;

        /* Consecutive boxes in one row: draw the flow between them,
           except into the trailing ghosts. */
        if (b.ghost)
            continue;

        double y = rowY_[a.what.chain] + STAGE_H / 2;
        double x0 = a.x + a.w, x1 = b.x;

        cr->move_to(x0 + 2, y);
        cr->line_to(x1 - 4, y);
        cr->stroke();
        cr->move_to(x1 - 3, y);
        cr->line_to(x1 - 8, y - 3.5);
        cr->line_to(x1 - 8, y + 3.5);
        cr->close_path();
        cr->fill();
    }

    int dragged = -1;

    for (size_t i = 0; i < boxes_.size(); i++)
    {
        if ((int)i == dragBox_)
        {
            dragged = (int)i;
            continue;                    /* drawn last, floating         */
        }

        drawBox(cr, boxes_[i], boxes_[i].what == sel_);
    }

    if (dragged >= 0)
    {
        /* The insertion caret, then the box in flight. */
        if (dropAt_ >= 0)
        {
            const Box &d = boxes_[dragged];
            size_t ci = d.what.chain;
            double cx = M + LABEL_W + ARROW_W +
                dropAt_ * (STAGE_W + ARROW_W) - ARROW_W / 2;

            cr->set_source_rgba(1.0, 0.85, 0.3, 0.9);
            cr->set_line_width(2);
            cr->move_to(cx, rowY_[ci] - 4);
            cr->line_to(cx, rowY_[ci] + STAGE_H + 4);
            cr->stroke();
        }

        Box floating = boxes_[dragged];

        floating.x += dragDx_;
        drawBox(cr, floating, true);
    }
}

/* ---- the enlarged view, and gestures that reach a plugin -------------- */

void
ComposerCanvas::twistyRect (const Box &b, double &x, double &y, double &s)
{
    s = TWISTY;
    x = b.x + b.w - TWISTY - 3;
    y = b.y + (TITLE_H - TWISTY) / 2;
}

thcStage *
ComposerCanvas::enlargedStage (void) const
{
    if (enlarged_.kind != Selection::STAGE || sched_ == NULL)
        return NULL;

    thcChain *c = sched_->chain(enlarged_.chain);

    if (c == NULL || enlarged_.index >= c->stages.size())
        return NULL;

    thcStage *s = c->stages[enlarged_.index].get();

    return (s != NULL && s->plugin->hasDraw()) ? s : NULL;
}

bool
ComposerCanvas::enlargedArea (double &x, double &y, double &w,
                              double &h) const
{
    /* About the view, not about the picture. enlargedStage() also asks
       whether the stage exports composer_draw, which is the right
       question for whether to draw anything and the wrong one here: the
       rectangle is where the picture *would* go, and that is geometry a
       harness can check on any piece rather than only on one whose first
       stage happens to have a face. */
    if (enlarged_.kind != Selection::STAGE)
        return false;

    enlargedRect(x, y, w, h);

    return true;
}

void
ComposerCanvas::enlargedRect (double &x, double &y, double &w,
                              double &h) const
{
    const double pad = 8;
    const double head = 18;          /* room for the label above it     */

    /* The part of the canvas that can be seen, in the same coordinates
       everything else here uses.
     *
       The widget's own size is the whole drawing, not the view: this
       canvas sizes itself to the scaled content and lives in a
       scroller, so a piece of eight chains makes get_height() eight
       chains tall. Laying the enlarged stage out in that put it at the
       top of the content rather than in front of the person -- fine
       while scrolled to the origin, and off-screen the moment they were
       not, with a plugin handed a rectangle far larger than anything
       visible. */
    double vx, vy, vw, vh;

    visibleRect(vx, vy, vw, vh);

    x = vx + pad;
    y = vy + pad + head;
    w = vw - 2 * pad;
    h = vh - 2 * pad - head;

    if (w < 1) w = 1;
    if (h < 1) h = 1;
}

void
ComposerCanvas::setEnlarged (const Selection &sel)
{
    if (enlarged_ == sel)
        return;

    enlarged_ = sel;
    feeding_ = false;

    sigEnlarged.emit(enlarged_);
    queue_draw();
}

/* The one place a gesture becomes a plugin's business.
 *
 * Only in the enlarged view, and only for a module that exports
 * composer_input. The coordinates handed over are the plugin's own --
 * the same origin and the same size composer_draw was last given -- so
 * a plugin maps a click by inverting the arithmetic it drew with, and
 * never has to know that a canvas exists. */
bool
ComposerCanvas::feedInput (thcInputType type, double x, double y,
                           int button)
{
    thcStage *s = enlargedStage();

    if (s == NULL || !s->plugin->hasInput())
        return false;

    double rx, ry, rw, rh;

    enlargedRect(rx, ry, rw, rh);

    if (x < rx || y < ry || x >= rx + rw || y >= ry + rh)
        return false;

    thcInputEvent ev;

    ev.type = type;
    ev.x = x - rx;
    ev.y = y - ry;
    ev.w = rw;
    ev.h = rh;
    ev.button = button;

    s->plugin->input(s->state, &ev);
    queue_draw();

    return true;
}

void
ComposerCanvas::onPressed (int nPress, double sx, double sy, int button)
{
    /* Every gesture arrives in widget pixels and everything below thinks
       in the laid-out coordinates the boxes were placed in. One
       conversion at the door, so nothing further in has to remember
       which of the two it is holding. */
    double x, y;

    toContent(sx, sy, x, y);

    pressX_ = x;
    pressY_ = y;

    /* A press the plugin takes is not a selection and not the start of
       a drag: the canvas gets out of the way entirely while someone is
       drawing on a board. */
    if (feedInput(THC_IN_PRESS, x, y, button))
    {
        feeding_ = true;
        feedButton_ = button;
        return;
    }

    const Box *box = hit(x, y);

    /* A knob's output port, before the hit test gets a say.
     *
       The port is centred on the box's bottom edge, so half of it is
       outside the box -- and `hit' stops at the edge, so a press on the
       lower half found no box at all and the branch below never ran.
       The target that was described as generous was in fact half of a
       small circle, and which half depended on nothing the user could
       see. Scanned directly instead, over the knob boxes, so the whole
       circle answers. */
    if (button == 1 && nPress == 1)
    {
        const int k = knobPortAt(x, y);

        if (k >= 0)
        {
            select(boxes_[k].what);
            wireFrom_ = k;
            wireX_ = x;
            wireY_ = y;
            queue_draw();
            return;
        }
    }

    /* The params handle, before anything else a press on a stage box
       could mean: it sits inside the box's title bar, so a selection or
       a drag would otherwise swallow it.
     *
       The stage is selected on the way, so the Edit panel and the
       popover are talking about the same thing -- opening a stage's
       params while the panel still shows the previous one would be two
       answers to the same question on screen at once. */
    if (box != NULL && box->what.kind == Selection::STAGE &&
        box->live != NULL && nPress == 1 && button == 1)
    {
        double tx, ty, ts;

        twistyRect(*box, tx, ty, ts);

        if (x >= tx - 3 && x <= tx + ts + 3 &&
            y >= ty - 3 && y <= ty + ts + 3)
        {
            select(box->what);
            sigParams.emit(box->what.chain, box->what.index,
                           boxRect(*box));
            return;
        }
    }

    /* A knob node's track sets the value; anywhere else on it selects
       it. (The port was taken above, before the hit test.) */
    if (box != NULL && box->what.kind == Selection::KNOB && button == 1)
    {
        double x0, x1, ty;

        knobTrackRect(*box, x0, x1, ty);

        if (y >= ty - 7 && y <= ty + 7 && x >= x0 - 6 && x <= x1 + 6)
        {
            select(box->what);
            dragKnob_ = (int)(box - &boxes_[0]);
            onMotion(sx, sy);       /* jump to where it was pressed     */
            return;
        }
    }

    /* Double-click enlarges a stage that has a picture, and a second
       one puts it back -- the same gesture both ways, because a mode
       you can enter and not leave is worse than no mode. */
    if (nPress >= 2)
    {
        if (enlarged_.kind != Selection::NONE)
        {
            setEnlarged(Selection());
            return;
        }

        if (box != NULL && box->what.kind == Selection::STAGE &&
            box->live != NULL && box->live->plugin->hasDraw())
        {
            setEnlarged(box->what);
            grab_focus();
            return;
        }
    }

    /* While enlarged, a click that missed the picture leaves the mode.
       Clicking the surround to get out is what every enlarged view in
       every program does. */
    if (enlarged_.kind != Selection::NONE)
    {
        setEnlarged(Selection());
        return;
    }

    select(box != NULL ? box->what : Selection());
}

void
ComposerCanvas::onReleased (int, double sx, double sy, int)
{
    double x, y;

    toContent(sx, sy, x, y);

    /* The committing emit. Everything during the drag was live feedback;
       this is the one the window turns into a line in the file. */
    if (dragKnob_ >= 0)
    {
        if ((size_t)dragKnob_ < boxes_.size() && doc_ != NULL &&
            boxes_[dragKnob_].what.index < doc_->knobs.size())
            sigKnob.emit(doc_->knobs[boxes_[dragKnob_].what.index].name,
                         boxes_[dragKnob_].kv, true);

        dragKnob_ = -1;
        return;
    }

    /* A wire let go of. On a stage it is a binding waiting for a param
       to be named; anywhere else it is a wire the user thought better
       of, which is what dropping on empty canvas ought to mean. */
    if (wireFrom_ >= 0)
    {
        const int from = wireFrom_;
        const Box *over = hit(x, y);

        wireFrom_ = -1;
        queue_draw();

        if (over != NULL && over->what.kind == Selection::STAGE &&
            over->live != NULL && doc_ != NULL &&
            (size_t)from < boxes_.size() &&
            boxes_[from].what.index < doc_->knobs.size())
            sigBindKnob.emit(doc_->knobs[boxes_[from].what.index].name,
                             over->what.chain, over->what.index,
                             boxRect(*over));

        return;
    }

    if (!feeding_)
        return;

    /* The button the gesture began with, not whichever one the
       controller reports now: a release that named a different button
       than its press would be a pair no plugin could match up. */
    feedInput(THC_IN_RELEASE, x, y, feedButton_);
    feeding_ = false;
}

void
ComposerCanvas::onMotion (double sx, double sy)
{
    double x, y;

    toContent(sx, sy, x, y);

    if (dragKnob_ >= 0 && (size_t)dragKnob_ < boxes_.size())
    {
        Box &b = boxes_[dragKnob_];

        double x0, x1, ty;

        knobTrackRect(b, x0, x1, ty);

        double t = x1 > x0 ? (x - x0) / (x1 - x0) : 0.0;

        t = std::clamp(t, 0.0, 1.0);

        b.kv = b.klo + (b.khi - b.klo) * t;

        if (doc_ != NULL && b.what.index < doc_->knobs.size())
            sigKnob.emit(doc_->knobs[b.what.index].name, b.kv, false);

        queue_draw();
        return;
    }

    if (wireFrom_ >= 0)
    {
        wireX_ = x;
        wireY_ = y;
        queue_draw();
        return;
    }

    if (feeding_)
        feedInput(THC_IN_DRAG, x, y, feedButton_);
}

bool
ComposerCanvas::onKey (guint keyval, guint, Gdk::ModifierType)
{
    if (keyval == GDK_KEY_Escape && enlarged_.kind != Selection::NONE)
    {
        setEnlarged(Selection());
        return true;
    }

    return false;
}

void
ComposerCanvas::onDragBegin (double sx, double sy)
{
    dragBox_ = -1;
    dragDx_ = 0;
    dropAt_ = -1;

    /* Not while someone is painting on a plugin's picture, working a
       knob or pulling a wire, and not in the enlarged view at all --
       there are no stage boxes to move. */
    if (feeding_ || dragKnob_ >= 0 || wireFrom_ >= 0 ||
        enlarged_.kind != Selection::NONE)
        return;

    double x, y;

    toContent(sx, sy, x, y);

    const Box *box = hit(x, y);

    if (box != NULL && box->what.kind == Selection::STAGE)
        dragBox_ = (int)(box - &boxes_[0]);
}

void
ComposerCanvas::onDragUpdate (double dx, double)
{
    if (dragBox_ < 0)
        return;

    /* A gesture's offset is in widget pixels and everything it is about
       to be compared against -- the box's x, the slot width -- is in
       laid-out ones. Dividing rather than calling toContent because this
       is a delta and not a point: an origin has no place in it. */
    dragDx_ = dx / zoom();

    /* Which slot would this land in? The box's center, in stage-slot
       units, clamped to the chain's stages. */
    const Box &d = boxes_[dragBox_];
    size_t ci = d.what.chain;
    size_t nStages = doc_->chains[ci].stages.size();
    double center = d.x + dragDx_ + d.w / 2;
    double first = M + LABEL_W + ARROW_W;
    int slot = (int)std::lround((center - first) / (STAGE_W + ARROW_W));

    dropAt_ = std::clamp(slot, 0, (int)nStages - 1);

    /* No caret while the drop would change nothing. */
    if (std::abs(dragDx_) < 6 || dropAt_ == (int)d.what.index)
        dropAt_ = -1;

    queue_draw();
}

void
ComposerCanvas::onDragEnd (double, double)
{
    if (dragBox_ >= 0 && dropAt_ >= 0 &&
        dropAt_ != (int)boxes_[dragBox_].what.index)
        sigMoveStage.emit(boxes_[dragBox_].what.chain,
                          (int)boxes_[dragBox_].what.index, dropAt_);

    dragBox_ = -1;
    dragDx_ = 0;
    dropAt_ = -1;
    queue_draw();
}
