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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>      /* isalnum, isdigit -- used by the RHS parsing */

#include <fstream>
#include <vector>

#include "think.h"
#include "NodeEdit.h"

const char *NodeEdit::resultText (Result r)
{
    switch (r)
    {
        case OK:          return "written";
        case NO_NODE:     return "no such node in the file";
        case NOT_A_VALUE: return "that parameter is wired to something";
        case UNWRITABLE:  return "that number cannot be written in a .dsp";
        case REFUSED:     return "that edit does not make sense";
        case IO_ERROR:    return "could not read or write the file";
    }

    return "unknown";
}

/* ---- small text helpers ------------------------------------------------ */

static bool isWordChar (char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/* Everything up to an unquoted `#'. The lexer drops comments entirely, so a
   `#' inside a value is not a thing that can happen. */
static string codeOf (const string &line)
{
    const string::size_type h = line.find('#');

    return (h == string::npos) ? line : line.substr(0, h);
}

static string trim (const string &s)
{
    string::size_type a = s.find_first_not_of(" \t\r");

    if (a == string::npos)
        return "";

    string::size_type b = s.find_last_not_of(" \t\r");

    return s.substr(a, b - a + 1);
}

/* ---- formatting -------------------------------------------------------- */

/* Plain decimal, no exponent, no trailing zeros -- the lexer's number pattern
   is `[0-9]+(\.[0-9]*)?' and nothing else. */
static bool plainDecimal (double value, int places, string &out)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%.*f", places, value);

    string s = buf;

    if (s.find('.') != string::npos)
    {
        string::size_type last = s.find_last_not_of('0');

        if (s[last] == '.')
            last--;             /* "5." is not in the grammar; "5" is */

        s = s.substr(0, last + 1);
    }

    if (s == "-0")
        s = "0";

    out = s;

    return true;
}

bool NodeEdit::format (double value, string &out)
{
    return formatWithUnits(value, "", out);
}

string NodeEdit::unitsOf (const string &rhs)
{
    const string s = trim(rhs);

    if (!s.empty() && s[s.size() - 1] == '%')
        return "%";

    if (s.size() >= 2 && s.compare(s.size() - 2, 2, "ms") == 0)
    {
        /* "...ms" only counts if it is a suffix and not the tail of an
           identifier -- `th_params' must not read as a millisecond value. */
        if (s.size() == 2 || !isWordChar(s[s.size() - 3]))
            return "ms";
    }

    return "";
}

/* "The same number", at the precision that matters.
 *
 * Not exact equality, and it cannot be. The grammar folds `0.5 ms' with
 * `$1.floatval * TH_SAMPLE / 1000' in float, and libthink is built with
 * -ffast-math, which lets the compiler turn that division into a multiply by
 * the reciprocal of 1000. So the engine holds 22.0500011 where honest
 * arithmetic gives 22.0499992 -- a couple of ULP apart. Re-deriving the
 * literal exactly is therefore impossible, and demanding it would rewrite
 * `0.5 ms' as `0.50000003 ms' every time anyone saved.
 *
 * Four ULP is comfortably more than that gap and comfortably less than any
 * edit a person could mean. (REVIVAL.md lists dropping the global -ffast-math
 * as outstanding work; this is one concrete thing it costs.) */
static bool sameValue (double a, double b)
{
    const float x = (float)a, y = (float)b;

    if (x == y)
        return true;

    if ((x < 0) != (y < 0))
        return false;

    float lo = x < y ? x : y;
    float hi = x < y ? y : x;

    for (int i = 0; i < 4; i++)
    {
        lo = nextafterf(lo, hi);

        if (lo == hi)
            return true;
    }

    return false;
}

/* The named constants the lexer knows. 229 uses of th_max and th_min across
   the corpus, so a writer that did not recognise them would turn `inmax =
   th_max' into `inmax = 1' on the first save of any file containing one. */
static bool namedConstant (const string &word, double &out)
{
    if (word == "th_max")      { out = TH_MAX;      return true; }
    if (word == "th_min")      { out = TH_MIN;      return true; }
    if (word == "th_range")    { out = TH_RANGE;    return true; }
    if (word == "th_midimax")  { out = MIDIVALMAX;  return true; }
    if (word == "th_sample")   { out = TH_SAMPLE;   return true; }

    return false;
}

/* The grammar's unit conversions, and their inverses. Both are exact. */
static double applyUnits (double literal, const string &units)
{
    if (units == "ms")
        return literal * (double)TH_SAMPLE / 1000.0;

    if (units == "%")
        return literal * (double)TH_MAX / 100.0;

    return literal;
}

static double removeUnits (double value, const string &units)
{
    if (units == "ms")
        return value * 1000.0 / (double)TH_SAMPLE;

    if (units == "%")
        return value * 100.0 / (double)TH_MAX;

    return value;
}

