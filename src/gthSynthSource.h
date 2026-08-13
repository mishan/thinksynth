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

#ifndef GTH_SYNTHSOURCE_H
#define GTH_SYNTHSOURCE_H 1

#include <vector>

#include "gthAudio.h"

class thSynth;

/* Turns thSynth's fixed-size windows into whatever block size the device
 * asked for.
 *
 * thSynth::process() produces exactly getWindowlen() frames per call and
 * cannot be asked for fewer. A device callback asks for whatever it asks
 * for. Those two numbers only matched by luck: JACK's default period is 1024
 * and so is TH_DEFAULT_WINDOW_LENGTH, and the ALSA path called Write() with
 * exactly getWindowlen() frames.
 *
 * What the old JACK callback did when they disagreed (main.cpp, before this
 * change):
 *
 *     int copy = ((int)nframes < l) ? (int)nframes : l;
 *     for (int k = 0; k < copy; k++)
 *         buf[k] = thClampSample(synthbuffer[k]);
 *     if ((int)nframes > copy)
 *         memset(buf + copy, 0, ((int)nframes - copy) * sizeof(float));
 *     process_synth();
 *
 * With a 512-frame period and a 1024-frame window, samples 512..1023 of every
 * window were thrown away and a fresh window generated -- the synth ran at
 * double speed with every other half-window dropped. With a 2048-frame
 * period, 1024 frames of audio were followed by 1024 frames of silence,
 * repeating. Only nframes == windowlen was correct, and nothing enforced it.
 *
 * CoreAudio and WASAPI both negotiate their own buffer size, so the port
 * cannot avoid this. Hence a leftover buffer: a window is produced once,
 * handed out in whatever sized pieces are asked for, and the next one is
 * produced only when the last is exhausted.
 *
 * Deliberately not a lock-free ring. Both the producing and the consuming
 * end run on the audio thread -- the callback calls process() itself when it
 * runs dry -- so there is nothing to synchronise. The cost is that a
 * callback which crosses a window boundary does a whole window's work; the
 * alternative is a second thread touching the graph, and thSynth's command
 * queue is single-consumer by construction.
 */
class gthSynthSource : public gthAudioSource {
public:
    explicit gthSynthSource (thSynth *synth);

    void prepare (unsigned maxFrames, unsigned channels);
    void render (float *out, unsigned frames, unsigned channels);

    /* Frames produced but not yet handed to a callback. Test hook. */
    unsigned pending (void) const { return fill_ - pos_; }

private:
    void produce (unsigned channels);

    thSynth *synth_;

    /* One window, interleaved. Sized in prepare() so that render() never
       allocates. */
    std::vector<float> window_;

    unsigned channels_;   /* what window_ is currently interleaved for */
    unsigned fill_;       /* frames held */
    unsigned pos_;        /* frames already handed out */
};

#endif /* GTH_SYNTHSOURCE_H */
