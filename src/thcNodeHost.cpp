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
 */

#include "config.h"

#include <cmath>
#include <cstdio>

#include "think.h"

#include "thcNodeHost.h"

/* One window, one sample. The degenerate case of a window, and the
 * correct one: a control signal is a value per tick, and asking a plugin
 * for more than the host is going to read would be asking it to compute
 * nineteen samples nobody looks at. */
#define THC_NODE_WINDOW 1

thcNodeHost::thcNodeHost (thSynth *synth, long rate)
    : rate_(rate > 0 ? rate : 1), synth_(synth), tree_(NULL), steps_(0)
{
}

thcNodeHost::~thcNodeHost (void)
{
    /* The tree only. The synth is the piece's, and outlives this. */
    destroyTree();
}

void
thcNodeHost::destroyTree (void)
{
    /* The tree owns its nodes; deleting it takes the args with them. */
    delete tree_;
    tree_ = NULL;
}

/* Which plugin families mean anything at fifty samples a second.
 *
 * This is a judgement about each family and not something the ABI can be
 * asked, which is why it is a list with an argument attached rather than
 * a flag on the plugin. A plugin does not know what rate it is being run
 * at and should not: `samples' is a number it divides by, and the whole
 * reason the same binary works in both hosts is that it never asks what
 * the number means.
 *
 * So the line is drawn by what the arithmetic *becomes* down here:
 *
 *   osc, env, math, logic, filt, misc  -- a shape over time, and time at
 *      fifty a second is still time. An LFO is an oscillator that
 *      happens to be slow, and the plugin cannot tell.
 *
 *   delay, fft  -- both count in *samples*, and a sample is four
 *      hundredths of a second here rather than twenty microseconds. A
 *      delay of 4410 is a hundred milliseconds on the audio thread and a
 *      minute and a half on this one; an FFT window is a spectrum of
 *      nothing. They would run, and produce numbers, and the numbers
 *      would mean something no author intended -- which is worse than
 *      refusing, because it looks like it worked.
 *
 *   dist, mixer, input, impulse, analysis, visual, composer -- about
 *      audio as audio, or about the other host entirely. There is no
 *      reading of `dist::clip' on a control signal that is wrong,
 *      exactly; there is just no reason to have written it, and letting
 *      the list grow by accident is how "the composer can run any
 *      plugin" becomes true by omission rather than by decision.
 *
 * The refusal says which family and why, because "no" without a reason
 * is a bug report waiting to be filed.
 */
bool
thcNodeHost::usefulAtControlRate (const std::string &category,
                                  std::string &why)
{
    static const char *ok[] = {
        "osc", "env", "math", "logic", "filt", "misc", NULL
    };

    for (int i = 0; ok[i] != NULL; i++)
        if (category == ok[i])
            return true;

    if (category == "delay" || category == "fft")
    {
        why = "'" + category + "' counts in samples, and a sample here is "
              "a fiftieth of a second; it would run and mean something "
              "nobody intended";
        return false;
    }

    why = "'" + category + "' is not a family that means anything at "
          "control rate (osc, env, math, logic, filt, misc are)";

    return false;
}

bool
thcNodeHost::addNode (const std::string &name, const std::string &spelling,
                      std::string &why)
{
    if (tree_ != NULL)
    {
        why = "the node host is already built";
        return false;
    }

    if (hasNode(name))
    {
        why = "there is already a node called '" + name + "'";
        return false;
    }

    /* build() invents one by that name to start the walk from, and
       thSynthTree keys its nodes by name -- so a second would overwrite
       the first in the map, leak it, and leave the piece with a
       self-referencing io node and a baffling error. */
    if (name == "ionode")
    {
        why = "'ionode' is the name this host gives the node it invents "
              "to run the others from; call it something else";
        return false;
    }

    const size_t slash = spelling.find('/');

    if (slash == std::string::npos)
    {
        why = "'" + spelling + "' is not a category/plugin name";
        return false;
    }

    if (!usefulAtControlRate(spelling.substr(0, slash), why))
        return false;

    /* And one refusal that is not about the family.
     *
     * osc::static draws from the global generator -- `rand()', unseeded
     * -- which is the one plugin in the tree that does. On the audio
     * thread that is what noise is and nobody minds. Down here it would
     * make a piece that does not replay, and the framework's whole
     * promise is that the same file and the same seed are the same
     * piece. A noise node in a chain has to be as replayable as a markov
     * stage, and this one cannot be: there is nowhere to hand it the
     * piece's seed, and seeding rand() globally would reach into the
     * audio thread's copy of it as well.
     *
     * Refused by name rather than quietly tolerated, because a piece
     * that replays *nearly* is worse than one that says it cannot. If a
     * seedable noise node is ever wanted, it is a new plugin taking a
     * seed arg, not a loosening of this. */
    if (spelling == "osc/static")
    {
        why = "'osc/static' draws from the global random generator, so a "
              "piece using it would not replay; a node in a chain has to "
              "be as repeatable as the composers around it";
        return false;
    }

    if (synth_ == NULL)
    {
        why = "there is no control-rate synth to run nodes on";
        return false;
    }

    thPlugin *plugin = synth_->getPluginManager()->getOrLoadPlugin(spelling);

    if (plugin == NULL || plugin->state() == thPlugin::NOTLOADED)
    {
        why = "no dsp module called '" + spelling + "' is installed";
        return false;
    }

    Decl d;

    d.name = name;
    d.spelling = spelling;
    d.plugin = plugin;

    decls_.push_back(d);

    return true;
}

