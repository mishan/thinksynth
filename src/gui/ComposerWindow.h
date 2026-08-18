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

#ifndef COMPOSER_WINDOW_H
#define COMPOSER_WINDOW_H

#include <map>
#include <string>
#include <vector>

#include <gtkmm.h>

#include "thcGenEdit.h"
#include "ComposerCanvas.h"

class thSynth;
class thArg;
class thcPlugin;
class thcScheduler;
struct thcStage;
class PianoRoll;

/* The composer's home: transport over the piece's knobs, the node
 * canvas, and the piano roll -- with, behind the Edit toggle, a panel
 * whose lower half follows the canvas selection.
 *
 * The canvas is the structure editor: click a stage, a sink, a chain
 * name or one of the ghost "+" slots and the panel grows the controls
 * for exactly that -- params with their units and knob bindings for a
 * stage, channel and target for a sink, the add forms for the ghosts.
 * Dragging a stage sideways reorders it. The canvas itself never
 * touches the file; it asks, and this window performs the edit through
 * thcGenEdit and reloads -- one writer, as everywhere else.
 *
 * The editing model is NodeEditor's: edits go to a work copy, Save
 * publishes. Value edits (params, knobs, tempo) splice the file AND
 * poke the live store, so the piece keeps playing; structural edits
 * splice and reload, which rewinds to zero -- the honest reading of
 * "same file, same seed, same piece" when the piece changed shape.
 *
 * Closing the window hides it; the scheduler and the music keep going.
 */
class ComposerWindow : public Gtk::Window
{
public:
    explicit ComposerWindow (thSynth *synth);
    ~ComposerWindow (void);

protected:
    /* Scan <pluginroot>/composer/ exactly as NodeEditor scans visual/. */
    void loadComposers (void);

    /* Source-file lifecycle: find the default piece, keep a work copy,
       parse the work copy, publish on save. */
    void loadPiece (void);          /* (re)copy source -> work, parse    */
    bool ensureWork (void);
    void parseWork (void);          /* work -> scheduler + all panels    */

    /* One structural edit has happened in the work file: reload it,
       rewind, resume if we were playing, rebuild the panels.
     *
     * Deferred to idle, because the handler asking for it usually lives
     * on a widget the rebuild is about to destroy -- a Remove button
     * cannot be deleted out from under its own clicked signal. */
    void structuralReload (void);
    void scheduleReload (bool markDirty);

    void onPlay (void);
    void onPause (void);
    void onRewind (void);
    void onReload (void);
    void onTempo (void);
    void onEditToggle (void);
    void onSave (void);
    void onSaveAs (void);
    void onSaveAsResponse (int response, Gtk::FileChooserDialog *dialog);
    void onNew (void);
    void onOpen (void);
    void onOpenConfirmed (void);
    void onOpenResponse (int response, Gtk::FileChooserDialog *dialog);

    /* Open and New both throw the work copy away; when it holds unsaved
       edits, the person gets asked first. `done' runs on yes, or
       immediately when there is nothing to lose. */
    void confirmDiscard (const sigc::slot<void ()> &done);

    void updateTransportButtons (void);
    void setDirty (bool dirty);

    /* An edit operation's outcome, shown to the person when it is no. */
    bool editOk (thcGenEdit::Result r, const std::string &why);

    bool onDrawTimer (void);

    /* ---- the editor panel -------------------------------------------- */

    void rebuildEditor (void);
    Gtk::Widget *buildPieceSection (void);
    Gtk::Widget *buildKnobsSection (void);
    Gtk::Widget *buildScalesSection (void);
    Gtk::Widget *buildPresetsSection (void);

    /* A preset's value changed: re-resolve it into every stage naming
       it. A value edit, not a structural one -- see the definition. */
    void presetChanged (const std::string &preset);

    /* Write a stage's clicked-into-shape state back into the file. See
       the definition for why it is a button and not a side effect of
       clicking. */
    void captureStage (size_t ci, size_t si);

    /* The selection-driven half, rebuilt whenever the canvas selection
       changes (or the piece reloads under it). */
    void rebuildSelection (void);
    void buildChainSelection (size_t ci);
    void buildStageSelection (size_t ci, size_t si);
    void buildSinkSelection (size_t ci, size_t ki);
    void buildAddStage (size_t ci);
    void buildAddSink (size_t ci);
    void buildAddChain (void);

    void addParamRow (Gtk::Grid *grid, int row, size_t ci, size_t si,
                      const thcPlugin *plugin, int paramIndex);

