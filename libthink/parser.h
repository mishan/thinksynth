/* $Id: parser.h,v 1.7 2003/05/03 07:24:40 aaronl Exp $ */

#ifndef PARSER_H
#define PARSER_H

#include "thinklang.hpp"

extern int yylex ();
extern int yyparse(void);
extern int linenum;
extern FILE *yyin;
extern thMod *parsemod;     /* Damn you yacc, I hate globals */
extern thNode *parsenode;
extern thSynth Synth;

#endif /* PARSER_H */
