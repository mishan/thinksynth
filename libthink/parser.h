/* $Id: parser.h,v 1.11 2004/05/25 04:42:47 misha Exp $ */

#ifndef PARSER_H
#define PARSER_H

#include "thinklang.h"

extern int yylex ();
extern int YYPARSE (thSynth *argsynth);
extern int linenum;
extern FILE *yyin;
extern thMod *parsemod;     /* Damn you yacc, I hate globals */
extern thNode *parsenode;

#endif /* PARSER_H */
