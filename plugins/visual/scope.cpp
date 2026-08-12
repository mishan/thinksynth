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

/*
 * visual/scope -- the waveform at a point in the graph.
 *
 * The one everybody wants, and the one whose whole difficulty is a single
 * problem: a scope that draws the newest N samples every frame shows a
 * waveform sliding sideways at the beat frequency between the signal and the
 * refresh rate. It is unreadable, and it looks like a bug in the synth rather
 * than in the display.
 *
 * The fix is what a hardware scope does: pick a repeatable place in the signal
 * to start drawing from, and start there every time. That is the trigger, and
 * it is most of this file.
 *
 * Everything here runs on the GUI thread. See src/thVisual.h.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <new>

#include "thVisual.h"

namespace {

/* Just under a tenth of a second at 44.1k. The trigger searches backwards
   through this, so it bounds how far back a repeatable starting point can be
   looked for -- and at 20Hz, the lowest thing anyone will point this at, one
   cycle is 2205 samples, so there is room for a couple. */
const unsigned int HISTORY = 4096;

/* How much signal the panel spans, in samples per pixel at 1:1. Two is about
   right for a 128-pixel panel: 256 samples is 5.8ms, which is two cycles of
   350Hz and twenty of 3.5kHz. Wider and a bass note is a straight line;
   narrower and anything bright is a solid block. */
const double SAMPLES_PER_PX = 2.0;

struct Scope {
    float ring[HISTORY];
    unsigned int at;        /* where the next sample goes */
    unsigned long seen;     /* total ever fed */

    bool sawNonFinite;

    Scope (void) : at(0), seen(0), sawNonFinite(false)
    {
        memset(ring, 0, sizeof(ring));
    }

    /* i counts backwards from the newest sample: 0 is the newest. */
    float back (unsigned int i) const
    {
        return ring[(at + HISTORY - 1 - (i % HISTORY)) % HISTORY];
    }
};

/* Same reasoning as visual/meter: a comparison against a NaN is always false,
   so a scope that filtered them by `if (x > lo && x < hi)' would draw a
   perfectly calm line for a signal that had blown up. They are counted and
   said, not hidden. */
bool finite (float x)
{
    return !(x != x) && x < 1e30f && x > -1e30f;
}

/* Where to start drawing, counted backwards from the newest sample.
 *
 * A rising zero crossing, searched from oldest to newest within the part of
 * the history that is not needed to fill the display -- so the slice that gets
 * drawn is the most recent one that starts on a crossing.
 *
 * Deliberately a pure function of the ring. It runs at draw time rather than
 * at feed time, which is what makes the display independent of where the feed
 * boundaries fell -- the property visualcheck asserts and the one a naive
 * implementation quietly breaks by remembering the trigger it found last time.
 *
 * Zero crossing rather than a level: at a level, a signal that never reaches
 * it never triggers, and the level that suits an envelope's output suits
 * nothing else. Every audio signal in the corpus crosses zero.
 */
unsigned int triggerOffset (const Scope *s, unsigned int span)
{
    if (span + 2 >= HISTORY)
        return span;    /* no room to search; free-run */

    const unsigned int oldest = HISTORY - 2;

    /* From the oldest usable point forwards, remembering the newest crossing
       found. Walking backwards from the newest and stopping at the first
       crossing would lock onto the *last* one before the display window,
       which for anything but a pure tone jitters between harmonics. */
    unsigned int found = span;
    bool any = false;

    for (unsigned int i = oldest; i > span; i--)
    {
        const float a = s->back(i);
        const float b = s->back(i - 1);

        if (!finite(a) || !finite(b))
            continue;

        if (a <= 0.0f && b > 0.0f)
        {
            found = i;
            any = true;
        }
    }

    if (!any)
        return span;    /* silence, or DC: free-run and show the newest */

    return found;
}

void drawText (cairo_t *cr, double x, double y, const char *s, double size)
{
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, s);
}

} /* namespace */

int visual_init (thVisual *visual)
{
    visual->setName("scope");
    visual->setDesc("The waveform, triggered on a rising zero crossing");

    /* Tall enough to see shape in. A waveform in 24 pixels is a smear. */
    visual->setPreferredSize(128, 56);

    return 0;
}

