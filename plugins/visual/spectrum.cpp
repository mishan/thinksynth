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
 * visual/spectrum -- what is in the signal, by frequency.
 *
 * The question a scope cannot answer. A filter's cutoff, an oscillator's
 * harmonic content, whether a "sine" is actually a sine: all of them are one
 * look at this and a guess at a waveform.
 *
 * Everything here runs on the GUI thread. See src/thVisual.h.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <new>

#include "thVisual.h"
#include "fftr.h"

namespace {

/* 1024 points: 43Hz per bin at 44.1k, and 23ms of signal.
 *
 * The trade is the usual one and worth stating. Doubling it halves the bin
 * width -- useful, since 43Hz is wider than a semitone below about 700Hz --
 * and doubles the window, so a note's attack smears across more of it. For
 * looking at a held tone's harmonics, which is what this is for, 1024 is the
 * size that shows the harmonics without turning every transient into a wall. */
const unsigned int ORDER = 10;

/* The floor of the dB scale. Below this everything is off the bottom of the
   panel. -84 is fourteen 6dB steps, which covers the useful range of a synth
   voice without spending half the height on the noise floor. */
const double FLOOR_DB = -84.0;

/* How fast a peak falls when the signal goes away, in dB per second. Slow
   enough to see what just happened, fast enough not to lie for long. */
const double FALL_DB_PER_SEC = 40.0;

struct Spectrum {
    thv::FFTR fft;

    unsigned int samplerate;

    /* The frame the transform reads, as a ring: the newest `n' samples,
       whenever anyone asks. A ring rather than a buffer that fills and
       triggers, because a spectrum has no trigger -- every frame is the last
       n samples and there is nothing to align. */
    float *ring;
    unsigned int at;

    unsigned long seen;

    /* Magnitudes, in dB, with a decay. Held across frames so a display that is
       drawn faster than the audio arrives does not flicker between a fresh
       transform and nothing. */
    float *db;

    bool sawNonFinite;

    /* Samples fed since the last transform. draw() runs the FFT only when
       there is new signal to transform, so drawing twice in a row is the same
       picture twice -- which visualcheck requires and which also happens to be
       the cheaper thing. */
    unsigned int fresh;

    Spectrum (unsigned int rate)
        : fft(ORDER), samplerate(rate ? rate : TH_VISUAL_DEFAULT_RATE),
          ring(NULL), at(0), seen(0), db(NULL), sawNonFinite(false), fresh(0)
    {
        if (!fft.ok())
            return;

        ring = new (std::nothrow) float[fft.size()];
        db = new (std::nothrow) float[fft.bins()];

        if (!ring || !db)
            return;

        memset(ring, 0, fft.size() * sizeof(float));

        for (unsigned int i = 0; i < fft.bins(); i++)
            db[i] = (float)FLOOR_DB;
    }

    ~Spectrum (void)
    {
        delete [] ring;
        delete [] db;
    }

    bool ok (void) const { return fft.ok() && ring && db; }

    /* Reads the ring oldest-first, which is the order the window expects. */
    struct Frame {
        const float *ring;
        unsigned int at, n;

        float operator() (unsigned int i) const
        {
            return ring[(at + i) % n];
        }
    };
};

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

/* Frequency to x, logarithmically, from 20Hz to Nyquist.
 *
 * Log because pitch is: on a linear axis the top octave is half the panel and
 * everything a bass note does is in the first three pixels. 20Hz rather than 0
 * because zero has no logarithm and nothing below 20 is audible anyway. */
double freqToX (double hz, double lo, double hi, int w)
{
    if (hz < lo)
        hz = lo;

    return (log(hz / lo) / log(hi / lo)) * (double)w;
}

} /* namespace */

int visual_init (thVisual *visual)
{
    visual->setName("spectrum");
    visual->setDesc("Frequency content, log axis, dBFS");

    visual->setPreferredSize(128, 64);

    return 0;
}

void *visual_open (thVisual *visual, unsigned int samplerate)
{
    (void)visual;

    Spectrum *s = new Spectrum(samplerate);

    if (!s->ok())
    {
        /* Allocation failed. Refusing is the honest answer: a panel that
           reported nothing forever would look like a bug in the tap. */
        delete s;
        return NULL;
    }

    return s;
}

int visual_feed (void *inst, const float *samples, unsigned int n)
{
    Spectrum *s = (Spectrum *)inst;

    if (s == NULL || samples == NULL || n == 0)
        return 0;

    const unsigned int size = s->fft.size();

    if (n > size)
    {
        /* Only the tail can be in the next frame. */
        samples += n - size;
        s->seen += n - size;
        s->fresh += n - size;
        n = size;
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

        s->at = (s->at + 1) % size;
    }

    s->seen += n;
    s->fresh += n;

    return 0;
}

