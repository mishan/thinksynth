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

#ifndef NODE_MUTATE_H
#define NODE_MUTATE_H 1

/*
 * The edits a search makes to a graph, over the graph's own text.
 *
 * A gene is a constant the author wrote down -- `node { arg = 0.3; }' or a
 * top-level `@control = 0.3;' -- moved in a coordinate where a step is about
 * as audible anywhere: octaves for a time or a frequency, a twentieth of the
 * declared range otherwise, or one of a short list for a choice (a waveform,
 * a count, an FM ratio). An edit is NodeEdit::Text splicing the text, so
 * every candidate is a .dsp that loads, diffs and opens in the editor: a
 * node from the pool spliced into a wire, an envelope or an LFO on a
 * constant, or a second voice beside an oscillator.
 *
 * Nothing here renders or judges. scripts/dspmatch searches with it against
 * a recording; why each rule is the way it is -- choices raced before they
 * are tuned, an edit judged by what it wins after tuning -- is argued there.
 * Free of GTK, like the rest of the node model.
 */

#include <string>
#include <utility>
#include <vector>

using std::pair;
using std::string;
using std::vector;

class thPlugin;
class thPluginManager;
class thSynth;
class thSynthTree;

namespace NodeMutate {

const double RANGED_STEP = 0.05;     /* of the declared range */
const double OCTAVE_STEP = 0.25;     /* octaves               */

struct Gene {
    string node;        /* empty for a top-level control */
    string arg;
    double value;
    double lo, hi;
    bool ranged;
    bool octaves;

    /* Not empty: the gene is one of these and nothing between. */
    vector<double> choices;
};

/* `node.arg = srcNode->srcPort', spelled just so: an arg fed by arithmetic
   is a graph of its own and has no one wire to splice into. */
struct Wire {
    string node, arg;
    string srcNode, srcPort;
};

/* What of a patch's structure an edit needs to know. */
/* A constant, or a control, an arg reads: somewhere a modulator can go. */
struct Slot {
    string node, arg;
    string plugin;      /* the plugin's path, to find siblings by */
    string control;     /* empty for a constant */
    double value;
    bool octaves;       /* how a depth is sized: by the value, or by the range */
    double lo, hi;
};

/* An oscillator and where it gets its pitch: what a second voice would
   read too. */
struct Voice {
    string node;
    string freqNode, freqPort;  /* `freq = node->port' */
    string freqControl;         /* or `freq = @control' */
};

struct Shape {
    vector<Wire> wires;
    vector<Slot> slots;
    vector<Voice> voices;
    vector<string> names;
};

/* Something that can go in a wire: one way in, one way out, and what to
   set the rest to so that it starts as something rather than as zeros. */
struct Splice {
    string spelling, name;
    string in, out;
    vector<pair<string, double> > initial;
};

/* One candidate: which kind of edit -- "splice", "modulate" or "voice" --
   what was done, in words, and the text it was done to. */
struct Edit {
    string kind;
    string what;
    string source;
};

/* The genes of `source' and the places an edit can go, as `tree' -- its
   parse -- has them. Either may be NULL. */
void read (thSynthTree *tree, const string &source, vector<Gene> *genes,
           Shape *shape);

/* A value in a gene's own coordinate and back; back clamps to the declared
   range. moved() is a step of `du' from `from'. */
double toU (const Gene &g, double v);
double fromU (const Gene &g, double u);
double moved (const Gene &g, double from, double du);

/* `value' written into `source' where `g' lives. */
bool write (string &source, const Gene &g, double value);
string geneName (const Gene &g);

int argIndex (thPlugin *plugin, const string &arg);

/* What may be spliced into a wire, from the plugins `pm' can load. */
void buildPool (thPluginManager *pm, vector<Splice> &pool);

/* The three edits, each on `source' in place; false if the text would not
   take it. `name' and `what' say what was added. */
bool splice (string &source, const Shape &shape, const Wire &wire,
             const Splice &sp, string &name);
bool addVoice (string &source, Shape &shape, const Wire &wire,
               const Voice &voice, int kind, string &what);
bool modulate (string &source, Shape &shape, const vector<Slot> &slots,
               int kind, string &what);

vector<Slot> siblings (const Shape &shape, const Slot &one);
vector<const Gene *> selectorsOf (const vector<Gene> &genes,
                                  const vector<Slot> &slots);

/* Every single edit `source' can take, in a fixed order: each pool splice
   into each wire, each modulation of each slot and sibling group (with the
   group's selector as it stands and at each other value), and each voice
   beside each pitch source. `voices', if given, gets how many pitch sources
   there were. */
vector<Edit> edits (const string &source, const Shape &shape,
                    const vector<Gene> &genes, const vector<Splice> &pool,
                    int *voices = NULL);

/* One step of growth, for a host: `text' parsed on `synth', its edits of
   the kinds `kinds' names (space- or comma-separated; empty for all), and
   the one `choice' picks, modulo how many there are -- or the first after
   it that keeps the level near `base', the graph the instrument declares.
   An edit arrives untuned, with whatever gain or resonance its plugin's
   defaults give it; a search judges it by ear, and growth that nobody
   judges would walk an instrument into clipping in a few steps. So each
   candidate is played on a synth of its own -- a phrase of overlapping
   notes across four octaves, so a monophonic graph slides -- and kept only
   if it comes out finite with its RMS no more than 3 dB above `base''s and
   no more than 12 below it, and its peak no more than 3 dB above. Rendering is deterministic, so a piece that grows still
   replays from its seed. False, with `why', for a graph that does not
   parse or has no edit of those kinds among the sixteen it tries that
   passes. */
bool grow (thSynth *synth, const string &text, const string &base,
           unsigned int choice, const string &kinds, string &grown,
           string &what, string &why);

}

#endif /* NODE_MUTATE_H */
