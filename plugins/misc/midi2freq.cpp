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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

enum {IN_NOTE, OUT_ARG};
int args[OUT_ARG + 1];

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
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "Frequency in hertz");

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

    /* Just short of Nyquist, not on it. A wavelength of exactly two samples is
       an exact integer, so an oscillator's position lands precisely on the
       boundary its pulse width names rather than stepping over it -- which is
       the difference between approaching a division by zero and reaching
       one. */
    const double top = samples / 2.0 - 1.0;

    for(i = 0; i < argnum; i++)
    {
        /* In double, and clamped before it is narrowed.
         *
         * The exponential takes any note, but a frequency past Nyquist is not
         * one the synth can represent -- the oscillators turn it into a
         * wavelength, and one under two samples is not a wave the rate can
         * carry. Past about note 1500 the answer is not a float at all, which
         * is why the clamp happens up here: narrowing first gives an infinity,
         * and an infinity clamped by magnitude comes back as the *bottom* of
         * the range. That would answer an absurdly high note with 0 Hz, an
         * infinite wavelength and silence.
         *
         * dsp/old/bd9.dsp maps an envelope sustaining at a hundred times full
         * scale onto a note range; note 4210 arrives here. */
        double hz = 440.0*pow(2.0, (buf_in[i] - 69) / 12.0);

        if (!(hz >= 0.0))       /* NaN: every comparison with one is false */
            hz = 0.0;
        else if (hz > top)
            hz = top;

        out[i] = (float)hz;
    }

/*    node->SetArg("out", out, windowlen); */
    return 0;
}

