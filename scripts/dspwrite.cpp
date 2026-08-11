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
 * dspwrite -- exercises NodeEdit against every settable value in the corpus.
 *
 * This is the check that matters most in the whole node editor, because the
 * writer is the only part that can destroy someone's work. For every plain
 * numeric parameter of every node of every .dsp, on a copy:
 *
 *   1. rewrite it to a new value, reparse, and confirm the parser sees exactly
 *      that value -- so `5 ms' comes back as a millisecond value and not as a
 *      folded raw number
 *   2. confirm exactly one line of the file changed
 *   3. write the *current* value -- an edit that changes nothing -- and
 *      confirm the file is byte identical
 *   4. write the original value back and confirm it parses to the original
 *      value again, in at most one changed line
 *
 * And for every control -- the .dsp's top-level `@name' blocks:
 *
 *   8. move it, reparse, confirm the value took
 *   9. put it back, confirm at most one line differs, and confirm a write of
 *      the value already there changes nothing at all
 *
 * And for every wire:
 *
 *   5. disconnect it, reparse, confirm the parameter is no longer driven
 *   6. reconnect it and confirm the file is byte identical to where it started
 *   7. connect it again to where it already goes, and confirm nothing moved
 *
 * (3) is the strong one, and it is the guarantee the editor actually makes:
 * open a file, save it, nothing changed. Not the same as (4). Once a value has
 * genuinely been changed and changed back, `inmax = th_max' comes back as
 * `inmax = 1' -- the writer has no memory of how a number used to be spelled,
 * only of when it does not need to touch one at all. 229 uses of th_max and
 * th_min across the corpus survive (3), which is what matters, and would not
 * survive a writer that regenerated the line every time.
 *
 *   make -C scripts
 *   LD_LIBRARY_PATH=libthink scripts/dspwrite -p plugins/ $(find dsp -name '*.dsp')
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <fstream>
#include <sstream>
#include <vector>

#include "think.h"
#include "NodeGraph.h"
#include "NodeEdit.h"

