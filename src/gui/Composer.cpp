/*
 * Copyright (C) 2004-2026 The thinksynth authors
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
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <fstream>
#include <sstream>

#include <gtkmm.h>

#include "think.h"

#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"
#include "thcFreeze.h"
#include "thcGenEdit.h"
#include "thcMidiExport.h"
#include "PianoRoll.h"
#include "Dialogs.h"
#include "GenCatalog.h"
#include "ItemBrowser.h"
#include "gthPatchfile.h"
#include "gthSignal.h"
#include "gthMidiOut.h"
#include "Composer.h"

/* ---- little local helpers --------------------------------------------- */

static bool
copyOver (const std::string &from, const std::string &to)
{
    std::error_code ec;

    std::filesystem::copy_file(from, to,
        std::filesystem::copy_options::overwrite_existing, ec);

    return !ec;
}

/* A grid's pattern -- rows top-down, `/' between them -- at `rows' rows,
   keeping the bottom ones: the lowest degree is the root a line is written
   on, and a taller grid grows empty rows above it. */
static std::string
bottomRows (const std::string &cells, int rows)
{
    std::vector<std::string> have;
    size_t from = 0;

    for (;;)
    {
        const size_t slash = cells.find('/', from);

        have.push_back(cells.substr(from, slash == std::string::npos
                                              ? std::string::npos
                                              : slash - from));

        if (slash == std::string::npos)
            break;

        from = slash + 1;
    }

    const std::string empty(have.back().size(), '.');
    std::vector<std::string> out;

    for (int i = 0; i < rows - (int)have.size(); i++)
        out.push_back(empty);

    for (size_t i = have.size() > (size_t)rows ? have.size() - rows : 0;
         i < have.size(); i++)
        out.push_back(have[i]);

    std::string text;

    for (size_t i = 0; i < out.size(); i++)
        text += (i > 0 ? "/" : "") + out[i];

    return text;
}

static std::string
readText (const std::string &path)
{
    std::ifstream in(path.c_str(), std::ios::binary);

    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

/* ---- construction ----------------------------------------------------- */

Composer::Composer (thSynth *synth)
    : synth_(synth), dirty_(false), reloadPending_(false),
      shownRunning_(false), tempoGuard_(false)
{
    selBox_ = NULL;
    kbdBtn_ = NULL;

    /* No scheduler until start(): it runs a clock of its own from the
       moment it exists, and a program that never opens a piece has no use
       for one. */
    sched_ = NULL;
    roll_ = NULL;

    playBtn_ = manage(new Gtk::Button("Play"));
    pauseBtn_ = manage(new Gtk::Button("Pause"));
    rewindBtn_ = manage(new Gtk::Button("Rewind"));

    playBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &Composer::onPlay));
    pauseBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &Composer::onPause));
    rewindBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &Composer::onRewind));

    tempoLbl_ = manage(new Gtk::Label("Tempo"));
    tempoVal_ = Gtk::Adjustment::create(120, 20, 300, 1, 10);
    tempoBtn_ = manage(new Gtk::SpinButton(tempoVal_));
    tempoVal_->signal_value_changed().connect(
        sigc::mem_fun(*this, &Composer::onTempo));

    status_ = manage(new Gtk::Label(""));
    status_->set_ellipsize(Pango::EllipsizeMode::END);

    kbdBtn_ = manage(new Gtk::ToggleButton("Kbd input"));
    kbdBtn_->set_tooltip_text("Feed the on-screen keyboard into "
                              "chains with MIDI input, alongside "
                              "hardware MIDI");
    kbdBtn_->signal_toggled().connect(
        sigc::mem_fun(*this, &Composer::onKbdToggle));

    buildActions();

    /* The node canvas: the piece's face. The tier-two visualizers live
       inside its stage boxes, where the old draw strip used to be a row
       of orphans. */
    canvas_ = manage(new ComposerCanvasWidget());
    canvas_->sigSelection.connect(
        sigc::mem_fun(*this, &Composer::onCanvasSelection));
    canvas_->sigMoveStage.connect(
        sigc::mem_fun(*this, &Composer::onCanvasMoveStage));
    canvas_->sigParams.connect(
        sigc::mem_fun(*this, &Composer::onCanvasParams));
    canvas_->sigKnob.connect(
        sigc::mem_fun(*this, &Composer::onCanvasKnob));
    canvas_->sigBindKnob.connect(
        sigc::mem_fun(*this, &Composer::onCanvasBindKnob));
    canvas_->sigMute.connect(
        sigc::mem_fun(*this, &Composer::onCanvasMute));
    canvas_->sigSolo.connect(
        sigc::mem_fun(*this, &Composer::onCanvasSolo));
    canvas_->sigSectionLevel.connect(
        sigc::mem_fun(*this, &Composer::onCanvasSectionLevel));
    canvas_->sigFreeze.connect(
        sigc::mem_fun(*this, &Composer::onCanvasFreeze));

    /* A section's block: the piece from there, as it would be had it
       played there. */
    canvas_->sigSeek.connect(
        [this](double at)
        {
            sched_->seek(at);
            updateTransportButtons();
            canvas_->queue_draw();
        });

    /* A picture that edits its own params -- a grid's cells, the euclid
       ring, accent's steps -- is written into the piece when the gesture
       ends, silently, as the sequencer's tracks are. A Life board is
       not: its capture stays the Selection pane's deliberate act. */
    canvas_->sigGestureEnd.connect(
        [this](size_t chain, size_t stage)
        {
            thcStage *s = liveStage(chain, stage);

            if (s != NULL && s->plugin->inputEdits())
                captureStage(chain, stage, false);
        });

    canvasScroll_.set_child(*canvas_);
    canvasScroll_.set_policy(Gtk::PolicyType::AUTOMATIC,
                             Gtk::PolicyType::AUTOMATIC);
    canvasScroll_.set_propagate_natural_height(true);
    canvasScroll_.set_propagate_natural_width(true);

    /* A pattern drawn on a track goes into the work file when the
       gesture ends, through the same capture the Selection's button
       makes. The canvas keeps the two acts apart -- a click on a picture
       is a performance, and writing it down a decision -- but a track's
       pattern is what the sequencer is for writing, and without this
       Save kept the file's pattern and dropped what was drawn over it. */
    seq_.signal_edited().connect(
        [this](size_t ci, size_t si) { captureStage(ci, si, false); });

    /* A patch loaded onto a track's channel by hand changes what the
       track's heading says. */
    if (gthPatchManager *pm = gthPatchManager::instance())
        pm->signal_patches_changed().connect(
            sigc::mem_fun(seq_, &SeqView::refresh));

    /* The roll's place: the roll itself reads the scheduler, so it is made
       with it, in start(). */
    rollBox_.set_hexpand(true);
    rollBox_.set_vexpand(true);

    /* Both directions, and that is deliberate.
     *
       With horizontal scrolling off, a scrolled window hands its child's
       full width up as a minimum -- and a Selection row is a label, a
       spin button, a unit menu and a binding menu, which comes to about
       seven hundred pixels. The pane could then not be narrower than
       that. Letting it scroll sideways is what makes the panel a panel
       instead of the other half of the window. */
    editorScroll_.set_child(editorBox_);
    editorScroll_.set_policy(Gtk::PolicyType::AUTOMATIC,
                             Gtk::PolicyType::AUTOMATIC);

    editorBox_.set_margin(6);
    editorBox_.set_spacing(6);

    selScroll_.set_child(selOuter_);
    selScroll_.set_policy(Gtk::PolicyType::AUTOMATIC,
                          Gtk::PolicyType::AUTOMATIC);

    selOuter_.set_margin(6);
    selOuter_.set_spacing(6);

    /* Live MIDI into the chains: the same m_sigNoteOn/Off hop that
       lights the on-screen keyboard, already on the GUI thread. A press
       has no known length, so it goes in held (duration 0) and the
       release follows as a NOTEOFF; every chain that declared `input
       midi' on that channel hears both. */
    midiOnConn_ = m_sigNoteOn.connect(
        sigc::mem_fun(*this, &Composer::injectOn));
    midiOffConn_ = m_sigNoteOff.connect(
        sigc::mem_fun(*this, &Composer::injectOff));

    playedConns_[0] = m_sigNoteOn.connect(
        sigc::mem_fun(*this, &Composer::playedOn));
    playedConns_[1] = m_sigNoteOff.connect(
        sigc::mem_fun(*this, &Composer::playedOff));
    playedConns_[2] = m_sigKbdNoteOn.connect(
        sigc::mem_fun(*this, &Composer::playedOn));
    playedConns_[3] = m_sigKbdNoteOff.connect(
        sigc::mem_fun(*this, &Composer::playedOff));
    playedConns_[4] = m_sigNoteClear.connect(
        sigc::mem_fun(*this, &Composer::playedCleared));

    updateTransportButtons();
}

Gtk::Widget &
Composer::rollView (void)
{
    return rollBox_;
}

void
Composer::start (void)
{
    if (started_)
        return;

    started_ = true;

    sched_ = new thcScheduler(synth_);
    sched_->setInstrumentLoader(
        [this](const thcInstrument &inst, std::string &why)
        { return loadInstrument(inst, why); });
    sched_->setInstrumentUnloader(
        [this](const thcInstrument &inst) { return unloadInstrument(inst); });
    sched_->setEffectLoader(
        [this](int channel, const std::string &effect, int side,
               std::string &why)
        { return loadEffect(channel, effect, side, why); });
    sched_->setChannelTaken(
        [this](int channel) { return channelTaken(channel); });
    sched_->setMidiOut(midiOut_);

    /* Managed, and in rollBox_, which is this's: taken out of it in the
       destructor, which is what frees it, before the scheduler it reads
       goes. */
    roll_ = manage(new PianoRoll(sched_));
    roll_->setColorByChain(rollByChain_);
    roll_->set_hexpand(true);
    roll_->set_vexpand(true);
    rollBox_.append(*roll_);

    loadComposers();

    drawTimer_ = Glib::signal_timeout().connect(
        sigc::mem_fun(*this, &Composer::onDrawTimer), 50);

    loadPiece();

    startedSig_.emit();
}

/* A file command from the menu, before anything has started: the piece is
   what it acts on, so the piece comes up first -- in view, which is where
   what the command did can be seen. */
void
Composer::wake (void)
{
    if (started_)
        return;

    wanted_.emit();
    start();
}

/* The piece's settings and the selection are built the first time one of
   them is in view, and kept while neither is, so a look at another tab and
   back finds them as they were left. What changes while they are out of
   view -- a reload, a new selection -- tears them down and leaves them
   stale, and they are built again when next looked at: the rows hold
   widgets bound to the piece, and rebuilding them for nobody is a
   reload's worth of work on every edit. */
void
Composer::setEditing (bool on)
{
    if (on == editing_)
        return;

    editing_ = on;

    if (on && (stale_ || editorBox_.get_first_child() == NULL))
        rebuildEditor();
}

Composer::~Composer (void)
{
    drawTimer_.disconnect();
    reloadIdle_.disconnect();
    midiIdle_.disconnect();
    midiOnConn_.disconnect();
    midiOffConn_.disconnect();
    kbdOnConn_.disconnect();
    kbdOffConn_.disconnect();

    for (sigc::connection &c : playedConns_)
        c.disconnect();

    /* The roll reads the scheduler, so it goes first: out of its box,
       which frees it. */
    if (roll_ != NULL)
        rollBox_.remove(*roll_);

    roll_ = NULL;

    /* The tracks point into the scheduler's stages. */
    seq_.setPiece(NULL, NULL);

    /* Order matters: the scheduler's destructor flushes note-offs and
       destroys chain instances, which calls back into the plugins -- so
       the plugins must still be loaded when it runs. */
    delete sched_;

    for (std::map<std::string, thcPlugin *>::iterator i = composers_.begin();
         i != composers_.end(); ++i)
        delete i->second;

    if (!workPath_.empty())
        ::remove(workPath_.c_str());

    closeParams();
}

/* Same walk NodeEditor does over visual/, one directory over. */
void
Composer::loadComposers (void)
{
    thPluginManager *pm = synth_ ? synth_->getPluginManager() : NULL;
    string root = pm ? pm->pluginPath() : string(PLUGIN_PATH);

    if (root.empty() || root[root.size() - 1] != '/')
        root += '/';

    root += "composer/";
    composerRoot_ = root;

    std::error_code ec;

    if (!std::filesystem::is_directory(root, ec))
        return;

    for (const auto &f : std::filesystem::directory_iterator(root, ec))
    {
        if (ec)
            break;

        if (f.path().extension() != PLUGIN_SUFFIX)
            continue;

        thcPlugin *p = new thcPlugin(f.path().string());

        if (p->state() != thcPlugin::LOADED)
        {
            delete p;                    /* it already said why          */
            continue;
        }

        std::map<std::string, thcPlugin *>::iterator have =
            composers_.find(p->name());

        if (have != composers_.end())
        {
            fprintf(stderr, "Composer: two composer modules both "
                    "called '%s'; keeping %s\n", p->name().c_str(),
                    have->second->path().c_str());
            delete p;
            continue;
        }

        composers_[p->name()] = p;
    }
}

/* ---- source and work files -------------------------------------------- */

bool
Composer::ensureWork (void)
{
    if (!workPath_.empty())
        return true;

    workPath_ = thUtil::tempFile("thinksynth-gen-");

    return !workPath_.empty();
}

void
Composer::loadPiece (void)
{
    if (!ensureWork())
    {
        pieceLabel_ = "could not create a working file";
        updateTransportButtons();
        return;
    }

    if (genPath_.empty() || !copyOver(genPath_, workPath_))
        startDocument(which_);

    setDirty(false);
    parseWork();
}

std::string
Composer::startingFile (Document which) const
{
    return thUtil::findDataFile(which == SEQUENCE ? "scratch.gen"
                                                  : "airports.gen",
                                "gen", "THINK_GEN_PATH", "");
}

void
Composer::startDocument (Document which)
{
    const std::string from = startingFile(which);

    if (from.empty() || !copyOver(from, workPath_))
    {
        /* No piece to load is a blank page, not an error: New starts
           here too. */
        std::ofstream out(workPath_.c_str(), std::ios::trunc);

        out << "name \"Untitled\";\n";
        genPath_.clear();
        return;
    }

    /* The piece is the file it came from. The sequence only starts from
       one: it is somebody's own from the first cell they draw, and Save
       writing it over the shipped scratch pad would be the wrong file. */
    genPath_ = which == SEQUENCE ? std::string() : from;
}

void
Composer::useDocument (Document which)
{
    if (which == which_)
        return;

    if (!started_ || !ensureWork())
    {
        which_ = which;
        return;
    }

    Held &was = held_[which_];

    forgetMix();

    /* A reload still queued belongs to the document being put away: its
       edit is already in the work text, and what it would have said
       about unsaved edits is this document's, not the next one's. Its
       live stages are about to be replaced, so there is nothing of them
       to capture either. */
    if (reloadPending_)
    {
        reloadIdle_.disconnect();
        reloadPending_ = false;
        was.dirty = reloadMarksDirty_;
    }
    else
    {
        /* What was drawn on a grid and never written down -- a click on
           the canvas, notes a `listen' grid recorded -- would otherwise
           go with the scheduler. Grids only: a Life board or a CA moves
           on its own, and capturing one is a decision about the piece. */
        captureGrids();
        was.dirty = dirty_;
    }

    was.held = true;
    was.genPath = genPath_;
    was.text = readText(workPath_);

    which_ = which;

    const Held &now = held_[which];
    bool dirty = false;

    /* Stopped, and from the top: what was playing is the other document,
       and a sequence left running under a piece would be two pieces. */
    sched_->halt();

    if (now.held)
    {
        std::ofstream out(workPath_.c_str(),
                          std::ios::trunc | std::ios::binary);

        out << now.text;
        genPath_ = now.genPath;
        dirty = now.dirty;
    }
    else
        startDocument(which);

    parseWork();
    sched_->reset();
    setDirty(dirty);
}

