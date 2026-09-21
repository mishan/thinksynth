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

/*
 * The .patch format, read and written. See PatchFile.h for what a document
 * is and what is deliberately not decided here.
 *
 * What this replaces is a 240-line loop over fgets() into a 256-byte buffer,
 * with strchr() writing NULs into it, three `goto owned' exits and a
 * `new string*[len+1]' that was freed on no path at all. The rules it
 * implemented are all still here; what is gone is the char * arithmetic they
 * were spelled in, and with it the fixed buffer -- a line longer than 255
 * bytes used to arrive as two lines, so a long `info comments' silently
 * became a complaint and half a property. The corpus's longest line is 91
 * bytes, which is why nobody met that.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "PatchFile.h"

/* `fx.', the prefix every effect parameter is addressed by. Spelled out
   rather than included from think.h: this file is the format and knows
   nothing about a synth, and what it needs the prefix for is grouping the
   `fx.' lines under the `effect' one when it writes -- a question about how a
   file reads, not about where a value goes. PatchApply is where it means
   something. The two spellings are held together by patchcheck, which
   composes a document with an effect parameter in it. */
static const char PATCH_FX_PREFIX[] = "fx.";

static bool hasPrefix (const string &s, const char *prefix)
{
    return s.compare(0, strlen(prefix), prefix) == 0;
}

/* Leading spaces and tabs off the front, trailing carriage return off the
 * back.
 *
 * The CR is the one thing here neither old parser agreed about. The desktop
 * stripped `\n' and kept whatever was in front of it, so a .patch saved on
 * Windows gave `dsp ts1.dsp\r' -- a filename with a control character in it,
 * which resolves to nothing and fails the load with a message naming a file
 * that looks exactly right. The page stripped it. A file edited on another
 * machine should load, this build ships for Windows, and there is no reading
 * under which a bare CR at the end of a line is content, so the page's answer
 * is the documented one.
 */
static string trimLine (const string &line)
{
    string::size_type b = 0, e = line.size();

    while (b < e && (line[b] == ' ' || line[b] == '\t'))
        b++;

    while (e > b && line[e - 1] == '\r')
        e--;

    return line.substr(b, e - b);
}

static string leadingTrim (const string &s)
{
    string::size_type b = 0;

    while (b < s.size() && (s[b] == ' ' || s[b] == '\t'))
        b++;

    return s.substr(b);
}

/* A whole field, or nothing.
 *
 * strtof on its own takes what it can and shrugs at the rest, so `4abc' is 4
 * and `abc' is 0 -- which is how a misspelled value used to become a silent
 * zero on a parameter, indistinguishable from somebody having meant zero.
 * Requiring the field to be consumed is the page's reading (Number() gives
 * NaN and the line is dropped) and the one worth documenting: a value that is
 * not a number is a complaint, and the arg keeps whatever the .dsp declared.
 */
static bool wholeNumber (const string &field, float &out)
{
    const string t = trimLine(leadingTrim(field));

    if (t.empty())
        return false;

    const char *s = t.c_str();
    char *end = NULL;

    out = strtof(s, &end);

    return end != NULL && *end == '\0';
}

static string lineNo (int n)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "line %d: ", n);

    return string(buf);
}

