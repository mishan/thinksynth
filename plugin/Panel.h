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

/* The editor, as a thing that draws on a cairo_t and answers questions
 * about points -- and nothing that knows what a window is.
 *
 * ThinkUI.cpp is the DPF window around it; uicheck.cpp draws it into
 * an image and asks it the same questions a mouse would, so the layout,
 * the hit-testing and the knob arithmetic are tested with no display.
 *
 * A panel is the controls in titled boxes, one box per group in the order
 * the groups are first seen, flowed left to right and wrapped at a fixed
 * width. A knob turns through 270 degrees; its label is under it and its
 * value under that, in the units the .dsp gives -- the ones it spells as
 * `ms' and the ones it spells in its labels, "Cutoff (Hz)" becoming
 * "Cutoff" and "700 Hz". A range that runs from above zero over more than
 * a factor of twenty turns on a log scale, which is where cutoffs and
 * rates want their resolution; the host still sees the plain value. */

#ifndef TH_PLUGIN_PANEL_H
#define TH_PLUGIN_PANEL_H 1

#include <string>
#include <vector>

#include "Controls.h"

typedef struct _cairo cairo_t;

class Panel
{
public:
    Panel (const std::vector<Control> &controls, const std::string &title,
           const std::string &subtitle);

    /* In unscaled pixels; a window multiplies by its scale factor and
       scales the cairo_t by the same. */
    int width (void) const { return width_; }
    int height (void) const { return height_; }

    /* The whole panel. `hover' and `active' are control indices, or -1. */
    void draw (cairo_t *cr, const std::vector<float> &values, int hover,
               int active) const;

    /* The control whose knob is at (x, y), or -1. */
    int hit (double x, double y) const;

    /* Where a knob's centre is, for a test to aim at. */
    bool centre (int control, double &x, double &y) const;

    /* A value as a fraction of its knob's travel, and back, on the knob's
       scale and snapped to its step. */
    double toTravel (int control, float value) const;
    float fromTravel (int control, double travel) const;

    /* A drag of `up' pixels upward from where it started at `start': the
       full travel is kDragPixels, a tenth as far with `fine'. */
    float drag (int control, float start, double up, bool fine) const;

    /* A mouse wheel's `notches' from `value'. */
    float wheel (int control, float value, double notches, bool fine) const;

    /* The label as drawn -- without a unit in brackets -- and the value
       with its unit. */
    std::string label (int control) const;
    std::string format (int control, float value) const;

    static const double kDragPixels;

private:
    struct Box
    {
        std::string title;
        double x, y, w, h;
        std::vector<int> controls;
    };

    struct Knob
    {
        double x, y;    /* centre */
        int box;
    };

    bool logScale (int control) const;
    std::string unitOf (int control) const;

    std::vector<Control> controls_;
    std::string title_;
    std::string subtitle_;
    std::vector<Box> boxes_;
    std::vector<Knob> knobs_;   /* one per control */
    int width_;
    int height_;
};

#endif /* TH_PLUGIN_PANEL_H */
