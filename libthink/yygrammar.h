/* $Id: yygrammar.h,v 1.3 2003/04/25 07:18:42 joshk Exp $ */

#ifndef YY_GRAMMAR_H
#define YY_GRAMMAR_H 1

typedef union {
  int intval;
  float floatval;
  char *str;
} ATTRIBUTE;

#define YYSTYPE ATTRIBUTE

#endif /* YY_GRAMMAR_H */
