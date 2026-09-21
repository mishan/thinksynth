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

#ifndef JSON_OUT_H
#define JSON_OUT_H 1

/*
 * Writing JSON, for the dumps a parity gate compares.
 *
 * Four functions, shared rather than copied, because of what they are for:
 * every one of these dumps is produced twice -- once by a native harness and
 * once by the same code compiled to wasm -- and diffed byte for byte. Two
 * escapers that agreed about everything except a tab would fail that gate
 * with a difference that is about the escaper and not about the thing being
 * described, and finding that out takes an afternoon.
 *
 * The rules are the ones the panel dump settled on, and the reasons are
 * recorded at each function.
 */

#include <math.h>
#include <stdio.h>

#include <string>

using std::string;
using std::to_string;

/* A JSON string: the quotes, the six escapes JSON requires, and \u00xx for
 * anything else below a space.
 *
 * Bytes above 127 pass through untouched. A label comes out of a .dsp or a
 * plugin and is whatever the file's author wrote; JSON is UTF-8 by
 * definition and so is that, so escaping it would be inventing a difference
 * between the two dumps this is compared across. */
inline void jsonString (string &out, const string &s)
{
    out += '"';

    for (size_t i = 0; i < s.size(); i++)
    {
        const unsigned char c = (unsigned char)s[i];

        switch (c)
        {
            case '"':  out += "\\\""; continue;
            case '\\': out += "\\\\"; continue;
            case '\b': out += "\\b"; continue;
            case '\f': out += "\\f"; continue;
            case '\n': out += "\\n"; continue;
            case '\r': out += "\\r"; continue;
            case '\t': out += "\\t"; continue;
        }

        if (c < 0x20)
        {
            char esc[8];

            snprintf(esc, sizeof esc, "\\u%04x", c);

            out += esc;
        }
        else
            out += (char)c;
    }

    out += '"';
}

/* A double, exactly.
 *
 * %.17g is the shortest format that round-trips every double through text,
 * and round-tripping is the whole requirement: what is compared is a native
 * dump against a wasm one, and a value that differs in the last bit is a
 * difference worth failing on rather than one to hide behind %g's six
 * figures. A non-finite value has no JSON spelling at all and becomes 0 --
 * it cannot reach here from a panel or a patch, and a bare NaN in the output
 * would make the page's JSON.parse throw rather than show one odd row. */
inline void jsonNumber (string &out, double v)
{
    char buf[40];

    if (!isfinite(v))
        v = 0;

    snprintf(buf, sizeof buf, "%.17g", v);

    out += buf;
}

inline void jsonInt (string &out, long v)
{
    out += to_string(v);
}

/* The shape, and nothing else.
 *
 * Its own function because `long' is 64 bits natively and 32 under wasm, so
 * a 32-bit hash handed to the signed writer came out as 3237944777 on one
 * side and -1057022519 on the other -- a panel that was identical in every
 * row and differed in the number a page polls to decide whether to redraw
 * it. The parity gate found it the first time it ran, which is what it is
 * for. */
inline void jsonUnsigned (string &out, unsigned v)
{
    out += to_string((unsigned long long)v);
}

#endif /* JSON_OUT_H */
