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

#ifndef COMPOSER_H
#define COMPOSER_H

#include <map>
#include <string>
#include <vector>

#include <gtkmm.h>

#include "thcGenEdit.h"
/* By value in prevInstruments_ below, so a forward declaration will not
   do -- and it is the same header ComposerCanvas already pulls in. */
#include "thcScheduler.h"
#include "ComposerCanvasWidget.h"
#include "SeqView.h"
#include "StageParamsView.h"

class thSynth;
class thArg;
class thcPlugin;
class thcScheduler;
struct thcStage;
class PianoRoll;

/* The composer: a piece, its scheduler, and the widgets that show it --
 * the node canvas, the sequencer, the piano roll, the piece's settings and
 * a panel that follows the canvas selection, each a pane of the main
 * window, and the transport for its title bar.
 *
 * The canvas is the structure editor: click a stage, a sink, a chain
 * name or one of the ghost "+" slots and the Selection pane grows the
 * controls for exactly that -- params with their units and knob bindings
 * for a stage, channel and target for a sink, the add forms for the
 * ghosts. Dragging a stage sideways reorders it. The canvas itself never
 * touches the file; it asks, and this performs the edit through
 * thcGenEdit and reloads -- one writer, as everywhere else.
 *
 * The editing model is NodeEditor's: edits go to a work copy, Save
 * publishes. Value edits (params, knobs, tempo) splice the file AND
 * poke the live store, so the piece keeps playing; structural edits
 * splice and reload, which rewinds to zero -- the honest reading of
 * "same file, same seed, same piece" when the piece changed shape.
 *
 * Nothing is loaded until start(): the piece's instruments go onto
 * channels, and a program that did that because it was opened would be
 * taking channels from whoever had them. The window starts it the first
 * time one of the panes comes into view. Closing the panes leaves the
 * scheduler and the music going.
 */
class Composer : public sigc::trackable
{
public:
    explicit Composer (thSynth *synth);
    ~Composer (void);

    Composer (const Composer &) = delete;
    Composer &operator= (const Composer &) = delete;

    /* Loads the composer modules and the piece, and starts the canvas's
       clock. Once; after that, it does nothing. */
    void start (void);
    bool started (void) const { return started_; }

    /* Which of two pieces is loaded: the one piece mode opens, or the
     * sequence, which starts as a copy of gen/scratch.gen with no file of
     * its own -- so Save asks where, rather than writing over the shipped
     * one. Each keeps its work and its unsaved edits while the other is
     * up, and switching stops the transport and loads the other from the
     * top. Before start() it only says which one start() loads.
     */
    enum Document { PIECE, SEQUENCE };

    void useDocument (Document which);
    Document document (void) const { return which_; }

    /* The graph behind the instrument track `ci', `si' plays, and so
       every track playing that instrument: its `dsp' line in the file,
       and the track's `rows' -- one for a graph that ignores the note,
       and a ladder for one that does not. Reloads, which rewinds. False
       for a track whose sink is a channel rather than an instrument. */
    bool setTrackInstrument (size_t ci, size_t si, const std::string &dsp,
                             bool readsNote);

    /* The tracks, for the host: which ones have a chooser, and what a
       click on one asks for. */
    SeqView &sequencer (void) { return seq_; }

    /* The panes' content. The host parents them, and has to let them go
       before this is destroyed. */
    Gtk::Widget &canvasView (void) { return canvasScroll_; }
    Gtk::Widget &sequencerView (void) { return seq_; }
    Gtk::Widget &rollView (void);
    Gtk::Widget &settingsView (void) { return editorScroll_; }
    Gtk::Widget &selectionView (void) { return selScroll_; }

    /* The transport and the Kbd input toggle, and the line that says what
       is playing: for a title bar. Managed, so the host's packing owns
       them -- they are the host's to pack, or they are never freed. */
    Gtk::Widget &transport (void) { return *transport_; }
    Gtk::Label &status (void) { return *status_; }

    /* New, Open, Save, Save As and Revert, as the "composer" actions, and
       a menu section naming them. */
    Glib::RefPtr<Gio::ActionGroup> actions (void) { return acts_; }
    Glib::RefPtr<Gio::MenuModel> menu (void) { return menu_; }

    /* Whether the piece's settings or the selection are in view: what is
       not is not built. */
    void setEditing (bool on);

