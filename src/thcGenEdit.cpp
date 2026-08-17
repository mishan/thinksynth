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
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

#include <sys/stat.h>   /* writeFile preserves the mode                 */

#include "think.h"      /* thUtil::replaceFile                          */

#include "thcGenFile.h"
#include "thcGenEdit.h"

const char *
thcGenEdit::resultText (Result r)
{
    switch (r)
    {
        case OK:         return "written";
        case NOT_FOUND:  return "not in the file";
        case REFUSED:    return "that edit does not make sense";
        case UNWRITABLE: return "that value cannot be written in a .gen";
        case IO_ERROR:   return "could not read or write the file";
    }

    return "unknown";
}

/* ---- file plumbing ---------------------------------------------------- */

/* Binary, here and in every read and write below: the splicer's whole
 * contract is byte spans into the file as it sits on disk, and Windows'
 * text mode rewrites newlines in both directions -- offsets that
 * counted \n would land splices one byte short per line above them,
 * and a written file would grow \r\n it never had. NodeEdit's
 * writeLines learned this first; the lexer reads the same way so the
 * two ends of the span agree. */
static bool
readFile (const std::string &filename, std::string &text)
{
    std::ifstream in(filename.c_str(), std::ios::binary);

    if (!in)
        return false;

    text.assign((std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>());

    return true;
}

/* Temporary beside the target, renamed into place, mode preserved --
 * the old file survives until the new one is complete, exactly as
 * NodeEdit's writeLines does it. */
static bool
writeFile (const std::string &filename, const std::string &text)
{
    std::string tmp = filename + ".gen-edit-tmp";

    {
        std::ofstream out(tmp.c_str(), std::ios::trunc | std::ios::binary);

        if (!out)
            return false;

        out << text;

        if (!out.good())
        {
            ::remove(tmp.c_str());
            return false;
        }
    }

    /* Unix only, for the reason NodeEdit's writeLines gives: Windows
       has no POSIX mode worth preserving, and MinGW declares chmod in
       <io.h>, so this would not even compile there. */
#ifndef _WIN32
    struct stat st;

    if (::stat(filename.c_str(), &st) == 0)
        ::chmod(tmp.c_str(), st.st_mode & 07777);
#endif

    /* thUtil::replaceFile rather than rename(): Windows' rename
       refuses a target that exists, and every save of a piece after
       the first is exactly that. */
    if (!thUtil::replaceFile(tmp, filename))
    {
        ::remove(tmp.c_str());
        return false;
    }

    return true;
}

/* ---- span edits ------------------------------------------------------- */

struct Edit
{
    size_t a, b;         /* replace [a, b)                              */
    std::string repl;
};

/* Applied back to front, so earlier offsets stay true while later text
 * moves. Edits are built against one index over one read, so they can
 * never overlap unless an operation is buggy -- and a buggy operation
 * must not half-happen: overlaps are detected before the first byte
 * moves, and false means the text was not touched at all. A partially
 * applied edit set is a file nobody wrote.
 *
 * stable_sort ascending, then applied in reverse. The stability and
 * the direction together make same-offset inserts land in push order:
 * the later-pushed insert is applied first and the earlier one then
 * lands above it, so what setKnobMeta pushes as min, max, label reads
 * back as min, max, label -- on every platform, every run. Plain sort
 * left equal offsets to the implementation's mood. */
static bool
applyEdits (std::string &text, std::vector<Edit> &edits)
{
    std::stable_sort(edits.begin(), edits.end(),
                     [](const Edit &x, const Edit &y)
                     { return x.a < y.a; });

    for (size_t i = 0; i + 1 < edits.size(); i++)
        if (edits[i].b > edits[i + 1].a)
            return false;

    for (size_t i = edits.size(); i-- > 0; )
        text.replace(edits[i].a, edits[i].b - edits[i].a, edits[i].repl);

    return true;
}

static size_t
lineStartOf (const std::string &text, size_t pos)
{
    size_t nl = text.rfind('\n', pos ? pos - 1 : 0);

    return nl == std::string::npos ? 0 : nl + 1;
}

/* Remove a statement and, when it sat on a line of its own, the line --
 * indentation and newline included. When something else shares the line
 * (another statement, or a trailing comment worth keeping), only the
 * statement's own bytes and the blanks before it go. */
static Edit
eraseStmt (const std::string &text, size_t a, size_t b)
{
    size_t ls = lineStartOf(text, a);
    bool lineBefore = text.find_first_not_of(" \t", ls) >= a;

    size_t e = b;

    while (e < text.size() && (text[e] == ' ' || text[e] == '\t'))
        e++;

    bool lineAfter = e >= text.size() || text[e] == '\n';

    Edit ed;

    if (lineBefore && lineAfter)
    {
        ed.a = ls;
        ed.b = e < text.size() ? e + 1 : e;   /* the newline too         */
    }
    else
    {
        ed.a = lineBefore ? ls : a;
        ed.b = e;
    }

    return ed;
}

/* ---- the structural index --------------------------------------------- */
/* A lenient scan: the files this operates on have already been through
 * the loader, so shape surprises mean "leave it alone", not "guess". */

typedef thcGenToken Tok;

struct PIdx
{
    std::string name;
    size_t stmtA, stmtB;         /* name .. ';'                          */
    size_t valA, valB;
    std::string valueText;
};

struct StageIdx
{
    std::string name, cat, plugin;
    size_t stmtA, stmtB;
    size_t bodyClose;            /* offset of the '}'                    */
    std::vector<PIdx> params;
};

struct SinkIdx
{
    size_t stmtA, stmtB;
    size_t bodyClose;
    int    channel;
    size_t chValA, chValB;
    bool   hasArg;
    std::string chanarg;
    size_t argStmtA, argStmtB, argValA, argValB;
};

struct MetaIdx
{
    bool present;
    size_t stmtA, stmtB;
    size_t valA, valB;
    double num;
    std::string str;

    MetaIdx (void) : present(false), num(0) {}
};

struct KnobIdx
{
    std::string name;
    size_t stmtA, stmtB;         /* the `@n = v;' declaration            */
    size_t valA, valB;
    double value;
    size_t blockEnd;             /* end of the last line of the block    */
    std::map<std::string, MetaIdx> meta;
};

struct ScaleIdx
{
    std::string name;
    size_t stmtA, stmtB;
    size_t valA, valB;           /* the quoted note list, quotes incl.   */
    std::string notes;
};

struct PresetCompIdx
{
    std::string name;
    size_t stmtA, stmtB;         /* name .. its ';'                      */
    size_t valA, valB;
    double value;
};

struct PresetIdx
{
    std::string name;
    size_t stmtA, stmtB;         /* `preset' .. the trailing ';'         */
    size_t bodyClose;            /* offset of the '}'                    */
    std::vector<PresetCompIdx> comps;
};

struct ChainIdx
{
    std::string name;
    size_t nameA, nameB;
    size_t stmtA, stmtB;
    size_t bodyClose;            /* the chain's '}'                      */
    bool   inputMidi;
    size_t inputA, inputB;
    std::vector<StageIdx> stages;
    std::vector<SinkIdx>  sinks;
};

struct Index
{
    std::map<std::string, MetaIdx> infos;    /* name/author/description  */
    MetaIdx seed, tempo;
    std::vector<KnobIdx>   knobs;
    std::vector<ScaleIdx>  scales;
    std::vector<PresetIdx> presets;
    std::vector<ChainIdx>  chains;

    size_t topInsert;        /* line start of the first token            */
    size_t headerEnd;        /* after the info/tempo/seed statements     */
    size_t firstScaleOff;    /* npos when there is none                  */
    size_t firstPresetOff;
    size_t firstChainOff;
};

/* Advance past one whole statement (to just after its `;' at depth 0
 * relative to where we are), used when the scan meets a shape it does
 * not index. */
static size_t
skipStmt (const std::vector<Tok> &t, size_t i)
{
    int depth = 0;

    while (t[i].kind != Tok::END)
    {
        if (t[i].kind == Tok::PUNCT)
        {
            if (t[i].text[0] == '{')
                depth++;
            else if (t[i].text[0] == '}' && depth > 0)
                depth--;
            else if (t[i].text[0] == ';' && depth == 0)
                return i + 1;
        }

        i++;
    }

    return i;
}

static bool
isPunct (const Tok &t, char c)
{
    return t.kind == Tok::PUNCT && t.text[0] == c;
}

/* `<word> = <value tokens> ;` starting at i. Fills p, returns the index
 * after the ';', or 0 on a shape that is not a param. */
static size_t
scanParam (const std::vector<Tok> &t, size_t i, PIdx &p)
{
    if (t[i].kind != Tok::WORD || !isPunct(t[i + 1], '='))
        return 0;

    p.name = t[i].text;
    p.stmtA = t[i].off;

    size_t v = i + 2;

    if (t[v].kind == Tok::END || isPunct(t[v], ';'))
        return 0;

    p.valA = t[v].off;

    size_t last = v;

    while (t[last + 1].kind != Tok::END && !isPunct(t[last + 1], ';') &&
           !isPunct(t[last + 1], '}'))
        last++;

    if (!isPunct(t[last + 1], ';'))
        return 0;

    p.valB = t[last].end;
    p.stmtB = t[last + 1].end;

    return last + 2;
}

static bool
buildIndex (const std::string &text, Index &ix, std::string &why)
{
    std::vector<Tok> t;
    std::string err;
    int errLine = 0;

    if (!thcGenLoader::tokenize(text, t, err, errLine))
    {
        std::ostringstream s;

        s << "line " << errLine << ": " << err;
        why = s.str();
        return false;
    }

    ix.topInsert = lineStartOf(text, t[0].off);
    ix.headerEnd = ix.topInsert;
    ix.firstScaleOff = std::string::npos;
    ix.firstPresetOff = std::string::npos;
    ix.firstChainOff = std::string::npos;

    size_t i = 0;

    while (t[i].kind != Tok::END)
    {
        /* @knob declarations and metadata */
        if (t[i].kind == Tok::KNOB)
        {
            const std::string &name = t[i].text;

            if (isPunct(t[i + 1], '=') && t[i + 2].kind == Tok::NUMBER &&
                isPunct(t[i + 3], ';'))
            {
                KnobIdx k;

                k.name = name;
                k.stmtA = t[i].off;
                k.valA = t[i + 2].off;
                k.valB = t[i + 2].end;
                k.value = t[i + 2].num;
                k.stmtB = t[i + 3].end;
                k.blockEnd = k.stmtB;
                ix.knobs.push_back(k);
                i += 4;
                continue;
            }

            if (isPunct(t[i + 1], '.') && t[i + 2].kind == Tok::WORD &&
                isPunct(t[i + 3], '=') &&
                (t[i + 4].kind == Tok::NUMBER ||
                 t[i + 4].kind == Tok::STRING) &&
                isPunct(t[i + 5], ';'))
            {
                for (size_t k = 0; k < ix.knobs.size(); k++)
                    if (ix.knobs[k].name == name)
                    {
                        MetaIdx m;

                        m.present = true;
                        m.stmtA = t[i].off;
                        m.stmtB = t[i + 5].end;
                        m.valA = t[i + 4].off;
                        m.valB = t[i + 4].end;
                        m.num = t[i + 4].num;
                        m.str = t[i + 4].text;
                        ix.knobs[k].meta[t[i + 2].text] = m;
                        ix.knobs[k].blockEnd = m.stmtB;
                        break;
                    }

                i += 6;
                continue;
            }

            i = skipStmt(t, i);
            continue;
        }

        if (t[i].kind != Tok::WORD)
        {
            i = skipStmt(t, i);
            continue;
        }

        const std::string &kw = t[i].text;

        if ((kw == "name" || kw == "author" || kw == "description") &&
            t[i + 1].kind == Tok::STRING && isPunct(t[i + 2], ';'))
        {
            MetaIdx m;

            m.present = true;
            m.stmtA = t[i].off;
            m.stmtB = t[i + 2].end;
            m.valA = t[i + 1].off;
            m.valB = t[i + 1].end;
            m.str = t[i + 1].text;
            ix.infos[kw] = m;
            ix.headerEnd = m.stmtB;
            i += 3;
            continue;
        }

        if ((kw == "seed" || kw == "tempo") &&
            t[i + 1].kind == Tok::NUMBER && isPunct(t[i + 2], ';'))
        {
            MetaIdx m;

            m.present = true;
            m.stmtA = t[i].off;
            m.stmtB = t[i + 2].end;
            m.valA = t[i + 1].off;
            m.valB = t[i + 1].end;
            m.num = t[i + 1].num;

            if (kw == "seed")
                ix.seed = m;
            else
                ix.tempo = m;

            ix.headerEnd = m.stmtB;
            i += 3;
            continue;
        }

        if (kw == "scale" && t[i + 1].kind == Tok::WORD &&
            t[i + 2].kind == Tok::STRING && isPunct(t[i + 3], ';'))
        {
            ScaleIdx s;

            s.name = t[i + 1].text;
            s.stmtA = t[i].off;
            s.stmtB = t[i + 3].end;
            s.valA = t[i + 2].off;
            s.valB = t[i + 2].end;
            s.notes = t[i + 2].text;

            if (ix.firstScaleOff == std::string::npos)
                ix.firstScaleOff = s.stmtA;

            ix.scales.push_back(s);
            i += 4;
            continue;
        }

        /* preset <name> { <comp> = <number>; ... }; */
        if (kw == "preset" && t[i + 1].kind == Tok::WORD &&
            isPunct(t[i + 2], '{'))
        {
            PresetIdx pr;

            pr.name = t[i + 1].text;
            pr.stmtA = t[i].off;
            pr.bodyClose = 0;

            size_t j = i + 3;
            bool shaped = true;

            while (t[j].kind != Tok::END && !isPunct(t[j], '}'))
            {
                if (t[j].kind == Tok::WORD && isPunct(t[j + 1], '=') &&
                    t[j + 2].kind == Tok::NUMBER && isPunct(t[j + 3], ';'))
                {
                    PresetCompIdx c;

                    c.name = t[j].text;
                    c.stmtA = t[j].off;
                    c.stmtB = t[j + 3].end;
                    c.valA = t[j + 2].off;
                    c.valB = t[j + 2].end;
                    c.value = t[j + 2].num;

                    pr.comps.push_back(c);
                    j += 4;
                    continue;
                }

                /* A shape the loader would refuse anyway. Leave the whole
                   statement unindexed rather than index half of it: an
                   edit aimed at a preset this scan did not understand
                   would splice against offsets it guessed. */
                shaped = false;
                break;
            }

            if (shaped && isPunct(t[j], '}') && isPunct(t[j + 1], ';'))
            {
                pr.bodyClose = t[j].off;
                pr.stmtB = t[j + 1].end;

                if (ix.firstPresetOff == std::string::npos)
                    ix.firstPresetOff = pr.stmtA;

                ix.presets.push_back(pr);
                i = j + 2;
                continue;
            }

            i = skipStmt(t, i);
            continue;
        }

        if (kw == "chain" && t[i + 1].kind == Tok::WORD &&
            isPunct(t[i + 2], '{'))
        {
            ChainIdx c;

            c.name = t[i + 1].text;
            c.nameA = t[i + 1].off;
            c.nameB = t[i + 1].end;
            c.stmtA = t[i].off;
            c.inputMidi = false;
            c.inputA = c.inputB = 0;

            if (ix.firstChainOff == std::string::npos)
                ix.firstChainOff = c.stmtA;

            size_t j = i + 3;

            while (t[j].kind != Tok::END && !isPunct(t[j], '}'))
            {
                if (t[j].kind == Tok::WORD && t[j].text == "input" &&
                    t[j + 1].kind == Tok::WORD && isPunct(t[j + 2], ';'))
                {
                    c.inputMidi = true;
                    c.inputA = t[j].off;
                    c.inputB = t[j + 2].end;
                    j += 3;
                    continue;
                }

                if (t[j].kind == Tok::WORD && t[j].text == "stage" &&
                    t[j + 1].kind == Tok::WORD &&
                    t[j + 2].kind == Tok::WORD &&
                    t[j + 3].kind == Tok::MODSEP &&
                    t[j + 4].kind == Tok::WORD && isPunct(t[j + 5], '{'))
                {
                    StageIdx s;

                    s.name = t[j + 1].text;
                    s.cat = t[j + 2].text;
                    s.plugin = t[j + 4].text;
                    s.stmtA = t[j].off;

                    size_t k = j + 6;

                    while (t[k].kind != Tok::END && !isPunct(t[k], '}'))
                    {
                        PIdx p;
                        size_t next = scanParam(t, k, p);

                        if (next == 0)
                        {
                            k = skipStmt(t, k);
                            continue;
                        }

                        p.valueText = text.substr(p.valA, p.valB - p.valA);
                        s.params.push_back(p);
                        k = next;
                    }

                    if (t[k].kind == Tok::END)
                        break;

                    s.bodyClose = t[k].off;

                    if (isPunct(t[k + 1], ';'))
                        k++;

                    s.stmtB = t[k].end;
                    c.stages.push_back(s);
                    j = k + 1;
                    continue;
                }

                if (t[j].kind == Tok::WORD && t[j].text == "sink" &&
                    isPunct(t[j + 1], '{'))
                {
                    SinkIdx s;

                    s.stmtA = t[j].off;
                    s.channel = 0;
                    s.chValA = s.chValB = 0;
                    s.hasArg = false;
                    s.argStmtA = s.argStmtB = s.argValA = s.argValB = 0;

                    size_t k = j + 2;

                    while (t[k].kind != Tok::END && !isPunct(t[k], '}'))
                    {
                        if (t[k].kind == Tok::WORD &&
                            t[k].text == "channel" &&
                            isPunct(t[k + 1], '=') &&
                            t[k + 2].kind == Tok::NUMBER &&
                            isPunct(t[k + 3], ';'))
                        {
                            s.channel = (int)t[k + 2].num;
                            s.chValA = t[k + 2].off;
                            s.chValB = t[k + 2].end;
                            k += 4;
                            continue;
                        }

                        if (t[k].kind == Tok::WORD &&
                            t[k].text == "chanarg" &&
                            isPunct(t[k + 1], '=') &&
                            t[k + 2].kind == Tok::STRING &&
                            isPunct(t[k + 3], ';'))
                        {
                            s.hasArg = true;
                            s.chanarg = t[k + 2].text;
                            s.argStmtA = t[k].off;
                            s.argStmtB = t[k + 3].end;
                            s.argValA = t[k + 2].off;
                            s.argValB = t[k + 2].end;
                            k += 4;
                            continue;
                        }

                        k = skipStmt(t, k);
                    }

                    if (t[k].kind == Tok::END)
                        break;

                    s.bodyClose = t[k].off;

                    if (isPunct(t[k + 1], ';'))
                        k++;

                    s.stmtB = t[k].end;
                    c.sinks.push_back(s);
                    j = k + 1;
                    continue;
                }

                j = skipStmt(t, j);
            }

            if (t[j].kind == Tok::END)
            {
                why = "unterminated chain " + c.name;
                return false;
            }

            c.bodyClose = t[j].off;

            if (isPunct(t[j + 1], ';'))
                j++;

            c.stmtB = t[j].end;
            ix.chains.push_back(c);
            i = j + 1;
            continue;
        }

        i = skipStmt(t, i);
    }

    return true;
}

/* ---- shared checks ---------------------------------------------------- */

/* The lexer's WORD shape, nothing more. Param names live inside a stage
 * body where no statement keyword can be confused for them -- quantize's
 * pitch-set param is literally called `scale', and refusing it would be
 * refusing the shipped piece. */
static bool
validWord (const std::string &name)
{
    if (name.empty())
        return false;

    char c = name[0];

    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        return false;

    for (size_t i = 1; i < name.size(); i++)
    {
        c = name[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return false;
    }

    return true;
}

bool
thcGenEdit::validName (const std::string &name)
{
    if (!validWord(name))
        return false;

    /* For the top-level names -- chains, scales, knobs, stages: the
       words that open statements, and the unit words a bare value could
       be mistaken for. A chain named `sink' would parse today, but the
       file it produces reads like a trap. */
    static const char *reserved[] = {
        "name", "author", "description", "tempo", "seed", "scale",
        "chain", "input", "stage", "sink", "midi",
        "s", "ms", "beats", "b", NULL
    };

    for (int i = 0; reserved[i] != NULL; i++)
        if (name == reserved[i])
            return false;

    return true;
}

bool
thcGenEdit::format (double value, std::string &out)
{
    if (value != value || value > 1e15 || value < -1e15)
        return false;

    char buf[64];

    snprintf(buf, sizeof(buf), "%.10g", value);

    if (strchr(buf, 'e') || strchr(buf, 'E'))
    {
        /* The lexer has no exponent, so spell it out and trim. */
        snprintf(buf, sizeof(buf), "%.12f", value);

        char *end = buf + strlen(buf) - 1;

        while (end > buf && *end == '0')
            *end-- = 0;
        if (end > buf && *end == '.')
            *end = 0;
    }

    out = buf;

    return true;
}

/* A .gen string literal: the lexer's pattern is "[^"\n]*" with no
 * escapes at all, so a quote or a newline simply cannot be written. */
static bool
validString (const std::string &s)
{
    return s.find('"') == std::string::npos &&
           s.find('\n') == std::string::npos;
}

/* Is `rhs' one value the loader's grammar accepts? Tokenize it and
 * pattern-match, so the check and the language cannot drift apart. */
static bool
validValueText (const std::string &rhs)
{
    std::vector<Tok> t;
    std::string err;
    int line;

    if (!thcGenLoader::tokenize(rhs, t, err, line))
        return false;

    size_t n = t.size() - 1;             /* END excluded                 */

    if (n == 1)
        return t[0].kind == Tok::NUMBER || t[0].kind == Tok::KNOB ||
               t[0].kind == Tok::STRING || t[0].kind == Tok::WORD;

    if (n == 2)
        return t[0].kind == Tok::NUMBER && t[1].kind == Tok::WORD &&
               (t[1].text == "s" || t[1].text == "ms" ||
                t[1].text == "beats" || t[1].text == "b");

    return false;
}

/* ---- the operations --------------------------------------------------- */

/* Every operation is the same sandwich: read, index, decide, splice,
 * write. `why' gets a sentence when the answer is no. */

typedef thcGenEdit::Result R;

static R
loadIndexed (const std::string &filename, std::string &text, Index &ix,
             std::string &why)
{
    if (!readFile(filename, text))
    {
        why = "could not read " + filename;
        return thcGenEdit::IO_ERROR;
    }

    if (!buildIndex(text, ix, why))
        return thcGenEdit::REFUSED;

    return thcGenEdit::OK;
}

static R
finish (const std::string &filename, std::string &text,
        std::vector<Edit> &edits, std::string &why)
{
    if (edits.empty())
        return thcGenEdit::OK;           /* nothing to change, no write  */

    if (!applyEdits(text, edits))
    {
        why = "internal: overlapping edits; nothing was written";
        return thcGenEdit::REFUSED;
    }

    if (!writeFile(filename, text))
    {
        why = "could not write " + filename;
        return thcGenEdit::IO_ERROR;
    }

    return thcGenEdit::OK;
}

static ChainIdx *
findChain (Index &ix, const std::string &name)
{
    for (size_t i = 0; i < ix.chains.size(); i++)
        if (ix.chains[i].name == name)
            return &ix.chains[i];

    return NULL;
}

static KnobIdx *
findKnob (Index &ix, const std::string &name)
{
    for (size_t i = 0; i < ix.knobs.size(); i++)
        if (ix.knobs[i].name == name)
            return &ix.knobs[i];

    return NULL;
}

/* ---- describe --------------------------------------------------------- */

R
thcGenEdit::describe (const std::string &filename, Doc &doc,
                      std::string &why)
{
    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    doc = Doc();
    doc.hasSeed = ix.seed.present;
    doc.seed = (unsigned)ix.seed.num;
    doc.hasTempo = ix.tempo.present;
    doc.tempo = ix.tempo.num;

    if (ix.infos.count("name"))
        doc.name = ix.infos["name"].str;
    if (ix.infos.count("author"))
        doc.author = ix.infos["author"].str;
    if (ix.infos.count("description"))
        doc.description = ix.infos["description"].str;

    for (size_t i = 0; i < ix.knobs.size(); i++)
    {
        Knob k;

        k.name = ix.knobs[i].name;
        k.value = ix.knobs[i].value;
        k.hasMin = ix.knobs[i].meta.count("min") != 0;
        k.hasMax = ix.knobs[i].meta.count("max") != 0;
        k.min = k.hasMin ? ix.knobs[i].meta["min"].num : 0;
        k.max = k.hasMax ? ix.knobs[i].meta["max"].num : 1;
        k.label = ix.knobs[i].meta.count("label")
            ? ix.knobs[i].meta["label"].str : "";

        doc.knobs.push_back(k);
    }

    for (size_t i = 0; i < ix.scales.size(); i++)
    {
        Scale s;

        s.name = ix.scales[i].name;
        s.notes = ix.scales[i].notes;
        doc.scales.push_back(s);
    }

    for (size_t i = 0; i < ix.presets.size(); i++)
    {
        Preset p;

        p.name = ix.presets[i].name;

        for (size_t k = 0; k < ix.presets[i].comps.size(); k++)
        {
            PresetValue v;

            v.name = ix.presets[i].comps[k].name;
            v.value = ix.presets[i].comps[k].value;
            p.values.push_back(v);
        }

        doc.presets.push_back(p);
    }

    for (size_t ci = 0; ci < ix.chains.size(); ci++)
    {
        ChainIdx &c = ix.chains[ci];
        Chain out;

        out.name = c.name;
        out.inputMidi = c.inputMidi;

        for (size_t si = 0; si < c.stages.size(); si++)
        {
            Stage st;

            st.name = c.stages[si].name;
            st.category = c.stages[si].cat;
            st.plugin = c.stages[si].plugin;

            for (size_t pi = 0; pi < c.stages[si].params.size(); pi++)
            {
                Param p;

                p.name = c.stages[si].params[pi].name;
                p.valueText = c.stages[si].params[pi].valueText;
                st.params.push_back(p);
            }

            out.stages.push_back(st);
        }

        for (size_t ki = 0; ki < c.sinks.size(); ki++)
        {
            Sink s;

            s.channel = c.sinks[ki].channel;
            s.chanarg = c.sinks[ki].hasArg ? c.sinks[ki].chanarg : "";
            out.sinks.push_back(s);
        }

        doc.chains.push_back(out);
    }

    return OK;
}

/* ---- piece header ----------------------------------------------------- */

R
thcGenEdit::setInfo (const std::string &filename, const std::string &key,
                     const std::string &text_, std::string &why)
{
    if (key != "name" && key != "author" && key != "description")
    {
        why = "no such info string";
        return REFUSED;
    }

    if (!validString(text_))
    {
        why = "a .gen string cannot contain a quote";
        return UNWRITABLE;
    }

    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    std::vector<Edit> edits;
    bool present = ix.infos.count(key) != 0;

    if (present && text_.empty())
        edits.push_back(eraseStmt(text, ix.infos[key].stmtA,
                                  ix.infos[key].stmtB));
    else if (present)
    {
        if (ix.infos[key].str != text_)
            edits.push_back({ ix.infos[key].valA, ix.infos[key].valB,
                              "\"" + text_ + "\"" });
    }
    else if (!text_.empty())
    {
        Edit e;

        e.a = e.b = lineStartOf(text, ix.headerEnd == ix.topInsert
                                ? ix.topInsert
                                : std::min(ix.headerEnd + 1, text.size()));

        if (ix.headerEnd != ix.topInsert)
        {
            /* after the existing header lines */
            size_t nl = text.find('\n', ix.headerEnd);

            e.a = e.b = nl == std::string::npos ? text.size() : nl + 1;
        }

        e.repl = key + " \"" + text_ + "\";\n";
        edits.push_back(e);
    }

    return finish(filename, text, edits, why);
}

/* seed and tempo share their shape exactly. */
static R
setHeaderNumber (const std::string &filename, const char *key, double value,
                 bool integer, std::string &why)
{
    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != thcGenEdit::OK)
        return r;

    MetaIdx &m = strcmp(key, "seed") == 0 ? ix.seed : ix.tempo;

    std::string num;

    if (integer)
    {
        char buf[32];

        snprintf(buf, sizeof(buf), "%u", (unsigned)value);
        num = buf;
    }
    else if (!thcGenEdit::format(value, num))
    {
        why = "that value cannot be written";
        return thcGenEdit::UNWRITABLE;
    }

    std::vector<Edit> edits;

    if (m.present)
    {
        if (m.num != value)
            edits.push_back({ m.valA, m.valB, num });
    }
    else
    {
        size_t at;

        if (ix.headerEnd != ix.topInsert)
        {
            size_t nl = text.find('\n', ix.headerEnd);

            at = nl == std::string::npos ? text.size() : nl + 1;
        }
        else
            at = ix.topInsert;

        edits.push_back({ at, at, std::string(key) + " " + num + ";\n" });
    }

    return finish(filename, text, edits, why);
}

static R
clearHeaderNumber (const std::string &filename, const char *key,
                   std::string &why)
{
    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != thcGenEdit::OK)
        return r;

    MetaIdx &m = strcmp(key, "seed") == 0 ? ix.seed : ix.tempo;

    std::vector<Edit> edits;

    if (m.present)
        edits.push_back(eraseStmt(text, m.stmtA, m.stmtB));

    return finish(filename, text, edits, why);
}

