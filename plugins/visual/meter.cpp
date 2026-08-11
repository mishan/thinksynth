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
 * visual/meter -- peak and RMS at a point in the graph.
 *
 * The first visual plugin, and deliberately the least interesting to look at.
 * It is the smallest thing that exercises open/feed/draw/close, and getting
 * the ABI wrong is much cheaper to discover against a hundred lines than
 * against a waterfall.
 *
 * It also earns its place. REVIVAL.md §6 is an account of a static that was
 * ultimately a headroom problem -- voices summing coherently past full scale
 * -- diagnosed by measuring peaks offline with scripts/dsplevel. This is that
 * measurement at an arbitrary node, live.
 *
 * Everything here runs on the GUI thread. See src/thVisual.h.
 */

#define VISUAL_PLUGIN_BUILD 1

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "thVisual.h"

namespace {

/* Peak falls at this many dB per second once the signal drops below it, which
   is roughly what a hardware peak meter does. Slow enough to read, fast enough
   not to lie about a transient thirty seconds later. */
const double PEAK_FALL_DB_PER_SEC = 20.0;

/* How long the RMS window is. 300ms is the usual VU-ish integration time; it
   is long enough that the number stops flickering and short enough to follow
   a phrase. */
const double RMS_SECONDS = 0.3;

/* Where the bar changes colour. Full scale is TH_MAX == 1, and the limiter's
   knee is at 0.7 (TH_LIMIT_KNEE), so that is the honest place to warn: above
   it thSoftLimit is bending the signal. Spelled as a number rather than
   included from think.h because a visual plugin has no business depending on
   the engine -- if the knee moves, this comment is the thing that has to
   move with it. */
const double KNEE = 0.7;

struct Meter {
    unsigned int samplerate;

    double peak;        /* decaying peak, linear                       */
    double truePeak;    /* the highest sample ever seen, never decayed */
    double meanSquare;  /* running mean of x^2                         */

    unsigned long seen; /* samples fed, ever */

    bool sawNonFinite;  /* a NaN or an inf came through */

    Meter (void)
        : samplerate(TH_VISUAL_DEFAULT_RATE), peak(0), truePeak(0),
          meanSquare(0), seen(0), sawNonFinite(false) { }
};

/* A NaN reaching a display is not hypothetical: REVIVAL.md records four DSPs
   whose filters diverge, and mixer.out on dsp/noargs/bd1.dsp reads -inf within
   seven windows. Comparing a NaN never yields true, so peak tracking would
   silently ignore it and the meter would read a confident 0.0 for a signal
   that has blown up -- which is precisely the case someone has a meter open
   for. So they are counted and shown, not filtered. */
bool finite (float x)
{
    return !(x != x) && x < 1e30f && x > -1e30f;
}

void drawText (cairo_t *cr, double x, double y, const char *s, double size)
{
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, s);
}

/* Linear to dBFS, with a floor so -inf never has to be printed. */
double dB (double linear)
{
    if (linear <= 1e-6)
        return -120.0;

    return 20.0 * log10(linear);
}

} /* namespace */

int visual_init (thVisual *visual)
{
    visual->setName("meter");
    visual->setDesc("Peak and RMS level, in dBFS");

    /* One row. A meter is a number and a bar; giving it the height of a scope
       would waste the canvas's scarcest axis on white space. */
    visual->setPreferredSize(128, 24);

    return 0;
}

void *visual_open (thVisual *visual, unsigned int samplerate)
{
    (void)visual;

    Meter *m = new Meter();

    /* A sample rate of zero would make every decay per-sample constant below
       infinite. The host should not pass one, and this does not depend on the
       host being careful. */
    if (samplerate > 0)
        m->samplerate = samplerate;

    return m;
}