bool
Composer::setTrackInstrument (size_t ci, size_t si, const std::string &dsp,
                              bool readsNote)
{
    if (!started_ || ci >= doc_.chains.size() ||
        si >= doc_.chains[ci].stages.size())
        return false;

    const thcGenEdit::Chain &chain = doc_.chains[ci];
    std::string inst;

    for (size_t k = 0; k < chain.sinks.size(); k++)
        if (!chain.sinks[k].instrument.empty() &&
            chain.sinks[k].chanarg.empty())
        {
            inst = chain.sinks[k].instrument;
            break;
        }

    if (inst.empty())
    {
        status_->set_text("chain " + chain.name + " plays a channel, not one "
                          "of the piece's instruments");
        return false;
    }

    /* Refused before anything is written, rather than after the rows
       have been: setInstrumentDsp refuses the same names, and by then a
       track would have been reshaped for a graph it never got. */
    if (dsp.empty() || dsp.find('"') != std::string::npos ||
        dsp.find('\n') != std::string::npos)
    {
        status_->set_text("'" + dsp + "' cannot be written as a file name");
        return false;
    }

    /* Several edits, and all of them or none: what the file said before
       goes back if any is refused, so the work file never holds half a
       change the live piece does not. */
    const std::string before = readText(workPath_);
    std::string why;
    bool ok = editOk(thcGenEdit::setInstrumentDsp(workPath_, inst, dsp, why),
                     why);

    /* Every grid the instrument plays, not only the one whose button was
       pressed: the `dsp' line is the instrument's, so each track on it
       now plays the new graph. */
    for (size_t c = 0; ok && c < doc_.chains.size(); c++)
    {
        bool plays = false;

        for (size_t k = 0; k < doc_.chains[c].sinks.size(); k++)
            if (doc_.chains[c].sinks[k].instrument == inst &&
                doc_.chains[c].sinks[k].chanarg.empty())
                plays = true;

        if (!plays)
            continue;

        for (size_t s = 0; ok && s < doc_.chains[c].stages.size(); s++)
            if (doc_.chains[c].stages[s].plugin == "grid" &&
                doc_.chains[c].stages[s].category == "gen")
                ok = fitRows(c, s, readsNote, why);
    }

    if (!ok)
    {
        std::ofstream out(workPath_.c_str(),
                          std::ios::trunc | std::ios::binary);

        out << before;
        return false;
    }

    scheduleReload(true);

    return true;
}

/* A kick, a hat, a clap ignores the note it is sent, so a ladder over one
 * is rows that all make the same sound: one row is all it has to say. A
 * graph that is played at pitch gets a ladder back if it was down to one,
 * and keeps the one it has otherwise.
 *
 * The height it has is the live grid's, not the file's line: a grid with
 * no `rows' line is the plugin's default, and one bound to a knob is
 * whatever the knob says. A binding is left alone -- it is somebody's
 * decision about the grid, and a choice of instrument is not a licence to
 * cut it.
 */
bool
Composer::fitRows (size_t ci, size_t si, bool readsNote, std::string &why)
{
    const thcGenEdit::Stage &stage = doc_.chains[ci].stages[si];

    for (size_t p = 0; p < stage.params.size(); p++)
        if (stage.params[p].name == "rows" &&
            stage.params[p].valueText.find_first_not_of("0123456789") !=
                std::string::npos)
            return true;

    thcStage *s = liveStage(ci, si);
    const int rowsAt = s != NULL ? s->plugin->paramIndex("rows") : -1;

    if (rowsAt < 0)
        return true;

    const int have = (int)(s->params.get(rowsAt) + 0.5);
    const int want = !readsNote ? 1 : have <= 1 ? 6 : have;

    if (want == have)
        return true;

    /* The pattern goes with it, reshaped here: a grid nobody has drawn on
       reads its cells from the top when it is reloaded at another height,
       which kept the empty row above a bass line and dropped the root the
       line was written on. From the bottom instead, as the grid itself
       does for one somebody has drawn on. */
    const int cellsAt = s->plugin->paramIndex("cells");
    const std::string cells =
        cellsAt >= 0 ? s->plugin->capture(s->state, cellsAt) : std::string();
    const std::string name = doc_.chains[ci].name;

    if (!editOk(thcGenEdit::setParam(workPath_, name, (int)si, "rows",
                                     std::to_string(want), why), why))
        return false;

    return cells.empty() ||
           editOk(thcGenEdit::setParam(workPath_, name, (int)si, "cells",
                                       "\"" + bottomRows(cells, want) + "\"",
                                       why), why);
}

void
Composer::captureGrids (void)
{
    for (size_t ci = 0; ci < doc_.chains.size(); ci++)
        for (size_t si = 0; si < doc_.chains[ci].stages.size(); si++)
            if (doc_.chains[ci].stages[si].plugin == "grid" &&
                doc_.chains[ci].stages[si].category == "gen")
                captureStage(ci, si, false);
}

/* An instrument arriving on a channel, in the application's terms.
 *
 * gthPatchManager rather than thSynth::loadTree, because in this program
 * a loaded graph is also a patch tab with a name on it, an arg panel,
 * and a dirty flag that decides whether Save is worth pressing. A
 * channel the composer filled behind the patch manager's back would
 * show as empty in the main window while sound came out of it, which is
 * the kind of disagreement that costs an afternoon.
 *
 * newPatch, not loadPatch: what the piece names is a .dsp, and the
 * values on top of it are the piece's own -- so this is the Patch
 * Selector's "new patch from this DSP" path, and the chanargs that
 * follow are applied by the scheduler through the same call a slider
 * makes. It marks the channel dirty, which is true: what is on it came
 * from a .gen and there is no .patch holding it.
 */
/* Is this the same instrument, in every respect the file can state?
 *
 * Not just the same .dsp. applyInstrument only writes the values the
 * block lists, so an instrument that keeps its graph and *drops* a line
 * would otherwise keep the value that line used to set -- delete
 * `a = 900 ms' from the pad and the attack stays at 900ms until the
 * program is restarted, which is exactly the kind of stale number that
 * costs an hour. Anything different about the declaration means rebuild.
 */
static bool
sameInstrument (const thcInstrument &a, const thcInstrument &b)
{
    if (a.name != b.name || a.dsp != b.dsp || a.channel != b.channel ||
        a.args.size() != b.args.size())
        return false;

    for (size_t i = 0; i < a.args.size(); i++)
        if (a.args[i].name != b.args[i].name ||
            a.args[i].value != b.args[i].value ||
            a.args[i].units != b.args[i].units ||
            a.args[i].knob != b.args[i].knob)
            return false;

    return true;
}

bool
Composer::loadInstrument (const thcInstrument &inst, std::string &why)
{
    gthPatchManager *pm = gthPatchManager::instance();

    if (pm == NULL)
    {
        why = "there is nowhere to load it";
        return false;
    }

    /* Already ours, still this graph, still declared the same way:
     * leave it alone.
     *
     * Every structural edit reloads the piece, and reloading it used to
     * mean newPatch on every instrument -- which drops the channel,
     * re-parses the .dsp and swaps a new tree in. So renaming a knob's
     * label cut every sounding voice on the pad. The values are written
     * again either way (a structural reload rewinds to zero, so the
     * file's numbers are the right ones to be at), but the graph does
     * not have to be rebuilt for that. */
    bool keep = stillOurs(inst.channel);

    /* Ours, but is it still this *graph*?
     *
       A swap replaces the .dsp on a channel the piece owns without
       touching either the generation (it is still our load) or the
       declaration (unchanged in the file), so both of the tests here
       said keep and a reload left the swapped-in graph up while
       believing the declared one was there -- whereupon applying the
       declaration's values fails on a chanarg the wrong .dsp does not
       have and the whole file refuses to load. Recording what actually
       went onto the channel is the only thing that can tell those two
       apart. */
    if (keep)
    {
        keep = false;

        for (size_t i = 0; i < prevOwned_.size(); i++)
            if (prevOwned_[i].channel == inst.channel &&
                prevOwned_[i].dsp == inst.dsp)
                keep = true;
    }

    if (keep)
    {
        /* Ours, and still the same instrument the file declares. Any
           difference at all -- a value changed, a line dropped -- means
           rebuild, because applying an instrument only writes the values
           its block lists and a dropped line would otherwise keep the
           value it used to set. */
        keep = false;

        for (size_t i = 0; i < prevInstruments_.size(); i++)
            if (sameInstrument(prevInstruments_[i], inst))
                keep = true;
    }

    if (!keep && !pm->newPatch(inst.dsp, inst.channel))
    {
        why = "'" + inst.dsp + "' did not load";
        return false;
    }

    gthPatchManager::PatchFile *have = pm->getPatch(inst.channel);
    Owned o;

    o.channel = inst.channel;
    o.generation = have != NULL ? have->generation : 0;
    o.dsp = inst.dsp;

    /* One entry per channel. This is no longer called once per load: a
       swap calls it, and so does every rewind of a piece a swap has
       touched, and a list that only grew would carry a stale generation
       for the same channel into releaseInstruments. */
    for (size_t i = 0; i < ownedChannels_.size(); i++)
        if (ownedChannels_[i].channel == inst.channel)
        {
            ownedChannels_[i] = o;
            return true;
        }

    ownedChannels_.push_back(o);

    return true;
}

bool
Composer::loadEffect (int channel, const std::string &effect, int side,
                            std::string &why)
{
    gthPatchManager *pm = gthPatchManager::instance();

    if (pm == NULL)
    {
        why = "there is nowhere to load it";
        return false;
    }

    /* setEffect answers both halves of this: it declines to rebuild an
       effect the channel already has under the same name, and it takes one
       off when the name is empty -- which is what a piece that dropped its
       `effect' clause means for a channel this loaded one onto. */
    if (pm->setEffect(channel, effect, side))
        return true;

    why = effect.empty()
          ? "the effect could not be taken off channel " +
            std::to_string(channel + 1)
          : "'" + effect + "' did not load as an effect";

    return false;
}

bool
Composer::unloadInstrument (const thcInstrument &inst)
{
    gthPatchManager *pm = gthPatchManager::instance();

    /* Still ours if it would not go. unloadPatch fails when the audio
       thread could not be told to drop the channel, and the channel is
       then still loaded and still sounding -- so forgetting it here
       would leave a graph playing that this no longer believes
       it owns and will never try to unload again. */
    if (pm == NULL || !pm->unloadPatch(inst.channel))
        return false;

    for (size_t i = 0; i < ownedChannels_.size(); i++)
        if (ownedChannels_[i].channel == inst.channel)
        {
            ownedChannels_.erase(ownedChannels_.begin() + i);
            break;
        }

    return true;
}

bool
Composer::stillOurs (int channel) const
{
    gthPatchManager *pm = gthPatchManager::instance();
    gthPatchManager::PatchFile *have =
        pm != NULL ? pm->getPatch(channel) : NULL;

    if (have == NULL)
        return false;

    for (size_t i = 0; i < prevOwned_.size(); i++)
        if (prevOwned_[i].channel == channel &&
            prevOwned_[i].generation == have->generation)
            return true;

    return false;
}

bool
Composer::channelTaken (int channel)
{
    gthPatchManager *pm = gthPatchManager::instance();

    if (pm == NULL || !pm->isLoaded(channel))
        return false;

    /* Loaded by this piece a moment ago, and not touched since -- so it
       is the piece's to have back, and the instrument that was on it
       stays on it. Anything else on that channel is somebody's, and
       taking it would replace an instrument they chose. */
    return !stillOurs(channel);
}

/* Give back the channels this filled for the piece that was open
 * a moment ago and the new one no longer wants.
 *
 * A patch outlives the file that asked for it -- that is why the
 * scheduler does not do this -- but a piece that drops an instrument and
 * leaves its channel loaded leaves a patch tab nothing plays, and the
 * next piece to allocate that number inherits somebody else's arg
 * values. Only channels in ownedChannels_ are candidates, so a patch
 * loaded by hand is never taken away.
 */
void
Composer::releaseInstruments (void)
{
    gthPatchManager *pm = gthPatchManager::instance();

    if (pm != NULL)
        for (size_t i = 0; i < prevOwned_.size(); i++)
        {
            const int channel = prevOwned_[i].channel;
            bool wanted = false;

            for (size_t k = 0; k < ownedChannels_.size(); k++)
                if (ownedChannels_[k].channel == channel)
                    wanted = true;

            if (wanted)
                continue;

            /* Only if what is on it is still the exact patch this
               put there. Somebody who loaded their own onto one of the
               piece's channels has made it theirs -- and their patch may
               well be built on the same .dsp, which is why this is a
               generation and not a filename. */
            if (!stillOurs(channel))
                continue;

            /* And keep it if it would not go: a channel the audio thread
               could not be told to drop is still loaded and still ours,
               so it stays on the list for the next parse to try again
               rather than becoming a graph nothing can reach. */
            if (!pm->unloadPatch(channel))
                ownedChannels_.push_back(prevOwned_[i]);
        }

    /* The parse is over; nothing may consult either again until the
       next one sets them. */
    prevOwned_.clear();
    prevInstruments_.clear();
}

