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

#include <string>
#include <vector>

#include "think.h"
#include "parser.h"

/* The shim yyparse calls and the reporter it reaches errors through;
   bodies live after the grammar, beside thParseDsp. */
static int yylex (YYSTYPE *yylval, thParseContext *ctx);
static void yyerror (thParseContext *ctx, const char *str);

/* False, having complained, if either operand of an arithmetic rule was
   written with a unit. See the ADD rule for the argument. */
static bool thCheckNoUnits (thParseContext *ctx, const char *a, const char *b);

/* One arithmetic rule: folds two numbers as this grammar always has, and
   builds a tree the moment either side carries one. Frees both operands and
   returns false on a refusal, because bison does not reclaim the symbols of
   the rule whose action raised YYERROR. */
static bool thArith (thParseContext *ctx, YYSTYPE *out, int op,
                     const YYSTYPE *a, const YYSTYPE *b);

/* `pow(a, b)'. Same contract: false having freed everything, `name'
   included. */
static bool thCall (thParseContext *ctx, YYSTYPE *out, char *name,
                    const YYSTYPE *argv, int argc);

/* The operand as an expression node -- the tree it already had, or a fresh
   constant. Takes over ownership of v->expr. */
static thExprNode *thOperand (const YYSTYPE *v);
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
%token ADD SUB MUL DIV MOD CPAREN OPAREN COMMA NIL
%token PERIOD
%token ATSIGN DOLLAR
%token STRING

/* Precedence, declared for exactly one ambiguity.
 *
 * `%' is the one token this grammar spells two things with: a modulo between
 * two numbers, and a percentage after one. Now that a `-' can begin a factor,
 * `50% - 3' is two readings -- the percentage and a subtraction, or 50 modulo
 * -3 -- and bison has to be told which. PERCENT is declared after SUB and so
 * binds tighter, which makes the `factor MOD' rule reduce rather than shift
 * into `SUB factor'. `50% - 3' stays what it has always been.
 *
 * `%precedence' rather than `%nonassoc': neither token is ever asked to
 * associate with itself, only to outrank the other.
 *
 * Nothing else in this grammar resolves by precedence: every other operator
 * groups by the shape of its rule. A future conflict on SUB would be settled
 * here silently rather than reported, which is the cost of the line. */
%precedence SUB
%precedence PERCENT

/* Who frees a string when a parse gives up partway.
 *
 * A completed rule frees what it consumed, which covers every successful
 * parse -- but YYERROR unwinds the stack, and everything already on it is
 * simply dropped. That was survivable while the only YYERRORs were at the
 * top level, where the stack below them is empty; a unit inside arithmetic
 * fails from *inside* a node body, with the node's name, its plugin's
 * name and the arg's name all still on the stack, and LeakSanitizer said
 * so at once.
 *
 * Destructors are bison's answer and they are the right one, because the
 * alternative is every future YYERROR remembering to free the whole path
 * back to the top. They cannot double-free what an action already freed:
 * only symbols bison *discards* are destroyed, and a symbol a reduction
 * consumed has been popped already.
 *
 * With one exception worth knowing before writing the next YYERROR.
 * Bison does not reclaim the symbols of the rule whose own action raised
 * it -- the assumption being that an action that decided to fail knows
 * what it was holding. So a rule that YYERRORs still frees its own RHS by
 * hand, and the destructors cover everything below it. The plugin-load
 * failure below is the one rule in this grammar that has to.
 *
 * WORD and STRING come from the lexer's strdup; plugname and fstr are
 * built with new char[]. Two allocators, two destructors, which is also
 * a reminder that this grammar has never settled on one. */
%destructor { free($$.str); }   WORD STRING
%destructor { delete[] $$.str; } plugname

/* An expression the stack was still holding when YYERROR unwound past it.
   NULL for every all-constant expression, which is every one in the corpus,
   so this frees nothing until a file writes arithmetic over a signal. */
%destructor { thExprFree($$.expr); }
    expression unsigned_simple_expression term factor
    unsigned_constant

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
    /* A bare expression statement, which only ever printed its value. A
       signal has no value to print at parse time and no arg to drive, so
       there is nothing to say about one. */
    if ($1.expr == NULL)
        printf("%f\n", $1.floatval);

    thExprFree($1.expr);
}
;

