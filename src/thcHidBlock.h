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

#ifndef TH_HID_BLOCK_H
#define TH_HID_BLOCK_H

#include <string>
#include <vector>

#include "thcGenFile.h"

/* What a HID controller's mapping says, as plain data.
 *
 * The same block is a device profile on its own (`device index { ... };')
 * and, later, the body of a chain's `input hid'. It lives beside the .gen
 * loader so both can read it with the .gen tokenizer, and it names controls
 * only as words: which words a device has is the HID library's business,
 * which depends on this and not the other way round. Every binding keeps its
 * line so that library can still say where a bad control name is. */

struct thcHidBinding
{
    enum Kind { NOTE, HOLD, TOGGLE, OCTAVE, VALUE, PITCH, VELOCITY, STRIKE,
                BEND, AUX, LOOP };
    enum Shape { LINEAR, EXP };

    Kind        kind;
    std::string control;
    int         line;

    int         semis;      /* NOTE, STRIKE: above the base; OCTAVE: the step;
                               AUX: which */
    std::string layer;      /* HOLD, TOGGLE */
    std::string arg;        /* VALUE; LOOP: record, undo or clear */
    int         channel;    /* NOTE, VALUE, STRIKE: 1-16; 0 inherits */
    bool        hasMin, hasMax;
    double      min, max;   /* VALUE, VELOCITY, STRIKE's velocity */
    double      at, off;    /* STRIKE, LOOP on an axis: on rising past at,
                               off below off */
    double      center;     /* VALUE, PITCH: where the axis counts from */
    double      smooth;     /* VALUE: seconds it takes to follow the axis */
    bool        latch;      /* NOTE: a press starts it, the next ends it */
    bool        both;       /* BEND, AUX: the other hand's notes too */
    Shape       shape;      /* VALUE */
    bool        patch;      /* from a patch's controller block */
    bool        octaves;    /* PITCH: steps of an octave, not a semitone */
    int         range;      /* PITCH: steps from the base note, either side
                               of it on a bipolar axis; BEND: semitones */
    std::vector<int> scale; /* PITCH: its steps are these degrees, semitones
                               above the base within an octave, if any */

    thcHidBinding ()
        : kind(NOTE), line(0), semis(0), channel(0), hasMin(false),
          hasMax(false), min(0), max(0), at(0.6), off(0.4), center(0),
          smooth(0), latch(false), both(false), shape(LINEAR), patch(false),
          octaves(false), range(0) {}
};

struct thcHidLayer
{
    std::string name;
    int         line;
    int         channel;    /* 1-16; 0 inherits the device's */
    int         base;       /* MIDI note; -1 inherits the device's */

    /* A posture: on while each of these axes is at least its value. */
    std::vector<std::pair<std::string, double> > when;
    int         whenLine;

    std::vector<thcHidBinding> bindings;

    thcHidLayer () : line(0), channel(0), base(-1), whenLine(0) {}
};

struct thcHidBlock
{
    std::string device;
    std::string hand;       /* "left", "right", or empty for either */
    int         line;       /* the device's, for an error about it */
    int         channel;    /* 1-16; 0 plays the host's current channel */
    int         base;       /* MIDI note */
    int         velocity;   /* what buttons play at with no velocity source */

    std::vector<thcHidBinding> bindings;
    std::vector<thcHidLayer>   layers;

    /* What a profile of changes says it sets, and drops from what it
       changes: `ignore NAME;', a control or a layer. */
    bool                       hasChannel, hasBase, hasVelocity;
    std::vector<std::pair<std::string, int> > ignore;   /* name, line */

    thcHidBlock () : line(0), channel(0), base(60), velocity(100), hasChannel(false),
                     hasBase(false), hasVelocity(false) {}
};

/* The body of a block, from the `{' at toks[pos] through its closing `}'.
 * On success pos is one past the `}'. On failure err and errLine say what
 * and where. */
bool thcHidParseBlock (const std::vector<thcGenToken> &toks, size_t &pos,
                       thcHidBlock &out, std::string &err, int &errLine);

/* A whole profile file: `device NAME { ... };' and nothing else. */
bool thcHidParseProfile (const std::string &text, thcHidBlock &out,
                         std::string &err, int &errLine);

/* A profile of a device a hand holds: `device NAME { ... };' for either
   hand, and `device NAME left { ... };' and `device NAME right { ... };'
   for one, each at most once, in any order. */
bool thcHidParseProfiles (const std::string &text,
                          std::vector<thcHidBlock> &out, std::string &err,
                          int &errLine);

#endif /* TH_HID_BLOCK_H */
