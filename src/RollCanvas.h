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

#ifndef ROLL_CANVAS_H
#define ROLL_CANVAS_H 1

#include <deque>
#include <string>
#include <vector>

#include <sigc++/sigc++.h>

#include "CanvasContent.h"
#include "thcScheduler.h"

/* The tier-one composer visualizer: every chain's output on one
 * scrolling timeline, past on the left of the now-line, *scheduled
 * future* ghosted on the right. The future half is the point -- with
 * generative music you want to see what the algorithms have already
 * decided before you hear it.
 *
 * Its whole input is a scheduler. It connects sigDelivered itself and
 * keeps what arrives; once a frame it takes the transport's time, a copy
 * of the pending queue, and fits the pitch lanes to what that leaves on
 * screen. There is no event plumbing for a shell to write, on either
 * platform, which is why this extraction moved no messages: the page's
 * roll stopped being fed the worklet's tape and started being fed the
 * same signal the desktop's roll is.
 *
 * It is the content of a canvas and not a widget (CanvasContent.h): it
 * compiles without a toolkit, the desktop wraps it in a Gtk::DrawingArea
 * (src/gui/PianoRoll.h) and the browser runs the same class in the
 * mirror worker, which is the only place in the page that holds a
 * scheduler. The worklet's tape cannot answer what this draws at all --
 * a tape is what has been delivered, and half of this is what has not
 * been.
 *
 * Everything runs on one thread wherever it runs: the scheduler delivers
 * on the thread that draws. Unlike Keyboard, which takes events across
 * the MIDI Dispatcher hop and needs its snapshot-under-lock dance, this
 * class's producer and consumer are the same thread. No mutex, on
 * purpose.
 *
 * On being a CanvasContent when the base is written for something else.
 * That base is for a drawing bigger than its shell -- an extent, a fit, a
 * scrolled viewport. The roll is the opposite: the drawing is always
 * exactly the view, a drag means scrub rather than pan, and a wheel means
 * *time* zoom rather than scale. So contentExtent() answers with the
 * shell's own size, which pins the zoom at 1 (zoomToFit and zoomToWidth
 * both compute a factor of exactly 1 from a drawing that is its own view,
 * so they are no-ops without needing to be written as any) and lets both
 * shells -- GraphCanvas's scroller arithmetic on the desktop, and
 * canvasview.js in the browser -- work with no change. That is the entire
 * reason to use the base class here.
 */
class RollCanvas : public CanvasContent
{
public:
    explicit RollCanvas (thcScheduler *sched);
    ~RollCanvas (void);

    /* seconds of history and of lookahead on screen */
    void SetTimeSpan (double past, double future);

    /* Drop everything kept, and go back to live at zero.
     *
     * What a transport reset does, and this is the signal handler it does
     * it through -- public because a *load* is the same thing said
     * differently and not every host spells it as a reset. The desktop's
     * reload rewinds the scheduler, so the signal is enough there; the
     * browser's tw_piece_load stops the transport and loads over the top,
     * and history kept across that is the previous piece's notes drawn
     * against this one's clock. */
    void clear (void);

    /* Draws everything, having first taken this frame's view of the
     * scheduler -- the transport's time while following, one copy of the
     * pending queue, the prune and the pitch-range ease.
     *
     * Inside the draw rather than beside it because this drawing is a
     * function of the piece and how many frames of it have been drawn:
     * the ease and the follow advance one step per picture, so a shell
     * that took its step somewhere else would be a shell that could take
     * a different number of them. Two shells drawing the same seeded
     * piece at the same frame produce the same list, and rollcheck.mjs
     * is that sentence as a gate. */
    void draw (const Cairo::RefPtr<Cairo::Context> &cr, int width,
               int height) override;