R
thcGenEdit::setSeed (const std::string &filename, unsigned seed,
                     std::string &why)
{
    return setHeaderNumber(filename, "seed", seed, true, why);
}

R
thcGenEdit::clearSeed (const std::string &filename, std::string &why)
{
    return clearHeaderNumber(filename, "seed", why);
}

R
thcGenEdit::setTempo (const std::string &filename, double bpm,
                      std::string &why)
{
    if (bpm <= 0)
    {
        why = "a tempo has to be positive";
        return REFUSED;
    }

    return setHeaderNumber(filename, "tempo", bpm, false, why);
}

R
thcGenEdit::clearTempo (const std::string &filename, std::string &why)
{
    return clearHeaderNumber(filename, "tempo", why);
}

/* ---- knobs ------------------------------------------------------------ */

R
thcGenEdit::setKnobValue (const std::string &filename,
                          const std::string &name, double value,
                          std::string &why)
{
    std::string text, num;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    KnobIdx *k = findKnob(ix, name);

    if (k == NULL)
    {
        why = "no knob called @" + name;
        return NOT_FOUND;
    }

    if (!format(value, num))
    {
        why = "that value cannot be written";
        return UNWRITABLE;
    }

    std::vector<Edit> edits;

    if (k->value != value)
        edits.push_back({ k->valA, k->valB, num });

    return finish(filename, text, edits, why);
}

