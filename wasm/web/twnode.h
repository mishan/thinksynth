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

#ifndef TW_NODE_H
#define TW_NODE_H 1

/*
 * The one thing thinkweb.cpp wants out of thinknode.cpp.
 *
 * The two are separate translation units in one module because they share
 * nothing: one is the scheduler and the synth that sounds, the other is the
 * node editor's model over a document's text, and thinknode.cpp's header
 * says at length why its synth is its own and silent.
 *
 * The parameter panels are the exception, and a small one. There is one
 * tw_panel_ family for every panel the page draws, and one of the four is
 * over a node of the graph -- so the family, which lives with the rest of
 * the page's ABI in thinkweb.cpp, has to be able to ask which graph that is.
 * A function rather than a reference to the object, so the ownership stays
 * exactly where it was.
 */

class NodeGraph;

/* The graph thinknode.cpp last built, or NULL before the first
   tw_graph_build. Valid until the next one, which replaces every box. */
const NodeGraph *twGraph (void);

#endif /* TW_NODE_H */
