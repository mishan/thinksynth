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
 * The module unloadcheck opens: think_embedded inside a shared object, as
 * an audio plugin would have it. unloadcheck.cpp says what is asked of it.
 */

#include "think.h"

static const char graph[] =
    "name \"unloadcheck\";\n"
    "node ionode {\n"
    "    channels = 2;\n"
    "    out0 = osc->out;\n"
    "    out1 = osc->out;\n"
    "    play = 1;\n"
    "};\n"
    "node osc osc::simple {\n"
    "    freq = 440;\n"
    "    waveform = 0;\n"
    "};\n"
    "io ionode;\n";

extern "C" __attribute__((visibility("default"))) int unloadcheck_probe (void)
{
    thSynth synth("", TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    if (synth.loadTreeText("unloadcheck", graph, 0, 100) == NULL)
        return 0;

    synth.addNote(0, 60, 100);
    synth.process();

    /* A lookup that fails, which is what writes the loader's error. */
    synth.getPluginManager()->loadPlugin("unloadcheck/absent");

    return 1;
}
