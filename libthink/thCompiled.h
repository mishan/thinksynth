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

/*
 * thCompiled -- a graph compiled at build time (scripts/dspcompile
 * --engine), run by a tree in place of its nodes, to the same bits.
 *
 * A tree takes the one the registry holds for its fingerprint
 * (thSynthTree::fingerprint), and keeps it if bind() says the tree is one
 * it can run. A voice's tree clones its channel's. It holds no pointer
 * into a tree: render() is handed the tree it runs for, and finds its args
 * there each window, so a clone needs no binding of its own.
 *
 * Which graphs a program has compiled is up to the program: one that adds
 * none runs every graph on its nodes. A graph is compiled from the build's own
 * plugins' kernels, so one run beside plugins from another build may not
 * match them.
 */

#ifndef TH_COMPILED_H
#define TH_COMPILED_H 1

#include <string>

#include "thExport.h"

class thSynthTree;

class THINK_API thCompiled
{
public:
    virtual ~thCompiled (void) {}

    /* GUI thread: a voice's own, where its tree is copied. */
    virtual thCompiled *clone (void) const = 0;

    /* GUI thread, once `tree' is built and on its synth: true if this
       renders what its nodes would, at the synth's rate and raster, and as
       an effect `channels' wide, or 0 for an instrument. */
    virtual bool bind (thSynthTree &tree, int channels) = 0;

    /* As a new voice's, without allocating: a recycled voice, or an effect
       after a stop. */
    virtual void reset (void) = 0;

    /* Audio thread: `n' samples into `tree''s io outputs and its play. */
    virtual void render (thSynthTree &tree, unsigned int n) = 0;
};

typedef thCompiled *(*thCompiledMake) (void);

/* What makes the compiled graph for `fingerprint', replacing any before
   it. Before the trees that would use it are loaded. */
THINK_API void thCompiledAdd (const std::string &fingerprint, thCompiledMake make);

/* A new compiled graph for `fingerprint', or NULL. */
THINK_API thCompiled *thCompiledFor (const std::string &fingerprint);

/* Whether the io arg `name' on `tree' ends, through its pointers, on a
   plugin's output, the voice's own to write. Otherwise it is a number or a
   knob, which the engine reads as it is and nothing writes. */
THINK_API bool thCompiledWrites (thSynthTree &tree, const std::string &name);

/* Adds every graph the build compiled: think_compiled's (scripts/), which
   a program links to have them. */
void thCompiledInstall (void);

#endif /* TH_COMPILED_H */