    /* Whether the canvas is in view: a hidden one is not redrawn. */
    void setCanvasShown (bool on) { canvasShown_ = on; }

    /* The same for the sequencer's tracks. */
    void setSequencerShown (bool on) { seqShown_ = on; }

    /* Something was selected on the canvas, and the Selection pane is
       where it can be edited. */
    sigc::signal<void ()> &signal_show_selection (void)
    {
        return showSelection_;
    }

    /* A menu command wants the piece before anything has shown it: the
       host puts the canvas in view. start() follows either way. */
    sigc::signal<void ()> &signal_wanted (void) { return wanted_; }

    /* start() has run: the transport has something to play. */
    sigc::signal<void ()> &signal_started (void) { return startedSig_; }

    /* New or Open, about to act: they are about the piece, so the host
       puts piece mode up -- and the piece back in -- before they do. */
    sigc::signal<void ()> &signal_file_command (void)
    {
        return fileCommand_;
    }

protected:
    /* Scan <pluginroot>/composer/ exactly as NodeEditor scans visual/. */
    void loadComposers (void);

    /* Source-file lifecycle: find the default piece, keep a work copy,
       parse the work copy, publish on save. */
    void loadPiece (void);          /* (re)copy source -> work, parse    */

    /* The file a document starts from: airports.gen for the piece, the
       sequence's scratch.gen. Empty when it cannot be found. */
    std::string startingFile (Document which) const;

    /* Copy what `which' starts from into the work file, and say whether
       that makes it the document's own file (the piece) or only its
       starting text (the sequence). */
    void startDocument (Document which);
    bool ensureWork (void);
    void parseWork (void);          /* work -> scheduler + all panels    */

    /* The scheduler's instrument hook, in the application's terms: a
       piece's instrument is not only a graph on a channel, it is a
       patch tab with a filename on it and an arg panel behind it, so
       this goes through gthPatchManager exactly as the Patch Selector
       does. The scheduler's own default -- loadTree and nothing else --
       is right for a harness and would be a channel the rest of the
       program could not see. */
    bool loadInstrument (const thcInstrument &inst, std::string &why);

    /* And the instrument's effect, on the same terms. An effect is part
       of a patch here -- the `effect' line in the file and the block on
       the page -- so it goes through gthPatchManager too, which is also
       what keeps a reload from rebuilding an effect it already has and
       emptying its delay line. `effect' empty takes one off. */
    bool loadEffect (int channel, const std::string &effect, int side,
                     std::string &why);

    /* The way back, for a load that failed after this one succeeded.
       False when the channel would not go, in which case it stays this
       window's to try again. */
    bool unloadInstrument (const thcInstrument &inst);

    /* Unloads whichever of prevOwned_ the piece just parsed no longer
       wants. A patch outlives the file that asked for it -- which is why
       the scheduler does not do this -- but a piece that drops an
       instrument and leaves its channel loaded leaves a tab nothing
       plays. */
    void releaseInstruments (void);

    /* The scheduler's channel-taken hook: true for a channel holding
       somebody else's patch. The piece's own channels from the load
       being replaced are not somebody else's, or an instrument would
       walk one to the right on every reload. */
    bool channelTaken (int channel);

    /* Every gen::grid in the piece written back into the work file where
       what it plays has moved from what was loaded or last written: see
       useDocument. */
    void captureGrids (void);

