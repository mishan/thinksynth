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

#include <algorithm>

#include <gtkmm.h>

#include "think.h"

#include "thUtil.h"
#include "thcPlugin.h"
#include "thcScheduler.h"
#include "gthPatchfile.h"
#include "thcGenFile.h"
#include "Access.h"
#include "SeqView.h"

/* A row of the grid, in pixels, and what a track may be at its shortest
   and tallest: the web page's numbers (seqview.js). A drum track is one
   row and a keys track is six or eight; both want to be hittable without
   either running the pane off the screen. */
static const int ROW = 24;          /* WCAG 2.5.8's smallest target */
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
        t.area->set_cursor("crosshair");

        /* A group of cells, as the keys are, by its number and what is
           on it: GTK puts the application role on the bus as a frame. */
        t.area->set_focusable(true);
        t.area->property_accessible_role() = Gtk::Accessible::Role::GROUP;
        nameAs(*t.area, "Track " + std::to_string(i + 1) + ": " + t.name);
        gtk_accessible_update_property(
            GTK_ACCESSIBLE(t.area->gobj()),
            GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
            "Arrows move between cells, Home and End along the row. Enter "
            "or Space puts a note in a cell, accents it, or clears it; "
            "Delete erases it. Shift with Right or Left holds a note a step "
            "longer or shorter.", -1);

        Glib::RefPtr<Gtk::EventControllerKey> keys =
            Gtk::EventControllerKey::create();

        keys->signal_key_pressed().connect(
            [this, i](guint keyval, guint, Gdk::ModifierType state)
            { return key(i, keyval, state); }, false);
        t.area->add_controller(keys);

        /* The cursor is drawn while the track has the focus. */
        Glib::RefPtr<Gtk::EventControllerFocus> focus =
            Gtk::EventControllerFocus::create();
        Gtk::DrawingArea *area = t.area;

        focus->signal_enter().connect([area] { area->queue_draw(); });
        focus->signal_leave().connect([area] { area->queue_draw(); });
        t.area->add_controller(focus);
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

    return std::max(1, std::min(32, (int)s->params.get(p)));
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

    /* The keys' cell, while they are on this track: white with a black
       edge, which reads on a cell lit or dark. */
    const Track &t = tracks_[i];

    if (t.area->has_focus())
    {
        const double cw = (double)w / stepsOf(t);
        const double ch = (double)h / rowsOf(t);

        cr->rectangle(std::min(t.curStep, stepsOf(t) - 1) * cw + 1.5,
                      std::min(t.curRow, rowsOf(t) - 1) * ch + 1.5,
                      cw - 3, ch - 3);
        cr->set_line_width(3);
        cr->set_source_rgb(0, 0, 0);
        cr->stroke_preserve();
        cr->set_line_width(1.5);
        cr->set_source_rgb(1, 1, 1);
        cr->stroke();
    }
}

int
SeqView::stepsOf (const Track &t) const
{
    const thcStage *s = stageOf(t);
    const int p = s == NULL ? -1 : s->plugin->paramIndex("steps");

    if (p < 0)
        return 16;

    /* Cut and held in range as the grid does (grid.cpp), so a gesture's
       columns are the grid's. */
    return std::max(1, std::min(64, (int)s->params.get(p)));
}

std::string
SeqView::trackCells (size_t i) const
{
    const thcStage *s = i < tracks_.size() ? stageOf(tracks_[i]) : NULL;

    if (s == NULL || !s->plugin->hasCapture())
        return "";

    return s->plugin->capture(s->state, s->plugin->paramIndex("cells"));
}

/* The cells' text read as the grid writes it (grid.cpp): rows split at
   `/', top first, `.' off, `x' a note, `X' an accent, `-' a tie. */
static std::vector<std::string>
rowsIn (const std::string &cells)
{
    std::vector<std::string> rows(1);

    for (char c : cells)
        if (c == '/')
            rows.emplace_back();
        else if (c != '|' && c != ' ')
            rows.back() += c;

    return rows;
}

std::string
SeqView::trackSaid (size_t i) const
{
    if (i >= tracks_.size())
        return "";

    const Track &t = tracks_[i];
    const thcStage *s = stageOf(t);
    const std::vector<std::string> rows = rowsIn(trackCells(i));

    /* The cursor as far in as the grid now goes, which a knob can shrink
       under it. */
    const int row = std::min(t.curRow, rowsOf(t) - 1);
    const int step = std::min(t.curStep, stepsOf(t) - 1);
    const std::string cells = row < (int)rows.size() ? rows[row] : "";
    const char c = step < (int)cells.size() ? cells[step] : '.';

    /* The row's pitch, from the grid's ladder climbing from the bottom an
       octave each time round (grid.cpp, noteOf), as a reader says it. */
    std::string pitch = "row " + std::to_string(row + 1);
    const int np = s == NULL ? -1 : s->plugin->paramIndex("notes");

    if (np >= 0)
    {
        /* As the grid reads it (grid.cpp, parseLadder): numbers, kept
           only on the MIDI range, and middle C for none. */
        std::vector<int> ladder;
        const std::string text = s->params.getString(np);

        for (const char *p = text.c_str(); *p != '\0';)
        {
            char *end;
            const long n = strtol(p, &end, 10);

            if (end == p)
                break;

            if (n >= 0 && n <= 127)
                ladder.push_back((int)n);

            p = *end == ',' ? end + 1 : end;
        }

        if (ladder.empty())
            ladder.push_back(60);

        const int y = rowsOf(t) - 1 - row;
        const int n = (int)ladder.size();
        std::string name = thcGenLoader::noteName(
            ladder[y % n] + 12 * (y / n));

        if (name.size() > 2 && name[1] == 'b')
            name = name.substr(0, 1) + " flat " + name.substr(2);

        /* Above the MIDI range a row plays nothing, and has no name. */
        if (!name.empty())
            pitch = name;
    }

    int held = 1;

    while ((c == 'x' || c == 'X') && step + held < (int)cells.size() &&
           cells[step + held] == '-')
        held++;

    const std::string what = c == 'x' ? "note" : c == 'X' ? "accent"
                           : c == '-' ? "held" : "empty";

    return pitch + ", step " + std::to_string(step + 1) + ": " + what +
           (held > 1 ? ", held " + std::to_string(held) + " steps" : "");
}

