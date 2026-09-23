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
 */

#ifndef TH_SOUNDFILE_H
#define TH_SOUNDFILE_H 1

/*
 * Sounds as vectors of samples at the synth's rate: read from a WAV, or
 * rendered from a patch. What thSoundFeat.h compares and what a search
 * or an audition renders to.
 */

#include <string>
#include <utility>
#include <vector>

#include "think.h"

class thSynth;

namespace thsound {

/* PCM of 16, 24 or 32 bits or 32-bit float, mixed to mono and brought to
   `rate' with a windowed sinc. False, with a sentence, for anything else. */
THINK_API bool readWav (const std::string &path, std::vector<float> &mono,
                        std::string &why, double rate = TH_DEFAULT_SAMPLES);

/* 16-bit mono at `rate', peaking 3 dB under full scale whatever the
   signal's own level: the distance does not hear gain, so what a search
   found has none worth keeping, and files at one level can be compared
   by ear. */
THINK_API bool writeWav (const std::string &path, const std::vector<float> &mono,
                         double rate = TH_DEFAULT_SAMPLES);

/* Windowed-sinc resampling, 32 taps a side, cut off below the lower of
   the two Nyquists. */
THINK_API void resample (std::vector<float> &x, double from, double to);

/* The MIDI note a recording is at, by YIN over the half second after its
   onset, or -1 for a sound with no pitch -- a drum, noise. */
THINK_API int detectNote (const std::vector<float> &x, double rate = TH_DEFAULT_SAMPLES);

/* One note of a patch, mixed to mono.

   `synth' is any synth nothing else is playing: the patch is loaded onto
   its channel 0, `chanargs' are set on it in the engine's own terms (the
   caller folds units), the note is held `holdWindows' windows and left
   to ring for `tailWindows' more. The synth's process() is driven here,
   so this is for a private synth on whatever thread owns it, never the
   one the audio thread is running.

   `effectPath', if not empty, is put on channel 0 after the patch, so
   `fx.' chanargs reach it; it hears only its own channel. */
THINK_API bool renderNote (thSynth &synth, const std::string &dspPath,
                           const std::vector<std::pair<std::string, float> > &chanargs,
                           int note, int holdWindows, int tailWindows,
                           std::vector<float> &mono,
                           const std::string &effectPath = std::string());

/* How long a note is auditioned, in windows: a second held and half a
   second of release. The release is half of what an envelope is, and a
   render that never lets go cannot hear it. */
const int HOLD_WINDOWS = 43;
const int TAIL_WINDOWS = 22;

} /* namespace thsound */

#endif /* TH_SOUNDFILE_H */
