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
   node model -- in the tick, or on threads of its own (GraftWorker). In a
   header of its own so the node model need not know the scheduler, nor
   the scheduler the node model. */

#include <atomic>
#include <list>
#include <memory>
#include <thread>

#include "NodeBreed.h"
#include "thcScheduler.h"

/* What a GraftAsk asks NodeBreed for. */
inline NodeBreed::Params
graftParams (const thcScheduler::GraftAsk &ask)
{
    NodeBreed::Params params;

    params.population = (size_t)std::max(ask.population, 2);
    params.elites = (size_t)std::max(ask.elites, 0);
    params.crossover = ask.crossover;
    params.mutation = ask.mutation;
    params.depth = (size_t)std::max(ask.depth, 0);
    params.kinds = ask.kinds;
    params.tune = ask.tune;

    return params;
}

inline bool
graftStep (thSynth *synth, const std::string &base, const std::string &flock,
           const thcScheduler::GraftAsk &ask,
           const NodeBreed::Params &params, std::string &flockOut,
           std::string &champion, size_t &depth, std::string &why)
{
    return NodeBreed::step(synth, base, flock, params, ask.dice,
                           ask.targetGraph, ask.targetWav, ask.args,
                           ask.targetArgs, ask.rate, flockOut, champion,
                           depth, why);
}

inline thcScheduler::Grafter
nodeBreedGrafter (thSynth *synth)
{
    return [synth] (const std::string &base, const std::string &flock,
                    const thcScheduler::GraftAsk &ask, std::string &flockOut,
                    std::string &champion, size_t &depth, std::string &why)
    {
        return graftStep(synth, base, flock, ask, graftParams(ask), flockOut,
                         champion, depth, why);
    };
}

/* The scheduler's GraftRequest on threads of its own, for a host whose
 * scheduler runs on a thread with a window to draw: each generation
 * asked for is bred on a thread with a synth of its own, and deliver(),
 * called where the scheduler runs, answers the ones that are done.
 * Going away stops the ones under way and waits for them.
 */
class GraftWorker
{
public:
    explicit GraftWorker (const std::string &pluginPath)
        : pluginPath_(pluginPath) {}

    ~GraftWorker (void)
    {
        stop_ = true;

        for (auto &j : jobs_)
            j->thread.join();
    }

    thcScheduler::GraftRequest request (void)
    {
        return [this] (unsigned int ticket, const std::string &base,
                       const std::string &flock,
                       const thcScheduler::GraftAsk &ask)
        {
            Job *j = new Job;

            j->ticket = ticket;
            j->thread = std::thread([this, j, base, flock, ask]
            {
                thSynth synth(pluginPath_, TH_DEFAULT_WINDOW_LENGTH,
                              TH_DEFAULT_SAMPLES);
                NodeBreed::Params params = graftParams(ask);

                params.stop = [this, j]
                              { return stop_.load() || j->stop.load(); };
                j->ok = graftStep(&synth, base, flock, ask, params, j->flock,
                                  j->champion, j->depth, j->why);
                j->done = true;
            });

            jobs_.emplace_back(j);
        };
    }

    /* The generations bred since the last call, answered; and the ones
       the scheduler has stopped waiting for, stopped. A channel has one
       generation out at a time, so the order they finish in is theirs. */
    void deliver (thcScheduler &sched)
    {
        for (auto j = jobs_.begin(); j != jobs_.end(); )
        {
            if (!(*j)->done)
            {
                if (!sched.graftWanted((*j)->ticket))
                    (*j)->stop = true;

                ++j;
                continue;
            }

            std::unique_ptr<Job> done = std::move(*j);

            j = jobs_.erase(j);
            done->thread.join();
            sched.graftAnswer(done->ticket, done->ok, done->flock,
                              done->champion, done->depth, done->why);
        }
    }

    bool busy (void) const { return !jobs_.empty(); }

private:
    struct Job
    {
        unsigned int ticket = 0;
        std::thread thread;
        std::atomic<bool> done { false };
        std::atomic<bool> stop { false };
        bool ok = false;
        std::string flock, champion, why;
        size_t depth = 0;
    };

    std::string pluginPath_;
    std::atomic<bool> stop_ { false };
    std::list<std::unique_ptr<Job> > jobs_;
};

#endif /* NODE_GRAFT_H */
