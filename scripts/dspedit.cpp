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

/*
 * dspedit -- one edit, printed.
 *
 * The reference the browser's node editor is held against. It makes a
 * single NodeEdit change to a .dsp and writes the result to stdout, so
 * that wasm/web/nodecheck.mjs can ask the module for the same edit and
 * compare the bytes. Everything it does, the editor's own harnesses
 * already cover; what it adds is a way to *say* what the answer should
 * be from outside the process.
 *
 * Nothing is written back: the file on disk is not touched.
 *
 *   dspedit FILE set-value NODE ARG VALUE
 *   dspedit FILE connect NODE ARG SRCNODE SRCPORT
 *   dspedit FILE connect-control NODE ARG CONTROL
 *   dspedit FILE disconnect NODE ARG VALUE
 *   dspedit FILE set-chanarg NAME VALUE
 *   dspedit FILE add-node NODE PLUGIN
 *   dspedit FILE remove-node NODE
 *   dspedit FILE set-control-meta NAME MIN MAX LABEL GROUP
 *   dspedit FILE layout-write -p PLUGINS
 *
 * The last one is not an edit to the patch but to its layout block: the
 * graph is built and laid out and the block rewritten from it, which is
 * what a drag in the editor is followed by. It needs the plugins, since
 * laying a graph out means parsing the .dsp.
 *
 * Exit status is the NodeEdit::Result: 0 is OK, and a refusal prints its
 * sentence on stderr.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <string>
#include <vector>

#include "think.h"

#include "NodeEdit.h"
#include "NodeGraph.h"
#include "NodeLayout.h"

using std::string;

static bool slurp (const string &path, string &out)
{
    std::ifstream in(path.c_str(), std::ios::binary);

    if (!in)
        return false;

    out.assign((std::istreambuf_iterator<char>(in)),
               std::istreambuf_iterator<char>());

    return true;
}

static int usage (void)
{
    fprintf(stderr, "usage: dspedit FILE OPERATION [ARGS...]\n"
                    "see the top of scripts/dspedit.cpp\n");
    return 2;
}

int main (int argc, char **argv)
{
    if (argc < 3)
        return usage();

    string source;

    if (!slurp(argv[1], source))
    {
        fprintf(stderr, "dspedit: cannot read %s\n", argv[1]);
        return 2;
    }

    const string op = argv[2];
    const int rest = argc - 3;
    char **a = argv + 3;

    string why;
    NodeEdit::Result r = NodeEdit::REFUSED;
    int removed = 0;

    /* Not an edit: the layout block, rewritten from the graph the patch
       lays out to. */
    if (op == "layout-write" && rest == 2 && string(a[0]) == "-p")
    {
        thSynth synth(a[1], TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
        thSynthTree *tree = synth.parseTree(argv[1]);

        if (tree == NULL)
        {
            fprintf(stderr, "dspedit: %s does not parse\n", argv[1]);
            return 2;
        }

        NodeGraph g;

        g.build(tree);
        delete tree;
        g.layout();

        if (!NodeLayout::Text::write(source, g))
        {
            fprintf(stderr, "dspedit: the layout block could not be "
                            "written\n");
            return 2;
        }

        fputs(source.c_str(), stdout);

        return 0;
    }

    if (op == "set-value" && rest == 3)
        r = NodeEdit::Text::setValue(source, a[0], a[1], atof(a[2]), why);
    else if (op == "connect" && rest == 4)
        r = NodeEdit::Text::connect(source, a[0], a[1], a[2], a[3], why);
    else if (op == "connect-control" && rest == 3)
        r = NodeEdit::Text::connectControl(source, a[0], a[1], a[2], why);
    else if (op == "disconnect" && rest == 3)
        r = NodeEdit::Text::disconnect(source, a[0], a[1], atof(a[2]), why);
    else if (op == "set-chanarg" && rest == 2)
        r = NodeEdit::Text::setChanArg(source, a[0], atof(a[1]), why);
    else if (op == "add-node" && rest == 2)
        r = NodeEdit::Text::addNode(source, a[0], a[1], why);
    else if (op == "remove-node" && rest == 1)
        r = NodeEdit::Text::removeNode(source, a[0], removed, why);
    else if (op == "set-control-meta" && rest == 5)
        r = NodeEdit::Text::setControlMeta(source, a[0], atof(a[1]),
                                           atof(a[2]), a[3], a[4], why);
    else
        return usage();

    if (r != NodeEdit::OK)
    {
        fprintf(stderr, "dspedit: %s: %s\n", NodeEdit::resultText(r),
                why.c_str());
        return (int)r;
    }

    fputs(source.c_str(), stdout);

    return 0;
}
