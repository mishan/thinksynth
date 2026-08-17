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
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <gtkmm.h>

#include "think.h"

#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"
#include "thcGenEdit.h"
#include "PianoRoll.h"
#include "Dialogs.h"
#include "gthSignal.h"
#include "ComposerWindow.h"

/* ---- little local helpers --------------------------------------------- */

static bool
copyOver (const std::string &from, const std::string &to)
{
    std::error_code ec;

    std::filesystem::copy_file(from, to,
        std::filesystem::copy_options::overwrite_existing, ec);

    return !ec;
}

/* What kind of right-hand side an authored value is. */
struct ValueShape
{
    enum Kind { NUMBER, KNOB, QUOTED, WORD } kind;

    double num;
    std::string unit;        /* "", "s", "ms", "beats"                  */
    std::string text;        /* knob name / string body / bare word     */
};

static ValueShape
shapeOf (const std::string &valueText)
{
    ValueShape v;

    v.num = 0;

    if (!valueText.empty() && valueText[0] == '@')
    {
        v.kind = ValueShape::KNOB;
        v.text = valueText.substr(1);
        return v;
    }

    if (!valueText.empty() && valueText[0] == '"')
    {
        v.kind = ValueShape::QUOTED;
        v.text = valueText.substr(1, valueText.size() > 1
                                     ? valueText.size() - 2 : 0);
        return v;
    }

    if (!valueText.empty() &&
        ((valueText[0] >= '0' && valueText[0] <= '9') ||
         valueText[0] == '-' || valueText[0] == '.'))
    {
        v.kind = ValueShape::NUMBER;
        v.num = atof(valueText.c_str());

        size_t sp = valueText.find_first_of(" \t");

        if (sp != std::string::npos)
        {
            std::string u = valueText.substr(
                valueText.find_first_not_of(" \t", sp));

            v.unit = u == "b" ? "beats" : u;
        }

        return v;
    }

    v.kind = ValueShape::WORD;
    v.text = valueText;

    return v;
}

/* "53,56,60" -> "F3 Ab3 C4", for showing a resolved default as notes. */
static std::string
intsToNotes (const std::string &ints)
{
    std::string out;
    const char *s = ints.c_str();

    while (s && *s)
    {
        std::string n = thcGenLoader::noteName(atoi(s));

        if (!n.empty())
        {
            if (!out.empty())
                out += " ";
            out += n;
        }

        s = strchr(s, ',');

        if (s)
            s++;
    }

    return out;
}

/* ---- construction ----------------------------------------------------- */

ComposerWindow::ComposerWindow (thSynth *synth)
    : synth_(synth), dirty_(false), reloadPending_(false),
      tempoGuard_(false)
{
    saveBtn_ = NULL;
    saveAsBtn_ = NULL;
    newBtn_ = NULL;
    openBtn_ = NULL;
    selBox_ = NULL;
    kbdBtn_ = NULL;

    set_title("thinksynth - Composer");
    set_default_size(1060, 640);

    sched_ = new thcScheduler(synth_);

    loadComposers();

    set_child(vbox_);

    playBtn_ = manage(new Gtk::Button("Play"));
    pauseBtn_ = manage(new Gtk::Button("Pause"));
    rewindBtn_ = manage(new Gtk::Button("Rewind"));
    reloadBtn_ = manage(new Gtk::Button("Revert"));
    editBtn_ = manage(new Gtk::ToggleButton("Edit"));

    playBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &ComposerWindow::onPlay));
    pauseBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &ComposerWindow::onPause));
    rewindBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &ComposerWindow::onRewind));
    reloadBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &ComposerWindow::onReload));
    editBtn_->signal_toggled().connect(
        sigc::mem_fun(*this, &ComposerWindow::onEditToggle));

    reloadBtn_->set_tooltip_text("Throw away unsaved edits and reload "
                                 "the piece from its file");

    tempoLbl_ = manage(new Gtk::Label("Tempo"));
    tempoVal_ = Gtk::Adjustment::create(120, 20, 300, 1, 10);
    tempoBtn_ = manage(new Gtk::SpinButton(tempoVal_));
    tempoVal_->signal_value_changed().connect(
        sigc::mem_fun(*this, &ComposerWindow::onTempo));

    status_ = manage(new Gtk::Label(""));
    status_->set_hexpand(true);
    status_->set_xalign(1.0);
    status_->set_ellipsize(Pango::EllipsizeMode::END);

    bar_.set_spacing(6);
    bar_.set_margin(6);
    bar_.append(*playBtn_);
    bar_.append(*pauseBtn_);
    bar_.append(*rewindBtn_);
    bar_.append(*reloadBtn_);
    bar_.append(*tempoLbl_);
    bar_.append(*tempoBtn_);
    bar_.append(*editBtn_);

    kbdBtn_ = manage(new Gtk::ToggleButton("Kbd input"));
    kbdBtn_->set_tooltip_text("Feed the on-screen Keyboard window into "
                              "chains with MIDI input, alongside "
                              "hardware MIDI");
    kbdBtn_->signal_toggled().connect(
        sigc::mem_fun(*this, &ComposerWindow::onKbdToggle));
    bar_.append(*kbdBtn_);

    bar_.append(*status_);

    vbox_.append(bar_);

    /* Playing side: knobs, the node canvas, the roll. The canvas is the
       piece's face whether or not the Edit panel is open -- the tier-two
       visualizers live inside its stage boxes now, where the old draw
       strip used to be a row of orphans. */
    knobBar_.set_spacing(12);
    knobBar_.set_margin_start(6);
    knobBar_.set_margin_end(6);
    playSide_.append(knobBar_);

    canvas_ = manage(new ComposerCanvas());
    canvas_->sigSelection.connect(
        sigc::mem_fun(*this, &ComposerWindow::onCanvasSelection));
    canvas_->sigMoveStage.connect(
        sigc::mem_fun(*this, &ComposerWindow::onCanvasMoveStage));

    canvasScroll_.set_child(*canvas_);
    canvasScroll_.set_policy(Gtk::PolicyType::AUTOMATIC,
                             Gtk::PolicyType::AUTOMATIC);
    canvasScroll_.set_propagate_natural_height(true);
    canvasScroll_.set_propagate_natural_width(true);
    canvasScroll_.set_max_content_height(300);
    playSide_.append(canvasScroll_);

    roll_ = manage(new PianoRoll(sched_));
    roll_->set_vexpand(true);
    playSide_.append(*roll_);

    /* The editor rides in a paned so the roll stays visible while
       editing -- watching the piece change is the point. */
    editorScroll_.set_child(editorBox_);
    editorScroll_.set_policy(Gtk::PolicyType::NEVER,
                             Gtk::PolicyType::AUTOMATIC);
    editorScroll_.set_visible(false);

    editorBox_.set_margin(6);
    editorBox_.set_spacing(6);

    paned_.set_start_child(editorScroll_);
    paned_.set_end_child(playSide_);
    paned_.set_resize_start_child(false);
    paned_.set_shrink_start_child(false);
    paned_.set_position(400);
    paned_.set_vexpand(true);

    vbox_.append(paned_);

    drawTimer_ = Glib::signal_timeout().connect(
        sigc::mem_fun(*this, &ComposerWindow::onDrawTimer), 50);

    /* Live MIDI into the chains: the same m_sigNoteOn/Off hop that
       lights the on-screen keyboard, already on the GUI thread. A press
       has no known length, so it goes in held (duration 0) and the
       release follows as a NOTEOFF; every chain that declared `input
       midi' on that channel hears both. */
    midiOnConn_ = m_sigNoteOn.connect(
        sigc::mem_fun(*this, &ComposerWindow::injectOn));
    midiOffConn_ = m_sigNoteOff.connect(
        sigc::mem_fun(*this, &ComposerWindow::injectOff));

    loadPiece();
}

