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

#include <algorithm>
#include <cmath>

#include "think.h"

/* Belt to <cmath>'s braces: ucrt only defines M_PI under
 * _USE_MATH_DEFINES, and this file should not depend on the gtkmm
 * include chain having smuggled it in the way NodeCanvas gets away
 * with. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

ComposerCanvas::ComposerCanvas (void)
    : doc_(NULL), sched_(NULL), dragBox_(-1), dragDx_(0), dropAt_(-1)
{
    set_draw_func(sigc::mem_fun(*this, &ComposerCanvas::onDraw));

    auto click = Gtk::GestureClick::create();

    click->signal_pressed().connect(
        sigc::mem_fun(*this, &ComposerCanvas::onPressed));
    add_controller(click);

    auto drag = Gtk::GestureDrag::create();

    drag->signal_drag_begin().connect(
        sigc::mem_fun(*this, &ComposerCanvas::onDragBegin));
    drag->signal_drag_update().connect(
        sigc::mem_fun(*this, &ComposerCanvas::onDragUpdate));
    drag->signal_drag_end().connect(
        sigc::mem_fun(*this, &ComposerCanvas::onDragEnd));
    add_controller(drag);
}

void
ComposerCanvas::SetPiece (const thcGenEdit::Doc *doc, thcScheduler *sched)
{
    doc_ = doc;
    sched_ = sched;
    dragBox_ = -1;
    dropAt_ = -1;

    rebuild();

    /* The selection may name things the new piece does not have. */
    if (sel_.kind != Selection::NONE && sel_.kind != Selection::ADD_CHAIN)
    {
        bool ok = doc_ != NULL && sel_.chain < doc_->chains.size();

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

void
ComposerCanvas::drawBox (const Cairo::RefPtr<Cairo::Context> &cr,
                         const Box &box, bool selected) const
{
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

    if (isSink && box.channel >= 0)
        gthChannelColor(box.channel, r, g, b);

    cr->set_source_rgba(r, g, b, isSink ? 0.13 : 0.07);
    roundedRect(cr, box.x, box.y, box.w, box.h, 5);
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

void
ComposerCanvas::onDraw (const Cairo::RefPtr<Cairo::Context> &cr,
                        int width, int height)
{
    cr->set_source_rgb(0.09, 0.09, 0.11);
    cr->paint();

    if (doc_ == NULL)
        return;

    cr->select_font_face("sans", Cairo::ToyFontFace::Slant::NORMAL,
                         Cairo::ToyFontFace::Weight::NORMAL);

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

void
ComposerCanvas::onPressed (int, double x, double y)
{
    pressX_ = x;
    pressY_ = y;

    const Box *box = hit(x, y);

    select(box != NULL ? box->what : Selection());
}

void
ComposerCanvas::onDragBegin (double x, double y)
{
    dragBox_ = -1;
    dragDx_ = 0;
    dropAt_ = -1;

    const Box *box = hit(x, y);

    if (box != NULL && box->what.kind == Selection::STAGE)
        dragBox_ = (int)(box - &boxes_[0]);
}

void
ComposerCanvas::onDragUpdate (double dx, double)
{
    if (dragBox_ < 0)
        return;

    dragDx_ = dx;

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