bool NodeEdit::formatWithUnits (double value, const string &units, string &out)
{
    if (!(value == value) || value > 1e30 || value < -1e30)
        return false;           /* NaN or infinity: no spelling exists */

    const double want = value;

    const double literal = removeUnits(value, units);

    const string suffix = (units == "ms") ? " ms" : units;

    /* Shortest decimal that comes back as the same float.
     *
     * Writing the value out at full precision looks safe and is not: the value
     * being written came from a float, so `0.5 ms' -- which folds to 22.05,
     * stored as 22.049999237 -- would be written back as `0.499999983 ms'.
     * Numerically indistinguishable, and it turns every saved file into a
     * diff full of noise. Searching from the short end gives `0.5 ms' back.
     *
     * The comparison is in float rather than double because a float is what
     * the engine ends up holding, so it is the only precision at which two
     * spellings are genuinely the same value. */
    for (int places = 0; places <= 9; places++)
    {
        string cand;

        plainDecimal(literal, places, cand);

        if (sameValue(applyUnits(atof(cand.c_str()), units), want))
        {
            /* A tiny value that rounded away to nothing is not the same
               number, whatever the float comparison says about zero. */
            if (value != 0.0 && atof(cand.c_str()) == 0.0)
                continue;

            out = cand + suffix;

            return true;
        }
    }

    return false;
}

/* Reads a right-hand side that is a single value: a number or a named
 * constant, optionally negated, optionally with a unit.
 *
 * Deliberately narrow. Eight right-hand sides in the corpus are arithmetic
 * (`a * 2', `b + 1'), and this returns false for those rather than guessing --
 * rewriting one would replace the author's expression with a constant, which
 * is exactly the kind of silent damage the whole splicing approach exists to
 * avoid. */
static bool parseRhs (const string &rhs, double &out)
{
    string s = trim(rhs);

    if (s.empty())
        return false;

    double sign = 1.0;

    if (s[0] == '-')
    {
        sign = -1.0;
        s = trim(s.substr(1));
    }

    /* strip the unit, which unitsOf() has already identified */
    const string units = NodeEdit::unitsOf(s);

    if (units == "%")
        s = trim(s.substr(0, s.size() - 1));
    else if (units == "ms")
        s = trim(s.substr(0, s.size() - 2));

    if (s.empty())
        return false;

    double literal;

    if (isdigit((unsigned char)s[0]) || s[0] == '.')
    {
        /* the whole of what is left must be the number */
        char *end = NULL;

        literal = strtod(s.c_str(), &end);

        if (end == NULL || *end != 0)
            return false;
    }
    else if (!namedConstant(s, literal))
        return false;

    out = sign * applyUnits(literal, units);

    return true;
}

/* ---- the splice -------------------------------------------------------- */

/* Finds the line range of `node <name> ... { ... }'.
 *
 * Brace counting rather than anything cleverer: the grammar nests exactly one
 * level, but counting costs nothing and does not care. */
static bool findNodeBlock (const vector<string> &lines, const string &node,
                           size_t &open, size_t &close)
{
    for (size_t i = 0; i < lines.size(); i++)
    {
        const string code = codeOf(lines[i]);
        const string t = trim(code);

        if (t.compare(0, 5, "node ") != 0 && t.compare(0, 5, "node\t") != 0)
            continue;

        /* the name is the next word */
        string::size_type a = t.find_first_not_of(" \t", 4);

        if (a == string::npos)
            continue;

        string::size_type b = a;

        while (b < t.size() && isWordChar(t[b]))
            b++;

        if (t.substr(a, b - a) != node)
            continue;

        int depth = 0;

        for (size_t j = i; j < lines.size(); j++)
        {
            const string c = codeOf(lines[j]);

            for (size_t k = 0; k < c.size(); k++)
            {
                if (c[k] == '{') depth++;
                else if (c[k] == '}')
                {
                    depth--;

                    if (depth == 0)
                    {
                        open = i;
                        close = j;

                        return true;
                    }
                }
            }
        }

        return false;       /* unterminated block; leave it alone */
    }

    return false;
}

/* Locates `<arg> = <rhs>;' within a block, returning the line and the extent
   of the right-hand side within it. */