void
Composer::parseWork (void)
{
    /* Every ParamInfo behind an open popover is about to be replaced, so
       the popover goes with them. Not "hidden": the widgets in it hold
       a plugin pointer and a param index, and the next reload is where
       both stop meaning what they meant. */
    closeParams();

    /* What the piece about to be replaced filled in. The loader refills
       ownedChannels_ as it brings each instrument up, and consults
       prevOwned_ on the way to decide which channels are free and which
       instruments can stay where they are; releaseInstruments unloads
       the difference and clears it again. */
    prevOwned_.clear();
    prevOwned_.swap(ownedChannels_);
    prevInstruments_ = sched_->instruments();

    /* The mute and the solos are the scheduler's and a load clears them.
       Taken by name now, to put back below. Not from a scheduler a
       failed load left empty: what was taken before that failure is
       still what is wanted. */
    if (mixForgotten_)
    {
        mutedNames_.clear();
        soloedNames_.clear();
    }
    else if (sched_->chainCount() > 0)
    {
        mutedNames_.clear();
        soloedNames_.clear();

        for (size_t ci = 0; ci < sched_->chainCount(); ci++)
        {
            const thcChain *c = sched_->chain(ci);
            const auto renamed = mixRenamed_.find(c->name);
            const std::string &name =
                renamed != mixRenamed_.end() ? renamed->second : c->name;

            if (c->muted)
                mutedNames_.push_back(name);

            if (c->soloed)
                soloedNames_.push_back(name);
        }
    }

    mixForgotten_ = false;
    mixRenamed_.clear();

    thcGenLoader loader(composers_);

    if (!loader.load(workPath_, sched_))
    {
        const std::vector<std::string> &errs = loader.errors();

        for (size_t i = 0; i < errs.size(); i++)
            fprintf(stderr, "%s\n", errs[i].c_str());

        pieceLabel_ = errs.empty() ? "load failed" : errs[0];
    }
    else
    {
        const std::vector<std::string> &warnings = loader.warnings();

        for (size_t i = 0; i < warnings.size(); i++)
            fprintf(stderr, "%s\n", warnings[i].c_str());

        std::string name = loader.pieceName();

        if (name.empty())
            name = genPath_.empty() ? "untitled"
                : std::filesystem::path(genPath_).filename().string();

        pieceLabel_ = name;

        if (loader.hasSeed())
        {
            char buf[32];

            snprintf(buf, sizeof(buf), " — seed %u", loader.seed());
            pieceLabel_ += buf;
        }

        /* Without the location, which names the working file -- a
           temporary copy of the piece that nobody has seen and that the
           label has no room for anyway. stderr above has the whole
           line, and that is where a line number is worth having. */
        if (!warnings.empty())
        {
            const std::string mark = ": warning: ";
            const size_t at = warnings[0].find(mark);

            pieceLabel_ += " — " +
                (at == std::string::npos ? warnings[0]
                                         : warnings[0].substr(at +
                                                              mark.size()));
        }
    }

    releaseInstruments();

    for (size_t ci = 0; ci < sched_->chainCount(); ci++)
    {
        const std::string &name = sched_->chain(ci)->name;

        if (std::find(mutedNames_.begin(), mutedNames_.end(), name) !=
            mutedNames_.end())
            sched_->setMuted(ci, true);

        if (std::find(soloedNames_.begin(), soloedNames_.end(), name) !=
            soloedNames_.end())
            sched_->setSoloed(ci, true);
    }

    std::string why;

    if (thcGenEdit::describe(workPath_, doc_, why) != thcGenEdit::OK)
        doc_ = thcGenEdit::Doc();

    /* What each grid plays as loaded, for captureStage to measure a
       later capture against. */
    baseline_.clear();

    for (size_t ci = 0; ci < doc_.chains.size(); ci++)
        for (size_t si = 0; si < doc_.chains[ci].stages.size(); si++)
        {
            if (doc_.chains[ci].stages[si].plugin != "grid")
                continue;

            thcStage *s = liveStage(ci, si);

            if (s == NULL || !s->plugin->hasCapture())
                continue;

            for (int pi = 0; pi < s->plugin->paramCount(); pi++)
            {
                const thcPlugin::ParamInfo *info = s->plugin->paramInfo(pi);

                if (info == NULL || info->type != THC_PARAM_STRING)
                    continue;

                const std::string text = s->plugin->capture(s->state, pi);

                if (!text.empty())
                    baseline_[std::to_string(ci) + "." + std::to_string(si) +
                              "." + info->name] = text;
            }
        }

    tempoGuard_ = true;
    tempoVal_->set_value(sched_->tempo());
    tempoGuard_ = false;

    /* The tempo scales beat-valued durations and nothing else, so on a
       piece written entirely in seconds it is a control that does
       nothing -- which was most of them, with no way to tell. Worse, it
       is an *edit*: nudging it wrote a `tempo' line into a file that had
       never had one and marked the piece dirty, for no audible reason.
       Offered only where it means something, and saying so where it does
       not. */
    {
        const bool live = sched_->usesBeats();

        tempoBtn_->set_sensitive(live);
        tempoLbl_->set_sensitive(live);

        tempoBtn_->set_tooltip_text(live
            ? "Beats per minute. This piece writes durations in beats, so "
              "everything moves together."
            : "This piece writes every duration in seconds, which the "
              "tempo does not scale. Write a duration as `4 beats' to "
              "put a stage on the clock.");
    }

    canvas_->SetPiece(&doc_, sched_);
    seq_.setPiece(&doc_, sched_);
    rebuildEditor();
    updateTransportButtons();
}

void
Composer::structuralReload (void)
{
    scheduleReload(true);
}

void
Composer::scheduleReload (bool markDirty)
{
    if (reloadPending_)
        return;

    reloadPending_ = true;
    reloadMarksDirty_ = markDirty;

    /* Stored: a reload queued at idle and a composer destroyed before
       the loop comes round again is a callback into freed memory that
       then reloads the piece through a deleted scheduler. The
       reloadPending_ flag makes sure there is only ever one. */
    reloadIdle_ = Glib::signal_idle().connect(
        [this, markDirty]
        {
            reloadPending_ = false;

            bool wasRunning = sched_->running();

            setDirty(markDirty);
            parseWork();

            /* A structural edit rewinds: the shape of the piece
               changed, and "the same piece from the top" is the only
               honest reading of the determinism story. Value edits
               never come through here. */
            sched_->reset();

            if (wasRunning)
                sched_->start();

            updateTransportButtons();

            return false;
        });
}

bool
Composer::editOk (thcGenEdit::Result r, const std::string &why)
{
    if (r == thcGenEdit::OK)
        return true;

    status_->set_text(why.empty() ? thcGenEdit::resultText(r) : why);

    return false;
}

/* ---- transport & top-bar actions -------------------------------------- */

void
Composer::onPlay (void)
{
    sched_->start();
    updateTransportButtons();
}

void
Composer::onPause (void)
{
    sched_->halt();
    updateTransportButtons();
}

void
Composer::onRewind (void)
{
    /* Back to the top, and nothing from where it was left still ringing
       into the start. */
    sched_->halt();
    sched_->reset();
    updateTransportButtons();
}

void
Composer::onReload (void)
{
    /* Revert: recopy the source over the work file and reload. Through
       the idle path like everything else, so the editor panel is not
       torn down under whatever asked for this.

       With no source at all it retries discovery instead -- a piece
       may have appeared (or THINK_GEN_PATH been pointed somewhere
       real) since startup, and restarting the program is not a reload
       button. */
    if (genPath_.empty() && which_ == SEQUENCE)
    {
        /* The sequence has no file of its own until it is saved, so
           Revert takes it back to what it started from. */
        if (ensureWork())
            startDocument(SEQUENCE);
    }
    else
    {
        if (genPath_.empty())
            genPath_ = startingFile(PIECE);

        if (!genPath_.empty() && ensureWork())
            copyOver(genPath_, workPath_);
    }

    scheduleReload(false);
}

void
Composer::onTempo (void)
{
    if (tempoGuard_)
        return;

    /* Belt for the insensitive spinner above: a control that cannot be
       reached should also do nothing if it is, because "cannot happen"
       and "does nothing when it does" are one line apart and only one of
       them survives a refactor. */
    if (!sched_->usesBeats())
        return;

    sched_->setTempo(tempoVal_->get_value());

    /* The spin is live *and* it is an edit: a piece whose tempo you
       changed and saved should come back at that tempo. */
    if (!workPath_.empty())
    {
        std::string why;

        if (editOk(thcGenEdit::setTempo(workPath_, tempoVal_->get_value(),
                                        why), why))
            setDirty(true);
    }
}

void
Composer::onSave (void)
{
    if (genPath_.empty())
    {
        onSaveAs();
        return;
    }

    if (copyOver(workPath_, genPath_))
        setDirty(false);
    else
        status_->set_text("could not write " + genPath_);
}

void
Composer::onSaveAs (void)
{
    Gtk::FileChooserDialog *dialog = new Gtk::FileChooserDialog(
        "Save piece as", Gtk::FileChooser::Action::SAVE);

    if (Gtk::Window *win = windowOf(&canvasScroll_))
        dialog->set_transient_for(*win);

    dialog->add_button("_Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("_Save", Gtk::ResponseType::OK);
    dialog->set_modal(true);
    dialog->set_current_name(doc_.name.empty() ? "untitled.gen"
                                               : doc_.name + ".gen");

    dialog->signal_response().connect(
        sigc::bind(sigc::mem_fun(*this, &Composer::onSaveAsResponse),
                   dialog));

    dialog->set_visible(true);
}

/* The piece as it stands -- the working file, edits and all -- composed
 * offline into a .mid (thcMidiExport), with the seed it is playing with and
 * its chains' mutes and solos. As long as its arrangement where it ends;
 * otherwise a length chosen in the dialog. */
void
Composer::onExportMidi (void)
{
    if (sched_ == NULL || workPath_.empty() || sched_->chainCount() == 0)
    {
        status_->set_text("MIDI export: there is no piece to export");
        return;
    }

    const bool ends = sched_->endsAfterSections() &&
                      sched_->sectionsLength() > 0;
    Gtk::FileChooserDialog *dialog = new Gtk::FileChooserDialog(
        "Export piece as MIDI", Gtk::FileChooser::Action::SAVE);

    if (Gtk::Window *win = windowOf(&canvasScroll_))
        dialog->set_transient_for(*win);

    dialog->add_button("_Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("_Export", Gtk::ResponseType::OK);
    dialog->set_modal(true);
    /* The piece's name as a file name: a slash in it would be a
       directory. */
    std::string stem = doc_.name.empty() ? "untitled" : doc_.name;

    std::replace(stem.begin(), stem.end(), '/', '_');
    std::replace(stem.begin(), stem.end(), '\\', '_');
    dialog->set_current_name(stem + ".mid");

    if (!ends)
        dialog->add_choice("length", "Length",
                           { "60", "120", "300", "600" },
                           { "1 minute", "2 minutes", "5 minutes",
                             "10 minutes" });

    if (!ends)
        dialog->set_choice("length", "120");

    dialog->signal_response().connect(
        sigc::bind(sigc::mem_fun(*this, &Composer::onExportMidiResponse),
                   dialog, ends));

    dialog->set_visible(true);
}

void
Composer::onExportMidiResponse (int response, Gtk::FileChooserDialog *dialog,
                                bool ends)
{
    std::string path;
    thcMidiExport::Options options;

    if (response == (int)Gtk::ResponseType::OK)
    {
        path = chosenPath(*dialog);
        if (!ends)
            options.seconds = atof(dialog->get_choice("length").c_str());
    }

    closeDialog(dialog);

    if (path.empty())
        return;

    {
        std::string ext = std::filesystem::path(path).extension().string();

        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext != ".mid" && ext != ".midi")
            path += ".mid";
    }

    {
        const double seconds = options.seconds;

        options = exportOptions();
        options.seconds = seconds;
    }

    confirmOverwrite(windowOf(&canvasScroll_), path,
        [this, path, options]
        {
            std::string why;
            double length = 0;

            if (!exportMidi(path, options, why, &length))
                status_->set_text("MIDI export: " + why);
            else
            {
                char buf[64];

                snprintf(buf, sizeof buf, "%.0f s", length);
                status_->set_text("exported " + std::string(buf) + " to " +
                                  path);
            }
        });
}

/* What is playing, as an export takes it: its seed, its name, and its
   chains' mutes and solos. */
thcMidiExport::Options
Composer::exportOptions (void)
{
    thcMidiExport::Options options;

    options.seed = sched_->masterSeed();
    options.name = !doc_.name.empty() ? doc_.name
                 : !genPath_.empty()
                     ? std::filesystem::path(genPath_).stem().string()
                     : "untitled";

    for (size_t c = 0; c < sched_->chainCount(); c++)
    {
        const thcChain *chain = sched_->chain(c);

        if (chain->muted)
            options.muted.push_back(chain->name);

        if (chain->soloed)
            options.soloed.push_back(chain->name);
    }

    for (const thcInstrument &inst : sched_->instruments())
        if (inst.channel >= 0)
            options.channels.push_back(inst.channel);

    return options;
}

bool
Composer::exportMidi (const std::string &path,
                      const thcMidiExport::Options &options,
                      std::string &why, double *length)
{
    /* A synth of the export's own, silent: the piece's graphs are loaded
       on it so a chanarg's range can be read, and the one playing is not
       touched. */
    thPluginManager *pm = synth_ ? synth_->getPluginManager() : NULL;
    thSynth synth(pm ? pm->pluginPath() : std::string(PLUGIN_PATH),
                  TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    std::vector<uint8_t> bytes;

    synth.setSilent(true);

    if (!thcMidiExport::render(composers_, &synth, workPath_, options, bytes,
                               why, length))
    {
        /* The working copy is a temporary file nobody chose; an error
           names the piece instead. */
        for (size_t at; (at = why.find(workPath_)) != std::string::npos; )
            why.replace(at, workPath_.size(),
                        genPath_.empty() ? options.name : genPath_);

        return false;
    }

    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);

    f.write((const char *)bytes.data(), (std::streamsize)bytes.size());
    f.close();

    if (!f)
    {
        why = "could not write " + path;
        return false;
    }

    return true;
}

void
Composer::onSaveAsResponse (int response, Gtk::FileChooserDialog *dialog)
{
    std::string path;

    if (response == (int)Gtk::ResponseType::OK)
        path = chosenPath(*dialog);

    closeDialog(dialog);

    if (path.empty())
        return;

    if (path.size() < 4 || path.compare(path.size() - 4, 4, ".gen") != 0)
        path += ".gen";

    /* GTK4's Save chooser hands back the path and says nothing about a
       file already being there -- the question is asked here, the same
       way every other save in the app asks it. */
    confirmOverwrite(windowOf(&canvasScroll_), path,
        [this, path]
        {
            if (copyOver(workPath_, path))
            {
                genPath_ = path;
                setDirty(false);
                updateTransportButtons();
            }
            else
                status_->set_text("could not write " + path);
        });
}

void
Composer::confirmDiscard (const sigc::slot<void ()> &done)
{
    if (!dirty_)
    {
        done();
        return;
    }

    Gtk::MessageDialog *dlg = new Gtk::MessageDialog(
        "Throw away unsaved edits?", false,
        Gtk::MessageType::QUESTION, Gtk::ButtonsType::YES_NO, true);

    if (Gtk::Window *win = windowOf(&canvasScroll_))
        dlg->set_transient_for(*win);

    dlg->set_secondary_text("The piece has edits that were never saved; "
                            "opening another one loses them.");

    dlg->signal_response().connect(
        [dlg, done](int response)
        {
            if (response == (int)Gtk::ResponseType::YES)
                done();

            closeDialog(dlg);
        });

    dlg->set_visible(true);
}

void
Composer::onOpen (void)
{
    confirmDiscard(sigc::mem_fun(*this, &Composer::onOpenConfirmed));
}

/* The rows the piece browser shows, given what is in its filter box. The
 * rule is GenCatalog's, so what the list offers can be held still without a
 * display; this turns the answer into rows. */
static std::vector<BrowserGroup> genRows (GenCatalog *catalog,
                                          const std::string &needle)
{
    std::vector<BrowserGroup> out;

    for (size_t g = 0; g < catalog->groups().size(); g++)
    {
        const std::string &group = catalog->groups()[g];
        const std::vector<GenCatalog::Entry> &list =
            catalog->inGroup(group);

        BrowserGroup rows;

        rows.name = group;

        for (size_t i = 0; i < list.size(); i++)
        {
            if (!GenCatalog::matches(list[i], needle))
                continue;

            BrowserItem item;

            /* The path, because opening a piece copies a file: there is no
               search path to resolve a name against, the way there is for a
               .dsp a patch names. */
            item.file = list[i].path;
            item.name = list[i].name;
            item.desc = list[i].desc;
            item.note = "<tt>" + Glib::Markup::escape_text(list[i].file) +
                        "</tt>";

            if (!list[i].author.empty())
                item.note += "  <small>" +
                             Glib::Markup::escape_text(list[i].author) +
                             "</small>";

            rows.items.push_back(item);
        }

        if (!rows.items.empty())
            out.push_back(rows);
    }

    return out;
}

/* Open: the pieces by what they are, in the sections gen/README.md groups
 * them into -- which are in the files now, so this list and that index say
 * the same thing because they are the same fact.
 *
 * What this replaces is a file chooser over gen/: thirty-one filenames, no
 * filter and no descriptions, while every one of those files declares a title
 * and a paragraph saying what it teaches. Other File... is still there for a
 * piece that lives somewhere else. */
void
Composer::onOpenConfirmed (void)
{
    /* Where the pieces live: beside the one that is open, or in the gen/
       data directory the default piece came from. */
    const std::string folder = !genPath_.empty()
        ? std::filesystem::path(genPath_).parent_path().string()
        : thUtil::findDataDir("gen", "THINK_GEN_PATH", "");

    std::shared_ptr<GenCatalog> catalog = std::make_shared<GenCatalog>();

    catalog->scan(folder);

    /* A browser has to belong to a window, and the actions that open one
       are the window's: there is always one to belong to. */
    Gtk::Window *win = windowOf(&canvasScroll_);

    if (win == NULL)
        return;

    ItemBrowser *browser = new ItemBrowser(
        *win, "Open piece",
        [catalog](const std::string &needle)
        {
            return genRows(catalog.get(), needle);
        },
        folder, genPath_);

    browser->setEmptyNote("<i>No pieces in</i>\n<tt>" +
                          Glib::Markup::escape_text(folder) + "</tt>\n"
                          "<small>Set THINK_GEN_PATH, or use Other "
                          "File...</small>");

    browser->signal_chosen().connect(
        sigc::mem_fun(*this, &Composer::onOpenChosen));

    browser->present();
}

void
Composer::onOpenChosen (std::string path)
{
    if (path.empty())
        return;

    if (!ensureWork() || !copyOver(path, workPath_))
    {
        status_->set_text("could not read " + path);
        return;
    }

    genPath_ = path;
    forgetMix();
    scheduleReload(false);
}

void
Composer::onNew (void)
{
    confirmDiscard(
        [this]
        {
            genPath_.clear();
            forgetMix();

            if (!ensureWork())
                return;

            {
                std::ofstream out(workPath_.c_str(), std::ios::trunc);

                out << "name \"Untitled\";\n";
            }

            scheduleReload(true);
        });
}

void
Composer::setDirty (bool dirty)
{
    dirty_ = dirty;

    if (saveAct_)
        saveAct_->set_enabled(dirty_ || genPath_.empty());

    updateTransportButtons();
}

void
Composer::updateTransportButtons (void)
{
    const bool have = sched_ != NULL && sched_->chainCount() > 0;
    const bool running = sched_ != NULL && sched_->running();

    shownRunning_ = running;

    playBtn_->set_sensitive(have && !running);
    pauseBtn_->set_sensitive(have && running);
    rewindBtn_->set_sensitive(have);

    std::string text = pieceLabel_;

    if (dirty_)
        text += " (edited)";

    if (!have && composers_.empty())
        text = "no composer modules found in " + composerRoot_;
    else if (running)
        text += " — playing";

    /* An instrument meant for a device and not on one says so here, where
       it is seen at once; where to change it is the Piece Settings pane's
       MIDI out section. */
    if (sched_ != NULL)
    {
        std::vector<std::string> off;

        /* An instrument whose channel a swap gave to another is not
           off its device: it is not playing at all. */
        for (const thcInstrument &inst : sched_->instruments())
            if (!inst.midi.empty() && !sched_->playsOverMidi(inst.channel) &&
                sched_->holding(inst.channel) == inst.name)
                off.push_back(inst.name + ": " +
                              sched_->midiWhy(inst.channel));

        if (off.size() == 1)
            text += " — " + off[0];
        else if (off.size() > 1)
            text += " — " + std::to_string(off.size()) +
                    " MIDI instruments are not on their devices";

        if (!off.empty())
            text += " (Piece Settings, MIDI out)";
    }

    status_->set_text(text);
}

/* The file commands, as actions and a menu section for the host's menu,
 * and the transport for its title bar.
 *
 * The transport is on the bar because it is pressed constantly and a menu
 * is not for that. Everything pressed rarely -- the file commands and
 * Revert -- is in the menu. */
void
Composer::buildActions (void)
{
    Glib::RefPtr<Gio::SimpleActionGroup> acts = acts_ =
        Gio::SimpleActionGroup::create();

    /* Each wakes the composer first: before start() there is no piece for
       it to act on, and New done then was undone by the load that
       followed. */
    acts->add_action("new",
                     [this] { wake(); fileCommand_.emit(); onNew(); });
    acts->add_action("open",
                     [this] { wake(); fileCommand_.emit(); onOpen(); });
    saveAct_ = acts->add_action("save",
                                sigc::mem_fun(*this, &Composer::onSave));
    acts->add_action("saveas", [this] { wake(); onSaveAs(); });
    acts->add_action("exportmidi", [this] { wake(); onExportMidi(); });
    acts->add_action("revert", [this] { wake(); onReload(); });

    /* Nothing to save until there is a piece; the load says whether there
       is anything then. */
    saveAct_->set_enabled(false);

    /* The roll's notes in the hue of the chain that made them, and the
       chains' names on the canvas striped to match. Held here as well as
       in the action, since the roll is not made until start(). */
    Glib::RefPtr<Gio::SimpleAction> byChain =
        acts->add_action_bool("roll-by-chain", false);

    byChain->signal_activate().connect(
        [this, byChain](const Glib::VariantBase &)
        {
            bool on = false;

            byChain->get_state(on);
            on = !on;
            byChain->change_state(on);
            rollByChain_ = on;

            if (roll_ != NULL)
                roll_->setColorByChain(on);

            canvas_->setChainHues(on);
        });

    menu_ = Gio::Menu::create();
    menu_->append("Ne_w Piece", "composer.new");
    menu_->append("_Open Piece...", "composer.open");
    menu_->append("Sa_ve Piece", "composer.save");
    menu_->append("Save Piece _As...", "composer.saveas");
    menu_->append("Export _MIDI...", "composer.exportmidi");
    menu_->append("Revert Pie_ce", "composer.revert");

    acts->add_action("collapse-chains",
                     [this] { canvas_->setAllCollapsed(true); });
    acts->add_action("expand-chains",
                     [this] { canvas_->setAllCollapsed(false); });

    Glib::RefPtr<Gio::Menu> roll = Gio::Menu::create();

    roll->append("Co_llapse Chains", "composer.collapse-chains");
    roll->append("E_xpand Chains", "composer.expand-chains");
    roll->append("Color Notes by C_hain", "composer.roll-by-chain");
    menu_->append_section(roll);

    transport_ = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 4));
    transport_->append(*playBtn_);
    transport_->append(*pauseBtn_);
    transport_->append(*rewindBtn_);
    transport_->append(*tempoLbl_);
    transport_->append(*tempoBtn_);
    transport_->append(*kbdBtn_);
}