int visual_draw (void *inst, cairo_t *cr, int w, int h)
{
    Spectrum *s = (Spectrum *)inst;

    if (s == NULL || !s->ok())
        return 0;

    cairo_set_source_rgb(cr, 0.11, 0.12, 0.14);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    if (w < 8 || h < 6)
        return 0;

    const double nyquist = s->samplerate * 0.5;
    const double lo = 20.0;

    /* Transform, but only if something arrived. Two draws in a row with no
       feed between them must be the same picture -- see `fresh'. */
    if (s->fresh > 0)
    {
        float *mag = new (std::nothrow) float[s->fft.bins()];

        if (mag)
        {
            Spectrum::Frame frame = { s->ring, s->at, s->fft.size() };

            s->fft.magnitude(frame, mag);

            /* The decay is per transform rather than per second of signal,
               which is close enough: a transform happens whenever a frame's
               worth of new audio has been drawn, and the two rates track. */
            const double fall = FALL_DB_PER_SEC *
                                ((double)s->fresh / (double)s->samplerate);

            for (unsigned int i = 0; i < s->fft.bins(); i++)
            {
                double d = (mag[i] > 1e-7)
                           ? 20.0 * log10((double)mag[i]) : FLOOR_DB;

                if (d < FLOOR_DB)
                    d = FLOOR_DB;

                /* Rise instantly, fall slowly. A spectrum that averaged both
                   ways would show neither the peak nor the shape. */
                if (d >= s->db[i])
                    s->db[i] = (float)d;
                else
                {
                    s->db[i] -= (float)fall;

                    if (s->db[i] < d)
                        s->db[i] = (float)d;
                }
            }

            delete [] mag;
        }

        s->fresh = 0;
    }

    /* Octave rules, so the axis is readable without labels on a 128-pixel
       panel. Every octave from 100Hz: 100, 200, 400, 800... */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.07);
    cairo_set_line_width(cr, 1.0);

    for (double f = 100.0; f < nyquist; f *= 2.0)
    {
        const double x = freqToX(f, lo, nyquist, w);

        cairo_move_to(cr, (double)(int)x + 0.5, 0);
        cairo_line_to(cr, (double)(int)x + 0.5, h);
    }

    cairo_stroke(cr);

    if (s->seen == 0)
    {
        cairo_set_source_rgb(cr, 0.55, 0.57, 0.62);
        drawText(cr, 4.0, h - 4.0, "--", 8.0);
        return 0;
    }

    /* The curve, as a filled area under it.
     *
     * Filled rather than a line because at 128 pixels wide a line through
     * 512 bins is a scribble, and what anyone reads off a spectrum at that
     * size is the shape of the envelope rather than individual partials.
     *
     * One vertical per pixel column, taking the loudest bin that falls in it,
     * for the same reason the scope takes min and max: 512 bins into 128
     * columns means four bins a column, and averaging them would hide exactly
     * the narrow peak anyone is looking for. */
    cairo_move_to(cr, 0, h);

    for (int px = 0; px < w; px++)
    {
        /* Which bins land in this column. Inverted from freqToX, so the two
           cannot disagree about where a frequency is. */
        const double f0 = lo * pow(nyquist / lo, (double)px / (double)w);
        const double f1 = lo * pow(nyquist / lo, (double)(px + 1) /
                                                 (double)w);

        unsigned int b0 = (unsigned int)(f0 / nyquist * (double)s->fft.bins());
        unsigned int b1 = (unsigned int)(f1 / nyquist * (double)s->fft.bins());

        if (b1 <= b0)
            b1 = b0 + 1;

        if (b0 >= s->fft.bins())
            b0 = s->fft.bins() - 1;

        if (b1 > s->fft.bins())
            b1 = s->fft.bins();

        double best = FLOOR_DB;

        for (unsigned int b = b0; b < b1; b++)
            if (s->db[b] > best)
                best = s->db[b];

        const double y = h - (best - FLOOR_DB) / (0.0 - FLOOR_DB) * h;

        cairo_line_to(cr, px + 0.5, y);
    }

    cairo_line_to(cr, w, h);
    cairo_close_path(cr);

    cairo_set_source_rgba(cr, 0.45, 0.65, 0.85, 0.55);
    cairo_fill_preserve(cr);

    cairo_set_source_rgb(cr, 0.60, 0.80, 0.95);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    if (s->sawNonFinite)
    {
        cairo_set_source_rgb(cr, 0.95, 0.35, 0.35);
        drawText(cr, 4.0, 10.0, "NOT FINITE", 8.0);
    }

    return 0;
}

void visual_close (void *inst)
{
    delete (Spectrum *)inst;
}

void visual_cleanup (thVisual *visual)
{
    (void)visual;
}
