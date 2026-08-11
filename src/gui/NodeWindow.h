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
#include "NodePalette.h"

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

    /* Parses and displays a .dsp. Safe to call again to switch files.
     *
     * `chan' is the channel the file is loaded on, or -1. When it is a real
     * channel playing this same .dsp, edits are applied to the live graph as
     * well as recorded for the save, so they can be heard.
     *
     * The file itself is not edited. It is copied to a scratch .dsp, and that
     * is what everything here writes to -- see work_. */
    bool open (const string &filename, int chan = -1);

    /* The .dsp this window is showing: where it was opened from and where
       Save will put it back. Not the file being written to as you work. */
    const string &filename (void) const { return source_; }

protected:
    void onArrange (void);
    void onSave (void);
    void onRevert (void);
    void onZoomIn (void);
    void onZoomOut (void);
    void onZoomReset (void);
    void onZoomFit (void);
    void onBoxMoved (int box);
    void onSelected (int box);

    /* A rubber band gathered several boxes; `n' of them. */
    void onSelectionChanged (int n);
    void onParamEdited (int box, string name, double value);
    void onConnect (int fromBox, int fromPort, int toBox, int toPort);
    void onDisconnect (int edge);
    void onRefused (string why);
    void onControlChanged (int box, double value, bool commit);
    void onPaletteAdd (string spelling);
    void onPaletteAddControl (void);

    /* Asks for a control's name, range and label. False if cancelled. */
    bool askControl (string &name, double &value, double &min, double &max,
                     string &label);
    void onNewFile (void);
    void onDeleteNode (void);
    void onSaveAs (void);

    /* Copies `from' over `to', whole. Used to take the source into the
       working copy on open, and to put it back on save. */
    static bool copyFile (const string &from, const string &to);

    /* Points work_ at a fresh scratch .dsp holding a copy of `source'. False
       if the copy could not be made, which is the one way opening a readable
       file can still fail. */
    bool startWorkingCopy (const string &source);

    /* Reparses work_ and rebuilds the display from it, without touching the
       working copy. What a structural edit calls: add and delete write to
       work_ and then need the ports a parse gives them.

       open() is this plus taking a fresh copy of the source first, which is
       why Revert can be open() and a structural edit cannot. */
    bool reload (void);

    /* Asks where to put it, saves there, and adopts it as the source. */
    bool saveAsDialog (void);

    /* True if the file this was opened from can be written. A .dsp installed
       with the package is typically not, and that must not stop anyone
       editing it -- it only decides whether Save can put it back. */
    bool sourceWritable (void) const;

    /* Every node name the file uses, so a new one can avoid them. */
    vector<string> takenNames (void) const;

    /* Pushes an edit into the running synth, if this window is attached to a
       channel playing this file. Returns what happened, for the status bar. */
    const char *applyControlLive (const string &name, double value);
    const char *applyValueLive (const string &node, const string &arg,
                                double value);

    /* True if channel_ is playing the file this window is showing. */
    bool attached (void) const;

    /* True if anything here is not in the source yet -- pending edits, or a
       structural change already in the working copy. */
    bool dirty (void) const;

    void setStatus (const string &text);

    /* The selection count, which the selection owns and clears again. Kept
       apart from setStatus so deselecting cannot wipe a real message. */
    void setSelectionStatus (const string &text);
    void clearSelectionStatus (void);
    void updateTitle (void);
    void updateDirty (void);

    /* Positions and pending values, written together. */
    bool writeAll (string &why);

    /* writeAll, but a no-op success when there is nothing pending. What the
       add and delete paths call before they touch the file: each of them
       reopens afterwards, and the reopen discards anything still in memory. */
    bool flushPending (string &why);

private:
    thSynth *synth_;
    thSynthTree *tree_;     /* owned by this window */

    NodeGraph graph_;

    /* Where this .dsp came from, and where Save writes it back to. Read on
       open and written on save; never edited in place. */
    string source_;

    /* The scratch copy every edit actually goes to.
     *
     * A new node's ports exist only after a parse, so adding one means writing
     * it out and reparsing -- there is no way to show it otherwise. That used
     * to be done to the user's own file, which had two consequences: a .dsp
     * you cannot write could not be edited at all, and an add could not be
     * undone, because Revert reloads the file the add had already gone into.
     * Both were the same mistake, which was editing the original.
     *
     * So the original is copied here on open and everything writes to the
     * copy. Save puts it back; Revert takes a fresh copy; and a read-only
     * source is now just a source that Save has to send somewhere else. */
    string work_;

    int channel_;           /* channel this file is loaded on, or -1 */

    bool layoutDirty_;

    /* True when the working copy has structural edits -- nodes or controls
       added or removed -- that the source does not have. Values and wires are
       tracked separately below, because those are still held in memory; this
       is for the ones that are already in the working file. */
    bool structuralDirty_;

    /* True while the status bar is showing the selection count rather than
       something that happened. */
    bool selStatus_;

    /* Edits typed into the panel but not yet written. Keyed by node name and
       arg name rather than by box index, so they survive the reparse that
       follows a save. Nothing touches the file until Save -- an editor that
       rewrote a .dsp on every focus-out would be alarming to use, however
       well tested the writer is. */
    std::map<std::pair<std::string, std::string>, double> pending_;

    /* Wire edits, in the order they were made. Order matters: connecting an
       input and then disconnecting it must end up disconnected, and a map
       keyed by target would be enough for that, but replaying them in order
       is easier to reason about and there are never many. */
    struct WireEdit {
        string node, arg;       /* the input end                     */
        string srcNode, srcPort;/* empty for a disconnect            */
        string srcControl;      /* set instead, for a control source */
    };

    vector<WireEdit> wires_;

    /* Control values changed by dragging a slider, keyed by chanarg name. */
    std::map<std::string, double> controls_;

    /* Nodes added and removed. Unlike values and wires these are applied to
       the file straight away rather than held until Save.

       Adding a node means asking the plugin what ports it has, which means
       loading it, which is exactly what reparsing the file does anyway -- and
       a half-added node that exists on the canvas but not on disk is a state
       with no honest way to draw it. So the file is the record, and Revert is
       what undoes it. */
    Gtk::Button newBtn_;
    Gtk::Button deleteBtn_;

    Gtk::VBox vbox_;
    Gtk::HBox toolbar_;
    Gtk::HPaned outer_;     /* palette | the rest      */
    Gtk::HPaned split_;     /* canvas  | parameters    */
    Gtk::ScrolledWindow scroller_;
    NodeCanvas canvas_;
    NodeParams params_;
    NodePalette palette_;
    Gtk::Label status_;

    Gtk::Button arrangeBtn_;
    Gtk::Button saveBtn_;
    Gtk::Button saveAsBtn_;
    Gtk::Button revertBtn_;
    Gtk::Button zoomInBtn_;
    Gtk::Button zoomOutBtn_;
    Gtk::Button zoomResetBtn_;
    Gtk::Button zoomFitBtn_;
};

#endif /* NODE_WINDOW_H */
