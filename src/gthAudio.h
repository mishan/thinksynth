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

#ifndef GTH_AUDIO_H
#define GTH_AUDIO_H 1

#include <string>
#include <vector>

/* The audio backend interface.
 *
 * This used to be push-shaped -- Write(float *, int len), plus Read() and
 * ProcessEvents() -- and exactly one of the three implementations used it
 * that way. gthALSAAudio ran its own thread on a private Glib::MainContext,
 * polling ALSA's descriptors and sleeping a millisecond between iterations.
 * gthJackAudio's Write() and Read() were no-ops; its real audio path was a
 * free function in main.cpp reaching for a global thSynth. Read() returned
 * -1 or 0 everywhere and nothing ever called it; ProcessEvents() returned
 * false everywhere and its ALSA body was dead code the author marked XXX.
 *
 * Every backend worth having is callback-driven, so the interface is now
 * shaped like one: the device asks for frames, and a gthAudioSource supplies
 * them.
 */

struct gthAudioFmt {
    int rate;             /* Hz */
    int channels;
    unsigned frames;      /* frames per callback; 0 means "device decides" */
};

struct gthAudioDevice {
    unsigned id;
    std::string name;
    unsigned outputChannels;
    bool isDefault;
};

/* What a backend pulls from.
 *
 * Keeping this separate from thSynth is what makes the block-size handling
 * testable without a sound card -- see scripts/dspblock.cpp.
 */
class gthAudioSource {
public:
    virtual ~gthAudioSource (void) { }

    /* Called on the audio thread. Fills exactly `frames' frames of
       `channels' interleaved samples, in -1..1. */
    virtual void render (float *out, unsigned frames, unsigned channels) = 0;

    /* Called before the stream starts, on the GUI thread, so a source can do
       its allocating where allocating is allowed. */
    virtual void prepare (unsigned maxFrames, unsigned channels) = 0;
};

class gthAudio {
public:
    virtual ~gthAudio (void) { }

    virtual bool open (const gthAudioFmt &want, gthAudioSource *source) = 0;
    virtual bool start (void) = 0;
    virtual void stop (void) = 0;

    virtual bool running (void) const = 0;

    /* What was actually negotiated, which is rarely exactly what was asked
       for -- the whole reason a source has to cope with any block size. */
    virtual const gthAudioFmt &format (void) const = 0;

    virtual std::string deviceName (void) const = 0;

    /* For the preferences UI. There was no way to enumerate devices at all
       before this. */
    virtual std::vector<gthAudioDevice> devices (void) const = 0;
};

#endif /* GTH_AUDIO_H */