ComposerWindow::~ComposerWindow (void)
{
    drawTimer_.disconnect();
    midiOnConn_.disconnect();
    midiOffConn_.disconnect();
    kbdOnConn_.disconnect();
    kbdOffConn_.disconnect();

    /* Order matters: the scheduler's destructor flushes note-offs and
       destroys chain instances, which calls back into the plugins -- so
       the plugins must still be loaded when it runs. */
    delete sched_;

    for (std::map<std::string, thcPlugin *>::iterator i = composers_.begin();
         i != composers_.end(); ++i)
        delete i->second;

    if (!workPath_.empty())
        ::remove(workPath_.c_str());
}

/* Same walk NodeEditor does over visual/, one directory over. */
void
ComposerWindow::loadComposers (void)
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
            fprintf(stderr, "ComposerWindow: two composer modules both "
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
ComposerWindow::ensureWork (void)
{
    if (!workPath_.empty())
        return true;

    workPath_ = thUtil::tempFile("thinksynth-gen-");

    return !workPath_.empty();
}

void
ComposerWindow::loadPiece (void)
{
    if (genPath_.empty())
        genPath_ = thUtil::findDataFile("airports.gen", "gen",
                                        "THINK_GEN_PATH", "");

    if (!ensureWork())
    {
        pieceLabel_ = "could not create a working file";
        updateTransportButtons();
        return;
    }

    if (genPath_.empty() || !copyOver(genPath_, workPath_))
    {
        /* No piece to load is a blank page, not an error: New starts
           here too. */
        std::ofstream out(workPath_.c_str(), std::ios::trunc);

        out << "name \"Untitled\";\n";
        genPath_.clear();
    }

    setDirty(false);
    parseWork();
}

void
ComposerWindow::parseWork (void)
{
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
    }

    std::string why;

    if (thcGenEdit::describe(workPath_, doc_, why) != thcGenEdit::OK)
        doc_ = thcGenEdit::Doc();

    tempoGuard_ = true;
    tempoVal_->set_value(sched_->tempo());
    tempoGuard_ = false;

    rebuildKnobs();
    canvas_->SetPiece(&doc_, sched_);
    rebuildEditor();
    updateTransportButtons();
}

void
ComposerWindow::structuralReload (void)
{
    scheduleReload(true);
}

void
ComposerWindow::scheduleReload (bool markDirty)
{
    if (reloadPending_)
        return;

    reloadPending_ = true;

    Glib::signal_idle().connect_once(
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
        });
}

bool
ComposerWindow::editOk (thcGenEdit::Result r, const std::string &why)
{
    if (r == thcGenEdit::OK)
        return true;

    status_->set_text(why.empty() ? thcGenEdit::resultText(r) : why);

    return false;
}

/* ---- transport & top-bar actions -------------------------------------- */

void
ComposerWindow::onPlay (void)
{
    sched_->start();
    updateTransportButtons();
}