    /* A grid's `rows' after a change of the graph that plays it, and its
       pattern reshaped from the bottom to match. False, with the edit
       refused in the status line, when the file would not take it. */
    bool fitRows (size_t ci, size_t si, bool readsNote, std::string &why);

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
    void onSave (void);
    void onSaveAs (void);
    void onSaveAsResponse (int response, Gtk::FileChooserDialog *dialog);
    void onNew (void);
    void onOpen (void);
    void onOpenConfirmed (void);
    /* The browser's answer: a path, since opening a piece copies a file. */
    void onOpenChosen (std::string path);

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
       clicking. `report': say what happened in the status line, which
       the button does and a sequencer track, captured on every gesture,
       does not. */
    void captureStage (size_t ci, size_t si, bool report = true);

    /* The selection-driven half, rebuilt whenever the canvas selection
       changes (or the piece reloads under it). */
    void rebuildSelection (void);
    void buildChainSelection (size_t ci);
    void buildStageSelection (size_t ci, size_t si);
    void buildSinkSelection (size_t ci, size_t ki);
    void buildAddStage (size_t ci);
    void buildAddSink (size_t ci);

    /* The "what does this sink play" control: the piece's instruments
       by name plus `channel' for a patch it does not own. NULL, and no
       widget appended, when the piece declares no instruments -- one
       choice is not a choice. `targets' comes back parallel to the
       drop-down's items, with "" for the channel entry. */
    Gtk::DropDown *buildSinkTarget (Gtk::Box *row, Gtk::SpinButton *chan,
                                    const std::string &selected,
                                    std::vector<std::string> &targets);
    void buildAddChain (void);

    /* One stage's parameters, drawn and bound: StagePanel through
       PanelView. Made afresh at each use for the reason the popover is --
       a reload replaces every ParamInfo behind the rows. */
    StageParamsView *makeStageParams (size_t ci, size_t si);

    /* Canvas callbacks. */
    void onCanvasSelection (const ComposerCanvas::Selection &sel);
    void onCanvasMoveStage (size_t chain, int from, int to);
    void onCanvasParams (size_t chain, size_t stage, CanvasRect at);
    void buildKnobSelection (size_t ki);
    void onCanvasKnob (std::string name, double value, bool commit);
    void onCanvasBindKnob (std::string knob, size_t chain, size_t stage,
                           CanvasRect at);
    void onCanvasMute (size_t chain, bool on);
    void onCanvasSolo (size_t chain, bool on);
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

    /* Loaded modules, keyed by name; this owns them. Chains hold
       bare pointers into this map, so it outlives them (clearChains runs
       in the scheduler's destructor, which runs first). */
    std::map<std::string, thcPlugin *> composers_;
    std::string composerRoot_;      /* where loadComposers looked        */

    /* A channel this filled, and *which* patch it put there.
     *
     * The number alone is not ownership. Somebody who loads their own
     * patch onto one of the piece's channels has taken it, and a
     * comparison by channel -- or by the .dsp's filename, which is the
     * same mistake wearing a hat, since their patch may well be built
     * on the same graph -- would go on treating it as the piece's:
     * overwriting it on the next reload, and unloading it when the piece
     * dropped the instrument. gthPatchManager stamps each load with a
     * generation that is never reused, and that is what gets compared. */
    struct Owned
    {
        int         channel;
        unsigned    generation;

        /* Which .dsp actually went onto that channel -- not which one
           the declaration names. A swap puts a different graph on a
           channel the piece owns, keeping the generation (it is still
           our load) and changing nothing about the declaration, so the
           "unchanged instrument keeps its graph" shortcut below said
           keep and left the swapped-in graph up while the reload
           believed the declared one was there. Recording what was
           loaded is the only thing that can tell those apart. */
        std::string dsp;
    };

    /* What this filled for the piece currently open, so a piece
       that loses an instrument gives its channel back. prevOwned_ is the
       same list for the piece being replaced, live only while a parse is
       in flight: allocation consults it (those channels are the piece's
       to have back), the loader refills ownedChannels_, and
       releaseInstruments unloads the difference. */
    std::vector<Owned> ownedChannels_;
    std::vector<Owned> prevOwned_;

    /* True if `channel' still holds the exact patch prevOwned_ recorded
       -- the one question both of those lists exist to answer. */
    bool stillOurs (int channel) const;

    /* The instrument table the piece being replaced was loaded with,
       taken before the parse wipes it. Two things read it: an
       instrument whose whole declaration is unchanged keeps the graph
       it already has instead of having it rebuilt underneath a
       sounding voice, and a channel is only given back if what is on
       it is still the graph this put there. */
    std::vector<thcInstrument> prevInstruments_;

    std::string genPath_;           /* the source file; may be empty     */
    std::string workPath_;          /* the copy the edits go to          */
    std::string pieceLabel_;        /* what the status line calls it     */
    bool        dirty_;
    bool        reloadPending_;     /* an idle reload is already queued  */
    bool        reloadMarksDirty_ = false;  /* ...and what it will say   */

    /* What each grid's pattern was when the piece was loaded, or last
       written back, by "chain.stage.param" in the document's
       numbering. A capture that hands back the same text has
       nothing to write, whatever the file's spelling of it -- scratch.gen
       writes its drums with `|' bar lines, and the grid hands its
       pattern back without them. */
    std::map<std::string, std::string> baseline_;

