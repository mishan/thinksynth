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

/* A wavetable oscillator: single cycles, one after another in a file, and
 * `position' moving through them while a note plays.
 *
 * THE TABLE is osc/wavetable.h's: the file's frames as recorded, each kept
 * at half-octave levels of its harmonics. A note reads the two levels
 * either side of what fits under Nyquist at its pitch, crossfaded, so it
 * never aliases and a glide never steps; and the two frames either side of
 * `position', crossfaded, so an envelope or an oscillator sweeping it
 * morphs the wave. Found under samples/ on THINK_DSP_PATH, as osc::sample
 * finds a file, and read once per synth.
 *
 * `reset' restarts the cycle at `phase' on its rising edge: a note's
 * trigger, so every note starts the same, or another oscillator's, for
 * hard sync -- which, a step in the wave, aliases as hard sync does
 * without a BLEP. A negative `freq' runs the cycle backward, for FM
 * through zero.
 *
 * The output is the table as it was written, not normalized: a saw
 * band-limited this way overshoots to about 1.18.
 *
 * THE FILE IS BUILT ONCE PER SYNTH, on the loading thread (module_prepare)
 * -- tens of milliseconds for a large table, which a voice's first window
 * cannot afford. A file read again (a pack arriving, one rewritten) is a
 * new table from the next window; the old one is kept until the synth lets
 * the plugin go, since a window rendering may still be reading it.
 */

#include "config.h"

#include <math.h>
#include <stdio.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thSynthTree.h"
#include "thSynth.h"

#include "sampleslot.h"
#include "wavetable.h"

enum {IN_FILE, IN_FREQ, IN_POSITION, IN_RESET, IN_PHASE, OUT_ARG,
      INOUT_STATE};

std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "Wavetable oscillator (single cycles, morphed)";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* ---- the tables, one set per synth --------------------------------------
 *
 * As osc/sampleslot.h keeps files: a map the audio thread reads without a
 * lock, never changed once published, a new one for each addition; and
 * everything ever published kept until the plugin is released. */

struct WtEntry
{
    thWaveTable table;          /* no frames: a file that would not read */
    unsigned generation = 0;
};

struct WtMap
{
    std::map<std::string, WtEntry *> files;
};

struct WtSlot
{
    std::atomic<const thPlugin *> owner;
    std::atomic<const WtMap *> map;
    std::mutex adding;
    std::vector<WtMap *> maps;
    std::vector<WtEntry *> entries;
};

static WtSlot wtSlots[THINK_SAMPLE_SLOTS];

static WtSlot *wtSlotFor (const thPlugin *plugin)
{
    for (int i = 0; i < THINK_SAMPLE_SLOTS; i++)
        if (wtSlots[i].owner.load(std::memory_order_acquire) == plugin)
            return &wtSlots[i];

    return NULL;
}

/* Holding `adding'. */
static void wtFree (WtSlot *slot)
{
    for (WtMap *m : slot->maps)
        delete m;

    for (WtEntry *e : slot->entries)
        delete e;

    slot->maps.clear();
    slot->entries.clear();
    slot->map.store(NULL, std::memory_order_release);
}

/* Holding `adding'. */
static void wtPublish (WtSlot *slot, WtMap *next)
{
    slot->maps.push_back(next);
    slot->map.store(next, std::memory_order_release);
}

/* Holding `adding'. The entry for `name', built now if there is none or
   the file has changed since. */
static const WtEntry *wtFile (WtSlot *slot, const std::string &name)
{
    const WtMap *map = slot->map.load(std::memory_order_relaxed);
    const auto i = map->files.find(name);
    const unsigned generation = thUtil::dataFilesGeneration();

    if (i != map->files.end())
    {
        WtEntry *kept = i->second;

        if (kept->generation == generation)
            return kept;

        /* Checked again, and unchanged: so marked, as osc::sample does,
           or every load from here on would ask the file again. */
        if (kept->table.frames > 0 &&
            thUtil::dataFileGeneration("samples/" + name) <= kept->generation)
        {
            kept->generation = generation;
            return kept;
        }
    }

    WtEntry *entry = new WtEntry();
    thSampleData data;
    std::string why;

    entry->generation = generation;
    slot->entries.push_back(entry);

    /* As recorded: a cycle is a cycle at any rate. */
    thSampleRead(name, 0, data);

    if (!data.frames.empty() &&
        !thWaveTableBuild(data.frames.data(), data.frames.size(), data.cycle,
                          entry->table, why))
        fprintf(stderr, "osc::wavetable: '%s' %s\n", name.c_str(),
                why.c_str());

    WtMap *next = new WtMap(*map);

    next->files[name] = entry;
    wtPublish(slot, next);

    return entry;
}

/* Audio thread. The table for `name', built here if nobody primed it. */
static const thWaveTable *wtGet (const thPlugin *plugin,
                                 const std::string &name)
{
    WtSlot *slot = wtSlotFor(plugin);
    const WtMap *map = slot ? slot->map.load(std::memory_order_acquire) : NULL;

    if (map == NULL)
        return NULL;

    const auto i = map->files.find(name);

    if (i != map->files.end())
        return &i->second->table;

    std::lock_guard<std::mutex> hold(slot->adding);

    return &wtFile(slot, name)->table;
}

void module_cleanup (thPlugin *plugin)
{
    for (int i = 0; i < THINK_SAMPLE_SLOTS; i++)
        if (wtSlots[i].owner.load(std::memory_order_acquire) == plugin)
        {
            {
                std::lock_guard<std::mutex> hold(wtSlots[i].adding);

                wtFree(&wtSlots[i]);
            }

            wtSlots[i].owner.store(NULL, std::memory_order_release);
            return;
        }
}