R
thcGenEdit::setKnobMeta (const std::string &filename, const std::string &name,
                         double min, double max, const std::string &label,
                         std::string &why)
{
    if (!validString(label))
    {
        why = "a label cannot contain a quote";
        return UNWRITABLE;
    }

    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    KnobIdx *k = findKnob(ix, name);

    if (k == NULL)
    {
        why = "no knob called @" + name;
        return NOT_FOUND;
    }

    std::string minS, maxS;

    if (!format(min, minS) || !format(max, maxS))
    {
        why = "that range cannot be written";
        return UNWRITABLE;
    }

    std::vector<Edit> edits;

    /* Where a line the knob does not have gets added: after the last
       line of its block, keeping the block one block. */
    size_t insertAt;
    {
        size_t nl = text.find('\n', k->blockEnd);

        insertAt = nl == std::string::npos ? text.size() : nl + 1;
    }

    struct { const char *key; double v; const std::string *s; } want[3] = {
        { "min", min, &minS }, { "max", max, &maxS }, { "label", 0, &label },
    };

    for (int i = 0; i < 3; i++)
    {
        bool isLabel = i == 2;
        bool present = k->meta.count(want[i].key) != 0;

        if (present)
        {
            MetaIdx &m = k->meta[want[i].key];

            if (isLabel)
            {
                if (label.empty())
                    edits.push_back(eraseStmt(text, m.stmtA, m.stmtB));
                else if (m.str != label)
                    edits.push_back({ m.valA, m.valB,
                                      "\"" + label + "\"" });
            }
            else if (m.num != want[i].v)
                edits.push_back({ m.valA, m.valB, *want[i].s });
        }
        else if (!isLabel || !label.empty())
        {
            std::string line = "@" + name + "." + want[i].key + " = " +
                (isLabel ? "\"" + label + "\"" : *want[i].s) + ";\n";

            edits.push_back({ insertAt, insertAt, line });
        }
    }

    return finish(filename, text, edits, why);
}

