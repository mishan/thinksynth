/* $Id: thinklex.ll,v 1.23 2004/07/18 21:22:50 ink Exp $ */

%{
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

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
