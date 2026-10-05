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

#ifndef TH_ARRAYS_H
#define TH_ARRAYS_H 1

#include <string>
#include <vector>

#include "thLexer.h"

/* Node arrays, written out before the grammar sees them.
 *
 *   node tone[2] filt::svf { in = motor[]->out; cutoff = @tone; };
 *
 * is two nodes, `tone[0]' and `tone[1]', each the block with every `[]'
 * read as its own index: `motor[]' is `motor[0]' in the first and
 * `motor[1]' in the second. A `[k]' names one element anywhere. After a
 * port, the index is the port's number instead -- `ionode->in[]' is
 * `ionode->in0' -- and so is an arg name's: `out[] = tone[]->out_low;'
 * in the io node is one line per channel the io node declares. A `[]' on
 * its own is the index as a number, for what differs between elements:
 * `phase = [] * 0.5;'.
 *
 * Done to the token stream because then nothing after it has to know:
 * units, expressions and folds see ordinary nodes with brackets in their
 * names. The tokens keep their lines and spans, so an error in the third
 * copy of a block points at the block.
 *
 * False, with `why' and `line' set, for a malformed array: a size that is
 * not a whole number from 1 to 64, or a `[]' outside an array block and
 * outside the io node. */
bool thExpandArrays (std::vector<thLexToken> &tokens, std::string &why,
                     int &line);

#endif /* TH_ARRAYS_H */
