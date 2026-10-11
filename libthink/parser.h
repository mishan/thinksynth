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

#ifndef PARSER_H
#define PARSER_H

#include <stdio.h>

#include <string>

class thSynth;
class thSynthTree;

/* One parse of `input', which the caller opens and closes. Returns
 * 0 on success; *treeOut receives the tree either way -- possibly
 * partial on failure -- and the caller consumes it (thSynth::finishParse
 * is the consumer that knows what a finished tree still needs).
 *
 * Reentrant: no globals, so two
 * parses may run at once and a failed parse leaves nothing behind for
 * the next one to trip over. */
extern int thParseDsp (thSynth *synth, FILE *input, thSynthTree **treeOut);

/* The same over text already in memory -- a graph a host carries inside
 * itself rather than as a file beside it. */
extern int thParseDspText (thSynth *synth, const std::string &text,
                           thSynthTree **treeOut);

#endif /* PARSER_H */
