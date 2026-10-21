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

#include "config.h"

#include "thcGenDiff.h"

#include "thcGenFile.h"

typedef thcGenEdit::Doc Doc;

static bool
sameStage (const thcGenEdit::Stage &a, const thcGenEdit::Stage &b)
{
    if (a.name != b.name || a.category != b.category ||
        a.plugin != b.plugin || a.params.size() != b.params.size())
        return false;

    for (size_t i = 0; i < a.params.size(); i++)
        if (a.params[i].name != b.params[i].name ||
            a.params[i].valueText != b.params[i].valueText)
            return false;

    return true;
}

static bool
sameChain (const thcGenEdit::Chain &a, const thcGenEdit::Chain &b)
{
    if (a.name != b.name || a.startText != b.startText ||
        a.inputMidi != b.inputMidi || a.hasSeed != b.hasSeed ||
        a.seed != b.seed || a.stages.size() != b.stages.size() ||
        a.sinks.size() != b.sinks.size())
        return false;

    for (size_t i = 0; i < a.stages.size(); i++)
        if (!sameStage(a.stages[i], b.stages[i]))
            return false;

    for (size_t i = 0; i < a.sinks.size(); i++)
        if (a.sinks[i].channel != b.sinks[i].channel ||
            a.sinks[i].instrument != b.sinks[i].instrument ||
            a.sinks[i].chanarg != b.sinks[i].chanarg ||
            a.sinks[i].knob != b.sinks[i].knob)
            return false;

    return true;
}

static bool
sameKnob (const thcGenEdit::Knob &a, const thcGenEdit::Knob &b)
{
    return a.name == b.name && a.value == b.value &&
           a.hasMin == b.hasMin && a.hasMax == b.hasMax &&
           (!a.hasMin || a.min == b.min) && (!a.hasMax || a.max == b.max) &&
           a.label == b.label;
}

/* Whatever an unchanged stage line can mean something else under: the
   scales and presets a param may name, and -- unless `seedToo' is false --
   the piece's seed, which every chain without one of its own is seeded
   from. */
static bool
sameContext (const Doc &a, const Doc &b, bool seedToo = true)
{
    if ((seedToo && (a.hasSeed != b.hasSeed ||
                     (a.hasSeed && a.seed != b.seed))) ||
        a.scales.size() != b.scales.size() ||
        a.presets.size() != b.presets.size())
        return false;

    for (size_t i = 0; i < a.scales.size(); i++)
        if (a.scales[i].name != b.scales[i].name ||
            a.scales[i].notes != b.scales[i].notes)
            return false;

    for (size_t i = 0; i < a.presets.size(); i++)
    {
        const thcGenEdit::Preset &p = a.presets[i], &q = b.presets[i];

        if (p.name != q.name || p.values.size() != q.values.size())
            return false;

        for (size_t v = 0; v < p.values.size(); v++)
            if (p.values[v].name != q.values[v].name ||
                p.values[v].value != q.values[v].value)
                return false;
    }

    return true;
}

static void
planOf (const Doc &was, const Doc &now,
        const std::set<std::string> &changedFiles,
        thcScheduler::EditPlan &plan, std::set<std::string> &keepKnobValues)
{
    plan = thcScheduler::EditPlan();
    plan.changedFiles = changedFiles;
    keepKnobValues.clear();

    if (now.hasTempo && (!was.hasTempo || was.tempo != now.tempo))
        plan.tempo = now.tempo;

    for (size_t n = 0; n < now.knobs.size(); n++)
        for (size_t o = 0; o < was.knobs.size(); o++)
            if (sameKnob(was.knobs[o], now.knobs[n]))
                keepKnobValues.insert(now.knobs[n].name);

    /* A new piece seed is another take of every chain but those with a
       seed of their own. */
    if (!sameContext(was, now, false))
        return;

    const bool sameSeed = sameContext(was, now);

    for (size_t n = 0; n < now.chains.size(); n++)
    {
        const thcGenEdit::Chain &nc = now.chains[n];

        for (size_t o = 0; o < was.chains.size(); o++)
        {
            const thcGenEdit::Chain &oc = was.chains[o];

            if (oc.name != nc.name)
                continue;

            if (sameChain(oc, nc))
                plan.keepChains.insert(nc.name);

            /* Another take: every stage built again from the new seed. */
            if (oc.hasSeed != nc.hasSeed || oc.seed != nc.seed ||
                (!nc.hasSeed && !sameSeed))
                break;

            for (size_t s = 0; s < nc.stages.size(); s++)
            {
                const thcGenEdit::Stage &ns = nc.stages[s];

                if (ns.name.empty() || thcGenEdit::isNodeStage(ns))
                    continue;

                for (size_t t = 0; t < oc.stages.size(); t++)
                    if (sameStage(oc.stages[t], ns))
                    {
                        plan.keep.insert(std::make_pair(nc.name, ns.name));
                        break;
                    }
            }

            break;
        }
    }
}

bool
thcGenDiff::plan (const std::string &oldPath, const std::string &newPath,
                  const std::set<std::string> &changedFiles,
                  thcScheduler::EditPlan &plan,
                  std::set<std::string> &keepKnobValues, std::string &why)
{
    Doc was, now;

    if (thcGenEdit::describe(oldPath, was, why) != thcGenEdit::OK ||
        thcGenEdit::describe(newPath, now, why) != thcGenEdit::OK)
        return false;

    planOf(was, now, changedFiles, plan, keepKnobValues);

    return true;
}

bool
thcGenDiff::apply (thcScheduler &live,
                   const std::map<std::string, thcPlugin *> &plugins,
                   const std::string &oldPath, const std::string &newPath,
                   const std::set<std::string> &changedFiles,
                   std::vector<std::string> &errors,
                   const std::function<void (void)> &beforeAdopt,
                   const Doc *was, Doc *now)
{
    errors.clear();

    thcScheduler::EditPlan edit;
    std::set<std::string> keepKnobValues;
    std::string why;

    Doc read, described;

    if (was == nullptr)
    {
        if (thcGenEdit::describe(oldPath, read, why) != thcGenEdit::OK)
        {
            errors.push_back(why);
            return false;
        }

        was = &read;
    }

    if (now == nullptr)
        now = &described;

    if (thcGenEdit::describe(newPath, *now, why) != thcGenEdit::OK)
    {
        errors.push_back(why);
        return false;
    }

    planOf(*was, *now, changedFiles, edit, keepKnobValues);

    thcScheduler next(live.synth());
    thcGenLoader loader(plugins);

    live.prepareEdit(next, keepKnobValues);

    if (!loader.load(newPath, &next))
    {
        errors = loader.errors();
        return false;
    }

    if (beforeAdopt)
        beforeAdopt();

    if (!live.adopt(next, edit, why))
        errors.push_back(why);

    return true;
}
