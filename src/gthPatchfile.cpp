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

#include "config.h"

#include <cassert>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "think.h"

#include "gthPatchfile.h"
#include "PatchApply.h"
#include "PatchFile.h"

gthPatchManager *gthPatchManager::instance_ = NULL;

gthPatchManager::gthPatchManager (int numPatches)
    : patches_(numPatches)
{
    if (instance_ == NULL)
        instance_ = this;
}

gthPatchManager::~gthPatchManager (void)
{
    if (instance_ == this)
        instance_ = NULL;
}

gthPatchManager *gthPatchManager::instance (void) {
    if (instance_ == NULL)
        instance_ = new gthPatchManager;
    
    return instance_;
}

string gthPatchManager::resolveDsp (const string &dspName)
{
    /* PatchApply's, because the page resolves a graph the same way and there
       is no second answer to give: see the header comment there for why this
       is the one lookup that crosses and resolvePatch below is not. Kept as a
       static on the manager because every call site in src/gui/ names it that
       way and a patch's graph is what they are asking about. */
    return thPatchResolveDsp(dspName);
}

string gthPatchManager::resolvePatch (const string &patchName)
{
    if (patchName.empty())
        return patchName;

    const string found =
        thUtil::findDataFile(patchName, "patches", "THINK_PATCH_PATH",
                             PATCH_PATH);

    return found.empty() ? patchName : found;
}

bool gthPatchManager::newPatch (const string &dspName, int chan)
{
    if ((chan < 0) || (chan >= patches_.count()))
        return false;

    thSynth *synth = thSynth::instance();
    thArg *amparg = NULL;

    /* Read before the load, because loadTree is what replaces the channel
       the value is being read off. Whether the old slot survives is decided
       below, after we know if there is a new one. */
    if (patches_.loaded(chan))
        amparg = new thArg (synth->getChanArg(chan, "amp"));

    /* Load the resolved path but remember the name as given, so a patch saved
       afterwards still carries the short name it came with. */
    /* Not 0. The third argument is the channel's amplitude, and a patch
       loaded at zero is a patch that makes no sound until you find the Patch
       Selector and raise it -- which looked like a broken DSP rather than a
       volume at the bottom of its range. The scale here is MIDI's 0..127; why
       TH_DEFAULT_CHAN_AMP sits where it does is argued where it is defined. */
    if (synth->loadTree(resolveDsp(dspName).c_str(), chan,
                        TH_DEFAULT_CHAN_AMP) == NULL)
    {
        /* The old slot used to be deleted before the load was attempted, so
           a DSP that failed to parse left the channel still playing the
           previous graph with nothing here describing it: no tab contents,
           no filename, nothing able to unload it. loadTree does not touch
           the channel unless it succeeds, so neither does this -- the
           failure is now a failure to change anything. */
        delete amparg;

        patches_.emitChanged();

        return false;
    }

    thPatchDoc doc;

    doc.dsp = dspName;

    /* A patch that has only just been given a DSP has been changed by
       definition: there is no file holding what is on screen. So: dirty,
       and no filename. put() says the patches changed. */
    patches_.put(chan, doc, string(), true);

    if (amparg != NULL)
        synth->setChanArg(chan, amparg);

    return true;
}

/* See the header. */
bool gthPatchManager::setEffect (int chan, const string &effectName,
                                 int side)
{
    PatchFile *patch = patches_.get(chan);

    /* An effect belongs to a channel and the channel is the patch, so there
       is nowhere to put one. Asking for none is already true of a channel
       with nothing on it, and answering false there would fail every
       instrument that declares no effect. */
    if (patch == NULL)
        return (chan >= 0 && chan < patches_.count()) && effectName.empty();

    thSynth *synth = thSynth::instance();

    /* Already this graph, still on the channel: leave it alone.
     *
     * Not an optimization. Reloading an effect builds a new one, and a new
     * delay line is an empty delay line -- so a piece reapplied for a reason
     * that has nothing to do with its sound (renaming a knob's label, moving
     * a stage) would cut the tail off every repeat and every reverb. The
     * instrument side already declines to rebuild a graph it recognizes, for
     * the same reason and in the same words; this is that promise kept for
     * the second graph on the channel.
     *
     * The document's effect is the right thing to test against because a
     * channel that was rebuilt underneath it arrives here with a fresh slot
     * and an empty one -- see newPatch. */
    if (!effectName.empty() && patch->doc.effect == effectName &&
        patch->doc.side == side && synth->getEffect(chan) != NULL)
        return true;

    if (effectName.empty())
    {
        /* Nothing to take off and nothing recorded: not a change, so not a
           reason to mark the patch dirty or rebuild every page. */
        if (patch->doc.effect.empty() && synth->getEffect(chan) == NULL)
            return true;

        if (!synth->removeEffect(chan))
            return false;

        patch->doc.effect.clear();
        patch->doc.side = -1;

        /* And its parameters, which have nowhere to land now. Left behind,
           they would be written back into the file under an `effect' line
           that is no longer there -- a patch the reader then refuses one
           `fx.' name at a time. thPatchCompose drops them for the same
           reason; this is the slot agreeing with it. */
        for (map<string, vector<float> >::iterator j = patch->doc.args.begin();
             j != patch->doc.args.end(); )
            if (j->first.compare(0, strlen(TH_EFFECT_PREFIX),
                                 TH_EFFECT_PREFIX) == 0)
                patch->doc.args.erase(j++);
            else
                ++j;
    }
    else
    {
        /* Resolved for opening, remembered as given -- resolveDsp's rule,
           and an effect is found the same way a graph is: a piece or a patch
           that only loaded from one directory would be one you could not
           send anybody. */
        if (synth->loadEffect(resolveDsp(effectName).c_str(), chan,
                              side) == NULL)
            return false;

        patch->doc.effect = effectName;
        patch->doc.side = side;
    }

    patches_.markDirty(chan);
    patches_.emitChanged();

    return true;
}

