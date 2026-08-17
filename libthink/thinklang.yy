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

#include "think.h"
#include "parser.h"
#include "thinklex.h"   /* the reentrant lexer: thinklex(), _init, _destroy */

/* The shim yyparse calls and the reporter it reaches errors through;
   bodies live after the grammar, beside thParseDsp. */
static int yylex (YYSTYPE *yylval, thParseContext *ctx);
static void yyerror (thParseContext *ctx, const char *str);
%}

/* Pure: no globals anywhere in the generated parser, a context threaded
 * through yyparse, yylex and yyerror instead. This is step one of the
 * .gen/.dsp reconciliation plan (COMPOSITION_HANDOFF.md paragraph 8) and
 * worth having regardless: the old parser could not run twice at once,
 * and every caller had to know the global-assignment ritual. */
%define api.pure full
%param { thParseContext *ctx }

%code requires {
#include "yygrammar.h"

struct thParseContext;
typedef struct thParseContext thParseContext;
}

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
    yyerror(ctx, "missing semicolon after node\n");
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
    $$.units = $1.units;
}
|
SUB unsigned_simple_expression
{
    $$.floatval = $2.floatval*-1;
    $$.units = $2.units;    /* -5 ms is still milliseconds */
}
;

unsigned_simple_expression:
term
{
    $$.floatval = $1.floatval;
    $$.units = $1.units;
}
|
term ADD unsigned_simple_expression
{
    $$.floatval = $1.floatval + $3.floatval;

    /* Cleared rather than guessed. `5 ms + 3' has no unit this grammar can
       name, and calling the result milliseconds because the left side was
       would put a wrong unit on a right number. No shipped .dsp does
       arithmetic on a united value; if one ever does, it gets a bare
       number, which is what it had before any of this. */
    $$.units = NULL;
}
|
term SUB unsigned_simple_expression
{
    $$.floatval = $1.floatval - $3.floatval;
    $$.units = NULL;
}
;

term:
factor
|
factor MUL term
{
    $$.floatval = $1.floatval * $3.floatval;
    $$.units = NULL;
}
|
factor DIV term
{
    $$.floatval = $1.floatval / $3.floatval;
    $$.units = NULL;
}
|
factor MOD term
{
    $$.floatval = ((int)$1.floatval) % ((int)$3.floatval);
    $$.units = NULL;
}
|
factor MOD /* percentage of TH_MAX  (ex: somearg = 50%) */
{
    $$.floatval = $1.floatval * TH_MAX / 100;

    /* The fold is exactly invertible, so remembering what was folded is
       enough to show the number back the way it was written. */
    $$.units = "%";
}
|
factor MS /* milliseconds */
{
    $$.floatval = $1.floatval * TH_SAMPLE / 1000;
    $$.units = "ms";
}
;

factor:
OPAREN expression CPAREN
{
    $$.floatval = $2.floatval;
    $$.units = $2.units;
}
|
unsigned_constant
{
    $$.floatval = $1.floatval;
    $$.units = $1.units;
}
;

unsigned_constant:
NUMBER
{
    $$.floatval = $1.floatval;
}
|
NIL
{
    /* This had no action, so $$ kept whatever the lexer last left in yylval --
       an arbitrary float propagated into node args. */
    $$.floatval = 0;
    $$.units = NULL;
}
;

nodes:
NODE WORD plugname LCBRACK assignments RCBRACK
{
    thPluginManager *plugMgr = ctx->synth->getPluginManager();

    /* One call rather than get-then-load-then-get. The old spelling was a
       check-then-act, and with parseTree no longer holding the synth mutex
       two parses of a file naming the same unloaded plugin could both miss
       and both load it. getOrLoadPlugin settles it under one lock. */
    thPlugin *plug = plugMgr->getOrLoadPlugin($3.str);

    if (plug == NULL) {
        /* This used to exit(1) -- a library taking the whole host process
           down because one .dsp referenced a plugin that would not load.
           Fail the parse instead; loadTree() discards the tree. */
        char errbuf[256];

        snprintf(errbuf, sizeof(errbuf),
                 "could not load plugin '%s' required by node '%s'",
                 $3.str, $2.str);
        yyerror(ctx, errbuf);

        free($2.str);        /* WORD comes from the lexer's strdup() */
        delete[] $3.str;     /* plugname is built with new char[] */

        YYERROR;
    }

    ctx->node->setPlugin(plug);
    ctx->node->setName($2.str);

    ctx->tree->newNode(ctx->node, true);
    ctx->node = new thNode("newnode", NULL);        /* add name, plugin */

    free($2.str);
    /* was free() -- plugname allocates with new char[], so this was an
       allocator mismatch that corrupted the heap on every node with a plugin. */
    delete[] $3.str;
}
|
NODE WORD LCBRACK assignments RCBRACK
{
    ctx->node->setName($2.str);
    ctx->node->setPlugin(NULL);

    ctx->tree->newNode(ctx->node, true);
    ctx->node = new thNode("newnode", NULL);

    free($2.str);
}
;

