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

#ifndef COMPOSER_CANVAS_WIDGET_H
#define COMPOSER_CANVAS_WIDGET_H 1

#include "../ComposerCanvas.h"
#include "GraphCanvas.h"

/*
 * ComposerCanvas as a widget: the content class with the desktop shell
 * around it. The same shape as NodeCanvasWidget, with the controllers
 * this canvas wants -- any button, since a plugin's picture tells a
 * primary click from a secondary one; a drag gesture, since stage boxes
 * are carried by offset; and the keyboard, for Escape.
 */
class ComposerCanvasWidget : public ComposerCanvas, public GraphCanvas
{
public:
    ComposerCanvasWidget (void);

    /* Where a stage's box is, as the toolkit spells a rectangle, for a
       popover to point at. The content answers in its own struct so it
       can be asked without a toolkit; this is the one conversion. */
    static Gdk::Rectangle toGdk (const CanvasRect &r)
    {
        return Gdk::Rectangle(r.x, r.y, r.w, r.h);
    }

protected:
    void requestRedraw (void) override { queue_draw(); }
    void resizeShell (int w, int h) override { set_size_request(w, h); }
    bool shellViewport (double &x, double &y,
                        double &w, double &h) const override
    {
        return viewport(x, y, w, h);
    }
    void takeFocus (void) override { grab_focus(); }

private:
    bool onKey (guint keyval, guint keycode, Gdk::ModifierType state);
};

#endif /* COMPOSER_CANVAS_WIDGET_H */