void
SeqView::gesture (size_t i, int button, int row, int from,
                  const std::vector<int> &through)
{
    const Track &t = tracks_[i];
    const double cw = (double)t.area->get_width() / stepsOf(t);
    const double ch = (double)t.area->get_height() / rowsOf(t);
    const double y = (row + 0.5) * ch;

    input(i, THC_IN_PRESS, (from + 0.5) * cw, y, button);

    for (int s : through)
        input(i, THC_IN_DRAG, (s + 0.5) * cw, y, button);

    input(i, THC_IN_RELEASE,
          ((through.empty() ? from : through.back()) + 0.5) * cw, y, button);
    edited_.emit(t.chain, t.docStage);
}

/* A cursor on a cell, and the press a pointer would make there for each
   key, so the grid's own rules decide what it does: a press on an empty
   cell paints a note, a tap on a note accents it and on an accent clears
   it, the other button erases, and a drag from a note along its row holds
   it (grid.cpp). Then said, from the grid as it now is. */
bool
SeqView::key (size_t i, guint keyval, Gdk::ModifierType state)
{
    if (i >= tracks_.size() ||
        (state & (Gdk::ModifierType::ALT_MASK |
                  Gdk::ModifierType::CONTROL_MASK)) != Gdk::ModifierType(0))
        return false;

    Track &t = tracks_[i];
    const int rows = rowsOf(t);
    const int steps = stepsOf(t);

    t.curRow = std::min(t.curRow, rows - 1);
    t.curStep = std::min(t.curStep, steps - 1);

    const bool shift = (state & Gdk::ModifierType::SHIFT_MASK) !=
                       Gdk::ModifierType(0);
    const std::vector<std::string> grid = rowsIn(trackCells(i));
    const std::string row = t.curRow < (int)grid.size() ? grid[t.curRow]
                                                        : "";
    const char c = t.curStep < (int)row.size() ? row[t.curStep] : '.';
    const bool note = c == 'x' || c == 'X';
    auto go = [&](int r, int s)
    {
        t.curRow = std::max(0, std::min(rows - 1, r));
        t.curStep = std::max(0, std::min(steps - 1, s));
    };

    switch (keyval)
    {
        case GDK_KEY_Left:
        case GDK_KEY_Right:
        {
            const int way = keyval == GDK_KEY_Right ? 1 : -1;

            if (!shift)
            {
                go(t.curRow, t.curStep + way);
                break;
            }

            /* A note held a step longer or shorter: up to the next note,
               which a hold stops at, and back to one step by a drag that
               leaves the note and comes back, since a press and release
               without leaving it is a tap, which accents it. */
            if (!note)
                return true;

            int n = 1;

            while (t.curStep + n < (int)row.size() &&
                   row[t.curStep + n] == '-')
                n++;

            const int to = way > 0 ? n : n - 2;

            if (to < 0 || t.curStep + to >= steps ||
                (to >= n && row[t.curStep + to] != '.'))
                return true;

            gesture(i, 1, t.curRow, t.curStep,
                    to == 0 ? std::vector<int>{t.curStep + 1, t.curStep}
                            : std::vector<int>{t.curStep + to});
            break;
        }

        case GDK_KEY_Up:    go(t.curRow - 1, t.curStep); break;
        case GDK_KEY_Down:  go(t.curRow + 1, t.curStep); break;
        case GDK_KEY_Home:  go(t.curRow, 0); break;
        case GDK_KEY_End:   go(t.curRow, steps - 1); break;

        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
        case GDK_KEY_space:
            gesture(i, 1, t.curRow, t.curStep, {});
            break;

        case GDK_KEY_Delete:
        case GDK_KEY_BackSpace:
            gesture(i, 3, t.curRow, t.curStep, {});
            break;

        default:
            return false;
    }

#if GTK_CHECK_VERSION(4, 14, 0)
    const std::string said = trackSaid(i);

    gtk_accessible_announce(GTK_ACCESSIBLE(t.area->gobj()), said.c_str(),
                            GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
#endif
    t.area->queue_draw();
    return true;
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
