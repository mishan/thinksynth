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
 * thinkweb -- the synth, for an AudioWorklet.
 *
 * worklet.js holds one of these and calls a handful of things: load a .dsp,
 * press and release a key, render a block. The synth runs in windows of its
 * own length -- 256 in the browser, JAM.md section 2 -- and a worklet asks
 * for 128-frame quanta; gthSynthSource is the ring between the two, the same
 * one the sound card's callback uses on the desktop.
 *
 * Everything here runs on the worklet's thread, the thread the desktop calls
 * the GUI's included: addNote() queues a command, and the next process()
 * applies it. There is no second thread for the queue to protect, but it is
 * the same path, so the synth does not know it is in a browser.
 *
 * Commands carry the frame they apply at. A window is rendered whole, so a
 * command lands at the start of the window its frame falls in: early by less
 * than a window, never late, and at the same frame on every run and every
 * machine, which is what a stamp is for. One stamped in the past -- or -1 --
 * goes into the next window rendered, which is how a key pressed now plays.
 *
 * What a note does once it has landed is the engine's: a note added before
 * a window is silent through that window and sounds from the next, here as
 * on the desktop. So a note is heard one window after the window its stamp
 * falls in -- which is the other reason the browser wants a short window.
 */

#include "config.h"

#include <stdint.h>
#include <stdio.h>

#include <algorithm>
#include <vector>

#include <emscripten.h>

#include "think.h"

#include "gthSynthSource.h"

/* The synth always mixes to two; so does this. */
#define TW_CHANNELS 2

/* Where a .dsp handed over as text is written, so the parser can open it
   like any other. The module's own MEMFS; nothing leaves the page. */
#define TW_PATCH_FILE "/patch.dsp"

namespace {

struct Stamped
{
    double        frame;
    unsigned long seq;      /* equal frames apply in the order they came */
    bool          on;
    float         note;
    float         velocity;
};

thSynth             *synth_;
gthSynthSource      *source_;
std::vector<float>   block_;
std::vector<Stamped> pending_;
unsigned long        seq_;
double               rendered_;     /* frames handed out so far */

/* Everything due before the end of the window about to be rendered, whose
   first frame is `start'. */
void applyDue (double start, int len)
{
    std::stable_sort(pending_.begin(), pending_.end(),
                     [](const Stamped &a, const Stamped &b)
                     {
                         return a.frame != b.frame ? a.frame < b.frame
                                                   : a.seq < b.seq;
                     });

    size_t k = 0;

    for (; k < pending_.size() && pending_[k].frame < start + len; k++)
    {
        const Stamped &s = pending_[k];

        if (s.on)
            synth_->addNote(0, s.note, s.velocity);
        else
            synth_->delNote(0, s.note);
    }

    pending_.erase(pending_.begin(), pending_.begin() + k);
}

void push (double frame, bool on, float note, float velocity)
{
    Stamped s;

    s.frame = frame;
    s.seq = seq_++;
    s.on = on;
    s.note = note;
    s.velocity = velocity;

    pending_.push_back(s);
}

} /* namespace */

extern "C" {

/* A synth at the page's rate and the given window, rendering blocks of up
   to maxFrames. Returns the window length the synth actually took. */
EMSCRIPTEN_KEEPALIVE int tw_create (int sampleRate, int windowlen,
                                    int maxFrames)
{
    synth_ = new thSynth("", windowlen, sampleRate);
    source_ = new gthSynthSource(synth_);

    source_->prepare((unsigned)maxFrames, TW_CHANNELS);
    block_.assign((size_t)maxFrames * TW_CHANNELS, 0.0f);

    return synth_->getWindowlen();
}

/* A .dsp, as text, onto channel 0 in place of whatever was there. Nonzero
   if it parsed; the parser's complaints go to stderr, which the worklet
   forwards to the page. */
EMSCRIPTEN_KEEPALIVE int tw_load (const char *text)
{
    FILE *f = fopen(TW_PATCH_FILE, "wb");

    if (f == NULL)
        return 0;

    fputs(text, f);
    fclose(f);

    pending_.clear();

    /* At the level the Patch Selector loads one at, on MIDI's 0..127 --
       gthPatchfile.cpp says why that level is where it is. */
    return synth_->loadTree(TW_PATCH_FILE, 0, TH_DEFAULT_CHAN_AMP) != NULL
           ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void tw_note_on (double frame, float note, float velocity)
{
    push(frame, true, note, velocity);
}

EMSCRIPTEN_KEEPALIVE void tw_note_off (double frame, float note)
{
    push(frame, false, note, 0);
}

/* Every sounding note released, now. */
EMSCRIPTEN_KEEPALIVE void tw_all_off (void)
{
    pending_.clear();
    synth_->clearAll();
}

/* `frames' frames, interleaved stereo, in a buffer that is overwritten by
   the next call. Windows are rendered as they are needed and the stamped
   commands due in each are applied just before it. */
EMSCRIPTEN_KEEPALIVE const float *tw_render (int frames)
{
    const int len = synth_->getWindowlen();

    for (int done = 0; done < frames; )
    {
        unsigned held = source_->pending();

        /* Nothing held: the next render() makes a window, and its first
           frame is the next one handed out. */
        if (held == 0)
        {
            applyDue(rendered_, len);
            held = (unsigned)len;
        }

        const int n = std::min(frames - done, (int)held);

        source_->render(&block_[(size_t)done * TW_CHANNELS], (unsigned)n,
                        TW_CHANNELS);

        done += n;
        rendered_ += n;
    }

    return block_.data();
}

/* The frame the next tw_render starts at. */
EMSCRIPTEN_KEEPALIVE double tw_frame (void)
{
    return rendered_;
}

} /* extern "C" */