/* The table, so a note builds none. */
void module_prepare (thNode *node, thSynthTree *mod, unsigned int samples,
                     int voice)
{
    (void)samples;

    if (voice)
        return;

    const thArg *file = mod->getArg(node, args[IN_FILE]);
    WtSlot *slot = wtSlotFor(node->plugin());

    if (file == NULL || file->text().empty() || slot == NULL)
        return;

    std::lock_guard<std::mutex> hold(slot->adding);

    if (slot->map.load(std::memory_order_relaxed) != NULL)
        wtFile(slot, file->text());
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc(desc);
    plugin->setState(mystate);

    for (int i = 0; i < THINK_SAMPLE_SLOTS; i++)
    {
        const thPlugin *none = NULL;

        if (wtSlots[i].owner.compare_exchange_strong(
                none, plugin, std::memory_order_acq_rel))
        {
            std::lock_guard<std::mutex> hold(wtSlots[i].adding);

            wtFree(&wtSlots[i]);
            wtPublish(&wtSlots[i], new WtMap());
            break;
        }
    }

    args[IN_FILE] = plugin->regArg("file", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FILE],
                       "The table: single cycles laid end to end, under "
                       "samples/ on THINK_DSP_PATH; their length from the "
                       "file's `clm ' or `srge', else 2048, or the whole "
                       "file when it is one cycle");
    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FREQ],
                       "The note; negative runs the cycle backward");
    plugin->setArgUnits(args[IN_FREQ], "Hz");
    args[IN_POSITION] = plugin->regArg("position", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_POSITION],
                       "Which frame: 0 the first, 1 the last, the two "
                       "between crossfaded");
    plugin->setArgRange(args[IN_POSITION], 0, 1);
    args[IN_RESET] = plugin->regArg("reset", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RESET],
                       "Rising above 0, start the cycle again at `phase'");
    plugin->setArgRange(args[IN_RESET], 0, 1);
    args[IN_PHASE] = plugin->regArg("phase", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_PHASE],
                       "Where in the cycle `reset' starts it");
    plugin->setArgRange(args[IN_PHASE], 0, 1);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The wave");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    /* [0] the phase, [1] the last `reset'. */
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

/* One frame at `phase', from level k of `t': below 0 is level 0, every
   harmonic fitting, and past the last is the last, the fundamental alone,
   which fits as long as the note is under Nyquist -- and a note that is
   not plays nothing. */
static float wtLevel (const thWaveTable &t, long k, unsigned frame,
                      double phase)
{
    const long last = (long)t.levels.size() - 1;

    return thWaveRead(t.levels[(size_t)(k < 0 ? 0 : k > last ? last : k)],
                      frame, phase);
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_file = mod->getArg(node, args[IN_FILE]);
    thArg *in_freq = mod->getArg(node, args[IN_FREQ]);
    thArg *in_position = mod->getArg(node, args[IN_POSITION]);
    thArg *in_reset = mod->getArg(node, args[IN_RESET]);
    thArg *in_phase = mod->getArg(node, args[IN_PHASE]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);

    double phase = (*inout_state)[0];
    float lastReset = (*inout_state)[1];
    float *state = inout_state->allocate(2);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);

    const thWaveTable *t = in_file->text().empty() ? NULL
                         : wtGet(node->plugin(), in_file->text());

    if (!(phase >= 0 && phase < 1))
        phase = 0;

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float reset = (*in_reset)[i];

        if (reset > 0 && !(lastReset > 0))
        {
            const float p = (*in_phase)[i];

            phase = p >= 0 && p < 1 ? p : 0;
        }

        lastReset = reset;

        const float hz = (*in_freq)[i];

        if (t == NULL || t->frames == 0 || !(fabsf(hz) < samples * 0.5f))
        {
            out[i] = 0;
            continue;
        }

        /* How many harmonics fit under Nyquist, and the half-octave level
           that holds no more: x = 2 log2(H / fit), level ceil(x) and the one
           above it, crossfaded so a glide is continuous. A freq of 0 holds
           the wave where it is, every harmonic fitting, so FM through zero
           does not drop to nothing for a sample. */
        const double x = hz == 0 ? -1
            : 2 * log2((double)t->levels[0].harmonics * fabs((double)hz) /
                       (samples * 0.5));
        const long k0 = (long)ceil(x);
        const float up = (float)(1 - ((double)k0 - x));

        /* Which frames, crossfaded. */
        float pos = (*in_position)[i];

        pos = pos > 0 ? (pos < 1 ? pos : 1) : 0;    /* NaN to 0 too */

        const float p = pos * (float)(t->frames - 1);
        const unsigned f0 = (unsigned)p;
        const unsigned f1 = f0 + 1 < t->frames ? f0 + 1 : f0;
        const float across = p - (float)f0;

        auto at = [&] (long k)
        {
            const float a = wtLevel(*t, k, f0, phase);

            return across > 0 ? a + (wtLevel(*t, k, f1, phase) - a) * across
                              : a;
        };

        const float lo = at(k0), hi = up > 0 ? at(k0 + 1) : 0;

        out[i] = TH_MAX * (lo + (hi - lo) * up);

        phase += (double)hz / samples;
        phase -= floor(phase);
    }

    state[0] = (float)phase;
    state[1] = lastReset;

    return 0;
}
