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

#ifndef THLEXER_H
#define THLEXER_H 1

#include <stdio.h>

#include <string>
#include <vector>

#include "thExport.h"

/* The thinklang lexical layer, on its own, for everyone who reads a
 * thinklang-shaped file.
 *
 * There are two such readers and there will not be a third: the bison
 * grammar in thinklang.yy, which builds a thSynthTree out of `.dsp', and
 * thcGenLoader's recursive descent in src/, which builds scheduler chains
 * out of `.gen'. They were written years apart and the second one
 * reproduced the first one's lexer by hand -- faithfully, and with the
 * usual consequence: two copies of one thing, free to drift apart the
 * next time either language grows a token. This header is that thing,
 * once, and the drift is now unconstructible rather than merely unlikely.
 *
 * What is shared is *lexical* and nothing more: comments, whitespace,
 * numbers, quoted strings, identifiers, operators, line numbers and byte
 * spans. Vocabulary is not shared, because it genuinely differs -- `ms'
 * is a keyword in .dsp and `beats' is one in .gen, and neither language
 * should have to know the other's words. Every identifier comes out of
 * here as a WORD carrying its text, and each consumer decides which
 * words it has opinions about. That division is what lets one lexer
 * serve two languages without becoming a lexer with a dialect switch.
 *
 * Every token carries its byte span, which is what thcGenEdit splices
 * files with: replace exactly the span an edit is aimed at and whatever
 * lies between spans -- comments, indentation, the author's blank lines
 * -- is untouched by construction. .dsp gets those spans now too, which
 * is what NodeEdit would need to retire its line-based scanning.
 */
struct thLexToken
{
    enum Kind
    {
        END,        /* one of these ends every stream, failed or not     */
        WORD,       /* identifier or keyword; `text' says which          */
        NUMBER,     /* `num' is the value, `text' the literal as written */
        STRING,     /* `text' is the body, the span includes the quotes  */
        PUNCT,      /* `text' is the spelling: ";", "::", "->", "@" ...  */
        ERROR       /* `text' is the complaint, in prose                 */
    };

    thLexToken (void)
        : kind(END), num(0), line(0), off(0), end(0) { }

    Kind        kind;
    std::string text;
    double      num;
    int         line;
    size_t      off;    /* where the token starts in the source          */
    size_t      end;    /* one past where it stops                       */
};

/* Lexes `text' into `out', which is cleared first and always ends with an
 * END token. False if a token could not be formed, in which case the
 * token before that END is an ERROR saying what and where -- the stream
 * carries its own failure, so a consumer walking it hits the problem in
 * the same loop it reads everything else in.
 *
 * Lexing stops at the first error. There is no resynchronization here on
 * purpose: what a lexical error means is a question about a language,
 * and the languages disagree (a .gen file with any error loads nothing;
 * a .dsp parse error names a line and gives up). The lexer reports and
 * stops; the caller decides what that costs.
 *
 * The shape holds even when the scan could not start -- a scanner or a
 * buffer that could not be allocated comes back as an ERROR and an END
 * like anything else. flex's own answer to that is yy_fatal_error, which
 * prints and exits; a library does not get to take the host process down
 * over a file it was handed. */
THINK_API bool thLexString (const std::string &text,
                            std::vector<thLexToken> &out);

/* The same, reading `input' to EOF first. The caller opens and closes it.
 *
 * **Open it in binary mode.** The spans handed back are byte offsets into
 * the file as it sits on disk, and text mode on Windows collapses each
 * CRLF into one byte on the way in -- so every offset past the first line
 * would name the wrong bytes, and an editor splicing by span would cut a
 * file in the wrong place. Nothing here can detect that after the fact, so
 * the mode is part of the contract; thcGenEdit reads binary for exactly
 * this reason and says so.
 *
 * A read that fails partway is reported rather than mistaken for end of
 * file. A truncated buffer parses as a syntax error on whichever line the
 * read stopped at, which sends the author to a line that is fine. */
THINK_API bool thLexStream (FILE *input, std::vector<thLexToken> &out);

/* The ERROR token of a failed lex, or NULL if there was not one. Inline
 * because it is a search over a vector the caller already owns, not
 * anything the lexer has state about. */
inline const thLexToken *
thLexError (const std::vector<thLexToken> &tokens)
{
    for (size_t i = 0; i < tokens.size(); i++)
        if (tokens[i].kind == thLexToken::ERROR)
            return &tokens[i];

    return NULL;
}

#endif /* THLEXER_H */
