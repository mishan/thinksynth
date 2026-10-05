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

#include "config.h"

#include <math.h>

#include "thArrays.h"

using std::string;
using std::vector;

/* More would be a file that is mostly copies; 64 is room for any channel
   count and any unison worth writing. */
#define TH_ARRAY_MAX 64

typedef vector<thLexToken> Tokens;

static bool
isPunct (const thLexToken &t, const char *p)
{
    return t.kind == thLexToken::PUNCT && t.text == p;
}

static bool
isWord (const thLexToken &t, const char *w)
{
    return t.kind == thLexToken::WORD && t.text == w;
}

static bool
fail (const thLexToken &at, const string &what, string &why, int &line)
{
    why = what;
    line = at.line;

    return false;
}

/* One past the `}' that closes the block whose `{' is at or after `from',
   and past a `;' straight after it. tokens.size() - 1 (the END) if the
   block never closes, which the grammar then reports. */
static size_t
blockEnd (const Tokens &tokens, size_t from)
{
    size_t k = from;
    int depth = 0;

    while (k < tokens.size() && tokens[k].kind != thLexToken::END)
    {
        if (isPunct(tokens[k], "{"))
            depth++;
        else if (isPunct(tokens[k], "}") && --depth == 0)
        {
            k++;

            if (k < tokens.size() && isPunct(tokens[k], ";"))
                k++;

            return k;
        }

        k++;
    }

    return k;
}

/* [b, e) onto `out', with every `name[]' read as index `i' (or refused,
   for i < 0) and every `name[k]' as k. After `->', or as the name an
   assignment sets, the index is a number on the end of the name; anywhere
   else it is the element of an array node. */
static bool
substitute (const Tokens &in, size_t b, size_t e, int i, Tokens &out,
            string &why, int &line)
{
    for (size_t k = b; k < e; k++)
    {
        const thLexToken &t = in[k];

        /* A `[]' of its own is the index as a number. */
        if (isPunct(t, "[") && k + 1 < e && isPunct(in[k + 1], "]"))
        {
            if (i < 0)
                return fail(t, "`[]' is outside an array node and the io "
                            "node, so it has no index", why, line);

            thLexToken n = t;

            n.kind = thLexToken::NUMBER;
            n.num = i;
            n.text = std::to_string(i);
            n.end = in[k + 1].end;
            out.push_back(n);
            k++;
            continue;
        }

        if (t.kind != thLexToken::WORD || k + 2 >= e ||
            !isPunct(in[k + 1], "["))
        {
            out.push_back(t);
            continue;
        }

        int idx;
        size_t close;

        if (isPunct(in[k + 2], "]"))
        {
            if (i < 0)
                return fail(t, "`" + t.text + "[]' is outside an array "
                            "node and the io node, so it has no index",
                            why, line);
            idx = i;
            close = k + 2;
        }
        else if (k + 3 < e && in[k + 2].kind == thLexToken::NUMBER &&
                 isPunct(in[k + 3], "]") &&
                 in[k + 2].num == floor(in[k + 2].num))
        {
            idx = (int)in[k + 2].num;
            close = k + 3;
        }
        else
            return fail(t, "an index is a whole number or nothing: `" +
                        t.text + "[1]' or `" + t.text + "[]'", why, line);

        const bool port = k > 0 && isPunct(in[k - 1], "->");
        const bool sets = close + 1 < e && isPunct(in[close + 1], "=") &&
                          k > 0 && (isPunct(in[k - 1], ";") ||
                                    isPunct(in[k - 1], "{"));
        thLexToken w = t;

        w.text = (port || sets)
            ? t.text + std::to_string(idx)
            : t.text + "[" + std::to_string(idx) + "]";
        w.end = in[close].end;
        out.push_back(w);
        k = close;
    }

    return true;
}

/* The io node's block, from its `{': each statement with a `[]' in it once
   per channel the block declares, the rest once. */