R
thcGenEdit::addKnob (const std::string &filename, const std::string &name,
                     double value, double min, double max,
                     const std::string &label, std::string &why)
{
    if (!validName(name))
    {
        why = "'" + name + "' is not a name the file format accepts";
        return REFUSED;
    }

    if (!validString(label))
    {
        why = "a label cannot contain a quote";
        return UNWRITABLE;
    }

    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    if (findKnob(ix, name) != NULL)
    {
        why = "there is already a knob called @" + name;
        return REFUSED;
    }

    std::string vS, minS, maxS;

    if (!format(value, vS) || !format(min, minS) || !format(max, maxS))
    {
        why = "those values cannot be written";
        return UNWRITABLE;
    }

    std::ostringstream block;

    block << "@" << name << " = " << vS << ";\n"
          << "@" << name << ".widget = 1;\n"
          << "@" << name << ".min = " << minS << ";\n"
          << "@" << name << ".max = " << maxS << ";\n";

    if (!label.empty())
        block << "@" << name << ".label = \"" << label << "\";\n";

    block << "\n";

    /* Before the scales, before the chains -- the shape the shipped
       piece has and the reader expects. */
    size_t at = std::min(ix.firstScaleOff, ix.firstChainOff);

    if (at == std::string::npos)
    {
        if (!text.empty() && text[text.size() - 1] != '\n')
            text += "\n";

        at = text.size();
    }
    else
        at = lineStartOf(text, at);

    std::vector<Edit> edits;

    edits.push_back({ at, at, block.str() });

    return finish(filename, text, edits, why);
}

