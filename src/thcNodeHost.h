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

#ifndef THCNODEHOST_H
#define THCNODEHOST_H

#include <string>
#include <vector>

class thArg;
class thNode;
class thPlugin;
class thSynth;
class thSynthTree;

/*
 * The second interpreter: DSP nodes run at control rate, on the GUI
 * thread, for a composer to read.
 *
 * UNIFICATION.md phase 3. The same dlopen'd .so files the audio thread
 * runs, the same thSynthTree walking them in dependency order, the same
 * `->' wiring between them -- one window of one sample at a time, fifty
 * times a second, so that a piece can put an LFO on a chain's density
 * and an envelope on its dynamics. "The same modules that modulate
 * sound", pointed at the composition instead.
 *
 * Second interpreter and not a shortcut into thSynth, and §0 of the plan
 * is why. Audio is per window on the audio callback; this is sparse and
 * on the GUI thread. Sharing the host would mean one of those two
 * columns bending, and the day it bends is the day a composer's LFO is
 * running inside the audio callback.
 *
 * ---- what makes this work at all
 *
 * A plugin reads `samples' as the rate it is running at -- osc::simple
 * computes `wavelength = samples/freq' -- and keeps whatever it
 * remembers between windows in an ARG_STATE arg rather than in the
 * buffer. So a graph run one sample at a time at fifty samples a second
 * is not a degenerate case that happens to work: it is the same
 * arithmetic at a different rate, which is exactly what a control signal
 * is. `freq = 0.05' is a twenty-second cycle here and a very low note
 * there, and neither reading is a special case in the plugin.
 *
 * thSynthTree reaches its thSynth for one thing only, getSampleRate(),
 * which is what makes a second host cheap: one small thSynth reporting
 * the control rate, shared by every chain in the piece that has nodes in
 * it, and nothing else about the engine duplicated. It does not become
 * thSynth::instance() -- that is set only when there is none -- so the
 * application's synth stays the one the patch manager talks to.
 *
 * ---- determinism
 *
 * Steps are counted off *transport time*, not off the wall clock and not
 * off the dt handed to the scheduler: the number of windows fired by the
 * time the transport reads t is floor(t * rate). So a pause freezes the
 * nodes with everything else, a reset replays them, and a frame that
 * arrived late does not change what a piece sounds like. The scheduler's
 * own dt jitters; this must not.
 *
 * One exception, stated because it is the only one: a forward jump of
 * more than a few seconds is capped rather than caught up (see stepTo).
 * A piece stalled for a minute would otherwise fire three thousand
 * windows inside one timer callback, and nodes are state over time, so
 * the honest reading of a jump is that it is a jump -- the same thing a
 * pause already does to them.
 */
class thcNodeHost
{
public:
    /* `synth' is the control-rate synth, owned by the caller and shared
       by every host in a piece: it answers one question -- what rate is
       this? -- and owns the plugin manager the nodes come out of.
     *
       Shared and not one each, for a reason that is not only thrift.
       Every plugin keeps its registered arg indices in a file-scope
       global (`int args[]' at the top of each one), which assumes one
       thPlugin per module per process. A second thPluginManager
       dlopen's the same .so and calls module_init again, and the two
       thPlugins then overwrite each other's idea of which index is
       which. It is benign while registration order is identical, and it
       is not a thing to leave lying about. */
    thcNodeHost (thSynth *synth, long rate);
    ~thcNodeHost (void);

    /* Owns a synth it did not make and a tree it did: copying one would
       double-free the second and confuse the first. */
    thcNodeHost (const thcNodeHost &) = delete;
    thcNodeHost &operator= (const thcNodeHost &) = delete;

    long rate (void) const { return rate_; }

    /* True once build() has succeeded and there is something to step. */
    bool ready (void) const { return tree_ != NULL; }

    /* ---- construction, in the order the loader does it ---------------- */

    /* One `stage lfo osc::simple { ... }'. `spelling' is the .dsp name --
       "osc/simple" -- and `why' says what is wrong when this returns
       false: no such plugin, or a category that means nothing one sample
       at a time. */
    bool addNode (const std::string &name, const std::string &spelling,
                  std::string &why);

    bool hasNode (const std::string &name) const;

    /* `freq = 0.05;' inside a node's block. */
    bool setValue (const std::string &node, const std::string &arg,
                   double value, std::string &why);

    /* `in0 = lfo->out;' -- one node reading another. Resolved by name
       when build() runs, so the two may be written in either order. */
    bool setWire (const std::string &node, const std::string &arg,
                  const std::string &fromNode, const std::string &fromArg,
                  std::string &why);

