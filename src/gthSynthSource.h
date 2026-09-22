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
 *
 * ---- and the same problem backwards, for a live input ----
 *
 * A host capturing audio hands over a device period and the synth wants a
 * window, so feedInput() is the mirror of the buffer above: periods are
 * accumulated until a window's worth is there, and that window is handed to
 * thSynth::feedCapture just before the process() that renders with it. Same
 * thread on both ends again, so again no ring in the lock-free sense -- a
 * plain circular buffer with a count.
 *
 * TWO CONSEQUENCES WORTH KNOWING, because they are what a live input costs
 * and neither is a bug.
 *
 * A LIVE GRAPH HEARS ONE WINDOW LATE, AND SOMETIMES TWO. Two things add up,
 * and they are worth telling apart because only the second one is a choice.
 *
 * One is produce() being a window ahead: it hands out the window the synth has
 * already rendered and *then* renders the next, which is what prepare() primes
 * it for -- otherwise the first callback has nothing but silence to give. The
 * capture fed to that render is heard one window later. Reordering it to
 * render-then-copy would remove that window and would change output timing for
 * every host, so it is not something to fix here.
 *
 * The other is the accumulation. A window cannot be served until a window has
 * arrived, and produce() asks at the top of the window it is rendering -- so
 * where the device period is *smaller* than the window, the first ask comes
 * with only a period in hand, the window is rendered with silence, and the
 * capture lands in the window after it. That costs a second window, and it
 * costs it steadily rather than only at the start.
 *
 *     device period == window      one window
 *     device period <  window      two
 *
 * Which makes the arithmetic:
 *
 *     window   rate       period == window   period < window
 *     1024     44.1 kHz   23.2 ms            46.4 ms
 *      256     48 kHz      5.3 ms            10.7 ms
 *      128     48 kHz      2.7 ms             5.3 ms
 *
 * and makes a window equal to the device period worth having twice over. In a
 * browser the period is the worklet's quantum of 128, which is exactly why a
 * page that wants a live input asks for a window of 128 rather than the 256 it
 * otherwise runs at: 2.7 ms against 10.7.
 *
 * AND THE ALIGNMENT DEPENDS ON THE BLOCK SIZE, which is the one place a live
 * input breaks the property dspblock exists to check. The *samples* a graph
 * sees are the samples the device captured, in order, whatever the period --
 * but which window boundary the accumulation completes on is a function of the
 * period, so two runs at two block sizes put the same capture against
 * different windows of the same piece, and the output is not bit-identical.
 * scripts/dspblock therefore checks a live graph differently: the capture the
 * graph reconstructs is held to be identical, and the render is not. See
 * scripts/dspcapture.cpp.
 *
 * Nothing is fed to the synth until something has fed this, so every offline
 * path -- genwav, gencheck, dspcheck -- leaves thSynth's capture buffer at the
 * zeros it was allocated with and renders reproducibly.
 */
class gthSynthSource : public gthAudioSource {
public:
    explicit gthSynthSource (thSynth *synth);

    void prepare (unsigned maxFrames, unsigned channels);
    void render (float *out, unsigned frames, unsigned channels);

    /* Audio thread. `in' is `frames' frames of `channels' interleaved capture,
       as the device handed it over, or NULL for a host with nothing to give.
       Summed to mono -- thSynth::feedCapture says why the capture is mono --
       and accumulated into a window. Called from the same callback as
       render(), before it. */
    void feedInput (const float *in, unsigned frames, unsigned channels);

    /* Frames produced but not yet handed to a callback. Test hook. */
    unsigned pending (void) const { return fill_ - pos_; }

    /* Capture frames dropped because a whole period did not fit, and windows
       rendered with no capture ready *after the stream got going*. Both should
       stay at zero on a host whose periods and windows are what it says they
       are; a run that finds either moving has found the accumulator too small
       or the host feeding at the wrong rate.

       Neither is counted before the first feedInput, so a host that captures
       nothing reports nothing -- and a starve is not counted until a window
       has actually been served, because the one or two windows it takes for a
       period that does not divide the window to accumulate the first one are
       the stream starting rather than a gap in it. A counter with known-benign
       nonzero values is a counter nobody reads. */
    unsigned long inputDropped (void) const { return indropped_; }
    unsigned long inputStarved (void) const { return instarved_; }

    /* Capture frames waiting for a window to complete. Test hook. */
    unsigned inputPending (void) const { return incount_; }

private:
    void produce (unsigned channels);
    void feedCapture (void);

    thSynth *synth_;

    /* One window, interleaved. Sized in prepare() so that render() never
       allocates. */
    std::vector<float> window_;

    unsigned channels_;   /* what window_ is currently interleaved for */
    unsigned fill_;       /* frames held */
    unsigned pos_;        /* frames already handed out */

    /* The capture accumulator. Circular, mono, and sized in prepare() to a
       window plus a device period, which is what makes a period larger than a
       window fit -- the case `maxFrames' is in the interface for. */
    std::vector<float> incoming_;
    unsigned intail_;     /* the next frame to hand the synth */
    unsigned incount_;    /* frames waiting */

    /* One window, contiguous, because feedCapture takes a pointer and a
       length and the accumulator's window may wrap. */
    std::vector<float> inwindow_;

    bool infed_;          /* feedInput has been called at all       */
    bool inserved_;       /* a window has been handed over; see above */

    unsigned long indropped_;
    unsigned long instarved_;
};

#endif /* GTH_SYNTHSOURCE_H */
