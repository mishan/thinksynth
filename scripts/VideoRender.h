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
 * The audio reaches it as chanargs, set on every channel it follows after
 * each audio window, where its .dsp declares them:
 *
 *   @in_level              the mix's RMS
 *   @in_level0 ...         each audio channel's own RMS, before the master
 *   @in_bass, @in_mid,     the mix's amplitude under 250 Hz (less DC), to
 *   @in_high               2 kHz, and above: the root of the band's summed
 *                          squared magnitudes, whatever its width, a
 *                          full-scale sine about 1, clamped to 0..1
 *   @knob_<name>           each of the piece's knobs
 *
 * `in_' and `knob_' so they cannot meet a patch's own @level. No smoothing:
 * a .dsp that wants it filters the chanarg like any signal.
 *
 * It renders only whole windows of whole lines that end at or before the
 * audio's time, in integer arithmetic, so it never drifts. A window is no
 * longer than an audio window, so the video trails the audio by less than
 * one, and a note reaches it within two of its time -- unless one line
 * takes longer to scan than an audio window lasts, as on a picture a few
 * lines tall, when a window is that line.
 */

#ifndef VIDEO_RENDER_H
#define VIDEO_RENDER_H

#include <limits.h>
#include <stdio.h>
#include <stdint.h>

#include <math.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "think.h"
#include "thArg.h"
#include "thFFT.h"
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
        int end = 0;

        if (sscanf(raster.c_str(), "%dx%d@%d%n", &width_, &height_, &fps_,
                   &end) != 3 || end != (int)raster.size() ||
            width_ < 1 || height_ < 1 || fps_ < 1 ||
            width_ > TH_MAX_WINDOW_LENGTH ||
            (int64_t)width_ * height_ * fps_ > INT_MAX)
        {
            why = "--raster wants WIDTHxHEIGHT@FPS in whole numbers, at most " +
                  std::to_string(TH_MAX_WINDOW_LENGTH) + " wide and " +
                  std::to_string(INT_MAX) + " pixels a second: " + raster;
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

    /* What the audio synth's last window said, onto every channel this
       follows: see the header. */
    void bridge (thSynth &audio, const std::map<std::string, thArg *> &knobs)
    {
        const int window = audio.getWindowlen(), channels = audio.audioChannelCount();
        const float *mix = audio.getOutput();
        std::vector<std::pair<std::string, float> > values;

        values.push_back({ "in_level", rms(mix, (size_t)channels * window) });
        for (int ch = 0; ch < audio.midiChanCount(); ch++)
        {
            int outputs = 0;
            const float *own = audio.getChannelOutput(ch, &outputs);

            values.push_back({ "in_level" + std::to_string(ch),
                               own != NULL ? rms(own, (size_t)outputs * window) : 0 });
        }

        if (window == (int)fft_.size() && fft_.ok())
        {
            const auto mono = [&](unsigned int i)
            {
                float sum = 0;

                for (int c = 0; c < channels; c++)
                    sum += mix[(size_t)c * window + i];
                return sum / channels;
            };
            const double hz = (double)audio.getSampleRate() / fft_.size();

            magnitudes_.resize(fft_.bins());
            fft_.magnitude(mono, magnitudes_.data());
            values.push_back({ "in_bass", band(1, 250 / hz) });
            values.push_back({ "in_mid", band(250 / hz, 2000 / hz) });
            values.push_back({ "in_high", band(2000 / hz, fft_.bins()) });
        }

        for (const auto &k : knobs)
            values.push_back({ "knob_" + k.first, (*k.second)[0] });

        for (int ch : follow_)
            for (const auto &v : values)
            {
                thArg *arg = synth_->getChanArg(ch, v.first);

                if (arg != NULL)
                    arg->setValue(v.second);
            }
    }

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
    static float rms (const float *x, size_t n)
    {
        double sum = 0;

        for (size_t i = 0; i < n; i++)
            sum += (double)x[i] * x[i];
        return n ? (float)sqrt(sum / n) : 0;
    }

    /* Bins from `lo' up to `hi'. */
    float band (double lo, double hi)
    {
        const unsigned int first = (unsigned int)ceil(lo);
        const unsigned int last = std::min((unsigned int)ceil(hi), fft_.bins());
        double sum = 0;

        for (unsigned int b = first; b < last; b++)
            sum += (double)magnitudes_[b] * magnitudes_[b];
        return (float)std::min(1.0, sqrt(sum));
    }

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
    thv::FFTR fft_ { 10 };
    std::vector<float> magnitudes_;
};

#endif /* VIDEO_RENDER_H */
