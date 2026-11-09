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

#include <math.h>

#include <algorithm>

#include "think.h"
#include "thSynth.h"
#include "thVideoBridge.h"

thVideoBridge::thVideoBridge (void)
    : mix_((size_t)TH_MAX_CHANNELS * N, 0.0f),
      squares_((size_t)TH_MIDI_CHANNELS * N, 0.0),
      magnitudes_(fft_.bins(), 0.0f)
{
}

void thVideoBridge::hear (const thSynth &audio)
{
    const int window = audio.getWindowlen();
    /* A window longer than the ring: only its last N samples are kept. */
    const int skip = std::max(0, window - N), n = window - skip;
    const float *out = audio.getOutput();

    const int mixChannels = std::min(audio.audioChannelCount(), TH_MAX_CHANNELS);

    /* Planes the mix had no part in until now start silent. */
    if (mixChannels > mixChannels_)
        std::fill(mix_.begin() + (size_t)mixChannels_ * N,
                  mix_.begin() + (size_t)mixChannels * N, 0.0f);
    mixChannels_ = mixChannels;
    channels_ = std::min(audio.midiChanCount(), TH_MIDI_CHANNELS);
    sampleRate_ = (double)audio.getSampleRate();

    for (int i = 0; i < n; i++)
    {
        const int slot = (at_ + i) % N;

        for (int c = 0; c < mixChannels_; c++)
            mix_[(size_t)c * N + slot] = out[(size_t)c * window + skip + i];
    }

    for (int ch = 0; ch < channels_; ch++)
    {
        int outputs = 0;
        const float *own = audio.getChannelOutput(ch, &outputs);

        for (int i = 0; i < n; i++)
        {
            double sum = 0;

            for (int o = 0; own != NULL && o < outputs; o++)
            {
                const double x = own[(size_t)o * window + skip + i];

                sum += x * x;
            }
            squares_[(size_t)ch * N + (at_ + i) % N] = outputs > 0 ? sum / outputs : 0;
        }
    }

    at_ = (at_ + n) % N;
}

void thVideoBridge::inputs (thVideoInputs &out)
{
    const size_t samples = (size_t)mixChannels_ * N;
    double sum = 0, dc = 0;

    /* Oldest first, plane by plane: one 1024-sample window's order. */
    for (int c = 0; c < mixChannels_; c++)
        for (int i = 0; i < N; i++)
        {
            const double x = mix_[(size_t)c * N + (at_ + i) % N];

            sum += x * x;
            dc += x;
        }
    out.level = samples ? (float)sqrt(sum / samples) : 0;
    dc = samples ? dc / samples : 0;

    out.channels = channels_;
    for (int ch = 0; ch < channels_; ch++)
    {
        double own = 0;

        for (int i = 0; i < N; i++)
            own += squares_[(size_t)ch * N + (at_ + i) % N];
        out.levels[ch] = (float)sqrt(own / N);
    }

    const auto mono = [&](unsigned int i)
    {
        float sum = 0;

        for (int c = 0; c < mixChannels_; c++)
            sum += mix_[(size_t)c * N + (at_ + i) % N];
        return mixChannels_ ? sum / mixChannels_ - (float)dc : 0.0f;
    };
    const double hz = sampleRate_ / fft_.size();

    fft_.magnitude(mono, magnitudes_.data());
    out.bass = band(1, 250 / hz);
    out.mid = band(250 / hz, 2000 / hz);
    out.high = band(2000 / hz, fft_.bins());
}

/* A Hann window spreads a sine over bins whose squares sum to 1.5 times its
   peak's: divided out, a full-scale sine reads 1 wherever it falls. */
float thVideoBridge::band (double lo, double hi) const
{
    const unsigned int first = (unsigned int)ceil(lo);
    const unsigned int last = std::min((unsigned int)ceil(hi), fft_.bins());
    double sum = 0;

    for (unsigned int b = first; b < last; b++)
        sum += (double)magnitudes_[b] * magnitudes_[b];
    return (float)std::min(1.0, sqrt(sum / 1.5));
}
