/*
 * Copyright (C) 2004-2014 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef YY_GRAMMAR_H
#define YY_GRAMMAR_H 1

/* A struct, not a union.
 *
 * It was a union, which is the right shape when a token carries exactly one
 * of these. But a number can carry a unit as well as a value: the grammar
 * folds `5 ms' into 220.5 samples, and until this field existed the `ms' was
 * thrown away at that moment. Nothing downstream could tell 220.5 samples
 * from 220.5 of anything else, so the parameter panel showed envelope times
 * as raw sample counts -- `288000.0312' where the file said `7000 ms'.
 *
 * The cost is a few bytes per token on the parser stack, which is nothing
 * against a .dsp of a few hundred lines. Every existing use of .floatval and
 * .str stays exactly as it was; they simply no longer overlap. */
typedef struct {
  int intval;
  float floatval;
  char *str;

  /* "ms", "%", or NULL. Set only where the grammar folds a unit away, and
     propagated through the rules that pass a value along unchanged.
     Arithmetic clears it: the unit of `5 ms + 3' is not a question this
     grammar can answer, so it declines to guess. */
  const char *units;
} ATTRIBUTE;

#define YYSTYPE ATTRIBUTE

#endif /* YY_GRAMMAR_H */