void
ComposerWindow::onPause (void)
{
    sched_->stop();
    updateTransportButtons();
}

void
ComposerWindow::onRewind (void)
{
    sched_->reset();
    updateTransportButtons();
}

void
ComposerWindow::onReload (void)
{
    /* Revert: recopy the source over the work file and reload. Through
       the idle path like everything else, so the editor panel is not
       torn down under whatever asked for this.

       With no source at all it retries discovery instead -- a piece
       may have appeared (or THINK_GEN_PATH been pointed somewhere
       real) since startup, and restarting the program is not a reload
       button. */
    if (genPath_.empty())
        genPath_ = thUtil::findDataFile("airports.gen", "gen",
                                        "THINK_GEN_PATH", "");

    if (!genPath_.empty() && ensureWork())
        copyOver(genPath_, workPath_);

    scheduleReload(false);
}

void
ComposerWindow::onTempo (void)
{
    if (tempoGuard_)
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
ComposerWindow::onEditToggle (void)
{
    editorScroll_.set_visible(editBtn_->get_active());

    if (editBtn_->get_active())
        rebuildEditor();
}

void
ComposerWindow::onSave (void)
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
ComposerWindow::onSaveAs (void)
{
    Gtk::FileChooserDialog *dialog = new Gtk::FileChooserDialog(
        *this, "Save piece as", Gtk::FileChooser::Action::SAVE);

    dialog->add_button("_Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("_Save", Gtk::ResponseType::OK);
    dialog->set_modal(true);
    dialog->set_current_name(doc_.name.empty() ? "untitled.gen"
                                               : doc_.name + ".gen");

    dialog->signal_response().connect(
        sigc::bind(sigc::mem_fun(*this, &ComposerWindow::onSaveAsResponse),
                   dialog));

    dialog->set_visible(true);
}

void
ComposerWindow::onSaveAsResponse (int response, Gtk::FileChooserDialog *dialog)
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
    confirmOverwrite(this, path,
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
ComposerWindow::confirmDiscard (const sigc::slot<void ()> &done)
{
    if (!dirty_)
    {
        done();
        return;
    }

    Gtk::MessageDialog *dlg = new Gtk::MessageDialog(
        *this, "Throw away unsaved edits?", false,
        Gtk::MessageType::QUESTION, Gtk::ButtonsType::YES_NO, true);

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
ComposerWindow::onOpen (void)
{
    confirmDiscard(sigc::mem_fun(*this, &ComposerWindow::onOpenConfirmed));
}

void
ComposerWindow::onOpenConfirmed (void)
{
    Gtk::FileChooserDialog *dialog = new Gtk::FileChooserDialog(
        *this, "Open piece", Gtk::FileChooser::Action::OPEN);

    dialog->add_button("_Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("_Open", Gtk::ResponseType::OK);
    dialog->set_modal(true);

    Glib::RefPtr<Gtk::FileFilter> filter = Gtk::FileFilter::create();

    filter->set_name("Pieces (*.gen)");
    filter->add_pattern("*.gen");
    dialog->add_filter(filter);

    Glib::RefPtr<Gtk::FileFilter> all = Gtk::FileFilter::create();

    all->set_name("All files");
    all->add_pattern("*");
    dialog->add_filter(all);

    /* Start where the pieces live: beside the one that is open, or in
       the gen/ data directory the default piece came from. */
    std::string folder = !genPath_.empty()
        ? std::filesystem::path(genPath_).parent_path().string()
        : thUtil::findDataDir("gen", "THINK_GEN_PATH", "");

    if (!folder.empty())
    {
        std::error_code ec;

        (void)ec;
        dialog->set_current_folder(Gio::File::create_for_path(folder));
    }

    dialog->signal_response().connect(
        sigc::bind(sigc::mem_fun(*this, &ComposerWindow::onOpenResponse),
                   dialog));

    dialog->set_visible(true);
}

void
ComposerWindow::onOpenResponse (int response, Gtk::FileChooserDialog *dialog)
{
    std::string path;

    if (response == (int)Gtk::ResponseType::OK)
        path = chosenPath(*dialog);

    closeDialog(dialog);

    if (path.empty())
        return;

    if (!ensureWork() || !copyOver(path, workPath_))
    {
        status_->set_text("could not read " + path);
        return;
    }

    genPath_ = path;
    scheduleReload(false);
}

void
ComposerWindow::onNew (void)
{
    confirmDiscard(
        [this]
        {
            genPath_.clear();

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
ComposerWindow::setDirty (bool dirty)
{
    dirty_ = dirty;

    if (saveBtn_ != NULL)
        saveBtn_->set_sensitive(dirty_ || genPath_.empty());

    updateTransportButtons();
}

void
ComposerWindow::updateTransportButtons (void)
{
    bool have = sched_->chainCount() > 0;

    playBtn_->set_sensitive(have && !sched_->running());
    pauseBtn_->set_sensitive(have && sched_->running());
    rewindBtn_->set_sensitive(have);
    /* Never disabled: with no piece found it is the "look again"
       button, which is the one moment it is most wanted. */
    reloadBtn_->set_sensitive(true);

    std::string text = pieceLabel_;

    if (dirty_)
        text += " (edited)";

    if (!have && composers_.empty())
        text = "no composer modules found in " + composerRoot_;
    else if (sched_->running())
        text += " — playing";

    status_->set_text(text);
}

/* ---- the playing side ------------------------------------------------- */

void
ComposerWindow::rebuildKnobs (void)
{
    while (Gtk::Widget *child = knobBar_.get_first_child())
        knobBar_.remove(*child);

    const std::map<std::string, thArg *> &knobs = sched_->knobs();

    for (std::map<std::string, thArg *>::const_iterator i = knobs.begin();
         i != knobs.end(); ++i)
    {
        thArg *arg = i->second;
        std::string name = i->first;

        Gtk::Label *lbl = manage(new Gtk::Label(
            arg->label().empty() ? name : arg->label()));

        float lo = arg->min(), hi = arg->max();

        if (hi <= lo)
        {
            lo = 0;
            hi = 1;
        }

        Gtk::Scale *scale = manage(new Gtk::Scale(
            Gtk::Adjustment::create((*arg)[0], lo, hi,
                                    (hi - lo) / 100.0),
            Gtk::Orientation::HORIZONTAL));

        scale->set_size_request(150, -1);
        scale->set_draw_value(true);
        scale->set_digits(2);

        /* Live and an edit, like the tempo spin: the drag reaches every
           bound param now, and the file remembers where it ended up. */
        scale->signal_value_changed().connect(
            [this, arg, scale, name]
            {
                arg->setValue((float)scale->get_value());

                std::string why;

                if (thcGenEdit::setKnobValue(workPath_, name,
                                             scale->get_value(), why) ==
                    thcGenEdit::OK)
                    setDirty(true);
            });

        knobBar_.append(*lbl);
        knobBar_.append(*scale);
    }

    knobBar_.set_visible(!knobs.empty());
}

/* One slow clock repaints the canvas so the inline composer_draws stay
 * live; 50ms is plenty for a euclid ring, and the piano roll keeps its
 * own frame clock. */
bool
ComposerWindow::onDrawTimer (void)
{
    if (canvas_ != NULL)
        canvas_->queue_draw();

    return true;
}

void
ComposerWindow::injectOn (int chan, float note, float veloc)
{
    thcEvent ev = {};

    ev.type = THC_EV_NOTE;
    ev.at = sched_->now();
    ev.channel = chan;
    ev.u.note.note = (int)note;
    ev.u.note.velocity = (int)veloc;
    ev.u.note.duration = 0;

    sched_->injectMidiEvent(ev);
}

void
ComposerWindow::injectOff (int chan, float note)
{
    thcEvent ev = {};

    ev.type = THC_EV_NOTEOFF;
    ev.at = sched_->now();
    ev.channel = chan;
    ev.u.note.note = (int)note;

    sched_->injectMidiEvent(ev);
}

/* The on-screen keyboard as a performance input, if wished for: the
 * same two handlers, fed from the pair the Keyboard window emits. A
 * toggle rather than always-on because the keyboard is also the tool
 * for auditioning patches, and auditioning through an arpeggiator you
 * forgot about is a confusing five minutes. */
void
ComposerWindow::onKbdToggle (void)
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
            sigc::mem_fun(*this, &ComposerWindow::injectOn));
        kbdOffConn_ = m_sigKbdNoteOff.connect(
            sigc::mem_fun(*this, &ComposerWindow::injectOff));
    }
    else
    {
        kbdOnConn_.disconnect();
        kbdOffConn_.disconnect();
    }
}

void
ComposerWindow::onCanvasSelection (const ComposerCanvas::Selection &)
{
    rebuildSelection();
}

void
ComposerWindow::onCanvasMoveStage (size_t chain, int from, int to)
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
ComposerWindow::liveStage (size_t ci, size_t si)
{
    thcChain *c = sched_->chain(ci);

    if (c == NULL || si >= c->stages.size())
        return NULL;

    return c->stages[si].get();
}

std::vector<std::pair<std::string, std::string> >
ComposerWindow::defaultParams (const thcPlugin *plugin)
{
    /* Every registered param, spelled out -- a .gen should survive a
       plugin's defaults changing; this is the lesson of noargs/. */
    std::vector<std::pair<std::string, std::string> > out;

    for (int i = 0; i < plugin->paramCount(); i++)
    {
        const thcPlugin::ParamInfo *p = plugin->paramInfo(i);
        std::string v;

        /* Every enumerator named, and no `default:'.
         *
         * This switch had one, and a param type added years after it was
         * written fell through to the numeric case and wrote `from = 0;'
         * -- a generated stage the loader then refused. -Wall's -Wswitch
         * only fires on a switch that covers the enum and misses a value,
         * so a default label is exactly what buys the silence. Spelling
         * the numeric types out costs three lines and turns the next
         * addition into a compiler warning instead of a bug report. */
        switch (p->type)
        {
            case THC_PARAM_NOTESET:
                v = "\"" + intsToNotes(p->defString) + "\"";
                break;
            case THC_PARAM_STRING:
                v = "\"" + p->defString + "\"";
                break;

            case THC_PARAM_PRESET:
                /* The one param that gets left out, and the rule above is
                   why rather than in spite of. "Write every param" exists
                   so a piece survives a plugin's *defaults* changing -- a
                   preset param has no default that can be written at all,
                   because the only legal value is the name of something
                   this piece declares, and a new stage cannot know one.
                   Falling through to the numeric case would write `from =
                   0;', which the loader rejects by name and line: a
                   generated stage that will not load is worse than an
                   absent line the loader is happy to default. */
                continue;

            case THC_PARAM_FLOAT:
            case THC_PARAM_INT:
            case THC_PARAM_NOTE:
                thcGenEdit::format(p->def, v);

                if (p->isDuration())
                    v += " s";
                break;
        }

        out.push_back(std::make_pair(p->name, v));
    }

    return out;
}

void
ComposerWindow::applyParam (size_t ci, size_t si, const std::string &param,
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

    /* And poke the live piece so the edit is audible now. */
    thcStage *s = liveStage(ci, si);

    if (s == NULL)
        return;

    int idx = s->plugin->paramIndex(param);

    if (idx < 0)
        return;

    ValueShape v = shapeOf(valueText);

    switch (v.kind)
    {
        case ValueShape::KNOB:
            /* A knob reads as plain seconds; a beats flag left over
               from "2 beats" would tempo-scale it, which is not what
               either spelling says -- and not what a reload would do. */
            s->params.setBeats(idx, false);
            sched_->bindKnob(s, idx, sched_->knob(v.text));
            break;

        case ValueShape::NUMBER:
        {
            sched_->bindKnob(s, idx, NULL);

            double stored = v.unit == "ms" ? v.num / 1000.0 : v.num;

            s->params.setBeats(idx, v.unit == "beats");
            s->params.set(idx, stored);
            break;
        }

        case ValueShape::QUOTED:
        {
            /* A binding shadows the stored value; without this unbind
               the new notes would be set and never heard. */
            sched_->bindKnob(s, idx, NULL);

            const thcPlugin::ParamInfo *pi = s->plugin->paramInfo(idx);

            if (pi->type == THC_PARAM_NOTESET ||
                pi->type == THC_PARAM_NOTE)
            {
                std::vector<int> notes;
                std::string bad;

                if (thcGenLoader::parseNoteList(v.text, notes, bad))
                {
                    if (pi->type == THC_PARAM_NOTE && notes.size() == 1)
                        s->params.set(idx, notes[0]);
                    else
                    {
                        std::ostringstream ints;

                        for (size_t i = 0; i < notes.size(); i++)
                            ints << (i ? "," : "") << notes[i];

                        s->params.setString(idx, ints.str());
                    }
                }
            }
            else
                s->params.setString(idx, v.text);
            break;
        }

        case ValueShape::WORD:
        {
            /* A scale's name or a preset's -- the two kinds of named
               object a param can refer to, spelled identically. Which
               one it is comes from the param's type, not from the word.
               And the same unbind QUOTED needs, for the same shadowing
               reason. */
            sched_->bindKnob(s, idx, NULL);

            const thcPlugin::ParamInfo *pi = s->plugin->paramInfo(idx);

            if (pi != NULL && pi->type == THC_PARAM_PRESET)
            {
                for (size_t i = 0; i < doc_.presets.size(); i++)
                {
                    if (doc_.presets[i].name != v.text)
                        continue;

                    /* The same "name=value,..." the loader hands a
                       plugin, built the same way, so a preset changed
                       through the panel and one read from the file are
                       indistinguishable to the composer. */
                    std::ostringstream vec;

                    for (size_t k = 0;
                         k < doc_.presets[i].values.size(); k++)
                    {
                        std::string num;

                        thcGenEdit::format(doc_.presets[i].values[k].value,
                                           num);

                        vec << (k ? "," : "")
                            << doc_.presets[i].values[k].name << "=" << num;
                    }

                    s->params.setString(idx, vec.str());
                }

                break;
            }

            for (size_t i = 0; i < doc_.scales.size(); i++)
                if (doc_.scales[i].name == v.text)
                {
                    std::vector<int> notes;
                    std::string bad;

                    if (thcGenLoader::parseNoteList(doc_.scales[i].notes,
                                                    notes, bad))
                    {
                        std::ostringstream ints;

                        for (size_t j = 0; j < notes.size(); j++)
                            ints << (j ? "," : "") << notes[j];

                        s->params.setString(idx, ints.str());
                    }
                }
            break;
        }
    }
}

void
ComposerWindow::rebuildEditor (void)
{
    /* The buttons below die with their row; forgetting that here left
       setDirty poking freed widgets whenever the panel was hidden.
     *
     * kbdBtn_ is deliberately *not* in this list, and used to be. It
       lives in the toolbar, which nothing here clears, so nulling it
       forgot a widget that was still on screen and still connected --
       and the next click on Kbd input reached onKbdToggle, which
       dereferenced the null and took the program with it. A pointer
       cleared here has to be one the loop below actually destroys. */
    saveBtn_ = NULL;
    saveAsBtn_ = NULL;
    newBtn_ = NULL;
    openBtn_ = NULL;
    selBox_ = NULL;

    while (Gtk::Widget *child = editorBox_.get_first_child())
        editorBox_.remove(*child);

    if (!editBtn_->get_active())
        return;

    /* File row: New / Save / Save As. */
    Gtk::Box *fileRow = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 6));

    newBtn_ = manage(new Gtk::Button("New"));
    openBtn_ = manage(new Gtk::Button("Open"));
    saveBtn_ = manage(new Gtk::Button("Save"));
    saveAsBtn_ = manage(new Gtk::Button("Save As"));

    newBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &ComposerWindow::onNew));
    openBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &ComposerWindow::onOpen));
    saveBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &ComposerWindow::onSave));
    saveAsBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &ComposerWindow::onSaveAs));

    saveBtn_->set_sensitive(dirty_ || genPath_.empty());

    fileRow->append(*newBtn_);
    fileRow->append(*openBtn_);
    fileRow->append(*saveBtn_);
    fileRow->append(*saveAsBtn_);
    editorBox_.append(*fileRow);

    editorBox_.append(*buildPieceSection());
    editorBox_.append(*buildKnobsSection());
    editorBox_.append(*buildScalesSection());
    editorBox_.append(*buildPresetsSection());

    /* The lower half follows the canvas: whatever is selected up there
       is editable down here. */
    editorBox_.append(*manage(new Gtk::Separator(
        Gtk::Orientation::HORIZONTAL)));

    selBox_ = manage(new Gtk::Box(Gtk::Orientation::VERTICAL, 4));
    editorBox_.append(*selBox_);

    rebuildSelection();
}

