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

/*
 * thinkwasm -- what genwav.mjs holds on to.
 *
 * genwav.mjs is scripts/genwav.cpp for Node. Everything genwav.cpp does with
 * the numbers it gets back -- the loop, the tail, the level summary, the WAV,
 * the tape's spelling -- it does in JavaScript. What it cannot do there is be
 * the synth: the objects genwav.cpp builds on its stack are built here
 * instead, once, and reached through a handful of C functions, since a C name
 * is the one thing both sides of the wasm boundary can spell.
 *
 * The plugins are Emscripten side modules and load the way they do natively:
 * thDynLib::open is dlopen, and dlopen here is Emscripten's. Built with
 * NODERAWFS, a path is the host's path, so -p, the .gen, and the dsp/ search
 * all mean what they mean to the native tool.
 */

#include "config.h"

#include <stdint.h>
#include <stddef.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <emscripten.h>

#include "think.h"

#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"

#include "twevent.h"

static std::map<std::string, thcPlugin *> plugins_;
static thSynth      *synth_;
static thcScheduler *sched_;
static thcGenLoader *loader_;

static sigc::connection conn_;
static twTape           tape_;

/* genwav.cpp's loadComposers, as it stands. */
static void loadComposers (const std::string &pluginDir,
                           std::map<std::string, thcPlugin *> &out)
{
    std::filesystem::path root =
        std::filesystem::path(pluginDir) / "composer";
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
            delete p;
            continue;
        }

        out[p->name()] = p;
    }
}

extern "C" {

/* The composers under pluginPath, and a synth built on the same path.
   Returns how many composers loaded; with none there is nothing else, and
   nothing else should be called. pluginPath ends in a slash. */
EMSCRIPTEN_KEEPALIVE int tw_open (const char *pluginPath)
{
    loadComposers(pluginPath, plugins_);

    if (plugins_.empty())
        return 0;

    synth_  = new thSynth(pluginPath, TH_DEFAULT_WINDOW_LENGTH,
                          TH_DEFAULT_SAMPLES);
    sched_  = new thcScheduler(synth_);
    loader_ = new thcGenLoader(plugins_);

    return (int)plugins_.size();
}

EMSCRIPTEN_KEEPALIVE int tw_load (const char *genFile)
{
    return loader_->load(genFile, sched_) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int tw_error_count (void)
{
    return (int)loader_->errors().size();
}

EMSCRIPTEN_KEEPALIVE const char *tw_error (int k)
{
    return loader_->errors()[k].c_str();
}

EMSCRIPTEN_KEEPALIVE int tw_channels (void)
{
    return synth_->audioChannelCount();
}

EMSCRIPTEN_KEEPALIVE int tw_window (void)
{
    return synth_->getWindowlen();
}

EMSCRIPTEN_KEEPALIVE int tw_rate (void)
{
    return TH_DEFAULT_SAMPLES;
}

/* Listening starts here rather than at tw_open, which is where genwav.cpp
   connects too: after the load, so nothing the load delivers is counted. */
EMSCRIPTEN_KEEPALIVE void tw_start (void)
{
    conn_ = sched_->sigDelivered.connect(
        sigc::mem_fun(tape_, &twTape::deliver));
    sched_->start();
}

/* stop() delivers the note-offs for whatever is still sounding, and only
   then does the listening end. */
EMSCRIPTEN_KEEPALIVE void tw_stop (void)
{
    sched_->stop();
    conn_.disconnect();
}

EMSCRIPTEN_KEEPALIVE double tw_now (void)
{
    return sched_->now();
}

EMSCRIPTEN_KEEPALIVE void tw_step (double dt)
{
    sched_->stepTransport(dt);
}

/* To an exact transport time: how a command stamped with one is applied
   where it falls inside a window rather than at the window's end
   (genwav.mjs's -c, and JAM_M3.md section 2). */
EMSCRIPTEN_KEEPALIVE void tw_step_to (double t)
{
    sched_->stepTransportTo(t);
}

/* ---- the scheduler's commands, applied now ----
 *
 * The browser host stamps these with a time and holds them; here the
 * caller steps to the time and then applies, so the same command stream
 * gives the same tape from either host, which is what M3's harness
 * compares. */

/* The index of a piece knob by name -- the order the browser host numbers
   them in, which is the scheduler's map order -- or -1. */
EMSCRIPTEN_KEEPALIVE int tw_knob_index (const char *name)
{
    int k = 0;

    for (const auto &knob : sched_->knobs())
    {
        if (knob.first == name)
            return k;

        k++;
    }

    return -1;
}

EMSCRIPTEN_KEEPALIVE void tw_knob (int k, double value)
{
    int i = 0;

    for (const auto &knob : sched_->knobs())
    {
        if (i++ == k)
        {
            knob.second->setValue((float)value);
            return;
        }
    }
}

EMSCRIPTEN_KEEPALIVE void tw_tempo (double bpm)
{
    sched_->setTempo(bpm);
}

/* One window, tw_channels() * tw_window() floats, planar: all of channel 0,
   then all of channel 1. The pointer is the synth's own buffer: read it
   before the next call. */
EMSCRIPTEN_KEEPALIVE const float *tw_process (void)
{
    synth_->process();

    return synth_->getOutput();
}

/* What was delivered since the last clear, as twEvents. */
EMSCRIPTEN_KEEPALIVE int tw_event_count (void)
{
    return (int)tape_.count();
}

EMSCRIPTEN_KEEPALIVE const twEvent *tw_events (void)
{
    return tape_.data();
}

EMSCRIPTEN_KEEPALIVE void tw_events_clear (void)
{
    tape_.clear();
}

} /* extern "C" */
