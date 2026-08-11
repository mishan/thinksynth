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

#ifndef TH_PROBE_H
#define TH_PROBE_H 1

#include "thExport.h"
#include "thSampleRing.h"

class thSynthTree;

/* A tap on one node's arg, summed across every voice, published a window at a
 * time to whoever is drawing it.
 *
 * Why this can be cheap. A note is a whole copy of the channel's prototype
 * tree, so "the filter's output" with eight voices sounding is eight buffers
 * in eight separate trees. Finding them would be a name lookup per voice per
 * window -- except that both halves of the address survive the copy verbatim:
 *
 *   thNode's copy constructor carries the node id  (thNode.cpp, `id_ =
 *   copyNode.id()'), and thSynthTree::copyHelper inserts with newNode(node,
 *   false), the overload that does not reassign one.
 *
 *   The arg index is carried twice over -- thArg's copy constructor assigns
 *   `index_ = copyArg->index_' and thNode::copyArgs then sets it again -- and
 *   copyArgs rebuilds argindex_ at those same slots. buildSynthTree() does not
 *   re-run buildArgMap(), so nothing renumbers them afterwards.
 *
 * So a probe is two integers, and resolving it in any voice is two array
 * subscripts. That is what makes summing across voices affordable inside the
 * callback, and it is the whole reason the tap is shaped this way.
 *
 * Worth knowing before relying on it: this is not a property the tap needs
 * anyone to preserve on its behalf. The audio path resolves pointer args
 * through the same ids and indices, so breaking either one segfaults dspcheck
 * long before it produces a wrong picture -- verified by doing it. The tap is
 * riding on load-bearing structure, not on a coincidence.
 *
 * Threading: chan_, nodeId_, argIndex_ and accum_ are set up on the GUI thread
 * before the probe is handed over, and thereafter touched only by the audio
 * thread. The names are for the GUI's benefit only -- they are what a probe is
 * re-resolved from after a reload, since ids are assigned in parse order and
 * do not survive a node being added or removed.
 */
class THINK_API thProbe {
public:
    /* windowlen is the synth's; ringsamples is the depth of the handoff.
       Allocating is the caller's job to do on the GUI thread. */
    thProbe (int chan, unsigned long chanSerial, int nodeId, int argIndex,
             const string &nodeName, const string &argName,
             unsigned int windowlen, unsigned int ringsamples);

    ~thProbe (void);

    /* ---- audio thread ---- */

    /* Zero the accumulator. Called once per window, before any voice. */
    void beginWindow (void);

    /* Add this voice's contribution. A tree that does not contain the node --
       the copy constructor only walks what is reachable from the ionode, so an
       unreachable node leaves a hole -- contributes nothing, which is not an
       error. */
    void accumulate (thSynthTree *tree);

    /* Hand the window over. Called once per window, after every voice, and
       unconditionally: a channel with nothing sounding publishes silence,
       because a display that froze on its last content rather than going quiet
       would be lying about the signal. */
    void publish (void);

    /* ---- GUI thread ---- */

    unsigned int read (float *out, unsigned int n) { return ring_.read(out, n); }
    unsigned int readable (void) const { return ring_.readable(); }
    bool empty (void) const { return ring_.empty(); }

    /* Samples the audio thread had to throw away because this was not being
       drained. Monotonic. */
    unsigned long dropped (void) const { return ring_.dropped(); }

    /* Windows published since the probe was armed, dropped ones included. Lets
       a caller distinguish "silent" from "not running". */
    unsigned long windows (void) const;

    int chan (void) const { return chan_; }

    /* The serial of the thMidiChan this probe's ids were measured against.
       Serials are never reused, so a mismatch means that channel is gone and
       the ids name nothing in particular. */
    unsigned long chanSerial (void) const { return chanSerial_; }

    int nodeId (void) const { return nodeId_; }
    int argIndex (void) const { return argIndex_; }

    const string &nodeName (void) const { return nodeName_; }
    const string &argName (void) const { return argName_; }

    unsigned int windowlen (void) const { return windowlen_; }

private:
    /* One probe, one accumulator, one producer. Copying would give two. */
    thProbe (const thProbe &);
    thProbe &operator= (const thProbe &);

    int chan_;
    unsigned long chanSerial_;
    int nodeId_;
    int argIndex_;

    string nodeName_;   /* GUI thread only */
    string argName_;    /* GUI thread only */

    unsigned int windowlen_;
    float *accum_;

    thSampleRing ring_;

    std::atomic<unsigned long> windows_;
};

/* Eight slots, fixed.
 *
 * Probes are a debugging aid, not a mixer. A bounded array means the audio
 * thread's inner loop is a for() over a small constant with no container to
 * walk and nothing to allocate, and eight is already more displays than fit
 * usefully on one canvas. */
#define TH_MAX_PROBES 8

/* How deep the handoff is, in windows. At 44100 and a 1024-sample window a
   60fps consumer drains roughly one window and a half per frame, so eight is
   about five frames of slack -- enough to ride out a GUI that misses a couple
   of frames, and small enough (32KB per probe) not to be worth economising. */
#define TH_PROBE_RING_WINDOWS 8

#endif /* TH_PROBE_H */
