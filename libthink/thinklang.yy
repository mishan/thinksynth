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

#include "think.h"
#include "parser.h"

/* The shim yyparse calls and the reporter it reaches errors through;
   bodies live after the grammar, beside thParseDsp. */
static int yylex (YYSTYPE *yylval, thParseContext *ctx);
static void yyerror (thParseContext *ctx, const char *str);

/* False, having complained, if either operand of an arithmetic rule was
   written with a unit. See the ADD rule for the argument. */
static bool thCheckNoUnits (thParseContext *ctx, const char *a, const char *b);
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
%destructor { delete[] $$.str; } plugname fstr

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
    if (!thCheckNoUnits(ctx, $1.units, $3.units))
        YYERROR;

    $$.floatval = $1.floatval + $3.floatval;
    $$.units = NULL;
}
|
term SUB unsigned_simple_expression
{
    if (!thCheckNoUnits(ctx, $1.units, $3.units))
        YYERROR;

    $$.floatval = $1.floatval - $3.floatval;
    $$.units = NULL;
}
;

term:
factor
|
factor MUL term
{
    if (!thCheckNoUnits(ctx, $1.units, $3.units))
        YYERROR;

    $$.floatval = $1.floatval * $3.floatval;
    $$.units = NULL;
}
|
factor DIV term
{
    if (!thCheckNoUnits(ctx, $1.units, $3.units))
        YYERROR;

    $$.floatval = $1.floatval / $3.floatval;
    $$.units = NULL;
}
|
factor MOD term
{
    if (!thCheckNoUnits(ctx, $1.units, $3.units))
        YYERROR;

    $$.floatval = ((int)$1.floatval) % ((int)$3.floatval);
    $$.units = NULL;
}
|
factor MOD /* percentage of TH_MAX  (ex: somearg = 50%) */
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
    $$.floatval = $1.floatval;
    $$.units = "%";
}
|
factor MS /* milliseconds */
{
    $$.floatval = $1.floatval;
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