    /* The chains muted and soloed when the piece was last parsed, by
       name, put back by the next parse: an edit is not a reason to hear
       every chain again. forgetMix() is what another document calls,
       and a rename says what the chain is called now. */
    std::vector<std::string> mutedNames_, soloedNames_;
    bool mixForgotten_ = true;
    std::map<std::string, std::string> mixRenamed_;

    void forgetMix (void);

    bool rollByChain_ = false;      /* the roll's notes by chain's hue   */

    thcGenEdit::Doc doc_;           /* what the work file says           */

    /* The roll, made in start() with the scheduler it reads, and the box
       that is its pane's content until then and its parent after. */
    PianoRoll *roll_;
    Gtk::Box rollBox_{Gtk::Orientation::VERTICAL};

    /* A menu command's way in before start(). */
    void wake (void);

    /* The file actions, and Save's own, because it is not fire-and-forget:
       it goes insensitive when there is nothing to save. */
    Glib::RefPtr<Gio::SimpleActionGroup> acts_;
    Glib::RefPtr<Gio::SimpleAction> saveAct_;
    Glib::RefPtr<Gio::Menu> menu_;

    bool started_ = false;
    bool editing_ = false;
    bool canvasShown_ = false;
    bool seqShown_ = false;
    sigc::signal<void ()> showSelection_;
    sigc::signal<void ()> wanted_;
    sigc::signal<void ()> startedSig_;
    sigc::signal<void ()> fileCommand_;

    /* The document up, and what the other one was left holding: its file,
       its work text and whether that had unsaved edits. `held' false for
       one never loaded, which starts from its starting file. */
    struct Held
    {
        bool held = false;
        std::string genPath;
        std::string text;
        bool dirty = false;
    };

    Document which_ = PIECE;
    Held held_[2];
    bool stale_ = false;

    Gtk::ScrolledWindow editorScroll_;
    Gtk::Box editorBox_{Gtk::Orientation::VERTICAL};
    Gtk::ScrolledWindow selScroll_;
    Gtk::Box selOuter_{Gtk::Orientation::VERTICAL};

    /* The selection's home inside selOuter_, refilled in place when the
       canvas selection changes. */
    Gtk::Box *selBox_;

    /* The node canvas; inline composer_draw replaced the old draw
       strip. */
    ComposerCanvasWidget *canvas_;
    Gtk::ScrolledWindow canvasScroll_;

    /* The piece's grids as tracks. What is drawn on one is captured into
       the work file when the gesture ends, as the Capture button does. */
    SeqView seq_;

    /* A stage's params, while one is showing. Owned by hand rather than
       managed: a popover parented to the canvas is not the canvas's
       child in the container sense, so nothing else would free it. */
    Gtk::Popover *paramPop_ = NULL;
    sigc::connection drawTimer_;

    /* The idle a reload is deferred to, kept so the destructor can take
       it back. The main loop holds a slot, not this, so an idle that
       captured `this' and outlived it is a call into freed memory. */
    sigc::connection reloadIdle_;

    /* The live MIDI hop into injectMidiEvent, and -- behind the Kbd
       input toggle -- the on-screen keyboard's hop into the same place. */
    sigc::connection midiOnConn_;
    sigc::connection midiOffConn_;
    sigc::connection kbdOnConn_;
    sigc::connection kbdOffConn_;
    Gtk::ToggleButton *kbdBtn_;

    void buildActions (void);
    void onKbdToggle (void);
    void injectOn (int chan, float note, float veloc);
    void injectOff (int chan, float note);

    Gtk::Button *playBtn_;
    Gtk::Button *pauseBtn_;
    Gtk::Button *rewindBtn_;

    /* What the transport is currently drawn as. A piece whose arrangement
       closes with `section end;' stops its own transport when the last
       section is over, and nothing pressed a button to make that
       happen, so the draw timer watches for the change. */
    bool shownRunning_;
    Gtk::Box *transport_;

    Gtk::Label *tempoLbl_;
    Gtk::SpinButton *tempoBtn_;
    Glib::RefPtr<Gtk::Adjustment> tempoVal_;
    bool tempoGuard_;               /* true while we set the spin        */

    Gtk::Label *status_;
};

#endif /* COMPOSER_H */
