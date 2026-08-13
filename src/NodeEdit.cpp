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

#include <sys/stat.h>   /* stat, chmod -- writeLines preserves the mode */
#include <unistd.h>     /* access -- and refuses a read-only target */

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

/* The part of a line that is code: comment removed, string bodies blanked.
 *
 * The old comment here claimed "a `#' inside a value is not a thing that can
 * happen". It is: the lexer's comment rule is `#.*$' and its string rule is
 * `"[^"\n]*"', flex takes the longest match at each position, so a `#' inside
 * a string belongs to the string and a `"' inside a comment belongs to the
 * comment. Cutting the line at the first `#' therefore chops
 * `name = "C# lead";' in half and loses whatever followed -- including any
 * brace, which is how findIoLine came to miscount depth and place a new node
 * after the `io' line, in a file that then would not parse.
 *
 * String bodies become spaces rather than disappearing, so that every offset
 * into the result is still an offset into the original line: callers hand
 * rhsFrom/rhsTo to the splicer, which slices the untouched line. Blanking also
 * stops the scanners here finding `{', `@blim' or `osc->out' inside a label
 * and acting on it.
 *
 * A string cannot span lines -- the pattern excludes `\n' -- so each line can
 * be read on its own with no state carried in from the one before. */
static string codeOf (const string &line)
{
    string out;

    out.reserve(line.size());

    bool inString = false;

    for (string::size_type i = 0; i < line.size(); i++)
    {
        const char c = line[i];

        if (inString)
        {
            out += (c == '"') ? '"' : ' ';

            if (c == '"')
                inString = false;

            continue;
        }

        if (c == '#')
            break;              /* a comment: the rest of the line is gone */

        if (c == '"')
            inString = true;

        out += c;
    }

    return out;
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

    if (s.size() > 2 && s.compare(s.size() - 2, 2, "ms") == 0)
    {
        /* Both `5 ms' and `80ms' occur -- 65 and 33 times respectively -- so
           the space cannot be required. What distinguishes a unit from the
           tail of an identifier is that a number comes before it: `th_params'
           has no number, `80ms' does. */
        const string head = trim(s.substr(0, s.size() - 2));

        if (!head.empty() && (isdigit((unsigned char)head[0]) ||
                              head[0] == '.' || head[0] == '-'))
            return "ms";
    }

    return "";
}

/* The exact trailing text of a right-hand side, so a rewrite keeps `80ms'
   spelled that way rather than turning it into `80 ms'. */
