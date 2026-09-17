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

#ifndef NODE_CANVAS_WIDGET_H
#define NODE_CANVAS_WIDGET_H 1

#include "../NodeCanvas.h"
#include "GraphCanvas.h"

/*
 * NodeCanvas as a widget: the content class with the desktop shell
 * around it. Creates the gesture controllers and forwards them to the
 * content's handlers, and answers the content's requests -- a redraw, a
 * size, the viewport, the focus -- with the widget's calls. Nothing
 * about what the canvas draws or does is here; see CanvasContent.h.
 */
class NodeCanvasWidget : public NodeCanvas, public GraphCanvas
{
public:
    NodeCanvasWidget (void);

protected:
    void requestRedraw (void) override { queue_draw(); }
    void resizeShell (int w, int h) override { set_size_request(w, h); }
    bool shellViewport (double &x, double &y,
                        double &w, double &h) const override
    {
        return viewport(x, y, w, h);
    }

private:
    Glib::RefPtr<Gtk::GestureClick> click_;
    Glib::RefPtr<Gtk::GestureClick> rightClick_;
    Glib::RefPtr<Gtk::EventControllerMotion> motion_;
};

#endif /* NODE_CANVAS_WIDGET_H */
