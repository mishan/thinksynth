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

#ifndef NODE_EVOLVE_H
#define NODE_EVOLVE_H 1

/*
 * The search that moves a graph toward a sound: its constants by a
 * separable CMA-ES, its choices raced each against the others, and -- in
 * grow() -- its shape, one NodeMutate edit at a time, each raced in heats
 * and kept only if it wins by a margin.
 *
 * Nothing here renders. A Host says how far a candidate graph is from the
 * target, alone or a generation at a time, what a graph's genes and shape
 * are, where the running commentary goes, and whether to stop: so
 * scripts/dspmatch scores in forked children and prints, and the
 * application scores on a worker thread with a synth of its own and draws
 * a progress bar, and the search is one search. Why each rule is the way it
 * is -- a choice judged after it has been tuned for, an edit by what it
 * wins after its heat -- is argued in scripts/dspmatch.cpp, where it was
 * found.
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "NodeMutate.h"
#include "thSoundFeat.h"

namespace NodeEvolve {

using NodeMutate::Gene;
using NodeMutate::Shape;
using NodeMutate::Splice;

struct Host {
    /* How far `source' is from the target, and the parts of that if
       `parts' is given; DEAD for a graph that will not load. */
    std::function<double (const string &source, thsound::Distance *parts)>
        score;

    /* score() for each of `sources', in `out' -- at once if the host can. */
    std::function<void (const vector<string> &sources, vector<double> &out)>
        scoreAll;

    /* `source''s genes and shape, as NodeMutate::read finds them. */
    std::function<bool (const string &source, vector<Gene> *genes,
                        Shape *shape)> read;

    /* The commentary, a line or part of one at a time. May be empty. */
    std::function<void (const string &text)> say;

    /* Asked between generations and between candidates: true ends the
       search where it stands, with the best it had. May be empty. */
    std::function<bool ()> stop;

    /* Every heat's result in the commentary, not only the finalists'. */
    bool verbose = false;
};

/* A candidate that will not load, or is not a sound, is as far away as
   anything can be. Finite, so a search can still rank two of them. */
const double DEAD = 1e6;

struct Candidate {
    string what;
    string source;
    vector<Gene> genes;
    vector<double> at;
    double score;

    bool operator< (const Candidate &o) const { return score < o.score; }
};

/* `generations' of CMA-ES over `genes' from `mean', in their coordinates;
   choice genes stay where `mean' has them. `label', if given, heads a
   table of the generations in the commentary. The best graph and where it
   was, and how far it is. */
double search (const Host &host, const string &base, const vector<Gene> &genes,
               vector<double> mean, double sigma, std::mt19937 &rng,
               int generations, const char *label, string &bestSource,
               vector<double> &bestAt);

/* Every choice gene's alternatives raced, each tuned for, and then the
   quantities at length around what won. */
double searchAll (const Host &host, const string &base,
                  const vector<Gene> &genes, const vector<double> &start,
                  unsigned int seed, int generations, string &bestSource,
                  vector<double> &bestAt);

/* `generations' more tuning for `c', from where it stands. */
void tune (const Host &host, Candidate &c, std::mt19937 &rng, int generations);

/* `c' as `source' stands, untuned and unscored; false for a graph with no
   genes or that will not parse. `shape', if given, gets where edits go. */
bool candidate (const Host &host, const string &source, const string &what,
                Candidate &c, Shape *shape = NULL);

/* What an edit added to `from' to make `text' -- the constants of nodes
   `from' does not have -- tuned for `generations' toward what `distance'
   measures, from where the edit put them; parsed on `synth'. `text' as it
   is when the edit added no constant, or nothing nearer was found. For a
   host growing toward a target, so an edit is judged as it could sound
   rather than with whatever its plugin's defaults happen to be. */
string tuneAdded (thSynth &synth, const string &from, const string &text,
                  int generations,
                  const std::function<double (const string &)> &distance);

/* The graph itself: up to `rounds' of every single edit raced against the
   graph as it stands, the winner kept if it wins by enough, and then
   searchAll over what is left. 0, with `found', or 2 for a start that
   would not load. */
int grow (const Host &host, const vector<Splice> &pool, const string &start,
          unsigned int seed, int generations, int rounds, string &found);

/* What a search is of: a graph, what it should sound like, and how far to
   go. */
struct Spec {
    string pluginPath;
    string start;           /* the graph to move, as text            */

