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
 *   4. build one real patch -- oscillator into the output -- wire it up, and
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

    int added = 0, restored = 0, unloadable = 0, portless = 0;

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
                NodeCatalog::suggestName(list[e].category, list[e].name, taken);

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

    /* ---- 4. a patch that actually makes a sound ---- */

    remove(scratch.c_str());

    if (NodeEdit::createFile(scratch, "beep", "dspnew", why) != NodeEdit::OK)
    { printf("FAIL  createFile: %s\n", why.c_str()); return 1; }

    /* An oscillator needs a frequency; midi2freq turns the note into one. */
    if (NodeEdit::addNode(scratch, "freq", "misc::midi2freq", why) != NodeEdit::OK ||
        NodeEdit::addNode(scratch, "osc", "osc::simple", why) != NodeEdit::OK)
    { printf("FAIL  building the patch: %s\n", why.c_str()); return 1; }

    if (NodeEdit::connect(scratch, "freq", "note", "ionode", "note", why) != NodeEdit::OK ||
        NodeEdit::connect(scratch, "osc", "freq", "freq", "out", why) != NodeEdit::OK ||
        NodeEdit::setValue(scratch, "osc", "amp", 8000, why) != NodeEdit::OK ||
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
