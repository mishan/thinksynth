/* $Id */

%{
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thMidiNote.h"
#include "thMidiChan.h"
#include "thSynth.h"
#include "thUtils.h"

#include "yygrammar.h"
#include "parser.h"

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
io				return IO;
name						return NAME;

\{                            return LCBRACK;
\}                            return RCBRACK;

[a-zA-Z][a-zA-Z0-9]*           yylval.str = thstrdup(yytext); return WORD;

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

\".*\"						   yylval.str = new char[strlen(yytext)-1];memcpy(yylval.str, &yytext[1], strlen(yytext)-2);yylval.str[strlen(yytext)-2]=0;return STRING;

%%