/* ---- the playing side ------------------------------------------------- */

/* One slow clock repaints the canvas so the inline composer_draws stay
 * live; 50ms is plenty for a euclid ring, and the piano roll keeps its
 * own frame clock. */
bool
Composer::onDrawTimer (void)
{
    /* The columns follow the view's width. Asked here rather than on a
       resize, because the drawing area is sized to the drawing and does
       not hear the scroller around it get narrower. */
    if (canvas_ != NULL && canvasShown_)
    {
        canvas_->fitColumns();
        canvas_->queue_draw();
    }

    if (seqShown_)
        seq_.tick();

    /* The transport can stop without anyone pressing Pause: a piece
       whose arrangement ends (`section end;') stops itself when its last
       section is over. The transport has to say so, and this is the only
       thing here that runs on its own. */
    if (sched_->running() != shownRunning_)
        updateTransportButtons();

    return true;
}

void
Composer::injectOn (int chan, float note, float veloc)
{
    if (sched_ == NULL)
        return;

    thcEvent ev = {};

    ev.type = THC_EV_NOTE;
    ev.u.note.level = 1;
    ev.at = sched_->now();
    ev.channel = chan;
    ev.u.note.note = (int)note;
    ev.u.note.velocity = (int)veloc;
    ev.u.note.duration = 0;

    sched_->injectMidiEvent(ev);
}

void
Composer::injectOff (int chan, float note)
{
    if (sched_ == NULL)
        return;

    thcEvent ev = {};

    ev.type = THC_EV_NOTEOFF;
    ev.at = sched_->now();
    ev.channel = chan;
    ev.u.note.note = (int)note;

    sched_->injectMidiEvent(ev);
}

void
Composer::playedOn (int chan, float note, float veloc)
{
    if (roll_ != NULL)
        roll_->keyPlayed(sched_->now(), chan, (int)note, (int)veloc, true);
}

void
Composer::playedOff (int chan, float note)
{
    if (roll_ != NULL)
        roll_->keyPlayed(sched_->now(), chan, (int)note, 0, false);
}

void
Composer::playedCleared (void)
{
    if (roll_ != NULL)
        roll_->endPlayed(sched_->now());
}

/* The on-screen keyboard as a performance input, if wished for: the
 * same two handlers, fed from the pair the on-screen keyboard emits. A
 * toggle rather than always-on because the keyboard is also the tool
 * for auditioning patches, and auditioning through an arpeggiator you
 * forgot about is a confusing five minutes. */
void
Composer::onKbdToggle (void)
{
    /* Guarded as well as fixed. This is a signal handler on a member
       pointer, so it can be reached from anywhere the toolkit decides to
       emit `toggled' -- including from set_active() during teardown --
       and a crash is a poor way to find out. */
    if (kbdBtn_ == NULL)
        return;

    if (kbdBtn_->get_active())
    {
        kbdOnConn_ = m_sigKbdNoteOn.connect(
            sigc::mem_fun(*this, &Composer::injectOn));
        kbdOffConn_ = m_sigKbdNoteOff.connect(
            sigc::mem_fun(*this, &Composer::injectOff));
    }
    else
    {
        kbdOnConn_.disconnect();
        kbdOffConn_.disconnect();
    }
}

/* A cell of the arrangement: written into the work file, and poked into
 * the running piece, which goes on playing -- an arrangement edit is a
 * value edit, like a param, and not a reason to rewind. */
void
Composer::onCanvasSectionLevel (size_t section, size_t chain, double level)
{
    if (section >= sched_->sections().size() ||
        chain >= doc_.chains.size())
        return;

    const std::string name = sched_->sections()[section].name;
    const std::string chainName = doc_.chains[chain].name;
    std::string why;

    if (!editOk(thcGenEdit::setSectionLevel(workPath_, name, chainName,
                                            level, why), why))
        return;

    sched_->setSectionLevel(section, chainName, level);
    setDirty(true);
    canvas_->queue_draw();
}

/* A chain frozen: what it played in the last two bars, as a new chain
 * beside it with a grid that plays it back, through the same instrument
 * or channel (thcFreeze.h). The original is muted rather than removed, so
 * nothing is lost that one M press will not bring back. */
void
Composer::onCanvasFreeze (size_t chain)
{
    if (chain >= doc_.chains.size())
        return;

    const thcGenEdit::Chain &c = doc_.chains[chain];
    thcFreeze::Params params;
    std::string why;

    if (!thcFreeze::fromChain(*sched_, chain, 2, params, why))
    {
        status_->set_text(c.name + ": " + why);
        return;
    }

    std::vector<std::string> names;

    for (const thcGenEdit::Chain &other : doc_.chains)
        names.push_back(other.name);

    const std::string name = thcFreeze::frozenName(c.name, names);

    /* All or nothing: a refusal part way through the arrangement would
       leave a frozen chain that plays where the original does not. */
    const std::string before = readText(workPath_);

    if (!editOk(thcFreeze::write(workPath_, c, *sched_, name, params, why),
                why))
    {
        std::ofstream out(workPath_.c_str(),
                          std::ios::trunc | std::ios::binary);

        out << before;
        return;
    }

    /* Kept by name across the reload the new chain needs. */
    sched_->setMuted(chain, true);
    structuralReload();
}

/* Another document: its chains are not these, whatever they are called. */
void
Composer::forgetMix (void)
{
    mutedNames_.clear();
    soloedNames_.clear();
    mixRenamed_.clear();
    mixForgotten_ = true;
}

/* A chain's M or S on the canvas. Live, like the Selection pane's mute
 * check, and not written to the file. The pane is rebuilt when it is
 * showing a chain, so its check follows the button. */
void
Composer::onCanvasMute (size_t chain, bool on)
{
    sched_->setMuted(chain, on);
    canvas_->queue_draw();

    if (canvas_->selection().kind == ComposerCanvas::Selection::CHAIN)
        rebuildSelection();
}

void
Composer::onCanvasSolo (size_t chain, bool on)
{
    sched_->setSoloed(chain, on);
    canvas_->queue_draw();
}

void
Composer::onCanvasSelection (const ComposerCanvas::Selection &sel)
{
    rebuildSelection();

    /* Ask for Selection, whether or not it is in view -- a stage picked
       is a stage to edit, and nothing else says where that is done -- but
       not for a click that deselected: clearing the canvas and being
       thrown into an empty pane reads as the window losing its place. */
    if (sel.kind != ComposerCanvas::Selection::NONE)
        showSelection_.emit();
}

/* A knob, selected on the canvas: its shape, and what it drives.
 *
 * The list is the wires in words, and it is where they are cut. Binding
 * is a drag onto a stage; unbinding cannot be, because there is nothing
 * to drag a wire *off* onto -- so it is a button here, next to the param
 * it releases. The param keeps the knob's current value when the wire
 * goes, which is the only answer that does not change what is playing:
 * removeKnob makes the same promise for the same reason. */
