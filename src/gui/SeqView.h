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

#ifndef SEQ_VIEW_H
#define SEQ_VIEW_H

#include <string>
#include <vector>

#include <gtkmm.h>

#include "thcGenEdit.h"

class thcScheduler;
struct thcStage;

/* The piece as tracks: one row per `gen::grid' stage, each drawn by the
 * plugin's own composer_draw at the pane's full width and clicked through
 * its composer_input.
 *
 * The web page's sequencer pane (wasm/web/seqview.js), on the desktop's
 * terms: the scheduler is in this process, so a draw is a call into the
 * plugin with the widget's cairo, and a gesture reaches the plugin at
 * once, as it does on the Composer canvas.
 *
 * A track is as tall as its grid has rows -- `rows' is a param, so it is
 * read from the live stage on every tick rather than remembered. Nothing
 * here decides what a click means: the pointer's position goes to the
 * plugin in the coordinates it drew with, and the plugin inverts its own
 * arithmetic.
 */
class SeqView : public Gtk::ScrolledWindow
{
public:
    SeqView (void);

    /* The piece, after a load or a reload: the tracks are rebuilt. NULL
       for either empties the pane. */
    void setPiece (const thcGenEdit::Doc *doc, thcScheduler *sched);

    /* A frame: every track redrawn and its height checked against its
       grid's `rows'. The Composer's draw timer calls it while the pane is
       in view. */
    void tick (void);

    /* The headings again: what plays a channel can change without the
       piece reloading, when somebody loads a patch onto it. */
    void refresh (void);

    size_t trackCount (void) const { return tracks_.size(); }

    /* The grid of track `i', for its size. NULL out of range. */
    Gtk::DrawingArea *trackArea (size_t i) const
    {
        return i < tracks_.size() ? tracks_[i].area : NULL;
    }

    /* A gesture on track `i', at (x, y) in its own pixels: a
       thcInputType, and the button the press came down with. What the
       drag controller calls, and what a harness presses with. The end
       of a gesture is signal_edited's to announce. */
    void input (size_t i, int type, double x, double y, int button);

    /* A gesture on a track has ended: the stage at doc chain `chain',
       doc stage `stage' was edited. */
    sigc::signal<void (size_t, size_t)> &signal_edited (void)
    {
        return edited_;
    }

protected:
    struct Track
    {
        size_t chain;           /* the chain, in doc and scheduler order */
        size_t docStage;        /* the stage in the document            */
        int    liveStage;       /* and in the scheduler's list          */
        int    channel;         /* 0-15, or -1 for a chain with no sink */
        std::string name;       /* the chain's                          */

        Gtk::DrawingArea *area;
        Gtk::Label *what;

        int rows;               /* what the height was last fitted to   */
        int button;             /* the press's, while a drag is on      */
    };

    thcStage *stageOf (const Track &t) const;

    /* The height a track wants for `rows' rows of its grid. */
    static int heightFor (int rows);
    int rowsOf (const Track &t) const;

    /* What is on a track's channel, in the words its heading wants. */
    std::string describe (const Track &t) const;

    void draw (size_t i, const Cairo::RefPtr<Cairo::Context> &cr, int w,
               int h);
    void say (void);

    const thcGenEdit::Doc *doc_;
    thcScheduler *sched_;

    std::vector<Track> tracks_;

    Gtk::Box box_{Gtk::Orientation::VERTICAL};
    Gtk::Box list_{Gtk::Orientation::VERTICAL};
    Gtk::Label hint_;

    sigc::signal<void (size_t, size_t)> edited_;
};

#endif /* SEQ_VIEW_H */
