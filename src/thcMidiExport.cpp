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
 */

#include <algorithm>
#include <filesystem>

#include "think.h"

#include "thcGenFile.h"
#include "thcMidiExport.h"
#include "thcScheduler.h"

/* An arrangement that never ends is cut here whatever is asked. */
static const double MOST = 3600;

void
thcMidiExport::describe (thcMidiFile &midi, thcScheduler &sched,
                         thSynth &synth, const std::string &name,
                         double seconds)
{
    midi.setName(name);
    midi.setRangeLookup(
        [&synth](int channel, const std::string &arg, double &min,
                 double &max)
        {
            const thArg *a = synth.getChanArg(channel, arg);

            if (a == NULL)
                return false;

            min = a->min();
            max = a->max();
            return true;
        });

    for (size_t c = 0; c < sched.chainCount(); c++)
        midi.setChainName((int)c, sched.chain(c)->name);

    for (int ch = 0; ch < synth.midiChanCount(); ch++)
    {
        const std::string holds = sched.holding(ch);

        if (!holds.empty())
            midi.setChannelName(ch, holds);
    }

    /* The arrangement at the times the scheduler gates it, and round
       again for as long as the render runs where it has no `section
       end;' -- sectionAt() wraps. */
    double cycle = 0;

    for (const thcSection &s : sched.sections())
        cycle += s.beats ? s.length * 60 / sched.tempo() : s.length;

    for (double start = 0; cycle > 0 && start < seconds; start += cycle)
    {
        double at = start;

        for (const thcSection &s : sched.sections())
        {
            if (at < seconds)
                midi.addMarker(at, s.name);

            at += s.beats ? s.length * 60 / sched.tempo() : s.length;
        }

        if (sched.endsAfterSections())
            break;
    }
}

bool
thcMidiExport::render (const std::map<std::string, thcPlugin *> &plugins,
                       thSynth *synth, const std::string &genPath,
                       const Options &options, std::vector<uint8_t> &out,
                       std::string &why, double *length)
{
    thcScheduler sched(synth);

    /* A piece that listens composes the same only where the ear answers
       inside the tick; offline that costs nothing but time. */
    sched.setAuditionSynchronous(true);

    if (options.seed >= 0)
        sched.setMasterSeed((unsigned)options.seed);

    /* Every channel but the host's own taken, so the loader -- which
       takes the lowest free one for each instrument, in order -- puts them
       where the host has them. */
    const std::vector<int> &mine = options.channels;

    if (!mine.empty())
        sched.setChannelTaken([&mine](int channel)
                              {
                                  return std::find(mine.begin(), mine.end(),
                                                   channel) == mine.end();
                              });

    thcGenLoader loader(plugins);

    if (!loader.load(genPath, &sched))
    {
        why = loader.errors().empty() ? "the piece did not load"
                                      : loader.errors()[0];
        return false;
    }

    /* An arrangement that ends is its own length; `seconds' is for one
       that does not. */
    const double seconds =
        sched.endsAfterSections() && sched.sectionsLength() > 0
            ? std::min(sched.sectionsLength(), MOST)
            : std::min(std::max(options.seconds, 0.0), MOST);

    for (size_t c = 0; c < sched.chainCount(); c++)
    {
        const std::string &chain = sched.chain(c)->name;

        if (std::find(options.muted.begin(), options.muted.end(), chain) !=
            options.muted.end())
            sched.setMuted(c, true);

        if (std::find(options.soloed.begin(), options.soloed.end(), chain) !=
            options.soloed.end())
            sched.setSoloed(c, true);
    }

    thcMidiFile midi(sched.tempo(), sched.meter());
    const double step = options.step > 0 ? options.step : 0.02;

    midi.setFineControllers(options.fine);
    describe(midi, sched, *synth,
             options.name.empty()
                 ? std::filesystem::path(genPath).stem().string()
                 : options.name,
             seconds);

    sigc::connection conn = sched.sigDelivered.connect(
        [&](const thcEvent &ev) { midi.add(ev, sched.deliveringChain()); });

    sched.start();

    /* A silent synth still takes a command per channel loaded, and a ring
       nobody drains is the failure the mode exists to remove -- so it is
       processed each step, which costs it nothing. */
    while (sched.running() && sched.now() < seconds)
    {
        sched.stepTransport(std::min(step, seconds - sched.now()));
        synth->process();
    }

    const double stoppedAt = sched.now();

    sched.stop();
    conn.disconnect();
    midi.end(stoppedAt);

    out = midi.bytes();

    if (length != NULL)
        *length = stoppedAt;

    return true;
}
