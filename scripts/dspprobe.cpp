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
 * dspprobe -- is a probe reading the signal it says it is reading?
 *
 * The tap addresses a node by (nodeId, argIndex) because both survive the
 * per-note tree copy verbatim -- see the comment in thProbe.h. So the
 * reference here is deliberately built the *other* way: it walks the sounding
 * notes and looks the node and arg up by **name**, in the note's own tree,
 * after process() has returned. A reference that used the same ids would be
 * checking addition.
 *
 * Run across the corpus it arms a probe on every declared output port of every
 * node, three voices sounding, and compares bitwise.
 *
 * What that does and does not prove, since it would be easy to overclaim. Each
 * of the four ways of breaking the addressing was tried:
 *
 *   thProbe::accumulate reading one sample along        20 of 54 checks
 *   the decaying-note loop not accumulated               1 of 54
 *   publish() not counting a window it dropped           1 of 54
 *   disarmProbesOn() doing nothing on a reload           1 of 34
 *   thNode's copy ctor not carrying the id              segfaults dspcheck
 *   neither path carrying the arg index                 segfaults dspcheck
 *
 * The last two are not this harness's to catch, and it is better to say so
 * than to imply otherwise. The audio path resolves every pointer arg through
 * the same ids and indices, so scrambling them takes the whole engine down and
 * the existing harnesses fire first. What is specific to the tap -- which
 * buffer it reads, which voices it sums, when it publishes -- is what the
 * comparison here is load-bearing for.
 *
 * One thing here is deliberately *not* tested: the channel-serial check in
 * thSynth::process(). Disarming on reload is what actually keeps a probe off a
 * replaced tree, and that is tested; the serial exists so that a future caller
 * who forgets to disarm gets silence rather than a confident picture of the
 * wrong node. There is no way to reach that state through the public API,
 * which is the point of it, so it cannot be provoked from here.
 *
 * The reference cannot see a note that ended during the window being checked,
 * because thMidiChan retires it before process() returns and the harness only
 * gets to look afterwards. That is why the comparison runs over the first few
 * windows with the trigger held, where the note count cannot change. The tap
 * covers releases -- it accumulates inside both note loops -- and dsplive is
 * the harness that has something to say about released notes.
 *
 * Also covered, once each rather than per file: a scalar arg read as its
 * constant, plugin state refused, slots exhausted, re-arming the same point
 * being idempotent, a reload disarming what pointed at the old tree, and a
 * ring nobody drains counting its drops instead of lying.
 *
 * The editor's hover (thSynth::armHover) is held to the newest voice's tree
 * on every port NodeGraph::tapAt names, and once each: it arms with the eight
 * taken, reads no voices before a note, follows a re-struck key and a voice
 * from the pool, and is disarmed by a reload.
 *
 *   cmake --build build
 *   ./build/scripts/dspprobe -p build/plugins/ dsp/ts1.dsp
 */

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "think.h"
#include "NodeGraph.h"

namespace {

int failures = 0;
int checks = 0;

void ok (bool cond, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)))
#endif
;

