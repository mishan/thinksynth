/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#include <math.h>
#include <stdio.h>

#include <cairo.h>

#include "Panel.h"

namespace {

/* The grid. */
const double kMargin = 16;
const double kHeader = 58;
const double kGap = 12;         /* between boxes */
const double kPad = 10;         /* inside a box */
const double kBoxTitle = 24;
const double kCell = 80;        /* one knob's column */
const double kCellHeight = 104;
const double kRadius = 22;
const double kMaxWidth = 900;

/* 270 degrees of travel, from the bottom left round to the bottom right. */
const double kStart = M_PI * 0.75;
const double kSweep = M_PI * 1.5;

void rgb (cairo_t *cr, unsigned hex, double alpha = 1)
{
    cairo_set_source_rgba(cr, ((hex >> 16) & 0xff) / 255.0,
                          ((hex >> 8) & 0xff) / 255.0, (hex & 0xff) / 255.0,
                          alpha);
}

void roundRect (cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);
}

/* `text' as it fits in `maxw': whole, or cut at a character boundary
   with an ellipsis after. `width' is what is returned, drawn. */
std::string fitted (cairo_t *cr, const std::string &text, double maxw,
                    double &width)
{
    static const char ellipsis[] = "\xe2\x80\xa6";
    cairo_text_extents_t e;

    cairo_text_extents(cr, text.c_str(), &e);

    if (e.x_advance <= maxw)
    {
        width = e.x_advance;
        return text;
    }

    std::string t = text;

    while (!t.empty())
    {
        t.erase(t.size() - 1);

        while (!t.empty() && ((unsigned char)t.back() & 0xc0) == 0x80)
            t.erase(t.size() - 1);

        cairo_text_extents(cr, (t + ellipsis).c_str(), &e);

        if (e.x_advance <= maxw || t.empty())
            break;
    }

    width = e.x_advance;

    return t + ellipsis;
}

/* `text' centered on x, its baseline at y, cut to `maxw'. */
void centered (cairo_t *cr, const std::string &text, double x, double y,
               double maxw)
{
    double w;
    const std::string t = fitted(cr, text, maxw, w);

    cairo_move_to(cr, x - w / 2, y);
    cairo_show_text(cr, t.c_str());
}

/* `text' from x, its baseline at y, cut to `maxw'. */
void leftAligned (cairo_t *cr, const std::string &text, double x, double y,
                  double maxw)
{
    double w;
    const std::string t = fitted(cr, text, maxw, w);

    cairo_move_to(cr, x, y);
    cairo_show_text(cr, t.c_str());
}

void font (cairo_t *cr, double size, bool bold)
{
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD
                                : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
}

/* NaN goes to 0: both comparisons are false for it, and it would pass. */
double clamp01 (double v)
{
    return !(v == v) ? 0 : v < 0 ? 0 : v > 1 ? 1 : v;
}

} /* namespace */

const double Panel::kDragPixels = 200;

Panel::Panel (const std::vector<Control> &controls, const std::string &title,
              const std::string &subtitle)
    : controls_(controls), title_(title), subtitle_(subtitle),
      width_(0), height_(0)
{
    /* The boxes, in the order their groups are first seen. A control with
       no group goes in one titled by nothing. */
    for (size_t i = 0; i < controls_.size(); i++)
    {
        size_t b = 0;

        while (b < boxes_.size() && boxes_[b].title != controls_[i].group)
            b++;

        if (b == boxes_.size())
        {
            Box box;

            box.title = controls_[i].group;
            box.x = box.y = box.w = box.h = 0;
            boxes_.push_back(box);
        }

        boxes_[b].controls.push_back((int)i);
    }

    /* A box that would be wider than kMaxWidth takes its knobs in rows of
       as many as fit; boxes are flowed into rows no wider than kMaxWidth,
       each row as tall as its tallest box. */
    const size_t perRow =
        (size_t)((kMaxWidth - 2 * kMargin - 2 * kPad) / kCell);

    knobs_.resize(controls_.size());

    double x = kMargin, y = kHeader, rowHeight = 0, right = 0;

    for (size_t b = 0; b < boxes_.size(); b++)
    {
        Box &box = boxes_[b];
        const size_t n = box.controls.size();
        const size_t across = n < perRow ? n : perRow;
        const size_t rows = (n + perRow - 1) / perRow;

        box.w = kPad * 2 + kCell * across;
        box.h = kBoxTitle + kCellHeight * rows + kPad;

        if (x > kMargin && x + box.w > kMaxWidth - kMargin)
        {
            x = kMargin;
            y += rowHeight + kGap;
            rowHeight = 0;
        }

        box.x = x;
        box.y = y;

        for (size_t k = 0; k < n; k++)
        {
            Knob &knob = knobs_[box.controls[k]];

            knob.x = box.x + kPad + kCell * (k % perRow + 0.5);
            knob.y = box.y + kBoxTitle + kCellHeight * (k / perRow) +
                     kRadius + 6;
            knob.box = (int)b;
        }

        x += box.w + kGap;

        if (box.h > rowHeight)
            rowHeight = box.h;

        if (box.x + box.w > right)
            right = box.x + box.w;
    }

    width_ = (int)ceil(right + kMargin);
    height_ = (int)ceil(y + rowHeight + kMargin);

    if (width_ < 360)
        width_ = 360;
}

