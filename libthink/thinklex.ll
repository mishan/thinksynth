/* $Id: thinklex.ll,v 1.27 2004/10/01 09:14:29 misha Exp $ */
/*
 * Copyright (C) 2004 Metaphonic Labs
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
\#.*$		{ }
\n		{ linenum++; }
  /* ignore whitespace */
[ \t]+		{ }


[0-9]+(\.([0-9]+)?)? {
  yylval.floatval = atof(yytext);
  return NUMBER;
}

th_max		{ yylval.floatval = TH_MAX; return NUMBER; }
th_min		{ yylval.floatval = TH_MIN; return NUMBER; }
th_range	{ yylval.floatval = TH_RANGE; return NUMBER; }
th_midimax	{ yylval.floatval = MIDIVALMAX; return NUMBER; }
th_sample	{ yylval.floatval = TH_SAMPLE; return NUMBER; }
";"		{ return ENDSTATE; }
"="		{ return ASSIGN; }

nil		{ return NIL; }
node		{ return NODE; }
io		{ return IO; }
name		{ return NAME; }
description	{ return DESC; }
author { return AUTHOR; }

ms		{ return MS; }

"{"		{ return LCBRACK; }
"}"		{ return RCBRACK; }

[a-zA-Z][a-zA-Z0-9_]* {
  yylval.str = strdup(yytext);
  return WORD;
}

->		{ return INTO; }
::		{ return MODSEP; }

"+"		{ return ADD; }
"-"		{ return SUB; }
"*"		{ return MUL; }
"/"		{ return DIV; }
"%"		{ return MOD; }
"("		{ return OPAREN; }
")"		{ return CPAREN; }

\.		{ return PERIOD; }
"@"		{ return ATSIGN; }	/* chan midi arg */
"$"		{ return DOLLAR; }  /* note midi arg */

\".*\" {
  yylval.str = (char *)malloc(strlen(yytext) - 1);
  memcpy(yylval.str, yytext + 1, strlen(yytext)-2);
  yylval.str[strlen(yytext) - 2] = 0;
  return STRING;
}

%%
