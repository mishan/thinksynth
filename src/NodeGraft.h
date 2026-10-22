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

#ifndef NODE_GRAFT_H
#define NODE_GRAFT_H 1

/* The scheduler's Grafter as NodeBreed breeds, for a host that links the
   node model: each generation parsed on `synth' and heard on synths of its
   own. In a header of its own so the node model need not know the
   scheduler, nor the scheduler the node model. */

#include "NodeBreed.h"
#include "thcScheduler.h"

inline thcScheduler::Grafter
nodeBreedGrafter (thSynth *synth)
{
    return [synth] (const std::string &base, const std::string &flock,
                    const thcScheduler::GraftAsk &ask, std::string &flockOut,
                    std::string &champion, size_t &depth, std::string &why)
    {
        NodeBreed::Params params;

        params.population = (size_t)std::max(ask.population, 2);
        params.elites = (size_t)std::max(ask.elites, 0);
        params.crossover = ask.crossover;
        params.mutation = ask.mutation;
        params.depth = (size_t)std::max(ask.depth, 0);
        params.kinds = ask.kinds;
        params.tune = ask.tune;

        return NodeBreed::step(synth, base, flock, params, ask.dice,
                               ask.targetGraph, ask.targetWav, ask.args,
                               ask.targetArgs, ask.rate, flockOut, champion,
                               depth, why);
    };
}

#endif /* NODE_GRAFT_H */
