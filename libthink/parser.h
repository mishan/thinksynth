#ifndef PARSER_H
#define PARSER_H

#include "thinklang.h"

extern int yylex ();
extern int yyparse(void);
extern int linenum;
extern FILE *yyin;

#endif /* PARSER_H */
