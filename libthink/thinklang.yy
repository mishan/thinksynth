%{
/*
#include "config.h"
#include "structs.h"
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
#include "thnodes.h"
#include "mods.h"
#include "itree.h"
#include "thplug.h"
#include "main.h"
*/

#include "thArg.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"

#include "yygrammar.h"
#include "parser.h"

thMod *parsemod;
thNode *parsenode;

// XX - REIMPLEMENT GLOBAL STUFFS
//modnode *parsemod = NULL;
//static linked_list *targs = NULL;

void yyerror (const char *str)
{
	fprintf(stderr, "line %d: error: %s\n", linenum, str);
}

int yywrap(void)
{
	return 1;
}
%}

%token NODE STATUS
%token WORD 
%token FLOAT NUMBER
%token ENDSTATE ASSIGN LCBRACK RCBRACK
%token INTO
%token MODSEP
%token ADD SUB MUL DIV MOD CPAREN OPAREN NIL
%token DOLLAR

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
status
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

/*output:
OUTPUT fstr fstr
{
	printf("outputting left %s and right %s\n", $2.str, $3.str);

	build_tree(node_find(parsemod->nodelist,strtok($2.str,"/")));
	build_tree(node_find(parsemod->nodelist,strtok($3.str,"/")));

	leftroot = node_find(parsemod->nodelist,strtok($2.str,"/"));
	rightroot = node_find(parsemod->nodelist,strtok($3.str,"/"));
}
;
*/

nodes:
NODE WORD plugname LCBRACK assignments RCBRACK
{
	printf("node %s defined using plugin %s\n", $2.str, $3.str);

/*
	XXX  --  FIX THIS SHIT
*/

/*	if(plug_isloaded(plist, $3.str) == 0) {
		loadplug(&plist, $3.str);
	}
	add_thnode(parsemod,$2.str,&targs,$3.str);*/
//	args_deinit(&targs);
//	targs=NULL;

	parsenode->SetName($2.str);
	parsemod->NewNode(parsenode);
	parsenode = new thNode("newnode", NULL);		/* add name, plugin */
}
;

status:
STATUS WORD
{
	printf("Status node defined as %s\n", $2.str);
//	XXX - - FIX THIS TOO
//	parsemod->statusnode = node_find(parsemod->nodelist, $2.str);
}
;

assignments:
|
assignments assignment ENDSTATE
;

assignment:
WORD ASSIGN expression
{
//	XXX - MORE FIXING
//	modify_num(&targs, (char *)$1.str, $3.floatval);

	parsenode->SetArg($1.str, &$3.floatval, 1);
}
|
WORD ASSIGN nodearg
{
//	XXX - MORE FIXING
//	modify_point(&targs, $1.str, $3.str);

	char *node, *arg;
	
	/* XXX Make $3.str ("node/arg" format) into the above vars */

	parsenode->SetArg($1.str, node, arg);
}
|
WORD ASSIGN WORD
{
//	XXX - MORE FIXING
//	modify_str(&targs, $1.str, $3.str);
}
|
WORD ASSIGN fstr
{
//	XXX - MORE FIXING
//	modify_fstring(&targs, $1.str, $3.str);
}
;

plugname:
WORD MODSEP WORD
{
	sprintf((char *)$$.str, "%s/%s", $1.str, $3.str);
}
|
WORD MODSEP plugname
{
	sprintf((char *)$$.str, "%s/%s", $1.str, $3.str);
}
;

nodearg:
WORD INTO numpoint
{
	sprintf((char *)$$.str, "%s/%s", $1.str, $3.str);
}
;

fstr:		/* a node name and an fstring */
WORD INTO WORD
{
	sprintf((char *)$$.str, "%s/%s", $1.str, $3.str);
}
;

numpoint:	/* pointer to a number which is not a string of floats */
DOLLAR WORD
{
	$$.str = $2.str;
}
;
%%
