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

class thSynth;
class thArg;
class thcPlugin;
class thcScheduler;
struct thcStage;
class PianoRoll;

/* The composer's home: transport controls, the piece's knobs, the
 * tier-two plugin visualizers, and the piano roll.
 *
 * This window owns the scheduler and the loaded composer modules, and
 * plays whatever .gen file it loaded -- gen/airports.gen by default,
 * found through the same data-file search everything else uses.
 *
 * Closing the window hides it; the scheduler and the music keep going.
 * That is a feature, not an accident -- ambient pieces are furniture, and
 * the window is the dashboard, not the power switch.
 */
class ComposerWindow : public Gtk::Window
{
public:
    explicit ComposerWindow (thSynth *synth);
    ~ComposerWindow (void);

protected:
    /* Scan <pluginroot>/composer/ exactly as NodeEditor scans visual/. */
    void loadComposers (void);

    /* (Re)load the .gen file into the scheduler, then rebuild the knob
       panel and the draw strip to match what it declared. */
    void loadPiece (void);

    void onPlay (void);
    void onPause (void);
    void onRewind (void);
    void onReload (void);
    void onTempo (void);

    void updateTransportButtons (void);
    void rebuildKnobs (void);
    void rebuildDrawStrip (void);
    bool onDrawTimer (void);

private:
    thSynth      *synth_;
    thcScheduler *sched_;

    /* Loaded modules, keyed by name; the window owns them. Chains hold
       bare pointers into this map, so it outlives them (clearChains runs
       in the scheduler's destructor, which runs first). */
    std::map<std::string, thcPlugin *> composers_;
    std::string composerRoot_;      /* where loadComposers looked        */

    std::string genPath_;           /* the file the piece came from      */
    std::string pieceLabel_;        /* what the status line calls it     */

    PianoRoll *roll_;

    Gtk::Box vbox_{Gtk::Orientation::VERTICAL};
    Gtk::Box bar_{Gtk::Orientation::HORIZONTAL};

    /* One slider per @knob the piece declares, rebuilt on load. */
    Gtk::Box knobBar_{Gtk::Orientation::HORIZONTAL};

    /* One drawing area per stage that exports composer_draw -- the
       tier-two visualizers (the euclid ring, one day the CA grid). All
       GUI-thread, reading instance state directly; a slow timer keeps
       them honest while the piano roll's frame clock keeps itself. */
    Gtk::Box drawBar_{Gtk::Orientation::HORIZONTAL};
    std::vector<Gtk::DrawingArea *> drawAreas_;
    sigc::connection drawTimer_;

    Gtk::Button *playBtn_;
    Gtk::Button *pauseBtn_;
    Gtk::Button *rewindBtn_;
    Gtk::Button *reloadBtn_;

    Gtk::Label *tempoLbl_;
    Gtk::SpinButton *tempoBtn_;
    Glib::RefPtr<Gtk::Adjustment> tempoVal_;

    Gtk::Label *status_;
};

#endif /* COMPOSER_WINDOW_H */
