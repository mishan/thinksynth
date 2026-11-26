/*
 * Copyright (C) 2026 The thinksynth authors
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

#ifndef TH_TUNING_H
#define TH_TUNING_H 1

/*
 * Scala tunings: a scale (.scl) and a keyboard mapping (.kbm), read into a
 * pitch for each of the 128 keys.
 *
 * A pitch is a fractional note in 12-tone equal temperament, 69 being
 * A440: the value a voice's `note' already carries, so misc::midi2freq
 * turns a tuned key into its frequency unchanged. It is worked out in
 * cents from the reference key and added to the reference's note, never
 * through hertz and back, so 12-TET (degrees in cents, the period 1200.0 or
 * 2/1) with no keymap, or with a linear keymap retuning every key whose
 * reference is 69 at 440 Hz, gives every key exactly itself.
 *
 * The formats are Scala's: huygens-fokker.org/scala/scl_format.html, and
 * the .kbm description in Scala's help. A map entry past the scale wraps
 * into the next period, and a formal octave of 0 is the scale's period,
 * both as Scala reads them. Lenient where it costs nothing: blank lines
 * before the count and between degrees. Deliberate refusals, each said in
 * the error: a scale of no degrees (the spec allows it, but it has no
 * period); a period at or below unison (nothing forbids it, but a keyboard
 * on it means nothing); a number that goes on past a blank (`3 / 2',
 * `100 .0'), which read as a value and a label would be another pitch; and
 * a value not ended by a blank or `!' (`100.0c').
 *
 * Text in, not file names: a piece may carry its tuning inline, and the
 * browser hands over strings. An error is a message and, separately, the
 * 1-based line, so a caller can offset it into a file of its own. A parse
 * or build that fails leaves its object as it was.
 */

#include <climits>
#include <string>
#include <vector>

#include "thExport.h"

struct THINK_API thScale {
    std::string description;
    std::vector<double> cents;      /* degrees 1..N; back() is the period */

    bool parse (const std::string &text, std::string &err, int &errLine);
    bool operator== (const thScale &o) const;
};

struct THINK_API thKeymap {
    /* As constructed: the mapping a scale gets with no .kbm. Every key
       retuned, one degree a key, degree 0 on middle C at its 12-TET
       pitch, held as the note itself so that no rounding of the
       frequency gets into it. */
    int size = 0;                   /* 0: one degree a key */
    int first = 0, last = 127;      /* keys retuned; the rest unmapped */
    int middle = 60;                /* the key degree 0 sits on */
    int reference = 60;
    /* The reference's pitch, twice: as written, and as the note build
       uses. They must agree to 1e-9; the note is kept so that the default
       can be 60 itself rather than a rounding of it. */
    double frequency = 261.6255653005986;
    double referenceNote = 60;
    int periodDegree = 0;           /* the degree one repeat of the map spans;
                                       0 for the scale's period */

    static constexpr int UNMAPPED = INT_MIN;
    std::vector<int> map;           /* a degree per slot, or UNMAPPED; slots
                                       past its end are unmapped too */

    bool parse (const std::string &text, std::string &err, int &errLine);
    bool operator== (const thKeymap &o) const;
};

class THINK_API thTuning {
public:
    static constexpr int KEYS = 128;

    thTuning (void);

    bool build (const thScale &scale, const thKeymap &keymap, std::string &err);

    /* Until a build, no key is mapped. */
    bool mapped (int key) const;
    double note (int key) const;

    bool operator== (const thTuning &o) const;

private:
    bool mapped_[KEYS];
    double note_[KEYS];
};

#endif /* TH_TUNING_H */
