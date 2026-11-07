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
 */

#ifndef THC_MIDIFILE_H
#define THC_MIDIFILE_H

#include <stdint.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "libthink/thcomposer.h"

/* A piece's delivered events as a Standard MIDI File.
 *
 * Fed from thcScheduler::sigDelivered, so what lands in the file is what
 * the synth was told to play: after the chains' transforms, the section
 * gates and the mutes. Format 1 at a fixed tempo -- the piece's -- so a
 * DAW's bar grid lines up with the piece's beats. Track 0 carries the
 * tempo, the meter and a marker per section; after it comes a track per
 * chain, named for the chain, so two chains sharing a channel (a closed
 * and an open hat) stay two parts to edit.
 *
 * What each event becomes:
 *
 *   note      a note-on and note-off on the event's channel, at least a
 *             tick long. MIDI has one voice per key per channel, as the
 *             engine does, and that is applied across tracks: a note
 *             ends where the next on its channel and key starts, from
 *             whichever chain, and notes struck together on one key are
 *             one note, as long and as loud as the longest and loudest,
 *             in the longest one's track. A note cut by another track's
 *             ends a tick early, since readers order two tracks' events
 *             at one tick as they like. So the merged stream is what
 *             the engine sounded, and a hit two chains double is in one
 *             of their tracks only.
 *   level     CC 11, expression, as round(100 * level): 1 is 100. Sent
 *             before a note whose level differs from what its track or
 *             its channel last sent -- so before each track's first note
 *             on a channel that sends any -- and a DAW instrument per
 *             track and one instrument per channel both hear it. A
 *             section fade is one step at the boundary; an accent moves
 *             the notes still sounding with it, and notes struck together
 *             on a channel share the loudest one's level.
 *   chanarg   a controller, scaled from the arg's range to 0..127. Each
 *             channel's chanargs take CC numbers in the order they first
 *             appear, from the ones MIDI leaves undefined (20-31, then
 *             102-119), and a text event at the start of the track says
 *             which is which. With fine controllers on, a 14-bit pair
 *             (20-31 and 52-63) instead, twelve to a channel.
 *   swap, node-arg edit
 *             a text event: nothing outside this synth can act on them.
 *
 * A chanarg, a swap or an edit belongs to the channel rather than the
 * chain that sent it, so it is written into every track that plays notes
 * on that channel, or into a track of its own where none does.
 *
 * end() closes every note still sounding at the time the transport
 * stopped, which is where the synth's own flush ends it too.
 *
 * Limits: a chanarg's range is asked for once, when it first appears, so
 * after a swap puts a different graph on the channel its values are
 * still scaled through the first one's. And an arg declared with no
 * range has thArg's default of 0..127, which counts as declared; the
 * range as played stands in only where the channel has no such arg.
 */
class thcMidiFile
{
public:
    /* A chanarg's declared range, asked once per channel and name. False
       where there is none, and the range the piece used stands in. */
    typedef std::function<bool (int channel, const std::string &name,
                                double &min, double &max)> RangeLookup;

    /* `tempo' in beats per minute; `meter' in beats to the bar, written as
       n/4 where it is a whole number and left out where it is not. */
    explicit thcMidiFile (double tempo, double meter = 4,
                          int division = 480);

    void setName (const std::string &name) { name_ = name; }
    void setChainName (int chain, const std::string &name);
    void setChannelName (int channel, const std::string &name);
    void setRangeLookup (const RangeLookup &fn) { range_ = fn; }
    void setFineControllers (bool on) { fine_ = on; }
    void addMarker (double at, const std::string &text);

    /* `chain' as thcScheduler::deliveringChain() says; -1 files the event
       under its channel. */
    void add (const thcEvent &ev, int chain = -1);

    /* Every note still open or still sounding at `at' ends there. */
    void end (double at);

    std::vector<uint8_t> bytes (void) const;
    bool write (const std::string &path) const;

    size_t notes (void) const;
    size_t skipped (void) const { return skipped_; }

private:
    struct Note
    {
        int      part;       /* chain, or -1 - channel                 */
        size_t   seq;        /* delivery order                         */
        int      channel, key, velocity;
        float    level;
        uint32_t on, off;
        bool     open;       /* a held note waiting for its NOTEOFF   */
    };

    /* One of a channel's chanargs, and where it goes. */
    struct Control
    {
        std::string name;
        int         cc;
        bool        declared;
        double      min, max;     /* declared, or the values seen      */
    };

    /* A channel's chanarg values and text, in delivery order. */
    struct ChannelEvent
    {
        uint32_t    tick;
        int         control;      /* into controls_, or -1 for text    */
        double      value;
        std::string text;
    };

    uint32_t ticks (double seconds) const;
    int      control (int channel, const std::string &name);
    std::vector<Note> resolve (void) const;

    double      tempo_, meter_;
    int         division_;
    std::string name_;
    size_t      skipped_;
    double      endAt_;
    bool        ended_;
    bool        fine_;
    RangeLookup range_;

    std::vector<Note>          notes_;
    std::map<int, std::string> chainNames_, channelNames_;

    std::map<int, std::vector<Control> >      controls_;
    std::map<int, std::vector<ChannelEvent> > channelEvents_;
    std::vector<std::pair<uint32_t, std::string> > markers_;
};

#endif /* THC_MIDIFILE_H */
