/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

#include "think.h"
#include "yygrammar.h"
#include "parser.h"

thSynthTree *parsetree = NULL;
thNode *parsenode = NULL;
static thSynth *parseSynth;

// XXX - REIMPLEMENT GLOBAL STUFFS
extern int linenum;

int yyparse (void);

int YYPARSE (thSynth *argsynth)
{
    parseSynth = argsynth;

    linenum = 1;

    return yyparse();
}

void yyerror (const char *str)
{
    fprintf(stderr, "line %d: error: %s\n", linenum, str);
}

extern "C" int yywrap(void)
{
    return 1;
}
%}

%token NODE IO NAME DESC AUTHOR
%token MS
%token WORD 
%token FLOAT NUMBER
%token ENDSTATE ASSIGN LCBRACK RCBRACK
%token INTO
%token MODSEP
%token ADD SUB MUL DIV MOD CPAREN OPAREN NIL
%token PERIOD
%token ATSIGN DOLLAR
%token STRING

%%

statements:
|
statements statement ENDSTATE
;

statement:
nodes
|
nodes nodes
{
    yyerror("missing semicolon after node\n");
    YYERROR;
}
|
paramsetup        /* $someparam.min = 20; sort of thing */
|
ionode
|
nameset
|
descset
|
authset
|
expression
{
    printf("%f\n", $1.floatval);
}
;

expression:
simple_expression
;

simple_expression:
unsigned_simple_expression
{
    $$.floatval = $1.floatval;
}
|
SUB unsigned_simple_expression
{
    $$.floatval = $2.floatval*-1;
}
;

unsigned_simple_expression:
term
{
    $$.floatval = $1.floatval;
}
|
term ADD unsigned_simple_expression
{
    $$.floatval = $1.floatval + $3.floatval;
}
|
term SUB unsigned_simple_expression
{
    $$.floatval = $1.floatval - $3.floatval;
}
;

term:
factor
|
factor MUL term
{
    $$.floatval = $1.floatval * $3.floatval;
}
|
factor DIV term
{
    $$.floatval = $1.floatval / $3.floatval;
}
|
factor MOD term
{
    $$.floatval = ((int)$1.floatval) % ((int)$3.floatval);
}
|
factor MOD /* percentage of TH_MAX  (ex: somearg = 50%) */
{
    $$.floatval = $1.floatval * TH_MAX / 100;
}
|
factor MS /* milliseconds */
{
    $$.floatval = $1.floatval * TH_SAMPLE / 1000;
}
;

factor:
OPAREN expression CPAREN
{
    $$.floatval = $2.floatval;
}
|
unsigned_constant
{
    $$.floatval = $1.floatval;
}
;

unsigned_constant:
NUMBER
{
    $$.floatval = $1.floatval;
}
|
NIL
;

nodes:
NODE WORD plugname LCBRACK assignments RCBRACK
{
    thPluginManager *plugMgr = parseSynth->getPluginManager();

    if (!plugMgr->getPlugin($3.str)) {
        if (plugMgr->loadPlugin($3.str) == 1) { /* FAILED */
            printf ("Error loading required %s, aborting.\n", $3.str);
            exit(1);
        }
    }

    parsenode->setPlugin(plugMgr->getPlugin($3.str));
    parsenode->setName($2.str);

    parsetree->newNode(parsenode, true);
    parsenode = new thNode("newnode", NULL);        /* add name, plugin */

    free($2.str);
    free($3.str);
}
|
NODE WORD LCBRACK assignments RCBRACK
{
    parsenode->setName($2.str);
    parsenode->setPlugin(NULL);

    parsetree->newNode(parsenode, true);
    parsenode = new thNode("newnode", NULL);

    free($2.str);
}
;

