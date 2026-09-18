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

#include "config.h"

#include <algorithm>

#include "CanvasContent.h"

/* The same bounds NodeCanvas has always had. A quarter is where a large
 * patch stops being legible and three is where a small one stops gaining
 * anything. */
#define ZOOM_MIN  0.25
#define ZOOM_MAX  3.0

/* Below this, in either direction, a view has not been laid out yet and
   fitting to it would be fitting to nothing. */
#define VIEW_MIN  32

CanvasContent::CanvasContent (void)
    : zoom_(1.0), fitPending_(false), fitWidthPending_(false)
{
}

CanvasContent::~CanvasContent (void)
{
}

void
CanvasContent::toContent (double sx, double sy, double &cx, double &cy) const
{
    cx = sx / zoom_;
    cy = sy / zoom_;
}

void
CanvasContent::contentResized (void)
{
    double w = 0, h = 0;

    contentExtent(w, h);

    if (w > 0 && h > 0)
        resizeShell((int)(w * zoom_), (int)(h * zoom_));
}

void
CanvasContent::setZoom (double z)
{
    z = std::min(std::max(z, (double)ZOOM_MIN), (double)ZOOM_MAX);

    /* Somebody has now said what the zoom is, so the fit that was waiting
       for a viewport is answered and must not fire later: a deferred fit
       surviving this came back on the next shellResized() and threw away
       a zoom the user had chosen in between. Cleared before the early
       return, because choosing the zoom it is already at is still
       choosing it. (zoomToFit and zoomToWidth clear it themselves first,
       so this takes nothing from them.) */
    fitPending_ = false;

    if (z == zoom_)
        return;

    zoom_ = z;

    contentResized();
    requestRedraw();
}

void
CanvasContent::visibleRect (double &x, double &y, double &w, double &h) const
{
    double px = 0, py = 0, pw = 0, ph = 0;

    if (shellViewport(px, py, pw, ph) && pw >= 1 && ph >= 1)
    {
        x = px / zoom_;
        y = py / zoom_;
        w = pw / zoom_;
        h = ph / zoom_;
        return;
    }

    /* No view to speak of: the whole drawing is what can be seen. */
    x = y = 0;
    contentExtent(w, h);

    if (w < 0) w = 0;
    if (h < 0) h = 0;
}

void
CanvasContent::zoomToFit (void)
{
    double gw = 0, gh = 0;

    contentExtent(gw, gh);

    if (gw <= 0 || gh <= 0)
        return;

    double px = 0, py = 0, pw = 0, ph = 0;

    if (!shellViewport(px, py, pw, ph) || pw < VIEW_MIN || ph < VIEW_MIN)
    {
        /* Nothing allocated yet -- this is the first open, before the
           shell has laid anything out. Try again when it has. */
        fitPending_ = true;
        fitWidthPending_ = false;
        return;
    }

    fitPending_ = false;

    double z = std::min(pw / gw, ph / gh);

    if (z > 1.0)
        z = 1.0;

    setZoom(z);

    /* setZoom returns early when the zoom did not change, which on a
       first fit of a drawing that already fits is exactly what happens
       -- and the size request still has to be made. */
    contentResized();
}

/* Fit the width alone, and scroll for the rest.
 *
 * For a drawing that is tall and narrow -- the composer's canvas is one
 * row per chain, and a piece with ten chains is ten rows -- fitting both
 * dimensions means the height decides the zoom, and a piece in a box half
 * a screen tall comes out at a quarter scale and unreadable. What such a
 * drawing wants is to be as wide as the view and scrolled down through,
 * which is what the scroller around it is for.
 *
 * Never magnifying, for zoomToFit's reason: the point is to bring an
 * oversized drawing down, not to blow a small one up.
 */
void
CanvasContent::zoomToWidth (void)
{
    double gw = 0, gh = 0;

    contentExtent(gw, gh);

    if (gw <= 0 || gh <= 0)
        return;

    double px = 0, py = 0, pw = 0, ph = 0;

    if (!shellViewport(px, py, pw, ph) || pw < VIEW_MIN || ph < VIEW_MIN)
    {
        fitPending_ = true;
        fitWidthPending_ = true;
        return;
    }

    fitPending_ = false;

    setZoom(std::min(1.0, pw / gw));
    contentResized();
}

void
CanvasContent::shellResized (void)
{
    if (!fitPending_)
        return;

    /* The fit that was asked for, not whichever one this file happens to
       call: a view that asked to be fitted to its width and was told to
       wait must not come back fitted to its height. */
    if (fitWidthPending_)
        zoomToWidth();
    else
        zoomToFit();
}
