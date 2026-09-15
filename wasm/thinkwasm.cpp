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

#include <deque>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <emscripten.h>

#include "think.h"

#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"

/* One delivered event, laid out for JavaScript to read straight out of the
   heap. genwav.mjs knows these offsets; the static_asserts below are what
   keep the two in step. `kind' is the tape's letter rather than the enum,
   so the enum's numbering stays this side of the boundary -- except for an
   event the tape has no letter for, whose raw type rides in `note'. */
struct twEvent
{
    double      at;
    double      duration;   /* N */
    double      value;      /* C, E */
    int32_t     kind;       /* 'N', 'C', 'P', 'E' or '?' */
    int32_t     channel;
    int32_t     note;       /* N; the THC_EV_* value for '?' */
    int32_t     velocity;   /* N */
    const char *name;       /* C: chanarg, P: patch, E: node */
    const char *arg;        /* E */
};

static_assert(offsetof(twEvent, at) == 0, "genwav.mjs reads at 0");
static_assert(offsetof(twEvent, duration) == 8, "genwav.mjs reads 8");
static_assert(offsetof(twEvent, value) == 16, "genwav.mjs reads 16");
static_assert(offsetof(twEvent, kind) == 24, "genwav.mjs reads 24");
static_assert(offsetof(twEvent, channel) == 28, "genwav.mjs reads 28");
static_assert(offsetof(twEvent, note) == 32, "genwav.mjs reads 32");
static_assert(offsetof(twEvent, velocity) == 36, "genwav.mjs reads 36");
static_assert(offsetof(twEvent, name) == 40, "genwav.mjs reads 40");
static_assert(offsetof(twEvent, arg) == 44, "genwav.mjs reads 44");
static_assert(sizeof(twEvent) == 48, "genwav.mjs steps by 48");

static std::map<std::string, thcPlugin *> plugins_;
static thSynth      *synth_;
static thcScheduler *sched_;
static thcGenLoader *loader_;

static sigc::connection      conn_;
static std::vector<twEvent>  events_;

/* The event's strings are the scheduler's and need not outlive delivery;
   these copies live until the next tw_events_clear. A deque, because
   growing one leaves the earlier elements -- and so their c_str()s -- where
   they were. */
static std::deque<std::string> strings_;

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

static const char *keep (const char *s)
{
    strings_.emplace_back(s != NULL ? s : "");

    return strings_.back().c_str();
}

static void deliver (const thcEvent &ev)
{
    twEvent e = {};

    e.at = ev.at;
    e.channel = ev.channel;

    switch (ev.type)
    {
        case THC_EV_NOTE:
            e.kind = 'N';
            e.note = ev.u.note.note;
            e.velocity = ev.u.note.velocity;
            e.duration = ev.u.note.duration;
            break;
        case THC_EV_CHANARG:
            e.kind = 'C';
            e.name = keep(ev.u.chanarg.name);
            e.value = ev.u.chanarg.value;
            break;
        case THC_EV_PATCH:
            e.kind = 'P';
            e.name = keep(ev.u.patch.name);
            break;
        case THC_EV_NODEARG:
            e.kind = 'E';
            e.name = keep(ev.u.nodearg.node);
            e.arg = keep(ev.u.nodearg.arg);
            e.value = ev.u.nodearg.value;
            break;
        default:
            e.kind = '?';
            e.note = (int)ev.type;
            break;
    }

    events_.push_back(e);
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
    conn_ = sched_->sigDelivered.connect(&deliver);
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
    return (int)events_.size();
}

EMSCRIPTEN_KEEPALIVE const twEvent *tw_events (void)
{
    return events_.data();
}

EMSCRIPTEN_KEEPALIVE void tw_events_clear (void)
{
    events_.clear();
    strings_.clear();
}

} /* extern "C" */