paramsetup:
ATSIGN WORD ASSIGN expression
{
    thArg *chanarg = new thArg($2.str, $4.floatval);

    /* `@a = 5 ms' is stored as 220.5 samples, which is what the engine wants
       and what every consumer of this value has always got. Recording that it
       was written in milliseconds costs nothing and lets a display show it
       back the way it was written. An explicit `@a.units = "Hz"' later still
       overrides this -- the author knows better than the fold does. */
    if ($4.units)
        chanarg->setUnits($4.units);

    ctx->tree->setChanArg(chanarg);

    free($2.str);
}
|
ATSIGN WORD PERIOD WORD ASSIGN expression
{
    thArg *chanarg;

    /* `@foo.min = 0' before any `@foo = ...' has no arg to modify. This used
       to hand back a NULL (inserted by map::operator[]) and dereference it. */
    chanarg = ctx->tree->getChanArg($2.str);

    if (chanarg == NULL)
    {
        printf("ERROR:  '@%s.%s' set before '@%s' was declared; ignoring\n",
               $2.str, $4.str, $2.str);
    }
    else if (strcmp($4.str, "min") == 0)
    {
        chanarg->setMin($6.floatval);

        /* `@a.max = 2000ms' says as much about the arg as `@a = 5 ms' does,
           and some patches give the unit only on the range. */
        if ($6.units && chanarg->units().empty())
            chanarg->setUnits($6.units);
    }
    else if (strcmp($4.str, "max") == 0)
    {
        chanarg->setMax($6.floatval);

        if ($6.units && chanarg->units().empty())
            chanarg->setUnits($6.units);
    }
    else if (strcmp($4.str, "widget") == 0)
    {
        chanarg->setWidgetType((thArg::WidgetType)$6.floatval);
    }
    else if (strcmp($4.str, "step") == 0)
    {
        /* `@x.step = 1' says this control means a whole number. Normally the
           plugin reading it says so and typeChanArgs() carries that along the
           wire, which is why no shipped file needs this line; it is here for a
           control the plugin cannot know about, and for overriding one that
           gets it wrong.

           `true' marks it as the author's, so typeChanArgs() leaves it alone.
           That is what lets `@x.step = 0' mean "continuous, and I mean it"
           rather than being indistinguishable from having said nothing. */
        chanarg->setStep($6.floatval, true);
    }
    else
        printf("ERROR:  Invalid arg parameter '%s <numeric>'\n", $4.str);

    free($2.str);
    free($4.str);
}
|
ATSIGN WORD PERIOD WORD ASSIGN STRING
{
    thArg *chanarg;

    chanarg = ctx->tree->getChanArg($2.str);

    if (chanarg == NULL)
    {
        printf("ERROR:  '@%s.%s' set before '@%s' was declared; ignoring\n",
               $2.str, $4.str, $2.str);
    }
    else if (strcmp($4.str, "label") == 0)
    {
        chanarg->setLabel($6.str);
    }
    else if (strcmp($4.str, "units") == 0)
    {
        chanarg->setUnits($6.str);
    }
    else if (strcmp($4.str, "group") == 0)
    {
        /* Presentation only. An editor draws a group's controls together;
           the engine never looks at it. */
        chanarg->setGroup($6.str);
    }
    else if (strcmp($4.str, "values") == 0)
    {
        /* `@x.values = "Sine,Sawtooth,Square"' -- names for the whole numbers
           this control selects between, which implies `.step = 1' and a range
           of 0..count-1. The author's word, so typeChanArgs() will not
           overwrite it. */
        chanarg->setValueNames(string($6.str), true);
    }
    else
        printf("ERROR:  Invalid arg parameter '%s <string>'\n", $4.str);

    free($2.str);
    free($4.str);
    free($6.str);
}
;

