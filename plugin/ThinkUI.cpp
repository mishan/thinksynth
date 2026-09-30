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
 * The plugin's editor: Panel (Panel.h) in a DPF window.
 *
 * Everything about what is drawn and what a point means is Panel's; this
 * is the plumbing between it and the host. A drag is bracketed by
 * editParameter(true/false), which is how a host records one gesture of
 * automation rather than a stream of separate edits. Values come back
 * through parameterChanged -- from the host's own controls, automation, a
 * project being opened -- and the editor draws whatever it was last told.
 *
 * A drag measures from where it started, so a value is a function of how
 * far the mouse has gone and not of how often it was sampled; pressing or
 * releasing Shift partway re-anchors it there, so switching to a fine drag
 * does not jump. A second click on a knob within 350 ms puts it back to
 * the .dsp's value.
 *
 * Sizes are Panel's, in unscaled pixels: DPF sizes the window for the
 * desktop's scale factor, and the drawing and the mouse are scaled here
 * by the same.
 */

#include "DistrhoUI.hpp"

#include <vector>

#include "Controls.h"
#include "Panel.h"

#include "thinksynth_dsp.h"

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::CairoGraphicsContext;

namespace {

const Panel &panel (void)
{
    static const Panel p(controls(), DISTRHO_PLUGIN_NAME,
                         thPluginDspDescription);

    return p;
}

const uint kDoubleClickMs = 350;

} /* namespace */

class ThinkUI : public UI
{
public:
    ThinkUI (void)
        : UI((uint)panel().width(), (uint)panel().height(), true),
          hover_(-1), active_(-1), fine_(false), anchorY_(0),
          anchorValue_(0), lastClick_(0), lastClicked_(-1)
    {
        for (size_t i = 0; i < controls().size(); i++)
            values_.push_back(controls()[i].def);
    }

protected:
    void parameterChanged (uint32_t index, float value) override
    {
        if (index < values_.size() && values_[index] != value)
        {
            values_[index] = value;
            repaint();
        }
    }

    void onCairoDisplay (const CairoGraphicsContext &context) override
    {
        cairo_t *cr = context.handle;
        const double s = getScaleFactor();

        cairo_save(cr);
        cairo_scale(cr, s, s);
        panel().draw(cr, values_, hover_, active_);
        cairo_restore(cr);
    }

    bool onMouse (const MouseEvent &ev) override
    {
        if (ev.button != 1)
            return false;

        const double s = getScaleFactor();
        const double x = ev.pos.getX() / s, y = ev.pos.getY() / s;

        if (!ev.press)
        {
            if (active_ < 0)
                return false;

            editParameter((uint32_t)active_, false);
            active_ = -1;
            repaint();

            return true;
        }

        const int i = panel().hit(x, y);

        if (i < 0)
            return false;

        /* A second click on the same knob: its default. */
        if (i == lastClicked_ && ev.time - lastClick_ < kDoubleClickMs)
        {
            lastClicked_ = -1;
            editParameter((uint32_t)i, true);
            set(i, controls()[i].def);
            editParameter((uint32_t)i, false);

            return true;
        }

        lastClick_ = ev.time;
        lastClicked_ = i;

        active_ = i;
        fine_ = (ev.mod & kModifierShift) != 0;
        anchorY_ = y;
        anchorValue_ = values_[i];

        editParameter((uint32_t)i, true);
        repaint();

        return true;
    }

    bool onMotion (const MotionEvent &ev) override
    {
        const double s = getScaleFactor();
        const double x = ev.pos.getX() / s, y = ev.pos.getY() / s;

        if (active_ >= 0)
        {
            const bool fine = (ev.mod & kModifierShift) != 0;

            if (fine != fine_)
            {
                fine_ = fine;
                anchorY_ = y;
                anchorValue_ = values_[active_];
            }

            set(active_, panel().drag(active_, anchorValue_, anchorY_ - y,
                                      fine_));

            return true;
        }

        const int h = panel().hit(x, y);

        if (h != hover_)
        {
            hover_ = h;
            repaint();
        }

        return false;
    }

    bool onScroll (const ScrollEvent &ev) override
    {
        const double s = getScaleFactor();
        const int i = panel().hit(ev.pos.getX() / s, ev.pos.getY() / s);

        if (i < 0 || active_ >= 0)
            return false;

        editParameter((uint32_t)i, true);
        set(i, panel().wheel(i, values_[i], ev.delta.getY(),
                             (ev.mod & kModifierShift) != 0));
        editParameter((uint32_t)i, false);

        return true;
    }

private:
    void set (int i, float value)
    {
        if (values_[i] == value)
            return;

        values_[i] = value;
        setParameterValue((uint32_t)i, value);
        repaint();
    }

    std::vector<float> values_;
    int hover_;
    int active_;
    bool fine_;
    double anchorY_;
    float anchorValue_;
    uint lastClick_;
    int lastClicked_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThinkUI)
};

UI *createUI (void)
{
    return new ThinkUI();
}

END_NAMESPACE_DISTRHO
