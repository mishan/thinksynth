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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "midi2freq.h"

enum {IN_NOTE, OUT_ARG};
std::atomic<int> args[OUT_ARG + 1];

#define SQR(x) ((x)*(x))

static const char desc[] = "Converts a midi note value to it's respective frequency";
thPlugin::State    mystate = thPlugin::PASSIVE;

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_NOTE] = plugin->regArg("note", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_NOTE],
                       "MIDI note; fractional and out of range are both "
                       "fine, but the answer stops at Nyquist");
    plugin->setArgUnits(args[IN_NOTE], "semitones");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "Frequency in hertz");
    plugin->setArgUnits(args[OUT_ARG], "Hz");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    thArg *in_note;
    float buf_in[windowlen];
    thArg *out_arg;
    unsigned int i, argnum;

    in_note = mod->getArg(node, args[IN_NOTE]);
    in_note->getBuffer(buf_in, windowlen);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    argnum = (unsigned int) in_note->len();

    /* getBuffer() filled windowlen samples of buf_in and no more; a longer
       arg would read off the end of it. Otherwise the length is kept, so a
       one-value note stays one value -- readers take an arg modulo its
       length, and a constant costs a float rather than a window. */
    if (argnum > windowlen)
        argnum = windowlen;

    out = out_arg->allocate(argnum);

    /* In float, and clamped as a float may be: thkMidi2freq. A note
       past float range is an infinity, which is more than the top; were
       it clamped by magnitude it would come back as the bottom, 0 Hz and
       silence -- and a graph mapping an envelope sustaining at a hundred
       times full scale onto a note range sends note 4210 in here. */
    for (i = 0; i < argnum; i++)
        out[i] = thkMidi2freq(buf_in[i], (float)samples);

/*    node->SetArg("out", out, windowlen); */
    return 0;
}

