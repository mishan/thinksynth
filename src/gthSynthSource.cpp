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

#include "config.h"

#include <string.h>

#include "think.h"

#include "gthSynthSource.h"

gthSynthSource::gthSynthSource (thSynth *synth)
    : synth_(synth), channels_(0), fill_(0), pos_(0)
{
}

/* GUI thread, before the stream starts. Everything render() needs is
   allocated here, because render() runs on the audio thread.

   maxFrames is unused: the leftover buffer is one synth window, not one
   device period, and a callback larger than a window simply drives more than
   one produce(). It is in the interface because a source that did want to
   size itself to the device period would need it. */
void gthSynthSource::prepare (unsigned maxFrames, unsigned channels)
{
    (void)maxFrames;

    if (channels == 0)
        channels = 1;

    channels_ = channels;

    window_.assign((size_t)synth_->getWindowlen() * channels, 0.0f);

    fill_ = 0;
    pos_  = 0;

    /* thSynth starts with no window rendered; gthJackAudio used to prime it
       by calling process() once at construction. Do it here instead, so the
       first callback has something to hand out rather than a window of
       silence. */
    synth_->process();
}

/* Audio thread. */
void gthSynthSource::produce (unsigned channels)
{
    const unsigned len = (unsigned)synth_->getWindowlen();
    const int have = synth_->audioChannelCount();

    if (len == 0 || have <= 0)
    {
        fill_ = 0;
        pos_  = 0;
        return;
    }

    /* prepare() sized this. If the window length changed underneath us the
       buffer is the wrong size, and growing it here would allocate on the
       audio thread -- so clamp instead and let the next prepare() catch up. */
    const unsigned capacity = (unsigned)(window_.size() / channels);
    const unsigned frames = (len < capacity) ? len : capacity;

    for (unsigned c = 0; c < channels; c++)
    {
        /* A mono DSP feeding a stereo device plays out of both, rather than
           out of the left only. */
        const int src = (c < (unsigned)have) ? (int)c : have - 1;

        const float *in = synth_->getChanBuffer(src);

        if (in == NULL)
        {
            for (unsigned i = 0; i < frames; i++)
                window_[i * channels + c] = 0.0f;
            continue;
        }

        /* The clamp the JACK callback used to do inline. A port expects
           -1..1 and the mix runs well past that with a couple of voices
           held down; converting an out-of-range float to an integer sample
           downstream is undefined and in practice wraps. */
        for (unsigned i = 0; i < frames; i++)
            window_[i * channels + c] = thClampSample(in[i]);
    }

    fill_ = frames;
    pos_  = 0;

    /* Generate the next window. Not RT-safe -- process() still builds
       std::strings in its inner loop and inserts into std::map -- but that
       is exactly what the JACK callback already did, and it is the separate
       item the TODO has always listed. */
    synth_->process();
}

/* Audio thread. Fills exactly `frames' frames, whatever the window length
   happens to be. */
void gthSynthSource::render (float *out, unsigned frames, unsigned channels)
{
    if (out == NULL || channels == 0)
        return;

    /* prepare() was called for a different channel count, or not at all.
       Silence beats reading off the end of window_. */
    if (channels != channels_ || window_.empty())
    {
        memset(out, 0, (size_t)frames * channels * sizeof(float));
        return;
    }

    unsigned done = 0;

    while (done < frames)
    {
        if (pos_ >= fill_)
        {
            produce(channels);

            if (fill_ == 0)   /* nothing to give; do not spin */
            {
                memset(out + (size_t)done * channels, 0,
                       (size_t)(frames - done) * channels * sizeof(float));
                return;
            }
        }

        unsigned take = fill_ - pos_;

        if (take > frames - done)
            take = frames - done;

        memcpy(out + (size_t)done * channels,
               &window_[(size_t)pos_ * channels],
               (size_t)take * channels * sizeof(float));

        pos_  += take;
        done  += take;
    }
}