expression:
unsigned_simple_expression
;

unsigned_simple_expression:
term
{
    $$.floatval = $1.floatval;
    $$.units = $1.units;
    $$.expr = $1.expr;
}
|
term ADD unsigned_simple_expression
{
    /* A unit in an arithmetic expression is refused rather than guessed.
     *
     * `5 ms + 3' has no unit this grammar can name, and it used to get a
     * number anyway: the fold happened at the leaf, so the left side was
     * already 220.5 samples and the right side was 3 of whatever, and the
     * sum was 223.5 samples by accident rather than by anyone's decision.
     * Now that the fold waits for load time there is no accident left to
     * have -- the unit would simply be dropped and `5 ms + 3' would mean 8
     * samples, which is worse than an error. Nothing in the corpus does
     * this; the rule is here so nothing quietly starts. */
    if (!thArith(ctx, &$$, '+', &$1, &$3))
        YYERROR;
}
|
term SUB unsigned_simple_expression
{
    if (!thArith(ctx, &$$, '-', &$1, &$3))
        YYERROR;
}
;

term:
factor
|
factor MUL term
{
    if (!thArith(ctx, &$$, '*', &$1, &$3))
        YYERROR;
}
|
factor DIV term
{
    if (!thArith(ctx, &$$, '/', &$1, &$3))
        YYERROR;
}
|
factor MOD term
{
    if (!thArith(ctx, &$$, '%', &$1, &$3))
        YYERROR;
}
|
factor MOD %prec PERCENT /* percentage of TH_MAX  (ex: somearg = 50%) */
{
    /* The literal, not the fold.
     *
     * Both of these rules used to convert here -- `50%' to 0.5 and `5 ms'
     * to 220.5 -- and the `ms' one did it with the compile-time TH_SAMPLE,
     * which is a rate the parser has no business knowing and, at
     * `thinksynth -r 48000', is not the rate anything is running at. So
     * the value keeps the author's number and carries its unit up through
     * the expression rules, and whoever stores it parks a fold for
     * thSynthTree::foldUnits to do at load time. The unit is still
     * recorded afterwards, because the fold is exactly invertible and
     * remembering what was folded is what lets a panel show the number
     * back the way it was written. */
    if ($1.expr)
    {
        yyerror(ctx, "a unit cannot be written on a signal");
        thExprFree($1.expr);
        YYERROR;
    }

    $$.floatval = $1.floatval;
    $$.units = "%";
    $$.expr = NULL;
}
|
factor MS /* milliseconds */
{
    if ($1.expr)
    {
        yyerror(ctx, "a unit cannot be written on a signal");
        thExprFree($1.expr);
        YYERROR;
    }

    $$.floatval = $1.floatval;
    $$.units = "ms";
    $$.expr = NULL;
}
;

