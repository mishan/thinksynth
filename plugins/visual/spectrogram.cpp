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
 * visual/spectrogram -- the spectrum over time.
 *
 * The one that answers the questions the other three cannot, because all three
 * of them show an instant and a synth patch is a shape in time. Where the
 * filter sweep actually goes, whether the attack has the click you can hear,
 * whether a "decaying" partial decays: none of that is one frame of anything.
 *
 * This is the most expensive of the four modules, and the only one that
 * does real work per hop rather than per frame. It is also the only one that
 * has to be careful about *when* it works: a display drawn at 30fps that
 * transformed on demand would show a different number of columns depending on
 * how the drain happened to split the audio.
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

/* 512 points, not the spectrum's 1024.
 *
 * A spectrogram is read for *when* rather than for *what*, and the trade runs
 * the other way: 512 is 12ms of signal instead of 23, so an attack transient
 * lands in one or two columns rather than smearing across four. The cost is
 * 86Hz bins instead of 43, which matters for resolving close partials and does
 * not matter for watching a filter sweep. */
const unsigned int ORDER = 9;

/* Overlap, as a fraction of the transform. A hop of a quarter means each
   sample is seen by four transforms, which is what stops a Hann window's
   troughs from making the picture flicker at the hop rate. */
const unsigned int HOP_DIVISOR = 4;

/* How many columns of history are kept. At a 128-sample hop and 44.1k a column
   is 2.9ms, so 256 columns is three quarters of a second -- about as much of a
   note as anyone looks at at once, and 256 * 256 floats is a quarter of a
   megabyte, which is not worth economising. */
const unsigned int COLUMNS = 256;

/* Vertical resolution: how many rows the frequency axis is resampled onto.
   Independent of the panel height so that the history does not have to be
   rebuilt when the panel is resized -- and so that an enlarged view shows more
   than an upscaled thumbnail. */
const unsigned int ROWS = 128;

const double FLOOR_DB = -84.0;

struct Gram {
    thv::FFTR fft;

    unsigned int samplerate;

    /* The sliding frame the transform reads. */
    float *ring;
    unsigned int at;

    /* Samples since the last transform. A hop happens every hop() samples,
       counted here rather than at feed boundaries -- which is the whole reason
       a module can be fed 373 samples at a time and still produce evenly
       spaced columns. */
    unsigned int sinceHop;

    unsigned long seen;

    /* The history, ROWS per column, newest at `col'. dB, already resampled
       onto the log frequency axis, so drawing is a blit rather than a
       transform. */
    float *cells;
    unsigned int col;
    unsigned int filled;

    bool sawNonFinite;

    /* Scratch, allocated once: the tick must not allocate thirty times a
       second and a hop happens more often than that. */
    float *mag;

    /* Which bin each row reads, precomputed -- the log mapping is fixed once
       the sample rate is known and recomputing it per hop would be the most
       expensive thing here after the transform itself. */
    unsigned int *rowBin0;
    unsigned int *rowBin1;

    Gram (unsigned int rate)
        : fft(ORDER), samplerate(rate ? rate : TH_VISUAL_DEFAULT_RATE),
          ring(NULL), at(0), sinceHop(0), seen(0),
          cells(NULL), col(0), filled(0), sawNonFinite(false),
          mag(NULL), rowBin0(NULL), rowBin1(NULL)
    {
        if (!fft.ok())
            return;

        ring = new (std::nothrow) float[fft.size()];
        cells = new (std::nothrow) float[(size_t)COLUMNS * ROWS];
        mag = new (std::nothrow) float[fft.bins()];
        rowBin0 = new (std::nothrow) unsigned int[ROWS];
        rowBin1 = new (std::nothrow) unsigned int[ROWS];

        if (!ring || !cells || !mag || !rowBin0 || !rowBin1)
            return;

        memset(ring, 0, fft.size() * sizeof(float));

        for (size_t i = 0; i < (size_t)COLUMNS * ROWS; i++)
            cells[i] = (float)FLOOR_DB;

        /* Row 0 is the bottom of the panel and the lowest frequency. Log
           spaced, for the reason the spectrum's x axis is. */
        const double nyquist = samplerate * 0.5;
        const double lo = 20.0;

        for (unsigned int r = 0; r < ROWS; r++)
        {
            const double f0 = lo * pow(nyquist / lo,
                                       (double)r / (double)ROWS);
            const double f1 = lo * pow(nyquist / lo,
                                       (double)(r + 1) / (double)ROWS);

            unsigned int b0 = (unsigned int)(f0 / nyquist * (double)fft.bins());
            unsigned int b1 = (unsigned int)(f1 / nyquist * (double)fft.bins());

            if (b0 >= fft.bins())
                b0 = fft.bins() - 1;

            if (b1 <= b0)
                b1 = b0 + 1;

            if (b1 > fft.bins())
                b1 = fft.bins();

            rowBin0[r] = b0;
            rowBin1[r] = b1;
        }
    }

