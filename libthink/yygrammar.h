/* $Id */

#ifndef YY_GRAMMAR_H
#define YY_GRAMMAR_H 1

typedef union {
  int intval;
  float floatval;
  char *str;
} ATTRIBUTE;

#define YYSTYPE ATTRIBUTE

#endif /* YY_GRAMMAR_H */
