/*
 * Copyright (C) 2026 The thinksynth authors
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

/* A misspelled name in a .dsp -- an arg a plugin does not have, an output
 * read that no node writes, an io node name the engine never reads -- loads
 * and plays as 0, so the graph is silent and nothing says why. The parse
 * warns instead, naming the line and the nearest real name. Each row is a
 * graph and the warnings it must give, as substrings, or none at all. */

#include "config.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <unistd.h>

#include "think.h"
#include "thSynth.h"
#include "thSynthTree.h"

using std::string;
using std::vector;

/* What `f' prints to stderr. A file beside the harness rather than
   tmpfile(), which on Windows wants to write to the root of the drive. */
static string
stderrOf (const std::function<void (void)> &f)
{
    const char *path = "namecheck.stderr";
    FILE *to = fopen(path, "w+b");
    string out;

    if (to == NULL)
        return "(could not open " + string(path) + ")";

    fflush(stderr);
    const int saved = dup(fileno(stderr));
    dup2(fileno(to), fileno(stderr));
    f();
    fflush(stderr);
    dup2(saved, fileno(stderr));
    close(saved);

    rewind(to);
    for (int c; (c = fgetc(to)) != EOF;)
        out += (char)c;
    fclose(to);
    remove(path);

    return out;
}

static size_t
count (const string &in, const string &what)
{
    size_t n = 0;

    for (size_t at = in.find(what); at != string::npos; at = in.find(what, at + 1))
        n++;
    return n;
}

