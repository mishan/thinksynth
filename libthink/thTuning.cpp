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

#include "config.h"

#include <cfloat>
#include <cmath>

#include "thTuning.h"
#include "thUtil.h"

using std::string;
using std::vector;

namespace {

/* The text as lines, whichever ending each has, a byte-order mark gone. */
vector<string> lines (const string &text)
{
    vector<string> out;
    size_t at = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;

    while (at < text.size())
    {
        const size_t end = text.find_first_of("\r\n", at);
        const size_t stop = end == string::npos ? text.size() : end;

        out.push_back(text.substr(at, stop - at));
        at = stop;

        /* CR LF, CR or LF: one ending. */
        if (at < text.size() && text[at] == '\r')
            at++;
        if (at < text.size() && text[at] == '\n')
            at++;
    }

    return out;
}

bool space (char c) { return c == ' ' || c == '\t'; }

bool comment (const string &line)
{
    return !line.empty() && line[0] == '!';
}

/* The line's value: from its first non-blank to whitespace or `!'. What
   follows is a label, as the spec has it (`5/4 E'). */
string token (const string &line)
{
    size_t i = 0;

    while (i < line.size() && space(line[i]))
        i++;

    size_t j = i;

    while (j < line.size() && !space(line[j]) && line[j] != '!')
        j++;

    return line.substr(i, j - i);
}

bool digits (const string &s, size_t from, size_t to)
{
    if (from >= to)
        return false;

    for (size_t i = from; i < to; i++)
        if (s[i] < '0' || s[i] > '9')
            return false;

    return true;
}

/* An integer, sign allowed, ten digits at most and within int: parsed by
   hand because thUtil has no integer parser and parseDouble would take
   `inf', hex and exponents. */
bool integer (const string &s, long long &out)
{
    const size_t start = s.size() && (s[0] == '-' || s[0] == '+') ? 1 : 0;

    if (!digits(s, start, s.size()) || s.size() - start > 10)
        return false;

    long long v = 0;

    for (size_t i = start; i < s.size(); i++)
        v = v * 10 + (s[i] - '0');

    out = s[0] == '-' ? -v : v;

    return out >= INT_MIN && out <= INT_MAX;
}

/* A decimal: sign, digits and one point (`408.', `.5'), nothing else. */
bool decimal (const string &s, bool needPoint, double &out)
{
    const size_t start = s.size() && (s[0] == '-' || s[0] == '+') ? 1 : 0;
    const size_t point = s.find('.', start);

    if (point == string::npos)
    {
        if (needPoint || !digits(s, start, s.size()))
            return false;
    }
    else if (!(digits(s, start, point) || digits(s, point + 1, s.size())) ||
             (point > start && !digits(s, start, point)) ||
             (point + 1 < s.size() && !digits(s, point + 1, s.size())))
        return false;

    out = thUtil::parseDouble(s.c_str());

    return std::isfinite(out);
}

/* What follows a line's value, blanks skipped. */
string after (const string &line)
{
    size_t i = 0;

    while (i < line.size() && space(line[i]))
        i++;
    while (i < line.size() && !space(line[i]) && line[i] != '!')
        i++;
    while (i < line.size() && space(line[i]))
        i++;

    return line.substr(i);
}

/* A degree: cents if it holds a point, else a ratio of positive integers
   up to 2^31 - 1, the least the spec says a reader must take. `rest' is
   what follows it on the line, a label -- unless the number plainly goes
   on past a blank, a slash or point it has not had yet (`3 / 2', `3/ 2',
   `100 .0'), which read as a label would give another pitch. */
bool degree (const string &s, const string &rest, double &cents, string &err)
{
    const bool hasPoint = s.find('.') != string::npos;
    const bool hasSlash = s.find('/') != string::npos;

    if (!rest.empty() && (s.back() == '/' ||
                          ((rest[0] == '/' || rest[0] == '.') && !hasPoint && !hasSlash)))
    {
        err = "'" + s + " " + rest + "': a blank inside a number";
        return false;
    }

    if (s.find('.') != string::npos)
    {
        if (!decimal(s, true, cents))
        {
            err = "'" + s + "' is not a number of cents";
            return false;
        }

        return true;
    }

    const size_t slash = s.find('/');
    const string a = s.substr(0, slash);
    const string b = slash == string::npos ? "1" : s.substr(slash + 1);
    long long num, den;

    const bool shaped = integer(a, num) && integer(b, den);
    const bool signedRatio = shaped && (a[0] == '-' || a[0] == '+' ||
                                        b[0] == '-' || b[0] == '+');

    if (!shaped)
    {
        const bool looksRatio = digits(a, a[0] == '-' || a[0] == '+' ? 1 : 0,
                                       a.size()) &&
                                (slash == string::npos ||
                                 digits(b, b[0] == '-' || b[0] == '+' ? 1 : 0,
                                        b.size()));

        err = looksRatio ? "'" + s + "': a ratio's parts must be at most 2147483647"
                         : "'" + s + "' is not a number of cents or a ratio";
        return false;
    }

    if (signedRatio || num <= 0 || den <= 0)
    {
        err = "'" + s + "': a ratio's parts must be positive integers";
        return false;
    }

    cents = 1200.0 * log2((double)num / (double)den);
    return true;
}

/* What parse refuses, said once: build applies the same rules to an
   object filled in by hand. */
bool scaleRules (const thScale &s, string &err)
{
    if (s.cents.empty())
    {
        err = "the scale has no degrees";
        return false;
    }

    for (double c : s.cents)
        if (!std::isfinite(c))
        {
            err = "a degree is not finite";
            return false;
        }

    if (!(s.cents.back() > 0))
    {
        err = "the period is at or below unison; refused, as a keyboard "
              "on it means nothing";
        return false;
    }

    return true;
}

bool keymapRules (const thKeymap &m, string &err)
{
    const auto key = [] (int k) { return k >= 0 && k < thTuning::KEYS; };

    if (m.size < 0 || (long long)m.map.size() > m.size)
        err = "the map size is negative, or below its entries";
    else if (!key(m.first) || !key(m.last) || !key(m.middle) || !key(m.reference))
        err = "a key is not 0 to 127";
    else if (m.last < m.first)
        err = "the last key retuned is below the first";
    else if (!std::isfinite(m.frequency) || !(m.frequency > 0))
        err = "the reference frequency is not above 0";
    else if (!(fabs(m.referenceNote - (69 + 12 * log2(m.frequency / 440))) <= 1e-9))
        err = "the reference note is not the reference frequency's";
    else
        return true;

    return false;
}

long long floorDiv (long long a, long long b)
{
    const long long q = a / b;

    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

}   /* namespace */

/* ------------------------------------------------------------- thScale */

bool thScale::parse (const string &text, string &err, int &errLine)
{
    const vector<string> ls = lines(text);
    thScale out;
    size_t i = 0;

    while (i < ls.size() && comment(ls[i]))
        i++;

    if (i == ls.size())
    {
        err = "no description line";
        errLine = (int)i + 1;
        return false;
    }

    out.description = ls[i++];

    while (i < ls.size() && (comment(ls[i]) || token(ls[i]).empty()))
        i++;

    long long count = 0;

    if (i == ls.size() || !integer(token(ls[i]), count) || count < 0)
    {
        err = "no count of degrees";
        errLine = (int)i + 1;
        return false;
    }

    if (count == 0)
    {
        err = "no degrees: a count of 0, which Scala allows, gives no period";
        errLine = (int)i + 1;
        return false;
    }

    i++;

    while ((long long)out.cents.size() < count)
    {
        while (i < ls.size() && (comment(ls[i]) || token(ls[i]).empty()))
            i++;

        if (i == ls.size())
        {
            err = "missing degree " + std::to_string(out.cents.size() + 1) +
                  " of " + std::to_string(count);
            errLine = (int)i + 1;
            return false;
        }

        double value;

        if (!degree(token(ls[i]), after(ls[i]), value, err))
        {
            errLine = (int)i + 1;
            return false;
        }

        out.cents.push_back(value);
        out.written.push_back(token(ls[i]));
        i++;
    }

    if (!scaleRules(out, err))
    {
        errLine = (int)i;
        return false;
    }

    *this = out;
    return true;
}

bool thScale::operator== (const thScale &o) const
{
    return description == o.description && cents == o.cents;
}

/* ------------------------------------------------------------ thKeymap */

bool thKeymap::parse (const string &text, string &err, int &errLineOut)
{
    int errLine = 0;
    const auto refuse = [&] (void) { errLineOut = errLine; return false; };
    static const char *const NAMES[7] = {
        "map size", "first key", "last key", "middle key", "reference key",
        "reference frequency", "formal octave degree",
    };

    const vector<string> ls = lines(text);
    thKeymap out;
    long long header[7] = { 0 };
    size_t i = 0;

    for (int h = 0; h < 7; h++)
    {
        while (i < ls.size() && (comment(ls[i]) || token(ls[i]).empty()))
            i++;

        errLine = (int)i + 1;

        if (i == ls.size())
        {
            err = string("missing the ") + NAMES[h];
            return refuse();
        }

        const string t = token(ls[i++]);

        out.written.push_back(t);

        if (h == 5)
        {
            if (!decimal(t, false, out.frequency) || !(out.frequency > 0))
            {
                err = "'" + t + "' is not a reference frequency above 0";
                return refuse();
            }

            continue;
        }

        if (!integer(t, header[h]))
        {
            err = "'" + t + "' is not a whole number for the " + NAMES[h];
            return refuse();
        }

        if (h == 0 && header[h] < 0)
        {
            err = "the map size is negative";
            return refuse();
        }

        if (h >= 1 && h <= 4 && (header[h] < 0 || header[h] > 127))
        {
            err = string("the ") + NAMES[h] + " is not a key 0 to 127";
            return refuse();
        }

        if (h == 2 && header[2] < header[1])
        {
            err = "the last key retuned is below the first";
            return refuse();
        }
    }

    out.size = (int)header[0];
    out.first = (int)header[1];
    out.last = (int)header[2];
    out.middle = (int)header[3];
    out.reference = (int)header[4];
    out.periodDegree = (int)header[6];
    out.referenceNote = 69 + 12 * log2(out.frequency / 440);

    for (; i < ls.size(); i++)
    {
        if (comment(ls[i]) || token(ls[i]).empty())
            continue;

        errLine = (int)i + 1;

        if ((long long)out.map.size() == out.size)
        {
            err = "more map entries than the map size, " + std::to_string(out.size);
            return refuse();
        }

        const string t = token(ls[i]);
        long long d;

        out.written.push_back(t);

        if (t == "x")
            out.map.push_back(UNMAPPED);
        else if (!integer(t, d))
        {
            err = "'" + t + "' is not a degree or x in the map";
            return refuse();
        }
        else if (d == UNMAPPED)
        {
            err = "'" + t + "' is reserved for x in the map";
            return refuse();
        }
        else
            out.map.push_back((int)d);
    }

    if (!keymapRules(out, err))
        return refuse();

    *this = out;
    return true;
}

bool thKeymap::operator== (const thKeymap &o) const
{
    return size == o.size && first == o.first && last == o.last &&
           middle == o.middle && reference == o.reference &&
           frequency == o.frequency && referenceNote == o.referenceNote &&
           periodDegree == o.periodDegree && map == o.map;
}

namespace {

/* The values form, one per line again, as the files have them. */
string joined (const vector<string> &words, const char *between)
{
    string out;

    for (size_t i = 0; i < words.size(); i++)
        out += (i ? between : "") + words[i];

    return out;
}

vector<string> split (const string &values)
{
    vector<string> out;
    size_t at = 0;

    while (true)
    {
        while (at < values.size() && (space(values[at]) || values[at] == '\n' ||
                                      values[at] == '\r'))
            at++;

        if (at == values.size())
            return out;

        const size_t from = at;

        while (at < values.size() && !space(values[at]) && values[at] != '\n' &&
               values[at] != '\r')
            at++;

        out.push_back(values.substr(from, at - from));
    }
}

}   /* namespace */

string thScale::values (void) const
{
    return joined(written, " ");
}

bool thScale::parseValues (const string &values, const string &name, string &err)
{
    const vector<string> words = split(values);
    int line;

    /* The name set afterwards, not parsed: one starting with `!' would
       read as a comment. */
    if (!parse("-\n" + std::to_string(words.size()) + "\n" +
               joined(words, "\n") + "\n", err, line))
        return false;

    description = name;
    return true;
}

string thKeymap::values (void) const
{
    return joined(written, " ");
}

bool thKeymap::parseValues (const string &values, string &err)
{
    int line;

    return parse(joined(split(values), "\n") + "\n", err, line);
}

/* ------------------------------------------------------------ thTuning */

namespace {

double degreeCents (const thScale &s, long long d)
{
    const long long n = (long long)s.cents.size();
    const long long q = floorDiv(d, n);
    const long long r = d - q * n;

    return (double)q * s.cents.back() + (r ? s.cents[r - 1] : 0.0);
}

/* A key's pitch in cents, or false for a key the map leaves out. */
bool keyCents (const thScale &s, const thKeymap &m, int key, double &cents)
{
    const long long o = (long long)key - m.middle;

    if (m.size == 0)
    {
        cents = degreeCents(s, o);
        return true;
    }

    const long long q = floorDiv(o, m.size);
    const long long r = o - q * m.size;

    if (r >= (long long)m.map.size() || m.map[r] == thKeymap::UNMAPPED)
        return false;

    /* A formal octave of 0 is the scale's period (Scala, SHOW MAPPING). */
    const long long octave = m.periodDegree ? m.periodDegree : (long long)s.cents.size();

    cents = (double)q * degreeCents(s, octave) + degreeCents(s, m.map[r]);
    return true;
}

}   /* namespace */

thTuning::thTuning (void)
{
    for (int k = 0; k < KEYS; k++)
    {
        mapped_[k] = false;
        note_[k] = 0;
    }
}

bool thTuning::build (const thScale &scale, const thKeymap &keymap, string &err)
{
    /* Their fields are public: neither need have come from parse. */
    if (!scaleRules(scale, err) || !keymapRules(keymap, err))
        return false;

    double ref;

    if (!keyCents(scale, keymap, keymap.reference, ref))
    {
        err = "the reference key, " + std::to_string(keymap.reference) +
              ", is unmapped";
        return false;
    }

    thTuning out;

    for (int k = 0; k < KEYS; k++)
    {
        double cents;

        if (k < keymap.first || k > keymap.last || !keyCents(scale, keymap, k, cents))
            continue;

        const double note = keymap.referenceNote + (cents - ref) / 100;

        if (!std::isfinite(note) || fabs(note) > FLT_MAX)
        {
            err = "key " + std::to_string(k) + "'s pitch is past float range";
            return false;
        }

        out.mapped_[k] = true;
        out.note_[k] = note;
    }

    *this = out;
    return true;
}

bool thTuning::mapped (int key) const
{
    return key >= 0 && key < KEYS && mapped_[key];
}

double thTuning::note (int key) const
{
    return mapped(key) ? note_[key] : 0;
}

double thTuning::pitch (double key) const
{
    if (!(key >= 0 && key <= KEYS - 1))     /* NaN too: as it came */
        return key;

    const int k = (int)floor(key);
    const double f = key - k;
    const double below = mapped_[k] ? note_[k] : k;

    if (f == 0)
        return below;

    if (mapped_[k] && mapped_[k + 1])
        return below + f * (note_[k + 1] - below);

    return below + f;
}

bool thTuning::operator== (const thTuning &o) const
{
    for (int k = 0; k < KEYS; k++)
        if (mapped_[k] != o.mapped_[k] || note_[k] != o.note_[k])
            return false;

    return true;
}

bool thTuningText::read (bool keymap, const std::string &text,
                         const std::string &fallback, std::string &err,
                         int &errLine)
{
    errLine = 0;

    if (keymap)
    {
        thKeymap m;

        if (!m.parse(text, err, errLine))
            return false;

        this->keymap = m.values();
        return true;
    }

    thScale s;

    if (!s.parse(text, err, errLine))
        return false;

    name = s.description.find_first_not_of(" \t") == std::string::npos
        ? fallback : s.description;
    scale = s.values();
    return true;
}

bool thTuningText::build (std::shared_ptr<const thTuning> &out,
                          std::string &err) const
{
    if (empty())
    {
        if (!keymap.empty())
        {
            err = "a keyboard map needs a scale to map";
            return false;
        }

        out = nullptr;
        return true;
    }

    thScale s;
    thKeymap m;
    auto built = std::make_shared<thTuning>();

    if (!s.parseValues(scale, name, err) ||
        (!keymap.empty() && !m.parseValues(keymap, err)) ||
        !built->build(s, m, err))
        return false;

    built->text = *this;
    out = built;
    return true;
}