void
Composer::buildKnobSelection (size_t ki)
{
    const thcGenEdit::Knob &k = doc_.knobs[ki];
    const std::string name = k.name;

    Gtk::Label *head = manage(new Gtk::Label());

    head->set_markup("<b>@" + Glib::Markup::escape_text(name) + "</b>");
    head->set_xalign(0);
    selBox_->append(*head);

    Gtk::Grid *grid = manage(new Gtk::Grid());

    grid->set_column_spacing(6);
    grid->set_row_spacing(4);

    Gtk::SpinButton *minSpin = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(k.min, -100000, 100000, 0.01), 0, 3));
    Gtk::SpinButton *maxSpin = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(k.max, -100000, 100000, 0.01), 0, 3));
    Gtk::Entry *lblEntry = manage(new Gtk::Entry());

    lblEntry->set_text(k.label);
    lblEntry->set_placeholder_text("label");

    auto applyMeta = [this, name, minSpin, maxSpin, lblEntry]
    {
        std::string why;

        if (editOk(thcGenEdit::setKnobMeta(workPath_, name,
                minSpin->get_value(), maxSpin->get_value(),
                lblEntry->get_text(), why), why))
            structuralReload();
    };

    minSpin->signal_value_changed().connect(applyMeta);
    maxSpin->signal_value_changed().connect(applyMeta);
    lblEntry->signal_activate().connect(applyMeta);

    static const char *heads[] = { "lowest", "highest", "shown as" };

    for (int c = 0; c < 3; c++)
    {
        Gtk::Label *h = manage(new Gtk::Label(heads[c]));

        h->set_xalign(0);
        h->set_sensitive(false);
        grid->attach(*h, c, 0);
    }

    grid->attach(*minSpin, 0, 1);
    grid->attach(*maxSpin, 1, 1);
    grid->attach(*lblEntry, 2, 1);
    selBox_->append(*grid);

    Gtk::Label *drives = manage(new Gtk::Label("drives"));

    drives->set_xalign(0);
    drives->set_sensitive(false);
    drives->set_margin_top(6);
    selBox_->append(*drives);

    int found = 0;

    for (size_t ci = 0; ci < doc_.chains.size(); ci++)
        for (size_t si = 0; si < doc_.chains[ci].stages.size(); si++)
        {
            thcStage *live = liveStage(ci, si);

            if (live == NULL)
                continue;

            for (int pi = 0; pi < live->plugin->paramCount(); pi++)
            {
                thArg *bound = live->params.knobBinding(pi);
                const thcPlugin::ParamInfo *info =
                    live->plugin->paramInfo(pi);

                if (bound == NULL || info == NULL ||
                    bound->name() != name)
                    continue;

                Gtk::Box *rowBox = manage(new Gtk::Box(
                    Gtk::Orientation::HORIZONTAL, 6));

                Gtk::Label *what = manage(new Gtk::Label(
                    doc_.chains[ci].name + " / " +
                    doc_.chains[ci].stages[si].name + " . " + info->name));

                what->set_xalign(0);
                what->set_hexpand(true);

                Gtk::Button *cut = manage(new Gtk::Button("Unbind"));

                const std::string param = info->name;

                /* Through the panel's own rule rather than a second copy of
                   it: letting a binding go holds the number the knob was at,
                   spelled the way a knob's number is spelled, and a duration
                   keeps a unit. StagePanel.h calls that "@", and this is the
                   same request a person makes by picking `(value)' out of a
                   row's binding menu. */
                cut->signal_clicked().connect(
                    [this, ci, si, param]
                    {
                        StagePanel panel;
                        thPanelEdit edit;

                        panel.setPiece(&doc_, sched_);
                        panel.setStage((int)ci,
                                       thcGenEdit::liveIndex(doc_.chains[ci],
                                                             si));

                        const thPanelResult r =
                            panel.propose(param, "@", edit);

                        if (!r.ok)
                        {
                            status_->set_text(r.why);
                            return;
                        }

                        if (r.changed)
                            applyParam(ci, si, param, edit.valueText);

                        rebuildSelection();
                    });

                rowBox->append(*what);
                rowBox->append(*cut);
                selBox_->append(*rowBox);
                found++;
            }
        }

    /* And the dsp nodes, which have no thcStage and so are invisible to
       the loop above. A knob whose only job is an LFO's depth would
       otherwise read as driving nothing at all -- which is exactly how
       it looked. */
    for (size_t ci = 0; ci < doc_.chains.size(); ci++)
    {
        thcChain *live = sched_->chain(ci);

        if (live == NULL || !live->nodes)
            continue;

        const std::vector<thcNodeHost::KnobUse> uses = live->nodes->knobUses();

        for (size_t u = 0; u < uses.size(); u++)
        {
            if (uses[u].knob != name)
                continue;

            Gtk::Label *row = manage(new Gtk::Label(
                doc_.chains[ci].name + " / " + uses[u].node + " . " +
                uses[u].arg));

            row->set_xalign(0);
            selBox_->append(*row);
            found++;
        }
    }

    /* And the other world. A knob may drive an instrument's chanarg as
       well as a stage's param -- one knob, both sides of the boundary --
       so a list of what it drives that stopped at the stages would be
       telling half the truth about the piece.
     *
       No Unbind beside these, unlike the stage rows: undoing one means
       editing the instrument block, and the instrument block has no
       authoring surface yet. Better a row that says what is true than a
       button that cannot do what it offers. */
    for (size_t i = 0; i < sched_->instruments().size(); i++)
    {
        const thcInstrument &inst = sched_->instruments()[i];

        for (size_t a = 0; a < inst.args.size(); a++)
        {
            if (inst.args[a].knob != name)
                continue;

            std::string what = inst.name + " . " + inst.args[a].name;

            if (!inst.args[a].units.empty())
                what += "  (" + inst.args[a].units + ")";

            Gtk::Label *row = manage(new Gtk::Label(what));

            row->set_xalign(0);
            selBox_->append(*row);
            found++;
        }
    }

    if (found == 0)
    {
        Gtk::Label *none = manage(new Gtk::Label(
            "nothing yet -- drag a wire from the knob's port onto a "
            "stage"));

        none->set_wrap(true);
        none->set_xalign(0);
        none->set_sensitive(false);
        selBox_->append(*none);
    }
}

/* A knob node's track, dragged.
 *
 * The same two-part shape as everything else on the canvas: the live
 * value moves under the finger and the file hears about it once, when
 * the finger comes off. thArg::setValue is what every bound param is
 * reading through, so the poke is one assignment however many params
 * that is -- which is the whole point of a knob. */
void
Composer::onCanvasKnob (std::string name, double value, bool commit)
{
    thArg *arg = sched_->knob(name);

    if (arg == NULL)
        return;

    arg->setValue((float)value);

    if (!commit)
        return;

    /* Through editOk, like every other edit in this file: a splice that
       fails says so on the status line. It used to test for OK and
       otherwise do nothing at all, so a knob dragged against an
       unwritable working copy moved on screen, moved the piece, and left
       the file behind without a word -- which is the failure mode where
       silence costs the most, because everything else about it looked
       like it had worked. */
    std::string why;

    if (editOk(thcGenEdit::setKnobValue(workPath_, name, value, why), why))
        setDirty(true);

    /* The Knobs section's own slider is now stale. Only when the panel
       is up: rebuildEditor on a hidden panel is work nobody sees, and
       the panel is rebuilt on the way to being shown anyway. */
    if (editing_)
        rebuildEditor();
}

/* A wire dropped on a stage box: which param is it for?
 *
 * The canvas cannot answer this and should not guess -- a stage with six
 * numeric params is six honest answers -- so the drop asks. One button
 * per param the wire could drive, and a param that is already bound says
 * which knob has it, because rebinding is a thing people do and finding
 * out by doing it is not.
 *
 * Params that cannot take a knob are left out rather than shown greyed:
 * a note set or a Life board is not a thing a wire could ever reach, and
 * a list of things you cannot have is not help. */
void
Composer::onCanvasBindKnob (std::string knob, size_t chain,
                                  size_t stage, CanvasRect at)
{
    closeParams();

    thcStage *live = liveStage(chain, stage);

    if (live == NULL || chain >= doc_.chains.size() ||
        stage >= doc_.chains[chain].stages.size())
        return;

    Gtk::Box *list = manage(new Gtk::Box(Gtk::Orientation::VERTICAL, 2));

    list->set_margin(8);

    Gtk::Label *head = manage(new Gtk::Label());

    head->set_markup("<b>@" + Glib::Markup::escape_text(knob) +
                     "</b> \u2192 " +
                     Glib::Markup::escape_text(
                         doc_.chains[chain].stages[stage].name));
    head->set_xalign(0);
    head->set_margin_bottom(4);
    list->append(*head);

    int offered = 0;

    for (int pi = 0; pi < live->plugin->paramCount(); pi++)
    {
        const thcPlugin::ParamInfo *info = live->plugin->paramInfo(pi);

        if (info == NULL ||
            (info->type != THC_PARAM_FLOAT && info->type != THC_PARAM_INT))
            continue;

        std::string label = info->name;
        thArg *bound = live->params.knobBinding(pi);

        if (bound != NULL)
            label += bound->name() == knob ? "  (already @" + knob + ")"
                                           : "  (now @" + bound->name() + ")";

        Gtk::Button *btn = manage(new Gtk::Button(label));

        btn->set_has_frame(false);
        btn->set_halign(Gtk::Align::FILL);

        if (!info->desc.empty())
            btn->set_tooltip_text(info->desc);

        Gtk::Widget *child = btn->get_child();

        if (child != NULL)
            child->set_halign(Gtk::Align::START);

        const std::string param = info->name;

        btn->signal_clicked().connect(
            [this, chain, stage, param, knob]
            {
                closeParams();
                applyParam(chain, stage, param, "@" + knob);
            });

        list->append(*btn);
        offered++;
    }

    if (offered == 0)
    {
        Gtk::Label *none = manage(new Gtk::Label(
            "this stage has nothing a knob can drive"));

        none->set_sensitive(false);
        list->append(*none);
    }

    paramPop_ = new Gtk::Popover();
    paramPop_->set_child(*list);
    paramPop_->set_parent(*canvas_);
    paramPop_->set_position(Gtk::PositionType::BOTTOM);
    paramPop_->set_pointing_to(ComposerCanvasWidget::toGdk(at));
    paramPop_->popup();
}

/* One stage's parameters, drawn and bound.
 *
 * Both places the window shows them are this call: the Selection pane and the
 * popover the canvas's params handle brings up. What they are is StagePanel
 * through PanelView, which is the same description the browser draws from
 * -- so the two pages of this program stopped guessing separately at what a
 * duration is worth in milliseconds and which knobs a param may be read
 * through.
 *
 * Made afresh at each use rather than kept: a reload replaces every
 * ParamInfo behind these widgets, and a panel that outlived one would be
 * editing a stage that no longer exists.
 *
 * `si' is the document's stage index, which is what the window counts in;
 * the panel names one by the scheduler's, so the two meet here. */
StageParamsView *
Composer::makeStageParams (size_t ci, size_t si)
{
    StageParamsView *view = manage(new StageParamsView());

    view->setStage(&doc_, sched_, (int)ci,
                   thcGenEdit::liveIndex(doc_.chains[ci], si));

    view->signal_param_edited().connect(
        [this, ci, si, view](const thPanelEdit &edit)
        {
            applyParam(ci, si, edit.row, edit.valueText);

            /* Described again from the document the splice just changed: a
             * row that has become a binding is shown and no longer offered,
             * and a unit that has changed changes what the number means.
             *
             * On an idle and not here, because redrawing destroys the
             * widget whose signal this is -- GTK would be left finishing an
             * emission on an object it has already disposed, which is a
             * critical in the log and worse than that under a sanitizer.
             * Tracked on the view, so a panel taken down before the idle
             * runs -- the popover closes on the next press anywhere -- is
             * not one this goes looking for. */
            Glib::signal_idle().connect_once(
                sigc::track_obj(
                    [this, view, edit]
                    {
                        view->setStage(&doc_, sched_, edit.a, edit.b);
                    },
                    *view));
        });

    /* The refusals this panel can produce are ones somebody just caused --
       `H4' is not a note name, this piece declares no preset called that --
       unlike the channel's, where every control is a number or a list. So
       they go where the person is looking. */
    view->signal_edited().connect(
        [this, view](const string &, const string &)
        {
            if (!view->why().empty())
                status_->set_text(view->why());
        });

    return view;
}

/* The params handle on a stage box, pressed.
 *
 * The rows are the Selection pane's rows -- makeStageParams, the same call the
 * Selection pane makes -- in a popover pointed at the box. That is the
 * whole of why this is a popover and not a drawing: boxes that take typed
 * numbers, unit menus that say `ms' or `beats', and the knob binding
 * dropdown are what a parameter is, and a canvas would have had to grow its
 * own versions of the three, in eight-pixel text, and would still not have
 * let anyone type. */
void
Composer::closeParams (void)
{
    if (paramPop_ == NULL)
        return;

    paramPop_->unparent();
    delete paramPop_;
    paramPop_ = NULL;
}

void
Composer::onCanvasParams (size_t chain, size_t stage,
                                CanvasRect at)
{
    closeParams();

    if (liveStage(chain, stage) == NULL || chain >= doc_.chains.size() ||
        stage >= doc_.chains[chain].stages.size())
        return;

    StageParamsView *params = makeStageParams(chain, stage);

    params->set_margin(10);

    /* Tall stages exist -- gen::life has eight -- and a popover taller
       than the window is one with an unreachable bottom. */
    Gtk::ScrolledWindow *scroll = manage(new Gtk::ScrolledWindow());

    scroll->set_child(*params);
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_propagate_natural_width(true);
    scroll->set_propagate_natural_height(true);
    scroll->set_max_content_height(420);

    paramPop_ = new Gtk::Popover();
    paramPop_->set_child(*scroll);
    paramPop_->set_parent(*canvas_);
    paramPop_->set_position(Gtk::PositionType::BOTTOM);
    paramPop_->set_pointing_to(ComposerCanvasWidget::toGdk(at));
    paramPop_->popup();
}

void
Composer::onCanvasMoveStage (size_t chain, int from, int to)
{
    if (chain >= doc_.chains.size())
        return;

    std::string why;

    if (editOk(thcGenEdit::moveStage(workPath_, doc_.chains[chain].name,
                                     from, to, why), why))
    {
        /* Keep the selection on the stage that moved. */
        ComposerCanvas::Selection sel;

        sel.kind = ComposerCanvas::Selection::STAGE;
        sel.chain = chain;
        sel.index = (size_t)to;
        canvas_->select(sel);

        structuralReload();
    }
}

/* ---- the editor panel ------------------------------------------------- */

thcStage *
Composer::liveStage (size_t ci, size_t si)
{
    thcChain *c = sched_->chain(ci);

    if (c == NULL || ci >= doc_.chains.size())
        return NULL;

    /* Through liveIndex, because a chain's document stages and its
       scheduler stages stopped being the same list when nodes arrived:
       a dsp stage is a stage in the file and nothing in the event flow.
       NULL for one of those is the honest answer -- it has no thcStage
       to hand back. */
    const int at = thcGenEdit::liveIndex(doc_.chains[ci], si);

    if (at < 0 || (size_t)at >= c->stages.size())
        return NULL;

    return c->stages[at].get();
}

std::vector<std::pair<std::string, std::string> >
Composer::defaultParams (const thcPlugin *plugin)
{
    /* Every registered param, spelled out -- a .gen should survive a
       plugin's defaults changing; this is the lesson of noargs/.
     *
       The spelling is StagePanel's, which is the same answer the panel over
       this stage will read back. A stage written with one set of rules and
       described with another is how a freshly added stage comes to show a
       value nobody wrote. */
    std::vector<std::pair<std::string, std::string> > out;

    for (int i = 0; i < plugin->paramCount(); i++)
        out.push_back(std::make_pair(plugin->paramInfo(i)->name,
                                     StagePanel::defaultText(plugin, i)));

    return out;
}

void
Composer::applyParam (size_t ci, size_t si, const std::string &param,
                            const std::string &valueText)
{
    if (ci >= doc_.chains.size())
        return;

    std::string why;

    if (!editOk(thcGenEdit::setParam(workPath_, doc_.chains[ci].name,
                                     (int)si, param, valueText, why), why))
        return;

    setDirty(true);

    /* Keep the cached doc true without a re-describe. */
    if (si < doc_.chains[ci].stages.size())
    {
        thcGenEdit::Stage &st = doc_.chains[ci].stages[si];
        bool found = false;

        for (size_t i = 0; i < st.params.size(); i++)
            if (st.params[i].name == param)
            {
                st.params[i].valueText = valueText;
                found = true;
            }

        if (!found)
        {
            thcGenEdit::Param p;

            p.name = param;
            p.valueText = valueText;
            st.params.push_back(p);
        }
    }

    /* And poke the live piece so the edit is audible now.
     *
       What a line means to a running stage -- bind this knob, hold that many
       beats, resolve these note names -- is StagePanel's, and is the same
       call the browser makes when the same edit arrives there as a command.
       After the splice, because the panel is described from the document and
       a stage poked first would be heard before it was written. */
    thPanelEdit edit;

    edit.kind = thPanelEdit::GEN_PARAM;
    edit.row = param;
    edit.valueText = valueText;
    edit.a = (int)ci;
    edit.b = thcGenEdit::liveIndex(doc_.chains[ci], si);

    StagePanel live;

    live.setPiece(&doc_, sched_);
    live.setStage(edit.a, edit.b);
    live.deliver(edit);
}