static bool
expandIO (const Tokens &in, size_t open, size_t end, Tokens &out,
          string &why, int &line)
{
    int channels = 1;

    for (size_t k = open; k + 2 < end; k++)
        if (isWord(in[k], "channels") && isPunct(in[k + 1], "=") &&
            in[k + 2].kind == thLexToken::NUMBER)
            channels = (int)in[k + 2].num;

    out.push_back(in[open]);

    size_t s = open + 1;

    while (s < end)
    {
        if (isPunct(in[s], "}"))
            return substitute(in, s, end, -1, out, why, line);

        size_t semi = s;

        while (semi < end && !isPunct(in[semi], ";") &&
               !isPunct(in[semi], "}"))
            semi++;

        if (semi < end && isPunct(in[semi], ";"))
            semi++;

        bool indexed = false;

        for (size_t k = s; k + 1 < semi; k++)
            if (isPunct(in[k], "[") && isPunct(in[k + 1], "]"))
                indexed = true;

        for (int c = 0; c < (indexed ? channels : 1); c++)
            if (!substitute(in, s, semi, indexed ? c : -1, out, why, line))
                return false;

        s = semi;
    }

    return true;
}

bool
thExpandArrays (Tokens &tokens, string &why, int &line)
{
    Tokens out;
    size_t k = 0;

    out.reserve(tokens.size());

    while (k < tokens.size())
    {
        const thLexToken &t = tokens[k];

        if (!isWord(t, "node") || k + 2 >= tokens.size() ||
            tokens[k + 1].kind != thLexToken::WORD)
        {
            /* One token, or a `name[...]' and its brackets. */
            size_t next = k + 1;

            if (t.kind == thLexToken::WORD && next < tokens.size() &&
                isPunct(tokens[next], "["))
            {
                while (next < tokens.size() && !isPunct(tokens[next], "]") &&
                       tokens[next].kind != thLexToken::END)
                    next++;

                if (next < tokens.size() && isPunct(tokens[next], "]"))
                    next++;

                /* And the `=' after it, so an assignment's name is seen
                   as one. */
                if (next < tokens.size() && isPunct(tokens[next], "="))
                    next++;
            }

            if (!substitute(tokens, k, next, -1, out, why, line))
                return false;

            k = next;
            continue;
        }

        /* `node NAME [ N ] ...': N copies. */
        if (isPunct(tokens[k + 2], "["))
        {
            const thLexToken &name = tokens[k + 1];

            if (k + 4 >= tokens.size() ||
                tokens[k + 3].kind != thLexToken::NUMBER ||
                !isPunct(tokens[k + 4], "]") ||
                tokens[k + 3].num != floor(tokens[k + 3].num) ||
                tokens[k + 3].num < 1 || tokens[k + 3].num > TH_ARRAY_MAX)
                return fail(name, "`node " + name.text + "[N]' wants a whole "
                            "number from 1 to " +
                            std::to_string(TH_ARRAY_MAX), why, line);

            const int n = (int)tokens[k + 3].num;
            const size_t end = blockEnd(tokens, k + 5);

            for (int i = 0; i < n; i++)
            {
                thLexToken w = name;

                w.text = name.text + "[" + std::to_string(i) + "]";
                w.end = tokens[k + 4].end;
                out.push_back(t);
                out.push_back(w);

                if (!substitute(tokens, k + 5, end, i, out, why, line))
                    return false;
            }

            k = end;
            continue;
        }

        /* `node NAME {': a node with no plugin, which is the io node. */
        if (isPunct(tokens[k + 2], "{"))
        {
            const size_t end = blockEnd(tokens, k + 2);

            out.push_back(t);
            out.push_back(tokens[k + 1]);

            if (!expandIO(tokens, k + 2, end, out, why, line))
                return false;

            k = end;
            continue;
        }

        out.push_back(t);
        k++;
    }

    tokens.swap(out);

    return true;
}
