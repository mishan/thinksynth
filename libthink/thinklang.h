/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_THINKLANG_H_INCLUDED
# define YY_YY_THINKLANG_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    NODE = 258,                    /* NODE  */
    IO = 259,                      /* IO  */
    NAME = 260,                    /* NAME  */
    DESC = 261,                    /* DESC  */
    AUTHOR = 262,                  /* AUTHOR  */
    MS = 263,                      /* MS  */
    WORD = 264,                    /* WORD  */
    FLOAT = 265,                   /* FLOAT  */
    NUMBER = 266,                  /* NUMBER  */
    ENDSTATE = 267,                /* ENDSTATE  */
    ASSIGN = 268,                  /* ASSIGN  */
    LCBRACK = 269,                 /* LCBRACK  */
    RCBRACK = 270,                 /* RCBRACK  */
    INTO = 271,                    /* INTO  */
    MODSEP = 272,                  /* MODSEP  */
    ADD = 273,                     /* ADD  */
    SUB = 274,                     /* SUB  */
    MUL = 275,                     /* MUL  */
    DIV = 276,                     /* DIV  */
    MOD = 277,                     /* MOD  */
    CPAREN = 278,                  /* CPAREN  */
    OPAREN = 279,                  /* OPAREN  */
    NIL = 280,                     /* NIL  */
    PERIOD = 281,                  /* PERIOD  */
    ATSIGN = 282,                  /* ATSIGN  */
    DOLLAR = 283,                  /* DOLLAR  */
    STRING = 284                   /* STRING  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif
/* Token kinds.  */
#define YYEMPTY -2
#define YYEOF 0
#define YYerror 256
#define YYUNDEF 257
#define NODE 258
#define IO 259
#define NAME 260
#define DESC 261
#define AUTHOR 262
#define MS 263
#define WORD 264
#define FLOAT 265
#define NUMBER 266
#define ENDSTATE 267
#define ASSIGN 268
#define LCBRACK 269
#define RCBRACK 270
#define INTO 271
#define MODSEP 272
#define ADD 273
#define SUB 274
#define MUL 275
#define DIV 276
#define MOD 277
#define CPAREN 278
#define OPAREN 279
#define NIL 280
#define PERIOD 281
#define ATSIGN 282
#define DOLLAR 283
#define STRING 284

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef int YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_THINKLANG_H_INCLUDED  */