void
Composer::rebuildEditor (void)
{
    /* The buttons below die with their row; forgetting that here left
       setDirty poking freed widgets whenever the panel was hidden.
     *
     * kbdBtn_ is deliberately *not* in this list, and used to be. It
       lives in the transport, which nothing here clears, so nulling it
       forgot a widget that was still on screen and still connected --
       and the next click on Kbd input reached onKbdToggle, which
       dereferenced the null and took the program with it. A pointer
       cleared here has to be one the loop below actually destroys. */
    selBox_ = NULL;

    while (Gtk::Widget *child = editorBox_.get_first_child())
        editorBox_.remove(*child);

    while (Gtk::Widget *child = selOuter_.get_first_child())
        selOuter_.remove(*child);

    if (!editing_)
    {
        stale_ = true;
        return;
    }

    stale_ = false;

    editorBox_.append(*buildPieceSection());
    editorBox_.append(*buildKnobsSection());
    editorBox_.append(*buildScalesSection());
    editorBox_.append(*buildPresetsSection());

    if (Gtk::Widget *midi = buildMidiSection())
        editorBox_.append(*midi);

    /* The Selection pane follows the canvas: whatever is selected up
       there is editable in here. */
    selBox_ = manage(new Gtk::Box(Gtk::Orientation::VERTICAL, 4));
    selOuter_.append(*selBox_);

    rebuildSelection();
}

void
Composer::rebuildSelection (void)
{
    if (selBox_ == NULL)
        return;

    while (Gtk::Widget *child = selBox_->get_first_child())
        selBox_->remove(*child);

    if (!editing_)
    {
        stale_ = true;
        return;
    }

    const ComposerCanvas::Selection &sel = canvas_->selection();

    switch (sel.kind)
    {
        case ComposerCanvas::Selection::NONE:
        {
            Gtk::Label *hint = manage(new Gtk::Label(
                "Select a stage, sink or chain on the canvas -- or a "
                "\"+\" to add one."));

            hint->set_wrap(true);
            hint->set_xalign(0);
            selBox_->append(*hint);
            break;
        }
        case ComposerCanvas::Selection::KNOB:
            if (sel.index < doc_.knobs.size())
                buildKnobSelection(sel.index);
            break;
        case ComposerCanvas::Selection::CHAIN:
            if (sel.chain < doc_.chains.size())
                buildChainSelection(sel.chain);
            break;
        case ComposerCanvas::Selection::STAGE:
            if (sel.chain < doc_.chains.size() &&
                sel.index < doc_.chains[sel.chain].stages.size())
                buildStageSelection(sel.chain, sel.index);
            break;
        case ComposerCanvas::Selection::SINK:
            if (sel.chain < doc_.chains.size() &&
                sel.index < doc_.chains[sel.chain].sinks.size())
                buildSinkSelection(sel.chain, sel.index);
            break;
        case ComposerCanvas::Selection::ADD_STAGE:
            if (sel.chain < doc_.chains.size())
                buildAddStage(sel.chain);
            break;
        case ComposerCanvas::Selection::ADD_SINK:
            if (sel.chain < doc_.chains.size())
                buildAddSink(sel.chain);
            break;
        case ComposerCanvas::Selection::ADD_CHAIN:
            buildAddChain();
            break;
    }
}

/* Where the piece's MIDI instruments play on this machine: a row each,
 * with the port its `midi' pattern found, a choice of any other port or
 * of its dsp; the ports MIDI clock goes to, which any piece can drive; and
 * the delay that lines the devices up with the synth. A choice is this
 * machine's, kept by the output, and the file is not touched: the pattern
 * in it is what somebody else's machine matches. */
Gtk::Widget *
Composer::buildMidiSection (void)
{
    if (sched_ == NULL || midiOut_ == NULL)
        return NULL;

    std::vector<size_t> midi;

    for (size_t i = 0; i < sched_->instruments().size(); i++)
        if (!sched_->instruments()[i].midi.empty())
            midi.push_back(i);

    Gtk::Expander *exp = manage(new Gtk::Expander("MIDI out"));
    Gtk::Grid *grid = manage(new Gtk::Grid());

    grid->set_column_spacing(6);
    grid->set_row_spacing(4);
    grid->set_margin(4);

    std::vector<std::string> ports;

    for (const std::string &p : midiOut_->ports())
        ports.push_back(gthMidiOut::stableName(p));

    int row = 0;

    for (size_t i : midi)
    {
        const thcInstrument &inst = sched_->instruments()[i];
        const std::string pattern = inst.midi;
        const std::string to = midiOut_->route(pattern);

        /* The choices, and what each stores as the route: the pattern's
           own match, every port there is, and the dsp. A route to a port
           that is not plugged in now is kept as a choice of its own, so
           looking at this does not lose it. */
        std::vector<Glib::ustring> shown;
        std::vector<std::string> routes;

        shown.push_back("Match \"" + pattern + "\"");
        routes.push_back("");

        for (const std::string &p : ports)
        {
            shown.push_back(p);
            routes.push_back(p);
        }

        if (!to.empty() && to != gthMidiOut::PLAY_ON_SYNTH &&
            std::find(ports.begin(), ports.end(), to) == ports.end())
        {
            shown.push_back(to + " (not connected)");
            routes.push_back(to);
        }

        shown.push_back(inst.dsp.empty() ? "Nothing (no dsp)"
                                         : "This synth: " + inst.dsp);
        routes.push_back(gthMidiOut::PLAY_ON_SYNTH);

        Gtk::Label *name = manage(new Gtk::Label(inst.name));
        Gtk::DropDown *pick = manage(new Gtk::DropDown(shown));
        Gtk::Label *state = manage(new Gtk::Label());

        name->set_xalign(0);
        state->set_xalign(0);
        state->add_css_class("dim-label");
        pick->set_tooltip_text("Where " + inst.name + " plays on this "
                               "machine. Kept here, not in the piece.");

        const int ch = inst.channel;

        state->set_text(sched_->playsOverMidi(ch)
                        ? "on " + midiOut_->routeOf(ch)
                        : sched_->midiWhy(ch) +
                          (inst.dsp.empty() ? "; silent"
                                            : "; playing " + inst.dsp));

        guint at = 0;

        for (size_t k = 0; k < routes.size(); k++)
            if (routes[k] == to)
                at = (guint)k;

        pick->set_selected(at);

        pick->property_selected().signal_changed().connect(
            [this, pick, routes, pattern]
            {
                const guint k = pick->get_selected();

                if (k >= routes.size())
                    return;

                midiOut_->setRoute(pattern, routes[k]);
                reroute(pattern);

                /* Rebuilt after the handler returns: this dropdown is
                   one of the widgets a rebuild destroys. */
                midiIdle_.disconnect();
                midiIdle_ = Glib::signal_idle().connect(
                    [this] { rebuildEditor(); return false; });
            });

        grid->attach(*name, 0, row);
        grid->attach(*pick, 1, row);
        grid->attach(*state, 1, row + 1, 2, 1);
        row += 2;
    }

    /* Clock: a check per port, and the ones checked are sent 24 ticks a
       beat, Start, Stop and Continue as the transport moves. */
    {
        Gtk::Label *cl = manage(new Gtk::Label("Clock to"));
        Gtk::Box *checks = manage(new Gtk::Box(Gtk::Orientation::VERTICAL,
                                               2));
        const std::vector<std::string> on = midiOut_->clockPorts();

        cl->set_xalign(0);
        cl->set_valign(Gtk::Align::START);

        for (const std::string &p : ports)
        {
            Gtk::CheckButton *check = manage(new Gtk::CheckButton(p));

            check->set_active(std::find(on.begin(), on.end(), p) != on.end());
            check->signal_toggled().connect(
                [this, p, check]
                {
                    std::vector<std::string> now = midiOut_->clockPorts();

                    now.erase(std::remove(now.begin(), now.end(), p),
                              now.end());

                    if (check->get_active())
                        now.push_back(p);

                    midiOut_->setClockPorts(now);
                });
            checks->append(*check);
        }

        if (ports.empty())
            checks->append(*manage(new Gtk::Label("no MIDI output ports")));

        grid->attach(*cl, 0, row);
        grid->attach(*checks, 1, row);
        row++;
    }

    Gtk::Label *dl = manage(new Gtk::Label("Delay (ms)"));
    Gtk::SpinButton *delay = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(midiOut_->delay(), 0, 500, 1, 10)));

    dl->set_xalign(0);
    delay->set_tooltip_text("How long after its time a note is sent to a "
                            "device. At least the composer's 20 ms step, so "
                            "notes go out evenly; about the synth's own "
                            "output latency, so the two line up.");
    delay->signal_value_changed().connect(
        [this, delay]
        { midiOut_->setDelay(delay->get_value_as_int()); });

    grid->attach(*dl, 0, row);
    grid->attach(*delay, 1, row);

    exp->set_child(*grid);
    exp->set_expanded(true);

    return exp;
}

void
Composer::reroute (const std::string &pattern)
{
    std::string failed;

    for (size_t i = 0; i < sched_->instruments().size(); i++)
    {
        if (sched_->instruments()[i].midi != pattern)
            continue;

        std::string why;

        /* Not applied over a graph that would not come off: the channel
           is still that graph's, and the scheduler retries taking it off
           on its own. */
        if (!sched_->unapplyInstrument(i))
            failed = sched_->instruments()[i].name + ": its graph could "
                     "not be taken off; try again";
        else if (!sched_->applyInstrument(i, why))
            failed = sched_->instruments()[i].name + ": " + why;
    }

    /* And the channels a swap put an instrument naming it on. */
    sched_->reapplySwapped(pattern);

    /* The status line says where the MIDI instruments are now, unless
       something here went wrong, which it says instead. */
    if (failed.empty())
        updateTransportButtons();
    else
        status_->set_text(failed);
}

Gtk::Widget *
Composer::buildPieceSection (void)
{
    Gtk::Expander *exp = manage(new Gtk::Expander("Piece"));
    Gtk::Grid *grid = manage(new Gtk::Grid());

    grid->set_column_spacing(6);
    grid->set_row_spacing(4);
    grid->set_margin(4);

    const char *keys[4] = { "name", "author", "description", "category" };
    const std::string *vals[4] = { &doc_.name, &doc_.author,
                                   &doc_.description, &doc_.category };

    for (int i = 0; i < 4; i++)
    {
        std::string key = keys[i];
        Gtk::Label *lbl = manage(new Gtk::Label(key));
        Gtk::Entry *entry = manage(new Gtk::Entry());

        lbl->set_xalign(0);
        entry->set_text(*vals[i]);
        entry->set_hexpand(true);
        entry->set_tooltip_text("Enter applies");

        entry->signal_activate().connect(
            [this, key, entry]
            {
                std::string why;

                if (editOk(thcGenEdit::setInfo(workPath_, key,
                                               entry->get_text(), why),
                           why))
                {
                    setDirty(true);

                    if (key == "name")
                    {
                        std::string why2;

                        thcGenEdit::describe(workPath_, doc_, why2);
                        pieceLabel_ = doc_.name;
                        updateTransportButtons();
                    }
                }
            });

        grid->attach(*lbl, 0, i);
        grid->attach(*entry, 1, i, 2, 1);
    }

    /* The seed: pinned or breathing. */
    Gtk::CheckButton *pin = manage(new Gtk::CheckButton("pin seed"));
    Gtk::SpinButton *seedSpin = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(doc_.hasSeed ? doc_.seed : 0, 0,
                                4294967295.0, 1)));

    pin->set_active(doc_.hasSeed);
    seedSpin->set_sensitive(doc_.hasSeed);
    pin->set_tooltip_text("A pinned seed replays the same piece every "
                          "time; unpinned, the piece breathes");

    pin->signal_toggled().connect(
        [this, pin, seedSpin]
        {
            std::string why;
            bool on = pin->get_active();

            seedSpin->set_sensitive(on);

            thcGenEdit::Result r = on
                ? thcGenEdit::setSeed(workPath_,
                      (unsigned)seedSpin->get_value(), why)
                : thcGenEdit::clearSeed(workPath_, why);

            if (editOk(r, why))
                structuralReload();
        });

    seedSpin->signal_value_changed().connect(
        [this, pin, seedSpin]
        {
            if (!pin->get_active())
                return;

            std::string why;

            if (editOk(thcGenEdit::setSeed(workPath_,
                    (unsigned)seedSpin->get_value(), why), why))
                structuralReload();
        });

    grid->attach(*pin, 0, 4);
    grid->attach(*seedSpin, 1, 4);

    exp->set_child(*grid);
    exp->set_expanded(false);

    return exp;
}

Gtk::Widget *
Composer::buildKnobsSection (void)
{
    Gtk::Expander *exp = manage(new Gtk::Expander("Knobs"));
    Gtk::Grid *grid = manage(new Gtk::Grid());

    grid->set_column_spacing(6);
    grid->set_row_spacing(4);
    grid->set_margin(4);

    int row = 0;

    /* A knob to a block of three rows -- its name and Remove, its range,
       and what the slider says -- rather than one row of all five, which
       came to some 640 pixels and wanted a pane that wide before any of
       it was cut off. The words stay beside the numbers, because without
       them this is two anonymous spinners, and there is no guessing which
       of `0.000' and `255.000' is which -- least of all that neither is
       the knob's *value*. The value lives on the slider above the canvas,
       where it can be dragged while the piece plays; this section is the
       knob's shape, not its position. */
    for (size_t i = 0; i < doc_.knobs.size(); i++)
    {
        const thcGenEdit::Knob &k = doc_.knobs[i];
        std::string name = k.name;

        Gtk::Label *lbl = manage(new Gtk::Label());

        lbl->set_markup("<b>@" + Glib::Markup::escape_text(name) + "</b>");
        lbl->set_xalign(0);

        Gtk::SpinButton *minSpin = manage(new Gtk::SpinButton(
            Gtk::Adjustment::create(k.min, -100000, 100000, 0.01), 0, 3));
        Gtk::SpinButton *maxSpin = manage(new Gtk::SpinButton(
            Gtk::Adjustment::create(k.max, -100000, 100000, 0.01), 0, 3));
        Gtk::Entry *lblEntry = manage(new Gtk::Entry());
        Gtk::Button *rm = manage(new Gtk::Button("Remove"));

        minSpin->set_width_chars(6);
        maxSpin->set_width_chars(6);

        lblEntry->set_text(k.label);
        lblEntry->set_placeholder_text("label");
        lblEntry->set_hexpand(true);

        rm->set_halign(Gtk::Align::END);

        auto applyMeta = [this, name, minSpin, maxSpin, lblEntry]
        {
            std::string why;

            if (editOk(thcGenEdit::setKnobMeta(workPath_, name,
                    minSpin->get_value(), maxSpin->get_value(),
                    lblEntry->get_text(), why), why))
                structuralReload();
        };

        minSpin->signal_value_changed().connect(applyMeta);
        maxSpin->signal_value_changed().connect(applyMeta);
        lblEntry->signal_activate().connect(applyMeta);

        rm->signal_clicked().connect(
            [this, name]
            {
                thArg *arg = sched_->knob(name);
                double fallback = arg != NULL ? (*arg)[0] : 0;
                int rewritten = 0;
                std::string why;

                if (editOk(thcGenEdit::removeKnob(workPath_, name,
                        fallback, rewritten, why), why))
                    structuralReload();
            });

        auto word = [] (const char *text)
        {
            Gtk::Label *w = manage(new Gtk::Label(text));

            w->set_xalign(0);
            w->set_sensitive(false);

            return w;
        };

        /* A little room above every knob but the first, so each block
           reads as one. */
        if (i > 0)
            lbl->set_margin_top(8);

        grid->attach(*lbl, 0, row, 3, 1);
        grid->attach(*rm, 3, row);
        row++;

        grid->attach(*word("lowest"), 0, row);
        grid->attach(*minSpin, 1, row);
        grid->attach(*word("highest"), 2, row);
        grid->attach(*maxSpin, 3, row);
        row++;

        grid->attach(*word("shown as"), 0, row);
        grid->attach(*lblEntry, 1, row, 3, 1);
        row++;
    }

    Gtk::Entry *newName = manage(new Gtk::Entry());
    Gtk::Button *add = manage(new Gtk::Button("Add knob"));

    newName->set_placeholder_text("new knob name");
    newName->set_max_width_chars(12);

    add->signal_clicked().connect(
        [this, newName]
        {
            std::string why;

            if (editOk(thcGenEdit::addKnob(workPath_, newName->get_text(),
                    0.5, 0, 1, "", why), why))
                structuralReload();
        });

    newName->set_hexpand(true);

    if (!doc_.knobs.empty())
    {
        newName->set_margin_top(8);
        add->set_margin_top(8);
    }

    grid->attach(*newName, 0, row, 3, 1);
    grid->attach(*add, 3, row);

    exp->set_child(*grid);
    exp->set_expanded(!doc_.knobs.empty());

    return exp;
}