int
main (int argc, char *argv[])
{
    string plugins = PLUGIN_PATH;

    if (argc > 2 && !strcmp(argv[1], "-p"))
        plugins = argv[2];
    if (plugins.back() != '/')
        plugins += '/';

    /* `suggests' says whether a warning names a real one to use instead. */
    static const struct { const char *what, *dsp; vector<const char *> want; bool suggests;
                          size_t warnings = 1; } rows[] = {
        { "an arg the plugin does not have",
          "node ionode { channels = 1; out0 = o->out; play = 1; };\n"
          "node o osc::simple { frequency = 220; };\n"
          "io ionode;\n",
          { "line 2, col 22:", "node 'o' (osc::simple) has no arg 'frequency'",
            "did you mean 'freq'?" }, true },
        { "an output read that the node does not have",
          "node ionode { channels = 1; out0 = m->out; play = 1; };\n"
          "node o osc::simple { freq = 220; };\n"
          "node e env::adsr { a = 0; d = 0; s = 1; r = 0; trigger = 1; };\n"
          "node m mixer::mul { in0 = o->out; in1 = e->outt; };\n"
          "io ionode;\n",
          { "line 4, col 44:", "node 'e' (env::adsr) has no arg 'outt'", "did you mean 'out'?" }, true },
        { "a misspelling inside an expression",
          "node ionode { channels = 1; out0 = o->out * e->outt * 0.5; play = 1; };\n"
          "node o osc::simple { freq = 220; };\n"
          "node e env::adsr { a = 0; d = 0; s = 1; r = 0; trigger = 1; };\n"
          "io ionode;\n",
          { "line 1, col 48:", "has no arg 'outt'", "did you mean 'out'?" }, true },
        { "an io node name the engine never reads",
          "node ionode { channels = 1; ouy0 = o->out; play = 1; };\n"
          "node o osc::simple { freq = 220; };\n"
          "io ionode;\n",
          { "line 1, col 29:", "'ouy0'", "did you mean 'out0'?" }, true },
        { "an io node name the engine never writes",
          "node ionode { channels = 1; out0 = o->out * ionode->velocty; play = 1; };\n"
          "node o osc::simple { freq = 220; };\n"
          "io ionode;\n",
          { "line 1, col 53:", "'velocty'", "did you mean 'velocity'?" }, true },
        { "an io name the engine supplies too few of",
          "node ionode { channels = 1; out0 = o->out * ionode->aux4; play = 1; };\n"
          "node o osc::simple { freq = 220; };\n"
          "io ionode;\n",
          { "line 1, col 53:", "'aux4'", "so it reads as 0" }, true },
        { "an io name the graph sets, read where it never does",
          "node ionode { channels = 1; out0 = o->out; play = 1; };\n"
          "node o osc::simple { freq = 220; amp = ionode->out1; };\n"
          "io ionode;\n",
          { "line 2, col 48:", "'out1'", "so it reads as 0" }, false },
        { "a send on the io node, which is the channel's",
          "node ionode { channels = 1; out0 = o->out; play = 1; send = 0.5; };\n"
          "node o osc::simple { freq = 220; };\n"
          "io ionode;\n",
          { "line 1, col 54:", "'send'", "nothing reads it" }, true },
        { "a name near nothing",
          "node ionode { channels = 1; out0 = o->out; play = 1; };\n"
          "node o osc::simple { freq = 220; zzzzzz = 1; };\n"
          "io ionode;\n",
          { "line 2, col 34:", "has no arg 'zzzzzz'" }, false },
        { "a short name, which every short name is near",
          "node ionode { channels = 1; out0 = o->out; play = 1; };\n"
          "node o osc::simple { freq = 220; q = 1; };\n"
          "io ionode;\n",
          { "line 2, col 34:", "has no arg 'q'" }, false },
        { "a misspelling on an array, once rather than per element",
          "node ionode { channels = 1; out0 = o[0]->out; play = 1; };\n"
          "node o[4] osc::simple { frequency = 220; };\n"
          "io ionode;\n",
          { "line 2, col 25:", "has no arg 'frequency'", "did you mean 'freq'?" }, true },
        { "a misspelling on an indexed name, once rather than per index",
          "node ionode { channels = 4; ouy[] = o->out; play = 1; };\n"
          "node o osc::simple { freq = 220; };\n"
          "io ionode;\n",
          { "line 1, col 29:", "'ouy0'", "nothing reads it" }, true },
        { "the same misspelling twice on one line, warned of twice",
          "node ionode { channels = 1; out0 = o->out + p->out; play = 1; };\n"
          "node o osc::simple { frequency = 220; }; node p osc::simple { frequency = 330; };\n"
          "io ionode;\n",
          { "node 'o' (osc::simple) has no arg 'frequency'",
            "node 'p' (osc::simple) has no arg 'frequency'" }, true, 2 },
        { "names that are all real",
          "node ionode { channels = 2; amp = 0.5; side0 = 0;\n"
          "  out0 = o->out * ionode->amp * ionode->velocity / th_max;\n"
          "  out1 = o->out * ionode->aux1; play = 1; };\n"
          "node p misc::midi2freq { note = ionode->note; };\n"
          "node o osc::simple { freq = p->out; held = 3; };\n"
          "node lfo osc::simple { freq = o->held; };\n"
          "io ionode;\n",
          {}, false },
        { "an io node that is also a plugin's",
          "node o osc::simple { freq = 220; out0 = o->out; channels = 1; play = 1; };\n"
          "node p misc::midi2freq { note = o->note; };\n"
          "io o;\n",
          {}, false },
    };
    int failed = 0;

    for (const auto &r : rows)
    {
        thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
        const thSynthTree *tree = synth.loadTreeText("namecheck", r.dsp, 0, MIDIVALMAX);

        if (tree == NULL)
        {
            printf("FAIL  %s: does not load\n", r.what);
            failed++;
            continue;
        }

        const vector<string> &said = tree->warnings();
        string all;

        for (const string &w : said)
            all += w + "\n";

        bool good = said.size() == (r.want.empty() ? 0 : r.warnings);

        for (const char *w : r.want)
            good = good && all.find(w) != string::npos;
        good = good && (all.find("did you mean") != string::npos) == r.suggests;

        if (good)
            printf("ok    %s: %s\n", r.what, said.empty() ? "no warning" : said[0].c_str());
        else
        {
            printf("FAIL  %s\n  said: %s\n", r.what, all.empty() ? "nothing\n" : all.c_str());
            failed++;
        }
    }

    /* Graphs that do not load, and what the load says on stderr. */
    static const struct { const char *what, *dsp; vector<const char *> want;
                          bool suggests; } refused[] = {
        { "a plugin that is not installed, near one that is",
          "node ionode { channels = 1; out0 = o->out; play = 1; };\n"
          "node o osc::simpel { freq = 220; };\n"
          "io ionode;\n",
          { "line 2, col 8: error: could not load plugin 'osc/simpel' required by node 'o'",
            "; did you mean 'osc::simple'?" }, true },
        { "a plugin that is not installed, near none",
          "node ionode { channels = 1; out0 = o->out; play = 1; };\n"
          "node o osc::zzzzzz { freq = 220; };\n"
          "io ionode;\n",
          { "line 2, col 8: error: could not load plugin 'osc/zzzzzz'" }, false },
        { "a character the language has no use for",
          "node ionode { channels = 1; out0 = o->out; play = 1; };\n"
          "node o osc::simple { freq = 220 ! 2; };\n"
          "io ionode;\n",
          { "line 2, col 33: error: stray character '!'" }, false },
        { "an index on a node that is not an array",
          "node ionode { channels = 1; out0 = o[1]->out; play = 1; };\n"
          "node o osc::simple { freq = 220; };\n"
          "io ionode;\n",
          { "line 1, col 36: error: " }, false },
    };

    for (const auto &r : refused)
    {
        thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
        const thSynthTree *tree = NULL;
        const string said = stderrOf([&] {
            tree = synth.loadTreeText("namecheck", r.dsp, 0, MIDIVALMAX);
        });
        bool good = tree == NULL;

        for (const char *w : r.want)
            good = good && said.find(w) != string::npos;
        good = good && (said.find("did you mean") != string::npos) == r.suggests;

        printf("%s  %s\n%s%s", good ? "ok  " : "FAIL", r.what,
               good ? "" : "  said: ", good ? "" : said.c_str());
        failed += !good;
    }

    /* dspcheck loads each file five times, and five copies of a warning
       read as five problems. It is printed once per patch, and the tree
       still holds it on every load. The web page names every patch it
       loads /patch.dsp, so a patch is its text, not its name. */
    {
        const char *typo = "node ionode { channels = 1; out0 = o->out; play = 1; };\n"
                           "node o osc::simple { frequency = 440; };\n"
                           "io ionode;\n";
        bool kept = true;
        const string said = stderrOf([&] {
            const string edited = string(typo) + "node p osc::simple { freq = 330; };\n";

            for (const string &text : { string(typo), string(typo), edited })
            {
                thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
                const thSynthTree *tree = synth.loadTreeText("/patch.dsp", text, 0, MIDIVALMAX);

                kept = kept && tree != NULL && tree->warnings().size() == 1;
            }
        });
        const bool good = kept && count(said, "has no arg 'frequency'") == 2;

        printf("%s  a warning printed once for each patch it is in\n%s%s",
               good ? "ok  " : "FAIL", good ? "" : "  said: ",
               good ? "" : kept ? said.c_str() : "a load lost its warning\n");
        failed += !good;
    }

    return failed ? 1 : 0;
}
