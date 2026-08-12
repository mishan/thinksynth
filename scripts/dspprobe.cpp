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
 *   cmake --build build
 *   ./build/scripts/dspprobe -p build/plugins/ dsp/ts1.dsp
 */

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Are all three notes still held?
 *
 * thMidiChan retires a note the moment its `play' arg reaches zero, which
 * happens inside process() -- so from out here the note is already gone while
 * the window it contributed to is the one about to be read. Percussive patches
 * reach that within a few windows (dsp/noargs/bd1.dsp does it in seven), and
 * comparing past it means comparing the tap against a reference that can no
 * longer see what the tap correctly still holds.
 *
 * So the comparison stops at that boundary rather than pretending to cover
 * it. What happens to a probe during a release is the tap's business and it is
 * covered by construction -- the accumulate is inside both note loops -- not
 * by this. */
bool allNotesSounding (thSynth &synth, int chan)
{
    thMidiChan *c = synth.getChannel(chan);

    if (c == NULL)
        return false;

    for (int n = 0; n < NUM_NOTES; n++)
        if (c->getNote(NOTES[n]) == NULL)
            return false;

    return true;
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

/* Every (node, arg) the graph model calls an output port, which is what a
   probe is for. Uses NodeGraph rather than the plugin tables directly so that
   what is checked here is what the editor would offer. */
void outputPorts (const NodeGraph &g, vector<pair<string, string> > &out)
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

            out.push_back(make_pair(bx.name, bx.ports[p].name));
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
    vector<pair<string, string> > ports;

    {
        thSynth look(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
        thSynthTree *tree = look.parseTree(file);

        if (tree == NULL)
        {
            r.bad = -1;   /* would not load; the caller counts it as skipped */
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
        const int slot = synth.armProbe(0, ports[p].first, ports[p].second,
                                        why);

        if (slot < 0)
        {
            /* An output port the tap will not take is a real disagreement
               between the editor and the engine, not a curiosity. */
            printf("FAIL  %s: cannot probe %s.%s -- %s\n", file,
                   ports[p].first.c_str(), ports[p].second.c_str(),
                   why.c_str());
            r.bad++;
            checks++;
            failures++;
            continue;
        }

        r.ports++;

        for (int n = 0; n < NUM_NOTES; n++)
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
            if (!allNotesSounding(synth, 0))
                break;

            thProbe *probe = synth.probe(slot);

            if (probe == NULL)
            {
                printf("FAIL  %s: probe on %s.%s disarmed itself\n", file,
                       ports[p].first.c_str(), ports[p].second.c_str());
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
                       ports[p].first.c_str(), ports[p].second.c_str(), got,
                       windowlen);
                r.bad++;
                checks++;
                failures++;
                break;
            }

            referenceSum(synth, 0, ports[p].first, ports[p].second, windowlen,
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
                           ports[p].first.c_str(), ports[p].second.c_str(), w);
                    mismatch = true;
                    r.bad++;
                    checks++;
                    failures++;
                    break;
                }

                printf("FAIL  %s: %s.%s differs from the reference sum at "
                       "window %d sample %d (%g vs %g)\n", file,
                       ports[p].first.c_str(), ports[p].second.c_str(), w, at,
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
                   ports[p].first.c_str(), ports[p].second.c_str(), windows);
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
                   ports[p].first.c_str(), ports[p].second.c_str(),
                   comparedWindows, anySignal ? "" : "  (silent)");
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

        if (slot >= 0)
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
                got = s.probe(slot)->read(&buf[0], (unsigned int)windowlen);
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

        vector<pair<string, string> > ports;

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
                if (s.armProbe(0, ports[i].first, ports[i].second, why) >= 0)
                    armed++;
            }

            ok(armed == TH_MAX_PROBES,
               "%d distinct points arm and the rest are refused (got %d)",
               TH_MAX_PROBES, armed);
        }

        thSynth s2(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        s2.loadTree(file, 0, 100);

        if (!ports.empty())
        {
            string why;
            const int a = s2.armProbe(0, ports[0].first, ports[0].second, why);
            const int b = s2.armProbe(0, ports[0].first, ports[0].second, why);

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

        if (slot < 0)
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
                s.probe(slot)->read(&buf[0], (unsigned int)windowlen);
            }

            const float held = buf[0];

            s.addNote(0, (float)NOTES[0], 100);   /* re-strike 60 */

            float withDecaying = held;

            for (int w = 0; w < 3; w++)
            {
                s.process();

                if (s.probe(slot)->read(&buf[0], (unsigned int)windowlen))
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

    /* -- a reload disarms what pointed at the old tree -- */
    {
        thSynth s(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        s.loadTree(file, 0, 100);

        vector<pair<string, string> > ports;

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
            const int slot = s.armProbe(0, ports[0].first, ports[0].second,
                                        why);

            ok(slot >= 0, "armed before the reload: %s", why.c_str());

            s.process();
            s.loadTree(file, 0, 100);   /* same file, new tree */
            s.process();

            ok(s.probe(slot) == NULL,
               "loading a patch disarms the probes that pointed at the old "
               "tree");
        }
    }

    /* -- a ring nobody drains counts its drops -- */
    {
        thSynth s(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        s.loadTree(file, 0, 100);

        vector<pair<string, string> > ports;

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
            const int slot = s.armProbe(0, ports[0].first, ports[0].second,
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

    for (int f = firstFile; f < argc; f++)
    {
        const Result r = checkFile(pluginPath, argv[f], windows, quiet);

        if (r.bad < 0 && r.ports == 0)
        {
            skipped++;
            continue;
        }

        files++;
        ports += r.ports;
        matched += r.matched;
        silent += r.silent;
    }

    /* The properties want one file, and it has to be one that loaded. */
    for (int f = firstFile; f < argc; f++)
    {
        thSynth look(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
        thSynthTree *tree = look.parseTree(argv[f]);

        if (tree == NULL)
            continue;

        delete tree;
        properties(pluginPath, argv[f]);
        break;
    }

    printf("\n%d files, %d skipped (would not load)\n", files, skipped);
    printf("  %d output ports probed: %d carried signal, %d were silent\n",
           ports, matched, silent);
    printf("  %d checks, %d failed\n", checks, failures);

    return failures;
}
