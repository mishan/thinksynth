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

#include <filesystem>

#include <gtkmm.h>

#include "think.h"

#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"
#include "PianoRoll.h"
#include "ComposerWindow.h"

ComposerWindow::ComposerWindow (thSynth *synth)
    : synth_(synth)
{
    set_title("thinksynth - Composer");
    set_default_size(760, 460);

    sched_ = new thcScheduler(synth_);

    loadComposers();

    set_child(vbox_);

    playBtn_ = manage(new Gtk::Button("Play"));
    pauseBtn_ = manage(new Gtk::Button("Pause"));
    rewindBtn_ = manage(new Gtk::Button("Rewind"));
    reloadBtn_ = manage(new Gtk::Button("Reload"));

    playBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &ComposerWindow::onPlay));
    pauseBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &ComposerWindow::onPause));
    rewindBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &ComposerWindow::onRewind));
    reloadBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &ComposerWindow::onReload));

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
    bar_.append(*status_);

    vbox_.append(bar_);

    knobBar_.set_spacing(12);
    knobBar_.set_margin_start(6);
    knobBar_.set_margin_end(6);
    vbox_.append(knobBar_);

    drawBar_.set_spacing(6);
    drawBar_.set_margin_start(6);
    drawBar_.set_margin_end(6);
    vbox_.append(drawBar_);

    roll_ = manage(new PianoRoll(sched_));
    roll_->set_vexpand(true);
    vbox_.append(*roll_);

    /* The tier-two visualizers do not need frame-rate; a euclid ring
       changes when a step fires. 50ms is plenty and costs nothing when
       drawAreas_ is empty. */
    drawTimer_ = Glib::signal_timeout().connect(
        sigc::mem_fun(*this, &ComposerWindow::onDrawTimer), 50);

    loadPiece();
}

ComposerWindow::~ComposerWindow (void)
{
    drawTimer_.disconnect();

    /* Order matters: the scheduler's destructor flushes note-offs and
       destroys chain instances, which calls back into the plugins -- so
       the plugins must still be loaded when it runs. */
    delete sched_;

    for (std::map<std::string, thcPlugin *>::iterator i = composers_.begin();
         i != composers_.end(); ++i)
        delete i->second;
}

/* Same walk NodeEditor does over visual/, one directory over. A tree with
 * no composer modules is not an error; the window just says where it
 * looked, because "no composers installed" on its own is the bad message
 * VISUALIZERS.md already paid a debugging session to learn about. */
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
            /* thcPlugin has already said why on stderr. */
            delete p;
            continue;
        }

        if (composers_.find(p->name()) != composers_.end())
        {
            fprintf(stderr, "ComposerWindow: two composer modules both "
                    "called '%s'; keeping %s\n", p->name().c_str(),
                    composers_[p->name()]->path().c_str());
            delete p;
            continue;
        }

        composers_[p->name()] = p;
    }
}

/* The default piece is gen/airports.gen, found through the same search
 * every other data file uses -- so it works from a build tree, an
 * install, a .app and a Flatpak, or is overridden with THINK_GEN_PATH.
 * The hardcoded table this replaces is gone; the file is the piece. */
void
ComposerWindow::loadPiece (void)
{
    if (genPath_.empty())
        genPath_ = thUtil::findDataFile("airports.gen", "gen",
                                        "THINK_GEN_PATH", "");

    if (genPath_.empty())
    {
        pieceLabel_ = "no .gen file found (looked for gen/airports.gen)";
        updateTransportButtons();
        return;
    }

    thcGenLoader loader(composers_);

    if (!loader.load(genPath_, sched_))
    {
        const std::vector<std::string> &errs = loader.errors();

        for (size_t i = 0; i < errs.size(); i++)
            fprintf(stderr, "%s\n", errs[i].c_str());

        pieceLabel_ = errs.empty() ? "load failed" : errs[0];
    }
    else
    {
        std::string name = loader.pieceName().empty()
            ? std::filesystem::path(genPath_).filename().string()
            : loader.pieceName();

        pieceLabel_ = name;

        if (loader.hasSeed())
        {
            char buf[32];

            snprintf(buf, sizeof(buf), " — seed %u", loader.seed());
            pieceLabel_ += buf;
        }
    }

    tempoVal_->set_value(sched_->tempo());

    rebuildKnobs();
    rebuildDrawStrip();
    updateTransportButtons();
}

/* One row of sliders: the piece's @knobs, straight off the thArg
 * metadata the loader stored -- the same widget/min/max/label spellings
 * a .dsp chanarg carries, driving the same kind of control. Dragging one
 * is live: bound params read through the knob on their next tick. */
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

        Gtk::Label *lbl = manage(new Gtk::Label(
            arg->label().empty() ? i->first : arg->label()));

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

        scale->signal_value_changed().connect(
            [arg, scale] { arg->setValue((float)scale->get_value()); });

        knobBar_.append(*lbl);
        knobBar_.append(*scale);
    }

    knobBar_.set_visible(!knobs.empty());
}

/* One small canvas per stage that exports composer_draw. The lambda
 * captures the thcStage, not the instance pointer: reset() swaps the
 * instance under it and the stage always names the current one. */
void
ComposerWindow::rebuildDrawStrip (void)
{
    while (Gtk::Widget *child = drawBar_.get_first_child())
        drawBar_.remove(*child);

    drawAreas_.clear();

    for (size_t ci = 0; ci < sched_->chainCount(); ci++)
    {
        thcChain *c = sched_->chain(ci);

        for (size_t si = 0; si < c->stages.size(); si++)
        {
            thcStage *s = c->stages[si].get();

            if (!s->plugin->hasDraw())
                continue;

            Gtk::DrawingArea *area = manage(new Gtk::DrawingArea());

            area->set_content_width(96);
            area->set_content_height(96);
            area->set_tooltip_text(c->name + " — " + s->plugin->name());

            area->set_draw_func(
                [s](const Cairo::RefPtr<Cairo::Context> &cr,
                    int w, int h)
                {
                    cr->set_source_rgb(0.09, 0.09, 0.11);
                    cr->paint();
                    s->plugin->draw(s->state, cr->cobj(), w, h);
                });

            drawBar_.append(*area);
            drawAreas_.push_back(area);
        }
    }

    drawBar_.set_visible(!drawAreas_.empty());
}

bool
ComposerWindow::onDrawTimer (void)
{
    for (size_t i = 0; i < drawAreas_.size(); i++)
        drawAreas_[i]->queue_draw();

    return true;
}

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
    loadPiece();
}

void
ComposerWindow::onTempo (void)
{
    sched_->setTempo(tempoVal_->get_value());
}

void
ComposerWindow::updateTransportButtons (void)
{
    bool have = sched_->chainCount() > 0;

    playBtn_->set_sensitive(have && !sched_->running());
    pauseBtn_->set_sensitive(have && sched_->running());
    rewindBtn_->set_sensitive(have);
    reloadBtn_->set_sensitive(!genPath_.empty());

    if (!have)
        status_->set_text(pieceLabel_.empty()
            ? "no composer modules found in " + composerRoot_
            : pieceLabel_);
    else if (sched_->running())
        status_->set_text(pieceLabel_ + " — playing");
    else
        status_->set_text(pieceLabel_);
}
