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

%top{
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "thLexer.h"

/* Everything one lex carries. `pos' is the byte cursor YY_USER_ACTION
 * advances, `off' is where the match now running started, and `tok' is
 * where an action leaves the token it built for the driver below to
 * collect -- the scanner returns a yes/no, not a grammar's token code,
 * because it no longer answers to one grammar. */
struct thLexExtra
{
    size_t     pos;
    size_t     off;
    thLexToken tok;
};
}

%{

/* Byte spans for every token, comments and whitespace included in the
 * accounting: the offsets are what thcGenEdit splices files by, and what
 * would let NodeEdit stop scanning .dsp text by line. Kept here rather
 * than recomputed by consumers because only the scanner knows how long a
 * match was -- yyleng is the whole of the bookkeeping. */
#define YY_USER_ACTION                          \
    do {                                        \
        yyextra->off = yyextra->pos;            \
        yyextra->pos += (size_t)yyleng;         \
    } while (0);

static void
thLexEmit (thLexExtra *x, thLexToken::Kind kind,
           const char *text, size_t len, int line)
{
    x->tok.kind = kind;
    x->tok.text.assign(text, len);
    x->tok.num  = 0;
    x->tok.line = line;
    x->tok.off  = x->off;
    x->tok.end  = x->pos;
}

%}

/* Reentrant, and no longer a bison bridge: yylval belonged to the .dsp
 * grammar, and this scanner now feeds .gen's recursive descent as well.
 * yylineno replaces the old linenum global and the `think' prefix keeps
 * the generated names to itself. */
%option reentrant
%option yylineno
%option noyywrap
%option nounput noinput
%option prefix="think"
%option extra-type="struct thLexExtra *"

%%

  /* comments to end of line; the newline is left for the rule below to
     count, which is why this is [^\n]* and not the old .*$ */
#[^\n]*        { }

  /* \r joins the set so a CRLF file lexes: the old .dsp scanner had no
     rule for it and fell through to flex's default, which echoed it. */
[ \t\r\n]+     { }

[0-9]+(\.([0-9]+)?)? {
  thLexEmit(yyextra, thLexToken::NUMBER, yytext, yyleng, yylineno);
  yyextra->tok.num = atof(yytext);
  return 1;
}

  /* Identifiers, keywords and the th_ constants all come out as WORD.
     Which words mean something is a question about a language, and the
     two languages answer it differently; see thLexer.h. */
[a-zA-Z][a-zA-Z0-9_]* {
  thLexEmit(yyextra, thLexToken::WORD, yytext, yyleng, yylineno);
  return 1;
}

  /* [^"\n]* rather than .* : the greedy version swallowed everything
     between the first and last quote on a line, merging two strings into
     one. The span keeps the quotes, the text drops them. */
\"[^\"\n]*\" {
  thLexEmit(yyextra, thLexToken::STRING, yytext + 1, yyleng - 2, yylineno);
  return 1;
}

\"[^\"\n]* {
  thLexEmit(yyextra, thLexToken::ERROR, "unterminated string",
            sizeof("unterminated string") - 1, yylineno);
  return 1;
}

  /* Two-character operators first only for readability; flex prefers the
     longest match, so `->' could not be read as `-' followed by `>'
     whatever the order. */
"::"|"->" {
  thLexEmit(yyextra, thLexToken::PUNCT, yytext, yyleng, yylineno);
  return 1;
}

[;={}.@$+*/%()-] {
  thLexEmit(yyextra, thLexToken::PUNCT, yytext, yyleng, yylineno);
  return 1;
}

  /* No default rule. flex's own default echoes what it cannot match to
     stdout and carries on, which is how a stray character in a .dsp used
     to print itself and then be forgotten. */
. {
  std::string msg = std::string("stray character '") + yytext[0] + "'";

  thLexEmit(yyextra, thLexToken::ERROR, msg.data(), msg.size(), yylineno);
  return 1;
}

%%

bool
thLexString (const std::string &text, std::vector<thLexToken> &out)
{
    thLexExtra x;
    yyscan_t   scanner = NULL;
    bool       ok = true;

    out.clear();

    x.pos = 0;
    x.off = 0;

    thinklex_init_extra(&x, &scanner);

    /* Scanning a buffer rather than a FILE* even for .dsp: byte offsets
       into the text as it sits on disk are the whole point, and a
       scanner that refills from a stream cannot hand them out. */
    YY_BUFFER_STATE buf = think_scan_bytes(text.data(), (int)text.size(),
                                           scanner);

    /* yylineno lives in the buffer, and yy_scan_bytes builds its buffer by
       hand instead of going through yy_init_buffer -- so the line count
       starts at whatever was in the malloc'd block, which on a second lex
       is the first lex's leftovers. Every token then reports a line from
       the wrong file. Set it here, where the buffer exists and the reason
       is visible; flex will not do it. */
    thinkset_lineno(1, scanner);

    while (thinklex(scanner))
    {
        out.push_back(x.tok);

        if (x.tok.kind == thLexToken::ERROR)
        {
            ok = false;
            break;
        }
    }

    thLexToken end;

    end.kind = thLexToken::END;
    end.line = thinkget_lineno(scanner);
    end.off  = end.end = text.size();

    out.push_back(end);

    think_delete_buffer(buf, scanner);
    thinklex_destroy(scanner);

    return ok;
}

bool
thLexStream (FILE *input, std::vector<thLexToken> &out)
{
    std::string text;
    char        buf[4096];
    size_t      got;

    while ((got = fread(buf, 1, sizeof(buf), input)) > 0)
        text.append(buf, got);

    return thLexString(text, out);
}