static string suffixTextOf (const string &rhs)
{
    const string s = trim(rhs);
    const string u = NodeEdit::unitsOf(s);

    if (u.empty())
        return "";

    if (u == "%")
        return "%";

    /* everything from where the number stops to the end */
    string::size_type p = s.size() - 2;

    while (p > 0 && (s[p - 1] == ' ' || s[p - 1] == '\t'))
        p--;

    return s.substr(p);
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
 * edit a person could mean. (DSP_FORMAT.md records this as one concrete thing
 * the global -ffast-math costs.) */
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

/* The number alone, correctly scaled for `units' but with no suffix. */
static bool formatLiteral (double value, const string &units, string &out);

bool NodeEdit::formatWithUnits (double value, const string &units, string &out)
{
    string n;

    if (!formatLiteral(value, units, n))
        return false;

    out = n + ((units == "ms") ? " ms" : units);

    return true;
}

static bool formatLiteral (double value, const string &units, string &out)
{
    if (!(value == value) || value > 1e30 || value < -1e30)
        return false;           /* NaN or infinity: no spelling exists */

    const double want = value;

    const double literal = removeUnits(value, units);

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

            out = cand;

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

/* Into a temporary beside the target, then renamed over it.
 *
 * This used to open the real file with ios::trunc and write into it, so a full
 * disk, a crash or a kill left a half-written .dsp -- and that is someone's
 * patch, possibly the only copy. Every writer in this file goes through here,
 * so all ten of them were destructive on failure. NodeLayout::write already
 * worked this way; the two now agree.
 *
 * rename() is atomic within a directory, so the file ends up either the old
 * one or the new one and never a prefix of the new one. The temporary sits
 * beside the target because rename cannot cross a filesystem.
 *
 * The mode of an existing file is carried across: rename replaces the inode,
 * so without this a .dsp that was group-writable would quietly come back with
 * whatever the umask happened to say. */
static bool writeLines (const string &filename, const vector<string> &lines,
                        bool endsWithNewline)
{
    /* Refuse a target that exists and cannot be written.
     *
     * Renaming over a file needs write permission on the *directory*, not on
     * the file, so temp-and-rename will happily replace a read-only .dsp that
     * happens to sit in a directory you own. The old ios::trunc write failed
     * on one, as it should. Making the write atomic must not also quietly
     * strip the meaning off a read-only bit, so this puts that back.
     *
     * The node editor never reaches this on a read-only patch anyway -- it
     * edits a scratch copy and offers Save As -- but NodeEdit is not only
     * called by the node editor, and "the file said no" is the answer here. */
    if (access(filename.c_str(), F_OK) == 0 &&
        access(filename.c_str(), W_OK) != 0)
        return false;

    const string tmp = filename + ".edit-tmp";

    {
        ofstream out(tmp.c_str(), ios::binary | ios::trunc);

        if (!out)
            return false;

        for (size_t i = 0; i < lines.size(); i++)
        {
            out << lines[i];

            if (i + 1 < lines.size() || endsWithNewline)
                out << "\n";
        }

        out.flush();

        if (!out.good())
        {
            out.close();
            remove(tmp.c_str());
            return false;
        }
    }

    /* Carry the original's permissions onto the replacement, so saving a .dsp
       does not quietly change who can read or write it.
     *
     * Unix only. Windows has no POSIX mode worth preserving -- st_mode there
     * carries little beyond a read-only bit -- and chmod is declared in
     * <io.h> rather than <sys/stat.h>, so this would not compile. Nothing is
     * lost by skipping it: what governs access on Windows is the ACL the file
     * inherits from its directory, which the rename below leaves alone. */
#ifndef _WIN32
    struct stat st;

    if (stat(filename.c_str(), &st) == 0)
        chmod(tmp.c_str(), st.st_mode & 07777);
#endif

    if (rename(tmp.c_str(), filename.c_str()) != 0)
    {
        remove(tmp.c_str());
        return false;
    }

    return true;
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

    if (node.empty() || arg.empty())
    {
        why = "no node or parameter named";
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

        if (!formatLiteral(value, units, text))
        {
            why = "cannot write that value in a .dsp";
            return UNWRITABLE;
        }

        lines[line] = lines[line].substr(0, from) + text +
                      suffixTextOf(oldRhs) + lines[line].substr(to);
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

/* Points an arg at whatever `text' says, adding the line if there is none. */
static NodeEdit::Result bindArg (const string &filename, const string &node,
                                 const string &arg, const string &text,
                                 string &why)
{
    /* An empty destination would be catastrophic rather than merely wrong:
       string::find("") returns 0, so findAssign would match at the start of
       the first line it looked at and rewrite whatever assignment happened to
       be there. Every caller passes a real name; this guards the one that
       someday does not. Here rather than in connect() so connectControl gets
       it too. */
    if (node.empty() || arg.empty())
    {
        why = "no destination for the connection";
        return NodeEdit::REFUSED;
    }

    vector<string> lines;
    bool endsWithNewline = true;

    if (!readLines(filename, lines, endsWithNewline))
    {
        why = "could not open " + filename;
        return NodeEdit::IO_ERROR;
    }

    size_t open = 0, close = 0;

    if (!findNodeBlock(lines, node, open, close))
    {
        why = "no `node " + node + "' block in the file";
        return NodeEdit::NO_NODE;
    }

    size_t line = 0;
    string::size_type from = 0, to = 0;

    if (findAssign(lines, open, close, arg, line, from, to))
    {
        /* Already says exactly this: leave the file alone. Same reasoning as
           setValue -- connecting something to where it already is connected
           must not produce a diff. */
        if (trim(lines[line].substr(from, to - from)) == text)
            return NodeEdit::OK;

        lines[line] = lines[line].substr(0, from) + text +
                      lines[line].substr(to);
    }
    else
        lines.insert(lines.begin() + close,
                     indentOf(lines, open, close) + arg + " = " + text + ";");

    if (!writeLines(filename, lines, endsWithNewline))
    {
        why = "could not write " + filename;
        return NodeEdit::IO_ERROR;
    }

    return NodeEdit::OK;
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
     * is visible; see NodeGraph::canConnect. This function writes faithfully
     * what it is told. */
    if (srcNode.empty() || srcPort.empty())
    {
        why = "no source for the connection";
        return REFUSED;
    }

    /* Every one of the 3476 node-to-node connections in the corpus is spelled
       exactly `name->port', with no spaces around the arrow, so writing it
       this way reproduces the existing text byte for byte. */
    return bindArg(filename, node, arg, srcNode + "->" + srcPort, why);
}

NodeEdit::Result NodeEdit::connectControl (const string &filename,
                                           const string &node,
                                           const string &arg,
                                           const string &control, string &why)
{
    why.clear();

    if (control.empty())
    {
        why = "no control to connect to";
        return REFUSED;
    }

    return bindArg(filename, node, arg, "@" + control, why);
}

NodeEdit::Result NodeEdit::disconnect (const string &filename,
                                       const string &node, const string &arg,
                                       double value, string &why)
{
    why.clear();

    if (node.empty() || arg.empty())
    {
        why = "no node or parameter named";
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

    size_t line = 0;
    string::size_type from = 0, to = 0;

    if (!findAssign(lines, open, close, arg, line, from, to))
        return OK;      /* nothing there to disconnect */

    const string oldRhs = trim(lines[line].substr(from, to - from));

    /* Two things count as connected: a node's output, and a control. Since
       controls became nodes on the canvas, `a = @a' is a wire like any other
       and cutting it has to work the same way. */
    if (oldRhs.find("->") == string::npos &&
        (oldRhs.empty() || oldRhs[0] != '@'))
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

/* Locates a top-level `@<name> = <rhs>;'.
 *
 * "Top level" means at brace depth zero: the control declarations sit outside
 * every node block, indented only by convention. Restricting to depth zero
 * means a node's `freq = @filt1' -- which mentions the same name -- cannot be
 * mistaken for the declaration.
 */
static bool findChanArg (const vector<string> &lines, const string &name,
                         size_t &line, string::size_type &rhsFrom,
                         string::size_type &rhsTo)
{
    const string want = "@" + name;

    int depth = 0;

    for (size_t i = 0; i < lines.size(); i++)
    {
        const string code = codeOf(lines[i]);

        if (depth == 0)
        {
            const string t = trim(code);

            if (t.compare(0, want.size(), want) == 0)
            {
                /* `@blim' and not `@blim2' or `@blim.min' */
                string::size_type p = want.size();

                if (p >= t.size() || !isWordChar(t[p]))
                {
                    while (p < t.size() && (t[p] == ' ' || t[p] == '\t'))
                        p++;

                    if (p < t.size() && t[p] == '=')
                    {
                        /* back to an offset in the original line */
                        const string::size_type base =
                            code.find_first_not_of(" \t");

                        const string::size_type eq = base + p;
                        const string::size_type semi = code.find(';', eq);

                        if (semi == string::npos)
                            return false;

                        string::size_type s =
                            code.find_first_not_of(" \t", eq + 1);

                        if (s == string::npos || s > semi)
                            return false;

                        string::size_type e = semi;

                        while (e > s &&
                               (code[e - 1] == ' ' || code[e - 1] == '\t'))
                            e--;

                        line = i;
                        rhsFrom = s;
                        rhsTo = e;

                        return true;
                    }
                }
            }
        }

        for (size_t k = 0; k < code.size(); k++)
        {
            if (code[k] == '{') depth++;
            else if (code[k] == '}' && depth > 0) depth--;
        }
    }

    return false;
}

NodeEdit::Result NodeEdit::setChanArg (const string &filename,
                                       const string &name, double value,
                                       string &why)
{
    why.clear();

    vector<string> lines;
    bool endsWithNewline = true;

    if (!readLines(filename, lines, endsWithNewline))
    {
        why = "could not open " + filename;
        return IO_ERROR;
    }

    size_t line = 0;
    string::size_type from = 0, to = 0;

    if (!findChanArg(lines, name, line, from, to))
    {
        why = "no `@" + name + " = ...' line in the file";
        return NO_NODE;
    }

    const string oldRhs = lines[line].substr(from, to - from);

    double oldValue;

    if (!parseRhs(oldRhs, oldValue))
    {
        why = "@" + name + " is currently `" + oldRhs +
              "', which this editor will not rewrite.";
        return UNWRITABLE;
    }

    if (sameValue(oldValue, value))
        return OK;

    string text;

    if (!formatLiteral(value, unitsOf(oldRhs), text))
    {
        why = "cannot write that value in a .dsp";
        return UNWRITABLE;
    }

    lines[line] = lines[line].substr(0, from) + text + suffixTextOf(oldRhs) +
                  lines[line].substr(to);

    if (!writeLines(filename, lines, endsWithNewline))
    {
        why = "could not write " + filename;
        return IO_ERROR;
    }

    return OK;
}

/* ---- adding and removing whole nodes ----------------------------------- */

bool NodeEdit::validName (const string &name)
{
    /* The lexer's WORD is [A-Za-z_][A-Za-z0-9_]*, and a name that does not
       match comes back as something else entirely -- or as a parse error on
       the next load, which is a bad way to find out. */
    if (name.empty() || isdigit((unsigned char)name[0]))
        return false;

    for (string::size_type i = 0; i < name.size(); i++)
        if (!isWordChar(name[i]))
            return false;

    /* Not a keyword either. `node', `io' and the info words would produce a
       file that parses as something the author did not write. */
    static const char *reserved[] = {
        "node", "io", "name", "author", "description", "desc",
        "th_max", "th_min", "th_range", "th_midimax", "th_sample", "ms", NULL
    };

    for (int i = 0; reserved[i]; i++)
        if (name == reserved[i])
            return false;

    return true;
}

/* The line index of `io <something>;', or lines.size() if there is none. */
static size_t findIoLine (const vector<string> &lines)
{
    int depth = 0;

    for (size_t i = 0; i < lines.size(); i++)
    {
        const string code = codeOf(lines[i]);

        if (depth == 0)
        {
            const string t = trim(code);

            if (t.compare(0, 3, "io ") == 0 || t.compare(0, 3, "io\t") == 0)
                return i;
        }

        for (size_t k = 0; k < code.size(); k++)
        {
            if (code[k] == '{') depth++;
            else if (code[k] == '}' && depth > 0) depth--;
        }
    }

    return lines.size();
}

NodeEdit::Result NodeEdit::addNode (const string &filename, const string &node,
                                    const string &plugin, string &why)
{
    why.clear();

    if (!validName(node))
    {
        why = "`" + node + "' is not a usable node name";
        return REFUSED;
    }

    if (plugin.find("::") == string::npos)
    {
        why = "`" + plugin + "' is not a <category>::<plugin> name";
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

    if (findNodeBlock(lines, node, open, close))
    {
        why = "the file already has a node called `" + node + "'";
        return REFUSED;
    }

    /* Before the io line: the grammar needs every node to exist by the time
       `io' names one, and it is where the last node sits in every shipped
       file anyway. */
    const size_t at = findIoLine(lines);

    vector<string> block;

    block.push_back("node " + node + " " + plugin + " {");
    block.push_back("};");
    block.push_back("");

    lines.insert(lines.begin() + at, block.begin(), block.end());

    if (!writeLines(filename, lines, endsWithNewline))
    {
        why = "could not write " + filename;
        return IO_ERROR;
    }

    return OK;
}

NodeEdit::Result NodeEdit::removeNode (const string &filename,
                                       const string &node, int &removed,
                                       string &why)
{
    why.clear();
    removed = 0;

    if (node.empty())
    {
        why = "no node named";
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

    /* Anything reading from it has to stop, or the file loads with
       "setPointers: Node x not found!!" and the arg silently reads zero.
       Rewritten to 0 rather than deleted, the same as disconnect(): the line
       keeps its place and its trailing comment. */
    const string ref = node + "->";

    for (size_t i = 0; i < lines.size(); i++)
    {
        if (i >= open && i <= close)
            continue;                   /* the block itself, going away */

        const string code = codeOf(lines[i]);
        const string::size_type eq = code.find('=');

        if (eq == string::npos)
            continue;

        const string::size_type at = code.find(ref, eq);

        if (at == string::npos)
            continue;

        /* `osc->out' and not `myosc->out' */
        if (at > 0 && isWordChar(code[at - 1]))
            continue;

        const string::size_type semi = code.find(';', eq);

        if (semi == string::npos || at > semi)
            continue;

        string::size_type from = code.find_first_not_of(" \t", eq + 1);
        string::size_type to = semi;

        while (to > from && (code[to - 1] == ' ' || code[to - 1] == '\t'))
            to--;

        lines[i] = lines[i].substr(0, from) + "0" + lines[i].substr(to);
        removed++;
    }

    /* Take a blank line after the block with it, so deleting nodes does not
       leave the file gappier each time. */
    size_t last = close;

    if (last + 1 < lines.size() &&
        trim(codeOf(lines[last + 1])).empty() &&
        lines[last + 1].find_first_not_of(" \t\r") == string::npos)
        last++;

    lines.erase(lines.begin() + open, lines.begin() + last + 1);

    if (!writeLines(filename, lines, endsWithNewline))
    {
        why = "could not write " + filename;
        return IO_ERROR;
    }

    return OK;
}

NodeEdit::Result NodeEdit::createFile (const string &filename,
                                       const string &name,
                                       const string &author, bool replace,
                                       string &why)
{
    why.clear();

    if (!replace)
    {
        ifstream probe(filename.c_str());

        if (probe)
        {
            why = filename + " already exists";
            return REFUSED;
        }
    }

    /* The name goes inside `name "..."', and the lexer's string has no escape
       for a quote -- `"[^"\n]*"' and nothing else. The GUI derives this from a
       filename the user typed, and a quote is legal in one, so a file called
       `my "best" patch.dsp' would produce a .dsp that does not parse. Refuse
       rather than write it. */
    if (!validLabel(name) || !validLabel(author))
    {
        why = "a name or author cannot contain a quote";
        return UNWRITABLE;
    }

    /* The smallest file that loads.
     *
     * finishParse rejects anything without an io node, so a genuinely empty
     * .dsp is not a thing that can exist -- "new" means this. out0 and out1
     * are wired to nothing yet, which is legal: buildArgMap invents them as
     * zero and the DSP renders silence until something is connected. */
    vector<string> lines;

    lines.push_back("# " + name);

    if (!author.empty())
        lines.push_back("# " + author);

    lines.push_back("");
    lines.push_back("name \"" + name + "\";");
    lines.push_back("author \"" + author + "\";");
    lines.push_back("description \"\";");
    lines.push_back("");
    lines.push_back("node ionode {");
    lines.push_back("    channels = 2;");
    lines.push_back("};");
    lines.push_back("");
    lines.push_back("io ionode;");

    if (!writeLines(filename, lines, true))
    {
        why = "could not write " + filename;
        return IO_ERROR;
    }

    return OK;
}

/* ---- controls ---------------------------------------------------------- */

bool NodeEdit::validLabel (const string &label)
{
    /* The lexer's string is `"[^"\n]*"'. No escapes, so a quote or a newline
       cannot be represented at all -- not "would look odd", cannot. */
    for (string::size_type i = 0; i < label.size(); i++)
        if (label[i] == '"' || label[i] == '\n' || label[i] == '\r')
            return false;

    return true;
}

/* The first `node ...' at brace depth zero, or lines.size(). */
static size_t findFirstNodeLine (const vector<string> &lines)
{
    for (size_t i = 0; i < lines.size(); i++)
    {
        const string t = trim(codeOf(lines[i]));

        if (t.compare(0, 5, "node ") == 0 || t.compare(0, 5, "node\t") == 0)
            return i;
    }

    return lines.size();
}

NodeEdit::Result NodeEdit::addControl (const string &filename,
                                       const string &name, double value,
                                       double min, double max,
                                       const string &label,
                                       const string &group, string &why)
{
    why.clear();

    if (!validName(name))
    {
        why = "`" + name + "' is not a usable control name";
        return REFUSED;
    }

    if (!validLabel(label))
    {
        why = "a label cannot contain a quote or a newline";
        return REFUSED;
    }

    if (!validLabel(group))
    {
        why = "a group name cannot contain a quote or a newline";
        return REFUSED;
    }

    if (max <= min)
    {
        why = "the maximum must be above the minimum";
        return REFUSED;
    }

    if (value < min) value = min;
    if (value > max) value = max;

    vector<string> lines;
    bool endsWithNewline = true;

    if (!readLines(filename, lines, endsWithNewline))
    {
        why = "could not open " + filename;
        return IO_ERROR;
    }

    /* Already declared? Two `@blim = ...' lines would have the second quietly
       win, which is not what anyone means by adding one. */
    {
        size_t line = 0;
        string::size_type from = 0, to = 0;

        if (findChanArg(lines, name, line, from, to))
        {
            why = "the file already has a control called `@" + name + "'";
            return REFUSED;
        }
    }

    string sv, smin, smax;

    if (!format(value, sv) || !format(min, smin) || !format(max, smax))
    {
        why = "cannot write those numbers in a .dsp";
        return UNWRITABLE;
    }

    vector<string> block;

    block.push_back("    @" + name + " = " + sv + ";");
    block.push_back("    @" + name + ".widget = 1;");
    block.push_back("    @" + name + ".min = " + smin + ";");
    block.push_back("    @" + name + ".max = " + smax + ";");

    if (!label.empty())
        block.push_back("    @" + name + ".label = \"" + label + "\";");

    if (!group.empty())
        block.push_back("    @" + name + ".group = \"" + group + "\";");

    block.push_back("");

    /* Before the first node, where every shipped file keeps them. The order
       within the block matters too: `@x.min' before `@x' has nothing to
       modify, and the parser says so and ignores it. */
    lines.insert(lines.begin() + findFirstNodeLine(lines),
                 block.begin(), block.end());

    if (!writeLines(filename, lines, endsWithNewline))
    {
        why = "could not write " + filename;
        return IO_ERROR;
    }

    return OK;
}

NodeEdit::Result NodeEdit::removeControl (const string &filename,
                                          const string &name, int &removed,
                                          string &why)
{
    why.clear();
    removed = 0;

    if (name.empty())
    {
        why = "no control named";
        return REFUSED;
    }

    vector<string> lines;
    bool endsWithNewline = true;

    if (!readLines(filename, lines, endsWithNewline))
    {
        why = "could not open " + filename;
        return IO_ERROR;
    }

    {
        size_t line = 0;
        string::size_type from = 0, to = 0;

        if (!findChanArg(lines, name, line, from, to))
        {
            why = "no `@" + name + "' in the file";
            return NO_NODE;
        }
    }

    /* Everything reading it stops reading it, for the same reason as
       removeNode: left alone the arg resolves to nothing and silently reads
       zero, which is a change to the sound nobody asked for. */
    const string ref = "@" + name;

    for (size_t i = 0; i < lines.size(); i++)
    {
        const string code = codeOf(lines[i]);
        const string::size_type eq = code.find('=');

        if (eq == string::npos)
            continue;

        const string::size_type at = code.find(ref, eq);

        if (at == string::npos)
            continue;

        /* `@blim' and not `@blim2'. */
        const string::size_type after = at + ref.size();

        if (after < code.size() && isWordChar(code[after]))
            continue;

        const string::size_type semi = code.find(';', eq);

        if (semi == string::npos || at > semi)
            continue;

        string::size_type from = code.find_first_not_of(" \t", eq + 1);
        string::size_type to = semi;

        while (to > from && (code[to - 1] == ' ' || code[to - 1] == '\t'))
            to--;

        lines[i] = lines[i].substr(0, from) + "0" + lines[i].substr(to);
        removed++;
    }

    /* The block itself: `@name = ...' and every `@name.something = ...'.
       Collected first, erased after, so the indices stay valid. */
    vector<size_t> drop;

    for (size_t i = 0; i < lines.size(); i++)
    {
        const string t = trim(codeOf(lines[i]));

        if (t.compare(0, ref.size(), ref) != 0)
            continue;

        const char next = (t.size() > ref.size()) ? t[ref.size()] : 0;

        /* `@blim = ', `@blim.min = ' -- but not `@blim2'. */
        if (next != 0 && next != '.' && next != ' ' && next != '\t' &&
            next != '=')
            continue;

        drop.push_back(i);
    }

    /* Plus a blank line left behind by the block. */
    if (!drop.empty())
    {
        const size_t after = drop.back() + 1;

        if (after < lines.size() &&
            lines[after].find_first_not_of(" \t\r") == string::npos)
            drop.push_back(after);
    }

    for (size_t i = drop.size(); i > 0; i--)
        lines.erase(lines.begin() + drop[i - 1]);

    if (!writeLines(filename, lines, endsWithNewline))
    {
        why = "could not write " + filename;
        return IO_ERROR;
    }

    return OK;
}
