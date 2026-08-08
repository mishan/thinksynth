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

#ifndef NODE_EDIT_H
#define NODE_EDIT_H 1

/*
 * Changing one value in a .dsp, by editing the text.
 *
 * The alternative -- re-emitting the parsed model -- would lose the 391
 * comments across the corpus, fold `5 ms' to `220.5', and write out dozens of
 * args buildArgMap() synthesised that nobody authored. So this finds the one
 * assignment being changed and rewrites only its right-hand side. Every other
 * byte of the file is copied through.
 *
 * What the grammar allows is narrower than it looks, and the writer is held to
 * it:
 *
 *   - Numbers are `[0-9]+(\.[0-9]*)?'. No exponent, so a value that needs one
 *     cannot be written at all.
 *   - Negatives exist only as a unary-minus rule over that, so `-0.5' is fine
 *     but `-5 ms' would parse as -(5 ms) -- which is what we want anyway.
 *   - `5 ms' means 5 * TH_SAMPLE / 1000 and `50%' means 50 * TH_MAX / 100.
 *     Both are exactly invertible, so a value written with a unit comes back
 *     with the same unit rather than as a folded raw number.
 *
 * Anything it cannot express, it refuses and says why, rather than writing
 * something that parses to a different number.
 */

#include <string>

class NodeEdit {
public:
    enum Result {
        OK = 0,
        NO_NODE,        /* no `node <name>' block in the file      */
        NOT_A_VALUE,    /* the arg is wired to something           */
        UNWRITABLE,     /* no way to spell this number             */
        IO_ERROR
    };

    /* Sets `<node> { <arg> = <value>; }'.
     *
     * If the block has no such assignment -- the arg exists only because
     * buildArgMap() invented it -- one is inserted before the closing brace.
     *
     * `why' gets a sentence suitable for showing to a person.
     */
    static Result setValue (const string &filename, const string &node,
                            const string &arg, double value, string &why);

    /* OK if the file has an explicit `<node> { <arg> = ...; }'. Distinguishes
       an arg the author wrote from one buildArgMap() invented, which is the
       difference between changing a line and adding one. */
    static Result find (const string &filename, const string &node,
                        const string &arg);

    /* Formats a number the way the grammar accepts it: plain decimal, no
       exponent, no trailing zeros. Returns false if it cannot be done, which
       is what the caller must not paper over. */
    static bool format (double value, string &out);

    /* Formats `value' using the unit `units' ("ms", "%", or empty), inverting
       the grammar's conversion so the file keeps the unit it had. */
    static bool formatWithUnits (double value, const string &units,
                                 string &out);

    /* The unit suffix on a right-hand side, or "" if there is none. Public
       because the round-trip check wants to assert on it. */
    static string unitsOf (const string &rhs);

    static const char *resultText (Result r);
};

#endif /* NODE_EDIT_H */