bool Panel::logScale (int i) const
{
    return ::logScale(controls_[i]);
}

/* The unit a value is shown in: the .dsp's own, or the one its label
   spells in brackets at the end. */
std::string Panel::unitOf (int i) const
{
    const Control &c = controls_[i];

    if (!c.units.empty())
        return c.units;

    const std::string &l = c.label;
    const size_t open = l.rfind(" (");

    if (open != std::string::npos && !l.empty() && l.back() == ')')
        return l.substr(open + 2, l.size() - open - 3);

    return "";
}

std::string Panel::label (int i) const
{
    const Control &c = controls_[i];

    if (c.units.empty() && !unitOf(i).empty())
        return c.label.substr(0, c.label.rfind(" ("));

    return c.label;
}

std::string Panel::format (int i, float v) const
{
    const Control &c = controls_[i];
    char buf[64];

    if (!(v == v))
        return "-";

    if (!c.valueNames.empty())
    {
        const long k = lrint(v - c.min);

        if (k >= 0 && k < (long)c.valueNames.size())
            return c.valueNames[k];
    }

    const std::string unit = unitOf(i);

    if (unit == "ms" && v >= 1000)
    {
        snprintf(buf, sizeof(buf), "%.2f s", v / 1000.0);
        return buf;
    }

    const double span = c.max - c.min;
    const int decimals = (c.step == 1.0f || span >= 100) ? 0
                       : (span >= 10) ? 1 : 2;

    snprintf(buf, sizeof(buf), "%.*f", decimals, (double)v);

    std::string out = buf;

    if (unit == "%")
        out += "%";
    else if (!unit.empty())
        out += " " + unit;

    return out;
}

double Panel::toTravel (int i, float v) const
{
    const Control &c = controls_[i];

    if (c.max <= c.min)
        return 0;

    if (logScale(i))
        return v <= c.min ? 0 : clamp01(log(v / c.min) / log(c.max / c.min));

    return clamp01((v - c.min) / (c.max - c.min));
}

float Panel::fromTravel (int i, double t) const
{
    const Control &c = controls_[i];

    t = clamp01(t);

    double v = logScale(i) ? c.min * pow(c.max / c.min, t)
                           : c.min + t * (c.max - c.min);

    if (c.step == 1.0f || !c.valueNames.empty())
        v = floor(v + 0.5);

    if (v < c.min) v = c.min;
    if (v > c.max) v = c.max;

    return (float)v;
}

float Panel::drag (int i, float start, double up, bool fine) const
{
    return fromTravel(i, toTravel(i, start) +
                         up / (kDragPixels * (fine ? 10 : 1)));
}

float Panel::wheel (int i, float v, double notches, bool fine) const
{
    const Control &c = controls_[i];

    /* A whole number moves by one a notch, whatever its range -- by at
       least one for any turn at all, so a trackpad's small deltas still
       step it, and by as many as a turn of several notches is. */
    if (c.step == 1.0f || !c.valueNames.empty())
    {
        const double r = floor(fabs(notches) + 0.5);
        const double by = (notches > 0 ? 1 : notches < 0 ? -1 : 0) *
                          (r < 1 ? 1 : r);
        double n = floor(v + 0.5) + by;

        return (float)(n < c.min ? c.min : n > c.max ? c.max : n);
    }

    return fromTravel(i, toTravel(i, v) + notches / (fine ? 500.0 : 50.0));
}

