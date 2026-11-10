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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * twvideo -- a video synth played live, in the video worker's own instance
 * of the module (videoworker.js).
 *
 * The worklet says what it played and when, in its own audio frames,
 * counted here from when video was turned on: notes, and what the sound
 * says (thVideoBridge). They wait in a queue until the picture reaches
 * their time, and land on the first window after it, a millisecond or so
 * late, rather than where their batch arrived. tv_render draws as far as the audio has been generated, and no
 * further, and hands back each finished frame as RGBA.
 *
 * A picture that cannot keep up does not fall further and further behind
 * its sound. Asked for a frame more than one past the one it is drawing,
 * it skips straight to it: the notes held at that moment are held, and
 * those that began and ended in between were never struck, so nothing
 * flashes and nothing sticks.
 */

#include <math.h>
#include <stdint.h>

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <emscripten.h>

#include "think.h"
#include "thCompiled.h"
#include "thVideoBridge.h"
#include "thVideoSynth.h"

namespace {

enum { OFF = 0, ON = 1, FLUSH = 2 };

/* tv_notes' records, as the worklet writes them. */
enum { AT, KIND, CHANNEL, NOTE, VELOCITY, LEVEL, AUX, VOICE = AUX + 4, RECORD };

struct Note
{
    double at;
    int64_t serial;     /* which striking this is */
    double voice;       /* the worklet's: an off's is its on's, 0 any */
    int    kind, channel, note, velocity;
    float  level, aux[4];
};

/* Finished frames not yet taken: more than this and the oldest go. */
const size_t DONE_MOST = 8;

std::unique_ptr<thVideoSynth> video_;
int64_t audioRate_ = 0, generated_ = 0, skips_ = 0, serial_ = 0;
std::deque<Note> queue_;
std::map<std::pair<int, int>, Note> held_;
std::deque<std::pair<int64_t, std::vector<uint8_t> > > done_;
std::string said_;
std::vector<double> noteBuffer_;
bool interpret_ = false;
float inputBuffer_[4 + TH_MIDI_CHANNELS];

uint8_t
byte (float v)
{
    if (!(v > 0))       /* and NaN */
        return 0;
    if (v >= 1)
        return 255;
    return (uint8_t)lrintf(v * 255);
}

bool
keep (int64_t frame, const float *r, const float *g, const float *b)
{
    const size_t pixels = (size_t)video_->width() * video_->height();

    if (done_.size() == DONE_MOST)
        done_.pop_front();
    done_.emplace_back(frame, std::vector<uint8_t>(4 * pixels));

    uint8_t *out = done_.back().second.data();

    for (size_t i = 0; i < pixels; i++)
    {
        out[4 * i] = byte(r[i]);
        out[4 * i + 1] = byte(g[i]);
        out[4 * i + 2] = byte(b[i]);
        out[4 * i + 3] = 255;
    }
    return true;
}

/* Whether off `n' ends the note held on its key: not one struck since, on
   a key whose off was stamped later than its next on -- a channel moved off
   a device, whose offs come its MIDI delay late. */
bool
ends (const std::map<std::pair<int, int>, Note> &held, const Note &n)
{
    const auto h = held.find({ n.channel, n.note });

    return h != held.end() && (n.voice == 0 || h->second.voice == n.voice);
}

/* Notes for a channel the picture does not follow are not held: nothing
   draws them, and a skip must not strike them if it comes to. */
void
hold (std::map<std::pair<int, int>, Note> &held, const Note &n)
{
    if (n.kind != FLUSH && !video_->follows(n.channel))
        return;
    if (n.kind == ON)
        held[{ n.channel, n.note }] = n;
    else if (n.kind == OFF)
    {
        if (ends(held, n))
            held.erase({ n.channel, n.note });
    }
    else
        for (auto k = held.begin(); k != held.end(); )
            k = n.channel < 0 || k->first.first == n.channel ? held.erase(k) : std::next(k);
}

/* Onto the synth as well as into what is held. */
void
play (const Note &n)
{
    if (n.kind == ON)
        video_->noteOn(n.channel, n.note, n.velocity, n.level, n.aux);
    else if (n.kind == OFF && ends(held_, n))
        video_->noteOff(n.channel, n.note);
    else if (n.kind == FLUSH)
        video_->flush(n.channel);
    hold(held_, n);
}

/* The first audio frame of video frame `frame'. */
double
startOf (int64_t frame)
{
    return (double)frame * audioRate_ / video_->fps();
}

/* Straight to `frame', which only the notes held there survive. */
void
skipTo (int64_t frame)
{
    std::map<std::pair<int, int>, Note> then = held_;

    while (!queue_.empty() && queue_.front().at < startOf(frame))
    {
        hold(then, queue_.front());
        queue_.pop_front();
    }
    /* A note let go and struck again in the gap is a new voice, with its
       own velocity, level and aux. */
    for (const auto &h : held_)
    {
        const auto t = then.find(h.first);

        if (t == then.end() || t->second.serial != h.second.serial)
            video_->noteOff(h.first.first, h.first.second);
    }
    for (const auto &t : then)
    {
        const auto h = held_.find(t.first);

        if (h == held_.end() || h->second.serial != t.second.serial)
            video_->noteOn(t.first.first, t.first.second, t.second.velocity,
                           t.second.level, t.second.aux);
    }
    held_ = then;
    video_->skipTo(frame);
    skips_++;
}

} // namespace