R
thcGenEdit::removeKnob (const std::string &filename, const std::string &name,
                        double fallback, int &rewritten, std::string &why)
{
    rewritten = 0;

    std::string text, num;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    KnobIdx *k = findKnob(ix, name);

    if (k == NULL)
    {
        why = "no knob called @" + name;
        return NOT_FOUND;
    }

    if (!format(fallback, num))
    {
        why = "the knob's value cannot be written in its place";
        return UNWRITABLE;
    }

    std::vector<Edit> edits;

    edits.push_back(eraseStmt(text, k->stmtA, k->stmtB));

    for (std::map<std::string, MetaIdx>::iterator m = k->meta.begin();
         m != k->meta.end(); ++m)
        edits.push_back(eraseStmt(text, m->second.stmtA, m->second.stmtB));

    /* Every `= @name' becomes the value those params were hearing. */
    for (size_t ci = 0; ci < ix.chains.size(); ci++)
        for (size_t si = 0; si < ix.chains[ci].stages.size(); si++)
            for (size_t pi = 0;
                 pi < ix.chains[ci].stages[si].params.size(); pi++)
            {
                PIdx &p = ix.chains[ci].stages[si].params[pi];

                if (p.valueText == "@" + name)
                {
                    edits.push_back({ p.valA, p.valB, num });
                    rewritten++;
                }
            }

    return finish(filename, text, edits, why);
}

/* ---- scales ----------------------------------------------------------- */

R
thcGenEdit::addScale (const std::string &filename, const std::string &name,
                      const std::string &notes, std::string &why)
{
    if (!validName(name))
    {
        why = "'" + name + "' is not a name the file format accepts";
        return REFUSED;
    }

    std::vector<int> resolved;
    std::string bad;

    if (!thcGenLoader::parseNoteList(notes, resolved, bad))
    {
        why = "'" + bad + "' is not a note name";
        return REFUSED;
    }

    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    for (size_t i = 0; i < ix.scales.size(); i++)
        if (ix.scales[i].name == name)
        {
            why = "there is already a scale called " + name;
            return REFUSED;
        }

    size_t at = ix.firstChainOff;

    if (at == std::string::npos)
    {
        if (!text.empty() && text[text.size() - 1] != '\n')
            text += "\n";

        at = text.size();
    }
    else
        at = lineStartOf(text, at);

    std::vector<Edit> edits;

    edits.push_back({ at, at,
                      "scale " + name + " \"" + notes + "\";\n\n" });

    return finish(filename, text, edits, why);
}

R
thcGenEdit::setScale (const std::string &filename, const std::string &name,
                      const std::string &notes, std::string &why)
{
    std::vector<int> resolved;
    std::string bad;

    if (!thcGenLoader::parseNoteList(notes, resolved, bad))
    {
        why = "'" + bad + "' is not a note name";
        return REFUSED;
    }

    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    std::vector<Edit> edits;

    for (size_t i = 0; i < ix.scales.size(); i++)
        if (ix.scales[i].name == name)
        {
            if (ix.scales[i].notes != notes)
                edits.push_back({ ix.scales[i].valA, ix.scales[i].valB,
                                  "\"" + notes + "\"" });

            return finish(filename, text, edits, why);
        }

    why = "no scale called " + name;
    return NOT_FOUND;
}

R
thcGenEdit::removeScale (const std::string &filename, const std::string &name,
                         int &rewritten, std::string &why)
{
    rewritten = 0;

    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    ScaleIdx *found = NULL;

    for (size_t i = 0; i < ix.scales.size(); i++)
        if (ix.scales[i].name == name)
            found = &ix.scales[i];

    if (found == NULL)
    {
        why = "no scale called " + name;
        return NOT_FOUND;
    }

    std::vector<Edit> edits;

    edits.push_back(eraseStmt(text, found->stmtA, found->stmtB));

    /* References inlined as the literal, so the chains keep playing the
       same notes the scale gave them. */
    for (size_t ci = 0; ci < ix.chains.size(); ci++)
        for (size_t si = 0; si < ix.chains[ci].stages.size(); si++)
            for (size_t pi = 0;
                 pi < ix.chains[ci].stages[si].params.size(); pi++)
            {
                PIdx &p = ix.chains[ci].stages[si].params[pi];

                if (p.valueText == name)
                {
                    edits.push_back({ p.valA, p.valB,
                                      "\"" + found->notes + "\"" });
                    rewritten++;
                }
            }

    return finish(filename, text, edits, why);
}

