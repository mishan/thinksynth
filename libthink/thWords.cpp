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

#include "thWords.h"

/* ---- pitch names ------------------------------------------------------ */

/* "F3 Ab3 C4" or "F3,Ab3,C4" -> {53, 56, 60}. The one place in the tree
 * that turns pitch text into numbers; plugins receive only the numbers.
 *
 * A `.' is a rest and resolves as -1: an entry that takes its turn and
 * sounds nothing. Every ladder already filters what it reads to 0..127,
 * so on one a rest is simply not a degree; a pool that is *cycled* --
 * gen::euclid's -- reads it as an onset with no note, which is what lets
 * a pool carry a rhythm as well as a progression. */
bool
thParseNoteList (const std::string &text, std::vector<int> &out,
                 std::string &bad)
{
    static const int semis[7] = { 9, 11, 0, 2, 4, 5, 7 };  /* A..G */

    out.clear();

    size_t i = 0;

    while (i < text.size())
    {
        if (text[i] == ' ' || text[i] == ',' || text[i] == '\t')
        {
            i++;
            continue;
        }

        if (text[i] == '.')
        {
            out.push_back(-1);
            i++;
            continue;
        }

        size_t start = i;
        char letter = text[i];

        if (letter < 'A' || letter > 'G')
        {
            bad = text.substr(start, text.find_first_of(" ,\t", start) - start);
            return false;
        }

        int n = semis[letter - 'A'];

        i++;

        if (i < text.size() && text[i] == '#') { n++; i++; }
        else if (i < text.size() && text[i] == 'b') { n--; i++; }

        bool neg = false;

        if (i < text.size() && text[i] == '-') { neg = true; i++; }

        if (i >= text.size() || text[i] < '0' || text[i] > '9')
        {
            bad = text.substr(start, i - start + 1);
            return false;
        }

        int octave = 0;

        /* Past 99 it is out of range either way, and stops growing
           before it overflows. */
        while (i < text.size() && text[i] >= '0' && text[i] <= '9')
        {
            if (octave < 100)
                octave = octave * 10 + (text[i] - '0');

            i++;
        }

        if (neg)
            octave = -octave;

        int midi = 12 * (octave + 1) + n;     /* C4 == 60 */

        if (midi < 0 || midi > 127)
        {
            bad = text.substr(start, i - start);
            return false;
        }

        out.push_back(midi);
    }

    return !out.empty();
}

/* ---- lexing ----------------------------------------------------------- */

/* The .dsp lexical layer, shared rather than reproduced.
 *
 * This used to be a hand-written scanner that copied thinklex.ll's rules
 * -- `#' comments, whitespace, numbers, words, quoted strings, `::', the
 * punctuation -- because linking the flex lexer would have meant
 * inheriting a grammar and a drawerful of globals along with it. The
 * globals are gone (thinklang is pure), and the lexer is now a
 * standalone thing in libthink that answers to neither language
 * (thLexer.h). So the copy is retired and this is an adapter over the
 * real one: two languages, one lexical layer, and nothing left for them
 * to drift apart along.
 *
 * The adapter does the three things .gen's shape asks for and .dsp's
 * does not:
 *
 *  - A leading `-' folds into the number after it. .gen has no
 *    arithmetic and so no grammar to hang a SUB token on; .dsp has
 *    both, which is why the shared lexer keeps them apart and this puts
 *    them back together. Adjacency is checked by span, which is what
 *    still makes `-5' a literal and `- 5' the error it always was.
 *  - `@name' folds into one KNOB token, spelled without the `@' and
 *    spanned with it. Same reason: `@' is an operator in a .dsp
 *    expression, and in .gen it is only ever the front of a knob's name.
 *  - Punctuation .gen has no use for is refused here rather than carried
 *    into the parser to be rejected further from the cause, and with the
 *    wording the hand-written scanner used: a `+' in a .gen file is a
 *    stray character, not a missing rule. `%' is on the useful side of
 *    that line: the unit suffix an instrument value needs when the
 *    chanarg it lands on was declared as a percentage, and the modulo
 *    .gen arithmetic shares with .dsp. `->' crosses the same way and for
 *    a better reason: it is the .dsp spelling for reading a node's
 *    output, and a .gen has nodes to read. One arrow, one meaning, both languages -- the shared lexer
 *    has always handed it back whole (longest match, so it
 *    was never `-' then `>'), and .gen simply stopped refusing it.
 *
 * Every token still carries its byte span, and a STRING's span still
 * includes its quotes: thcGenEdit replaces spans, and what sits between
 * them -- comments, indentation, the author's blank lines -- is never
 * touched. */
/* True if the last token emitted could be the end of a value, which is what
   decides whether a `-' after it is a sign or an operator. */