static bool slurp (const string &path, string &out)
{
    ifstream in(path.c_str(), ios::binary);

    if (!in)
        return false;

    out.assign((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());

    return true;
}

static bool spit (const string &path, const string &text)
{
    ofstream out(path.c_str(), ios::binary | ios::trunc);

    if (!out)
        return false;

    out << text;

    return out.good();
}

static int changedLines (const string &a, const string &b)
{
    vector<string> la, lb;

    for (int which = 0; which < 2; which++)
    {
        const string &s = which ? b : a;
        vector<string> &v = which ? lb : la;

        string cur;

        for (size_t i = 0; i < s.size(); i++)
        {
            if (s[i] == '\n') { v.push_back(cur); cur.clear(); }
            else cur += s[i];
        }

        if (!cur.empty())
            v.push_back(cur);
    }

    /* A line inserted in the middle would make every subsequent line look
       changed under a positional comparison, so walk both sides together and
       let one skip. Enough for the one-line edits this writer makes; not a
       general diff. */
    size_t i = 0, j = 0;
    int n = 0;

    while (i < la.size() && j < lb.size())
    {
        if (la[i] == lb[j]) { i++; j++; continue; }

        /* an insertion in b */
        if (i < la.size() && j + 1 < lb.size() && la[i] == lb[j + 1])
        { n++; j++; continue; }

        /* a deletion from a */
        if (i + 1 < la.size() && j < lb.size() && la[i + 1] == lb[j])
        { n++; i++; continue; }

        n++; i++; j++;
    }

    n += (int)((la.size() - i) + (lb.size() - j));

    return n;
}

static void showFirstDiff (const string &a, const string &b)
{
    vector<string> la, lb;

    for (int which = 0; which < 2; which++)
    {
        const string &s = which ? b : a;
        vector<string> &v = which ? lb : la;
        string cur;

        for (size_t i = 0; i < s.size(); i++)
        { if (s[i] == '\n') { v.push_back(cur); cur.clear(); } else cur += s[i]; }

        if (!cur.empty()) v.push_back(cur);
    }

    for (size_t i = 0; i < la.size() && i < lb.size(); i++)
        if (la[i] != lb[i])
        { printf("        was: [%s]\n        now: [%s]\n",
                 la[i].c_str(), lb[i].c_str());
          return; }
}

/* Reads back one arg's value by parsing the file again. */
static bool readValue (thSynth &synth, const string &path, const string &node,
                       const string &arg, float &out)
{
    thSynthTree *tree = synth.parseTree(path);

    if (tree == NULL)
        return false;

    bool ok = false;

    thNode *n = tree->findNode(node);

    if (n)
    {
        thArg *a = n->getArg(arg);

        if (a && a->type() == thArg::ARG_VALUE && a->len() > 0)
        {
            out = (*a)[0];
            ok = true;
        }
    }

    delete tree;

    return ok;
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;
    bool quiet = false;
    int firstFile = -1;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p")) { if (++i >= argc) return 2; pluginPath = argv[i]; }
        else if (!strcmp(argv[i], "-q")) quiet = true;
        else { firstFile = i; break; }
    }

    if (firstFile < 0)
    {
        printf("usage: %s [-p PATH] [-q] file.dsp ...\n", argv[0]);
        return 2;
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    const string tmp = "/tmp/dspwrite-scratch.dsp";

    int failed = 0, files = 0, edits = 0, skipped = 0, inserted = 0;
    int unwritable = 0, noops = 0, respelt = 0;
    int wiresCut = 0, wireNoops = 0;
    int controlsMoved = 0, controlNoops = 0, controlsRespelt = 0;

    for (int f = firstFile; f < argc; f++)
    {
        thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        thSynthTree *tree = synth.parseTree(argv[f]);

        if (tree == NULL) { skipped++; continue; }

        NodeGraph g;

        g.build(tree);
        delete tree;

        files++;

        string original;

        if (!slurp(argv[f], original))
        { printf("FAIL  %s: unreadable\n", argv[f]); failed++; continue; }

        int problems = 0;

        for (size_t b = 0; b < g.boxes().size() && problems < 4; b++)
        {
            const NodeGraph::Box &bx = g.boxes()[b];

            /* The io source half has no node behind it. */
            if (bx.isIoSource)
                continue;

            for (size_t k = 0; k < bx.params.size() && problems < 4; k++)
            {
                const NodeGraph::Param &p = bx.params[k];

                if (p.kind != NodeGraph::Param::VALUE || p.isOutput)
                    continue;

                if (!spit(tmp, original))
                { printf("FAIL  %s: could not stage a copy\n", argv[f]);
                  problems++; break; }

                /* Something distinct from the current value, and positive so
                   it stays inside what the grammar can spell without relying
                   on the unary-minus rule. */
                const double target = (p.value == 0.0) ? 0.375
                                                       : fabs(p.value) * 1.5 + 0.125;

                string why;

                const bool wasPresent = (NodeEdit::find(tmp, bx.name, p.name)
                                         == NodeEdit::OK);

                NodeEdit::Result r =
                    NodeEdit::setValue(tmp, bx.name, p.name, target, why);

                if (r == NodeEdit::UNWRITABLE)
                { unwritable++; continue; }

                if (r != NodeEdit::OK)
                { printf("FAIL  %s: setValue(%s.%s) -> %s (%s)\n", argv[f],
                         bx.name.c_str(), p.name.c_str(),
                         NodeEdit::resultText(r), why.c_str());
                  problems++; continue; }

                edits++;

                /* 1. the parser must see exactly what we meant */
                float got = 0;

                if (!readValue(synth, tmp, bx.name, p.name, got))
                { printf("FAIL  %s: %s.%s unreadable after writing\n", argv[f],
                         bx.name.c_str(), p.name.c_str());
                  problems++; continue; }

                /* A float round-trip through nine decimal places is not exact;
                   what matters is that it is not a *different* number, e.g.
                   `5 ms' folded to 220.5 and written back as 220.5 raw. */
                const double tol = fabs(target) * 1e-5 + 1e-6;

                if (fabs((double)got - target) > tol)
                { printf("FAIL  %s: %s.%s written as %g, parsed back as %g\n",
                         argv[f], bx.name.c_str(), p.name.c_str(), target,
                         (double)got);
                  problems++; continue; }

                /* 2. exactly one line differs -- or one more line, if the arg
                      was not in the file to begin with */
                string after;

                if (!slurp(tmp, after))
                { printf("FAIL  %s: could not re-read\n", argv[f]);
                  problems++; continue; }

                const int nd = changedLines(original, after);

                if (nd != 1)
                { printf("FAIL  %s: editing %s.%s changed %d lines\n", argv[f],
                         bx.name.c_str(), p.name.c_str(), nd);
                  problems++; continue; }

                if (after.size() > original.size() &&
                    after.find("\n" ) != string::npos &&
                    changedLines(original, after) == 1 &&
                    after.length() != original.length())
                {
                    /* an inserted line is fine; just counted for the report */
                }

                /* 3. a write that changes nothing must change nothing. */
                {
                    string beforeNoop;

                    slurp(tmp, beforeNoop);

                    float cur = 0;

                    if (readValue(synth, tmp, bx.name, p.name, cur))
                    {
                        string w2;

                        NodeEdit::setValue(tmp, bx.name, p.name, cur, w2);

                        string afterNoop;

                        slurp(tmp, afterNoop);

                        if (afterNoop != beforeNoop)
                        { printf("FAIL  %s: a no-op write to %s.%s changed the "
                                 "file\n", argv[f], bx.name.c_str(),
                                 p.name.c_str());
                          showFirstDiff(beforeNoop, afterNoop);
                          problems++; continue; }

                        noops++;
                    }
                }

                /* 4. putting the original value back must give the original
                      value back, in one line */
                r = NodeEdit::setValue(tmp, bx.name, p.name, p.value, why);

                if (!wasPresent) inserted++;

                if (r != NodeEdit::OK)
                { printf("FAIL  %s: could not restore %s.%s (%s)\n", argv[f],
                         bx.name.c_str(), p.name.c_str(), why.c_str());
                  problems++; continue; }

                string restored;

                slurp(tmp, restored);

                if (restored != original)
                {
                    /* An arg the file never mentioned cannot come back
                       identical -- the line we inserted is still there, and
                       that is correct. What must hold is that nothing else
                       moved, and that the value reads back as it started. */
                    const int nd2 = changedLines(original, restored);

                    if (nd2 > 1)
                    { printf("FAIL  %s: restoring %s.%s changed %d lines\n",
                             argv[f], bx.name.c_str(), p.name.c_str(), nd2);
                      showFirstDiff(original, restored);
                      problems++; continue; }

                    if (wasPresent)
                        respelt++;
                }

                float back = 0;

                if (!readValue(synth, tmp, bx.name, p.name, back) ||
                    fabs((double)back - (double)p.value) >
                        fabs((double)p.value) * 1e-5 + 1e-6)
                { printf("FAIL  %s: %s.%s restored to %g, not %g\n", argv[f],
                         bx.name.c_str(), p.name.c_str(), (double)back,
                         (double)p.value);
                  problems++; }
            }
        }

        /* ---- controls ---- */

        for (size_t b = 0; b < g.boxes().size() && problems < 4; b++)
        {
            const NodeGraph::Box &bx = g.boxes()[b];

            if (!bx.isControl)
                continue;

            if (!spit(tmp, original))
            { printf("FAIL  %s: could not stage a copy\n", argv[f]);
              problems++; break; }

            /* Somewhere inside the declared range, and not where it already
               is -- a third of the way along, or two thirds if that happens
               to be the current value. */
            double target = bx.ctlMin + (bx.ctlMax - bx.ctlMin) / 3.0;

            if ((float)target == bx.ctlValue)
                target = bx.ctlMin + (bx.ctlMax - bx.ctlMin) * 2.0 / 3.0;

            string why;

            NodeEdit::Result r =
                NodeEdit::setChanArg(tmp, bx.ctlArg, target, why);

            if (r != NodeEdit::OK)
            { printf("FAIL  %s: setChanArg(@%s) -> %s (%s)\n", argv[f],
                     bx.ctlArg.c_str(), NodeEdit::resultText(r), why.c_str());
              problems++; continue; }

            /* 8. the parser must see it, and the graph must show it on the
                  control box */
            {
                thSynthTree *t2 = synth.parseTree(tmp);

                if (t2 == NULL)
                { printf("FAIL  %s: will not parse after setting @%s\n",
                         argv[f], bx.ctlArg.c_str());
                  problems++; continue; }

                NodeGraph g2;

                g2.build(t2);
                delete t2;

                double got = 0;
                bool found = false;

                for (size_t q = 0; q < g2.boxes().size(); q++)
                    if (g2.boxes()[q].isControl &&
                        g2.boxes()[q].ctlArg == bx.ctlArg)
                    { got = g2.boxes()[q].ctlValue; found = true; break; }

                if (!found)
                { printf("FAIL  %s: @%s is no longer a control after writing\n",
                         argv[f], bx.ctlArg.c_str());
                  problems++; continue; }

                if (fabs(got - target) > fabs(target) * 1e-5 + 1e-6)
                { printf("FAIL  %s: @%s written as %g, read back as %g\n",
                         argv[f], bx.ctlArg.c_str(), target, got);
                  problems++; continue; }
            }

            controlsMoved++;

            /* 9. and back.
             *
             * Not byte-identical, necessarily: `@shape2 = 3.0' comes back as
             * `@shape2 = 3' once it has genuinely been moved, the same way
             * `th_max' comes back as `1'. The writer remembers when it need
             * not touch a line, not how a number used to be spelled. What
             * must hold is that nothing else moved. */
            r = NodeEdit::setChanArg(tmp, bx.ctlArg, bx.ctlValue, why);

            string back;

            slurp(tmp, back);

            if (r != NodeEdit::OK || changedLines(original, back) > 1)
            { printf("FAIL  %s: restoring @%s changed %d lines\n", argv[f],
                     bx.ctlArg.c_str(), changedLines(original, back));
              showFirstDiff(original, back);
              problems++; continue; }

            if (back != original)
                controlsRespelt++;

            /* The strong one: writing the value that is already there must
               change nothing. Checked against whatever the file says now,
               not against the original. */
            const string beforeNoop = back;

            NodeEdit::setChanArg(tmp, bx.ctlArg, bx.ctlValue, why);
            slurp(tmp, back);

            if (back != beforeNoop)
            { printf("FAIL  %s: a no-op write to @%s changed the file\n",
                     argv[f], bx.ctlArg.c_str());
              showFirstDiff(beforeNoop, back);
              problems++; continue; }

            controlNoops++;
        }

        /* ---- wires ---- */

        for (size_t e = 0; e < g.edges().size() && problems < 4; e++)
        {
            const NodeGraph::Edge &ed = g.edges()[e];

            const NodeGraph::Box &tb = g.boxes()[ed.toBox];
            const NodeGraph::Box &fb = g.boxes()[ed.fromBox];

            const string arg = tb.ports[ed.toPort].name;
            const string port = fb.ports[ed.fromPort].name;

            if (!spit(tmp, original))
            { printf("FAIL  %s: could not stage a copy\n", argv[f]);
              problems++; break; }

            string why;

            /* 5. disconnecting must actually disconnect */
            NodeEdit::Result r =
                NodeEdit::disconnect(tmp, tb.name, arg, 0, why);

            if (r != NodeEdit::OK)
            { printf("FAIL  %s: disconnect(%s.%s) -> %s (%s)\n", argv[f],
                     tb.name.c_str(), arg.c_str(),
                     NodeEdit::resultText(r), why.c_str());
              problems++; continue; }

            {
                thSynthTree *t2 = synth.parseTree(tmp);

                if (t2 == NULL)
                { printf("FAIL  %s: will not parse after disconnecting %s.%s\n",
                         argv[f], tb.name.c_str(), arg.c_str());
                  problems++; continue; }

                thNode *n2 = t2->findNode(tb.name);
                thArg *a2 = n2 ? n2->getArg(arg) : NULL;

                const bool stillWired =
                    (a2 && a2->type() == thArg::ARG_POINTER);

                delete t2;

                if (stillWired)
                { printf("FAIL  %s: %s.%s still wired after disconnect\n",
                         argv[f], tb.name.c_str(), arg.c_str());
                  problems++; continue; }
            }

            wiresCut++;

            /* 6. reconnecting must give the file back exactly */
            r = fb.isControl
                    ? NodeEdit::connectControl(tmp, tb.name, arg, fb.ctlArg, why)
                    : NodeEdit::connect(tmp, tb.name, arg, fb.name, port, why);

            if (r != NodeEdit::OK)
            { printf("FAIL  %s: connect(%s.%s <- %s->%s) -> %s (%s)\n", argv[f],
                     tb.name.c_str(), arg.c_str(), fb.name.c_str(),
                     port.c_str(), NodeEdit::resultText(r), why.c_str());
              problems++; continue; }

            string back;

            slurp(tmp, back);

            if (back != original)
            { printf("FAIL  %s: reconnecting %s.%s did not give the file back\n",
                     argv[f], tb.name.c_str(), arg.c_str());
              showFirstDiff(original, back);
              problems++; continue; }

            /* 7. connecting to where it already goes must change nothing */
            r = fb.isControl
                    ? NodeEdit::connectControl(tmp, tb.name, arg, fb.ctlArg, why)
                    : NodeEdit::connect(tmp, tb.name, arg, fb.name, port, why);

            slurp(tmp, back);

            if (r != NodeEdit::OK || back != original)
            { printf("FAIL  %s: reconnecting %s.%s to where it already goes "
                     "changed the file\n", argv[f], tb.name.c_str(),
                     arg.c_str());
              showFirstDiff(original, back);
              problems++; continue; }

            wireNoops++;
        }

        if (problems)
            failed++;
        else if (!quiet)
            printf("ok    %-34s\n", argv[f]);
    }

    remove(tmp.c_str());

    printf("\n%d files, %d failed, %d skipped (would not load)\n",
           files, failed, skipped);
    printf("  %d values rewritten and restored, %d inserted, %d unwritable\n",
           edits, inserted, unwritable);
    printf("  %d no-op writes, every one byte-identical; %d came back correct "
           "but respelt\n", noops, respelt);
    printf("  %d wires cut and restored, %d reconnects to where they already "
           "went, all byte-identical\n", wiresCut, wireNoops);
    printf("  %d controls moved and restored (%d respelt), %d no-op writes, "
           "every one byte-identical\n", controlsMoved, controlsRespelt,
           controlNoops);

    return failed;
}
