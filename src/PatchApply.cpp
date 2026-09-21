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
 * A document onto a channel. See PatchApply.h for the order and for what is
 * deliberately left in the shells.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "think.h"

#include "PatchApply.h"

string thPatchResolveDsp (const string &name)
{
    if (name.empty())
        return name;

    /* A .patch names its graph by bare filename -- `dsp ts1.dsp' -- so this
       has to search. It used to try exactly two places, the name as given and
       DSP_PATH, which meant a patch only loaded if you were standing in the
       right directory or had run `make install'. That looked like it worked
       for a long time on a machine with a stale /usr/local install on it. */
    const string found =
        thUtil::findDataFile(name, "dsp", "THINK_DSP_PATH", DSP_PATH);

    return found.empty() ? name : found;
}

thPatchApplied thPatchApply (thSynth *synth, int channel,
                             const thPatchDoc &doc)
{
    thPatchApplied out;

    if (synth == NULL)
    {
        out.why = "there is no synth";
        return out;
    }

    if (channel < 0 || channel >= synth->midiChanCount())
    {
        out.why = "there is no such channel";
        return out;
    }

    /* The graph, first and on its own: everything below sets values on the
     * chanargs loading it declares, and a value set before it would be thrown
     * away with the tree it was set on.
     *
     * Not 0 for the level. The third argument is the channel's amplitude, and
     * a patch loaded at zero is a patch that makes no sound until you find
     * the Patch Selector and raise it -- which looked like a broken DSP
     * rather than a volume at the bottom of its range. The scale is MIDI's
     * 0..127; why TH_DEFAULT_CHAN_AMP sits where it does is argued where it
     * is defined. */
    if (synth->loadTree(thPatchResolveDsp(doc.dsp).c_str(), channel,
                        TH_DEFAULT_CHAN_AMP) == NULL)
    {
        out.why = "could not load the graph '" + doc.dsp + "'";
        return out;
    }

    /* Past here the patch is on the channel and nothing can take it off
       again: what is left can fail one line at a time. */
    out.ok = true;

    /* The side, clamped.
     *
     * Out of range is no side rather than a refused patch -- what is lost is
     * a sidechain, and the instrument still plays. A channel naming itself is
     * the same kind of wrong and has to be caught here rather than left to
     * loadEffect, which answers a cycle with NULL: the effect would be
     * dropped from a patch that is otherwise fine, and then written back out
     * without it the next time the patch was saved.
     *
     * The synth is asked how many channels there are rather than being told,
     * because it is the one that knows. A document cannot: see
     * thPatchDoc::side for why the number is kept as written until here. */
    int side = doc.side;

    if (side >= 0 && (side >= synth->midiChanCount() || side == channel))
    {
        char buf[128];

        snprintf(buf, sizeof(buf), "side %d is not a channel this effect can "
                 "listen to; loading it without one", doc.side + 1);
        out.complaints.push_back(buf);
        side = -1;
    }

    if (!doc.effect.empty())
    {
        /* Resolved for opening, remembered as given -- thPatchResolveDsp's
           rule, and an effect is found the way a graph is: a piece or a patch
           that only loaded from one directory would be one you could not send
           anybody. */
        if (synth->loadEffect(thPatchResolveDsp(doc.effect).c_str(), channel,
                              side) == NULL)
        {
            /* Not a failed patch. The instrument is up and playable; what is
               missing is a delay. Saying so beats refusing a patch somebody
               can still use. */
            out.complaints.push_back("could not load the effect '" +
                                     doc.effect + "'; the patch is loaded "
                                     "without it");
        }
        else
        {
            out.effect = doc.effect;
            out.side = side;
        }
    }

    /* And the values, in whatever order the map hands them over -- which is
       not the file's: one map holds both kinds and it sorts by name, so
       `fx.wet' comes before `res' (PatchFile.cpp says the same where it
       writes them in two passes to keep the file's order). Nothing here
       depends on the order, because the effect is on the channel before this
       loop starts and an `fx.' name has somewhere to land from the first
       one. */
    for (map<string, vector<float> >::const_iterator j = doc.args.begin();
         j != doc.args.end(); ++j)
    {
        const string &key = j->first;
        const vector<float> &values = j->second;

        if (values.empty())
            continue;

        thArg *arg = synth->getChanArg(channel, key);

        if (arg == NULL)
        {
            /* An unknown `fx.' name is not invented. See thSynth::setChanArg:
               the tolerance for names no graph declares belongs to the
               instrument's side, where the corpus has a history of them, and
               an invented one here would land in a map nothing reads. */
            if (key.compare(0, strlen(TH_EFFECT_PREFIX),
                            TH_EFFECT_PREFIX) == 0)
            {
                out.complaints.push_back("no effect parameter called '" +
                                         key + "'");
                continue;
            }

            synth->setChanArg(channel, new thArg(key, &values[0],
                                                 (int)values.size()));
        }
        else if (values.size() == 1)
        {
            /* A single float is safe to write while the audio thread reads;
               a longer one reallocates, so it goes through setChanArg, which
               queues the swap. thArg::setValue says so at both overloads. */
            arg->setValue(values[0]);
        }
        else
        {
            synth->setChanArg(channel, new thArg(key, &values[0],
                                                 (int)values.size()));
        }
    }

    return out;
}

/* Every value an arg holds, not only its first.
 *
 * A thArg has always held a list and the format has always written
 * `value[,value]', but the writer took `(*arg)[0]' and the reader took the
 * first field -- so a parameter declared as a list was quietly flattened by
 * the first Save anybody pressed. The reading is the page's now, on both
 * sides of the file; this is the writing to match. */
static vector<float> allValues (thArg *arg)
{
    vector<float> out;

    for (unsigned int i = 0; i < arg->len(); i++)
        out.push_back((*arg)[i]);

    return out;
}

thPatchDoc thPatchCapture (thSynth *synth, int channel, const thPatchDoc &was)
{
    thPatchDoc doc = was;

    /* The document's own args are what it was read with; what goes back out
       is what the channel holds now, which is the whole point of a Save. */
    doc.args.clear();

    /* Complaints are about lines in a file, and this document is not one that
       came from a file. Carrying them would put somebody else's typo in the
       output of a Save. */
    doc.complaints.clear();

    if (synth == NULL)
        return doc;

    thMidiChan *chan = synth->getChannel(channel);

    if (chan != NULL)
    {
        thArgMap args = chan->args();

        for (thArgMap::iterator j = args.begin(); j != args.end(); j++)
            if (j->second && j->second->widgetType() != j->second->HIDE)
                doc.args[j->first] = allValues(j->second);
    }

    /* And the effect's, under the name the rest of the engine addresses them
     * by. A second map on the synth's side, so a patch that sets `a' and an
     * effect that declares one are two lines and two numbers.
     *
     * Only where there is an effect to hold them: values with no file to
     * attach them to are values the reader refuses one by one, since it has
     * no effect on the channel to look their names up in. thPatchCompose
     * drops them for the same reason, so this is the cheaper half of one
     * rule. */
    if (!doc.effect.empty())
    {
        thArgMap fxargs = synth->getEffectArgs(channel);

        for (thArgMap::iterator j = fxargs.begin(); j != fxargs.end(); j++)
            if (j->second && j->second->widgetType() != j->second->HIDE)
                doc.args[string(TH_EFFECT_PREFIX) + j->first] =
                    allValues(j->second);
    }

    return doc;
}
