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
 * controllercheck -- a patch's `controller' blocks, kept as the text of a
 * controller profile (thSynthTree::controller()), each device's apart.
 *
 * The engine does not read the block; a host hands it to the controller
 * code. So what is held down here is the text: `device' for `controller',
 * the block's tokens spaced as written, so a sign stays against its
 * number, and on the lines they were on, so a mistake in one is reported
 * at the patch's own line.
 */

#include <cstdio>
#include <string>

#include "think.h"

using std::string;

static int failed = 0;

#define GRAPH "node ionode { channels = 1; out0 = 0; play = 0; };\nio ionode;\n"

struct Row
{
    const char *name;
    const char *dsp;
    const char *want;       /* the controller text, or NULL if refused */
    const char *device;     /* whose blocks, if not index's */
};

static const Row ROWS[] = {
    { "a patch with none has none",
      "name \"t\";\n" GRAPH, "" },
    { "a block, as a profile, on its own lines",
      "name \"t\";\ncontroller index {\n    tilt = \"bright\" { smooth = 0.05; };\n};\n"
      GRAPH,
      "\ndevice index {\ntilt = \"bright\" { smooth = 0.05; };\n};" },
    { "a hand's block, a sign against its number, and a comment dropped",
      "controller index left { # the left hand\n  roll = bend { range = 2; center = -0.5; };\n};\n"
      GRAPH,
      "device index left {\nroll = bend { range = 2; center = -0.5; };\n};" },
    { "two blocks, each where it was",
      "controller index { a = note 0; };\n" GRAPH
      "controller index right { b = note 7; };\n",
      "device index { a = note 0; };\n\n\ndevice index right { b = note 7; };" },
    { "two blocks on one line stay on it",
      "controller index { a = note 0; }; controller index left { b = note 7; };\n" GRAPH,
      "device index { a = note 0; }; device index left { b = note 7; };" },
    { "a block with no braces is refused, not the rest of the file swallowed",
      "controller index;\nnode spare { a = 1; };\n" GRAPH, NULL },
    { "a block never closed is refused",
      "controller index {\n  a = note 0;\n" GRAPH, NULL },
    { "each device's blocks apart, on their own lines",
      "controller index { a = note 0; };\ncontroller box { touch = note 0; };\n"
      "controller index left { b = note 7; };\n" GRAPH,
      "device index { a = note 0; };\n\ndevice index left { b = note 7; };" },
    { "...and the other's",
      "controller index { a = note 0; };\ncontroller box { touch = note 0; };\n" GRAPH,
      "\ndevice box { touch = note 0; };", "box" },
    { "a block with no device is refused",
      "controller { a = note 0; };\n" GRAPH, NULL },
};

int
main ()
{
    for (const Row &r : ROWS)
    {
        thSynth synth;
        thSynthTree *tree = synth.loadTreeText("t", r.dsp, 0, 100);
        const string got = tree ? tree->controller(r.device ? r.device : "index") : "(refused)";
        const string want = r.want ? r.want : "(refused)";

        if (got == want)
            printf("ok    %s\n", r.name);
        else
        {
            printf("FAIL  %s\n  got  [%s]\n  want [%s]\n", r.name, got.c_str(),
                   want.c_str());
            failed++;
        }
    }

    printf("%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed ? 1 : 0;
}