factor:
OPAREN expression CPAREN
{
    $$.floatval = $2.floatval;
    $$.units = $2.units;
    $$.expr = $2.expr;
}
|
unsigned_constant
{
    $$.floatval = $1.floatval;
    $$.units = $1.units;
    $$.expr = NULL;
}
|
SUB factor
{
    /* `-x'. A number negates as it always did; a signal becomes a mul by
     * -1, which is the node the desugar already has.
     *
     * A `factor' rather than the top of an expression, which is where this
     * rule used to be. There it scoped over everything to its right, so
     * `-0.4 + 0.1' was `-(0.4 + 0.1)' and came out -0.5 -- while .gen's
     * parser, which binds the sign to its operand, read the same text as
     * -0.3. That rule also made a negative literal unwritable anywhere but
     * the front of an expression: `a->out * -0.5' was a syntax error here,
     * and is what ebb.gen writes. Nothing in the corpus wrote a leading
     * minus over a sum, which is why moving it costs no shipped file; the
     * two languages now group one expression one way.
     *
     * The unit rides along: `-5 ms' is a factor of -5 that the MS rule
     * below then marks, and `-(5 ms)' a parenthesised factor that carries
     * one in already. */
    if ($2.expr == NULL)
    {
        $$.floatval = $2.floatval*-1;
        $$.units = $2.units;
        $$.expr = NULL;
    }
    else
    {
        /* A unit on a signal is refused by the MS and MOD rules below, so
           $2.units is NULL here whenever $2.expr is not. */
        $$.floatval = 0;
        $$.units = NULL;
        $$.expr = thExprOp('*', $2.expr, thExprConst(-1));
    }
}
|
WORD INTO WORD
{
    /* The same leaf `freq = osc->out' has always been, now that it can also
       appear under an operator. A bare one still becomes an ARG_POINTER and
       no node -- see the assignment rule. */
    $$.floatval = 0;
    $$.units = NULL;
    $$.expr = thExprNodeRef($1.str, $3.str);

    free($1.str);
    free($3.str);
}
|
ATSIGN WORD
{
    $$.floatval = 0;
    $$.units = NULL;
    $$.expr = thExprChanRef($2.str);

    free($2.str);
}
|
WORD OPAREN expression CPAREN
{
    YYSTYPE argv[1];

    argv[0] = $3;

    if (!thCall(ctx, &$$, $1.str, argv, 1))
        YYERROR;
}
|
WORD OPAREN expression COMMA expression CPAREN
{
    YYSTYPE argv[2];

    argv[0] = $3;
    argv[1] = $5;

    if (!thCall(ctx, &$$, $1.str, argv, 2))
        YYERROR;
}
|
WORD OPAREN expression COMMA expression COMMA expression CPAREN
{
    YYSTYPE argv[3];

    argv[0] = $3;
    argv[1] = $5;
    argv[2] = $7;

    if (!thCall(ctx, &$$, $1.str, argv, 3))
        YYERROR;
}
;

