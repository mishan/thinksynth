/* $Id: parser.h,v 1.9 2004/04/22 08:47:20 misha Exp $ */

#ifndef PARSER_H
#define PARSER_H

#include "thinklang.h"

extern int yylex ();
extern int YYPARSE (void);
extern int linenum;
extern FILE *yyin;
extern thMod *parsemod;     /* Damn you yacc, I hate globals */
extern thNode *parsenode;
extern thSynth Synth;

#endif /* PARSER_H */