Gtk::Widget *
Composer::buildScalesSection (void)
{
    Gtk::Expander *exp = manage(new Gtk::Expander("Scales"));
    Gtk::Grid *grid = manage(new Gtk::Grid());

    grid->set_column_spacing(6);
    grid->set_row_spacing(4);
    grid->set_margin(4);

    int row = 0;

    for (size_t i = 0; i < doc_.scales.size(); i++)
    {
        std::string name = doc_.scales[i].name;

        Gtk::Label *lbl = manage(new Gtk::Label(name));
        Gtk::Entry *notes = manage(new Gtk::Entry());
        Gtk::Button *rm = manage(new Gtk::Button("Remove"));

        lbl->set_xalign(0);
        notes->set_text(doc_.scales[i].notes);
        notes->set_hexpand(true);
        notes->set_tooltip_text("Note names; Enter applies");

        notes->signal_activate().connect(
            [this, name, notes]
            {
                std::string why;

                if (editOk(thcGenEdit::setScale(workPath_, name,
                        notes->get_text(), why), why))
                    structuralReload();
            });

        rm->signal_clicked().connect(
            [this, name]
            {
                int rewritten = 0;
                std::string why;

                if (editOk(thcGenEdit::removeScale(workPath_, name,
                        rewritten, why), why))
                    structuralReload();
            });

        grid->attach(*lbl, 0, row);
        grid->attach(*notes, 1, row);
        grid->attach(*rm, 2, row);
        row++;
    }

    Gtk::Entry *newName = manage(new Gtk::Entry());
    Gtk::Entry *newNotes = manage(new Gtk::Entry());
    Gtk::Button *add = manage(new Gtk::Button("Add scale"));

    newName->set_placeholder_text("name");
    newName->set_max_width_chars(8);
    newNotes->set_placeholder_text("C4 D4 E4 G4 A4");
    newNotes->set_hexpand(true);

    add->signal_clicked().connect(
        [this, newName, newNotes]
        {
            std::string why;

            if (editOk(thcGenEdit::addScale(workPath_, newName->get_text(),
                    newNotes->get_text(), why), why))
                structuralReload();
        });

    grid->attach(*newName, 0, row);
    grid->attach(*newNotes, 1, row);
    grid->attach(*add, 2, row);

    exp->set_child(*grid);
    exp->set_expanded(!doc_.scales.empty());

    return exp;
}

/* Presets: a named chanarg vector per block, one spin button per
 * component.
 *
 * Laid out as rows under the preset's name rather than as one text field
 * of "res=0.4,fmin=0.1", because a preset is the thing a morph travels
 * between and dragging one component while listening is the whole point.
 * Every edit is a splice *and* a poke, so the sweep between two presets
 * changes under the transport rather than at the next load. */
Gtk::Widget *
Composer::buildPresetsSection (void)
{
    Gtk::Expander *exp = manage(new Gtk::Expander("Presets"));
    Gtk::Grid *grid = manage(new Gtk::Grid());

    grid->set_column_spacing(6);
    grid->set_row_spacing(4);
    grid->set_margin(4);

    int row = 0;

    for (size_t i = 0; i < doc_.presets.size(); i++)
    {
        const std::string preset = doc_.presets[i].name;

        Gtk::Label *head = manage(new Gtk::Label(preset));
        Gtk::Button *rmPreset = manage(new Gtk::Button("Remove"));

        head->set_xalign(0);
        head->set_markup("<b>" + Glib::Markup::escape_text(preset) +
                         "</b>");

        rmPreset->signal_clicked().connect(
            [this, preset]
            {
                std::string why;

                if (editOk(thcGenEdit::removePreset(workPath_, preset,
                                                    why), why))
                    structuralReload();
            });

        grid->attach(*head, 0, row, 2, 1);
        grid->attach(*rmPreset, 3, row);
        row++;

        for (size_t k = 0; k < doc_.presets[i].values.size(); k++)
        {
            const std::string comp = doc_.presets[i].values[k].name;

            Gtk::Label *lbl = manage(new Gtk::Label("    " + comp));
            Gtk::SpinButton *spin = manage(new Gtk::SpinButton(
                Gtk::Adjustment::create(doc_.presets[i].values[k].value,
                                        -1e6, 1e6, 0.01), 0, 4));
            Gtk::Button *rm = manage(new Gtk::Button("-"));

            lbl->set_xalign(0);
            spin->set_hexpand(true);

            spin->signal_value_changed().connect(
                [this, preset, comp, spin]
                {
                    std::string why;

                    if (editOk(thcGenEdit::setPresetValue(
                            workPath_, preset, comp, spin->get_value(),
                            why), why))
                        presetChanged(preset);
                });

            rm->signal_clicked().connect(
                [this, preset, comp]
                {
                    std::string why;

                    if (editOk(thcGenEdit::removePresetValue(
                            workPath_, preset, comp, why), why))
                        structuralReload();
                });

            grid->attach(*lbl, 0, row);
            grid->attach(*spin, 1, row, 2, 1);
            grid->attach(*rm, 3, row);
            row++;
        }

        Gtk::Entry *newComp = manage(new Gtk::Entry());
        Gtk::Button *addComp = manage(new Gtk::Button("Add value"));

        newComp->set_placeholder_text("chanarg");
        newComp->set_max_width_chars(10);

        addComp->signal_clicked().connect(
            [this, preset, newComp]
            {
                std::string why;

                if (editOk(thcGenEdit::addPresetValue(
                        workPath_, preset, newComp->get_text(), 0.5, why),
                        why))
                    structuralReload();
            });

        grid->attach(*newComp, 1, row);
        grid->attach(*addComp, 2, row);
        row++;
    }

    /* A new preset arrives with one component, because one with none
       does not load and every state written here has to. */
    Gtk::Entry *newName = manage(new Gtk::Entry());
    Gtk::Entry *firstComp = manage(new Gtk::Entry());
    Gtk::Button *add = manage(new Gtk::Button("Add preset"));

    newName->set_placeholder_text("name");
    newName->set_max_width_chars(8);
    firstComp->set_placeholder_text("first chanarg");
    firstComp->set_hexpand(true);

    add->signal_clicked().connect(
        [this, newName, firstComp]
        {
            std::vector<thcGenEdit::PresetValue> vals;
            thcGenEdit::PresetValue v;

            v.name = firstComp->get_text();
            v.value = 0.5;
            vals.push_back(v);

            std::string why;

            if (editOk(thcGenEdit::addPreset(workPath_,
                    newName->get_text(), vals, why), why))
                structuralReload();
        });

    grid->attach(*newName, 0, row);
    grid->attach(*firstComp, 1, row);
    grid->attach(*add, 2, row);

    exp->set_child(*grid);
    exp->set_expanded(!doc_.presets.empty());

    return exp;
}

/* A preset's value changed: re-resolve it into every live stage that
 * names it, so the piece keeps playing and hears the edit.
 *
 * A value edit splices and pokes; only a structural one reloads. A
 * preset's *components* are its value, so moving one is a value edit --
 * which is what makes dragging a component while a morph is sweeping
 * behave the way dragging a knob does. */
void
Composer::presetChanged (const std::string &preset)
{
    setDirty(true);

    std::string why;
    thcGenEdit::Doc fresh;

    if (thcGenEdit::describe(workPath_, fresh, why) != thcGenEdit::OK)
        return;

    doc_.presets = fresh.presets;

    std::string vecText;

    for (size_t i = 0; i < doc_.presets.size(); i++)
    {
        if (doc_.presets[i].name != preset)
            continue;

        for (size_t k = 0; k < doc_.presets[i].values.size(); k++)
        {
            std::string num;

            thcGenEdit::format(doc_.presets[i].values[k].value, num);

            vecText += (k ? "," : "");
            vecText += doc_.presets[i].values[k].name + "=" + num;
        }
    }

    if (vecText.empty())
        return;

    /* Every stage naming it, in every chain: one preset can be the
       destination of several morphs at once, and half of them hearing
       the edit would be worse than none. */
    for (size_t ci = 0; ci < doc_.chains.size(); ci++)
        for (size_t si = 0; si < doc_.chains[ci].stages.size(); si++)
        {
            thcStage *s = liveStage(ci, si);

            if (s == NULL)
                continue;

            const thcGenEdit::Stage &st = doc_.chains[ci].stages[si];

            for (size_t pi = 0; pi < st.params.size(); pi++)
            {
                if (st.params[pi].valueText != preset)
                    continue;

                const int idx = s->plugin->paramIndex(st.params[pi].name);

                if (idx < 0)
                    continue;

                const thcPlugin::ParamInfo *info = s->plugin->paramInfo(idx);

                if (info != NULL && info->type == THC_PARAM_PRESET)
                    s->params.setString(idx, vecText);
            }
        }
}

/* Ask a stage's module what its touchable state is now, and write it
 * back through the ordinary param path.
 *
 * Every param is offered and the module answers for the ones it can.
 * `composer_capture' returns NULL for the rest, which thcPlugin::capture
 * turns into an empty string at the ABI boundary -- so what this loop
 * tests is emptiness, and the two spellings mean the same thing on
 * either side of that line. Offering every param is what keeps the host
 * from having to know which param of which plugin holds a board, and it
 * is what makes this one button rather than one per plugin.
 *
 * Through applyParam, so the splice, the cached doc and the live poke
 * all happen the way they do for a value typed by hand. Writing the file
 * behind thcGenEdit's back would be a second writer, and there is one. */
void
Composer::captureStage (size_t ci, size_t si, bool report)
{
    thcStage *s = liveStage(ci, si);

    if (s == NULL || !s->plugin->hasCapture())
        return;

    int written = 0;
    std::string refused;

    for (int pi = 0; pi < s->plugin->paramCount(); pi++)
    {
        std::string text;

        /* Nothing captured is not the same as an empty capture: an
           accent pattern cleared of every mark is "", and is written. */
        if (!s->plugin->capture(s->state, pi, text))
            continue;

        const thcPlugin::ParamInfo *info = s->plugin->paramInfo(pi);

        if (info == NULL)
            continue;

        /* A string param -- a board, an axiom, a pattern -- quoted; a
           whole number as it is, which is what the euclid ring hands back
           for its fills and rotation. Nothing else is written: a float
           may need a unit the plugin cannot know it was written in. */
        const bool quoted = info->type == THC_PARAM_STRING;

        if (!quoted && (info->type != THC_PARAM_INT || text.empty() ||
                        text.find_first_not_of("-0123456789") !=
                            std::string::npos))
            continue;

        /* A .gen string is "[^"\n]*" with no escapes at all, so a quote
           or a newline in what a module hands back simply cannot be
           written. thcGenEdit::setParam refuses such a value and the
           file is safe either way -- but it would refuse it three
           layers down, and this loop would then go on to report a
           capture that did not happen. Checked here so the message
           names the param and the count is true. */
        if (text.find('"') != std::string::npos ||
            text.find('\n') != std::string::npos)
        {
            refused = info->name;
            continue;
        }

        /* Unchanged, from a sequencer track or a change of document: a
           gesture that ended where it began is not an edit, and writing
           it would mark the piece dirty for nothing. The button writes
           regardless, as it always has. */
        const std::string key = std::to_string(ci) + "." +
                                std::to_string(si) + "." + info->name;

        if (!report && baseline_.count(key) && baseline_[key] == text)
            continue;

        /* Nor what the file already says, a grid's bar lines aside --
           the grid hands its cells back without them. The sequencer's
           grids have baseline_ for this; every other picture has the
           document. */
        if (!report && ci < doc_.chains.size() &&
            si < doc_.chains[ci].stages.size())
        {
            auto bare = [](const std::string &t)
            {
                std::string out;

                for (char ch : t)
                    if (ch != '|' && ch != '"')
                        out += ch;

                return out;
            };
            bool same = false;

            for (const thcGenEdit::Param &p : doc_.chains[ci].stages[si].params)
                if (p.name == info->name && bare(p.valueText) == bare(text))
                    same = true;

            if (same)
                continue;
        }

        applyParam(ci, si, info->name, quoted ? "\"" + text + "\"" : text);
        baseline_[key] = text;
        written++;
    }

    if (!refused.empty())
        status_->set_text("'" + refused + "' cannot be written: a .gen "
                          "string holds no quotes or newlines");
    else if (!report)
        return;
    else if (written)
        status_->set_text("captured into the piece; Save to keep it");
    else
        status_->set_text("this stage had nothing to capture");
}

