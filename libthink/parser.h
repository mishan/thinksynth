/* $Id: parser.h,v 1.8 2003/05/03 17:51:58 joshk Exp $ */

#ifndef PARSER_H
#define PARSER_H

#include "thinklang.h"

extern int yylex ();
extern int yyparse(void);
extern int linenum;
extern FILE *yyin;
extern thMod *parsemod;     /* Damn you yacc, I hate globals */
extern thNode *parsenode;
extern thSynth Synth;

#endif /* PARSER_H */
