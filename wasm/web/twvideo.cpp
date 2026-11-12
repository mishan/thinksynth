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
#include <string.h>

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
#include "thMidiChan.h"
#include "thSynth.h"
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
/* What the sound says, and knobs turned, each from the audio frame it is
   as of. */
std::deque<std::pair<double, thVideoInputs> > inputs_;
struct Knob
{
    double at;
    std::string chanarg;
    float value;
};
std::deque<Knob> knobs_;
std::map<std::pair<int, int>, Note> held_;
std::deque<std::pair<int64_t, std::vector<uint8_t> > > done_;
std::string said_;
std::vector<double> noteBuffer_;
bool interpret_ = false;
/* tv_window: the audio frames a channel's notes may be struck in here,
   for a channel being handed between workers. */
std::map<int, std::pair<double, double> > windows_;

/* tv_planes: finished frames as float planes, for a leader to sum. */
bool planes_ = false;
/* tv_rgba's: a frame's planes in, its RGBA out. */
std::vector<float> planeBuffer_;
std::vector<uint8_t> rgbaBuffer_;
float inputBuffer_[4 + TH_MIDI_CHANNELS];
int lines_ = 0;

/* tv_stage: an effect on a picture summed from modules that drew it
   without one, run window by window as one module runs it. Its own
   inputs and knobs, queued as the drawing synth's are, and its finished
   frames. */
std::unique_ptr<thVideoSynth> stage_;
std::deque<std::pair<double, thVideoInputs> > stageInputs_;
std::deque<Knob> stageKnobs_;
std::vector<float> add_;
std::deque<std::pair<int64_t, std::vector<uint8_t> > > stageDone_;

uint8_t
byte (float v)
{
    if (!(v > 0))       /* and NaN */
        return 0;
    if (v >= 1)
        return 255;
    return (uint8_t)lrintf(v * 255);
}

void
toRgba (size_t pixels, const float *r, const float *g, const float *b, uint8_t *out)
{
    for (size_t i = 0; i < pixels; i++)
    {
        out[4 * i] = byte(r[i]);
        out[4 * i + 1] = byte(g[i]);
        out[4 * i + 2] = byte(b[i]);
        out[4 * i + 3] = 255;
    }
}

bool
keep (int64_t frame, const float *r, const float *g, const float *b)
{
    const size_t pixels = (size_t)video_->width() * video_->height();
    const size_t plane = pixels * sizeof(float);

    if (done_.size() == DONE_MOST)
        done_.pop_front();
    done_.emplace_back(frame, std::vector<uint8_t>(planes_ ? 3 * plane : 4 * pixels));

    uint8_t *out = done_.back().second.data();

    if (planes_)
    {
        memcpy(out, r, plane);
        memcpy(out + plane, g, plane);
        memcpy(out + 2 * plane, b, plane);
    }
    else
        toRgba(pixels, r, g, b, out);
    return true;
}

/* Onto `v', the inputs and knobs due by its sample `start'. */
void
arrive (thVideoSynth &v, std::deque<std::pair<double, thVideoInputs> > &inputs,
        std::deque<Knob> &knobs, int64_t start)
{
    const int64_t rate = v.rate();

    while (!inputs.empty() && inputs.front().first * rate <= (double)start * audioRate_)
    {
        v.setInputs(inputs.front().second);
        inputs.pop_front();
    }
    while (!knobs.empty() && knobs.front().at * rate <= (double)start * audioRate_)
    {
        v.set(knobs.front().chanarg, knobs.front().value);
        knobs.pop_front();
    }
}

/* Those before frame `frame', for a skip to it: the sound as it last was,
   and every knob turned. */
