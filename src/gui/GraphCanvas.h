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

#ifndef GRAPH_CANVAS_H
#define GRAPH_CANVAS_H 1

#include <gtkmm.h>

#include "../CanvasContent.h"

/*
 * The desktop shell of a canvas: the part that is a widget.
 *
 * A CanvasContent draws and decides what a click means; this is what
 * it needs from the toolkit and nothing more. A drawing area that hands
 * its draw to the content, a wheel that zooms it, a resize that lets a
 * deferred zoom-to-fit happen, and the arithmetic of how much of the
 * drawing is on screen -- which has to come from the scrolled window
 * this lives in, because the widget itself is sized to the whole
 * drawing and get_width() on it is the width of everything, scrolled
 * off or not.
 *
 * This used to hold the zoom and the coordinate conversion too, and
 * NodeCanvas and ComposerCanvas were its subclasses. The zoom went into
 * CanvasContent when the two canvases stopped being widgets (see that
 * header for why), and what is left here is the shell. A widget class
 * inherits from its content class and from this, forwards the content's
 * shell virtuals -- requestRedraw, resizeShell, shellViewport,
 * takeFocus -- to the widget calls, and creates the gesture controllers
 * that feed the content's handlers: src/gui/NodeCanvasWidget.h and
 * ComposerCanvasWidget.h, a few dozen lines each.
 *
 * There is no panning: the canvas sizes itself to the scaled content
 * and lives in a Gtk::ScrolledWindow, so scrolling is the scroller's
 * job and always behaves the way scrolling does everywhere else.
 * Ctrl+wheel zooms; a bare wheel is left alone for that reason.
 */
class GraphCanvas : public Gtk::DrawingArea
{
public:
    /* `content' is the content class this widget also is; it outlives
       this by being the same object. */
    explicit GraphCanvas (CanvasContent &content);

protected:
    /* The visible part of the drawing, in widget pixels: the scroller's
       position and its viewport's size, or the widget's own allocation
       when there is no scroller. False before anything is allocated.
       What a widget subclass answers CanvasContent::shellViewport with. */
    bool viewport (double &x, double &y, double &w, double &h) const;

    /* The scroll controller, so a subclass can ask about modifiers on
       its own gestures without making a second one. */
    Glib::RefPtr<Gtk::EventControllerScroll> scroll_;

private:
    void onDraw (const Cairo::RefPtr<Cairo::Context> &cr, int width,
                 int height);
    void onResize (int width, int height);
    bool onScroll (double dx, double dy);

    CanvasContent &content_;
};

#endif /* GRAPH_CANVAS_H */