void
ComposerWindow::rebuildSelection (void)
{
    if (selBox_ == NULL)
        return;

    while (Gtk::Widget *child = selBox_->get_first_child())
        selBox_->remove(*child);

    if (!editBtn_->get_active())
        return;

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

Gtk::Widget *
ComposerWindow::buildPieceSection (void)
{
    Gtk::Expander *exp = manage(new Gtk::Expander("Piece"));
    Gtk::Grid *grid = manage(new Gtk::Grid());

    grid->set_column_spacing(6);
    grid->set_row_spacing(4);
    grid->set_margin(4);

    const char *keys[3] = { "name", "author", "description" };
    const std::string *vals[3] = { &doc_.name, &doc_.author,
                                   &doc_.description };

    for (int i = 0; i < 3; i++)
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

    grid->attach(*pin, 0, 3);
    grid->attach(*seedSpin, 1, 3);

    exp->set_child(*grid);
    exp->set_expanded(false);

    return exp;
}

Gtk::Widget *
ComposerWindow::buildKnobsSection (void)
{
    Gtk::Expander *exp = manage(new Gtk::Expander("Knobs"));
    Gtk::Grid *grid = manage(new Gtk::Grid());

    grid->set_column_spacing(6);
    grid->set_row_spacing(4);
    grid->set_margin(4);

    int row = 0;

    for (size_t i = 0; i < doc_.knobs.size(); i++)
    {
        const thcGenEdit::Knob &k = doc_.knobs[i];
        std::string name = k.name;

        Gtk::Label *lbl = manage(new Gtk::Label("@" + name));

        lbl->set_xalign(0);

        Gtk::SpinButton *minSpin = manage(new Gtk::SpinButton(
            Gtk::Adjustment::create(k.min, -100000, 100000, 0.01), 0, 3));
        Gtk::SpinButton *maxSpin = manage(new Gtk::SpinButton(
            Gtk::Adjustment::create(k.max, -100000, 100000, 0.01), 0, 3));
        Gtk::Entry *lblEntry = manage(new Gtk::Entry());
        Gtk::Button *rm = manage(new Gtk::Button("Remove"));

        lblEntry->set_text(k.label);
        lblEntry->set_placeholder_text("label");
        lblEntry->set_max_width_chars(10);

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

        grid->attach(*lbl, 0, row);
        grid->attach(*minSpin, 1, row);
        grid->attach(*maxSpin, 2, row);
        grid->attach(*lblEntry, 3, row);
        grid->attach(*rm, 4, row);
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

    grid->attach(*newName, 0, row, 2, 1);
    grid->attach(*add, 2, row);

    exp->set_child(*grid);
    exp->set_expanded(!doc_.knobs.empty());

    return exp;
}

Gtk::Widget *
ComposerWindow::buildScalesSection (void)
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
ComposerWindow::buildPresetsSection (void)
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
ComposerWindow::presetChanged (const std::string &preset)
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

void
ComposerWindow::buildChainSelection (size_t ci)
{
    const thcGenEdit::Chain &chain = doc_.chains[ci];
    std::string chainName = chain.name;

    selBox_->append(*manage(new Gtk::Label("chain " + chainName)));

    Gtk::Box *head = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 6));
    Gtk::Entry *nameEntry = manage(new Gtk::Entry());
    Gtk::CheckButton *mute = manage(new Gtk::CheckButton("mute"));
    Gtk::CheckButton *input = manage(new Gtk::CheckButton("MIDI in"));
    Gtk::Button *rm = manage(new Gtk::Button("Remove chain"));

    nameEntry->set_text(chainName);
    nameEntry->set_max_width_chars(12);
    nameEntry->set_tooltip_text("Enter renames");

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
                structuralReload();
        });

    mute->signal_toggled().connect(
        [this, ci, mute]
        {
            sched_->setMuted(ci, mute->get_active());
            canvas_->queue_draw();
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
    head->append(*mute);
    head->append(*input);
    selBox_->append(*head);
    selBox_->append(*rm);
}

void
ComposerWindow::buildStageSelection (size_t ci, size_t si)
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
        Gtk::Grid *grid = manage(new Gtk::Grid());

        grid->set_column_spacing(6);
        grid->set_row_spacing(2);

        for (int pi = 0; pi < found->second->paramCount(); pi++)
            addParamRow(grid, pi, ci, si, found->second, pi);

        selBox_->append(*grid);
    }
    else
        selBox_->append(*manage(new Gtk::Label(
            "module '" + stage.plugin + "' is not installed")));

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