void ok (bool cond, const char *fmt, ...)
{
    checks++;

    if (cond)
        return;

    va_list ap;

    va_start(ap, fmt);
    printf("FAIL  ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);

    failures++;
}

const int NOTES[] = { 60, 64, 67 };
const int NUM_NOTES = 3;

/* How many of NOTES this channel will hold at once.
 *
 * Three, unless the graph says otherwise on its io node: `mono = 1' answers
 * every note with the same voice, `choke = 1' leaves only the newest keyed
 * and sends the rest into their release, and `poly = N' retires down to N. A
 * harness that played three and then insisted on finding three would read a
 * mono graph as a graph that publishes nothing -- which is what dsp/bass.dsp
 * did to this one. */
int voicesFor (thSynth &synth, int chan)
{
    thMidiChan *c = synth.getChannel(chan);

    if (c == NULL)
        return NUM_NOTES;

    if (c->mono() || c->choke())
        return 1;

    const int limit = c->polyMax();

    return (limit > 0 && limit < NUM_NOTES) ? limit : NUM_NOTES;
}

/* Are the notes this channel can hold still held?
 *
 * thMidiChan retires a note the moment its `play' arg reaches zero, which
 * happens inside process() -- so from out here the note is already gone while
 * the window it contributed to is the one about to be read. Percussive patches
 * reach that within a few windows (scripts/guard/divergent.dsp does it in
 * seven), and comparing past it means comparing the tap against a reference
 * that can no longer see what the tap correctly still holds.
 *
 * So the comparison stops at that boundary rather than pretending to cover
 * it. What happens to a probe during a release is the tap's business and it is
 * covered by construction -- the accumulate is inside both note loops -- not
 * by this. */
bool allNotesSounding (thSynth &synth, int chan, int voices)
{
    thMidiChan *c = synth.getChannel(chan);

    if (c == NULL)
        return false;

    /* The newest survive a polyphony limit, so those are the ones to look
       for: the channel retires from the oldest held voice forward. */
    for (int n = NUM_NOTES - voices; n < NUM_NOTES; n++)
        if (c->getNote(NOTES[n]) == NULL)
            return false;

    return true;
}

/* Does this channel answer a note by cutting the voices that are sounding?
 *
 * It matters here because referenceSum below walks the notes the channel
 * still has keyed, and a choked voice is neither keyed nor gone: it is in
 * decaying_, releasing, and the tap goes on accumulating it. The reference
 * cannot see it, so a choked graph is played one note rather than three. */
bool chokes (thSynth &synth, int chan)
{
    thMidiChan *c = synth.getChannel(chan);

    return c != NULL && c->choke();
}

/* The independent path: every sounding note's tree, by name. */
void referenceSum (thSynth &synth, int chan, const string &node,
                   const string &arg, int windowlen, vector<float> &out)
{
    out.assign(windowlen, 0.0f);

    thMidiChan *c = synth.getChannel(chan);

    if (c == NULL)
        return;

    for (int n = 0; n < NUM_NOTES; n++)
    {
        thMidiNote *note = c->getNote(NOTES[n]);

        if (note == NULL)
            continue;

        thSynthTree *tree = note->synthTree();

        if (tree == NULL)
            continue;

        thNode *nd = tree->findNode(node);

        if (nd == NULL)
            continue;

        thArg *a = nd->getArg(arg);

        if (a == NULL)
            continue;

        for (int i = 0; i < windowlen; i++)
            out[i] += (*a)[i];
    }
}

/* One output port, under both the names it has.
 *
 * They differ for an expression: the canvas and the file call its box
 * `osc2.freq', and the engine knows the last node of the chain the desugar
 * built. Arming goes through the box name, which is what the editor holds and
 * what a `# @probe' line stores; the reference sum reads the note's own tree,
 * which only knows the other one. */
struct PortRef
{
    string arm;     /* what a probe is armed on  */
    string ref;     /* what the note's tree calls it */
    string port;
};

/* Every output port the graph model has, which is what a probe is for. Uses
   NodeGraph rather than the plugin tables directly so that what is checked
   here is what the editor would offer. */
void outputPorts (const NodeGraph &g, vector<PortRef> &out)
{
    for (size_t b = 0; b < g.boxes().size(); b++)
    {
        const NodeGraph::Box &bx = g.boxes()[b];

        if (bx.isControl)
            continue;

        for (size_t p = 0; p < bx.ports.size(); p++)
        {
            if (bx.ports[p].isInput)
                continue;

            PortRef r;

            r.arm = bx.name;
            r.ref = bx.isExpr ? bx.exprOutNode : bx.name;
            r.port = bx.ports[p].name;

            out.push_back(r);
        }
    }
}

/* ---- the per-file check ---- */

struct Result { int ports; int matched; int silent; int bad; };

Result checkFile (const string &pluginPath, const char *file, int windows,
                  bool quiet)
{
    Result r;

    memset(&r, 0, sizeof(r));

    /* The port list comes off an unowned parse, so building it cannot disturb
       anything a channel is playing. */
    vector<PortRef> ports;

    {
        thSynth look(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
        thSynthTree *tree = look.parseTree(file);

        if (tree == NULL)
        {
            r.bad = -1;   /* would not load; the caller counts it as skipped */
            return r;
        }

        /* A channel effect has no note to play. It is a .dsp, and every
           structural gate covers it, but this one plays a chord and reads
           what the voices publish -- and a graph the engine feeds from in0
           publishes nothing when nobody is feeding it, which is not a
           failure, it is the wrong question. fxcheck asks the right one. */
        if (tree->takesInput())
        {
            delete tree;

            if (!quiet)
                printf("      (%s is a channel effect; fxcheck covers it)\n",
                       file);

            r.bad = -1;
            return r;
        }

        NodeGraph g;

        g.build(tree);
        delete tree;

        outputPorts(g, ports);
    }

    /* One synth per port. Wasteful, and the alternative -- eight probes on one
       render -- would let a bug in one probe's accumulator hide inside another
       probe's sum. Renders are 16 windows; this stays quick enough. */
    for (size_t p = 0; p < ports.size(); p++)
    {
        srand(1);

        thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        if (synth.loadTree(file, 0, 100) == NULL)
        {
            r.bad = -1;
            return r;
        }

        string why;
        const int slot = synth.armProbe(0, ports[p].arm, ports[p].port,
                                        why);

        if (slot < 0)
        {
            /* An output port the tap will not take is a real disagreement
               between the editor and the engine, not a curiosity. */
            printf("FAIL  %s: cannot probe %s.%s -- %s\n", file,
                   ports[p].arm.c_str(), ports[p].port.c_str(),
                   why.c_str());
            r.bad++;
            checks++;
            failures++;
            continue;
        }

        r.ports++;

        /* The channel exists from the load, and what is asked of it here is
           the constants read when it was built. */
        const int voices = voicesFor(synth, 0);
        const int first = chokes(synth, 0) ? NUM_NOTES - 1 : 0;

        for (int n = first; n < NUM_NOTES; n++)
            synth.addNote(0, (float)NOTES[n], 100);

        const int windowlen = synth.getWindowlen();

        vector<float> tapped(windowlen), reference(windowlen);

        bool mismatch = false, anySignal = false;
        int comparedWindows = 0;

        for (int w = 0; w < windows && !mismatch; w++)
        {
            synth.process();

            /* Checked after process() and before the read: once a note has
               been retired the reference is blind and the rest of this run
               would be measuring the harness, not the tap. */
            if (!allNotesSounding(synth, 0, voices))
                break;

            thProbe *probe = synth.probe(slot);

            if (probe == NULL)
            {
                printf("FAIL  %s: probe on %s.%s disarmed itself\n", file,
                       ports[p].arm.c_str(), ports[p].port.c_str());
                r.bad++;
                checks++;
                failures++;
                break;
            }

            const unsigned int got =
                probe->read(&tapped[0], (unsigned int)windowlen);

            if (got == 0)
                continue;   /* the first window is armed, not yet published */

            if ((int)got != windowlen)
            {
                printf("FAIL  %s: %s.%s published %u of %d samples\n", file,
                       ports[p].arm.c_str(), ports[p].port.c_str(), got,
                       windowlen);
                r.bad++;
                checks++;
                failures++;
                break;
            }

            referenceSum(synth, 0, ports[p].ref, ports[p].port, windowlen,
                         reference);

            /* The tap is a window behind: it published what was computed
               during the process() before this read, and referenceSum is
               looking at the buffers that same call left behind. Both describe
               the window that just ran. */
            if (memcmp(&tapped[0], &reference[0],
                       windowlen * sizeof(float)) != 0)
            {
                /* Located with the same comparison memcmp used, not with ==.
                 *
                 * +0.0f and -0.0f compare equal and differ bitwise, and so do
                 * two NaNs -- both of which this corpus produces, since four
                 * DSPs diverge and mixer.out on bd1 reaches -inf. With == the
                 * scan could pass every sample and leave `at' at windowlen,
                 * which is one past the end of both buffers and was read
                 * anyway to print the values. */
                int at = 0;

                while (at < windowlen &&
                       memcmp(&tapped[at], &reference[at], sizeof(float)) == 0)
                    at++;

                if (at >= windowlen)
                {
                    /* memcmp said they differ and a per-float memcmp cannot
                       disagree with it, so this is unreachable -- but printing
                       a sample past the end to say so would be worse than
                       saying it plainly. */
                    printf("FAIL  %s: %s.%s differs from the reference sum at "
                           "window %d, but no single sample does\n", file,
                           ports[p].arm.c_str(), ports[p].port.c_str(), w);
                    mismatch = true;
                    r.bad++;
                    checks++;
                    failures++;
                    break;
                }

                printf("FAIL  %s: %s.%s differs from the reference sum at "
                       "window %d sample %d (%g vs %g)\n", file,
                       ports[p].arm.c_str(), ports[p].port.c_str(), w, at,
                       (double)tapped[at], (double)reference[at]);
                mismatch = true;
                r.bad++;
                checks++;
                failures++;
                break;
            }

            comparedWindows++;

            for (int i = 0; i < windowlen; i++)
                if (tapped[i] != 0.0f)
                    anySignal = true;
        }

        if (mismatch)
            continue;

        checks++;

        if (comparedWindows == 0)
        {
            printf("FAIL  %s: %s.%s published nothing in %d windows\n", file,
                   ports[p].arm.c_str(), ports[p].port.c_str(), windows);
            r.bad++;
            failures++;
            continue;
        }

        if (anySignal)
            r.matched++;
        else
            r.silent++;   /* correct for a port this note never drives */

        if (!quiet)
            printf("ok    %-28s %s.%-12s %d windows%s\n", file,
                   ports[p].arm.c_str(), ports[p].port.c_str(),
                   comparedWindows, anySignal ? "" : "  (silent)");
    }

    return r;
}

/* ---- the hover, on every port the canvas would hover ----
 *
 * NodeGraph::tapAt names what a hover on a port reads; each one is armed,
 * the notes struck so that the newest voice is neither the first nor the
 * last the channel runs (it runs them in key order), and the reading
 * compared against that voice's own tree, looked up by name. */

struct HoverResult { int ports; int bad; };

HoverResult checkHover (const string &pluginPath, const char *file,
                        bool quiet)
{
    HoverResult r = { 0, 0 };
    vector<PortRef> taps;

    {
        thSynth look(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
        thSynthTree *tree = look.parseTree(file);

        if (tree == NULL)
            return r;

        if (tree->takesInput())
        {
            delete tree;
            return r;
        }

        NodeGraph g;

        g.build(tree);
        delete tree;

        for (size_t b = 0; b < g.boxes().size(); b++)
        {
            for (size_t k = 0; k < g.boxes()[b].ports.size(); k++)
            {
                PortRef t;

                if (!g.tapAt((int)b, (int)k, t.arm, t.port))
                    continue;

                t.ref = g.boxes()[b].isExpr ? g.boxes()[b].exprOutNode
                                            : t.arm;
                taps.push_back(t);
            }
        }
    }

    for (size_t p = 0; p < taps.size(); p++)
    {
        const PortRef &t = taps[p];

        srand(1);

        thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        if (synth.loadTree(file, 0, 100) == NULL)
            return r;

        string why;

        if (synth.armHover(0, t.arm, t.port, why) != TH_HOVER_SLOT)
        {
            printf("FAIL  %s: cannot hover %s.%s -- %s\n", file,
                   t.arm.c_str(), t.port.c_str(), why.c_str());
            r.bad++;
            checks++;
            failures++;
            continue;
        }

        r.ports++;

        /* As many as the channel holds, so none is stolen: a stolen voice
           still sounds, and the reference below cannot see it. */
        const int voices = chokes(synth, 0) ? 1 : voicesFor(synth, 0);

        vector<int> order;

        for (int n = 0; n < voices; n++)
            order.push_back(n);

        if (voices == 3)
            order = { 0, 2, 1 };

        for (int n : order)
            synth.addNote(0, (float)NOTES[n], 100);

        synth.process();

        thMidiChan *c = synth.getChannel(0);
        thProbe *probe = synth.probe(TH_HOVER_SLOT);
        thProbe::Reading got;

        if (probe == NULL || !probe->reading(got))
        {
            printf("FAIL  %s: hover on %s.%s read nothing\n", file,
                   t.arm.c_str(), t.port.c_str());
            r.bad++;
            checks++;
            failures++;
            continue;
        }

        /* The newest is the last struck; the voices, those whose tree has
           the arg, as the tap counts them. */
        const thArg *newest = NULL;
        int heard = 0;

        for (int n = 0; c != NULL && n < voices; n++)
        {
            thMidiNote *note = c->getNote(NOTES[n]);
            thNode *nd = note ? note->synthTree()->findNode(t.ref) : NULL;
            const thArg *a = nd ? nd->getArg(t.port) : NULL;

            if (a == NULL)
                continue;

            heard++;

            if (n == order.back())
                newest = a;
        }

        const int windowlen = synth.getWindowlen();
        float last = 0, lo = 0, hi = 0;

        if (newest != NULL)
        {
            last = lo = hi = (*newest)[windowlen - 1];

            for (int i = 0; i < windowlen; i++)
            {
                lo = std::min(lo, (*newest)[i]);
                hi = std::max(hi, (*newest)[i]);
            }
        }

        /* The last sample bitwise, as the sweep above compares. The range by
           value: which of -0 and +0 a window's minimum is depends on where
           the scan starts. */
        auto equal = [] (float x, float y)
        {
            return x == y || (std::isnan(x) && std::isnan(y));
        };
        const bool same = got.voices == heard &&
            memcmp(&got.last, &last, sizeof(float)) == 0 &&
            equal(got.lo, lo) && equal(got.hi, hi);

        ok(same, "%s: hover on %s.%s read %g (%g to %g) of %d voices, "
           "wanted %g (%g to %g) of %d", file, t.arm.c_str(), t.port.c_str(),
           (double)got.last, (double)got.lo, (double)got.hi, got.voices,
           (double)last, (double)lo, (double)hi, heard);

        if (!same)
            r.bad++;
        else if (!quiet)
            printf("ok    %-28s hover %s.%s\n", file, t.arm.c_str(),
                   t.port.c_str());
    }

    return r;
}

/* ---- the properties that need saying once, not per file ---- */

/* A tree whose plugin declares state, so the refusal has something to refuse.
   Returns node and arg names, or false if this .dsp has none. */
bool findStateArg (thSynthTree *tree, string &node, string &arg)
{
    const thSynthTree::NodeMap &nodes = tree->nodes();

    for (thSynthTree::NodeMap::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
        thNode *n = i->second;

        if (n == NULL || n->plugin() == NULL)
            continue;

        thPlugin *plug = n->plugin();

        for (int k = 0; k < plug->argCount(); k++)
        {
            if (plug->getArgDir(k) != thPlugin::ARG_STATE)
                continue;

            /* Only if the node actually carries it: buildArgMap creates every
               registered arg, so it should, but asserting on an arg that is
               not there would test the wrong thing. */
            if (n->getArg(plug->getArgName(k)) == NULL)
                continue;

            node = n->name();
            arg = plug->getArgName(k);

            return true;
        }
    }

    return false;
}

/* A node arg holding a plain constant, for the scalar case. */
bool findScalarArg (thSynthTree *tree, string &node, string &arg, float &value)
{
    const thSynthTree::NodeMap &nodes = tree->nodes();

    for (thSynthTree::NodeMap::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
        thNode *n = i->second;

        if (n == NULL || n->plugin() == NULL)
            continue;

        const thArgMap &args = n->args();

        for (thArgMap::const_iterator j = args.begin(); j != args.end(); ++j)
        {
            thArg *a = j->second;

            if (a == NULL || a->type() != thArg::ARG_VALUE || a->len() != 1)
                continue;

            if ((*a)[0] == 0.0f)
                continue;   /* zero would not distinguish a working tap */

            /* Must not be something the plugin writes, or it will not still
               be a constant by the time it is read. */
            const int idx = a->index();

            if (idx >= 0 && idx < n->plugin()->argCount() &&
                n->plugin()->getArgDir(idx) != thPlugin::ARG_IN)
                continue;

            node = n->name();
            arg = a->name();
            value = (*a)[0];

            return true;
        }
    }

    return false;
}

void properties (const string &pluginPath, const char *file)
{
    printf("== properties (against %s)\n", file);

    string stateNode, stateArg, scalarNode, scalarArg;
    float scalarValue = 0;
    bool haveState = false, haveScalar = false;

    {
        thSynth look(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
        thSynthTree *tree = look.parseTree(file);

        if (tree == NULL)
        {
            printf("FAIL  %s will not load; no properties checked\n", file);
            checks++;
            failures++;
            return;
        }

        haveState = findStateArg(tree, stateNode, stateArg);
        haveScalar = findScalarArg(tree, scalarNode, scalarArg, scalarValue);

        delete tree;
    }

    /* -- plugin state is not a signal -- */
    {
        thSynth s(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        if (s.loadTree(file, 0, 100) != NULL && haveState)
        {
            string why;

            ok(s.armProbe(0, stateNode, stateArg, why) < 0,
               "arming %s.%s (plugin state) should be refused",
               stateNode.c_str(), stateArg.c_str());
        }
        else if (!haveState)
        {
            printf("      (no ARG_STATE arg in %s; skipping that check)\n",
                   file);
        }
    }

    /* -- a nonexistent node and arg -- */
    {
        thSynth s(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        s.loadTree(file, 0, 100);

        string why;

        ok(s.armProbe(0, "no_such_node_at_all", "out", why) < 0,
           "arming a node that does not exist is refused");
        ok(s.armProbe(0, "ionode", "no_such_arg_at_all", why) < 0,
           "arming an arg that does not exist is refused");
        ok(s.armProbe(9999, "ionode", "out0", why) < 0,
           "arming a channel out of range is refused");

        thSynth empty(pluginPath, TH_DEFAULT_WINDOW_LENGTH,
                      TH_DEFAULT_SAMPLES);

        ok(empty.armProbe(0, "ionode", "out0", why) < 0,
           "arming a channel with nothing loaded is refused");
    }

    /* -- a scalar arg reads as its constant, times the voice count -- */
    if (haveScalar)
    {
        thSynth s(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        s.loadTree(file, 0, 100);

        string why;
        const int slot = s.armProbe(0, scalarNode, scalarArg, why);

        ok(slot >= 0, "a constant arg (%s.%s) can be probed: %s",
           scalarNode.c_str(), scalarArg.c_str(), why.c_str());

        /* Fetched once and checked rather than dereferenced inside the
           loop -- see the note on the same shape further down, and the
           port sweep above that this copies. */
        thProbe *const probe = (slot < 0) ? NULL : s.probe(slot);

        if (probe != NULL)
        {
            for (int n = 0; n < NUM_NOTES; n++)
                s.addNote(0, (float)NOTES[n], 100);

            const int windowlen = s.getWindowlen();
            vector<float> buf(windowlen);

            bool flat = true;
            unsigned int got = 0;

            for (int w = 0; w < 4 && got == 0; w++)
            {
                s.process();
                got = probe->read(&buf[0], (unsigned int)windowlen);
            }

            const float want = scalarValue * NUM_NOTES;

            for (int i = 0; i < windowlen; i++)
                if (buf[i] != want)
                    flat = false;

            ok(got > 0 && flat,
               "a length-1 arg reads as its constant across the window "
               "(%s.%s: wanted %g x %d, got %g)", scalarNode.c_str(),
               scalarArg.c_str(), (double)scalarValue, NUM_NOTES,
               got ? (double)buf[0] : 0.0);
        }
    }
    else
    {
        printf("      (no constant input arg in %s; skipping that check)\n",
               file);
    }

    /* -- slots, and re-arming the same point -- */
    {
        thSynth s(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        s.loadTree(file, 0, 100);

        vector<PortRef> ports;

        {
            thSynthTree *tree = s.parseTree(file);
            NodeGraph g;

            g.build(tree);
            delete tree;
            outputPorts(g, ports);
        }

        if ((int)ports.size() > TH_MAX_PROBES)
        {
            string why;
            int armed = 0;

            for (int i = 0; i < TH_MAX_PROBES + 2; i++)
            {
                if (s.armProbe(0, ports[i].arm, ports[i].port, why) >= 0)
                    armed++;
            }

            ok(armed == TH_MAX_PROBES,
               "%d distinct points arm and the rest are refused (got %d)",
               TH_MAX_PROBES, armed);

            const int hover = s.armHover(0, ports[0].arm, ports[0].port, why);
            int still = 0;

            for (int i = 0; i < TH_MAX_PROBES; i++)
                if (s.probe(i) != NULL && s.probe(i)->nodeName() == ports[i].arm &&
                    s.probe(i)->argName() == ports[i].port)
                    still++;

            ok(hover == TH_HOVER_SLOT && still == TH_MAX_PROBES,
               "a hover arms with every slot taken (%d) and moves none of "
               "the %d (%d still where they were)", hover, TH_MAX_PROBES,
               still);
        }

        thSynth s2(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        s2.loadTree(file, 0, 100);

        if (!ports.empty())
        {
            string why;
            const int a = s2.armProbe(0, ports[0].arm, ports[0].port, why);
            const int b = s2.armProbe(0, ports[0].arm, ports[0].port, why);

            ok(a >= 0 && a == b,
               "arming the same point twice reuses its slot (%d, %d)", a, b);

            s2.disarmProbe(a);
            s2.process();          /* let the audio side apply the disarm */

            ok(s2.probe(a) == NULL, "disarming clears the slot");
        }
    }

    /* -- decaying voices are summed too --
     *
     * The corpus comparison above never reaches thMidiChan's second note loop.
     * A released note stays in notes_ until its `play' arg falls to zero, so
     * releasing one does not put it in decaying_; the only things that do are
     * the polyphony limiter and re-triggering a note that is already sounding.
     *
     * So: hold three notes, then strike 60 again. insertNote moves the old 60
     * into decaying_ and installs a new one, leaving four voices -- three held
     * and one decaying. `ionode.note' carries each voice's note number
     * unchanged, so the tap on it is an exact voice census: 60+64+67+60 = 251
     * with the decaying loop, 191 without it.
     *
     * Deleting the accumulate from the second loop turns this into 191 and
     * fails, which is the only reason it is written this way rather than as
     * "the sum got bigger". */
    {
        thSynth s(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        s.loadTree(file, 0, 100);

        string why;
        const int slot = s.armProbe(0, "ionode", "note", why);

        /* Fetched once and checked, as the port sweep above does. probe()
           is NULL for a slot that is not armed, and the two loops below
           dereference it without ever saying so -- at -O3 gcc inlines
           thProbe::read here, reasons about the NULL it was never told
           could not happen, and reports the atomic loads inside
           thSampleRing as writes "at address zero". Nothing between the
           arm and the last read reloads the channel, which is the only
           thing that retires a probe, so one fetch is enough. */
        thProbe *const probe = (slot < 0) ? NULL : s.probe(slot);

        if (probe == NULL)
        {
            printf("      (cannot probe ionode.note on %s: %s)\n", file,
                   why.c_str());
        }
        else
        {
            for (int n = 0; n < NUM_NOTES; n++)
                s.addNote(0, (float)NOTES[n], 100);

            const int windowlen = s.getWindowlen();
            vector<float> buf(windowlen);

            /* Settle, and drain what the three held voices published. */
            for (int w = 0; w < 3; w++)
            {
                s.process();
                probe->read(&buf[0], (unsigned int)windowlen);
            }

            const float held = buf[0];

            s.addNote(0, (float)NOTES[0], 100);   /* re-strike 60 */

            float withDecaying = held;

            for (int w = 0; w < 3; w++)
            {
                s.process();

                if (probe->read(&buf[0], (unsigned int)windowlen))
                    withDecaying = buf[0];
            }

            const float wantHeld = (float)(NOTES[0] + NOTES[1] + NOTES[2]);
            const float wantBoth = wantHeld + (float)NOTES[0];

            ok(held == wantHeld,
               "three held voices sum to %g on ionode.note (got %g)",
               (double)wantHeld, (double)held);

            ok(withDecaying == wantBoth,
               "a re-struck note leaves a decaying voice that is still summed:"
               " wanted %g, got %g", (double)wantBoth, (double)withDecaying);
        }
    }

    /* -- the hover reads the newest voice --
     *
     * ionode.note is each voice's key and ionode.velocity its velocity, so
     * they name which voice was read. Struck 67, 64, 60, the newest is the
     * one the channel runs first; then 64 again, softer, which leaves the old
     * 64 decaying and the new one newest. */
    {
        thSynth s(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        s.loadTree(file, 0, 100);

        string why;
        const int slot = s.armHover(0, "ionode", "note", why);
        thProbe::Reading r = { -1, -1, -1, -1, -1 };

        ok(slot == TH_HOVER_SLOT, "a hover on ionode.note arms: %s",
           why.c_str());

        s.process();

        const bool silent = s.probe(TH_HOVER_SLOT) != NULL &&
                            s.probe(TH_HOVER_SLOT)->reading(r);

        ok(silent && r.voices == 0,
           "with nothing sounding a hover reads no voices (%d)", r.voices);

        for (int n = NUM_NOTES - 1; n >= 0; n--)
            s.addNote(0, (float)NOTES[n], 100);

        s.process();
        s.probe(TH_HOVER_SLOT)->reading(r);

        ok(r.last == NOTES[0] && r.lo == NOTES[0] && r.hi == NOTES[0] &&
           r.voices == NUM_NOTES,
           "the newest of %d voices, %d, is the one read: %g of %d",
           NUM_NOTES, NOTES[0], (double)r.last, r.voices);

        const float soft = 50;

        s.addNote(0, (float)NOTES[1], soft);
        s.armHover(0, "ionode", "velocity", why);
        s.process();
        s.probe(TH_HOVER_SLOT)->reading(r);

        ok(r.last == soft * TH_MAX / MIDIVALMAX && r.voices == NUM_NOTES + 1,
           "a re-struck %d is newest, its old voice still counted: velocity "
           "%g of %d", NOTES[1], (double)r.last, r.voices);
    }

    /* -- a voice from the pool is as new as any --
     *
     * 67 and 64 struck, 67 let go until its voice is retired, then 60: the
     * channel builds it from 67's old voice, which has to be newer than 64.
     * On a graph of its own, whose release ends. */
    {
        thSynth s(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        s.loadTreeText("short", "node env env::adsr { a = 1 ms; d = 1 ms; "
                       "s = 0.5; r = 1 ms; trigger = ionode->trigger; };\n"
                       "node ionode { channels = 1; out0 = env->out; "
                       "play = env->play; };\nio ionode;\n", 0, 100);
        s.setVoicePool(true);

        string why;
        thProbe::Reading r = { -1, -1, -1, -1, -1 };

        s.addNote(0, (float)NOTES[2], 100);
        s.addNote(0, (float)NOTES[1], 100);
        s.process();
        s.delNote(0, (float)NOTES[2]);

        for (int w = 0; w < 16 && s.getChannel(0)->getNote(NOTES[2]); w++)
            s.process();

        s.addNote(0, (float)NOTES[0], 100);
        s.armHover(0, "ionode", "note", why);
        s.process();
        s.probe(TH_HOVER_SLOT)->reading(r);

        ok(s.getChannel(0)->restarts() > 0 && r.voices == 2 &&
           r.last == NOTES[0],
           "%d, from the pool (%lu restarts), is newer than %d: read %g of "
           "%d voices", NOTES[0], s.getChannel(0)->restarts(), NOTES[1],
           (double)r.last, r.voices);
    }

    /* -- a reload disarms what pointed at the old tree -- */
    {
        thSynth s(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        s.loadTree(file, 0, 100);

        vector<PortRef> ports;

        {
            thSynthTree *tree = s.parseTree(file);
            NodeGraph g;

            g.build(tree);
            delete tree;
            outputPorts(g, ports);
        }

        if (!ports.empty())
        {
            string why;
            const int slot = s.armProbe(0, ports[0].arm, ports[0].port,
                                        why);

            ok(slot >= 0, "armed before the reload: %s", why.c_str());

            const int hover = s.armHover(0, ports[0].arm, ports[0].port, why);

            ok(hover == TH_HOVER_SLOT, "hovered before the reload: %s",
               why.c_str());

            s.process();
            s.loadTree(file, 0, 100);   /* same file, new tree */
            s.process();

            ok(s.probe(slot) == NULL && s.probe(TH_HOVER_SLOT) == NULL,
               "loading a patch disarms the probes and the hover that pointed "
               "at the old tree");
        }
    }

    /* -- a ring nobody drains counts its drops -- */
    {
        thSynth s(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        s.loadTree(file, 0, 100);

        vector<PortRef> ports;

        {
            thSynthTree *tree = s.parseTree(file);
            NodeGraph g;

            g.build(tree);
            delete tree;
            outputPorts(g, ports);
        }

        if (!ports.empty())
        {
            string why;
            const int slot = s.armProbe(0, ports[0].arm, ports[0].port,
                                        why);

            for (int n = 0; n < NUM_NOTES; n++)
                s.addNote(0, (float)NOTES[n], 100);

            const int runs = TH_PROBE_RING_WINDOWS * 4;

            for (int w = 0; w < runs; w++)
                s.process();

            thProbe *probe = s.probe(slot);

            ok(probe != NULL && probe->dropped() > 0,
               "a ring nobody drains reports dropped samples (%lu)",
               probe ? probe->dropped() : 0);

            /* The count of windows offered has to keep advancing even while
               they are being thrown away, or a caller cannot tell a probe
               that is silent from one nothing is feeding. */
            ok(probe != NULL && probe->windows() >= (unsigned long)(runs - 1),
               "every window is counted whether or not it fitted (%lu of %d)",
               probe ? probe->windows() : 0, runs);

            /* And what does come out is still a whole window, in order --
               never the tail of one spliced onto the head of another. */
            const int windowlen = s.getWindowlen();
            vector<float> buf(windowlen);

            ok(probe != NULL &&
               probe->read(&buf[0], (unsigned int)windowlen) ==
                   (unsigned int)windowlen,
               "and a full window can still be read out of it");
        }
    }
}

} /* namespace */

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;
    int windows = 8;
    bool quiet = false;
    int firstFile = -1;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p")) { if (++i >= argc) return 2; pluginPath = argv[i]; }
        else if (!strcmp(argv[i], "-w")) { if (++i >= argc) return 2; windows = atoi(argv[i]); }
        else if (!strcmp(argv[i], "-q")) quiet = true;
        else { firstFile = i; break; }
    }

    if (firstFile < 0)
    {
        printf("usage: %s [-p PATH] [-w N] [-q] file.dsp ...\n", argv[0]);
        return 2;
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    int files = 0, skipped = 0, ports = 0, matched = 0, silent = 0;
    int hovered = 0;

    for (int f = firstFile; f < argc; f++)
    {
        const Result r = checkFile(pluginPath, argv[f], windows, quiet);

        if (r.bad < 0 && r.ports == 0)
        {
            skipped++;
            continue;
        }

        hovered += checkHover(pluginPath, argv[f], quiet).ports;

        files++;
        ports += r.ports;
        matched += r.matched;
        silent += r.silent;
    }

    /* The properties want one file, it has to be one that loaded, and it has
       to be one that plays all three notes: the scalar case reads a constant
       summed across the voices and compares it against the constant times
       three. A `mono = 1' graph would answer with one. */
    for (int f = firstFile; f < argc; f++)
    {
        thSynth look(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        if (look.loadTree(argv[f], 0, 100) == NULL)
            continue;

        if (voicesFor(look, 0) < NUM_NOTES)
            continue;

        properties(pluginPath, argv[f]);
        break;
    }

    printf("\n%d files, %d skipped (would not load)\n", files, skipped);
    printf("  %d output ports probed: %d carried signal, %d were silent\n",
           ports, matched, silent);
    printf("  %d ports hovered\n", hovered);
    printf("  %d checks, %d failed\n", checks, failures);

    return failures;
}
