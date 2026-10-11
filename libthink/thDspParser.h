/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#ifndef TH_DSP_PARSER_H
#define TH_DSP_PARSER_H 1

#include <functional>
#include <string>
#include <vector>

#include "thExport.h"
#include "thLexer.h"

struct thExprNode;

/* A value as the expression rules read it: a number with the unit it was
 * written in, or a tree once a leaf is a signal, in which case `units' is
 * NULL. The caller owns `expr'. */
struct thDspValue
{
    float        num;
    const char  *units;
    thExprNode  *expr;

    thDspValue (void) : num(0), units(NULL), expr(NULL) { }
};

typedef std::function<void (int line, const std::string &msg)> thDspErrorFn;

/* Asked about each `@name' as it is read. False refuses the expression,
 * having said why through the error callback. */
typedef std::function<bool (int line, const std::string &name)>
    thDspControlFn;

/* One expression from `tokens', starting at `pos', by .dsp's own rules --
 * operators, grouping, units, functions and every refusal -- for a language
 * that writes the same arithmetic: .gen. On success `pos' is one past the
 * expression. .dsp's statement keywords are ordinary names here, so a .gen
 * stage may still be called `name'.
 *
 * False having reported through `error', with nothing left in `out' to
 * free. */
THINK_API bool thDspExpression (const std::vector<thLexToken> &tokens,
                                size_t &pos, const thDspErrorFn &error,
                                const thDspControlFn &control,
                                thDspValue &out);

#endif /* TH_DSP_PARSER_H */