void *visual_open (thVisual *visual, unsigned int samplerate)
{
    (void)visual;
    (void)samplerate;   /* the trigger works in samples; the rate is only
                           needed to *say* a time base, which this does not */

    /* std::nothrow, because this is a C ABI boundary.
     *
     * thVisual::open calls this through a function pointer. A bad_alloc thrown
     * here would unwind across that -- out of a dlopen'd module and into a host
     * that has no catch anywhere near it -- and terminate the process. A
     * visualizer failing to allocate should cost a panel, not the synth. */
    return new (std::nothrow) Scope();
}

int visual_feed (void *inst, const float *samples, unsigned int n)
{
    Scope *s = (Scope *)inst;

    if (s == NULL || samples == NULL || n == 0)
        return 0;

    /* More than the ring holds: only the tail can matter, and copying the rest
       would be writing over itself. A drain that has fallen this far behind
       has already lost the middle. */
    if (n > HISTORY)
    {
        samples += n - HISTORY;
        s->seen += n - HISTORY;
        n = HISTORY;
    }

    for (unsigned int i = 0; i < n; i++)
    {
        const float x = samples[i];

        if (!finite(x))
        {
            s->sawNonFinite = true;
            s->ring[s->at] = 0.0f;
        }
        else
            s->ring[s->at] = x;

        s->at = (s->at + 1) % HISTORY;
    }

    s->seen += n;

    return 0;
}

int visual_draw (void *inst, cairo_t *cr, int w, int h)
{
    Scope *s = (Scope *)inst;

    if (s == NULL)
        return 0;

    cairo_set_source_rgb(cr, 0.11, 0.12, 0.14);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    if (w < 8 || h < 6)
        return 0;

    const double mid = h * 0.5;

    /* The rails and the centre. Full scale is TH_MAX == 1, so the top and
       bottom of the box are +1 and -1 and a waveform that touches them is at
       the limiter's ceiling. */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.10);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 0, (double)(int)mid + 0.5);
    cairo_line_to(cr, w, (double)(int)mid + 0.5);
    cairo_stroke(cr);

    unsigned int span = (unsigned int)(w * SAMPLES_PER_PX);

    if (span < 2)
        span = 2;

    if (span > HISTORY - 4)
        span = HISTORY - 4;

    if (s->seen == 0)
    {
        cairo_set_source_rgb(cr, 0.55, 0.57, 0.62);
        drawText(cr, 4.0, h - 4.0, "--", 8.0);
        return 0;
    }

    const unsigned int start = triggerOffset(s, span);

    /* The trace. One vertical span per pixel column rather than one sample:
       at 2 samples per pixel a polyline through every other sample aliases
       badly on anything bright, and drawing the min and max of what falls in
       each column is what makes a square wave look square. */
    cairo_set_source_rgb(cr, 0.45, 0.80, 0.60);
    cairo_set_line_width(cr, 1.0);

    bool clipped = false;

    for (int px = 0; px < w; px++)
    {
        const unsigned int from = start - (unsigned int)
                                  ((double)px * span / (double)w);
        const unsigned int to = start - (unsigned int)
                                ((double)(px + 1) * span / (double)w);

        double lo = 1e30, hi = -1e30;

        for (unsigned int i = to; i <= from && i <= start; i++)
        {
            const float v = s->back(i);

            if (v < lo) lo = v;
            if (v > hi) hi = v;

            if (v > 1.0f || v < -1.0f)
                clipped = true;
        }

        if (lo > hi)
            continue;

        /* Clamped for drawing only. A diverging filter reaches 1e5 and a trace
           drawn to scale would be a vertical line off both edges of the panel,
           which says less than a trace pinned to the rails does. */
        if (lo < -1.0) lo = -1.0;
        if (hi > 1.0) hi = 1.0;

        const double y0 = mid - hi * (mid - 1.0);
        const double y1 = mid - lo * (mid - 1.0);

        cairo_move_to(cr, px + 0.5, y0);
        cairo_line_to(cr, px + 0.5, y1 + (y1 - y0 < 1.0 ? 1.0 : 0.0));
    }

    cairo_stroke(cr);

    /* Two things worth saying over the trace, and only when they are true. */
    if (s->sawNonFinite)
    {
        cairo_set_source_rgb(cr, 0.95, 0.35, 0.35);
        drawText(cr, 4.0, 10.0, "NOT FINITE", 8.0);
    }
    else if (clipped)
    {
        cairo_set_source_rgb(cr, 0.95, 0.65, 0.30);
        drawText(cr, 4.0, 10.0, "over", 8.0);
    }

    return 0;
}

void visual_close (void *inst)
{
    delete (Scope *)inst;
}

void visual_cleanup (thVisual *visual)
{
    (void)visual;
}
