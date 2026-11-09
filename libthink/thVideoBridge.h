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
 * thVideoBridge -- what an audio synth's sound says to a video synth.
 *
 * It hears each audio window and keeps the last 1024 samples of the mix and
 * of each channel, so what it measures is the same whatever the window
 * length: one 1024-sample window under genwav, eight of 128 in a worklet.
 * hear() copies and nothing more, and allocates nothing, so it can run on
 * the audio thread; inputs() does the arithmetic, when it is wanted, on
 * the same thread: nothing here is shared safely between two.
 *
 * What it measures, over those 1024 samples:
 *
 *   level      the mix's RMS
 *   levels[c]  channel c's own RMS, before the master
 *   bass, mid, high
 *              the mix's amplitude under 250 Hz, to 2 kHz, and above, with
 *              its DC taken out: the root of the band's summed squared
 *              magnitudes, whatever its width, a full-scale sine 1, clamped
 *              to 0..1
 */

#ifndef TH_VIDEO_BRIDGE_H
#define TH_VIDEO_BRIDGE_H 1

#include <vector>

#include "think.h"
#include "thExport.h"
#include "thFFT.h"

class thSynth;

struct thVideoInputs
{
    float level = 0, bass = 0, mid = 0, high = 0;
    float levels[TH_MIDI_CHANNELS] = {};
    int   channels = 0;     /* how many of levels[] the synth has */
};

class THINK_API thVideoBridge
{
public:
    thVideoBridge (void);

    /* The window `audio' last rendered. Audio thread. */
    void hear (const thSynth &audio);

    void inputs (thVideoInputs &out);

private:
    float band (double lo, double hi) const;

    static const int N = TH_DEFAULT_WINDOW_LENGTH;

    thv::FFTR fft_ { 10 };
    std::vector<float> mix_;        /* TH_MAX_CHANNELS planes of N */
    std::vector<double> squares_;   /* per channel, each sample's mean square */
    std::vector<float> magnitudes_;
    int at_ = 0;                    /* the oldest sample's slot */
    int mixChannels_ = 0, channels_ = 0;
    double sampleRate_ = TH_DEFAULT_SAMPLES;
};

#endif /* TH_VIDEO_BRIDGE_H */
