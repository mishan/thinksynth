#ifndef PARSER_H
#define PARSER_H

#include "thinklang.h"

extern int yylex ();
extern int yyparse(void);
extern int linenum;
extern FILE *yyin;
extern thMod *parsemod;     /* Damn you yacc, I hate globals */
extern thNode *parsenode;
extern thSynth *Synth;

#endif /* PARSER_H */
