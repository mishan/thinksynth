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
 * VideoRender: a video synth stepped beside genwav's audio synth, written
 * to a .y4m.
 *
 * The video synth is a thSynth whose sample clock is the pixel clock: one
 * sample a pixel, row by row, three output channels for R, G and B, and
 * no limiter, so voices that add past white clip there when the frame is
 * written. Its .dsp is loaded on each audio channel it follows, at full
 * amp, and plays the notes the scheduler hands that channel (a
 * thcNoteTap), so a chord draws as three voices.
 *
 * It renders only whole windows of whole lines that end at or before the
 * audio's time, in integer arithmetic, so it never drifts. A window is no
 * longer than an audio window, so the video trails the audio by less than
 * one, and a note reaches it within two of its time.
 */

#ifndef VIDEO_RENDER_H
#define VIDEO_RENDER_H

#include <stdio.h>
#include <stdint.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "think.h"
#include "thMidiChan.h"
#include "thSynth.h"
#include "thUtil.h"
#include "thVideoFile.h"
#include "thcScheduler.h"

class VideoRender : public thcNoteTap
{
public:
    /* `raster' is WxH@FPS. False, with a sentence, for a raster that is no
       picture, a .dsp that does not load, or a file that cannot be made. */
    bool open (const std::string &pluginPath, const std::string &dsp,
               const std::string &raster, const std::set<int> &follow,
               const std::string &path, std::string &why)
    {
        if (sscanf(raster.c_str(), "%dx%d@%d", &width_, &height_, &fps_) != 3 ||
            width_ < 1 || height_ < 1 || fps_ < 1 || width_ > TH_MAX_WINDOW_LENGTH)
        {
            why = "--raster wants WIDTHxHEIGHT@FPS, at most " +
                  std::to_string(TH_MAX_WINDOW_LENGTH) + " wide: " + raster;
            return false;
        }

        rate_ = (int64_t)width_ * height_ * fps_;

        const int64_t most = std::min<int64_t>(
            TH_MAX_WINDOW_LENGTH,
            rate_ * TH_DEFAULT_WINDOW_LENGTH / TH_DEFAULT_SAMPLES);

        window_ = (int)std::max<int64_t>(1, most / width_) * width_;
        synth_.reset(new thSynth(pluginPath, window_, (int)rate_, 3));
        synth_->setRaster(width_, height_);
        synth_->setLimiter(false);
        follow_ = follow;

        const std::string found =
            thUtil::findDataFile(dsp, "dsp", "THINK_DSP_PATH", DSP_PATH);

        for (int ch : follow_)
        {
            if (synth_->loadTree((found.empty() ? dsp : found).c_str(), ch,
                                 MIDIVALMAX) == NULL)
            {
                why = "'" + dsp + "' did not load";
                return false;
            }
        }
        dsp_ = dsp;
        frame_.assign(3 * (size_t)width_ * height_, 0.0f);
        return file_.open(path, width_, height_, fps_, why);
    }

    int fps (void) const { return fps_; }

    /* Only frames first..last-1 are written: --from and --length. */
    void keep (int64_t first, int64_t last) { keepFirst_ = first; keepLast_ = last; }

    thSynth *synth (void) { return synth_.get(); }

    /* Every whole window that ends at or before `audio' samples at
       `audioRate'. */
    bool advance (int64_t audio, int64_t audioRate, std::string &why)
    {
        while ((rendered_ + window_) * audioRate <= audio * rate_)
            if (!render(why))
                return false;
        return true;
    }

    /* On to the end of the frame that holds the last audio sample, and the
       file closed. */
    bool finish (int64_t audio, int64_t audioRate, std::string &why)
    {
        const int64_t frames = (audio * fps_ + audioRate - 1) / audioRate;

        keepLast_ = std::min(keepLast_, frames);
        while (frames_ < frames)
            if (!render(why))
                return false;
        return file_.close(why);
    }

    int64_t framesWritten (void) const { return written_; }

    void noteOn (int channel, int note, int velocity, float level,
                 const float *aux) override
    {
        if (!follow_.count(channel))
            return;
        synth_->addNote(channel, note, velocity, level, aux);
        down_.insert({ channel, note });
    }

    void noteOff (int channel, int note) override
    {
        if (!follow_.count(channel))
            return;
        synth_->delNote(channel, note);
        down_.erase({ channel, note });
    }

    /* Released rather than cut: the picture fades as the voice would. */
    void flush (int channel) override
    {
        for (auto k = down_.begin(); k != down_.end(); )
            if (channel < 0 || k->first == channel)
            {
                synth_->delNote(k->first, k->second);
                k = down_.erase(k);
            }
            else
                ++k;
    }

private:
    /* One window, into the frame being filled; a full frame to the file. */
    bool render (std::string &why)
    {
        synth_->process();
        rendered_ += window_;

        /* After the first window, which is when the loads are applied. */
        if (rendered_ == window_)
            for (int ch : follow_)
                if (synth_->getChannel(ch) != NULL &&
                    synth_->getChannel(ch)->numChannels() < 3)
                {
                    fprintf(stderr, "%s has fewer than 3 outputs, so it draws "
                            "in red only: give its ionode `channels = 3'\n",
                            dsp_.c_str());
                    break;
                }

        const float *out = synth_->getOutput();
        const size_t pixels = (size_t)width_ * height_;

        for (int i = 0; i < window_; i++)
        {
            for (int c = 0; c < 3; c++)
                frame_[c * pixels + filled_] = out[(size_t)c * window_ + i];
            if (++filled_ < pixels)
                continue;
            filled_ = 0;
            if (frames_ >= keepFirst_ && frames_ < keepLast_)
            {
                if (!file_.write(frame_.data(), frame_.data() + pixels,
                                 frame_.data() + 2 * pixels, why))
                    return false;
                written_++;
            }
            frames_++;
        }
        return true;
    }

    std::unique_ptr<thSynth> synth_;
    std::string dsp_;
    std::set<int> follow_;
    std::set<std::pair<int, int> > down_;
    thVideoFile file_;
    int width_ = 0, height_ = 0, fps_ = 0, window_ = 0;
    int64_t rate_ = 0, rendered_ = 0, frames_ = 0, written_ = 0;
    int64_t keepFirst_ = 0, keepLast_ = INT64_MAX;
    std::vector<float> frame_;
    size_t filled_ = 0;
};

#endif /* VIDEO_RENDER_H */