bool thPatchParse (const string &text, thPatchDoc &doc, string &why)
{
    doc = thPatchDoc();
    why.clear();

    bool seenDsp = false;
    string::size_type at = 0;
    int number = 0;

    while (at <= text.size())
    {
        const string::size_type nl = text.find('\n', at);
        const string raw = (nl == string::npos)
            ? text.substr(at) : text.substr(at, nl - at);

        at = (nl == string::npos) ? text.size() + 1 : nl + 1;
        number++;

        const string line = trimLine(raw);

        if (line.empty() || line[0] == '#')
            continue;

        /* Split at the first space, and only a space: a tab between a key and
           its value is not the format, and treating one as a separator would
           accept files the desktop has always ignored. */
        const string::size_type sp = line.find(' ');

        if (sp == string::npos)
        {
            doc.complaints.push_back(lineNo(number) + "`" + line +
                                     "' is a word with no value after it");
            continue;
        }

        const string key = line.substr(0, sp);
        const string rest = leadingTrim(line.substr(sp + 1));

        if (rest.empty())
        {
            doc.complaints.push_back(lineNo(number) + "`" + key +
                                     "' was given no value");
            continue;
        }

        if (key == "info")
        {
            /* `info NAME the rest of the line' -- two fields and then
               everything, so a comment may have spaces in it and needs no
               quoting. `info foo', which names a property and gives it
               nothing, is the line that used to fail the whole file. */
            const string::size_type sp2 = rest.find(' ');

            if (sp2 == string::npos)
            {
                doc.complaints.push_back(lineNo(number) + "info `" + rest +
                                         "' was given no value");
                continue;
            }

            const string name = rest.substr(0, sp2);
            string value = leadingTrim(rest.substr(sp2 + 1));

            if (value.empty())
            {
                doc.complaints.push_back(lineNo(number) + "info `" + name +
                                         "' was given no value");
                continue;
            }

            /* The format's one escape, so a description can be more than a
               line. NB: string::size_type and not unsigned int -- find()
               returns a 64-bit size_t, and truncating npos to 32 bits gives a
               value that compares unequal to npos, so the not-found case
               entered the loop and replace() threw. That worked in 2005
               because size_t was 32 bits. */
            string::size_type i;

            while ((i = value.find("\\n")) != string::npos)
                value.replace(i, 2, "\n");

            doc.info[name] = value;
            continue;
        }

        /* The two structural keys whose value is a filename rather than a
           number, and which therefore never reach the arg parsing below --
           which is exactly what the page got wrong about `effect': it tried
           the value as a number, got NaN, and dropped the line. */
        if (key == "dsp")
        {
            doc.dsp = rest;
            seenDsp = true;
            continue;
        }

        if (key == "effect")
        {
            doc.effect = rest;
            continue;
        }

        if (key == "side")
        {
            float n = 0;

            if (!wholeNumber(rest, n))
            {
                doc.complaints.push_back(lineNo(number) + "side `" + rest +
                                         "' is not a number");
                continue;
            }

            /* 1-based in the file, engine numbering here. Whether the channel
               it names exists is PatchApply's question -- see the field. */
            const int c = (int)n - 1;

            doc.side = (c >= 0) ? c : -1;
            continue;
        }

        /* Everything else is a chanarg and its comma-separated values. All of
           them: see thPatchDoc::args for why this is the one divergence
           resolved toward the page. */
        vector<float> values;
        bool good = true;
        string::size_type from = 0;

        for (;;)
        {
            const string::size_type comma = rest.find(',', from);
            const string field = (comma == string::npos)
                ? rest.substr(from) : rest.substr(from, comma - from);
            float v = 0;

            if (!wholeNumber(field, v))
            {
                doc.complaints.push_back(lineNo(number) + "`" + key +
                                         "' was given `" + field +
                                         "', which is not a number");
                good = false;
                break;
            }

            values.push_back(v);

            if (comma == string::npos)
                break;

            from = comma + 1;
        }

        if (good)
            doc.args[key] = values;
    }

    if (!seenDsp)
    {
        why = "names no dsp";
        return false;
    }

    return true;
}

/* One value, as the writer has always spelled one: %f, which is six decimal
   places. Not a taste -- the corpus was written by this format string, and
   `220.500015' reading back as itself is what makes the round trip exact. */
static string number (float v)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%f", v);

    return string(buf);
}

static string values (const vector<float> &v)
{
    string out;

    for (size_t i = 0; i < v.size(); i++)
    {
        if (i)
            out += ",";

        out += number(v[i]);
    }

    return out;
}

string thPatchCompose (const thPatchDoc &doc, const string &stamp)
{
    /* The banner, and the two blank lines under it that every file in the
       corpus has: the writer's format string ended in "\n\n" and ctime()'s
       line brought its own newline along, so trimming the stamp here is what
       keeps the shape the same whatever the caller hands over. */
    string when = stamp;

    while (!when.empty() && (when[when.size() - 1] == '\n' ||
                             when[when.size() - 1] == '\r'))
        when.erase(when.size() - 1);

    string out = "# Thinksynth Patch File\n#\n# Generated by Thinksynth ";

    out += PACKAGE_VERSION;
    out += "\n# ";
    out += when;
    out += "\n\n\n";

    out += "dsp " + doc.dsp + "\n";

    /* The side before the effect and the effect before its values, which is
       the order a reader needs them in: the effect is built when its line is
       read, its side is part of building it, and its parameters have nowhere
       to land until it is on the channel. The writer decides the order so
       that the reader does not have to tolerate both -- see PatchApply. */
    if (!doc.effect.empty())
    {
        if (doc.side >= 0)
        {
            char buf[32];

            snprintf(buf, sizeof(buf), "side %d\n", doc.side + 1);
            out += buf;
        }

        out += "effect " + doc.effect + "\n";
    }

    out += "\n";

    for (map<string, string>::const_iterator k = doc.info.begin();
         k != doc.info.end(); ++k)
    {
        if (k->second.empty())
            continue;

        string t = k->second;
        string::size_type i;

        while ((i = t.find("\n")) != string::npos)
            t.replace(i, 1, "\\n");

        out += "info " + k->first + " " + t + "\n";
    }

    /* The instrument's values, then the effect's. One map holds both and a
     * map is sorted, so writing it straight through would interleave them --
     * `fx.wet' sorts before `res' -- and the file would stop looking like the
     * one the application has written for twenty years. Two passes, and the
     * bytes are the same bytes.
     *
     * An `fx.' value with no `effect' line above it is dropped rather than
     * written: the reader has no effect on the channel to look the name up
     * in, so it refuses each one by name, and writing them would be writing a
     * patch that complains at itself on every load.
     */
    for (map<string, vector<float> >::const_iterator j = doc.args.begin();
         j != doc.args.end(); ++j)
        if (!hasPrefix(j->first, PATCH_FX_PREFIX))
            out += j->first + " " + values(j->second) + "\n";

    if (!doc.effect.empty())
        for (map<string, vector<float> >::const_iterator j = doc.args.begin();
             j != doc.args.end(); ++j)
            if (hasPrefix(j->first, PATCH_FX_PREFIX))
                out += j->first + " " + values(j->second) + "\n";

    return out;
}