bool
thcNodeHost::hasNode (const std::string &name) const
{
    for (size_t i = 0; i < decls_.size(); i++)
        if (decls_[i].name == name)
            return true;

    return false;
}

/* The plugin behind a declared node, or NULL. */
const thPlugin *
thcNodeHost::pluginOf (const std::string &node) const
{
    for (size_t i = 0; i < decls_.size(); i++)
        if (decls_[i].name == node)
            return decls_[i].plugin;

    return NULL;
}

/* Does the plugin declare this arg, and is it one a file may write?
 *
 * thNode::setArg creates an arg that does not exist, which is right for
 * the .dsp grammar -- a node may carry values its plugin never asked
 * about -- and quietly wrong here. `frq = 0.05' for `freq' produced a
 * dead arg and an oscillator running at zero, so `wavelength =
 * rate / 0' and a flat line: no error anywhere, and a piece that simply
 * does not breathe. The composer side of the arrow was already checked;
 * a reader would reasonably assume both ends were.
 *
 * ARG_STATE is refused too. It is a plugin's scratch -- osc::simple's
 * `last' is a raw phase counter -- and writing one from a file is
 * reaching into the middle of somebody's arithmetic.
 */
bool
thcNodeHost::checkArg (const std::string &node, const std::string &arg,
                       bool wantOutput, std::string &why) const
{
    const thPlugin *p = pluginOf(node);

    if (p == NULL)
    {
        why = "no node called '" + node + "'";
        return false;
    }

    for (int i = 0; i < p->argCount(); i++)
    {
        if (p->getArgName(i) != arg)
            continue;

        const thPlugin::ArgDir dir = p->getArgDir(i);

        if (dir == thPlugin::ARG_STATE)
        {
            why = "'" + node + "." + arg + "' is that module's own "
                  "scratch, not something a piece may set or read";
            return false;
        }

        if (wantOutput && dir != thPlugin::ARG_OUT)
        {
            why = "'" + node + "->" + arg + "' is an input, not an "
                  "output";
            return false;
        }

        return true;
    }

    why = "'" + node + "' is " + spellingOf(node) + ", which has no arg "
          "called '" + arg + "'";

    return false;
}

std::string
thcNodeHost::spellingOf (const std::string &node) const
{
    for (size_t i = 0; i < decls_.size(); i++)
        if (decls_[i].name == node)
            return "'" + decls_[i].spelling + "'";

    return "unknown";
}

bool
thcNodeHost::setValue (const std::string &node, const std::string &arg,
                       double value, std::string &why)
{
    if (!checkArg(node, arg, false, why))
        return false;

    Value v;

    v.node = node;
    v.arg = arg;
    v.value = value;

    values_.push_back(v);

    return true;
}

bool
thcNodeHost::setWire (const std::string &node, const std::string &arg,
                      const std::string &fromNode, const std::string &fromArg,
                      std::string &why)
{

    /* The destination is checkable now; the source is not, because a
       wire may name a node the file has not reached yet, exactly as a
       .dsp may. build() checks that end once every name is known. */
    if (!checkArg(node, arg, false, why))
        return false;

    Wire w;

    w.node = node;
    w.arg = arg;
    w.fromNode = fromNode;
    w.fromArg = fromArg;

    wires_.push_back(w);

    return true;
}

bool
thcNodeHost::setKnob (const std::string &node, const std::string &arg,
                      thArg *knob, std::string &why)
{
    if (!checkArg(node, arg, false, why))
        return false;

    if (knob == NULL)
    {
        why = "no such knob";
        return false;
    }

    KnobBind b;

    b.node = node;
    b.arg = arg;
    b.knob = knob;
    b.dest = NULL;

    knobs_.push_back(b);

    return true;
}