void
Composer::buildChainSelection (size_t ci)
{
    const thcGenEdit::Chain &chain = doc_.chains[ci];
    std::string chainName = chain.name;

    selBox_->append(*manage(new Gtk::Label("chain " + chainName)));

    Gtk::Box *head = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 6));
    Gtk::Entry *nameEntry = manage(new Gtk::Entry());
    Gtk::Entry *startEntry = manage(new Gtk::Entry());
    Gtk::CheckButton *mute = manage(new Gtk::CheckButton("mute"));
    Gtk::CheckButton *input = manage(new Gtk::CheckButton("MIDI in"));
    Gtk::Button *rm = manage(new Gtk::Button("Remove chain"));
    Gtk::Button *freeze = manage(new Gtk::Button(
        sched_->usesBeats() ? "Freeze last 2 bars" : "Freeze last 4 seconds"));

    freeze->set_tooltip_text("What this chain just played, as a new chain "
                             "with a grid you can edit; this one is muted. "
                             "The F on the canvas does the same.");
    freeze->signal_clicked().connect([this, ci] { onCanvasFreeze(ci); });

    nameEntry->set_text(chainName);
    nameEntry->set_max_width_chars(12);
    nameEntry->set_tooltip_text("Enter renames");

    startEntry->set_text(chain.startText);
    startEntry->set_max_width_chars(10);
    startEntry->set_placeholder_text("start");
    startEntry->set_tooltip_text("When this chain's generators first "
                                 "wake -- \"8 bars\", \"2 s\"; "
                                 "empty starts with the transport");

    thcChain *live = sched_->chain(ci);

    mute->set_active(live != NULL && live->muted);
    mute->set_tooltip_text("Live only -- mutes at the end of the chain, "
                           "the algorithm keeps running; not saved");
    input->set_active(chain.inputMidi);

    nameEntry->signal_activate().connect(
        [this, chainName, nameEntry]
        {
            std::string why;

            if (editOk(thcGenEdit::renameChain(workPath_, chainName,
                    nameEntry->get_text(), why), why))
            {
                mixRenamed_[chainName] = nameEntry->get_text();
                canvas_->renameCollapsed(chainName, nameEntry->get_text());
                structuralReload();
            }
        });

    mute->signal_toggled().connect(
        [this, ci, mute]
        {
            sched_->setMuted(ci, mute->get_active());
            canvas_->queue_draw();
        });

    startEntry->signal_activate().connect(
        [this, chainName, startEntry]
        {
            std::string why;

            if (editOk(thcGenEdit::setChainStart(workPath_, chainName,
                    startEntry->get_text(), why), why))
                structuralReload();
        });

    input->signal_toggled().connect(
        [this, chainName, input]
        {
            std::string why;

            if (editOk(thcGenEdit::setChainInput(workPath_, chainName,
                    input->get_active(), why), why))
                structuralReload();
        });

    rm->signal_clicked().connect(
        [this, chainName]
        {
            std::string why;

            if (editOk(thcGenEdit::removeChain(workPath_, chainName, why),
                       why))
                structuralReload();
        });

    head->append(*nameEntry);
    head->append(*startEntry);
    head->append(*mute);
    head->append(*input);
    selBox_->append(*head);
    selBox_->append(*freeze);
    selBox_->append(*rm);
}

void
Composer::buildStageSelection (size_t ci, size_t si)
{
    const thcGenEdit::Stage &stage = doc_.chains[ci].stages[si];
    std::string chainName = doc_.chains[ci].name;

    std::ostringstream title;

    title << chainName << " · " << stage.category << "::" << stage.plugin
          << "  (" << stage.name << ")";

    Gtk::Label *lbl = manage(new Gtk::Label(title.str()));

    lbl->set_xalign(0);
    selBox_->append(*lbl);

    /* Every registered param, whether or not the file writes it --
       editing one that exists only as a default inserts the line.
       Reordering is a drag on the canvas now, so the buttons here are
       down to the one thing a drag cannot say. */
    std::map<std::string, thcPlugin *>::iterator found =
        composers_.find(stage.plugin);

    if (found != composers_.end())
    {
        selBox_->append(*makeStageParams(ci, si));
    }
    else if (stage.category != "gen" && stage.category != "xform")
    {
        /* A dsp:: node rather than a composer. It has params, but they
           belong to the other world's plugin and are edited as a .dsp
           node's args are -- which this panel has no vocabulary for
           yet. Saying what it is beats "not installed", which is what
           looking it up in the composer map was about to conclude. */
        std::string text = stage.category + "::" + stage.plugin +
                           " runs at control rate";

        /* Named from the plugin rather than assumed to be `out'. Plenty
           of modules have no arg by that name -- filt::moog answers on
           out_low, out_high and out_bandpass -- so a fixed `->out' here
           was advice that would not load, blamed on whichever line took
           it. Asked of the live host because it is holding the plugin
           already; a piece that did not load has no host, and then the
           honest hint is the shape of the spelling without a name in
           it. */
        thcChain *live = sched_->chain(ci);
        std::vector<std::string> outs;

        if (live != NULL && live->nodes)
            outs = live->nodes->outputArgs(stage.name);

        if (outs.empty())
            text += "; a stage reads one of its outputs with " +
                    stage.name + "->";
        else
        {
            text += "; a stage reads it with " + stage.name + "->" +
                    outs[0];

            for (size_t o = 1; o < outs.size(); o++)
                text += (o == 1 ? " (or ->" : ", ->") + outs[o];

            if (outs.size() > 1)
                text += ")";
        }

        Gtk::Label *what = manage(new Gtk::Label(text));

        what->set_wrap(true);
        what->set_xalign(0);
        what->set_sensitive(false);
        selBox_->append(*what);
    }
    else
        selBox_->append(*manage(new Gtk::Label(
            "module '" + stage.plugin + "' is not installed")));

    /* A module whose picture can be clicked can also be asked what its
       picture currently is. Offered here rather than on the canvas
       because this is where every other edit to a stage is made, and
       because capturing is deliberately a separate act from clicking: a
       click is a performance and changes what is playing, and writing it
       into the file is a decision about the piece. */
    if (found != composers_.end() && found->second->hasCapture())
    {
        Gtk::Button *cap = manage(new Gtk::Button("Capture to file"));

        cap->set_tooltip_text("Write what this stage is playing now back "
                              "into the piece -- double-click the stage on "
                              "the canvas to change it first");

        cap->signal_clicked().connect(
            [this, ci, si] { captureStage(ci, si); });

        selBox_->append(*cap);
    }

    Gtk::Button *rm = manage(new Gtk::Button("Remove stage"));
    int idx = (int)si;

    rm->signal_clicked().connect(
        [this, chainName, idx]
        {
            std::string why;

            if (editOk(thcGenEdit::removeStage(workPath_, chainName, idx,
                                               why), why))
                structuralReload();
        });

    selBox_->append(*rm);
}

/* What a sink plays, as a control.
 *
 * A piece that declares instruments should be steered by their names --
 * that is the whole of what carrying an instrument bought -- but `channel
 * = N' is still in the language for driving a patch the piece does not
 * own, so the last entry is that. The spinner comes alive only for that
 * entry: the channel behind an instrument is an allocation nobody chose,
 * and showing an editable number for it would invite somebody to change
 * it into a collision.
 *
 * A piece with no instruments gets the spinner it always had and no
 * drop-down at all. One choice is not a choice.
 */
Gtk::DropDown *
Composer::buildSinkTarget (Gtk::Box *row, Gtk::SpinButton *chan,
                                 const std::string &selected,
                                 std::vector<std::string> &targets)
{
    targets.clear();

    if (doc_.instruments.empty())
        return NULL;

    std::vector<Glib::ustring> shown;

    for (size_t i = 0; i < doc_.instruments.size(); i++)
    {
        shown.push_back(doc_.instruments[i].name);
        targets.push_back(doc_.instruments[i].name);
    }

    shown.push_back("channel");
    targets.push_back("");

    Gtk::DropDown *tgt = manage(new Gtk::DropDown(shown));

    tgt->set_tooltip_text("What this sink plays: one of the piece's own "
                          "instruments, or a bare MIDI channel for a patch "
                          "loaded from somewhere else");

    guint at = (guint)(targets.size() - 1);

    for (size_t i = 0; i + 1 < targets.size(); i++)
        if (targets[i] == selected)
            at = (guint)i;

    tgt->set_selected(at);
    chan->set_sensitive(targets[at].empty());

    row->append(*tgt);

    return tgt;
}

void
Composer::buildSinkSelection (size_t ci, size_t ki)
{
    const thcGenEdit::Chain &chain = doc_.chains[ci];
    std::string chainName = chain.name;
    int sinkIndex = (int)ki;

    selBox_->append(*manage(new Gtk::Label(chainName + " · sink")));

    Gtk::Box *row = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 6));

    /* An instrument sink carries no channel of its own, and the spinner
       has to start somewhere legal; 1 is what it offers if the target is
       switched to a bare channel. */
    Gtk::SpinButton *chan = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(chain.sinks[ki].channel > 0
                                ? chain.sinks[ki].channel : 1, 1, 16, 1)));
    Gtk::Entry *arg = manage(new Gtk::Entry());
    Gtk::Button *rm = manage(new Gtk::Button("Remove"));
    std::vector<std::string> targets;
    Gtk::DropDown *tgt = buildSinkTarget(row, chan,
                                         chain.sinks[ki].instrument, targets);

    chan->set_tooltip_text("MIDI channel, 1-16 -- the number on "
                           "the main window's patch tab");
    arg->set_text(chain.sinks[ki].chanarg);
    arg->set_placeholder_text("chanarg (empty: notes)");
    arg->set_max_width_chars(12);
    rm->set_sensitive(chain.sinks.size() > 1);

    auto applySink = [this, chainName, sinkIndex, chan, arg, tgt, targets]
    {
        std::string why;
        std::string instrument;

        if (tgt != NULL && tgt->get_selected() < targets.size())
            instrument = targets[tgt->get_selected()];

        if (editOk(thcGenEdit::setSink(workPath_, chainName, sinkIndex,
                chan->get_value_as_int(), instrument, arg->get_text(), why),
                why))
            structuralReload();
    };

    chan->signal_value_changed().connect(applySink);
    arg->signal_activate().connect(applySink);

    if (tgt != NULL)
        tgt->property_selected().signal_changed().connect(applySink);

    rm->signal_clicked().connect(
        [this, chainName, sinkIndex]
        {
            std::string why;

            if (editOk(thcGenEdit::removeSink(workPath_, chainName,
                    sinkIndex, why), why))
                structuralReload();
        });

    row->append(*chan);
    row->append(*arg);
    row->append(*rm);
    selBox_->append(*row);
}

void
Composer::buildAddStage (size_t ci)
{
    std::string chainName = doc_.chains[ci].name;
    size_t nStages = doc_.chains[ci].stages.size();

    selBox_->append(*manage(new Gtk::Label(
        "add a stage to " + chainName)));

    Gtk::Box *row = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 6));
    std::vector<Glib::ustring> shown;
    std::vector<std::string> names;

    for (std::map<std::string, thcPlugin *>::iterator i =
             composers_.begin(); i != composers_.end(); ++i)
    {
        shown.push_back(i->first);
        names.push_back(i->first);
    }

    Gtk::DropDown *sel = manage(new Gtk::DropDown(shown));
    Gtk::Button *add = manage(new Gtk::Button("Add stage"));

    add->signal_clicked().connect(
        [this, chainName, sel, names, nStages]
        {
            if (names.empty())
                return;

            guint s = sel->get_selected();

            if (s >= names.size())
                s = 0;

            thcPlugin *plugin = composers_[names[s]];
            std::ostringstream stageName;

            stageName << "s" << nStages + 1;

            std::string why;
            std::string cat = plugin->hasTick() ? "gen" : "xform";

            if (editOk(thcGenEdit::addStage(workPath_, chainName,
                    stageName.str(), cat, plugin->name(),
                    defaultParams(plugin), why), why))
                structuralReload();
        });

    row->append(*sel);
    row->append(*add);
    selBox_->append(*row);
}

void
Composer::buildAddSink (size_t ci)
{
    std::string chainName = doc_.chains[ci].name;

    selBox_->append(*manage(new Gtk::Label(
        "add a sink to " + chainName)));

    Gtk::Box *row = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 6));
    Gtk::SpinButton *chan = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(1, 1, 16, 1)));
    Gtk::Entry *arg = manage(new Gtk::Entry());
    Gtk::Button *add = manage(new Gtk::Button("Add sink"));
    std::vector<std::string> targets;

    /* No selection to preserve, so the first instrument is the offer --
       which for a piece that carries its instruments is nearly always
       the right one, and for a piece that does not is not offered. */
    Gtk::DropDown *tgt = buildSinkTarget(row, chan,
                                         doc_.instruments.empty()
                                         ? std::string()
                                         : doc_.instruments[0].name, targets);

    chan->set_tooltip_text("MIDI channel, 1-16 -- the number on "
                           "the main window's patch tab");
    arg->set_placeholder_text("chanarg (empty: notes)");
    arg->set_max_width_chars(12);

    add->signal_clicked().connect(
        [this, chainName, chan, arg, tgt, targets]
        {
            std::string why;
            std::string instrument;

            if (tgt != NULL && tgt->get_selected() < targets.size())
                instrument = targets[tgt->get_selected()];

            if (editOk(thcGenEdit::addSink(workPath_, chainName,
                    chan->get_value_as_int(), instrument, arg->get_text(),
                    why), why))
                structuralReload();
        });

    if (tgt != NULL)
        tgt->property_selected().signal_changed().connect(
            [chan, tgt, targets]
            {
                if (tgt->get_selected() < targets.size())
                    chan->set_sensitive(
                        targets[tgt->get_selected()].empty());
            });

    row->append(*chan);
    row->append(*arg);
    row->append(*add);
    selBox_->append(*row);
}

void
Composer::buildAddChain (void)
{
    selBox_->append(*manage(new Gtk::Label("add a chain")));

    Gtk::Box *row = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 6));
    Gtk::Entry *nameEntry = manage(new Gtk::Entry());
    std::vector<Glib::ustring> gens;
    std::vector<std::string> genNames;

    nameEntry->set_placeholder_text("name");
    nameEntry->set_max_width_chars(10);

    for (std::map<std::string, thcPlugin *>::iterator i = composers_.begin();
         i != composers_.end(); ++i)
        if (i->second->hasTick())
        {
            gens.push_back(i->first);
            genNames.push_back(i->first);
        }

    Gtk::DropDown *genSel = manage(new Gtk::DropDown(gens));
    Gtk::SpinButton *chanSel = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(1, 1, 16, 1)));
    Gtk::Button *addBtn = manage(new Gtk::Button("Add chain"));
    std::vector<std::string> targets;

    /* Same control the sink panels get, and here it earns its keep
       twice over: instrument channels are allocated around the numbers
       sinks claim, so a new chain written `channel = 1' into a piece
       whose instrument sits there does not collide -- it moves the
       instrument to another tab, which is not what anyone clicking
       "Add chain" was asking for. */
    Gtk::DropDown *tgt = buildSinkTarget(row, chanSel,
                                         doc_.instruments.empty()
                                         ? std::string()
                                         : doc_.instruments[0].name, targets);

    chanSel->set_tooltip_text("MIDI channel, 1-16 -- the number on "
                              "the main window's patch tab");

    addBtn->signal_clicked().connect(
        [this, nameEntry, genSel, chanSel, tgt, genNames, targets]
        {
            if (genNames.empty())
                return;

            guint sel = genSel->get_selected();

            if (sel >= genNames.size())
                sel = 0;

            thcPlugin *plugin = composers_[genNames[sel]];
            std::string why;
            std::string instrument;

            if (tgt != NULL && tgt->get_selected() < targets.size())
                instrument = targets[tgt->get_selected()];

            if (editOk(thcGenEdit::addChain(workPath_,
                    nameEntry->get_text(), chanSel->get_value_as_int(),
                    instrument, "src", "gen", plugin->name(),
                    defaultParams(plugin), why), why))
                structuralReload();
        });

    if (tgt != NULL)
        tgt->property_selected().signal_changed().connect(
            [chanSel, tgt, targets]
            {
                if (tgt->get_selected() < targets.size())
                    chanSel->set_sensitive(
                        targets[tgt->get_selected()].empty());
            });

    row->append(*nameEntry);
    row->append(*genSel);
    row->append(*chanSel);
    row->append(*addBtn);
    selBox_->append(*row);
}
