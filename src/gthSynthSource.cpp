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
    : synth_(synth), channels_(0), fill_(0), pos_(0),
      intail_(0), incount_(0), infed_(false), inserved_(false),
      indropped_(0), instarved_(0)
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

    /* And the capture accumulator: a window plus a device period, so that a
       period *larger* than a window still fits whole. maxFrames is what the
       header says it is in the interface for, and this is the side that
       wanted it. Two windows at the least, for a host that passed 0. */
    {
        const unsigned len = (unsigned)synth_->getWindowlen();
        const unsigned room = (maxFrames > len) ? maxFrames : len;

        incoming_.assign((size_t)len + room, 0.0f);
        inwindow_.assign(len ? len : 1, 0.0f);
    }

    intail_    = 0;
    incount_   = 0;
    infed_     = false;
    inserved_  = false;
    indropped_ = 0;
    instarved_ = 0;

    /* thSynth starts with no window rendered; gthJackAudio used to prime it
       by calling process() once at construction. Do it here instead, so the
       first callback has something to hand out rather than a window of
       silence. */
    synth_->process();
}

/* Audio thread. See the header for the two things this costs. */
void gthSynthSource::feedInput (const float *in, unsigned frames,
                                unsigned channels)
{
    if (in == NULL || frames == 0 || channels == 0 || incoming_.empty())
        return;

    infed_ = true;

    const unsigned capacity = (unsigned)incoming_.size();

    /* Dropped whole, for thSampleRing's reason: half a period accumulated is
       a discontinuity a graph would read as signal, and a graph reading a
       click it cannot account for is worse than one reading a gap that was
       counted. */
    if (frames > capacity - incount_)
    {
        indropped_ += frames;
        return;
    }

    unsigned at = (intail_ + incount_) % capacity;

    for (unsigned i = 0; i < frames; i++)
    {
        /* Summed and then scaled by the channel count, which is what
           plugins/osc/sampleslot.h does to a stereo wav and for the same
           reason: two sides carrying one signal come out at the level they
           went in. */
        double sum = 0;

        for (unsigned c = 0; c < channels; c++)
            sum += in[(size_t)i * channels + c];

        incoming_[at] = (float)(sum / channels);

        if (++at == capacity)
            at = 0;
    }

    incount_ += frames;
}

/* Audio thread, from produce(), immediately before the process() that will
   render with it. */
void gthSynthSource::feedCapture (void)
{
    /* Nothing has ever been fed: leave thSynth's buffer at the zeros it was
       allocated with. An offline render must not differ by so much as a write
       for having been built against a host that can capture. */
    if (!infed_)
        return;

    const unsigned len = (unsigned)synth_->getWindowlen();

    if (len == 0 || incoming_.empty())
        return;

    if (incount_ < len)
    {
        /* A window with no capture ready is silence and not the last one
           again, for the reason thChanEffect gives about a side channel going
           away. Held frames stay held: they are the front of the next
           window. */
        if (inserved_)
            instarved_++;

        synth_->feedCapture(NULL, 0);
        return;
    }

    const unsigned capacity = (unsigned)incoming_.size();
    const unsigned first = (intail_ + len > capacity) ? capacity - intail_ : len;

    memcpy(&inwindow_[0], &incoming_[intail_], (size_t)first * sizeof(float));

    if (first < len)
        memcpy(&inwindow_[first], &incoming_[0],
               (size_t)(len - first) * sizeof(float));

    intail_ = (intail_ + len) % capacity;
    incount_ -= len;

    inserved_ = true;

    synth_->feedCapture(&inwindow_[0], len);
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

    /* What the machine is hearing, for the window about to be rendered. The
       one before it is what is being handed out above, which is where the
       window of latency in the header comes from. */
    feedCapture();

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