paramsetup:
ATSIGN WORD ASSIGN expression
{
    parsetree->setChanArg(new thArg($2.str, $4.floatval));
}
|
ATSIGN WORD PERIOD WORD ASSIGN expression
{
    thArg *chanarg;

    chanarg = parsetree->getChanArg($2.str);

    if (strcmp($4.str, "min") == 0)
    {
        chanarg->setMin($6.floatval);
    }
    else if (strcmp($4.str, "max") == 0)
    {
        chanarg->setMax($6.floatval);
    }
    else if (strcmp($4.str, "widget") == 0)
    {
        chanarg->setWidgetType((thArg::WidgetType)$6.floatval);
    }
    else
        printf("ERROR:  Invalid arg parameter '%s <numeric>'\n", $4.str);
}
|
ATSIGN WORD PERIOD WORD ASSIGN STRING
{
    thArg *chanarg;

    chanarg = parsetree->getChanArg($2.str);

    if (strcmp($4.str, "label") == 0)
    {
        chanarg->setLabel($6.str);
    }
    else if (strcmp($4.str, "units") == 0)
    {
        chanarg->setUnits($6.str);
    }
    else
        printf("ERROR:  Invalid arg parameter '%s <string>'\n", $4.str);
}

ionode:
IO WORD
{
    parsetree->setIONode($2.str);
}
;

authset:
AUTHOR STRING
{
    thArg *autharg = new thArg("author", NULL, 0);
    autharg->setComment($2.str);

    parsetree->setChanArg(autharg);
}
;

nameset:
NAME STRING
{
    thArg *namearg = new thArg("name", NULL, 0);
    namearg->setComment($2.str);

    parsetree->setChanArg(namearg);
}
;

descset:
DESC STRING
{
    thArg *descarg = new thArg("desc", NULL, 0);
    descarg->setComment($2.str);

    parsetree->setChanArg(descarg);
};

assignments:
|
assignments assignment ENDSTATE
;

assignment:
WORD ASSIGN expression
{
    /* XXX: This is sorta hackish, make it not index it here */
    parsenode->setArg($1.str, $3.floatval)->setIndex(-1);

    free($1.str);
}
|
WORD ASSIGN fstr
{
    char *node, *arg, *p;
    int argsize, nodesize;
    
    /* Make $3.str ("node/arg" format) into the above vars */
    p = strchr($3.str, '/');
    p++;
    
    argsize = strlen(p);
    nodesize = strlen($3.str)-argsize-1;
    
    node = new char[nodesize+1];
    memcpy(node, $3.str, nodesize);
    node[nodesize] = 0;

    arg = new char[argsize+1];
    memcpy(arg, p, argsize);
    arg[argsize] = 0;

    /* XXX: This is sorta hackish, make it not index it here */
    parsenode->setArg($1.str, node, arg)->setIndex(-1);

    delete[] node;
    delete[] arg;
    delete[] $3.str;
    free($1.str);
}
|
WORD ASSIGN ATSIGN WORD
{
    char *chanarg;
    int chanarglen;

    chanarglen = strlen($4.str);
    chanarg = new char[chanarglen + 1];        /* +1 for the terminating '\0' */
    memcpy(chanarg, $4.str, chanarglen + 1);

    parsenode->setArg($1.str, chanarg)->setIndex(-1); /* XXX: This is sorta
                                        hackish, make it not index it here */
}
;

plugname:
WORD MODSEP WORD
{
    $$.str = new char[strlen($1.str) + strlen($3.str) + 2];
    sprintf((char *)$$.str, "%s/%s", $1.str, $3.str);
    free($1.str);
    free($3.str);
}
|
WORD MODSEP plugname
{
    $$.str = new char[strlen($1.str) + strlen($3.str) + 2];
    sprintf((char *)$$.str, "%s/%s", $1.str, $3.str);
    delete $3.str;
    free($1.str);
}
;

fstr:        /* a node name and an fstring */
WORD INTO WORD
{
    /* we must allocate two extra bytes; one for the '/' and one for the null
       terminator */
    $$.str = new char[strlen($1.str) + strlen($3.str) + 2];
    sprintf((char *)$$.str, "%s/%s", $1.str, $3.str);
    free($1.str);
    free($3.str);
}
;
%%