/* ---- building blocks for chains and stages ---------------------------- */

static std::string
stageText (const std::string &stageName, const std::string &category,
           const std::string &plugin,
           const std::vector<std::pair<std::string, std::string> > &params)
{
    std::ostringstream s;

    s << "    stage " << stageName << " " << category << "::" << plugin
      << " {\n";

    for (size_t i = 0; i < params.size(); i++)
        s << "        " << params[i].first << " = " << params[i].second
          << ";\n";

    s << "    };\n";

    return s.str();
}

static std::string
sinkText (int channel, const std::string &chanarg)
{
    std::ostringstream s;

    s << "    sink { channel = " << channel << ";";

    if (!chanarg.empty())
        s << " chanarg = \"" << chanarg << "\";";

    s << " };\n";

    return s.str();
}

static bool
validParams (const std::vector<std::pair<std::string, std::string> > &params,
             std::string &why)
{
    for (size_t i = 0; i < params.size(); i++)
        if (!validWord(params[i].first) ||
            !validValueText(params[i].second))
        {
            why = "'" + params[i].first + " = " + params[i].second +
                  "' is not something the file format can say";
            return false;
        }

    return true;
}

/* ---- chains ----------------------------------------------------------- */

/* ---- presets ----------------------------------------------------------- */

/* Where a preset's block is written and what one line of it looks like.
 * A preset sits with the scales -- both are named objects a chain refers
 * to, both must be declared before the chain that names them -- so it
 * goes after the last one of either, and before the first chain. */
static std::string
presetLine (const std::string &indent, const std::string &name, double value)
{
    std::string num;

    thcGenEdit::format(value, num);

    return indent + name + " = " + num + ";\n";
}

/* The indentation this preset's components already use, so a component
 * added to a hand-formatted block does not arrive at the house default
 * and make the file look edited. Four spaces when there is nothing to
 * copy from -- but a preset always has a component to copy from, since
 * one that sets nothing does not load. */
static std::string
presetIndent (const std::string &text, const PresetIdx &pr)
{
    if (pr.comps.empty())
        return "    ";

    const size_t ls = lineStartOf(text, pr.comps[0].stmtA);

    if (ls < pr.comps[0].stmtA &&
        text.find_first_not_of(" \t", ls) >= pr.comps[0].stmtA)
        return text.substr(ls, pr.comps[0].stmtA - ls);

    return "    ";
}

static PresetIdx *
findPreset (Index &ix, const std::string &name)
{
    for (size_t i = 0; i < ix.presets.size(); i++)
        if (ix.presets[i].name == name)
            return &ix.presets[i];

    return NULL;
}

/* Every stage param whose value is the bare word `name'.
 *
 * A scale reference and a preset reference are spelled identically -- a
 * bare word -- and this index does not know which params are which type,
 * because that is the plugin's business and thcGenEdit deliberately does
 * not load plugins. So this over-reports by design: a stage whose NOTESET
 * param names a scale that happens to share a preset's name counts here.
 * The consequence is a removal refused that could have gone ahead, which
 * is the safe direction and is fixed by not naming two things the same. */
static int
referencesTo (const Index &ix, const std::string &name,
              std::string &firstWhere)
{
    int n = 0;

    for (size_t ci = 0; ci < ix.chains.size(); ci++)
        for (size_t si = 0; si < ix.chains[ci].stages.size(); si++)
            for (size_t pi = 0;
                 pi < ix.chains[ci].stages[si].params.size(); pi++)
                if (ix.chains[ci].stages[si].params[pi].valueText == name)
                {
                    if (n == 0)
                        firstWhere = ix.chains[ci].name + "'s stage " +
                            ix.chains[ci].stages[si].name;

                    n++;
                }

    return n;
}

R
thcGenEdit::addPreset (const std::string &filename, const std::string &name,
                       const std::vector<PresetValue> &values,
                       std::string &why)
{
    if (!validName(name))
    {
        why = "'" + name + "' is not a name the file format accepts";
        return REFUSED;
    }

    if (values.empty())
    {
        why = "a preset needs at least one value";
        return REFUSED;
    }

    for (size_t i = 0; i < values.size(); i++)
    {
        if (!validName(values[i].name))
        {
            why = "'" + values[i].name + "' is not a chanarg name";
            return REFUSED;
        }

        for (size_t k = 0; k < i; k++)
            if (values[k].name == values[i].name)
            {
                why = "'" + values[i].name + "' is set twice";
                return REFUSED;
            }

        std::string num;

        if (!thcGenEdit::format(values[i].value, num))
        {
            why = "that value cannot be written in a .gen";
            return REFUSED;
        }
    }

    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    if (findPreset(ix, name) != NULL)
    {
        why = "there is already a preset called " + name;
        return REFUSED;
    }

    std::string body;

    for (size_t i = 0; i < values.size(); i++)
        body += presetLine("    ", values[i].name, values[i].value);

    const std::string block = "preset " + name + " {\n" + body + "};\n";

    /* After the last scale or preset already there; failing that, before
       the first chain; failing that, at the end. That is the order the
       loader requires -- a named object has to be declared before the
       chain that refers to it -- and it puts a new preset where an author
       would have written one. */
    size_t after = std::string::npos;

    if (!ix.presets.empty())
        after = ix.presets[ix.presets.size() - 1].stmtB;
    else if (!ix.scales.empty())
        after = ix.scales[ix.scales.size() - 1].stmtB;

    std::vector<Edit> edits;

    if (after != std::string::npos)
    {
        /* The start of the line after that statement's, so the insert
           lands between whole lines and any trailing comment on it stays
           where its author put it. */
        const size_t nl = text.find('\n', after);
        const size_t at = (nl == std::string::npos) ? text.size() : nl + 1;

        edits.push_back({ at, at, "\n" + block });
    }
    else if (ix.firstChainOff != std::string::npos)
    {
        const size_t at = lineStartOf(text, ix.firstChainOff);

        edits.push_back({ at, at, block + "\n" });
    }
    else
    {
        if (!text.empty() && text[text.size() - 1] != '\n')
            text += "\n";

        edits.push_back({ text.size(), text.size(), "\n" + block });
    }

    return finish(filename, text, edits, why);
}

R
thcGenEdit::setPresetValue (const std::string &filename,
                            const std::string &preset,
                            const std::string &component,
                            double value, std::string &why)
{
    std::string num;

    if (!thcGenEdit::format(value, num))
    {
        why = "that value cannot be written in a .gen";
        return REFUSED;
    }

    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    PresetIdx *pr = findPreset(ix, preset);

    if (pr == NULL)
    {
        why = "no preset called " + preset;
        return NOT_FOUND;
    }

    for (size_t i = 0; i < pr->comps.size(); i++)
        if (pr->comps[i].name == component)
        {
            std::vector<Edit> edits;

            /* A no-op write changes no byte, which is the property the
               whole splice model rests on. */
            if (text.compare(pr->comps[i].valA,
                             pr->comps[i].valB - pr->comps[i].valA, num) != 0)
                edits.push_back({ pr->comps[i].valA, pr->comps[i].valB, num });

            return finish(filename, text, edits, why);
        }

    why = preset + " does not set " + component;
    return NOT_FOUND;
}

