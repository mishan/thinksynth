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

#ifndef THC_MIDIFILE_H
#define THC_MIDIFILE_H

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "libthink/thcomposer.h"

/* A piece's delivered events as a Standard MIDI File.
 *
 * Fed from thcScheduler::sigDelivered, so what lands in the file is what
 * the synth was told to play: after the chains' transforms, the section
 * gates and the mutes. Format 1 at a fixed tempo -- the piece's -- so a
 * DAW's bar grid lines up with the piece's beats: track 0 carries the
 * tempo, the meter and a marker per section, and each engine channel that
 * played a note gets a track of its own, named for its instrument.
 *
 * Only notes are written. A chanarg has no CC number and no range a MIDI
 * device would agree with, and a swap or a node-arg edit means nothing
 * outside this synth, so those are counted (skipped()) and left out.
 *
 * MIDI has one voice per key per channel, and the engine does not: a note
 * retriggered before its off gets its off moved up to the retrigger, and
 * a note that ends up with no length is dropped. end() closes everything
 * still sounding at the time the transport stopped, which is where the
 * synth's own flush ends it too.
 */
class thcMidiFile
{
public:
    /* `tempo' in beats per minute; `meter' in beats to the bar, written as
       n/4 where it is a whole number and left out where it is not. */
    explicit thcMidiFile (double tempo, double meter = 4,
                          int division = 480);

    void setName (const std::string &name) { name_ = name; }
    void setChannelName (int channel, const std::string &name);
    void addMarker (double at, const std::string &text);

    /* Notes and note-offs are kept; anything else is counted. */
    void add (const thcEvent &ev);

    /* Every note still open or still sounding at `at' ends there. */
    void end (double at);

    std::vector<uint8_t> bytes (void) const;
    bool write (const std::string &path) const;

    size_t notes (void) const;
    size_t skipped (void) const { return skipped_; }

private:
    struct Note
    {
        int      key, velocity;
        uint32_t on, off;
        bool     open;       /* a held note waiting for its NOTEOFF   */
    };

    uint32_t ticks (double seconds) const;
    static std::vector<Note> playable (const std::vector<Note> &notes);

    double      tempo_, meter_;
    int         division_;
    std::string name_;
    size_t      skipped_;
    double      endAt_;
    bool        ended_;

    std::map<int, std::string>        channelNames_;
    std::map<int, std::vector<Note> > channels_;
    std::vector<std::pair<uint32_t, std::string> > markers_;
};

#endif /* THC_MIDIFILE_H */
