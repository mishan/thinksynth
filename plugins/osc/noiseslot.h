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

#ifndef THINK_NOISESLOT_H
#define THINK_NOISESLOT_H

/* The noise source osc::static and osc::noise draw from.
 *
 * This was rand(), which is the C library's, and glibc's, musl's and macOS's
 * are three different sequences -- so a patch with a noise node in it
 * rendered differently on each platform, for no reason anybody chose. A
 * 32-bit LCG with the multiplier and increment of the C standard's example
 * rand(), written out so it is the same everywhere.
 *
 * One stream per synth. Every synth's plugin manager loads its own thPlugin
 * for each file, module_init is handed it, and every node built from it
 * reaches it through node->plugin() -- so a slot claimed in module_init and
 * keyed on that pointer is a generator one synth draws from, on the one
 * thread that synth renders on. It starts where every synth's does, so two
 * renders from two freshly built synths are the same render, which is what
 * dspcheck's render-twice comparison depends on. And a synth being built does
 * not touch the noise of one already playing.
 *
 * rand() was one stream for the whole process, and so was the first version
 * of this: a second synth restarted the first's noise when it loaded the
 * plugin, and two render threads could draw the same number. Should more
 * synths be alive at once than there are slots, the rest do share one stream
 * again -- stepped by compare-and-swap, so no draw is lost -- but that takes
 * more synths than anything here creates.
 *
 * A header rather than a copy in each plugin because the two are the same
 * generator and have to stay the same: the table is per shared object, since
 * each plugin is one, so each still gets a stream of its own. The one thing
 * this cannot do is seed itself, which is why thcNodeHost refuses both
 * plugins in a chain -- a piece has to replay.
 */

#include <atomic>

#define THINK_NOISE_SLOTS 64

struct thNoiseSlot
{
    std::atomic<const thPlugin *> owner;
    unsigned                      state;   /* the owning synth's thread only */
};

static thNoiseSlot thNoiseSlots[THINK_NOISE_SLOTS];

static std::atomic<unsigned> thNoiseShared(1);

static inline unsigned thNoiseStep (unsigned s)
{
    return s * 1103515245u + 12345u;
}

/* The 31 bits a stepped state yields. */
static inline unsigned thNoiseBits (unsigned s)
{
    return (s >> 1) & 0x7fffffff;
}

static inline thNoiseSlot *thNoiseSlotFor (const thPlugin *plugin)
{
    for (int i = 0; i < THINK_NOISE_SLOTS; i++)
        if (thNoiseSlots[i].owner.load(std::memory_order_acquire) == plugin)
            return &thNoiseSlots[i];

    return NULL;
}

/* A synth is loading this plugin: claim it a generator, starting where every
   synth's does. The state is written after the claim, and reaches the audio
   thread the way the rest of that load does -- in the graph the synth hands
   over. */
static inline void thNoiseClaim (const thPlugin *plugin)
{
    for (int i = 0; i < THINK_NOISE_SLOTS; i++)
    {
        const thPlugin *none = NULL;

        if (thNoiseSlots[i].owner.compare_exchange_strong(
                none, plugin, std::memory_order_acq_rel))
        {
            thNoiseSlots[i].state = 1;
            break;
        }
    }
}

/* The synth is done with this plugin: its slot goes back. */
static inline void thNoiseRelease (const thPlugin *plugin)
{
    thNoiseSlot *slot = thNoiseSlotFor(plugin);

    if (slot != NULL)
        slot->owner.store(NULL, std::memory_order_release);
}

/* The fallback, for a synth that found no slot of its own. */
static inline unsigned thNoiseSharedBits (void)
{
    unsigned s = thNoiseShared.load(std::memory_order_relaxed);

    while (!thNoiseShared.compare_exchange_weak(s, thNoiseStep(s),
                                                std::memory_order_relaxed))
        ;

    return thNoiseBits(thNoiseStep(s));
}

#endif /* THINK_NOISESLOT_H */
