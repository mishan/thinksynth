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
 * dspnew -- can the editor build a working .dsp from nothing?
 *
 * The palette and the New action are only worth having if what they produce
 * loads, renders and can be reopened. So, without a display:
 *
 *   1. create a file and confirm it parses
 *   2. add one node of every plugin in the catalogue, one at a time, each
 *      time confirming the file still parses and the node appears in the
 *      graph with the ports its plugin declares
 *   3. remove each one again and confirm the file comes back to what it was
 *   4. add controls, confirming each becomes a control box with the range and
 *      label it was given, and that removing one restores the file
 *   5. build one real patch -- oscillator into the output -- wire it up, and
 *      render a note, checking something other than silence comes out
 *
 * (4) is the one that matters. Everything else can pass while the result is
 * a file that loads and makes no sound, which is not "authoring".
 *
 *   make -C scripts
 *   LD_LIBRARY_PATH=libthink scripts/dspnew -p plugins/
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <fstream>
#include <vector>

#include "think.h"
#include "NodeGraph.h"
#include "NodeCatalog.h"
#include "NodeEdit.h"

static string slurp (const string &path)
{
    ifstream in(path.c_str(), ios::binary);

    return string((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
}

/* Parses, and reports whether a named node is present with the expected
   number of ports. */
static bool hasNode (thSynth &synth, const string &path, const string &node,
                     int &ports)
{
    thSynthTree *tree = synth.parseTree(path);

    if (tree == NULL)
        return false;

    NodeGraph g;

    g.build(tree);
    delete tree;

    for (size_t b = 0; b < g.boxes().size(); b++)
        if (g.boxes()[b].name == node && !g.boxes()[b].isControl)
        {
            ports = (int)g.boxes()[b].ports.size();
            return true;
        }

    return false;
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;
    bool quiet = false;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p")) { if (++i >= argc) return 2; pluginPath = argv[i]; }
        else if (!strcmp(argv[i], "-q")) quiet = true;
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    const string scratch = "/tmp/dspnew-scratch.dsp";

    int failed = 0;

    /* ---- where the plugins are ----
     *
     * The palette scans whatever the plugin manager resolved to, so this
     * checks the resolution itself: an uninstalled tree keeps its plugins in
     * ./plugins and `make install' has never been run on most checkouts. If
     * this picks the wrong place the palette comes up empty, which is exactly
     * what happened the first time it was run outside the build directory. */
    {
        thSynth probe(pluginPath, TH_DEFAULT_WINDOW_LENGTH,
                      TH_DEFAULT_SAMPLES);

        const string resolved = probe.getPluginManager()->pluginPath();

        printf("plugin root: %s\n", resolved.c_str());

        if (resolved != pluginPath &&
            !(pluginPath.size() && resolved == pluginPath))
            printf("  (asked for %s)\n", pluginPath.c_str());

        pluginPath = resolved;
    }

    /* ---- the catalogue ---- */

    NodeCatalog cat;

    const int found = cat.scan(pluginPath);

    printf("catalogue: %d plugins in %d categories\n",
           found, (int)cat.categories().size());

    if (found == 0)
    {
        printf("FAIL  nothing in %s\n", pluginPath.c_str());
        return 1;
    }

    for (size_t c = 0; c < cat.categories().size(); c++)
        if (!quiet)
            printf("  %-10s %d\n", cat.categories()[c].c_str(),
                   (int)cat.inCategory(cat.categories()[c]).size());

    /* ---- 1. a new file loads ---- */

    remove(scratch.c_str());

    string why;

    if (NodeEdit::createFile(scratch, "scratch", "dspnew", why) != NodeEdit::OK)
    { printf("FAIL  createFile: %s\n", why.c_str()); return 1; }

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    {
        thSynthTree *t = synth.parseTree(scratch);

        if (t == NULL)
        { printf("FAIL  a freshly created .dsp does not parse\n"); return 1; }

        delete t;

        printf("ok    a new .dsp parses\n");
    }

    /* Creating over an existing file must be refused. */
    if (NodeEdit::createFile(scratch, "again", "dspnew", why) == NodeEdit::OK)
    { printf("FAIL  createFile overwrote an existing file\n"); failed++; }

    const string blank = slurp(scratch);

    /* ---- 2 and 3. every plugin, added and removed ---- */

    int added = 0, restored = 0, unloadable = 0, portless = 0, controls = 0;

    thPluginManager *pm = synth.getPluginManager();

    for (size_t c = 0; c < cat.categories().size(); c++)
    {
        const vector<NodeCatalog::Entry> &list =
            cat.inCategory(cat.categories()[c]);

        for (size_t e = 0; e < list.size(); e++)
        {
            NodeCatalog::Entry info;

            if (!cat.describe(list[e].spelling, pm, info))
            {
                /* A plugin that will not load must not be offered, and saying
                   so is more useful than skipping it silently. */
                printf("      %-22s will not load\n", list[e].spelling.c_str());
                unloadable++;
                continue;
            }

            if (info.ports.empty())
                portless++;

            vector<string> taken;
            const string name =
                NodeCatalog::suggestName(list[e].name, taken);

            NodeEdit::Result r =
                NodeEdit::addNode(scratch, name, list[e].spelling, why);

            if (r != NodeEdit::OK)
            { printf("FAIL  addNode(%s): %s\n", list[e].spelling.c_str(),
                     why.c_str());
              failed++; continue; }

            int ports = -1;

            if (!hasNode(synth, scratch, name, ports))
            { printf("FAIL  %s: added but not in the graph\n",
                     list[e].spelling.c_str());
              failed++; }
            else if (ports != (int)info.ports.size())
            { printf("FAIL  %s: graph shows %d ports, plugin declares %d\n",
                     list[e].spelling.c_str(), ports, (int)info.ports.size());
              failed++; }
            else
                added++;

            int refs = 0;

            if (NodeEdit::removeNode(scratch, name, refs, why) != NodeEdit::OK)
            { printf("FAIL  removeNode(%s): %s\n", name.c_str(), why.c_str());
              failed++; continue; }

            if (slurp(scratch) != blank)
            { printf("FAIL  %s: add then remove did not restore the file\n",
                     list[e].spelling.c_str());
              failed++; continue; }

            restored++;
        }
    }

    printf("ok    %d plugins added and removed, file restored each time\n",
           restored);

    if (added != restored)
        printf("      (%d added, %d restored)\n", added, restored);

    if (unloadable)
        printf("      %d plugin(s) would not load\n", unloadable);

    if (portless)
        printf("      %d plugin(s) declare no ports at all\n", portless);

    /* ---- 4. controls ---- */

    {
        struct { const char *name; double v, lo, hi; const char *label; }
        cases[] = {
            { "cutoff",  0.5,  0,    1,     "Cutoff"      },
            { "wide",    3,   -10,   10,    "Wide Range"  },
            { "nolabel", 1,    0,    2,     ""            },
            { "tiny",    0.001, 0,   0.01,  "Tiny"        },
            { NULL, 0, 0, 0, NULL }
        };

        for (int i = 0; cases[i].name; i++)
        {
            const string before = slurp(scratch);

            if (NodeEdit::addControl(scratch, cases[i].name, cases[i].v,
                                     cases[i].lo, cases[i].hi,
                                     cases[i].label, why) != NodeEdit::OK)
            { printf("FAIL  addControl(@%s): %s\n", cases[i].name, why.c_str());
              failed++; continue; }

            /* It has to come back as a control box, with its range -- a
               chanarg without .widget is metadata, not a knob, and would show
               up as nothing at all. */
            thSynthTree *t = synth.parseTree(scratch);

            if (t == NULL)
            { printf("FAIL  @%s: the file no longer parses\n", cases[i].name);
              failed++; continue; }

            NodeGraph g;

            g.build(t);
            delete t;

            bool seen = false;

            for (size_t b = 0; b < g.boxes().size(); b++)
            {
                const NodeGraph::Box &bx = g.boxes()[b];

                if (!bx.isControl || bx.ctlArg != cases[i].name)
                    continue;

                seen = true;

                if (fabs((double)bx.ctlMin - cases[i].lo) > 1e-4 ||
                    fabs((double)bx.ctlMax - cases[i].hi) > 1e-4)
                { printf("FAIL  @%s: range came back %g-%g, not %g-%g\n",
                         cases[i].name, (double)bx.ctlMin, (double)bx.ctlMax,
                         cases[i].lo, cases[i].hi);
                  failed++; }

                if (fabs((double)bx.ctlValue - cases[i].v) > 1e-4)
                { printf("FAIL  @%s: value came back %g, not %g\n",
                         cases[i].name, (double)bx.ctlValue, cases[i].v);
                  failed++; }

                const string wanted =
                    *cases[i].label ? cases[i].label : cases[i].name;

                if (bx.ctlLabel != wanted)
                { printf("FAIL  @%s: label came back `%s', not `%s'\n",
                         cases[i].name, bx.ctlLabel.c_str(), wanted.c_str());
                  failed++; }

                break;
            }

            if (!seen)
            { printf("FAIL  @%s: added but not a control box\n",
                     cases[i].name);
              failed++; continue; }

            controls++;

            int refs = 0;

            if (NodeEdit::removeControl(scratch, cases[i].name, refs, why)
                != NodeEdit::OK)
            { printf("FAIL  removeControl(@%s): %s\n", cases[i].name,
                     why.c_str());
              failed++; continue; }

            if (slurp(scratch) != before)
            { printf("FAIL  @%s: add then remove did not restore the file\n",
                     cases[i].name);
              failed++; continue; }
        }

        /* Groups: an envelope's four sliders declared as one thing. */
        {
            const string g = "/tmp/dspnew-group.dsp";

            remove(g.c_str());

            if (NodeEdit::createFile(g, "grp", "dspnew", why) != NodeEdit::OK ||
                NodeEdit::addNode(g, "env", "env::adsr", why) != NodeEdit::OK)
            { printf("FAIL  group setup: %s\n", why.c_str()); failed++; }
            else
            {
                const char *names[] = { "a", "d", "s", "r", NULL };
                const char *labels[] = { "Attack", "Decay", "Sustain",
                                         "Release" };
                bool ok = true;

                for (int i = 0; names[i]; i++)
                    if (NodeEdit::addControl(g, names[i], 0.25, 0, 1,
                                             labels[i], "Envelope", why)
                            != NodeEdit::OK ||
                        NodeEdit::connectControl(g, "env", names[i], names[i],
                                                 why) != NodeEdit::OK)
                    { printf("FAIL  group control %s: %s\n", names[i],
                             why.c_str());
                      failed++; ok = false; break; }

                thSynthTree *gt = ok ? synth.parseTree(g) : NULL;

                if (ok && gt == NULL)
                { printf("FAIL  a grouped .dsp does not parse\n"); failed++; }
                else if (gt)
                {
                    NodeGraph gg;

                    gg.build(gt);
                    gg.layout();
                    delete gt;

                    int inGroup = 0, heads = 0;

                    for (size_t b = 0; b < gg.boxes().size(); b++)
                    {
                        if (gg.boxes()[b].ctlGroup != "Envelope")
                            continue;

                        inGroup++;

                        if (gg.boxes()[b].groupHead)
                            heads++;
                    }

                    if (inGroup != 4)
                    { printf("FAIL  %d controls in the group, not 4\n",
                             inGroup);
                      failed++; }

                    /* Exactly one heading, or the block is drawn as several
                       blocks that happen to share a name. */
                    if (heads != 1)
                    { printf("FAIL  the group has %d headings, not 1\n",
                             heads);
                      failed++; }

                    if (inGroup == 4 && heads == 1)
                        printf("ok    a group of 4 reads back as one block\n");
                }
            }

            remove(g.c_str());
        }

        /* A group name has the same constraint as a label. */
        if (NodeEdit::addControl(scratch, "bad3", 0, 0, 1, "", "no \"quotes\"",
                                 why) == NodeEdit::OK)
        { printf("FAIL  a group name containing a quote was accepted\n");
          failed++; }

        /* A label the lexer could not read back must be refused, not written:
           the string rule is `"[^"\n]*"' with no escapes at all. */
        if (NodeEdit::addControl(scratch, "bad", 0, 0, 1, "say \"hi\"", why)
            == NodeEdit::OK)
        { printf("FAIL  a label containing a quote was accepted\n");
          failed++; }

        /* And an inverted range. */
        if (NodeEdit::addControl(scratch, "bad2", 0, 1, 0, "", why)
            == NodeEdit::OK)
        { printf("FAIL  max below min was accepted\n"); failed++; }

        printf("ok    %d controls added and removed, file restored each "
               "time\n", controls);
    }

    /* ---- 4b. braces and hashes inside strings ----
     *
     * Every scanner in NodeEdit walks the raw text, and two lexer rules make
     * that harder than it looks. A comment is `#.*$' and a string is
     * `"[^"\n]*"', and flex takes the longest match -- so a `#' inside a
     * string belongs to the string, and a brace inside one is just a
     * character.
     *
     * The code got both wrong: it cut each line at the first `#', and counted
     * every brace it saw. A name containing `{' therefore left findIoLine
     * believing it was still inside a node block, so it never found the `io'
     * line and put the new node *after* it -- a file that no longer parses,
     * because the io node is then referenced before it is defined.
     *
     * No shipped .dsp does this, which is why nothing caught it. */
    {
        const string tricky = "/tmp/dspnew-tricky.dsp";

        remove(tricky.c_str());

        {
            ofstream out(tricky.c_str());

            /* These two lines have to be exactly this shape to be a test. An
               unmatched `{' in the name pushes the naive scanner to depth 1,
               and the `}' that would bring it back sits *after* a `#' in the
               description -- so cutting at the first `#' throws that `}' away
               and the depth never returns to 0. A `{' and a `}' in separate
               strings would cancel out and pass either way. */
            out << "# a patch with awkward strings\n"
                << "name \"brace { in a name\";\n"
                << "author \"hash # in an author\";\n"
                << "description \"hash # and then a closing brace }\";\n"
                << "\n"
                << "node ionode {\n"
                << "    channels = 2;\n"
                << "};\n"
                << "\n"
                << "io ionode;\n";
        }

        if (NodeEdit::addNode(tricky, "osc1", "osc::simple", why) !=
            NodeEdit::OK)
        { printf("FAIL  addNode on a file with braces in strings: %s\n",
                 why.c_str());
          failed++; }
        else
        {
            const string text = slurp(tricky);

            const string::size_type node = text.find("node osc1");
            const string::size_type io = text.find("\nio ionode;");

            if (node == string::npos || io == string::npos || node > io)
            { printf("FAIL  the new node was not placed before the io line\n");
              failed++; }

            int ports = -1;

            if (!hasNode(synth, tricky, "osc1", ports))
            { printf("FAIL  a file with braces in strings stopped parsing "
                     "after addNode\n");
              failed++; }
            else
                printf("ok    braces and hashes in strings do not confuse the "
                       "writer\n");
        }

        remove(tricky.c_str());
    }

    /* ---- 5. a patch that actually makes a sound ---- */

    remove(scratch.c_str());

    if (NodeEdit::createFile(scratch, "beep", "dspnew", why) != NodeEdit::OK)
    { printf("FAIL  createFile: %s\n", why.c_str()); return 1; }

    /* An oscillator needs a frequency; midi2freq turns the note into one. */
    if (NodeEdit::addNode(scratch, "freq", "misc::midi2freq", why) != NodeEdit::OK ||
        NodeEdit::addNode(scratch, "osc", "osc::simple", why) != NodeEdit::OK)
    { printf("FAIL  building the patch: %s\n", why.c_str()); return 1; }

    /* And a control driving something, since that is the point of adding
       one: a patch with a knob on it. */
    if (NodeEdit::addControl(scratch, "level", 6000, 0, 12000, "Level", why)
        != NodeEdit::OK)
    { printf("FAIL  addControl: %s\n", why.c_str()); return 1; }

    if (NodeEdit::connectControl(scratch, "osc", "amp", "level", why)
        != NodeEdit::OK)
    { printf("FAIL  wiring @level: %s\n", why.c_str()); return 1; }

    if (NodeEdit::connect(scratch, "freq", "note", "ionode", "note", why) != NodeEdit::OK ||
        NodeEdit::connect(scratch, "osc", "freq", "freq", "out", why) != NodeEdit::OK ||

        NodeEdit::connect(scratch, "ionode", "out0", "osc", "out", why) != NodeEdit::OK ||
        NodeEdit::connect(scratch, "ionode", "out1", "osc", "out", why) != NodeEdit::OK)
    { printf("FAIL  wiring the patch: %s\n", why.c_str()); return 1; }

    {
        thSynth player(pluginPath, TH_DEFAULT_WINDOW_LENGTH,
                       TH_DEFAULT_SAMPLES);

        if (player.loadTree(scratch, 0, 100) == NULL)
        { printf("FAIL  the built patch does not load\n"); return 1; }

        player.addNote(0, 60, 100);

        const int frame = player.audioChannelCount() * player.getWindowlen();

        double peak = 0;

        for (int w = 0; w < 8; w++)
        {
            player.process();

            const float *buf = player.getOutput();

            for (int i = 0; i < frame; i++)
                if (fabs((double)buf[i]) > peak)
                    peak = fabs((double)buf[i]);
        }

        if (peak <= 0.0)
        { printf("FAIL  the built patch renders silence\n"); failed++; }
        else
            printf("ok    a patch built from nothing renders audio "
                   "(peak %.4f)\n", peak);
    }

    remove(scratch.c_str());

    printf("\n%d failure(s)\n", failed);

    return failed;
}
