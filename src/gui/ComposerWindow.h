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

class thSynth;
class thArg;
class thcPlugin;
class thcScheduler;
struct thcStage;
class PianoRoll;

/* The composer's home: transport controls, the piece's knobs, the
 * tier-two plugin visualizers, the piano roll -- and, behind the Edit
 * toggle, a structural editor for the piece itself.
 *
 * The editing model is NodeEditor's, deliberately: edits go to a work
 * copy of the .gen through thcGenEdit's text splices (comments survive;
 * see thcGenEdit.h), and Save publishes the work copy over the source.
 * Two kinds of edit behave differently on purpose:
 *
 *   value edits (a param, a knob) splice the file AND poke the live
 *   store, so the piece keeps playing and just sounds different;
 *
 *   structural edits (stages, chains, sinks, scales, seed) splice the
 *   file and then reload it into the scheduler, which rewinds to zero
 *   -- the honest reading of "same file, same seed, same piece" when
 *   the piece itself changed shape.
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

    void rebuildKnobs (void);
    void rebuildDrawStrip (void);
    bool onDrawTimer (void);

    /* ---- the editor panel -------------------------------------------- */

    void rebuildEditor (void);
    Gtk::Widget *buildPieceSection (void);
    Gtk::Widget *buildKnobsSection (void);
    Gtk::Widget *buildScalesSection (void);
    Gtk::Widget *buildChainSection (size_t ci);
    Gtk::Widget *buildStageFrame (size_t ci, size_t si);
    void addParamRow (Gtk::Grid *grid, int row, size_t ci, size_t si,
                      const thcPlugin *plugin, int paramIndex);

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

    /* One slider per @knob the piece declares, rebuilt on load. */
    Gtk::Box knobBar_{Gtk::Orientation::HORIZONTAL};

    /* One drawing area per stage that exports composer_draw. */
    Gtk::Box drawBar_{Gtk::Orientation::HORIZONTAL};
    std::vector<Gtk::DrawingArea *> drawAreas_;
    sigc::connection drawTimer_;

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
