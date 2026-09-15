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

#include <atomic>

#include "think.h"

static const char desc[] = "Produces Random Signal";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* The noise source. This was rand(), which is the C library's, and
 * glibc's, musl's and macOS's are three different sequences -- so a patch
 * with a static node in it rendered differently on each platform, for no
 * reason anybody chose. A 32-bit LCG with the multiplier and increment of
 * the C standard's example rand(), written out so it is the same
 * everywhere.
 *
 * One stream for the process, as rand()'s was, restarted by module_init
 * -- which runs whenever a synth loads this plugin, so two renders from
 * two freshly built synths are the same render. dspcheck's
 * render-twice-and-compare depends on that; it used to get it by
 * reseeding rand().
 *
 * Relaxed atomics rather than a plain variable: two synths on two threads
 * would otherwise race on the state, which is undefined behaviour however
 * little it matters who gets which number. rand() took a lock for the same
 * reason. Nothing seeds it, which is why thcNodeHost still refuses this
 * plugin in a chain. */
static std::atomic<unsigned> state(1);

static unsigned noise (void)
{
    const unsigned s =
        state.load(std::memory_order_relaxed) * 1103515245u + 12345u;

    state.store(s, std::memory_order_relaxed);

    return (s >> 1) & 0x7fffffff;
}

void module_cleanup (thPlugin *plugin)
{
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
enum { OUT_ARG,INOUT_LAST,IN_SAMPLE };

int args[IN_SAMPLE + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    /* A synth is loading this: its noise starts where every synth's does. */
    state.store(1, std::memory_order_relaxed);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);
    args[IN_SAMPLE] = plugin->regArg("sample", thPlugin::ARG_IN);
    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    int i;
    float *out;
    float *out_last;
    float position, last;
    thArg* in_sample;  /* How often to change the random number */
    thArg* out_arg;
    thArg* inout_last;  /* So sample can be consistant over windows */

    out_arg = mod->getArg(node, args[OUT_ARG]);
    inout_last = mod->getArg(node, args[INOUT_LAST]);
    position = (*inout_last)[0];
    last = (*inout_last)[1];
    /* `last' carries two values across windows -- the position counter and the
       held sample -- and both are written back below. This allocated room for
       one, so out_last[1] wrote off the end of the buffer every window. */
    out_last = inout_last->allocate(2);
    out = out_arg->allocate(windowlen);

    in_sample = mod->getArg(node, args[IN_SAMPLE]);

    for(i=0; i < (int)windowlen; i++) {
        if(++position > (*in_sample)[i]) {
            /* 2^31, not RAND_MAX+1: noise() is 31 bits everywhere, and
               RAND_MAX is 32767 on Windows. */
            out[i] = TH_RANGE*(noise()/2147483648.0)+TH_MIN;
            position = 0;
            last = out[i];
        } else {
            out[i] = last;
        }
    }

    out_last[0] = position;
    out_last[1] = last;
/*    node->SetArg("out", out, windowlen);
    node->SetArg("last", last, 2);
*/
    return 0;
}
