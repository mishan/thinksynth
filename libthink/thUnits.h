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

#ifndef TH_UNITS_H
#define TH_UNITS_H 1

#include <string>

#include "think.h"

/* What a unit suffix in a `.dsp' means, and how to read it back.
 *
 * The language lets a value be written `5 ms' or `90%' instead of in the
 * raw terms the engine works in -- samples, and fractions of TH_MAX. Both
 * conversions are exact and exactly invertible, which is what lets an
 * editor show `7000 ms' rather than `288000.03' and write the file back
 * the way its author spelled it.
 *
 * There used to be three copies of this arithmetic: the grammar action
 * that folded, ArgTable that unfolded for display, and NodeEdit that did
 * both to round-trip a file. Three copies of a conversion is a slow leak
 * -- they agreed, but nothing made them agree -- and the sample rate is
 * what finally forced the issue.
 *
 * `ms' takes a rate, because milliseconds are only samples once you know
 * how many samples a second is. The grammar used to fold with the
 * compile-time TH_SAMPLE, so `thinksynth -r 48000' played every envelope
 * in every patch 8.8% short: the audio backend opened at 48k and every
 * duration in every .dsp had been converted as though it had not. The
 * fold now happens at load time with the rate the synth is actually
 * running at, and this is the one piece of arithmetic that knows how.
 *
 * `%' takes the rate too, and ignores it. A percentage of TH_MAX has
 * nothing to do with time, and the parameter is there so that callers do
 * not have to know which units are which -- the point of having one
 * function is that a caller can hand it any unit string, including one
 * the author wrote as a label (`@x.units = "Hz"'), and get back something
 * sensible. A unit nothing folded is a unit nothing should unfold.
 */
inline double
thFoldUnit (double literal, const std::string &units, long sampleRate)
{
    if (units == "ms")
        return literal * (double)sampleRate / 1000.0;

    if (units == "%")
        return literal * (double)TH_MAX / 100.0;

    return literal;
}

inline double
thUnfoldUnit (double value, const std::string &units, long sampleRate)
{
    if (units == "ms")
        return sampleRate ? value * 1000.0 / (double)sampleRate : value;

    if (units == "%")
        return value * 100.0 / (double)TH_MAX;

    return value;
}

/* True if `units' is one the language folds, as opposed to a label the
 * author attached for the panel to print. Callers that have to decide
 * whether a stored value is in engine terms or in the author's want this
 * rather than a list of their own. */
inline bool
thUnitIsFolded (const std::string &units)
{
    return units == "ms" || units == "%";
}

#endif /* TH_UNITS_H */
