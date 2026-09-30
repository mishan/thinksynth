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
 * uicheck -- the plugin's editor, with no window.
 *
 *   uicheck [OUT.png]
 *
 * Panel is the whole editor but the window, so this asks it what a mouse
 * would: where every knob is and that a click there finds it, what a drag
 * and a wheel do to a value, and how a value is spelled. And it draws the
 * panel into an image, at 1x and 2x, which is also what OUT.png is for:
 * something to look at without a DAW.
 *
 * Exit status is the number of failures.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cairo.h>

#include <string>
#include <vector>

#include "Controls.h"
#include "Panel.h"

static int failed = 0;

static void check (bool ok, const std::string &what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());

    if (!ok)
        failed++;
}

static int find (const std::vector<Control> &c, const char *name)
{
    for (size_t i = 0; i < c.size(); i++)
        if (c[i].name == name)
            return (int)i;

    return -1;
}

/* Distinct colours in an image: a blank or single-colour one has one. */
static size_t colours (cairo_surface_t *s)
{
    cairo_surface_flush(s);

    const unsigned char *d = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    const int w = cairo_image_surface_get_width(s);
    const int h = cairo_image_surface_get_height(s);
    std::vector<uint32_t> seen;

    for (int y = 0; y < h && seen.size() < 64; y += 3)
        for (int x = 0; x < w && seen.size() < 64; x += 3)
        {
            uint32_t p;

            memcpy(&p, d + y * stride + x * 4, 4);

            bool known = false;

            for (size_t k = 0; k < seen.size() && !known; k++)
                known = seen[k] == p;

            if (!known)
                seen.push_back(p);
        }

    return seen.size();
}

int main (int argc, char **argv)
{
    const std::vector<Control> &c = controls();

    check(c.size() == 17, "17 controls: " + std::to_string(c.size()));

    if (c.empty())
        return failed;

    Panel panel(c, "thinksynth Juno", "A description.");

    check(panel.width() >= 360 && panel.width() <= 900 &&
          panel.height() > 100 && panel.height() < 400,
          "a panel of a sensible size, two rows of boxes: " + std::to_string(panel.width()) +
          " x " + std::to_string(panel.height()));

    /* Every knob is where a click finds it, and only it. */
    {
        bool all = true;

        for (size_t i = 0; i < c.size(); i++)
        {
            double x, y;

            all = all && panel.centre((int)i, x, y) &&
                  panel.hit(x, y) == (int)i && x > 0 && y > 0 &&
                  x < panel.width() && y < panel.height();
        }

        check(all, "a click on each knob finds that knob");
        check(panel.hit(2, 2) == -1 &&
              panel.hit(panel.width() - 2, panel.height() - 2) == -1,
              "and a click in a corner finds none");
    }

    const int cutoff = find(c, "cutoff");
    const int fa = find(c, "fa");
    const int fd = find(c, "fd");
    const int pw = find(c, "pw");
    const int res = find(c, "res");
    const int amp = find(c, "amp");

    check(cutoff >= 0 && fa >= 0 && fd >= 0 && pw >= 0 && res >= 0 &&
          amp >= 0, "cutoff, fa, fd, pw, res and the level are there");

    if (cutoff < 0 || fa < 0 || fd < 0 || pw < 0 || res < 0 || amp < 0)
        return failed;

    /* The groups juno.dsp declares, in its order, then the level's. */
    check(c[pw].group == "Oscillator" && c[cutoff].group == "Filter" &&
          c[amp].group == "Output", "grouped as juno.dsp says");

    /* Travel. */
    check(panel.fromTravel(res, 0) == c[res].min &&
          panel.fromTravel(res, 1) == c[res].max,
          "the ends of a knob's travel are the ends of its range");
    check(fabs(panel.fromTravel(cutoff, 0.5) - sqrt(80.0 * 8000.0)) < 0.01,
          "Cutoff turns on a log scale: halfway is 800 Hz");
    check(fabs(panel.fromTravel(res, 0.5) - c[res].max / 2) < 1e-6,
          "Resonance, which starts at 0, turns on a straight one");

    {
        bool round = true;

        for (int i = 0; i < (int)c.size(); i++)
            for (double t = 0; t <= 1.0001; t += 0.125)
                round = round && fabs(panel.toTravel(i,
                                      panel.fromTravel(i, t)) - t) < 1e-4;

        check(round, "a travel comes back through a value unchanged");
    }

    /* Drag and wheel. */
    check(panel.drag(res, c[res].min, Panel::kDragPixels, false) ==
          c[res].max, "a drag of the full distance goes from end to end");
    check(panel.drag(res, c[res].max, 50, false) == c[res].max &&
          panel.drag(res, c[res].min, -50, false) == c[res].min,
          "and stops at the ends");
    check(fabs(panel.drag(res, 0, Panel::kDragPixels, true) -
               c[res].max / 10) < 1e-6,
          "a fine drag goes a tenth as far");
    {
        /* A whole-numbered control, which juno has none of. */
        Control n;

        n.name = n.label = "count";
        n.min = 1;
        n.max = 16;
        n.def = 4;
        n.step = 1;

        Panel whole(std::vector<Control>(1, n), "", "");

        check(whole.wheel(0, 4, 1, false) == 5 &&
              whole.wheel(0, 4, -3, false) == 3 &&
              whole.drag(0, 4, 3, false) == 4,
              "a whole number moves by one a notch, and drags in steps");
    }
    check(fabs(panel.wheel(res, 0.5f, 1, false) - (0.5f + c[res].max / 50)) <
          1e-5, "and a continuous knob by a fiftieth of its travel");

    /* Spelling. */
    check(panel.label(cutoff) == "Cutoff" &&
          panel.format(cutoff, 700) == "700 Hz",
          "Cutoff (Hz) is drawn Cutoff, 700 Hz: " + panel.label(cutoff) +
          ", " + panel.format(cutoff, 700));
    check(panel.format(fa, 180) == "180 ms" &&
          panel.format(fd, 2500) == "2.50 s",
          "milliseconds, and seconds past a thousand of them: " +
          panel.format(fa, 180) + ", " + panel.format(fd, 2500));
    check(panel.format(pw, 0.5f) == "0.50" && panel.format(amp, 30) == "30",
          "a fraction to two places, the level whole: " +
          panel.format(pw, 0.5f) + ", " + panel.format(amp, 30));

    /* Drawn, at two scales. */
    std::vector<float> values;

    for (size_t i = 0; i < c.size(); i++)
        values.push_back(c[i].def);

    for (int scale = 1; scale <= 2; scale++)
    {
        cairo_surface_t *s = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, panel.width() * scale,
            panel.height() * scale);
        cairo_t *cr = cairo_create(s);

        cairo_scale(cr, scale, scale);
        panel.draw(cr, values, cutoff, -1);

        check(cairo_status(cr) == CAIRO_STATUS_SUCCESS && colours(s) > 8,
              "drawn at " + std::to_string(scale) + "x");

        if (scale == 1 && argc > 1)
            cairo_surface_write_to_png(s, argv[1]);

        cairo_destroy(cr);
        cairo_surface_destroy(s);
    }

    printf("\n%d failure(s)\n", failed);

    return failed;
}