    /* Canvas callbacks. */
    void onCanvasSelection (const ComposerCanvas::Selection &sel);
    void onCanvasMoveStage (size_t chain, int from, int to);
    void onCanvasParams (size_t chain, size_t stage, Gdk::Rectangle at);
    void buildKnobSelection (size_t ki);
    void onCanvasKnob (std::string name, double value, bool commit);
    void onCanvasBindKnob (std::string knob, size_t chain, size_t stage,
                           Gdk::Rectangle at);
    void closeParams (void);

    /* The live stage behind a doc position, for poking values without a
       reload. Doc order and scheduler order agree because the loader
       builds chains in file order. */
    thcStage *liveStage (size_t ci, size_t si);

    /* Splice one param and poke the live store to match. */
    void applyParam (size_t ci, size_t si, const std::string &param,
                     const std::string &valueText);

    /* Default (name, valueText) pairs for a freshly added stage: every
       registered param, spelled per the writer's rules. */
    std::vector<std::pair<std::string, std::string> >
        defaultParams (const thcPlugin *plugin);

    /* Protected like the handlers above it, so a harness can drive the
       window the way editorcheck drives the node editor. */
    thSynth      *synth_;
    thcScheduler *sched_;

    /* Loaded modules, keyed by name; the window owns them. Chains hold
       bare pointers into this map, so it outlives them (clearChains runs
       in the scheduler's destructor, which runs first). */
    std::map<std::string, thcPlugin *> composers_;
    std::string composerRoot_;      /* where loadComposers looked        */

    std::string genPath_;           /* the source file; may be empty     */
    std::string workPath_;          /* the copy the edits go to          */
    std::string pieceLabel_;        /* what the status line calls it     */
    bool        dirty_;
    bool        reloadPending_;     /* an idle reload is already queued  */

    thcGenEdit::Doc doc_;           /* what the work file says           */

    PianoRoll *roll_;

    Gtk::Box vbox_{Gtk::Orientation::VERTICAL};
    Gtk::Box bar_{Gtk::Orientation::HORIZONTAL};

    /* Editor on the left of the paned when Edit is on. */
    Gtk::Paned paned_{Gtk::Orientation::HORIZONTAL};
    Gtk::Box playSide_{Gtk::Orientation::VERTICAL};
    Gtk::ScrolledWindow editorScroll_;
    Gtk::Box editorBox_{Gtk::Orientation::VERTICAL};

    /* The selection panel's home inside editorBox_, refilled in place
       so the sections above it keep their state. */
    Gtk::Box *selBox_;

    /* One slider per @knob the piece declares, rebuilt on load. */

    /* The node view, above the roll; inline composer_draw replaced the
       old draw strip. */
    ComposerCanvas *canvas_;
    Gtk::ScrolledWindow canvasScroll_;

    /* The canvas over the roll, with the split where the user left it.
       paneSet_ is false until the first split has been made. */
    Gtk::Paned rollPane_;
    bool paneSet_ = false;

    /* A stage's params, while one is showing. Owned by hand rather than
       managed: a popover parented to the canvas is not the canvas's
       child in the container sense, so nothing else would free it. */
    Gtk::Popover *paramPop_ = NULL;
    sigc::connection drawTimer_;

    /* The live MIDI hop into injectMidiEvent, and -- behind the Kbd
       input toggle -- the on-screen keyboard's hop into the same place. */
    sigc::connection midiOnConn_;
    sigc::connection midiOffConn_;
    sigc::connection kbdOnConn_;
    sigc::connection kbdOffConn_;
    Gtk::ToggleButton *kbdBtn_;

    void onKbdToggle (void);
    void injectOn (int chan, float note, float veloc);
    void injectOff (int chan, float note);

    Gtk::Button *playBtn_;
    Gtk::Button *pauseBtn_;
    Gtk::Button *rewindBtn_;
    Gtk::Button *reloadBtn_;
    Gtk::ToggleButton *editBtn_;
    Gtk::Button *saveBtn_;
    Gtk::Button *saveAsBtn_;
    Gtk::Button *newBtn_;
    Gtk::Button *openBtn_;

    Gtk::Label *tempoLbl_;
    Gtk::SpinButton *tempoBtn_;
    Glib::RefPtr<Gtk::Adjustment> tempoVal_;
    bool tempoGuard_;               /* true while we set the spin        */

    Gtk::Label *status_;
};

#endif /* COMPOSER_WINDOW_H */
