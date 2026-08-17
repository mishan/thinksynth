/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

#ifndef PARSER_H
#define PARSER_H

#include <stdio.h>

#include "thinklang.h"

class thSynth;
class thSynthTree;
class thNode;

/* Everything one parse carries, where yacc used to keep globals.
 *
 * The old spelling -- extern parsetree/parsenode/yyin, callers assigning
 * them under a mutex before calling in -- is why the parser could not
 * run twice at once, why every caller had to know the loading ritual,
 * and why a parse that stopped early could poison the next one through
 * flex's retained buffer. All of it is this struct now, one per parse,
 * on the stack of thParseDsp. */
struct thParseContext
{
    thSynth     *synth;      /* for resolving plugins                    */
    thSynthTree *tree;       /* built up by the grammar actions          */
    thNode      *node;       /* the node currently being assembled       */
    void        *scanner;    /* the reentrant lexer's yyscan_t           */
};

/* One parse of `input', which the caller opens and closes. Returns
 * yyparse()'s result; *treeOut receives the tree either way -- possibly
 * partial on failure -- and the caller consumes it (thSynth::finishParse
 * is the consumer that knows what a finished tree still needs).
 *
 * Reentrant: a scanner and a context per call, no globals, so two
 * parses may run at once and a failed parse leaves nothing behind for
 * the next one to trip over. */
extern int thParseDsp (thSynth *synth, FILE *input, thSynthTree **treeOut);

#endif /* PARSER_H */
