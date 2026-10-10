/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#include <gtkmm.h>

#include "think.h"

#include "thUtil.h"
#include "thcPlugin.h"
#include "thcScheduler.h"
#include "gthPatchfile.h"
#include "SeqView.h"

/* A row of the grid, in pixels, and what a track may be at its shortest
   and tallest: the web page's numbers (seqview.js). A drum track is one
   row and a keys track is six or eight; both want to be hittable without
   either running the pane off the screen. */
static const int ROW = 18;
static const int MIN_H = 26;
static const int MAX_H = 240;

/* The heading's width, beside the grid rather than over it: a row saved
   per track, and a drum's one-row strip no longer shorter than its
   heading. */
static const int HEAD_W = 160;

SeqView::SeqView (void)
    : doc_(NULL), sched_(NULL)
{
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    set_hexpand(true);
    set_vexpand(true);

    list_.set_spacing(8);

    hint_.set_wrap(true);
    hint_.set_xalign(0);
    hint_.add_css_class("dim-label");

    box_.set_margin(8);
    box_.set_spacing(12);
    box_.append(list_);
    box_.append(hint_);

    set_child(box_);

    say();
}

void
SeqView::setPiece (const thcGenEdit::Doc *doc, thcScheduler *sched)
{
    doc_ = doc;
    sched_ = sched;

    tracks_.clear();

    while (Gtk::Widget *w = list_.get_first_child())
        list_.remove(*w);

    if (doc_ == NULL || sched_ == NULL)
    {
        say();
        return;
    }

    /* Every `gen::grid', in the piece's own order. By plugin name and only
       that one: a pane that showed every stage with a picture would be the
       canvas with the wires rubbed out, and what makes this one worth
       having is that every row is the same kind of thing. */
    for (size_t ci = 0; ci < doc_->chains.size(); ci++)
    {
        const thcGenEdit::Chain &chain = doc_->chains[ci];
        const thcChain *live = sched_->chain(ci);

        if (live == NULL)
            continue;

        int channel = -1;

        for (size_t k = 0; k < live->sinks.size(); k++)
            if (live->sinks[k].isNotes())
            {
                channel = live->sinks[k].channel;
                break;
            }

        /* The graph the chain's instrument plays, as the file names it: a
           chain whose sink is a raw channel has none to change. */
        std::string dsp;

        for (size_t k = 0; k < chain.sinks.size() && dsp.empty(); k++)
            if (!chain.sinks[k].instrument.empty() &&
                chain.sinks[k].chanarg.empty())
                for (size_t n = 0; n < doc_->instruments.size(); n++)
                    if (doc_->instruments[n].name == chain.sinks[k].instrument)
                        dsp = doc_->instruments[n].dsp;

        for (size_t si = 0; si < chain.stages.size(); si++)
        {
            const int at = thcGenEdit::liveIndex(chain, si);

            if (at < 0 || (size_t)at >= live->stages.size())
                continue;

            const thcStage *s = live->stages[at].get();

            if (s == NULL || s->plugin == NULL ||
                s->plugin->name() != "grid" || !s->plugin->hasDraw())
                continue;

            Track t;

            t.chain = ci;
            t.docStage = si;
            t.liveStage = at;
            t.channel = channel;
            t.name = chain.name;
            t.dsp = dsp;
            t.area = NULL;
            t.what = NULL;
            t.pick = NULL;
            t.rows = 0;
            t.button = 0;

            tracks_.push_back(t);
        }
    }

    for (size_t i = 0; i < tracks_.size(); i++)
    {
        Track &t = tracks_[i];

        Gtk::Box *row = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 8));
        Gtk::Box *head = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 6));

        /* The file's numbering, 1-16, which is what Channels says. */
        Gtk::Label *num = manage(new Gtk::Label(
            t.channel >= 0 ? std::to_string(t.channel + 1) : "-"));

        num->add_css_class("dim-label");
        num->add_css_class("monospace");
        num->set_width_chars(2);
        num->set_xalign(1);
        num->set_valign(Gtk::Align::START);

        t.what = manage(new Gtk::Label);
        t.what->set_xalign(0);
        t.what->set_valign(Gtk::Align::START);
        t.what->set_ellipsize(Pango::EllipsizeMode::END);

        head->append(*num);

        if (choosing_ && !t.dsp.empty())
        {
            const std::string title = title_ ? title_(t.dsp) : t.dsp;

            t.pick = manage(new Gtk::Button);
            t.pick->set_child(*t.what);
            t.pick->set_valign(Gtk::Align::START);
            t.pick->set_hexpand(true);
            t.pick->set_tooltip_text(title + " (" + t.dsp + ") plays " +
                                     t.name + ". Click to choose another.");
            t.what->set_text(title.empty() ? t.dsp : title);
            t.pick->signal_clicked().connect(
                [this, i]
                {
                    if (i < tracks_.size())
                        choose_.emit(tracks_[i].chain, tracks_[i].docStage,
                                     tracks_[i].dsp);
                });
            head->append(*t.pick);
        }
        else
            head->append(*t.what);

        /* Its own width and no more: a button that fills the heading
           would otherwise hand its expanding up, and the heading would
           take half the row from the grid. */
        head->set_size_request(HEAD_W, -1);
        head->set_hexpand(false);

        t.rows = rowsOf(t);
        t.area = manage(new Gtk::DrawingArea);
        t.area->set_hexpand(true);
        t.area->set_content_height(heightFor(t.rows));
        t.area->set_focusable(true);
        t.area->set_cursor("crosshair");
        t.area->set_draw_func(
            [this, i](const Cairo::RefPtr<Cairo::Context> &cr, int w, int h)
            { draw(i, cr, w, h); });

        /* One drag per press, whichever button: the plugin is told which,
           and the secondary one erases. The press's button rather than the
           one a later event reports, because a release naming another
           button than its press is a pair no plugin can match up. */
        Glib::RefPtr<Gtk::GestureDrag> drag = Gtk::GestureDrag::create();

        /* The handlers hold the gesture by pointer: the gesture holds
           them, and a reference back would keep both alive after the
           track is gone. */
        Gtk::GestureDrag *g = drag.get();

        drag->set_button(0);
        drag->signal_drag_begin().connect(
            [this, i, g](double x, double y)
            {
                if (i >= tracks_.size())
                    return;

                tracks_[i].button = (int)g->get_current_button();
                input(i, THC_IN_PRESS, x, y, tracks_[i].button);
            });
        drag->signal_drag_update().connect(
            [this, i, g](double dx, double dy)
            {
                double x, y;

                if (i < tracks_.size() && g->get_start_point(x, y))
                    input(i, THC_IN_DRAG, x + dx, y + dy,
                          tracks_[i].button);
            });
        drag->signal_drag_end().connect(
            [this, i, g](double dx, double dy)
            {
                double x, y;

                if (i >= tracks_.size())
                    return;

                if (g->get_start_point(x, y))
                    input(i, THC_IN_RELEASE, x + dx, y + dy,
                          tracks_[i].button);

                const Track &done = tracks_[i];

                edited_.emit(done.chain, done.docStage);
            });
        t.area->add_controller(drag);

        row->append(*head);
        row->append(*t.area);
        list_.append(*row);
    }

    refresh();
    say();
}