int Panel::hit (double x, double y) const
{
    for (size_t i = 0; i < knobs_.size(); i++)
    {
        const double dx = x - knobs_[i].x, dy = y - knobs_[i].y;

        /* The knob and the label under it: the cell, not the circle. */
        if (fabs(dx) <= kCell / 2 && dy >= -kRadius - 4 &&
            dy <= kCellHeight - kRadius - 10)
            return (int)i;
    }

    return -1;
}

bool Panel::center (int i, double &x, double &y) const
{
    if (i < 0 || i >= (int)knobs_.size())
        return false;

    x = knobs_[i].x;
    y = knobs_[i].y;

    return true;
}

void Panel::draw (cairo_t *cr, const std::vector<float> &values, int hover,
                  int active) const
{
    cairo_save(cr);

    rgb(cr, 0x1c1d22);
    cairo_paint(cr);

    /* The header: the plugin's name and what the .dsp says about itself. */
    rgb(cr, 0xe8e9ec);
    font(cr, 18, true);
    cairo_move_to(cr, kMargin, 30);
    cairo_show_text(cr, title_.c_str());

    rgb(cr, 0x8b909a);
    font(cr, 11, false);
    {
        cairo_text_extents_t e;

        cairo_text_extents(cr, subtitle_.c_str(), &e);

        const double maxw = width_ - 2 * kMargin;

        if (e.x_advance <= maxw)
        {
            cairo_move_to(cr, kMargin, 47);
            cairo_show_text(cr, subtitle_.c_str());
        }
        else
            centered(cr, subtitle_, kMargin + maxw / 2, 47, maxw);
    }

    for (size_t b = 0; b < boxes_.size(); b++)
    {
        const Box &box = boxes_[b];

        rgb(cr, 0x272930);
        roundRect(cr, box.x, box.y, box.w, box.h, 8);
        cairo_fill(cr);

        if (!box.title.empty())
        {
            std::string t = box.title;

            for (size_t k = 0; k < t.size(); k++)
                t[k] = (char)toupper((unsigned char)t[k]);

            rgb(cr, 0x7d828c);
            font(cr, 10, true);
            leftAligned(cr, t, box.x + kPad + 2, box.y + 17,
                        box.w - 2 * kPad - 2);
        }
    }

    for (size_t i = 0; i < knobs_.size(); i++)
    {
        const Knob &k = knobs_[i];
        const float v = i < values.size() ? values[i] : controls_[i].def;
        const double t = toTravel((int)i, v);
        const bool lit = (int)i == hover || (int)i == active;

        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_width(cr, 4);

        /* The track, and the value along it. */
        rgb(cr, 0x3a3d46);
        cairo_new_path(cr);
        cairo_arc(cr, k.x, k.y, kRadius, kStart, kStart + kSweep);
        cairo_stroke(cr);

        rgb(cr, lit ? 0xf2b84b : 0xd99a2b);
        cairo_new_path(cr);
        cairo_arc(cr, k.x, k.y, kRadius, kStart, kStart + kSweep * t);
        cairo_stroke(cr);

        /* The cap, and its pointer. */
        rgb(cr, lit ? 0x40434d : 0x33363e);
        cairo_new_path(cr);
        cairo_arc(cr, k.x, k.y, kRadius - 7, 0, 2 * M_PI);
        cairo_fill(cr);

        const double a = kStart + kSweep * t;

        rgb(cr, 0xe8e9ec);
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, k.x + cos(a) * 5, k.y + sin(a) * 5);
        cairo_line_to(cr, k.x + cos(a) * (kRadius - 9),
                      k.y + sin(a) * (kRadius - 9));
        cairo_stroke(cr);

        /* A label that does not fit is set smaller before it is cut. */
        rgb(cr, 0xd8dadf);

        const std::string text = label((int)i);
        double size = 11;
        cairo_text_extents_t e;

        font(cr, size, false);
        cairo_text_extents(cr, text.c_str(), &e);

        while (e.x_advance > kCell - 6 && size > 8.5)
        {
            size -= 0.5;
            font(cr, size, false);
            cairo_text_extents(cr, text.c_str(), &e);
        }

        centered(cr, text, k.x, k.y + kRadius + 20, kCell - 6);

        rgb(cr, (int)i == active ? 0xf2b84b : 0x8b909a);
        font(cr, 10, false);
        centered(cr, format((int)i, v), k.x, k.y + kRadius + 35, kCell - 6);
    }

    cairo_restore(cr);
}
