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

#ifndef GTH_RTAUDIO_H
#define GTH_RTAUDIO_H 1

#include <atomic>

#include <RtAudio.h>

#include "gthAudio.h"

/* The one audio backend.
 *
 * RtAudio covers ALSA, JACK, PulseAudio and OSS on Linux, CoreAudio and JACK
 * on macOS, and WASAPI, DirectSound and ASIO on Windows, behind a single
 * callback-driven API -- which is why it replaces both gthALSAAudio and
 * gthJackAudio rather than joining them.
 *
 * `api' is the RtAudio API name as the user spells it on the command line
 * ("alsa", "jack", "pulse", "core", "wasapi", "asio"), or empty to let
 * RtAudio choose. That keeps `-d jack' and `-d alsa' meaning what they
 * always meant, while the implementation behind them stops being ours.
 *
 * Written against RtAudio 6, which reports errors by return code and error
 * callback rather than by throwing, and which identifies devices by an
 * opaque id rather than by index.
 */
class gthRtAudio : public gthAudio {
public:
    explicit gthRtAudio (const std::string &api = "",
                         const std::string &device = "");
    ~gthRtAudio (void);

    bool open (const gthAudioFmt &want, gthAudioSource *source);
    bool start (void);
    void stop (void);

    bool running (void) const;

    const gthAudioFmt &format (void) const { return fmt_; }
    std::string deviceName (void) const { return deviceName_; }

    std::vector<gthAudioDevice> devices (void) const;

    /* Whether the constructor managed to bring RtAudio up at all. */
    bool valid (void) const { return rt_ != NULL; }

    /* The API actually in use, for the status line and for prefs. */
    std::string apiName (void) const;

    /* Callbacks that arrived with the device reporting an output underflow,
       i.e. the previous callback did not return in time. Counted rather than
       logged because the counting happens on the audio thread, where a
       printf is precisely the wrong thing to do. Reported once at stop(). */
    unsigned long underruns (void) const {
        return underruns_.load(std::memory_order_relaxed);
    }

    static std::vector<std::string> availableApis (void);

private:
    static int trampoline (void *out, void *in, unsigned frames,
                           double streamTime, RtAudioStreamStatus status,
                           void *user);

    unsigned resolveDevice (const std::string &name) const;

    RtAudio *rt_;
    gthAudioSource *source_;
    gthAudioFmt fmt_;

    std::string wantDevice_;
    std::string deviceName_;

    unsigned deviceId_;

    std::atomic<unsigned long> underruns_;
};

#endif /* GTH_RTAUDIO_H */