bool
thcNodeHost::build (std::string &why)
{
    if (tree_ != NULL)
    {
        why = "the node host is already built";
        return false;
    }

    if (decls_.empty())
        return true;                    /* nothing to build, and not an error */

    tree_ = new thSynthTree("composer", synth_);

    for (size_t i = 0; i < decls_.size(); i++)
    {
        thNode *n = new thNode(decls_[i].name, decls_[i].plugin);

        tree_->newNode(n, true);
    }

    for (size_t i = 0; i < values_.size(); i++)
    {
        thNode *n = tree_->findNode(values_[i].node);

        if (n != NULL)
            n->setArg(values_[i].arg, (float)values_[i].value);
    }

    for (size_t i = 0; i < wires_.size(); i++)
    {
        thNode *n = tree_->findNode(wires_[i].node);

        if (tree_->findNode(wires_[i].fromNode) == NULL)
        {
            why = "'" + wires_[i].node + "." + wires_[i].arg +
                  "' reads '" + wires_[i].fromNode + "->" + wires_[i].fromArg +
                  "', and there is no node called '" + wires_[i].fromNode + "'";
            destroyTree();
            return false;
        }

        /* The far end, now that every name is known. setPointers would
           otherwise *create* the missing arg as a permanent zero and
           wire to it, so `in0 = lfo->nosuch' became a silent constant. */
        if (!checkArg(wires_[i].fromNode, wires_[i].fromArg, true, why))
        {
            destroyTree();
            return false;
        }

        if (n != NULL)
            n->setArg(wires_[i].arg, wires_[i].fromNode, wires_[i].fromArg);
    }

    /* The io node, which this host invents.
     *
     * thSynthTree::process walks *down from the io node* and fires what
     * it reaches, so a node nothing points at is a node that never runs.
     * In a .dsp that is a real statement -- an unreachable node is dead
     * code and not computing it is the optimisation. Here every node is
     * declared because a composer wanted it, and the thing that reads it
     * is a param store rather than another node, which the tree cannot
     * see. So the io node reaches for all of them.
     *
     * It has no plugin, which the walk allows for: `if ((plug =
     * ionode_->plugin()))'. It is a handle for the walk to start at and
     * nothing else. */
    thNode *io = new thNode("ionode", NULL);

    tree_->newNode(io, true);

    for (size_t i = 0; i < decls_.size(); i++)
    {
        const std::string out = outputArgOf(decls_[i].plugin);

        if (out.empty())
        {
            why = "'" + decls_[i].spelling + "' has no output arg, so "
                  "nothing could ever read node '" + decls_[i].name + "'";
            destroyTree();
            return false;
        }

        io->setArg(decls_[i].name, decls_[i].name, out);
    }

    tree_->setIONode("ionode");

    /* The same finishing sequence thSynth::finishParse runs, minus the
       parts that are about a file: no unit folds were deferred (this
       host takes numbers, not `5 ms'), and no chanargs exist to type. */
    tree_->buildArgMap();
    tree_->setPointers();
    tree_->buildSynthTree();

    /* The knob bindings, resolved to the args they write once rather
       than looked up fifty times a second. An arg the file never
       mentioned does not exist yet, so it is created here the same way
       setPointers creates the ones a wire referred to. */
    for (size_t i = 0; i < knobs_.size(); i++)
    {
        thNode *n = tree_->findNode(knobs_[i].node);

        if (n == NULL)
            continue;

        knobs_[i].dest = n->getArg(knobs_[i].arg);

        if (knobs_[i].dest == NULL)
            knobs_[i].dest = n->setArg(knobs_[i].arg, 0.0f);
    }

    steps_ = 0;

    return true;
}

/* The plugin's first declared output.
 *
 * First rather than one called "out", because that is a convention and
 * not a rule -- and a plugin with several says which is which by
 * declaring them in order, the same way its enum does. The io node needs
 * one arg per node purely to reach it; which output it names does not
 * change what gets computed, since a node fires whole. */
std::string
thcNodeHost::outputArgOf (const thPlugin *plugin)
{
    if (plugin == NULL)
        return "";

    for (int i = 0; i < plugin->argCount(); i++)
        if (plugin->getArgDir(i) == thPlugin::ARG_OUT)
            return plugin->getArgName(i);

    return "";
}

thArg *
thcNodeHost::output (const std::string &node, const std::string &arg,
                     std::string &why)
{
    if (tree_ == NULL)
    {
        why = "there are no dsp stages in this chain";
        return NULL;
    }

    thNode *n = tree_->findNode(node);

    if (n == NULL)
    {
        why = "no node called '" + node + "'";
        return NULL;
    }

    if (!checkArg(node, arg, true, why))
        return NULL;

    thArg *a = n->getArg(arg);

    if (a == NULL)
    {
        why = "'" + node + "' has no arg called '" + arg + "'";
        return NULL;
    }

    return a;
}

