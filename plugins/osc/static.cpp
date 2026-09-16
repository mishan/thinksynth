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
 * One stream per synth. Every synth's plugin manager loads its own
 * thPlugin for this file, module_init is handed it, and every node built
 * from it reaches it through node->plugin() -- so a slot claimed in
 * module_init and keyed on that pointer is a generator one synth draws
 * from, on the one thread that synth renders on. It starts where every
 * synth's does, so two renders from two freshly built synths are the same
 * render, which is what dspcheck's render-twice comparison depends on. And
 * a synth being built does not touch the noise of one already playing.
 *
 * rand() was one stream for the whole process, and so was the first
 * version of this: a second synth restarted the first's noise when it
 * loaded the plugin, and two render threads could draw the same number.
 * Should more synths be alive at once than there are slots, the rest do
 * share one stream again -- stepped by compare-and-swap, so no draw is
 * lost -- but that takes more synths than anything here creates.
 *
 * Nothing seeds any of it, which is why thcNodeHost still refuses this
 * plugin in a chain. */

#define STATIC_SLOTS 64

struct Slot
{
    std::atomic<const thPlugin *> owner;
    unsigned                      state;   /* the owning synth's thread only */
};

static Slot slots[STATIC_SLOTS];

static std::atomic<unsigned> shared(1);

static inline unsigned step (unsigned s)
{
    return s * 1103515245u + 12345u;
}

/* The 31 bits a stepped state yields. */
static inline unsigned bits (unsigned s)
{
    return (s >> 1) & 0x7fffffff;
}

static Slot *slotFor (const thPlugin *plugin)
{
    for (int i = 0; i < STATIC_SLOTS; i++)
        if (slots[i].owner.load(std::memory_order_acquire) == plugin)
            return &slots[i];

    return NULL;
}

static unsigned sharedNoise (void)
{
    unsigned s = shared.load(std::memory_order_relaxed);

    while (!shared.compare_exchange_weak(s, step(s),
                                         std::memory_order_relaxed))
        ;

    return bits(step(s));
}

/* The synth is done with this plugin: its slot goes back. */
void module_cleanup (thPlugin *plugin)
{
    Slot *slot = slotFor(plugin);

    if (slot != NULL)
        slot->owner.store(NULL, std::memory_order_release);
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
enum { OUT_ARG,INOUT_LAST,IN_SAMPLE };

int args[IN_SAMPLE + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    /* A synth is loading this: claim it a generator, starting where every
       synth's does. The state is written after the claim, and reaches the
       audio thread the way the rest of this load does -- in the graph the
       synth hands over. */
    for (int i = 0; i < STATIC_SLOTS; i++)
    {
        const thPlugin *none = NULL;

        if (slots[i].owner.compare_exchange_strong(none, plugin,
                                                   std::memory_order_acq_rel))
        {
            slots[i].state = 1;
            break;
        }
    }

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

    /* This synth's generator, held in a local for the window and written
       back once at the end. */
    Slot *slot = slotFor(node->plugin());
    unsigned s = (slot != NULL) ? slot->state : 0;

    for(i=0; i < (int)windowlen; i++) {
        if(++position > (*in_sample)[i]) {
            unsigned r;

            if (slot != NULL) {
                s = step(s);
                r = bits(s);
            } else {
                r = sharedNoise();
            }

            /* 2^31, not RAND_MAX+1: r is 31 bits everywhere, and RAND_MAX
               is 32767 on Windows. */
            out[i] = TH_RANGE*(r/2147483648.0)+TH_MIN;
            position = 0;
            last = out[i];
        } else {
            out[i] = last;
        }
    }

    if (slot != NULL)
        slot->state = s;

    out_last[0] = position;
    out_last[1] = last;
/*    node->SetArg("out", out, windowlen);
    node->SetArg("last", last, 2);
*/
    return 0;
}