static bool findAssign (const vector<string> &lines, size_t open, size_t close,
                        const string &arg, size_t &line,
                        string::size_type &rhsFrom, string::size_type &rhsTo)
{
    for (size_t i = open; i <= close && i < lines.size(); i++)
    {
        const string code = codeOf(lines[i]);

        string::size_type p = code.find(arg);

        while (p != string::npos)
        {
            const bool leftOk = (p == 0 || !isWordChar(code[p - 1]));
            const string::size_type after = p + arg.size();
            const bool rightOk = (after >= code.size() ||
                                  !isWordChar(code[after]));

            if (leftOk && rightOk)
            {
                /* only whitespace may precede it on the line */
                if (trim(code.substr(0, p)).empty())
                {
                    string::size_type eq = code.find('=', after);

                    if (eq != string::npos &&
                        trim(code.substr(after, eq - after)).empty())
                    {
                        string::size_type semi = code.find(';', eq);

                        if (semi == string::npos)
                            return false;   /* spans lines; not handled */

                        string::size_type s = code.find_first_not_of(" \t",
                                                                    eq + 1);

                        if (s == string::npos || s > semi)
                            return false;

                        string::size_type e = semi;

                        while (e > s && (code[e - 1] == ' ' || code[e - 1] == '\t'))
                            e--;

                        line = i;
                        rhsFrom = s;
                        rhsTo = e;

                        return true;
                    }
                }
            }

            p = code.find(arg, p + 1);
        }
    }

    return false;
}

/* ---- file in, file out ------------------------------------------------- */