    /* The gestures, in shell pixels. A horizontal drag scrubs back
     * through history and drops out of follow mode; a double-click (or
     * scrubbing back up to the live edge) resumes following.
     *
     * The desktop's controllers call these and so does the browser's
     * pointer handling, which is the same arrangement ComposerCanvas
     * has. A press is the only thing that starts a scrub: motion with
     * nothing held is a pointer crossing the widget. */
    void pressAt (double x, double y, int button, int nPress);
    void motionTo (double x, double y);
    void releaseAt (double x, double y, int button);

    /* A wheel notch, as the shell reports it: how much bigger the view
     * should get.
     *
     * A factor, because that is what canvasview.js already sends for a
     * ctrl-wheel (`zoomBy') and keeping the two shells identical is
     * worth more than a message of this roll's own. What is scaled is
     * the *span* -- the seconds on screen -- and not the drawing, which
     * is the one thing here that reads a shell's number as something
     * other than what the shell calls it. A factor above one means
     * "bigger", so it shows *less* time; RollCanvas.cpp says it at
     * length where the division is. */
    void zoomBy (double factor);

    /* What the view is on, for a harness: the time at the now-line, and
       whether it is still tracking the transport. Pressing a drag on the
       roll and watching these is how a test says "it scrubbed". */
    double viewNow (void) const { return viewNow_; }
    bool   following (void) const { return following_; }
    double spanPast (void) const { return spanPast_; }
    double spanFuture (void) const { return spanFuture_; }

protected:
    /* The shell's own size: the roll's drawing is always exactly the
       view. See the header comment for why that is the whole of what
       this class owes the base. */
    void contentExtent (double &w, double &h) const override;

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

    /* A structure edit: the piece rebuilding its own instrument.
     *
     * Drawn because everything schedulable is drawn -- a rule structure
     * edits inherited rather than invented, and it is what makes an
     * edit debuggable: you watch one arrive instead of wondering why
     * the sound changed. Kept as text because that is what it is; a
     * swap has no value to plot. */
    struct Edit
    {
        double      at;
        int         channel;
        std::string label;
    };

    void   onDelivered (const thcEvent &ev);
    void   onTransportReset (void);

    /* This frame's view of the scheduler, taken once by draw(). */
    void   step (void);

    /* History older than anyone could scrub to, dropped. Called on every
       delivery rather than once a frame -- RollCanvas.cpp says why. */
    void   prune (void);

    double timeToX (double t, int width) const;
    void   fitPitchRange (void);

    /* How wide the shell is, in its own pixels, for a scrub that has to
       turn a distance into seconds. Zero before anything is laid out,
       which is a division a scrub must not do. */
    double shellWidth (void) const;

    thcScheduler        *sched_;
    std::deque<Note>     notes_;      /* delivered; pruned off the left  */
    std::deque<ArgTick>  argTicks_;   /* delivered chanarg events        */
    std::deque<Edit>     edits_;      /* delivered structure edits       */

    /* This frame's copy of the scheduled future, taken once per draw
       and read by both the range fit and the draw. */
    std::vector<thcEvent> pendingView_;

    double spanPast_, spanFuture_;    /* seconds each side of now        */
    double viewNow_;                  /* time at the now-line            */
    bool   following_;                /* viewNow_ tracks the transport   */
    bool   dragging_;                 /* a button is down on the roll    */
    double dragX0_;                   /* where the drag began, in pixels */
    double dragT0_;                   /* view time when the drag began   */

    /* Pitch range auto-fits what is on screen, but by easing toward the
       fitted range rather than jumping to it -- a new lowest note slides
       the view open instead of snapping every lane's height. */
    double loShown_, hiShown_;        /* fractional lanes, eased         */
    int    loFit_, hiFit_;            /* target range from the notes     */

    /* The size of the last picture drawn, which is what a gesture is
       against. It is also the answer to contentExtent when there is no
       shell to ask -- a canvas built by a harness has none, and a drag
       on one would otherwise divide by a width nobody had said. */
    int    lastW_, lastH_;

    sigc::connection deliveredConn_;
    sigc::connection resetConn_;
};

#endif /* ROLL_CANVAS_H */
