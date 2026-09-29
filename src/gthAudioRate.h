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

#ifndef GTH_AUDIORATE_H
#define GTH_AUDIORATE_H 1

#include <string>

class gthAudio;
class gthSynthSource;
class thSynth;

/* Makes the synth run at the rate the device came up at.
 *
 * The synth is made before the device is opened, at the rate asked for on
 * the command line, and a device is free to open at another one: a JACK
 * server runs at its own rate, and a card that only does 48 kHz does 48
 * kHz. A synth's rate is fixed for its life (thSynth::getSampleRate), so a
 * synth at 44.1 kHz feeding a 48 kHz device played every note about 9%
 * sharp and every envelope and delay that much short, with nothing but a
 * line on stdout to say so.
 *
 * If `audio' is running at a rate other than `synth''s, this stops it,
 * replaces `synth' and `source' with ones at the device's rate -- deleting
 * the old ones -- and opens and starts `audio' again on the new source.
 * True if it did. Only while nothing is loaded onto the synth and nothing
 * else holds it: at startup, before MIDI is connected and before the
 * preferences put patches on channels. */
bool gthFollowDeviceRate (gthAudio *audio, thSynth *&synth,
                          gthSynthSource *&source,
                          const std::string &pluginPath);

#endif /* GTH_AUDIORATE_H */