static bool readLines (const string &filename, vector<string> &lines,
                       bool &endsWithNewline)
{
    ifstream in(filename.c_str(), ios::binary);

    if (!in)
        return false;

    string all((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());

    endsWithNewline = all.empty() || all[all.size() - 1] == '\n';

    string cur;

    lines.clear();

    for (size_t i = 0; i < all.size(); i++)
    {
        if (all[i] == '\n') { lines.push_back(cur); cur.clear(); }
        else cur += all[i];
    }

    if (!cur.empty())
        lines.push_back(cur);

    return true;
}

static bool writeLines (const string &filename, const vector<string> &lines,
                        bool endsWithNewline)
{
    ofstream out(filename.c_str(), ios::binary | ios::trunc);

    if (!out)
        return false;

    for (size_t i = 0; i < lines.size(); i++)
    {
        out << lines[i];

        if (i + 1 < lines.size() || endsWithNewline)
            out << "\n";
    }

    return out.good();
}

/* The indentation already used inside a block, so an inserted line looks like
   the ones around it rather than like an editor got at the file. */
static string indentOf (const vector<string> &lines, size_t open, size_t close)
{
    for (size_t i = open + 1; i < close; i++)
    {
        const string::size_type a = lines[i].find_first_not_of(" \t");

        if (a != string::npos && a > 0)
            return lines[i].substr(0, a);
    }

    return "    ";
}

/* ---- the edits --------------------------------------------------------- */

NodeEdit::Result NodeEdit::setValue (const string &filename,
                                     const string &node, const string &arg,
                                     double value, string &why)
{
    why.clear();

    vector<string> lines;
    bool endsWithNewline = true;

    if (!readLines(filename, lines, endsWithNewline))
    {
        why = "could not open " + filename;
        return IO_ERROR;
    }

    size_t open = 0, close = 0;

    if (!findNodeBlock(lines, node, open, close))
    {
        why = "no `node " + node + "' block in the file";
        return NO_NODE;
    }

    size_t line = 0;
    string::size_type from = 0, to = 0;

    if (findAssign(lines, open, close, arg, line, from, to))
    {
        const string oldRhs = lines[line].substr(from, to - from);

        /* Refuse to turn a connection into a number. Disconnecting is a
           deliberate act and belongs to edge editing, not to typing in a
           box. */
        if (oldRhs.find("->") != string::npos || oldRhs.find('@') == 0)
        {
            why = arg + " is driven by " + oldRhs +
                  ". Remove the connection first.";
            return NOT_A_VALUE;
        }

        double oldValue;

        if (!parseRhs(oldRhs, oldValue))
        {
            why = arg + " is currently `" + oldRhs +
                  "', which this editor will not rewrite.";
            return UNWRITABLE;
        }

        /* If the text already says this number, say nothing.
         *
         * Not an optimisation -- it is what makes "open a file, save it,
         * nothing changed" true. The value on screen came from a float, so
         * re-deriving its spelling can differ from what the author wrote even
         * when nothing was edited: `0.5 ms' folds to 22.05, is held as
         * 22.049999237, and comes back as `0.499999983 ms'. It also keeps
         * `th_max' spelled that way. Comparing at float precision -- what the
         * engine actually holds -- and leaving the line alone when they agree
         * means only genuinely changed values are ever touched. */
        if (sameValue(oldValue, value))
            return OK;

        const string units = unitsOf(oldRhs);

        string text;

        if (!formatWithUnits(value, units, text))
        {
            why = "cannot write that value in a .dsp";
            return UNWRITABLE;
        }

        lines[line] = lines[line].substr(0, from) + text +
                      lines[line].substr(to);
    }
    else
    {
        /* The file never mentioned this arg -- buildArgMap() invented it. Add
           a line rather than rewriting the block, matching the indentation of
           whatever is already inside it. */
        string text;

        if (!formatWithUnits(value, "", text))
        {
            why = "cannot write that value in a .dsp";
            return UNWRITABLE;
        }

        lines.insert(lines.begin() + close,
                     indentOf(lines, open, close) + arg + " = " + text + ";");
    }

    if (!writeLines(filename, lines, endsWithNewline))
    {
        why = "could not write " + filename;
        return IO_ERROR;
    }

    return OK;
}

NodeEdit::Result NodeEdit::connect (const string &filename, const string &node,
                                    const string &arg, const string &srcNode,
                                    const string &srcPort, string &why)
{
    why.clear();

    /* No self-reference check here.
     *
     * It would be wrong: three shipped DSPs contain `ionode.fade78 =
     * ionode->velocity', because the io node is one node in the file but two
     * things in reality -- the MIDI source and the audio sink. Whether a
     * connection makes sense is a question about the graph, where that split
     * is visible; see NodeGraph::canConnect. This function's job is to write
     * faithfully what it is told. */
    if (srcNode.empty() || srcPort.empty())
    {
        why = "no source for the connection";
        return REFUSED;
    }

    vector<string> lines;
    bool endsWithNewline = true;

    if (!readLines(filename, lines, endsWithNewline))
    {
        why = "could not open " + filename;
        return IO_ERROR;
    }

    size_t open = 0, close = 0;

    if (!findNodeBlock(lines, node, open, close))
    {
        why = "no `node " + node + "' block in the file";
        return NO_NODE;
    }

    /* Every one of the 3476 connections in the corpus is spelled exactly
       `name->port', with no spaces around the arrow, so writing it this way
       reproduces the existing text byte for byte. */
    const string text = srcNode + "->" + srcPort;

    size_t line = 0;
    string::size_type from = 0, to = 0;

    if (findAssign(lines, open, close, arg, line, from, to))
    {
        /* Already says exactly this: leave the file alone. Same reasoning as
           setValue -- connecting something to where it already is connected
           must not produce a diff. */
        if (trim(lines[line].substr(from, to - from)) == text)
            return OK;

        lines[line] = lines[line].substr(0, from) + text +
                      lines[line].substr(to);
    }
    else
        lines.insert(lines.begin() + close,
                     indentOf(lines, open, close) + arg + " = " + text + ";");

    if (!writeLines(filename, lines, endsWithNewline))
    {
        why = "could not write " + filename;
        return IO_ERROR;
    }

    return OK;
}

NodeEdit::Result NodeEdit::disconnect (const string &filename,
                                       const string &node, const string &arg,
                                       double value, string &why)
{
    why.clear();

    vector<string> lines;
    bool endsWithNewline = true;

    if (!readLines(filename, lines, endsWithNewline))
    {
        why = "could not open " + filename;
        return IO_ERROR;
    }

    size_t open = 0, close = 0;

    if (!findNodeBlock(lines, node, open, close))
    {
        why = "no `node " + node + "' block in the file";
        return NO_NODE;
    }

    size_t line = 0;
    string::size_type from = 0, to = 0;

    if (!findAssign(lines, open, close, arg, line, from, to))
        return OK;      /* nothing there to disconnect */

    const string oldRhs = trim(lines[line].substr(from, to - from));

    if (oldRhs.find("->") == string::npos)
    {
        why = arg + " is not connected to anything";
        return REFUSED;
    }

    /* The line is rewritten rather than deleted.
     *
     * To the engine `in = 0;' and no line at all are the same thing --
     * buildArgMap() invents the arg either way. Keeping the line preserves
     * whatever trailing comment was on it, and it means a disconnect followed
     * by a reconnect restores the file exactly, because only the right-hand
     * side ever moved. Deleting and re-inserting would put the line somewhere
     * else with different indentation. */
    string text;

    if (!formatWithUnits(value, "", text))
    {
        why = "cannot write that value in a .dsp";
        return UNWRITABLE;
    }

    lines[line] = lines[line].substr(0, from) + text + lines[line].substr(to);

    if (!writeLines(filename, lines, endsWithNewline))
    {
        why = "could not write " + filename;
        return IO_ERROR;
    }

    return OK;
}

NodeEdit::Result NodeEdit::find (const string &filename, const string &node,
                                 const string &arg)
{
    vector<string> lines;

    {
        ifstream in(filename.c_str());

        if (!in)
            return IO_ERROR;

        string line;

        while (getline(in, line))
            lines.push_back(line);
    }

    size_t open = 0, close = 0;

    if (!findNodeBlock(lines, node, open, close))
        return NO_NODE;

    size_t line = 0;
    string::size_type from = 0, to = 0;

    if (!findAssign(lines, open, close, arg, line, from, to))
        return NOT_A_VALUE;

    return OK;
}