std::vector<thcNodeHost::KnobUse>
thcNodeHost::knobUses (void) const
{
    std::vector<KnobUse> out;

    for (size_t i = 0; i < knobs_.size(); i++)
    {
        if (knobs_[i].knob == NULL)
            continue;

        KnobUse u;

        u.node = knobs_[i].node;
        u.arg  = knobs_[i].arg;
        u.knob = knobs_[i].knob->name();

        out.push_back(u);
    }

    return out;
}

/* Every node runs every window.
 *
 * NOT thSynthTree::setActiveNodes(), which is the audio thread's answer
 * to a different question. That walks up from the nodes whose plugin
 * declared itself ACTIVE -- an oscillator, an envelope: something that
 * produces without being asked -- and marks them and everything
 * downstream. A patch always has one, because a patch that generates no
 * sound is not a patch, and skipping the rest is the optimisation that
 * makes a hundred voices affordable.
 *
 * Down here that assumption is simply false. `math' and `logic' plugins
 * are PASSIVE to a module, and a chain is entitled to contain nothing
 * but arithmetic: a knob through a mul and an add, which the format
 * openly invites, has an empty active list. Such a graph fired once --
 * on the stale recalc flags buildSynthTree happens to leave behind --
 * and then froze, and after a reset produced zeros forever, which is a
 * replay divergence arriving through the quietest possible door.
 *
 * There is nothing to optimise here anyway: one window of one sample,
 * fifty times a second, over a handful of nodes. Marking everything is
 * both cheaper to reason about and the only reading that is right.
 */
void
thcNodeHost::markAll (void)
{
    const thSynthTree::NodeMap &nodes = tree_->nodes();

    for (thSynthTree::NodeMap::const_iterator i = nodes.begin();
         i != nodes.end(); ++i)
        if (i->second != NULL)
            i->second->setRecalc(true);
}

void
thcNodeHost::stepTo (double transport)
{
    /* `!(x >= 0)' rather than `x < 0', so a NaN is refused too: it
       would otherwise reach floor() and come back as LONG_MIN. */
    if (tree_ == NULL || !(transport >= 0))
        return;

    /* How many windows the transport has earned, as a pure function of
       the time it reads. Not "one per call": the scheduler's dt jitters
       with the frame, and a piece whose LFO advanced by however much
       wall clock had passed would not replay. */
    const long want = (long)std::floor(transport * (double)rate_);

    /* A reset moves the transport backwards. Catching up would fire
       nothing; the step count is put back where the time says. */
    if (want < steps_)
        steps_ = want;

    /* A guard rather than a policy: a piece opened at an hour in has
       earned a hundred and eighty thousand windows, and firing them all
       inside one timer callback would stop the program. Nodes are
       *state over time*, so the honest thing is to accept that a jump
       is a jump -- the same thing pause and resume already do to them. */
    if (want - steps_ > rate_ * 4)
        steps_ = want - rate_ * 4;

    while (steps_ < want)
    {
        /* Where the knobs are now, before the window that reads them. */
        for (size_t i = 0; i < knobs_.size(); i++)
            if (knobs_[i].dest != NULL && knobs_[i].knob != NULL)
                knobs_[i].dest->setValue((*knobs_[i].knob)[0]);

        markAll();
        tree_->process(THC_NODE_WINDOW);
        steps_++;
    }
}

/* Back to the top.
 *
 * Zeroed rather than rebuilt, and that is the whole of the difference
 * between this and what the scheduler does to a composer instance. A
 * composer's state is opaque -- behind a void* the module owns -- so the
 * only way to be sure of it is to make a new one. A node's is not: it
 * lives in the args the plugin declared ARG_STATE, and its outputs live
 * in the ones it declared ARG_OUT, and both are thArgs this can see.
 *
 * Zero is exactly right for them, not merely plausible. thArg::allocate
 * value-initialises, so a node that has never run has zeroes in both --
 * which makes "set them to zero" and "make it again" the same state, and
 * the cheaper one keeps the promise a param binding depends on: the
 * output buffer a `step = lfo->out' points at is the same buffer
 * afterwards. Rebuilding would hand every binding in the piece a
 * dangling pointer at the exact moment a replay began.
 *
 * The inputs are left alone. They are the file's numbers, or wires to
 * other nodes, and neither is state.
 */
void
thcNodeHost::reset (void)
{
    if (tree_ == NULL)
        return;

    for (size_t i = 0; i < decls_.size(); i++)
    {
        thNode *n = tree_->findNode(decls_[i].name);
        const thPlugin *p = decls_[i].plugin;

        if (n == NULL || p == NULL)
            continue;

        for (int a = 0; a < p->argCount(); a++)
        {
            const thPlugin::ArgDir dir = p->getArgDir(a);

            if (dir != thPlugin::ARG_STATE && dir != thPlugin::ARG_OUT)
                continue;

            thArg *arg = n->getArg(p->getArgName(a));

            if (arg != NULL)
                arg->setValue(0.0f);
        }
    }

    steps_ = 0;
}