    ~Gram (void)
    {
        delete [] ring;
        delete [] cells;
        delete [] mag;
        delete [] rowBin0;
        delete [] rowBin1;
    }

    bool ok (void) const
    {
        return fft.ok() && ring && cells && mag && rowBin0 && rowBin1;
    }

    unsigned int hop (void) const { return fft.size() / HOP_DIVISOR; }

    struct Frame {
        const float *ring;
        unsigned int at, n;

        float operator() (unsigned int i) const
        {
            return ring[(at + i) % n];
        }
    };

    /* One transform, one column. */
    void takeColumn (void)
    {
        Frame frame = { ring, at, fft.size() };

        fft.magnitude(frame, mag);

        float *cell = cells + (size_t)col * ROWS;

        for (unsigned int r = 0; r < ROWS; r++)
        {
            double best = 0.0;

            /* The loudest bin in the row's band, not the mean. At the bottom
               of a log axis a row covers less than one bin and at the top it
               covers dozens; averaging the top rows would wash a bright
               partial out into the noise around it. */
            for (unsigned int b = rowBin0[r]; b < rowBin1[r]; b++)
                if (mag[b] > best)
                    best = mag[b];

            double d = (best > 1e-7) ? 20.0 * log10(best) : FLOOR_DB;

            if (d < FLOOR_DB)
                d = FLOOR_DB;

            if (d > 0.0)
                d = 0.0;

            cell[r] = (float)d;
        }

        col = (col + 1) % COLUMNS;

        if (filled < COLUMNS)
            filled++;
    }
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

/* dB to colour: black, blue, magenta, orange, white.
 *
 * A ramp through hue rather than through brightness alone. On a 56-pixel panel
 * a greyscale spectrogram is a smudge -- the eye reads maybe five levels of
 * grey and twenty of hue, and the whole point of the display is small
 * differences in level. */
void colourFor (double db, double &r, double &g, double &b)
{
    double t = (db - FLOOR_DB) / (0.0 - FLOOR_DB);

    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    /* Four segments, linear within each. */
    if (t < 0.25)
    {
        const double u = t / 0.25;

        r = 0.07 * (1 - u) + 0.10 * u;
        g = 0.08 * (1 - u) + 0.10 * u;
        b = 0.10 * (1 - u) + 0.45 * u;
    }
    else if (t < 0.5)
    {
        const double u = (t - 0.25) / 0.25;

        r = 0.10 * (1 - u) + 0.55 * u;
        g = 0.10 * (1 - u) + 0.10 * u;
        b = 0.45 * (1 - u) + 0.60 * u;
    }
    else if (t < 0.75)
    {
        const double u = (t - 0.5) / 0.25;

        r = 0.55 * (1 - u) + 0.95 * u;
        g = 0.10 * (1 - u) + 0.50 * u;
        b = 0.60 * (1 - u) + 0.20 * u;
    }
    else
    {
        const double u = (t - 0.75) / 0.25;

        r = 0.95 * (1 - u) + 1.00 * u;
        g = 0.50 * (1 - u) + 0.98 * u;
        b = 0.20 * (1 - u) + 0.92 * u;
    }
}

} /* namespace */

int visual_init (thVisual *visual)
{
    visual->setName("spectrogram");
    visual->setDesc("The spectrum over time, newest at the right");

    /* The tallest of the four, and the only one where height buys resolution
       rather than just room. */
    visual->setPreferredSize(128, 80);

    return 0;
}

void *visual_open (thVisual *visual, unsigned int samplerate)
{
    (void)visual;

    /* std::nothrow, because this is a C ABI boundary.
     *
     * thVisual::open calls this through a function pointer. A bad_alloc thrown
     * here would unwind across that -- out of a dlopen'd module and into a host
     * that has no catch anywhere near it -- and terminate the process. A
     * visualizer failing to allocate should cost a panel, not the synth. */
    Gram *g = new (std::nothrow) Gram(samplerate);

    if (g == NULL)
        return NULL;

    if (!g->ok())
    {
        delete g;
        return NULL;
    }

    return g;
}

