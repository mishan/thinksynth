/* $Id: thinklang.yy,v 1.57 2004/07/18 21:22:50 ink Exp $ */

%{
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

thMod *parsemod = NULL;
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

%token NODE IO NAME DESC
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
paramsetup		/* $someparam.min = 20; sort of thing */
|
ionode
|
nameset
|
descset
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

#ifdef USE_DEBUG
	printf("node %s defined using plugin %s\n", $2.str, $3.str);

	printf("Checking if plugin %s is loaded...\n", $3.str);
#endif
	thPluginManager *plugMgr = parseSynth->GetPluginManager();

	if(!plugMgr->GetPlugin($3.str)) {
		if (plugMgr->LoadPlugin($3.str) == 1) { /* FAILED */
			printf ("Error loading required %s, aborting.\n", $3.str);
			exit(1);
		}
	}

	parsenode->SetPlugin(plugMgr->GetPlugin($3.str));

	delete[] $3.str;

	parsenode->SetName($2.str);
	parsemod->NewNode(parsenode);
	parsenode = new thNode("newnode", NULL);		/* add name, plugin */

	free($2.str);
}
|
NODE WORD LCBRACK assignments RCBRACK
{
#ifdef USE_DEBUG
	printf("node %s defined with no plugin\n", $2.str);
#endif

	parsenode->SetName($2.str);
	parsenode->SetPlugin(NULL);
	parsemod->NewNode(parsenode);
	parsenode = new thNode("newnode", NULL);

	free($2.str);
}
;

paramsetup:
ATSIGN WORD ASSIGN expression
{
	printf("Chan Arg %s = %f\n", $2.str, $4.floatval);
	float *copy = new float[1];
	*copy = $4.floatval;
	parsemod->SetChanArg(new thArg($2.str, copy, 1));
}
|
ATSIGN WORD PERIOD WORD ASSIGN expression
{
	thArg *chanarg;

	printf("Arg Name: %s \tData: %s \t=> %f\n", $2.str, $4.str, $6.floatval);

	chanarg = parsemod->GetChanArg($2.str);
	printf("Value: %f\n", chanarg->argValues[0]);

	if(strcmp($4.str, "min") == 0)
	{
		chanarg->SetArgMin($6.floatval);
	}
	else if(strcmp($4.str, "max") == 0)
	{
		chanarg->SetArgMax($6.floatval);
	}
	else if(strcmp($4.str, "widget") == 0)
	{
		chanarg->SetArgWidget((int)$6.floatval);
	}
	else
		printf("ERROR:  Invalid arg parameter '%s <numeric>'\n", $4.str);
}
|
ATSIGN WORD PERIOD WORD ASSIGN STRING
{
	thArg *chanarg;

	printf("Arg Name: %s \tData: %s \t=> %s\n", $2.str, $4.str, $6.str);

	chanarg = parsemod->GetChanArg($2.str);
	printf("Value: %f\n", chanarg->argValues[0]);

	if(strcmp($4.str, "label") == 0)
	{
		chanarg->SetArgLabel($6.str);
	}
	else if(strcmp($4.str, "units") == 0)
	{
		chanarg->SetArgUnits($6.str);
	}
	else
		printf("ERROR:  Invalid arg parameter '%s <string>'\n", $4.str);
}

ionode:
IO WORD
{
	printf("IO node defined as %s\n", $2.str);
	parsemod->SetIONode($2.str);
}
;

nameset:
NAME STRING
{
	parsemod->SetName($2.str);
	free($2.str);
	printf("DSP Name: %s\n", parsemod->GetName().c_str());
}
;

descset:
DESC STRING
{
	parsemod->SetDesc($2.str);
	free($2.str);
	printf("DSP Description: %s\n", parsemod->GetDesc().c_str());
};

assignments:
|
assignments assignment ENDSTATE
;

assignment:
WORD ASSIGN expression
{
//	XXX - MORE FIXING
//	modify_num(&targs, (char *)$1.str, $3.floatval);

	float *copy = new float[1];
	*copy = $3.floatval;
	parsenode->SetArg($1.str, copy, 1)->SetIndex(-1); /* XXX: This is sorta
										hackish, make it not index it here */
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
	parsenode->SetArg($1.str, node, arg)->SetIndex(-1); /* XXX: This is sorta
										hackish, make it not index it here */
	delete[] node;
	delete[] arg;
	delete[] $3.str;
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
	delete $3.str;
	free($1.str);
}
;

fstr:		/* a node name and an fstring */
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
