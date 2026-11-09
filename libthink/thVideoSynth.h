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
 * thVideoSynth -- a video synth: a thSynth whose sample clock is the pixel
 * clock, stepped beside an audio synth and handed its notes.
 *
 * One sample a pixel, row by row, three output channels for R, G and B,
 * and no limiter, so voices that add past white clip where the frame is
 * shown. A .dsp is loaded on each audio channel it follows, at full amp,
 * and plays the notes that channel plays, so a chord draws as three
 * voices. An effect on those channels sees their whole picture and
 * outlives every note: a delay a frame long is video feedback.
 *
 * What the audio says (thVideoBridge) reaches it as chanargs on every
 * channel it follows, where its .dsp uses them: @in_level, @in_level<N>,
 * @in_bass, @in_mid, @in_high, and whatever else set() names.
 *
 * It renders in windows of whole lines, and advance() renders only those
 * that end at or before the audio's time, in integer arithmetic, so it
 * never drifts. A finished frame goes to the caller's Frame as three
 * planes, R, G and B, unclamped.
 */

#ifndef TH_VIDEO_SYNTH_H
#define TH_VIDEO_SYNTH_H 1

#include <stdint.h>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "thExport.h"

class thSynth;
struct thVideoInputs;

class THINK_API thVideoSynth
{
public:
    /* Frame `frame', counted from the first; false stops the render. */
    typedef std::function<bool (int64_t frame, const float *r, const float *g,
                                const float *b)> Frame;

    thVideoSynth (void);
    ~thVideoSynth (void);

    /* `lines' a window, or 0 for as many as last about as long as an audio
       window: the longest that keeps the picture within one of the sound,
       and the most `lines' is held to. Before anything else.
       False, with a sentence, for a raster too big to be a synth's. */
    bool open (const std::string &pluginPath, int width, int height, int fps,
               int lines, std::string &why);

    /* `dsp', and `fx' as its effect if not empty, on `channel'. */
    bool follow (int channel, const std::string &dsp, const std::string &fx,
                 std::string &why);

    void setInputs (const thVideoInputs &in);
    void set (const std::string &chanarg, float value);

    /* One window. */
    bool render (const Frame &frame);

    /* Every whole window that ends at or before `audio' samples at
       `audioRate'. */
    bool advance (int64_t audio, int64_t audioRate, const Frame &frame);

    /* On to the end of the frame that holds audio sample `audio' - 1. */
    bool finish (int64_t audio, int64_t audioRate, const Frame &frame);

    /* Straight to the start of frame `frame', a later one, rendering
       nothing between: a picture that fell behind its sound catching up. */
    void skipTo (int64_t frame);

    void noteOn (int channel, int note, int velocity, float level,
                 const float *aux);
    void noteOff (int channel, int note);
    /* Every note on `channel' let go, or on every channel for -1: released
       rather than cut, so the picture fades as the voice would. */
    void flush (int channel);

    /* After the first window, which is when the loads are applied: a
       sentence for a .dsp with fewer than 3 outputs, once, or "". */
    std::string narrow (void);

    int width (void) const { return width_; }
    int height (void) const { return height_; }
    int fps (void) const { return fps_; }
    int window (void) const { return window_; }
    int64_t frames (void) const { return frames_; }
    thSynth *synth (void) { return synth_.get(); }

private:
    std::unique_ptr<thSynth> synth_;
    std::set<int> follow_;
    std::set<std::pair<int, int> > down_;
    std::string dsp_;
    int width_ = 0, height_ = 0, fps_ = 0, window_ = 0;
    int64_t rate_ = 0, rendered_ = 0, frames_ = 0;
    std::vector<float> frame_;
    size_t filled_ = 0;
    bool processed_ = false, narrowSaid_ = false;
};

#endif /* TH_VIDEO_SYNTH_H */