bool gthPatchManager::loadPatch (const string &filename, int chan)
{
    if ((chan < 0) || (chan >= patches_.count()))
        return false;

    /* parse() fills the slot, and putting a document in one is already a
       change everybody watching hears about. What is left for this to say is
       the failure, which nothing else can. */
    if (parse(filename, chan))
        return true;

    patches_.emitLoadError(filename);

    return false;
}

bool gthPatchManager::unloadPatch (int chan)
{
    if (!patches_.loaded(chan))
        return false;

    thSynth *synth = thSynth::instance();

    /* Only forget it if the audio thread was actually told to drop it. A
       dropped command means the channel is still loaded and still sounding;
       deleting the slot anyway left the graph playing with isLoaded()
       saying false, no tab contents naming it, and nothing able to unload it
       on a second attempt -- and the next thing looking for a free channel
       would take that one. removeChan says which happened, exactly so this
       can agree with it. */
    if (!synth->removeChan(chan))
        return false;

    return patches_.clear(chan);
}

bool gthPatchManager::isLoaded (int chan)
{
    return patches_.loaded(chan);
}

thArgMap gthPatchManager::getChannelArgs (int chan)
{
    if (!patches_.loaded(chan))
        return thArgMap();

    thSynth *synth = thSynth::instance();
    thMidiChan *mchan = synth->getChannel(chan);

    if (mchan == NULL)
    {
        printf("ERROR! Got NULL MidiChan\n");
        return thArgMap();
    }

    return mchan->args();
}

/* Reads a whole file. A .patch is a few hundred bytes and the reader wants a
   string, so there is nothing here to stream. */
static bool readWhole (const string &path, string &out)
{
    ifstream in(path.c_str(), ios::binary);

    if (!in)
        return false;

    ostringstream buf;

    buf << in.rdbuf();
    out = buf.str();

    return true;
}

/* A .patch onto a channel.
 *
 * What is left of the 240-line loop this used to be: find the file, read it,
 * hand the text to the one reader of the format (src/PatchFile.h), hand the
 * document to the one thing that puts one on a channel (src/PatchApply.h),
 * and record what happened. Finding the file is the only part of this the
 * browser does differently, which is why it is the only part still here.
 */
bool gthPatchManager::parse (const string &filename, int chan)
{
    /* Opened by the resolved path, recorded by the name as given -- see
       resolvePatch. A thinkrc that says "leads/SuperRes.patch" stays saying
       that across a save rather than being rewritten to wherever this
       particular install happens to keep its patches. */
    string text;

    if (!readWhole(resolvePatch(filename), text))
        return false;

    thPatchDoc doc;
    string why;

    if (!thPatchParse(text, doc, why))
    {
        fprintf(stderr, "%s: %s\n", filename.c_str(), why.c_str());
        return false;
    }

    /* Lines the reader could not use. Not failures -- the patch is loaded
       without them -- but a .patch with a typo in it used to be either
       silently two-thirds loaded or refused outright, and in neither case did
       anything name the line. */
    for (size_t i = 0; i < doc.complaints.size(); i++)
        fprintf(stderr, "%s: %s\n", filename.c_str(),
                doc.complaints[i].c_str());

    const thPatchApplied got =
        thPatchApply(thSynth::instance(), chan, doc);

    for (size_t i = 0; i < got.complaints.size(); i++)
        fprintf(stderr, "%s: %s\n", filename.c_str(),
                got.complaints[i].c_str());

    if (!got.ok)
    {
        fprintf(stderr, "%s: %s\n", filename.c_str(), got.why.c_str());
        return false;
    }

    /* What went on, not what was asked for: the effect may have failed and
       the side may have been clamped, and this is what a Save writes back. */
    doc.effect = got.effect;
    doc.side = got.side;

    /* Only now. thSynth::loadTree does not touch the channel unless it
       succeeds, so a patch naming a .dsp that will not parse is a failure to
       change anything -- rather than one that left the old graph playing with
       no slot describing it: no tab contents, no filename, and nothing able
       to unload it. newPatch makes the same promise in the same words.

       Not dirty: this is what the file says, and nothing has changed it. */
    patches_.put(chan, doc, filename, false);

    return true;
}

bool gthPatchManager::savePatch (const string &filename, int chan)
{
    PatchFile *patch = patches_.get(chan);

    if (patch == NULL)
        return false;

    /* What the file will say: the patch's own record of what it is, and the
     * channel's live values. Both halves are shared -- thPatchCapture reads
     * the channel back into a document, thPatchCompose turns one into bytes
     * -- which is what makes a .patch a browser writes open here and a .patch
     * written here open there. It is also what makes scripts/patchcheck's
     * round trip a claim about this function rather than about a fixture. */
    const thPatchDoc doc =
        thPatchCapture(thSynth::instance(), chan, patch->doc);

    time_t t = time(NULL);
    ofstream out(filename.c_str(), ios::binary | ios::trunc);

    if (!out)
        return false;

    printf("Saving %s\n", filename.c_str());

    out << thPatchCompose(doc, ctime(&t));
    out.close();

    /* A patch that did not reach the disk is not a patch that has been saved,
       and clearing the dirty flag over one would put the Save button out and
       leave the work only in memory. */
    if (!out)
        return false;

    patches_.markSaved(chan, filename);

    return true;
}