void
ComposerWindow::buildSinkSelection (size_t ci, size_t ki)
{
    const thcGenEdit::Chain &chain = doc_.chains[ci];
    std::string chainName = chain.name;
    int sinkIndex = (int)ki;

    selBox_->append(*manage(new Gtk::Label(chainName + " · sink")));

    Gtk::Box *row = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 6));
    Gtk::SpinButton *chan = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(chain.sinks[ki].channel, 1, 16, 1)));
    Gtk::Entry *arg = manage(new Gtk::Entry());
    Gtk::Button *rm = manage(new Gtk::Button("Remove"));

    chan->set_tooltip_text("MIDI channel, 1-16 -- the number on "
                           "the main window's patch tab");
    arg->set_text(chain.sinks[ki].chanarg);
    arg->set_placeholder_text("chanarg (empty: notes)");
    arg->set_max_width_chars(12);
    rm->set_sensitive(chain.sinks.size() > 1);

    auto applySink = [this, chainName, sinkIndex, chan, arg]
    {
        std::string why;

        if (editOk(thcGenEdit::setSink(workPath_, chainName, sinkIndex,
                chan->get_value_as_int(), arg->get_text(), why), why))
            structuralReload();
    };

    chan->signal_value_changed().connect(applySink);
    arg->signal_activate().connect(applySink);

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
ComposerWindow::buildAddStage (size_t ci)
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
ComposerWindow::buildAddSink (size_t ci)
{
    std::string chainName = doc_.chains[ci].name;

    selBox_->append(*manage(new Gtk::Label(
        "add a sink to " + chainName)));

    Gtk::Box *row = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 6));
    Gtk::SpinButton *chan = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(1, 1, 16, 1)));
    Gtk::Entry *arg = manage(new Gtk::Entry());
    Gtk::Button *add = manage(new Gtk::Button("Add sink"));

    chan->set_tooltip_text("MIDI channel, 1-16 -- the number on "
                           "the main window's patch tab");
    arg->set_placeholder_text("chanarg (empty: notes)");
    arg->set_max_width_chars(12);

    add->signal_clicked().connect(
        [this, chainName, chan, arg]
        {
            std::string why;

            if (editOk(thcGenEdit::addSink(workPath_, chainName,
                    chan->get_value_as_int(), arg->get_text(), why), why))
                structuralReload();
        });

    row->append(*chan);
    row->append(*arg);
    row->append(*add);
    selBox_->append(*row);
}