    /* `in1 = @depth;' -- a piece knob driving a node's arg.
     *
     * The same knob a stage param binds to and an instrument chanarg
     * reads, reaching one world further. Phase 2 made a knob mean the
     * same thing on both sides of the composer/instrument boundary;
     * leaving the nodes out of that would have made the depth of an
     * LFO the one number in a piece that could not be put on a slider.
     *
     * A copy per window rather than a signal connection, and the same
     * argument the instrument bindings make in reverse: what reads a
     * node arg is the tree, on this host's clock, so the value has to
     * be *there* when the window fires rather than delivered whenever a
     * hand moved. Fifty copies a second of a handful of floats is not a
     * cost worth being clever about. */
    bool setKnob (const std::string &node, const std::string &arg,
                  thArg *knob, std::string &why);

    /* Wires the synthetic io node and hands the tree to the engine's own
       builder. Nothing steps before this. */
    bool build (std::string &why);

    /* ---- reading and running ----------------------------------------- */

    /* The live buffer behind `node->arg', for a composer param to read
       through. Stable for the host's lifetime: the buffer is one sample
       and allocate() keeps it when the length has not changed, so a
       binding may hold the pointer. NULL when there is no such arg,
       which the loader reports rather than passing on. */
    thArg *output (const std::string &node, const std::string &arg,
                   std::string &why);

    /* Every output arg the node's plugin declares, in declaration order,
       for anything that has to tell a reader how to reach this node.
     *
       A list and not a name, because there is no such thing as "the"
       output: filt::moog declares out_low, out_high and out_bandpass,
       and a panel that said `->out' would be naming an arg that does
       not exist -- a wire the loader refuses, blamed on the line that
       copied the advice. Empty when there is no such node, which is
       also the honest answer before build(). */
    std::vector<std::string> outputArgs (const std::string &node) const;

    /* Which knob drives which node arg, for anything drawing the piece.
       A knob reaching a node is as much a wire as one reaching a stage
       param, and a canvas that showed only the second would draw a
       slider connected to nothing. */
    struct KnobUse
    {
        std::string node, arg, knob;
    };

    std::vector<KnobUse> knobUses (void) const;

    /* Fire whatever windows the transport has earned since the last
       call. Cheap and exact when nothing is due. */
    void stepTo (double transport);

    /* Back to the top: every node's state forgotten, the window count
       zeroed. What thcScheduler::reset means down here. */
    void reset (void);

private:
    /* One node as the file declared it, kept until build(). The engine's
       nodes cannot be made until every name is known, because a wire may
       name a node that has not been read yet. */
    struct Decl
    {
        std::string name, spelling;
        thPlugin   *plugin;
    };

    struct Wire
    {
        std::string node, arg, fromNode, fromArg;
    };

    struct Value
    {
        std::string node, arg;
        double      value;
    };

    /* A knob and the node arg it drives, resolved to the arg itself by
       build() so that stepping is a copy rather than a lookup. */
    struct KnobBind
    {
        std::string node, arg;
        thArg      *knob;
        thArg      *dest;
    };

    /* True if a category is worth running one sample at a time. See the
       definition: the answer is a judgement about each family, not a
       property the plugin ABI can be asked for. */
    static bool usefulAtControlRate (const std::string &category,
                                     std::string &why);

    /* The plugin's first declared output, for the io node to reach it
       by. See the definition for why first rather than "out". */
    static std::string outputArgOf (const thPlugin *plugin);

    /* The plugin behind a declared node, and its `osc/simple' name. */
    const thPlugin *pluginOf (const std::string &node) const;
    std::string     spellingOf (const std::string &node) const;

    /* Does the plugin declare this arg, and may a file touch it? See
       the definition -- thNode::setArg invents an arg that does not
       exist, which is right for a .dsp and silent here. */
    bool checkArg (const std::string &node, const std::string &arg,
                   bool wantOutput, std::string &why) const;

    /* Marks every node for recalculation before a window. See the
       definition: the audio thread's setActiveNodes() answers a
       question this host does not have. */
    void markAll (void);

    void destroyTree (void);

    long         rate_;

    thSynth     *synth_;        /* borrowed: a rate and a plugin manager  */
    thSynthTree *tree_;

    std::vector<Decl>     decls_;
    std::vector<Wire>     wires_;
    std::vector<Value>    values_;
    std::vector<KnobBind> knobs_;

    /* Windows fired so far. The whole determinism story is that this is
       floor(transport * rate) and nothing else. */
    long steps_;
};

#endif /* THCNODEHOST_H */
