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

#include <deque>
#include <gtkmm.h>

#include "thcScheduler.h"

/* The tier-one composer visualizer: every chain's output on one scrolling
 * timeline, past on the left of the now-line, *scheduled future* ghosted on
 * the right. The future half is the point -- with generative music you want
 * to see what the algorithms have already decided before you hear it.
 *
 * Everything here runs on the GUI thread: the scheduler delivers on it, GTK
 * draws on it. Unlike Keyboard, which takes events across the MIDI
 * Dispatcher hop and needs its snapshot-under-lock dance, this widget's
 * producer and consumer are the same thread. No mutex, on purpose.
 */
class PianoRoll : public Gtk::DrawingArea
{
public:
    explicit PianoRoll (thcScheduler *sched);
    ~PianoRoll (void);

    /* seconds of history and of lookahead on screen */
    void SetTimeSpan (double past, double future);

protected:
    void onDraw (const Cairo::RefPtr<Cairo::Context> &cr, int width,
                 int height);
    bool onTick (const Glib::RefPtr<Gdk::FrameClock> &clock);
    void onDelivered (const thcEvent &ev);

    /* horizontal drag scrubs back through history and drops out of
     * follow mode; double-click (or scrubbing back to the live edge)
     * resumes following. Scroll wheel zooms time about the pointer. */
    void onDragBegin  (double x, double y);
    void onDragUpdate (double dx, double dy);
    bool onScroll     (double dx, double dy);
    void onDoubleClick (int nPress, double x, double y);

private:
    struct Note
    {
        double start, duration;
        int    channel, note, velocity;
    };
    struct ArgTick
    {
        double at;
        int    channel;
        float  value;
    };

    double timeToX (double t, int width) const;
    void   fitPitchRange (void);
    void   prune (void);

    thcScheduler        *sched_;
    std::deque<Note>     notes_;      /* delivered; pruned off the left  */
    std::deque<ArgTick>  argTicks_;   /* delivered chanarg events        */

    double spanPast_, spanFuture_;    /* seconds each side of now        */
    double viewNow_;                  /* time at the now-line            */
    bool   following_;                /* viewNow_ tracks the transport   */
    double dragT0_;                   /* view time when the drag began   */

    /* Pitch range auto-fits what is on screen, but by easing toward the
     * fitted range rather than jumping to it -- a new lowest note slides
     * the view open instead of snapping every lane's height. */
    double loShown_, hiShown_;        /* fractional lanes, eased         */
    int    loFit_, hiFit_;            /* target range from the notes     */

    sigc::connection deliveredConn_;
    guint            tickId_;
};

#endif /* PIANOROLL_H */