static bool
endsAValue (const std::vector<thWordToken> &out)
{
    if (out.empty())
        return false;

    const thWordToken &t = out[out.size() - 1];

    return t.kind == thWordToken::NUMBER || t.kind == thWordToken::KNOB ||
           t.kind == thWordToken::WORD ||
           (t.kind == thWordToken::PUNCT && t.text == ")");
}

bool
thWordTokenize (const std::string &text, std::vector<thWordToken> &out,
                std::string &err, int &errLine,
                std::vector<thLexToken> *rawOut)
{
    /* A public tokenizer owns its output: a caller reusing one vector
       across files must not get the previous file's tail. */
    out.clear();
    err.clear();
    errLine = 0;

    std::vector<thLexToken> raw;

    thLexString(text, raw);

    bool ok = true;
    int graphDepth = 0;     /* inside `dsp { }', where .dsp's tokens are */

    /* raw always ends with an END token, so raw[i + 1] below is in range
       for every i this loop looks at a real token at. */
    for (size_t i = 0; i < raw.size(); i++)
    {
        const thLexToken &t = raw[i];

        if (t.kind == thLexToken::END)
            break;

        if (t.kind == thLexToken::ERROR)
        {
            err = t.text;
            errLine = t.line;
            ok = false;
            break;
        }

        thWordToken g;

        g.line = t.line;
        g.num  = 0;
        g.off  = t.off;
        g.end  = t.end;
        g.text = t.text;

        if (t.kind == thLexToken::WORD)
            g.kind = thWordToken::WORD;
        else if (t.kind == thLexToken::STRING)
            g.kind = thWordToken::STRING;
        else if (t.kind == thLexToken::NUMBER)
        {
            g.kind = thWordToken::NUMBER;
            g.num  = t.num;
        }
        else if (t.text == "::")
            g.kind = thWordToken::MODSEP;
        else if (t.text == ";" || t.text == "=" || t.text == "{" ||
                 t.text == "}" || t.text == "." || t.text == "%" ||
                 t.text == "->" || t.text == "+" || t.text == "*" ||
                 t.text == "/" || t.text == "(" || t.text == ")" ||
                 t.text == ",")
            g.kind = thWordToken::PUNCT;
        /* `-' fuses onto the number after it -- `= -5' is one token -- but
           only where nothing before it could have ended a value. `a - 5' and
           `a -5' are both a subtraction; without the second half of this test
           the latter lexed as two values in a row, which was an error message
           about a missing semicolon on a line whose semicolon is right
           there. */
        else if (t.text == "-" && raw[i + 1].kind == thLexToken::NUMBER &&
                 raw[i + 1].off == t.end && !endsAValue(out))
        {
            g.kind = thWordToken::NUMBER;
            g.text = "-" + raw[i + 1].text;
            g.num  = -raw[i + 1].num;
            g.end  = raw[i + 1].end;
            i++;
        }
        else if (t.text == "-")
            g.kind = thWordToken::PUNCT;
        else if (t.text == "@" && raw[i + 1].kind == thLexToken::WORD &&
                 raw[i + 1].off == t.end)
        {
            g.kind = thWordToken::KNOB;
            g.text = raw[i + 1].text;
            g.end  = raw[i + 1].end;
            i++;
        }
        else if (t.text == "@")
        {
            err = "'@' with no knob name after it";
            errLine = t.line;
            ok = false;
            break;
        }
        /* A node array, inside a graph written into the piece and nowhere
           else. */
        else if ((t.text == "[" || t.text == "]") && graphDepth > 0)
            g.kind = thWordToken::PUNCT;
        else
        {
            err = std::string("stray character '") + t.text[0] + "'";
            errLine = t.line;
            ok = false;
            break;
        }

        /* `dsp {' as a statement, not a block that happens to be named
           dsp: the word starts the statement it is in. */
        if (g.kind == thWordToken::PUNCT && g.text == "{" &&
            (graphDepth > 0 ||
             (out.size() >= 2 && out.back().kind == thWordToken::WORD &&
              out.back().text == "dsp" &&
              out[out.size() - 2].kind == thWordToken::PUNCT &&
              (out[out.size() - 2].text == ";" ||
               out[out.size() - 2].text == "{"))))
            graphDepth++;
        else if (g.kind == thWordToken::PUNCT && g.text == "}" &&
                 graphDepth > 0)
            graphDepth--;

        out.push_back(g);
    }

    thWordToken end;

    end.kind = thWordToken::END;
    end.line = ok ? raw[raw.size() - 1].line : errLine;
    end.num  = 0;
    end.off  = end.end = text.size();
    out.push_back(end);

    if (rawOut)
        rawOut->swap(raw);

    return ok;
}
