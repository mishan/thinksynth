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
#include "NodeParams.h"

class thSynth;

/*
 * Window around a NodeCanvas: opens a .dsp, shows its signal flow, remembers
 * where the boxes were put.
 *
 * The tree it parses is its own -- see thSynth::parseTree -- so nothing here
 * can disturb a channel that is playing. Read-only as far as the audio engine
 * is concerned: what this window writes, it writes to the .dsp on disk, and
 * only when asked.
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
    void onSave (void);
    void onRevert (void);
    void onZoomIn (void);
    void onZoomOut (void);
    void onZoomReset (void);
    void onBoxMoved (int box);
    void onSelected (int box);
    void onParamEdited (int box, string name, double value);

    void setStatus (const string &text);
    void updateTitle (void);
    void updateDirty (void);

    /* Positions and pending values, written together. */
    bool writeAll (string &why);

private:
    thSynth *synth_;
    thSynthTree *tree_;     /* owned by this window */

    NodeGraph graph_;
    string filename_;

    bool layoutDirty_;

    /* Edits typed into the panel but not yet written. Keyed by node name and
       arg name rather than by box index, so they survive the reparse that
       follows a save. Nothing touches the file until Save -- an editor that
       rewrote a .dsp on every focus-out would be alarming to use, however
       well tested the writer is. */
    std::map<std::pair<std::string, std::string>, double> pending_;

    Gtk::VBox vbox_;
    Gtk::HBox toolbar_;
    Gtk::HPaned split_;
    Gtk::ScrolledWindow scroller_;
    NodeCanvas canvas_;
    NodeParams params_;
    Gtk::Label status_;

    Gtk::Button arrangeBtn_;
    Gtk::Button saveBtn_;
    Gtk::Button revertBtn_;
    Gtk::Button zoomInBtn_;
    Gtk::Button zoomOutBtn_;
    Gtk::Button zoomResetBtn_;
};

#endif /* NODE_WINDOW_H */