ionode:
IO WORD
{
    ctx->tree->setIONode($2.str);

    free($2.str);
}
;

authset:
AUTHOR STRING
{
    thArg *autharg = new thArg("author", NULL, 0);
    autharg->setComment($2.str);

    ctx->tree->setChanArg(autharg);

    free($2.str);
}
;

nameset:
NAME STRING
{
    thArg *namearg = new thArg("name", NULL, 0);
    namearg->setComment($2.str);

    ctx->tree->setChanArg(namearg);

    /* setName() rather than only stashing a comment: the tree's name is what
       treelist_ is keyed on, and it was never actually set from `name "..."'. */
    ctx->tree->setName($2.str);

    free($2.str);
}
;

descset:
DESC STRING
{
    thArg *descarg = new thArg("desc", NULL, 0);
    descarg->setComment($2.str);

    ctx->tree->setChanArg(descarg);
    ctx->tree->setDesc($2.str);

    free($2.str);
};

assignments:
|
assignments assignment ENDSTATE
;

assignment:
WORD ASSIGN expression
{
    /* XXX: This is sorta hackish, make it not index it here */
    ctx->node->setArg($1.str, $3.floatval)->setIndex(-1);

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
    ctx->node->setArg($1.str, node, arg)->setIndex(-1);

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

    ctx->node->setArg($1.str, chanarg)->setIndex(-1); /* XXX: This is sorta
                                        hackish, make it not index it here */

    /* setArg() takes a const string& and copies, so none of these outlive the
       call. All three used to leak, once per chanarg reference per .dsp. */
    delete[] chanarg;
    free($1.str);
    free($4.str);
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
    delete[] $3.str;   /* was `delete' on a new char[] allocation */
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

static int yylex (YYSTYPE *yylval, thParseContext *ctx)
{
    return thinklex(yylval, (yyscan_t)ctx->scanner);
}

static void yyerror (thParseContext *ctx, const char *str)
{
    fprintf(stderr, "line %d: error: %s\n",
            thinkget_lineno((yyscan_t)ctx->scanner), str);
}

/* The whole loading ritual, in the one place that should know it. The
 * old entry point had callers assigning yyin, parsetree and parsenode
 * under a mutex, and needed yyrestart() because flex's retained buffer
 * let a parse that stopped early feed its unread tail to the next one,
 * which then failed at "line 1" for no visible reason. A scanner per
 * parse makes that hazard unconstructible rather than handled. */
int thParseDsp (thSynth *synth, FILE *input, thSynthTree **treeOut)
{
    /* Checked rather than assumed. Every caller in the tree passes all
       three, but this is the entry point an out-of-tree consumer of
       libthink reaches the language through, and the failure modes are a
       null dereference for treeOut and a parse of nothing for input. A
       nonzero return with *treeOut left NULL is a shape finishParse already
       handles -- it is what a parse that failed on line one looks like. */
    if (treeOut == NULL)
        return 1;

    *treeOut = NULL;

    if (synth == NULL || input == NULL)
        return 1;

    /* Before the tree is built, so a scanner that could not be made costs
       nothing to clean up. thinklex_init allocates, and flex's own
       yy_fatal_error is the alternative to noticing here. */
    yyscan_t scanner = NULL;

    if (thinklex_init(&scanner) != 0 || scanner == NULL)
    {
        fprintf(stderr, "error: could not create a scanner\n");
        return 1;
    }

    thParseContext ctx;

    ctx.synth = synth;
    ctx.tree = new thSynthTree("newmod", synth);
    ctx.node = new thNode("newnode", NULL);

    thinkset_in(input, scanner);
    ctx.scanner = scanner;

    int result = yyparse(&ctx);

    thinklex_destroy(scanner);
    delete ctx.node;

    *treeOut = ctx.tree;
    return result;
}
