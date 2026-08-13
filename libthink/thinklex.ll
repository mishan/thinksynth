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

%{
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yygrammar.h"
#include "think.h"
#include "parser.h"

int linenum = 1;

%}

%%

  /* ignore comments */
\#.*$        { }
\n        { linenum++; }
  /* ignore whitespace */
[ \t]+        { }


  /* units cleared with every number: yylval is one shared struct now, so a
     `ms' left by an earlier token would otherwise still be sitting there. */
[0-9]+(\.([0-9]+)?)? {
  yylval.floatval = atof(yytext);
  yylval.units = NULL;
  return NUMBER;
}

th_max        { yylval.floatval = TH_MAX; yylval.units = NULL; return NUMBER; }
th_min        { yylval.floatval = TH_MIN; yylval.units = NULL; return NUMBER; }
th_range    { yylval.floatval = TH_RANGE; yylval.units = NULL; return NUMBER; }
th_midimax    { yylval.floatval = MIDIVALMAX; yylval.units = NULL; return NUMBER; }
th_sample    { yylval.floatval = TH_SAMPLE; yylval.units = NULL; return NUMBER; }
";"        { return ENDSTATE; }
"="        { return ASSIGN; }

nil        { yylval.floatval = 0; yylval.units = NULL; return NIL; }
node        { return NODE; }
io        { return IO; }
name        { return NAME; }
description    { return DESC; }
author { return AUTHOR; }

ms        { return MS; }

"{"        { return LCBRACK; }
"}"        { return RCBRACK; }

[a-zA-Z][a-zA-Z0-9_]* {
  yylval.str = strdup(yytext);
  return WORD;
}

->        { return INTO; }
::        { return MODSEP; }

"+"        { return ADD; }
"-"        { return SUB; }
"*"        { return MUL; }
"/"        { return DIV; }
"%"        { return MOD; }
"("        { return OPAREN; }
")"        { return CPAREN; }

\.        { return PERIOD; }
"@"        { return ATSIGN; }    /* chan midi arg */
"$"        { return DOLLAR; }  /* note midi arg */

  /* [^"\n]* rather than .* : the greedy version swallowed everything between
     the first and last quote on a line, merging two strings into one. */
\"[^\"\n]*\" {
  size_t len = strlen(yytext) - 2;   /* strip the surrounding quotes */

  yylval.str = (char *)malloc(len + 1);
  memcpy(yylval.str, yytext + 1, len);
  yylval.str[len] = 0;
  return STRING;
}

%%