int visual_feed (void *inst, const float *samples, unsigned int n)
{
    Gram *g = (Gram *)inst;

    if (g == NULL || samples == NULL || n == 0 || !g->ok())
        return 0;

    const unsigned int size = g->fft.size();

    /* Sample by sample, taking a column every hop.
     *
     * The transforms happen HERE and not in draw(), which is the decision this
     * module turns on. A spectrogram drawn on demand would produce columns at
     * the frame rate -- so a stall would compress a second of audio into one
     * column and the picture would be a record of the GUI's scheduling rather
     * than of the sound. Hopping on sample count means the time axis is the
     * signal's, whatever the display does.
     *
     * It also makes the module indifferent to where the drain's boundaries
     * fell, which is what visualcheck asserts of all of them and what this one
     * would find easiest to get wrong. */
    for (unsigned int i = 0; i < n; i++)
    {
        const float x = samples[i];

        if (!finite(x))
        {
            g->sawNonFinite = true;
            g->ring[g->at] = 0.0f;
        }
        else
            g->ring[g->at] = x;

        g->at = (g->at + 1) % size;

        if (++g->sinceHop >= g->hop())
        {
            g->sinceHop = 0;
            g->takeColumn();
        }
    }

    g->seen += n;

    return 0;
}

int visual_draw (void *inst, cairo_t *cr, int w, int h)
{
    Gram *g = (Gram *)inst;

    if (g == NULL || !g->ok())
        return 0;

    cairo_set_source_rgb(cr, 0.07, 0.08, 0.10);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    if (w < 8 || h < 6)
        return 0;

    if (g->filled == 0)
    {
        cairo_set_source_rgb(cr, 0.55, 0.57, 0.62);
        drawText(cr, 4.0, h - 4.0, "--", 8.0);
        return 0;
    }

    /* An image surface the size of the history, painted scaled.
     *
     * Rather than a rectangle per cell: at 256 columns by 128 rows that is
     * 32768 fills, which is the one thing in these four modules that would
     * actually show up against the 0.8ms budget canvasbench measured. One surface and
     * one paint is a blit.
     *
     * Rebuilt every frame rather than kept and scrolled. Keeping it would mean
     * a second copy of the history in a different layout, and the whole
     * argument for scrolling -- that redrawing costs too much -- does not
     * survive the measurement: filling 32768 bytes is nothing next to the
     * cairo_paint that follows it. */
    const int cw = (int)(g->filled < COLUMNS ? g->filled : COLUMNS);

    cairo_surface_t *img =
        cairo_image_surface_create(CAIRO_FORMAT_RGB24, cw, (int)ROWS);

    if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS)
    {
        cairo_surface_destroy(img);
        return 0;
    }

    unsigned char *data = cairo_image_surface_get_data(img);
    const int stride = cairo_image_surface_get_stride(img);

    /* Oldest at the left. col is one past the newest, so the oldest kept
       column is col - filled, modulo the ring. */
    const unsigned int oldest = (g->col + COLUMNS - g->filled) % COLUMNS;

    for (int x = 0; x < cw; x++)
    {
        const unsigned int c = (oldest + (unsigned int)x) % COLUMNS;
        const float *cell = g->cells + (size_t)c * ROWS;

        for (unsigned int r = 0; r < ROWS; r++)
        {
            double rr, gg, bb;

            colourFor(cell[r], rr, gg, bb);

            /* Row 0 is the lowest frequency and belongs at the bottom. */
            unsigned char *px = data + (size_t)(ROWS - 1 - r) * stride + x * 4;

            px[0] = (unsigned char)(bb * 255.0);
            px[1] = (unsigned char)(gg * 255.0);
            px[2] = (unsigned char)(rr * 255.0);
            px[3] = 0xff;
        }
    }

    cairo_surface_mark_dirty(img);

    cairo_save(cr);
    cairo_scale(cr, (double)w / (double)cw, (double)h / (double)ROWS);
    cairo_set_source_surface(cr, img, 0, 0);

    /* Bilinear rather than nearest: a spectrogram squeezed into a 128-pixel
       panel is being downsampled by two in x and by nothing much in y, and
       nearest-neighbour would drop every other column -- so a partial that
       lasted one hop could vanish entirely. */
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);
    cairo_restore(cr);

    cairo_surface_destroy(img);

    if (g->sawNonFinite)
    {
        cairo_set_source_rgb(cr, 0.95, 0.35, 0.35);
        drawText(cr, 4.0, 10.0, "NOT FINITE", 8.0);
    }

    return 0;
}

void visual_close (void *inst)
{
    delete (Gram *)inst;
}

void visual_cleanup (thVisual *visual)
{
    (void)visual;
}