extern "C" {

/* `lines' a window; audio frames at `audioRate'. "" or why not. */
EMSCRIPTEN_KEEPALIVE const char *
tv_open (int width, int height, int fps, int lines, int audioRate)
{
    static bool installed = false;

    if (!installed && !interpret_)
        thCompiledInstall();
    installed = true;
    video_.reset(new thVideoSynth);
    queue_.clear();
    held_.clear();
    serial_ = 0;
    done_.clear();
    audioRate_ = audioRate;
    generated_ = skips_ = 0;
    said_.clear();
    if (audioRate < 1 || !video_->open("", width, height, fps, lines, said_))
    {
        if (said_.empty())
            said_ = "no audio rate";
        video_.reset();
    }
    return said_.c_str();
}

/* Before the first tv_open: the patches run on their nodes, never
   compiled. */
EMSCRIPTEN_KEEPALIVE void
tv_interpret (void)
{
    interpret_ = true;
}

/* Windows drawn so far, compiled or on the nodes. */
EMSCRIPTEN_KEEPALIVE double
tv_windows (int compiled)
{
    return thSynthTree::windowsRun(compiled != 0);
}

/* The patch, called `name', on `channel'. */
EMSCRIPTEN_KEEPALIVE const char *
tv_follow (int channel, const char *name, const char *dsp)
{
    said_.clear();
    if (video_ != NULL)
        video_->followText(channel, name, dsp, said_);
    return said_.c_str();
}

/* An effect, called `name', on the whole picture. */
EMSCRIPTEN_KEEPALIVE const char *
tv_effect (const char *name, const char *fx)
{
    said_.clear();
    if (video_ != NULL)
        video_->effectText(name, fx, said_);
    return said_.c_str();
}

EMSCRIPTEN_KEEPALIVE void
tv_unfollow (int channel)
{
    if (video_ != NULL)
        video_->unfollow(channel);
    hold(held_, Note { 0, 0, 0, FLUSH, channel, 0, 0, 0, {} });
}

/* Room for `count' records, for tv_notes to read. */
EMSCRIPTEN_KEEPALIVE double *
tv_note_buffer (int count)
{
    noteBuffer_.resize((size_t)count * RECORD);
    return noteBuffer_.data();
}

/* `count' records of RECORD doubles each in tv_note_buffer, in the order
   they were played; those at the same frame keep it. */
EMSCRIPTEN_KEEPALIVE void
tv_notes (int count)
{
    const double *records = noteBuffer_.data();

    for (int i = 0; i < count; i++)
    {
        const double *r = records + (size_t)i * RECORD;
        Note n = { r[AT], serial_++, r[VOICE], (int)r[KIND], (int)r[CHANNEL], (int)r[NOTE], (int)r[VELOCITY],
                   (float)r[LEVEL], { (float)r[AUX], (float)r[AUX + 1], (float)r[AUX + 2],
                                      (float)r[AUX + 3] } };

        /* In time order: a device's channel is stamped its delay late, so
           a batch is not. */
        queue_.insert(std::upper_bound(queue_.begin(), queue_.end(), n,
                                       [](const Note &a, const Note &b) { return a.at < b.at; }),
                      n);
    }
}

/* Where tv_inputs reads from: level, bass, mid, high, then each channel's
   level. */
EMSCRIPTEN_KEEPALIVE float *
tv_input_buffer (void)
{
    return inputBuffer_;
}

/* What the sound says, as of the newest batch, with `channels' channels'
   levels. */
EMSCRIPTEN_KEEPALIVE void
tv_inputs (int channels)
{
    const float *in = inputBuffer_;
    thVideoInputs v;

    v.level = in[0];
    v.bass = in[1];
    v.mid = in[2];
    v.high = in[3];
    v.channels = channels < TH_MIDI_CHANNELS ? channels : TH_MIDI_CHANNELS;
    for (int c = 0; c < v.channels; c++)
        v.levels[c] = in[4 + c];
    if (video_ != NULL)
        video_->setInputs(v);
}

EMSCRIPTEN_KEEPALIVE void
tv_set (const char *chanarg, float value)
{
    if (video_ != NULL)
        video_->set(chanarg, value);
}

/* The audio has been generated to audio frame `at'. */
EMSCRIPTEN_KEEPALIVE void
tv_generated (double at)
{
    generated_ = (int64_t)at;
}

/* Up to `windows' windows, as far as the audio has been generated, after a
   skip to `due' if the picture is more than a frame behind it (-1: no
   frame is due yet). Returns how many finished frames wait. */
EMSCRIPTEN_KEEPALIVE int
tv_render (double due, int windows)
{
    if (video_ == NULL)
        return 0;

    const int64_t reachable = generated_ * video_->fps() / audioRate_;
    const int64_t to = due < reachable ? (int64_t)due : reachable;

    if (due >= 0 && to > video_->frames() + 1)
        skipTo(to);

    const int64_t rate = video_->rate();

    for (int w = 0; w < windows; w++)
    {
        const int64_t start = video_->rendered();

        if ((start + video_->window()) * audioRate_ > generated_ * rate)
            break;
        while (!queue_.empty() && queue_.front().at * rate <= (double)start * audioRate_)
        {
            play(queue_.front());
            queue_.pop_front();
        }
        video_->render(keep);
    }
    return (int)done_.size();
}

EMSCRIPTEN_KEEPALIVE double
tv_done_frame (int i)
{
    return (double)done_[i].first;
}

EMSCRIPTEN_KEEPALIVE const uint8_t *
tv_done_rgba (int i)
{
    return done_[i].second.data();
}

EMSCRIPTEN_KEEPALIVE void
tv_done_clear (void)
{
    done_.clear();
}

/* How many times the picture has skipped ahead. */
EMSCRIPTEN_KEEPALIVE double
tv_skips (void)
{
    return (double)skips_;
}

/* After the first window: a sentence for a patch with fewer than 3
   outputs, once, or "". */
EMSCRIPTEN_KEEPALIVE const char *
tv_narrow (void)
{
    said_ = video_ != NULL ? video_->narrow() : "";
    return said_.c_str();
}

} // extern "C"
