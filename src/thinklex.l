%{
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yygrammar.h"
//#include "parser.h"

int linenum = 1;

%}

%%

\#.*$						 /* ignore comments */
\n							 linenum++;
[ \t]+						 /* ignore whitespace */


[0-9]+(\.([0-9]+)?)?          yylval.floatval = atof(yytext); return NUMBER;

\;                           return ENDSTATE;

\=                           return ASSIGN;

nil                            return NIL;
node                         return NODE;
status				return STATUS;

\{                            return LCBRACK;
\}                            return RCBRACK;

[a-zA-Z][a-zA-Z0-9]*           yylval.str = (char*)strdup(yytext); return WORD;

->                             return INTO;
::				return MODSEP;

\+                              return ADD;
\-                              return SUB;
\*                             return MUL;
\/                             return DIV;
\%                             return MOD;
\(                             return OPAREN;
\)                             return CPAREN;

\$				return DOLLAR;

%%