    /* What it moves toward: a graph's sound, or a recording's -- a WAV
       file, read where the search runs -- whichever is not empty. */
    string targetGraph;
    string targetWav;

    /* The values an instrument sets on each graph, in the engine's
       terms at `rate' -- a time folded to samples is folded at one --
       so the search hears what the piece plays. */
    vector<pair<string, float> > startArgs, targetArgs;
    double rate = TH_DEFAULT_SAMPLES;

    bool grow = false;      /* the graph's shape too, not only its
                               constants                              */
    unsigned int seed = 1;
    int generations = 60;   /* of the closing tune                   */
    int rounds = 2;         /* of edits raced, when growing          */
};

/* How far a graph sounds from what a Spec moves toward, on a synth of its
   own: the target heard once, at the note a recording sounds or middle C,
   and each graph rendered at that note and measured against it. A search
   scores on one per thread; a host with no threads to give it, on one per
   worker of its own (Report::scoreAll). */
class Listener {
public:
    explicit Listener (const Spec &spec);

    /* Another on a synth of its own, hearing what `first' heard. */
    Listener (const Listener &first);

    /* The target heard; false, with `why', if it cannot be. */
    bool open (string &why);

    /* How far `source' is from the target, and the parts of that if
       `parts' is given; DEAD for a graph that will not load or is no
       sound. */
    double distance (const string &source, thsound::Distance *parts = NULL);

    thSynth &synth (void) { return *synth_; }

private:
    bool render (const string &text, const vector<pair<string, float> > &args,
                 vector<float> &mono);

    Spec spec_;
    std::unique_ptr<thSynth> synth_;
    thsound::Extractor ex_;
    thsound::Features target_;
    int at_ = 60;
};

/* What a search says as it goes, to whatever runs it. Each may be empty. */
struct Report {
    std::function<bool ()> stop;                 /* asked between candidates */
    std::function<void (const string &text)> say;    /* the commentary  */
    std::function<void (double distance)> started;   /* where it began  */

    /* Each time a candidate comes nearer than any before it. From any of
       the threads a generation is scored on. */
    std::function<void (double distance, const string &graph)> nearer;

    /* A generation scored by the host, on Listeners of its own, in place
       of this search's threads: the browser's, in workers. */
    std::function<void (const vector<string> &sources, vector<double> &out)>
        scoreAll;
};

struct Outcome {
    bool ok = false;
    string found;
    double distance = 0;
    string why;
};

/* One search on the calling thread, a generation scored on `workers'
   synths at once -- the first here, the rest on threads of their own. One
   worker starts no thread, for a host that has none: the browser. */
Outcome evolve (const Spec &spec, size_t workers, const Report &report);

/* One search, run on a thread of its own, for a host that has a window to
 * keep drawing: Evolve toward, in the Composer. A generation is scored on
 * several synths at once, one per thread -- up to eight, a core spare --
 * so a tune is a minute or two and a grow is several. */
class Job {
public:
    typedef NodeEvolve::Spec Spec;

    Job (void) {}
    ~Job (void);

    /* Runs `spec' on the thread; false, with `why', if it cannot start. */
    bool start (const Spec &spec, string &why);

    /* Ends the search at the next generation, with the best it had. */
    void cancel (void) { stop_ = true; }

    bool done (void) const { return done_; }

    /* The last line of the commentary, and the nearest any candidate has
       come, in dB of thSoundFeat's distance; a negative number for none
       yet. */
    string progress (void) const;
    double best (void) const;

    /* How far the graph as it started was from the target, once known. */
    double started (void) const;

    /* Each time the search came nearer, how many seconds in and how near;
       the graph it started from first. */
    vector<pair<double, double> > trail (void) const;

    /* Seconds since it started, and the nearest graph so far: false for
       none yet. */
    double seconds (void) const;
    bool nearest (string &graph) const;

    /* Once done: the graph it found and how far that is from the target,
       or false with why there is none. */
    bool result (string &graph, double &distance, string &why) const;

private:
    void run (Spec spec);
    void work (const Spec &spec);

    std::thread thread_;
    std::atomic<bool> stop_ { false };
    std::atomic<bool> done_ { false };

    mutable std::mutex lock_;
    string line_, partial_;
    double best_ = -1;
    string bestSource_;
    vector<pair<double, double> > trail_;
    std::chrono::steady_clock::time_point begun_;
    double started_ = -1;
    bool ok_ = false;
    string found_, why_;
    double distance_ = 0;
};

}

#endif /* NODE_EVOLVE_H */
