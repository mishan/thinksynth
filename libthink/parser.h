/* $Id: parser.h,v 1.10 2004/05/25 03:54:04 misha Exp $ */

#ifndef PARSER_H
#define PARSER_H

#include "thinklang.h"

extern int yylex ();
extern int YYPARSE (void);
extern int linenum;
extern FILE *yyin;
extern thMod *parsemod;     /* Damn you yacc, I hate globals */
extern thNode *parsenode;
extern thSynth *Synth;

#endif /* PARSER_H */
