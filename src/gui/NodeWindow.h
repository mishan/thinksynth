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

#ifndef NODE_WINDOW_H
#define NODE_WINDOW_H 1

#include "../NodeGraph.h"
#include "NodeCanvas.h"

class thSynth;

/*
 * Window around a NodeCanvas: opens a .dsp, shows its signal flow, remembers
 * where the boxes were put.
 *
 * The tree it parses is its own -- see thSynth::parseTree -- so nothing here
 * can disturb a channel that is playing. Read-only as far as the audio engine
 * is concerned; the only thing this window can write is the layout comments.
 */
class NodeWindow : public Gtk::Window
{
public:
    NodeWindow (thSynth *synth);
    ~NodeWindow (void);

    /* Parses and displays a .dsp. Safe to call again to switch files. */
    bool open (const string &filename);

    const string &filename (void) const { return filename_; }

protected:
    void onArrange (void);
    void onSaveLayout (void);
    void onZoomIn (void);
    void onZoomOut (void);
    void onZoomReset (void);
    void onBoxMoved (int box);

    void setStatus (const string &text);
    void updateTitle (void);

private:
    thSynth *synth_;
    thSynthTree *tree_;     /* owned by this window */

    NodeGraph graph_;
    string filename_;
    bool dirty_;

    Gtk::VBox vbox_;
    Gtk::HBox toolbar_;
    Gtk::ScrolledWindow scroller_;
    NodeCanvas canvas_;
    Gtk::Label status_;

    Gtk::Button arrangeBtn_;
    Gtk::Button saveBtn_;
    Gtk::Button zoomInBtn_;
    Gtk::Button zoomOutBtn_;
    Gtk::Button zoomResetBtn_;
};

#endif /* NODE_WINDOW_H */