void
ComposerWindow::buildAddChain (void)
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
        Gtk::Adjustment::create(0, 0, 15, 1)));
    Gtk::Button *addBtn = manage(new Gtk::Button("Add chain"));

    addBtn->signal_clicked().connect(
        [this, nameEntry, genSel, chanSel, genNames]
        {
            if (genNames.empty())
                return;

            guint sel = genSel->get_selected();

            if (sel >= genNames.size())
                sel = 0;

            thcPlugin *plugin = composers_[genNames[sel]];
            std::string why;

            if (editOk(thcGenEdit::addChain(workPath_,
                    nameEntry->get_text(), chanSel->get_value_as_int(),
                    "src", "gen", plugin->name(),
                    defaultParams(plugin), why), why))
                structuralReload();
        });

    row->append(*nameEntry);
    row->append(*genSel);
    row->append(*chanSel);
    row->append(*addBtn);
    selBox_->append(*row);
}

void
ComposerWindow::addParamRow (Gtk::Grid *grid, int row, size_t ci, size_t si,
                             const thcPlugin *plugin, int paramIndex)
{
    const thcPlugin::ParamInfo *pi = plugin->paramInfo(paramIndex);
    const thcGenEdit::Stage &stage = doc_.chains[ci].stages[si];

    /* The authored text, or the default's spelling for a line the file
       does not have. */
    std::string authored;
    bool inFile = false;

    for (size_t i = 0; i < stage.params.size(); i++)
        if (stage.params[i].name == pi->name)
        {
            authored = stage.params[i].valueText;
            inFile = true;
        }

    if (!inFile)
    {
        std::vector<std::pair<std::string, std::string> > defs =
            defaultParams(plugin);

        authored = defs[paramIndex].second;
    }

    ValueShape v = shapeOf(authored);

    Gtk::Label *lbl = manage(new Gtk::Label(pi->name));

    lbl->set_xalign(0);

    if (!pi->desc.empty())
        lbl->set_tooltip_text(pi->desc);

    grid->attach(*lbl, 0, row);

    std::string paramName = pi->name;

    if (pi->type == THC_PARAM_FLOAT || pi->type == THC_PARAM_INT)
    {
        bool isInt = pi->type == THC_PARAM_INT;

        /* Wide bounds rather than the declared ones when a unit scales
           the number: 600 seconds of period is 600000 ms. */
        Gtk::SpinButton *spin = manage(new Gtk::SpinButton(
            Gtk::Adjustment::create(v.kind == ValueShape::NUMBER ? v.num
                                                                 : pi->def,
                                    isInt ? pi->min : -1e6,
                                    isInt ? pi->max : 1e6,
                                    isInt ? 1 : 0.1),
            0, isInt ? 0 : 3));

        Gtk::DropDown *unitSel = NULL;
        static const char *unitNames[3] = { "s", "ms", "beats" };

        if (pi->isDuration())
        {
            std::vector<Glib::ustring> units;

            units.push_back("s");
            units.push_back("ms");
            units.push_back("beats");
            unitSel = manage(new Gtk::DropDown(units));

            guint u = 0;

            if (v.unit == "ms")
                u = 1;
            else if (v.unit == "beats")
                u = 2;

            unitSel->set_selected(u);
        }

        /* The binding: a plain value, or any of the piece's knobs. */
        std::vector<Glib::ustring> bindShown;
        std::vector<std::string> bindNames;

        bindShown.push_back("(value)");

        for (size_t i = 0; i < doc_.knobs.size(); i++)
        {
            bindShown.push_back("@" + doc_.knobs[i].name);
            bindNames.push_back(doc_.knobs[i].name);
        }

        Gtk::DropDown *bindSel = manage(new Gtk::DropDown(bindShown));

        if (v.kind == ValueShape::KNOB)
            for (size_t i = 0; i < bindNames.size(); i++)
                if (bindNames[i] == v.text)
                    bindSel->set_selected((guint)i + 1);

        bool bound = v.kind == ValueShape::KNOB;

        spin->set_sensitive(!bound);

        if (unitSel != NULL)
            unitSel->set_sensitive(!bound);

        auto compose = [this, ci, si, paramName, spin, unitSel, bindSel,
                        bindNames, pi]
        {
            std::string text;

            if (bindSel->get_selected() > 0 &&
                bindSel->get_selected() <= bindNames.size())
                text = "@" + bindNames[bindSel->get_selected() - 1];
            else
            {
                thcGenEdit::format(spin->get_value(), text);

                if (unitSel != NULL)
                    text += std::string(" ") +
                        unitNames[std::min(unitSel->get_selected(),
                                           (guint)2)];
            }

            applyParam(ci, si, paramName, text);
        };

        spin->signal_value_changed().connect(compose);

        if (unitSel != NULL)
            unitSel->property_selected().signal_changed().connect(compose);

        bindSel->property_selected().signal_changed().connect(
            [compose, spin, unitSel, bindSel]
            {
                bool nowBound = bindSel->get_selected() > 0;

                spin->set_sensitive(!nowBound);

                if (unitSel != NULL)
                    unitSel->set_sensitive(!nowBound);

                compose();
            });

        grid->attach(*spin, 1, row);

        if (unitSel != NULL)
            grid->attach(*unitSel, 2, row);

        grid->attach(*bindSel, 3, row);
    }
    else
    {
        /* NOTESET, NOTE, STRING and PRESET: an entry. What the typed
           text becomes is worked out below from the param's type -- a
           NOTESET takes note names or a scale's name, a PRESET takes a
           preset's name and nothing else. */
        Gtk::Entry *entry = manage(new Gtk::Entry());

        entry->set_text(v.kind == ValueShape::QUOTED ? v.text : authored);
        entry->set_hexpand(true);

        if (pi->type == THC_PARAM_NOTESET)
            entry->set_tooltip_text(
                "Note names, or a scale's name; Enter applies");
        else if (pi->type == THC_PARAM_PRESET)
            entry->set_tooltip_text(
                "The name of a preset this piece declares; Enter applies");
        else
            entry->set_tooltip_text("Enter applies");

        thcParamType type = pi->type;

        entry->signal_activate().connect(
            [this, ci, si, paramName, entry, type]
            {
                std::string text = entry->get_text();
                std::string valueText;

                if (type == THC_PARAM_NOTESET)
                {
                    bool isScale = false;

                    for (size_t i = 0; i < doc_.scales.size(); i++)
                        if (doc_.scales[i].name == text)
                            isScale = true;

                    if (isScale)
                        valueText = text;
                    else
                    {
                        std::vector<int> notes;
                        std::string bad;

                        if (!thcGenLoader::parseNoteList(text, notes, bad))
                        {
                            status_->set_text("'" + bad +
                                              "' is not a note name");
                            return;
                        }

                        valueText = "\"" + text + "\"";
                    }
                }
                else if (type == THC_PARAM_PRESET)
                {
                    /* Bare, not quoted. A preset is referred to by name,
                       the way a scale is, and the loader refuses a
                       quoted one on purpose -- a timbre vector spelled
                       inline is a preset that cannot be saved under a
                       name. Checked here rather than left to the load,
                       because the panel is where the person is. */
                    bool ok = !text.empty() &&
                        ((text[0] >= 'a' && text[0] <= 'z') ||
                         (text[0] >= 'A' && text[0] <= 'Z'));

                    for (size_t i = 1; ok && i < text.size(); i++)
                    {
                        const char c = text[i];

                        ok = (c >= 'a' && c <= 'z') ||
                             (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '_';
                    }

                    if (!ok)
                    {
                        status_->set_text("a preset's name, like `warm'");
                        return;
                    }

                    valueText = text;
                }
                else if (type == THC_PARAM_NOTE)
                {
                    std::vector<int> notes;
                    std::string bad;

                    if (!thcGenLoader::parseNoteList(text, notes, bad) ||
                        notes.size() != 1)
                    {
                        status_->set_text("one note name, like C4");
                        return;
                    }

                    valueText = "\"" + text + "\"";
                }
                else
                    valueText = "\"" + text + "\"";

                applyParam(ci, si, paramName, valueText);
            });

        grid->attach(*entry, 1, row, 3, 1);
    }
}