R
thcGenEdit::addPresetValue (const std::string &filename,
                            const std::string &preset,
                            const std::string &component,
                            double value, std::string &why)
{
    if (!validName(component))
    {
        why = "'" + component + "' is not a chanarg name";
        return REFUSED;
    }

    std::string num;

    if (!thcGenEdit::format(value, num))
    {
        why = "that value cannot be written in a .gen";
        return REFUSED;
    }

    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    PresetIdx *pr = findPreset(ix, preset);

    if (pr == NULL)
    {
        why = "no preset called " + preset;
        return NOT_FOUND;
    }

    for (size_t i = 0; i < pr->comps.size(); i++)
        if (pr->comps[i].name == component)
        {
            why = preset + " already sets " + component;
            return REFUSED;
        }

    /* At the end of the block, not in some canonical slot: a preset is a
       vector and its order is the author's. Reshuffling someone's file
       into the order this writer would have chosen is an edit nobody
       asked for -- the same rule the .dsp writer follows. */
    std::vector<Edit> edits;

    const size_t ls = lineStartOf(text, pr->bodyClose);

    /* Whether the closing brace has a line to itself decides how the new
       component is written, and getting this wrong is not cosmetic. A
       preset written `preset p { a = 1; };' has its `}' on the same line
       as the `preset' keyword, so the start of that line is *before* the
       block -- inserting there would put the new component above the
       statement it belongs to and break the file. Multi-line blocks get
       a line of their own; a one-liner stays a one-liner. */
    if (text.find_first_not_of(" \t", ls) >= pr->bodyClose)
        edits.push_back({ ls, ls,
                          presetLine(presetIndent(text, *pr), component,
                                     value) });
    else
    {
        std::string num;

        thcGenEdit::format(value, num);

        /* A space of our own only where there is not one already, so
           `{ a = 1; }' and `{ a = 1;}' both come out readable. */
        const bool spaced = pr->bodyClose > 0 &&
            (text[pr->bodyClose - 1] == ' ' ||
             text[pr->bodyClose - 1] == '\t');

        edits.push_back({ pr->bodyClose, pr->bodyClose,
                          (spaced ? "" : " ") + component + " = " + num +
                          "; " });
    }

    return finish(filename, text, edits, why);
}

R
thcGenEdit::removePresetValue (const std::string &filename,
                               const std::string &preset,
                               const std::string &component,
                               std::string &why)
{
    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    PresetIdx *pr = findPreset(ix, preset);

    if (pr == NULL)
    {
        why = "no preset called " + preset;
        return NOT_FOUND;
    }

    for (size_t i = 0; i < pr->comps.size(); i++)
        if (pr->comps[i].name == component)
        {
            if (pr->comps.size() == 1)
            {
                why = component + " is all " + preset + " sets, and a "
                    "preset that sets nothing will not load";
                return REFUSED;
            }

            std::vector<Edit> edits;

            edits.push_back(eraseStmt(text, pr->comps[i].stmtA,
                                      pr->comps[i].stmtB));

            return finish(filename, text, edits, why);
        }

    why = preset + " does not set " + component;
    return NOT_FOUND;
}

R
thcGenEdit::removePreset (const std::string &filename,
                          const std::string &name, std::string &why)
{
    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    PresetIdx *pr = findPreset(ix, name);

    if (pr == NULL)
    {
        why = "no preset called " + name;
        return NOT_FOUND;
    }

    std::string where;
    const int refs = referencesTo(ix, name, where);

    if (refs > 0)
    {
        /* Refused rather than inlined, because there is nothing to inline
           into: the format has no literal form for a chanarg vector, on
           purpose. Naming where the first reference is, because "it is
           used" without "by what" sends the reader through the file. */
        std::ostringstream m;

        m << name << " is still used by " << where;

        if (refs > 1)
            m << " and " << (refs - 1) << " other"
              << (refs > 2 ? "s" : "");

        why = m.str();
        return REFUSED;
    }

    std::vector<Edit> edits;

    edits.push_back(eraseStmt(text, pr->stmtA, pr->stmtB));

    return finish(filename, text, edits, why);
}

R
thcGenEdit::addChain (const std::string &filename, const std::string &name,
                      int channel, const std::string &stageName,
                      const std::string &category, const std::string &plugin,
                      const std::vector<std::pair<std::string,
                          std::string> > &params,
                      std::string &why)
{
    if (!validName(name) || !validName(stageName))
    {
        why = "that is not a name the file format accepts";
        return REFUSED;
    }

    /* File numbers, because this edits the file. 1-16, and the loader
       maps them onto the engine's 0-15 on the way in. */
    if (channel < 1 || channel > 16)
    {
        why = "channel is 1-16";
        return REFUSED;
    }

    if (!validParams(params, why))
        return UNWRITABLE;

    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    if (findChain(ix, name) != NULL)
    {
        why = "there is already a chain called " + name;
        return REFUSED;
    }

    if (!text.empty() && text[text.size() - 1] != '\n')
        text += "\n";

    std::ostringstream block;

    block << "\nchain " << name << " {\n"
          << stageText(stageName, category, plugin, params)
          << sinkText(channel, "")
          << "};\n";

    std::vector<Edit> edits;

    edits.push_back({ text.size(), text.size(), block.str() });

    return finish(filename, text, edits, why);
}

R
thcGenEdit::removeChain (const std::string &filename, const std::string &name,
                         std::string &why)
{
    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    ChainIdx *c = findChain(ix, name);

    if (c == NULL)
    {
        why = "no chain called " + name;
        return NOT_FOUND;
    }

    std::vector<Edit> edits;

    edits.push_back(eraseStmt(text, c->stmtA, c->stmtB));

    return finish(filename, text, edits, why);
}

R
thcGenEdit::renameChain (const std::string &filename,
                         const std::string &oldName,
                         const std::string &newName, std::string &why)
{
    if (!validName(newName))
    {
        why = "'" + newName + "' is not a name the file format accepts";
        return REFUSED;
    }

    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    if (findChain(ix, newName) != NULL)
    {
        why = "there is already a chain called " + newName;
        return REFUSED;
    }

    ChainIdx *c = findChain(ix, oldName);

    if (c == NULL)
    {
        why = "no chain called " + oldName;
        return NOT_FOUND;
    }

    std::vector<Edit> edits;

    edits.push_back({ c->nameA, c->nameB, newName });

    return finish(filename, text, edits, why);
}

R
thcGenEdit::setChainInput (const std::string &filename,
                           const std::string &chain, bool midi,
                           std::string &why)
{
    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    ChainIdx *c = findChain(ix, chain);

    if (c == NULL)
    {
        why = "no chain called " + chain;
        return NOT_FOUND;
    }

    std::vector<Edit> edits;

    if (midi && !c->inputMidi)
    {
        /* On its own line right after the opening brace. */
        size_t nl = text.find('\n', c->stmtA);
        size_t at = nl == std::string::npos ? text.size() : nl + 1;

        edits.push_back({ at, at, "    input midi;\n" });
    }
    else if (!midi && c->inputMidi)
        edits.push_back(eraseStmt(text, c->inputA, c->inputB));

    return finish(filename, text, edits, why);
}

/* ---- stages ----------------------------------------------------------- */

R
thcGenEdit::addStage (const std::string &filename, const std::string &chain,
                      const std::string &stageName,
                      const std::string &category, const std::string &plugin,
                      const std::vector<std::pair<std::string,
                          std::string> > &params,
                      std::string &why)
{
    if (!validName(stageName))
    {
        why = "'" + stageName + "' is not a name the file format accepts";
        return REFUSED;
    }

    if (!validParams(params, why))
        return UNWRITABLE;

    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    ChainIdx *c = findChain(ix, chain);

    if (c == NULL)
    {
        why = "no chain called " + chain;
        return NOT_FOUND;
    }

    /* Before the first sink: sinks end a chain. With no sink yet (a
       state this editor never writes but a hand file might briefly be
       in), before the closing brace. */
    size_t at = c->sinks.empty() ? c->bodyClose : c->sinks[0].stmtA;

    at = lineStartOf(text, at);

    std::vector<Edit> edits;

    edits.push_back({ at, at, stageText(stageName, category, plugin,
                                        params) });

    return finish(filename, text, edits, why);
}