int visual_feed (void *inst, const float *samples, unsigned int n)
{
    Meter *m = (Meter *)inst;

    if (m == NULL || samples == NULL || n == 0)
        return 0;

    /* Per-sample coefficients, derived once per call rather than per sample. */
    const double fall = pow(10.0, -PEAK_FALL_DB_PER_SEC /
                                  (20.0 * (double)m->samplerate));

    double rmsCoeff = 1.0 / (RMS_SECONDS * (double)m->samplerate);

    if (rmsCoeff > 1.0)
        rmsCoeff = 1.0;

    for (unsigned int i = 0; i < n; i++)
    {
        const float x = samples[i];

        if (!finite(x))
        {
            m->sawNonFinite = true;
            continue;   /* deliberately not folded into peak or RMS */
        }

        const double a = fabs((double)x);

        if (a > m->peak)
            m->peak = a;
        else
            m->peak *= fall;

        if (a > m->truePeak)
            m->truePeak = a;

        m->meanSquare += ((double)x * (double)x - m->meanSquare) * rmsCoeff;
    }

    m->seen += n;

    return 0;
}

int visual_draw (void *inst, cairo_t *cr, int w, int h)
{
    Meter *m = (Meter *)inst;

    if (m == NULL)
        return 0;

    const double rms = sqrt(m->meanSquare);

    /* Background. */
    cairo_set_source_rgb(cr, 0.11, 0.12, 0.14);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    /* At one or two pixels there is nothing to say and every division below
       would be drawing sub-pixel detail nobody can see. The box still gets
       filled so it does not read as a hole. */
    if (w < 8 || h < 6)
        return 0;

    const double pad = 2.0;
    const double barH = (h >= 18) ? h * 0.42 : h - pad * 2;
    const double barW = w - pad * 2;

    /* The bar: RMS filled solid, peak as a single bright rule on top of it.
       Both clamped, because a diverging DSP reaches 1e5 and a bar drawn to
       scale would simply be "full" with no way to tell how full. */
    double rmsFrac = rms;
    double peakFrac = m->peak;

    if (rmsFrac > 1.0) rmsFrac = 1.0;
    if (peakFrac > 1.0) peakFrac = 1.0;

    cairo_set_source_rgb(cr, 0.18, 0.19, 0.22);
    cairo_rectangle(cr, pad, pad, barW, barH);
    cairo_fill(cr);

    if (rmsFrac > 0.0)
    {
        if (m->peak > 1.0)
            cairo_set_source_rgb(cr, 0.85, 0.25, 0.25);   /* over full scale */
        else if (m->peak > KNEE)
            cairo_set_source_rgb(cr, 0.85, 0.65, 0.20);   /* the limiter is
                                                             bending it */
        else
            cairo_set_source_rgb(cr, 0.35, 0.70, 0.45);

        cairo_rectangle(cr, pad, pad, barW * rmsFrac, barH);
        cairo_fill(cr);
    }

    if (peakFrac > 0.0)
    {
        cairo_set_source_rgb(cr, 0.92, 0.92, 0.95);
        cairo_rectangle(cr, pad + barW * peakFrac - 1.0, pad, 2.0, barH);
        cairo_fill(cr);
    }

    /* The knee, so "the limiter starts here" is visible rather than implied. */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.25);
    cairo_rectangle(cr, pad + barW * KNEE, pad, 1.0, barH);
    cairo_fill(cr);

    if (h < 18)
        return 0;   /* no room for the numbers */

    char label[64];

    if (m->sawNonFinite)
    {
        /* Said first and said plainly. A meter that read "-inf dB" while the
           signal was actually a NaN would be the most misleading thing on the
           canvas. */
        snprintf(label, sizeof(label), "NOT FINITE");
        cairo_set_source_rgb(cr, 0.95, 0.35, 0.35);
    }
    else if (m->seen == 0)
    {
        snprintf(label, sizeof(label), "--");
        cairo_set_source_rgb(cr, 0.55, 0.57, 0.62);
    }
    else
    {
        snprintf(label, sizeof(label), "%.1f pk  %.1f rms", dB(m->truePeak),
                 dB(rms));
        cairo_set_source_rgb(cr, 0.78, 0.80, 0.85);
    }

    drawText(cr, pad, h - pad - 1.0, label, (h >= 24) ? 9.0 : 8.0);

    return 0;
}

void visual_close (void *inst)
{
    delete (Meter *)inst;
}

void visual_cleanup (thVisual *visual)
{
    (void)visual;
}