unsigned_constant:
NUMBER
{
    $$.floatval = $1.floatval;
    $$.expr = NULL;
}
|
NIL
{
    /* This had no action, so $$ kept whatever the lexer last left in yylval --
       an arbitrary float propagated into node args. */
    $$.floatval = 0;
    $$.units = NULL;
    $$.expr = NULL;
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

        /* Still freed by hand, destructors or not. Bison does not reclaim
           the symbols of the rule whose action raised YYERROR -- it
           assumes the action dealt with them, which is the only sane
           assumption when the action is what decided to fail. The
           destructors take over one frame further down. */
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
    /* A control is a constant the GUI writes into, and nothing drives one:
       thMidiChan copies the declared value into every voice and a slider
       overwrites it. An expression here would be a value with two authors. */
    if ($4.expr)
    {
        yyerror(ctx, "a control cannot be driven by an expression");
        thExprFree($4.expr);
        free($2.str);
        YYERROR;
    }

    thArg *chanarg = new thArg($2.str, $4.floatval);

    /* `@a = 5 ms' ends up stored as samples, which is what the engine wants
       and what every consumer of this value has always got -- but the fold
       waits for foldUnits, which knows the rate. Recording that it was
       written in milliseconds costs nothing and lets a display show it back
       the way it was written. An explicit `@a.units = "Hz"' later still
       overrides this -- the author knows better than the fold does. */
    if ($4.units)
        chanarg->setUnits($4.units);

    ctx->tree->setChanArg(chanarg);

    /* The fold is parked against an arg the tree owns, which is what
       setChanArg above has just made true -- and it matters because
       ownership is what decides the arg's lifetime. Declaring `@a' twice
       makes the second declaration replace the first, and setChanArg
       deletes the arg it replaces; a record still aimed at that arg would
       be aimed at freed memory by the time foldUnits ran, so setChanArg
       drops those on its way past. Written in this order so the two halves
       read together; either order works, because the sweep is looking for
       the *old* arg and this record names the new one. */
    if ($4.units)
        ctx->tree->deferUnitFold(chanarg, thUnitFold::VALUE, $4.floatval,
                                 $4.units);

    free($2.str);
}
|
ATSIGN WORD PERIOD WORD ASSIGN expression
{
    thArg *chanarg;

    if ($6.expr)
    {
        yyerror(ctx, "a control's range cannot be driven by an expression");
        thExprFree($6.expr);
        free($2.str);
        free($4.str);
        YYERROR;
    }

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

        /* Parked per site, not per arg. A file may write the value in
           milliseconds and the range in samples -- `@decay = 500 ms' and
           `@decay.max = 88200' -- and folding the whole arg by its unit
           would convert a number that was already converted. */
        if ($6.units)
            ctx->tree->deferUnitFold(chanarg, thUnitFold::MIN, $6.floatval,
                                     $6.units);
    }
    else if (strcmp($4.str, "max") == 0)
    {
        chanarg->setMax($6.floatval);

        if ($6.units && chanarg->units().empty())
            chanarg->setUnits($6.units);

        if ($6.units)
            ctx->tree->deferUnitFold(chanarg, thUnitFold::MAX, $6.floatval,
                                     $6.units);
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

        if ($6.units)
            ctx->tree->deferUnitFold(chanarg, thUnitFold::STEP, $6.floatval,
                                     $6.units);
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
    /* One rule for all four right-hand sides a .dsp can write.
     *
     * `in = osc->out' and `in = @cut' used to be rules of their own, and
     * could not be: a `node->arg' that may appear under an operator is a
     * `factor', and two rules deriving `WORD ASSIGN WORD INTO WORD' are the
     * same sentence twice. So the expression grammar reads all of them and
     * this sorts out what came back.
     *
     * The first three branches are exactly what the old rules did, and a
     * file that writes no arithmetic takes them for every line it has. Only
     * the fourth is new, and only it makes nodes. */
    ctx->tree->dropExpr(ctx->node, $1.str);

    if ($3.expr == NULL)
    {
        /* XXX: This is sorta hackish, make it not index it here */
        thArg *arg = ctx->node->setArg($1.str, $3.floatval);

        arg->setIndex(-1);

        /* A node arg remembers its unit now, which it never used to: only
           chanargs did, because only chanargs were ever drawn. The arg holds
           the author's number until foldUnits runs, so anything that reads a
           tree between the parse and finishParse sees milliseconds -- nothing
           does, and saying so is cheaper than pretending the value is already
           in samples. */
        if ($3.units)
        {
            arg->setUnits($3.units);
            ctx->tree->deferUnitFold(arg, thUnitFold::VALUE, $3.floatval,
                                     $3.units);
        }
    }
    else if ($3.expr->kind == thExprNode::NODEREF)
    {
        ctx->node->setArg($1.str, $3.expr->node, $3.expr->arg)->setIndex(-1);
        thExprFree($3.expr);
    }
    else if ($3.expr->kind == thExprNode::CHANREF)
    {
        ctx->node->setArg($1.str, $3.expr->name)->setIndex(-1);
        thExprFree($3.expr);
    }
    else
    {
        /* Against the node rather than its name: the body reduces before the
           `node osc osc::simple {' around it does, so ctx->node has no name
           yet. See thPendingExpr. */
        ctx->tree->deferExpr(ctx->node, $1.str, $3.expr);
    }

    free($1.str);
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

%%

/* .dsp's vocabulary.
 *
 * The shared lexer (thLexer.h) hands back every identifier as a WORD and
 * every operator as its spelling, because which words and marks mean
 * something is a question about a *language*, and there are two of them
 * reading its output now. `ms' is a keyword here and an ordinary unit
 * name in .gen; `beats' is the reverse. So the vocabulary lives with the
 * grammar that has it, and the lexical layer stays one thing.
 *
 * This costs a string compare per token against a table of thirteen
 * words, on files of a few hundred lines, at load time. It buys a lexer
 * that cannot drift from the one .gen reads. */
static int
yylex (YYSTYPE *yylval, thParseContext *ctx)
{
    /* units cleared with every token: yylval is one shared struct, so a
       `ms' left by an earlier number would otherwise still be sitting
       there when a later rule read it. */
    yylval->units = NULL;
    yylval->floatval = 0;
    yylval->str = NULL;
    yylval->expr = NULL;

    const thLexToken &t = ctx->tokens[ctx->pos];

    if (t.kind == thLexToken::END)
        return 0;

    ctx->pos++;

    switch (t.kind)
    {
    case thLexToken::NUMBER:
        yylval->floatval = (float)t.num;
        return NUMBER;

    case thLexToken::STRING:
        /* strdup because the grammar actions free() what they are given;
           the token itself outlives the parse and must not be stolen. */
        yylval->str = strdup(t.text.c_str());
        return STRING;

    case thLexToken::WORD:
    {
        const std::string &w = t.text;

        /* The named constants. NUMBER tokens with a value the engine
           supplies, which is why they are keywords rather than something
           a .dsp could shadow. */
        if (w == "th_max")      { yylval->floatval = TH_MAX;     return NUMBER; }
        if (w == "th_min")      { yylval->floatval = TH_MIN;     return NUMBER; }
        if (w == "th_range")    { yylval->floatval = TH_RANGE;   return NUMBER; }
        if (w == "th_midimax")  { yylval->floatval = MIDIVALMAX; return NUMBER; }
        if (w == "th_sample")   { yylval->floatval = TH_SAMPLE;  return NUMBER; }

        if (w == "nil")         return NIL;
        if (w == "node")        return NODE;
        if (w == "io")          return IO;
        if (w == "name")        return NAME;
        if (w == "description") return DESC;
        if (w == "author")      return AUTHOR;
        if (w == "ms")          return MS;

        yylval->str = strdup(w.c_str());
        return WORD;
    }

    case thLexToken::PUNCT:
    {
        const std::string &p = t.text;

        if (p == ";")  return ENDSTATE;
        if (p == "=")  return ASSIGN;
        if (p == "{")  return LCBRACK;
        if (p == "}")  return RCBRACK;
        if (p == "->") return INTO;
        if (p == "::") return MODSEP;
        if (p == "+")  return ADD;
        if (p == "-")  return SUB;
        if (p == "*")  return MUL;
        if (p == "/")  return DIV;
        if (p == "%")  return MOD;
        if (p == "(")  return OPAREN;
        if (p == ")")  return CPAREN;
        if (p == ".")  return PERIOD;
        if (p == ",")  return COMMA;
        if (p == "@")  return ATSIGN;
        if (p == "$")  return DOLLAR;

        break;
    }

    default:
        break;
    }

    /* Unreachable: thParseDsp refuses to start on a stream that carries
       an ERROR, and every other kind is spoken for above. Ending the
       parse rather than asserting, because a lexer that grew a token
       this grammar has no word for should fail a file, not the process. */
    return 0;
}

static bool
thCheckNoUnits (thParseContext *ctx, const char *a, const char *b)
{
    const char *unit = a ? a : b;

    if (unit == NULL)
        return true;

    char msg[128];

    snprintf(msg, sizeof(msg),
             "'%s' cannot be used in arithmetic; write the number the "
             "engine wants, or the unit on its own", unit);
    yyerror(ctx, msg);

    return false;
}

static thExprNode *
thOperand (const YYSTYPE *v)
{
    return v->expr ? v->expr : thExprConst(v->floatval);
}

static bool
thArith (thParseContext *ctx, YYSTYPE *out, int op,
         const YYSTYPE *a, const YYSTYPE *b)
{
    out->floatval = 0;
    out->units = NULL;
    out->expr = NULL;

    if (!thCheckNoUnits(ctx, a->units, b->units))
    {
        thExprFree(a->expr);
        thExprFree(b->expr);

        return false;
    }

    /* Two numbers fold, to the same bits they always did -- including
       `1 / 0', which this grammar has always let through as an infinity and
       which the non-finite guard reports at the voice that reaches it. */
    if (a->expr == NULL && b->expr == NULL)
    {
        /* `7 % 0' is an integer division by zero, which is a SIGFPE and not
           a number -- `amp = 7 % 0;' in any .dsp took the whole process down
           with it, the editor included. `/' has an answer for a zero
           denominator and this has none, so it is refused against the line
           rather than given one. */
        if (op == '%' && (int)b->floatval == 0)
        {
            yyerror(ctx, "'%' by zero has no value");

            return false;
        }

        switch (op)
        {
        case '+': out->floatval = a->floatval + b->floatval; break;
        case '-': out->floatval = a->floatval - b->floatval; break;
        case '*': out->floatval = a->floatval * b->floatval; break;
        case '/': out->floatval = a->floatval / b->floatval; break;
        case '%':
            out->floatval = (float)(((int)a->floatval) % ((int)b->floatval));
            break;
        }

        return true;
    }

    /* Every other operator has a math:: node behind it and this one does
       not. Saying so beats desugaring to something that is nearly a modulo. */
    if (op == '%')
    {
        yyerror(ctx, "'%' takes two numbers; there is no node for a modulo "
                     "over signals");
        thExprFree(a->expr);
        thExprFree(b->expr);

        return false;
    }

    out->expr = thExprOp(op, thOperand(a), thOperand(b));

    return out->expr != NULL;
}

static bool
thCall (thParseContext *ctx, YYSTYPE *out, char *name,
        const YYSTYPE *argv, int argc)
{
    std::vector<thExprNode *> kids;
    std::string why;
    bool ok = true;

    out->floatval = 0;
    out->units = NULL;
    out->expr = NULL;

    for (int i = 0; i < argc; i++)
    {
        if (argv[i].units && !thCheckNoUnits(ctx, argv[i].units, NULL))
            ok = false;

        kids.push_back(thOperand(&argv[i]));
    }

    if (ok)
        out->expr = thExprCall(name, kids, why);

    /* thExprCall empties `kids' whether it succeeds or not; this is the
       units path, which never reached it. */
    for (size_t i = 0; i < kids.size(); i++)
        thExprFree(kids[i]);

    if (ok && out->expr == NULL)
        yyerror(ctx, why.c_str());

    free(name);

    if (out->expr == NULL)
        return false;

    /* `exp2(2)' is 4 and no node, for the same reason `5 * 2' is 10 and no
       node: a function over constants is a constant. */
    if (out->expr->kind == thExprNode::CONST)
    {
        out->floatval = out->expr->value;
        thExprFree(out->expr);
        out->expr = NULL;
    }

    return true;
}

static void yyerror (thParseContext *ctx, const char *str)
{
    /* The token that was read, not the one about to be: yyparse reports
       after taking the lookahead, so pos_ is already one past the thing
       the author got wrong. */
    size_t at = ctx->pos ? ctx->pos - 1 : 0;

    fprintf(stderr, "line %d: error: %s\n", ctx->tokens[at].line, str);
}

/* The whole loading ritual, in the one place that should know it. The
 * old entry point had callers assigning yyin, parsetree and parsenode
 * under a mutex, and needed yyrestart() because flex's retained buffer
 * let a parse that stopped early feed its unread tail to the next one,
 * which then failed at "line 1" for no visible reason. A context per
 * parse makes that hazard unconstructible rather than handled. */
int thParseDsp (thSynth *synth, FILE *input, thSynthTree **treeOut)
{
    /* Checked rather than assumed. Every caller in the tree passes both,
       but this is the entry point an out-of-tree consumer of libthink
       reaches the language through, and the failure modes are a null
       dereference for treeOut and a parse of nothing for input. A nonzero
       return with *treeOut left NULL is a shape finishParse already
       handles -- it is what a parse that failed on line one looks like. */
    if (treeOut == NULL)
        return 1;

    *treeOut = NULL;

    if (synth == NULL || input == NULL)
        return 1;

    thParseContext ctx;

    ctx.synth = synth;
    ctx.tree = new thSynthTree("newmod", synth);
    ctx.node = new thNode("newnode", NULL);
    ctx.pos = 0;

    int result = 0;

    if (!thLexStream(input, ctx.tokens))
    {
        /* A lexical error stops the file here rather than being echoed
           to stdout and forgotten, which is what flex's default rule did
           with anything the old scanner had no pattern for. A character
           the language cannot spell is a broken file, and a broken file
           should not half-build a tree. */
        const thLexToken *bad = thLexError(ctx.tokens);

        fprintf(stderr, "line %d: error: %s\n",
                bad ? bad->line : 0,
                bad ? bad->text.c_str() : "lexical error");

        result = 1;
    }
    else
        result = yyparse(&ctx);

    delete ctx.node;

    *treeOut = ctx.tree;
    return result;
}
