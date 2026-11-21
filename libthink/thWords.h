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

#ifndef TH_WORDS_H
#define TH_WORDS_H

#include <string>
#include <vector>

#include "thExport.h"
#include "thLexer.h"

/* The words-numbers-and-knobs view of the shared lexer (thLexer.h): what
   .gen, its editor and a controller's profile are read in, below all
   three, as the lexer is below .dsp. */

/* One token of .gen source, or of a controller profile, with its byte
 * span. Public rather than a loader detail because thcGenEdit splices
 * files by replacing exact token spans -- the same lexer feeds the reader
 * and the writer, which is what guarantees an edit never moves a byte it
 * was not aimed at (comments included; they live between the spans).
 *
 * Not thLexToken itself, though it is built from one. The shared lexer
 * is deliberately language-neutral: it hands back `-' and `5' as two
 * tokens because .dsp has arithmetic, and `@' and `density' as two
 * because .dsp has an operator there. .gen has neither, and its parser
 * is much clearer reading a NUMBER of -5 and a KNOB called density than
 * re-deciding that at every call site. thWordTokenize() is where the
 * one shape becomes the other. */
struct thWordToken
{
    enum Kind { WORD, NUMBER, STRING, KNOB, PUNCT, MODSEP, END } kind;

    std::string text;    /* WORD/STRING body/KNOB name, PUNCT char      */
    double      num;
    int         line;
    size_t      off;     /* where the token starts in the source        */
    size_t      end;     /* one past where it stops (quotes included)   */
};

/* The lexical layer in .gen's shape: `out' gets an END-terminated
   token stream with byte spans. False on a lexical error, with `err'
   and `errLine' saying what and where. thcGenEdit and the profile
   reader (thHidBlock.h) share this, which is what keeps "the editor
   and the loader read the same language" true by construction;
   thLexString does the scanning under it, which is what keeps that
   language the same one .dsp is written in.
   `raw', if given, gets thLexString's own stream, which is what .dsp's
   expression reader walks. */
THINK_API bool thWordTokenize (const std::string &text,
                               std::vector<thWordToken> &out,
                               std::string &err, int &errLine,
                               std::vector<thLexToken> *raw = NULL);

/* The one shared pitch parser the format promises: "F3 Ab3 C4" or
   "F3,Ab3,C4" to MIDI numbers. [A-G], optional # or b, octave;
   middle C is C4. On failure `bad' holds the offending token. */
THINK_API bool thParseNoteList (const std::string &text,
                                std::vector<int> &out, std::string &bad);

#endif /* TH_WORDS_H */
