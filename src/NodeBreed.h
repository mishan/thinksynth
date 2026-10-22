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

#ifndef NODE_BREED_H
#define NODE_BREED_H 1

/*
 * A population of instruments, bred.
 *
 * Every member descends from the graph a piece declares and is held as
 * its lineage (NodeMutate::Op), so a child of two is their lists crossed
 * and made again on the declared graph. A generation scores the members,
 * names the best the champion -- what a host puts on the channel -- and
 * breeds the next from them: the best few carried, the rest each from a
 * tournament, crossed with a second parent or not, and given one more
 * edit or not, or one fewer at the depth allowed. A child that moves the
 * level as growth would not is bred again.
 *
 * Scored against a target, a member is how far it sounds from it, and an
 * edit is tuned toward it before it is judged. With none, it is how far it
 * sounds from the champion just played, up to a point: the instrument
 * keeps changing, as breed's drift keeps a timbre moving.
 *
 * Everything a generation draws is from its dice and every render is
 * deterministic, so a host that hands it the same dice breeds the same
 * flock. The flock is text (save/load), for a host to keep as it keeps
 * any state and to drop on a rewind. Nothing here plays anything.
 */

#include <functional>
#include <string>
#include <vector>

#include "NodeMutate.h"
#include "think.h"

class thSynth;

namespace NodeBreed {

using NodeMutate::Op;

struct Params {
    size_t population = 6;
    size_t elites = 1;          /* carried as they are                   */
    double crossover = 0.7;     /* the chance a child has two parents    */
    double mutation = 0.8;      /* the chance it takes one edit more     */
    size_t depth = 6;           /* edits on the declared graph, at most  */
    string kinds;               /* as gen::grow's; empty for all         */
    int tune = 6;               /* generations an edit is tuned toward a
                                   target before it is judged            */
};

/* The renders a generation judges by, the host's. */
struct Ear {
    /* As NodeMutate::level: the phrase's RMS and peak. */
    std::function<bool (const string &text, double &rms, double &peak)>
        level;

    /* How far `text' sounds from the target, in dB; empty for none. */
    std::function<double (const string &text)> target;

    /* How far `text' sounds from `from', in dB: what novelty is. */
    std::function<double (const string &text, const string &from)> apart;
};

typedef vector<pair<string, float> > Values;

/* An Ear on synths of its own, from `pluginPath', for a flock bred on
   `base': the target a graph's text or a sound file's path, whichever is
   not empty, or neither. A member is heard with `values' set on it, and a
   target graph with `targetValues', in the terms of a synth at `rate'. */
Ear localEar (const string &pluginPath, const string &base,
              const string &targetGraph, const string &targetWav,
              const Values &values = Values(),
              const Values &targetValues = Values(),
              double rate = TH_DEFAULT_SAMPLES);

/* Novelty is rewarded this far and no further, in dB: past it a member
   is new enough, and what decides is the dice. */
const double NOVELTY_DB = 6;

struct Member {
    vector<Op> ops;
    double near = -1;           /* from the target, once heard           */
};

struct Flock {
    vector<Member> members;
    unsigned int generations = 0;
    string played;              /* the champion last named               */
};

string save (const Flock &flock);
bool load (const string &text, Flock &flock);

/* One generation of `flock' on `base', the declared graph, parsed on
   `synth': scored, its champion's text in `champion' and how many edits
   it is from `base' in `depth', and the next bred from `dice'. A flock
   with no members starts as `base' and mutants of it. False, with `why',
   for a base that does not parse or makes no sound. */
bool generation (thSynth *synth, const string &base, Flock &flock,
                 const Params &params, unsigned int dice, const Ear &ear,
                 string &champion, size_t &depth, string &why);

/* generation() for a host that keeps the flock as text: `flock' as it
   was, empty for none yet, and `flockOut' as it is after, heard on a
   localEar of these. */
bool step (thSynth *synth, const string &base, const string &flock,
           const Params &params, unsigned int dice,
           const string &targetGraph, const string &targetWav,
           const Values &values, const Values &targetValues, double rate,
           string &flockOut, string &champion, size_t &depth, string &why);

}

#endif /* NODE_BREED_H */
