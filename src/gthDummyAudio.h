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

#ifndef GTH_DUMMYAUDIO_H
#define GTH_DUMMYAUDIO_H

#include "gthAudio.h"

/* No device. The GUI comes up, patches load and the keyboard works; nothing
   is heard. This is what -d none selects, and what everything falls back to
   when no backend will open. */
class gthDummyAudio : public gthAudio
{
public:
    gthDummyAudio (void);
    virtual ~gthDummyAudio (void);

    bool open (const gthAudioFmt &want, gthAudioSource *source);
    bool start (void);
    void stop (void);

    bool running (void) const { return running_; }

    const gthAudioFmt &format (void) const { return fmt_; }
    std::string deviceName (void) const { return "none"; }

    std::vector<gthAudioDevice> devices (void) const {
        return std::vector<gthAudioDevice>();
    }

private:
    gthAudioFmt fmt_;
    bool running_;
};

#endif /* GTH_DUMMYAUDIO_H */
