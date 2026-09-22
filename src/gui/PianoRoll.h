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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef PIANOROLL_H
#define PIANOROLL_H

#include <gtkmm.h>

#include "../RollCanvas.h"

/*
 * RollCanvas as a widget: the desktop shell, and nothing but.
 *
 * What the roll draws, what it keeps, and what a drag or a wheel on it
 * means are RollCanvas's and are the same code the browser runs
 * (src/RollCanvas.h). This is the four answers a shell owes it, the
 * controllers that feed its gestures, and the frame clock.
 *
 * Not a GraphCanvas, which is the shell the other two canvases use, for
 * the two reasons that base exists: it hunts for the Gtk::ScrolledWindow
 * around the canvas and it answers a resize with set_size_request. The
 * roll has no scroller on purpose -- its x axis is time and its own
 * business, and a scroller around it would be a second, silent answer to
 * where "now" is -- and it lives in a Gtk::Paned that has to be able to
 * shrink it, which a size request pinned to its current height would
 * stop. So: a drawing area, and the shell written out.
 */
class PianoRoll : public RollCanvas, public Gtk::DrawingArea
{
public:
    explicit PianoRoll (thcScheduler *sched);
    ~PianoRoll (void);

protected:
    void requestRedraw (void) override { queue_draw(); }

    /* Deliberately nothing. The drawing is the view (RollCanvas.h), so
       the only size this could ask for is the size it already has -- and
       asking for it would pin the Paned's handle where it happened to be
       the first time the roll was drawn. */
    void resizeShell (int w, int h) override { (void)w; (void)h; }

    bool shellViewport (double &x, double &y,
                        double &w, double &h) const override;

private:
    void onDraw (const Cairo::RefPtr<Cairo::Context> &cr, int width,
                 int height);
    bool onTick (const Glib::RefPtr<Gdk::FrameClock> &clock);
    bool onScroll (double dx, double dy);
};

#endif /* PIANOROLL_H */