R
thcGenEdit::removeStage (const std::string &filename,
                         const std::string &chain, int stageIndex,
                         std::string &why)
{
    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    ChainIdx *c = findChain(ix, chain);

    if (c == NULL)
    {
        why = "no chain called " + chain;
        return NOT_FOUND;
    }

    if (stageIndex < 0 || stageIndex >= (int)c->stages.size())
    {
        why = "no such stage";
        return NOT_FOUND;
    }

    std::vector<Edit> edits;

    edits.push_back(eraseStmt(text, c->stages[stageIndex].stmtA,
                              c->stages[stageIndex].stmtB));

    return finish(filename, text, edits, why);
}

R
thcGenEdit::moveStage (const std::string &filename, const std::string &chain,
                       int from, int to, std::string &why)
{
    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    ChainIdx *c = findChain(ix, chain);

    if (c == NULL)
    {
        why = "no chain called " + chain;
        return NOT_FOUND;
    }

    int n = (int)c->stages.size();

    if (from < 0 || from >= n || to < 0 || to >= n)
    {
        why = "no such stage";
        return NOT_FOUND;
    }

    if (from == to)
        return OK;

    /* The whole block moves as text -- one cut, one paste -- so any
       comment inside the stage travels with it. */
    Edit cut = eraseStmt(text, c->stages[from].stmtA, c->stages[from].stmtB);
    std::string block = text.substr(cut.a, cut.b - cut.a);

    if (block.empty() || block[block.size() - 1] != '\n')
        block += "\n";

    size_t at;

    if (to < from)
        at = lineStartOf(text, c->stages[to].stmtA);
    else if (to + 1 < n)
        at = lineStartOf(text, c->stages[to + 1].stmtA);
    else
        at = lineStartOf(text, c->sinks.empty() ? c->bodyClose
                                                : c->sinks[0].stmtA);

    text.erase(cut.a, cut.b - cut.a);

    if (at > cut.a)
        at -= cut.b - cut.a;

    text.insert(at, block);

    if (!writeFile(filename, text))
    {
        why = "could not write " + filename;
        return IO_ERROR;
    }

    return OK;
}

R
thcGenEdit::setParam (const std::string &filename, const std::string &chain,
                      int stageIndex, const std::string &param,
                      const std::string &valueText, std::string &why)
{
    if (!validValueText(valueText))
    {
        why = "'" + valueText + "' is not a value the file format can say";
        return UNWRITABLE;
    }

    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    ChainIdx *c = findChain(ix, chain);

    if (c == NULL)
    {
        why = "no chain called " + chain;
        return NOT_FOUND;
    }

    if (stageIndex < 0 || stageIndex >= (int)c->stages.size())
    {
        why = "no such stage";
        return NOT_FOUND;
    }

    StageIdx &s = c->stages[stageIndex];
    std::vector<Edit> edits;

    for (size_t i = 0; i < s.params.size(); i++)
        if (s.params[i].name == param)
        {
            if (s.params[i].valueText != valueText)
                edits.push_back({ s.params[i].valA, s.params[i].valB,
                                  valueText });

            return finish(filename, text, edits, why);
        }

    /* The param existed only as the plugin's default: add the line,
       indented like its neighbors (or the house four-plus-four). */
    std::string indent = "        ";

    if (!s.params.empty())
    {
        size_t ls = lineStartOf(text, s.params[0].stmtA);
        size_t sp = text.find_first_not_of(" \t", ls);

        if (sp != std::string::npos && sp >= s.params[0].stmtA - 0 &&
            text.compare(ls, s.params[0].stmtA - ls,
                         std::string(s.params[0].stmtA - ls, ' ')) == 0)
            indent = text.substr(ls, s.params[0].stmtA - ls);
    }

    size_t at = lineStartOf(text, s.bodyClose);

    edits.push_back({ at, at, indent + param + " = " + valueText + ";\n" });

    return finish(filename, text, edits, why);
}

/* ---- sinks ------------------------------------------------------------ */

R
thcGenEdit::addSink (const std::string &filename, const std::string &chain,
                     int channel, const std::string &chanarg,
                     std::string &why)
{
    /* File numbers, because this edits the file. 1-16, and the loader
       maps them onto the engine's 0-15 on the way in. */
    if (channel < 1 || channel > 16)
    {
        why = "channel is 1-16";
        return REFUSED;
    }

    if (!validString(chanarg))
    {
        why = "a chanarg name cannot contain a quote";
        return UNWRITABLE;
    }

    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    ChainIdx *c = findChain(ix, chain);

    if (c == NULL)
    {
        why = "no chain called " + chain;
        return NOT_FOUND;
    }

    size_t at = lineStartOf(text, c->bodyClose);

    std::vector<Edit> edits;

    edits.push_back({ at, at, sinkText(channel, chanarg) });

    return finish(filename, text, edits, why);
}

R
thcGenEdit::removeSink (const std::string &filename, const std::string &chain,
                        int sinkIndex, std::string &why)
{
    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    ChainIdx *c = findChain(ix, chain);

    if (c == NULL)
    {
        why = "no chain called " + chain;
        return NOT_FOUND;
    }

    if (sinkIndex < 0 || sinkIndex >= (int)c->sinks.size())
    {
        why = "no such sink";
        return NOT_FOUND;
    }

    if (c->sinks.size() == 1)
    {
        why = "a chain needs at least one sink";
        return REFUSED;
    }

    std::vector<Edit> edits;

    edits.push_back(eraseStmt(text, c->sinks[sinkIndex].stmtA,
                              c->sinks[sinkIndex].stmtB));

    return finish(filename, text, edits, why);
}

R
thcGenEdit::setSink (const std::string &filename, const std::string &chain,
                     int sinkIndex, int channel, const std::string &chanarg,
                     std::string &why)
{
    /* File numbers, because this edits the file. 1-16, and the loader
       maps them onto the engine's 0-15 on the way in. */
    if (channel < 1 || channel > 16)
    {
        why = "channel is 1-16";
        return REFUSED;
    }

    if (!validString(chanarg))
    {
        why = "a chanarg name cannot contain a quote";
        return UNWRITABLE;
    }

    std::string text;
    Index ix;
    R r = loadIndexed(filename, text, ix, why);

    if (r != OK)
        return r;

    ChainIdx *c = findChain(ix, chain);

    if (c == NULL)
    {
        why = "no chain called " + chain;
        return NOT_FOUND;
    }

    if (sinkIndex < 0 || sinkIndex >= (int)c->sinks.size())
    {
        why = "no such sink";
        return NOT_FOUND;
    }

    SinkIdx &s = c->sinks[sinkIndex];
    std::vector<Edit> edits;

    if (s.channel != channel)
    {
        char buf[16];

        snprintf(buf, sizeof(buf), "%d", channel);

        if (s.chValB > s.chValA)
            edits.push_back({ s.chValA, s.chValB, buf });
    }

    if (s.hasArg && chanarg.empty())
    {
        /* Back to a note sink; take the statement and the blank before
           it so `{ channel = 3; };' closes up tidily. */
        size_t a = s.argStmtA;

        while (a > 0 && (text[a - 1] == ' ' || text[a - 1] == '\t'))
            a--;

        edits.push_back({ a, s.argStmtB, "" });
    }
    else if (s.hasArg)
    {
        if (s.chanarg != chanarg)
            edits.push_back({ s.argValA, s.argValB,
                              "\"" + chanarg + "\"" });
    }
    else if (!chanarg.empty())
        edits.push_back({ s.bodyClose, s.bodyClose,
                          "chanarg = \"" + chanarg + "\"; " });

    return finish(filename, text, edits, why);
}
