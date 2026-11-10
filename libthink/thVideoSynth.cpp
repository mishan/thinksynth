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

#include "config.h"

#include <limits.h>

#include <algorithm>

#include "think.h"
#include "thArg.h"
#include "thMidiChan.h"
#include "thSynth.h"
#include "thVideoBridge.h"
#include "thVideoSynth.h"

thVideoSynth::thVideoSynth (void) = default;
thVideoSynth::~thVideoSynth (void) = default;

bool thVideoSynth::open (const std::string &pluginPath, int width, int height,
                         int fps, int lines, std::string &why)
{
    if (width < 1 || height < 1 || fps < 1 || width > TH_MAX_WINDOW_LENGTH ||
        (int64_t)width * height * fps > INT_MAX)
    {
        why = "a picture is at most " + std::to_string(TH_MAX_WINDOW_LENGTH) +
              " wide and " + std::to_string(INT_MAX) + " pixels a second";
        return false;
    }

    width_ = width;
    height_ = height;
    fps_ = fps;
    rate_ = (int64_t)width_ * height_ * fps_;

    const int64_t most = std::min<int64_t>(
        TH_MAX_WINDOW_LENGTH, rate_ * TH_DEFAULT_WINDOW_LENGTH / TH_DEFAULT_SAMPLES);

    window_ = (int)std::max<int64_t>(1, lines > 0 ? std::min<int64_t>(lines, most / width_)
                                                  : most / width_) * width_;
    synth_.reset(new thSynth(pluginPath, window_, (int)rate_, 3));
    synth_->setRaster(width_, height_);
    synth_->setLimiter(false);
    frame_.assign(3 * (size_t)width_ * height_, 0.0f);
    return true;
}

/* Each voice draws every pixel, so a channel's voices are what a picture
   costs: a dense piece's long releases stack up past what a page can draw
   at its frame rate. Two a channel, where its io says no other, and the
   oldest fades as a third starts. */
static const int VOICES = 2;

static void
limitVoices (thSynth *synth, int channel)
{
    thMidiChan *chan = synth->getChannel(channel);

    if (chan != NULL && chan->modnode() != NULL && chan->modnode()->IONode() != NULL &&
        chan->modnode()->IONode()->getArg("poly") == NULL)
        chan->setPolyMax(VOICES);
}

bool thVideoSynth::follow (int channel, const std::string &dsp, std::string &why)
{
    if (synth_->loadTree(dsp.c_str(), channel, MIDIVALMAX) == NULL)
    {
        why = "'" + dsp + "' did not load";
        return false;
    }
    limitVoices(synth_.get(), channel);
    follow_.insert(channel);
    if (dsp_.empty())
        dsp_ = dsp;
    return true;
}

bool thVideoSynth::followText (int channel, const std::string &name,
                               const std::string &dsp, std::string &why)
{
    if (synth_->loadTreeText(name, dsp, channel, MIDIVALMAX) == NULL)
    {
        why = "'" + name + "' did not load";
        return false;
    }
    limitVoices(synth_.get(), channel);
    follow_.insert(channel);
    if (dsp_.empty())
        dsp_ = name;
    return true;
}

bool thVideoSynth::effect (const std::string &fx, std::string &why)
{
    if (synth_->loadMasterEffect(fx) != NULL)
        return true;
    why = "'" + fx + "' did not load as an effect";
    return false;
}

bool thVideoSynth::effectText (const std::string &name, const std::string &fx,
                               std::string &why)
{
    if (synth_->loadMasterEffectText(name, fx) != NULL)
        return true;
    why = "'" + name + "' did not load as an effect";
    return false;
}

void thVideoSynth::unfollow (int channel)
{
    flush(channel);
    synth_->removeChan(channel);
    follow_.erase(channel);
}

void thVideoSynth::set (const std::string &chanarg, float value)
{
    for (int ch : follow_)
    {
        thArg *arg = synth_->getChanArg(ch, chanarg);

        if (arg != NULL)
            arg->setValue(value);
    }

    thArg *master = synth_->getMasterArg(chanarg);

    if (master != NULL)
        master->setValue(value);
}

void thVideoSynth::setInputs (const thVideoInputs &in)
{
    set("in_level", in.level);
    for (int ch = 0; ch < in.channels; ch++)
        set("in_level" + std::to_string(ch), in.levels[ch]);
    set("in_bass", in.bass);
    set("in_mid", in.mid);
    set("in_high", in.high);
}

bool thVideoSynth::render (const Frame &frame)
{
    synth_->process();
    rendered_ += window_;
    processed_ = true;

    const float *out = synth_->getOutput();
    const size_t pixels = (size_t)width_ * height_;

    for (int i = 0; i < window_; i++)
    {
        for (int c = 0; c < 3; c++)
            frame_[c * pixels + filled_] = out[(size_t)c * window_ + i];
        if (++filled_ < pixels)
            continue;
        filled_ = 0;
        if (!frame(frames_, frame_.data(), frame_.data() + pixels,
                   frame_.data() + 2 * pixels))
            return false;
        frames_++;
    }
    return true;
}

bool thVideoSynth::advance (int64_t audio, int64_t audioRate, const Frame &frame)
{
    while ((rendered_ + window_) * audioRate <= audio * rate_)
        if (!render(frame))
            return false;
    return true;
}

bool thVideoSynth::finish (int64_t audio, int64_t audioRate, const Frame &frame)
{
    const int64_t frames = (audio * fps_ + audioRate - 1) / audioRate;

    while (frames_ < frames)
        if (!render(frame))
            return false;
    return true;
}

void thVideoSynth::skipTo (int64_t frame)
{
    const int64_t to = frame * width_ * height_;

    if (to <= rendered_)
        return;
    synth_->skip(to - rendered_);
    rendered_ = to;
    frames_ = frame;
    filled_ = 0;
}

void thVideoSynth::noteOn (int channel, int note, int velocity, float level,
                           const float *aux)
{
    if (!follow_.count(channel))
        return;
    synth_->addNote(channel, note, velocity, level, aux);
    down_.insert({ channel, note });
}

void thVideoSynth::noteOff (int channel, int note)
{
    if (!follow_.count(channel))
        return;
    synth_->delNote(channel, note);
    down_.erase({ channel, note });
}

void thVideoSynth::flush (int channel)
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

std::string thVideoSynth::narrow (void)
{
    if (narrowSaid_ || !processed_)
        return "";
    narrowSaid_ = true;
    for (int ch : follow_)
        if (synth_->getChannel(ch) != NULL && synth_->getChannel(ch)->numChannels() < 3)
            return dsp_ + " has fewer than 3 outputs, so it draws in red only: "
                   "give its ionode `channels = 3'";
    return "";
}