thcStage *
SeqView::stageOf (const Track &t) const
{
    if (sched_ == NULL)
        return NULL;

    thcChain *c = sched_->chain(t.chain);

    if (c == NULL || t.liveStage < 0 ||
        (size_t)t.liveStage >= c->stages.size())
        return NULL;

    thcStage *s = c->stages[t.liveStage].get();

    return s != NULL && s->state != NULL && s->plugin != NULL ? s : NULL;
}

int
SeqView::heightFor (int rows)
{
    const int h = rows * ROW;

    return h < MIN_H ? MIN_H : h > MAX_H ? MAX_H : h;
}

/* Asked of the live stage, not the file: the page sets `rows' when the
   instrument under a track ignores the note, and a knob or a Selection
   edit can move it too. */
int
SeqView::rowsOf (const Track &t) const
{
    const thcStage *s = stageOf(t);

    if (s == NULL)
        return 1;

    const int p = s->plugin->paramIndex("rows");

    if (p < 0)
        return 1;

    const int rows = (int)(s->params.get(p) + 0.5);

    return rows < 1 ? 1 : rows;
}

/* The name the piece gave the instrument on the channel, or the patch
   loaded there, or the chain's own name when there is neither. */
std::string
SeqView::describe (const Track &t) const
{
    if (t.channel < 0 || sched_ == NULL)
        return t.name;

    const std::vector<thcInstrument> &insts = sched_->instruments();

    for (size_t i = 0; i < insts.size(); i++)
        if (insts[i].channel == t.channel)
            return insts[i].name;

    gthPatchManager *pm = gthPatchManager::instance();
    gthPatchManager::PatchFile *patch =
        pm != NULL ? pm->getPatch(t.channel) : NULL;

    if (patch != NULL)
    {
        if (!patch->filename.empty())
        {
            std::string name = thUtil::basename(patch->filename.c_str());
            const std::string ext = ".patch";

            if (name.size() > ext.size() &&
                name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
                name.erase(name.size() - ext.size());

            return name;
        }

        if (!patch->doc.dsp.empty())
            return thUtil::basename(patch->doc.dsp.c_str());
    }

    return t.name;
}

void
SeqView::setChoosing (bool on,
                      std::function<std::string (const std::string &)> title)
{
    if (title)
        title_ = title;

    if (on == choosing_)
        return;

    choosing_ = on;

    /* The headings are made with the tracks, so they are made again. */
    setPiece(doc_, sched_);
}

void
SeqView::refresh (void)
{
    for (size_t i = 0; i < tracks_.size(); i++)
        if (tracks_[i].what != NULL && tracks_[i].pick == NULL)
        {
            const std::string what = describe(tracks_[i]);

            tracks_[i].what->set_text(what);
            tracks_[i].what->set_tooltip_text(what);
        }
}

void
SeqView::tick (void)
{
    for (size_t i = 0; i < tracks_.size(); i++)
    {
        Track &t = tracks_[i];
        const int rows = rowsOf(t);

        if (rows != t.rows)
        {
            t.rows = rows;
            t.area->set_content_height(heightFor(rows));
        }

        t.area->queue_draw();
    }
}

void
SeqView::draw (size_t i, const Cairo::RefPtr<Cairo::Context> &cr, int w,
               int h)
{
    if (i >= tracks_.size() || w <= 0 || h <= 0)
        return;

    thcStage *s = stageOf(tracks_[i]);

    if (s == NULL)
        return;

    /* The page's ground under the cells (style.css's .trackgrid): the
       grid draws only what is on it, and the theme's own background in
       light mode washes the pale cells out. */
    cr->save();
    cr->set_source_rgb(0x14 / 255.0, 0x16 / 255.0, 0x1a / 255.0);
    cr->paint();
    cr->restore();

    cr->save();
    s->plugin->draw(s->state, cr->cobj(), w, h);
    cr->restore();
}

/* The one place a gesture becomes the plugin's business: in the
   coordinates the draw was given, which are the widget's own, and with
   the size it was drawn at. */
void
SeqView::input (size_t i, int type, double x, double y, int button)
{
    if (i >= tracks_.size())
        return;

    Track &t = tracks_[i];
    thcStage *s = stageOf(t);

    if (s == NULL || !s->plugin->hasInput())
        return;

    thcInputEvent ev;

    ev.type = (thcInputType)type;
    ev.x = x;
    ev.y = y;
    ev.w = t.area->get_width();
    ev.h = t.area->get_height();
    ev.button = button > 0 ? button : 1;

    s->plugin->input(s->state, &ev);
    t.area->queue_draw();
}

void
SeqView::say (void)
{
    hint_.set_text(tracks_.empty()
        ? "This piece has no grid tracks. Open a piece with gen::grid "
          "stages in it -- gen/scratch.gen is five of them -- and they "
          "appear here."
        : "Click a cell for a note, again to accent it, again to clear "
          "it. Drag from a note to the right to hold it over the steps "
          "you cover, and back to shorten it. Drag across empty cells to "
          "draw a run of notes, and use the other button to erase. What "
          "you draw goes into the piece, and Save keeps it.");
}