void
arriveBefore (thVideoSynth &v, std::deque<std::pair<double, thVideoInputs> > &inputs,
              std::deque<Knob> &knobs, int64_t frame)
{
    const double at = (double)frame * audioRate_ / v.fps();

    while (inputs.size() > 1 && inputs[1].first < at)
        inputs.pop_front();
    if (!inputs.empty() && inputs.front().first < at)
    {
        v.setInputs(inputs.front().second);
        inputs.pop_front();
    }
    while (!knobs.empty() && knobs.front().at < at)
    {
        v.set(knobs.front().chanarg, knobs.front().value);
        knobs.pop_front();
    }
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

/* Whether `n' is struck here: a note on outside its channel's window is
   another worker's. Offs and flushes always are. */
bool
here (const Note &n)
{
    const auto w = windows_.find(n.channel);

    return n.kind != ON || w == windows_.end() ||
           (n.at >= w->second.first && n.at < w->second.second);
}

/* Whether another worker's note `n', on a channel being handed over, is
   struck here too, at level 0: it draws nothing and takes its place among
   the channel's voices as on one worker, so past the limit the oldest
   goes, whichever worker draws it. Not with no limit, where it would cut
   nothing; nor on a mono channel, whose one voice would slide to it and go
   dark with the key before it still down. */
bool
shadows (const Note &n)
{
    thMidiChan *chan = video_->synth()->getChannel(n.channel);

    return chan != NULL && chan->polyMax() > 0 && !chan->mono();
}

/* `n' as this worker strikes it. */
Note
mine (const Note &n)
{
    Note m = n;

    if (!here(n))
        m.level = 0;
    return m;
}

/* Notes for a channel the picture does not follow are not held: nothing
   draws them, and a skip must not strike them if it comes to. */
void
hold (std::map<std::pair<int, int>, Note> &held, const Note &n)
{
    if (n.kind != FLUSH && !video_->follows(n.channel))
        return;
    /* Struck again on another worker: the voice here goes, as it would
       to a voice struck again here. */
    if (!here(n) && !shadows(n))
    {
        held.erase({ n.channel, n.note });
        return;
    }
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
play (const Note &note)
{
    const Note n = mine(note);

    if (!here(n) && !shadows(n))
    {
        if (held_.count({ n.channel, n.note }) && video_->follows(n.channel))
            video_->noteOff(n.channel, n.note);
        hold(held_, n);
        return;
    }
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
        hold(then, mine(queue_.front()));
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
    arriveBefore(*video_, inputs_, knobs_, frame);
    video_->skipTo(frame);
    skips_++;
}

} // namespace

extern "C" {

/* `lines' a window; audio frames at `audioRate'. "" or why not. */
EMSCRIPTEN_KEEPALIVE const char *
tv_open (int width, int height, int fps, int lines, int audioRate)
{
    if (!interpret_)
        thCompiledInstall();
    video_.reset(new thVideoSynth);
    queue_.clear();
    windows_.clear();
    inputs_.clear();
    knobs_.clear();
    held_.clear();
    serial_ = 0;
    done_.clear();
    stage_.reset();
    stageInputs_.clear();
    stageKnobs_.clear();
    stageDone_.clear();
    audioRate_ = audioRate;
    generated_ = skips_ = 0;
    said_.clear();
    /* Windows that divide a frame, so a summed frame goes through the
       stage whole, never waiting on the next for a window's end. */
    for (; video_->open("", width, height, fps, lines, said_) &&
           (int64_t)width * height % video_->window() != 0 &&
           (lines = video_->window() / width - 1) > 0;)
        video_.reset(new thVideoSynth);
    lines_ = video_->window() / std::max(width, 1);
    if (audioRate < 1 || video_->width() == 0)
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

/* An effect, called `name', on the pictures tv_stage_frame is handed:
   for a leader, whose modules draw with none. */
EMSCRIPTEN_KEEPALIVE const char *
tv_stage (const char *name, const char *fx)
{
    said_.clear();
    if (video_ == NULL)
        return said_.c_str();
    stage_.reset(new thVideoSynth);
    if (!stage_->open("", video_->width(), video_->height(), video_->fps(), lines_, said_) ||
        !stage_->effectText(name, fx, said_))
        stage_.reset();
    return said_.c_str();
}

/* Notes on for `channel' struck from audio frame `from' up to `until'
   are drawn here, and others not: the window a worker handing it over
   keeps, or the one it is handed. */
EMSCRIPTEN_KEEPALIVE void
tv_window (int channel, double from, double until)
{
    windows_[channel] = std::make_pair(from, until);
}

EMSCRIPTEN_KEEPALIVE void
tv_unfollow (int channel)
{
    windows_.erase(channel);
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

/* What the sound says, with `channels' channels' levels, as of audio
   frame `at': it reaches the picture there, as a note does, so where it
   lands doesn't hang on how far the picture had drawn when it came. */
EMSCRIPTEN_KEEPALIVE void
tv_inputs (int channels, double at)
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
        inputs_.push_back(std::make_pair(at, v));
    if (stage_ != NULL)
        stageInputs_.push_back(std::make_pair(at, v));
}

EMSCRIPTEN_KEEPALIVE void
tv_set (const char *chanarg, float value)
{
    if (video_ != NULL)
        video_->set(chanarg, value);
    if (stage_ != NULL)
        stage_->set(chanarg, value);
}

/* `chanarg' to `value' as of audio frame `at', as the sound's inputs
   are: a knob the music turned, in the batch it was turned in. */
EMSCRIPTEN_KEEPALIVE void
tv_set_at (const char *chanarg, float value, double at)
{
    if (video_ != NULL)
        knobs_.push_back({ at, chanarg, value });
    if (stage_ != NULL)
        stageKnobs_.push_back({ at, chanarg, value });
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
        arrive(*video_, inputs_, knobs_, start);
        video_->render(keep);
    }
    return (int)done_.size();
}

EMSCRIPTEN_KEEPALIVE double
tv_done_frame (int i)
{
    return (double)done_[i].first;
}

/* Finished frames from now as float planes, R, G then B, unclamped,
   through tv_done_rgba, for a leader to sum; 0 for RGBA again. */
EMSCRIPTEN_KEEPALIVE void
tv_planes (int on)
{
    planes_ = on != 0;
}

/* Where tv_rgba reads a frame's planes from. */
EMSCRIPTEN_KEEPALIVE float *
tv_plane_buffer (void)
{
    if (video_ == NULL)
        return NULL;
    planeBuffer_.resize(3 * (size_t)video_->width() * video_->height());
    return planeBuffer_.data();
}

/* tv_plane_buffer's planes as RGBA, as a finished frame is. */
EMSCRIPTEN_KEEPALIVE const uint8_t *
tv_rgba (void)
{
    if (video_ == NULL)
        return NULL;

    const size_t pixels = (size_t)video_->width() * video_->height();

    planeBuffer_.resize(3 * pixels);
    rgbaBuffer_.resize(4 * pixels);
    toRgba(pixels, planeBuffer_.data(), planeBuffer_.data() + pixels,
           planeBuffer_.data() + 2 * pixels, rgbaBuffer_.data());
    return rgbaBuffer_.data();
}

/* tv_plane_buffer's planes, frame `frame' summed, through the stage's
   effect window by window. A frame later than the next is a skip to it;
   an earlier one is passed over. Returns how many finished frames wait,
   RGBA, for tv_stage_rgba. */
EMSCRIPTEN_KEEPALIVE int
tv_stage_frame (double frame)
{
    if (stage_ == NULL)
        return 0;

    const int64_t pixels = (int64_t)stage_->width() * stage_->height();
    const int64_t window = stage_->window();
    const int64_t at = (int64_t)frame * pixels;

    if (at < stage_->rendered())
        return (int)stageDone_.size();
    if (at > stage_->rendered())
    {
        arriveBefore(*stage_, stageInputs_, stageKnobs_, (int64_t)frame);
        stage_->skipTo((int64_t)frame);
    }
    planeBuffer_.resize(3 * (size_t)pixels);
    add_.resize(3 * (size_t)window);

    const auto out = [] (int64_t f, const float *r, const float *g, const float *b)
    {
        const size_t n = (size_t)stage_->width() * stage_->height();

        if (stageDone_.size() == DONE_MOST)
            stageDone_.pop_front();
        stageDone_.emplace_back(f, std::vector<uint8_t>(4 * n));
        toRgba(n, r, g, b, stageDone_.back().second.data());
        return true;
    };

    for (int64_t from = 0; from < pixels; from += window)
    {
        for (int c = 0; c < 3; c++)
            std::copy(planeBuffer_.begin() + c * pixels + from,
                      planeBuffer_.begin() + c * pixels + from + window,
                      add_.begin() + c * window);
        arrive(*stage_, stageInputs_, stageKnobs_, stage_->rendered());
        stage_->render(out, add_.data());
    }
    return (int)stageDone_.size();
}

EMSCRIPTEN_KEEPALIVE double
tv_stage_done_frame (int i)
{
    return (double)stageDone_[i].first;
}

EMSCRIPTEN_KEEPALIVE const uint8_t *
tv_stage_rgba (int i)
{
    return stageDone_[i].second.data();
}

EMSCRIPTEN_KEEPALIVE void
tv_stage_clear (void)
{
    stageDone_.clear();
}

/* Straight to frame `frame', when told: a helper the leader keeps in step
   with the others, which tv_render with no frame due never skips. */
EMSCRIPTEN_KEEPALIVE void
tv_skip_to (double frame)
{
    if (video_ != NULL && (int64_t)frame > video_->frames())
        skipTo((int64_t)frame);
}

/* The audio frame the picture is drawn to: silence a channel reports is
   as of here, so a note queued past it is still to come. */
EMSCRIPTEN_KEEPALIVE double
tv_drawn (void)
{
    return video_ != NULL ? (double)video_->rendered() * audioRate_ / video_->rate() : 0;
}

/* 1 while a voice on `channel' still draws, held, releasing or fading,
   as of the windows drawn; 0 once none does. Another worker's, struck
   here at level 0, draws nothing. */
EMSCRIPTEN_KEEPALIVE int
tv_sounding (int channel)
{
    thMidiChan *chan = video_ != NULL ? video_->synth()->getChannel(channel) : NULL;

    return chan != NULL && chan->audible() ? 1 : 0;
}

/* The i-th finished frame: RGBA, or with tv_planes its float planes. */
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
