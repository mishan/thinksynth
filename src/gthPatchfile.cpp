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
#include "PatchFile.h"

gthPatchManager *gthPatchManager::instance_ = NULL;

/* Stamped on every PatchFile, never reused. See the field's comment for why
   this exists rather than a filename comparison or a pointer. GUI thread
   only, like everything else here. */
static unsigned patchGeneration = 0;

gthPatchManager::PatchFile::PatchFile (void)
    : effectSide(-1), dirty(false), generation(++patchGeneration)
{
}

gthPatchManager::gthPatchManager (int numPatches)
{
    numPatches_ = numPatches;

    if (instance_ == NULL)
        instance_ = this;

    patches_ = new PatchFile*[numPatches_];

    /* init patches to NULL */
    for (int i = 0; i < numPatches_; i++)
        patches_[i] = NULL;
}

gthPatchManager::~gthPatchManager (void)
{
    if (instance_ == this)
        instance_ = NULL;

    for (int i = 0; i < numPatches_; i++)
    {
        delete patches_[i];
        patches_[i] = NULL;
    }

    delete [] patches_;   /* the array itself was never freed */
    patches_ = NULL;
}

gthPatchManager *gthPatchManager::instance (void) {
    if (instance_ == NULL)
        instance_ = new gthPatchManager;
    
    return instance_;
}

string gthPatchManager::resolveDsp (const string &dspName)
{
    if (dspName.empty())
        return dspName;

    /* A .patch names its DSP by bare filename -- `dsp ts1.dsp' -- so this has
       to search. It used to try exactly two places, the name as given and
       DSP_PATH, which meant a patch only loaded if you were standing in the
       right directory or had run `make install'. That looked like it worked
       for a long time on a machine with a stale /usr/local install on it. */
    const string found =
        thUtil::findDataFile(dspName, "dsp", "THINK_DSP_PATH", DSP_PATH);

    /* Hand back the original if nothing matched, so the error message names
       what the patch actually asked for. */
    return found.empty() ? dspName : found;
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
    /* The same guard loadPatch, unloadPatch, isLoaded and getChannelArgs all
       carry, and the one place it was missing. Nothing reaches here with a bad
       channel now the notebook always holds sixteen pages, but the first thing
       below is `delete patches_[chan]'. */
    if ((chan < 0) || (chan >= numPatches_))
        return false;

    thSynth *synth = thSynth::instance();
    thArg *amparg = NULL;
    bool r = true;

    /* Read before the load, because loadTree is what replaces the channel
       the value is being read off. Whether the old PatchFile survives is
       decided below, after we know if there is a new one. */
    if (patches_[chan])
        amparg = new thArg (synth->getChanArg(chan, "amp"));

    /* Load the resolved path but remember the name as given, so a patch saved
       afterwards still carries the short name it came with. */
    /* Not 0. The third argument is the channel's amplitude, and a patch
       loaded at zero is a patch that makes no sound until you find the Patch
       Selector and raise it -- which looked like a broken DSP rather than a
       volume at the bottom of its range. The scale here is MIDI's 0..127; why
       TH_DEFAULT_CHAN_AMP sits where it does is argued where it is defined. */
    thSynthTree *mod = synth->loadTree(resolveDsp(dspName).c_str(), chan,
                                       TH_DEFAULT_CHAN_AMP);

    if (mod == NULL)
    {
        /* The old PatchFile used to be deleted before the load was
           attempted, so a DSP that failed to parse left the channel still
           playing the previous graph with nothing here describing it: no
           tab contents, no filename, nothing able to unload it. loadTree
           does not touch the channel unless it succeeds, so neither does
           this -- the failure is now a failure to change anything. */
        r = false;
        delete amparg;
    }
    else
    {
        delete patches_[chan];

        patches_[chan] = new PatchFile;
        patches_[chan]->dspFile = dspName;

        /* A patch that has only just been given a DSP has been changed by
           definition: there is no file holding what is on screen. */
        patches_[chan]->dirty = true;

        if (amparg != NULL)
            synth->setChanArg(chan, amparg);
    }

    m_signal_patches_changed();

    return r;
}

/* See the header. */
bool gthPatchManager::setEffect (int chan, const string &effectName,
                                 int side)
{
    if ((chan < 0) || (chan >= numPatches_))
        return false;

    /* An effect belongs to a channel and the channel is the patch, so there
       is nowhere to put one. Asking for none is already true of a channel
       with nothing on it, and answering false there would fail every
       instrument that declares no effect. */
    if (patches_[chan] == NULL)
        return effectName.empty();

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
     * effectFile is the right thing to test against because a channel that
     * was rebuilt underneath it arrives here with a fresh PatchFile and an
     * empty one -- see newPatch. */
    if (!effectName.empty() && patches_[chan]->effectFile == effectName &&
        patches_[chan]->effectSide == side &&
        synth->getEffect(chan) != NULL)
        return true;

    if (effectName.empty())
    {
        /* Nothing to take off and nothing recorded: not a change, so not a
           reason to mark the patch dirty or rebuild every page. */
        if (patches_[chan]->effectFile.empty() &&
            synth->getEffect(chan) == NULL)
            return true;

        if (!synth->removeEffect(chan))
            return false;

        patches_[chan]->effectFile.clear();
        patches_[chan]->effectSide = -1;
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

        patches_[chan]->effectFile = effectName;
        patches_[chan]->effectSide = side;
    }

    patches_[chan]->dirty = true;

    m_signal_patch_dirty(chan);
    m_signal_patches_changed();

    return true;
}

bool gthPatchManager::loadPatch (const string &filename, int chan)
{
    if ((chan < 0) || (chan >= numPatches_))
        return false;

    bool r = parse(filename, chan);

    if (r)
        m_signal_patches_changed();
    else
        m_signal_patch_load_error(filename.c_str());
    
    return r;
}

bool gthPatchManager::unloadPatch (int chan)
{
    if ((chan < 0) || (chan >= numPatches_) || (patches_[chan] == NULL))
        return false;

    thSynth *synth = thSynth::instance();

    /* Only forget it if the audio thread was actually told to drop it. A
       dropped command means the channel is still loaded and still sounding;
       deleting the PatchFile anyway left the graph playing with isLoaded()
       saying false, no tab contents naming it, and nothing able to unload it
       on a second attempt -- and the next thing looking for a free channel
       would take that one. removeChan says which happened, exactly so this
       can agree with it. */
    if (!synth->removeChan(chan))
        return false;

    delete patches_[chan];
    patches_[chan] = NULL;

    m_signal_patches_changed();

    return true;
}

bool gthPatchManager::isLoaded (int chan)
{
    if ((chan < 0) || (chan >= numPatches_) || patches_[chan] == NULL)
        return false;

    return true;
}

thArgMap gthPatchManager::getChannelArgs (int chan)
{
    if ((chan < 0) || (chan >= numPatches_) || patches_[chan] == NULL)
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
 * hand the text to the one reader of the format (src/PatchFile.h), and put
 * what came back on the channel. The rules about what a line means are not
 * here any more, and neither is the browser's second copy of them.
 *
 * The order below is the format's and is not free to change: the graph first,
 * because loading one is what builds the chanargs an override would otherwise
 * be set on and thrown away with the tree; the side before the effect, because
 * the effect is built when it is loaded and its side is part of building it;
 * and the effect before its `fx.' values, because they have nowhere to land
 * until it is on the channel. savePatch writes them in that order for the same
 * reason -- the writer decides the order so the reader need not tolerate both.
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

    thSynth *synth = thSynth::instance();

    /* Before anything is thrown away. loadTree does not touch the channel
       unless it succeeds, so neither does this: a patch naming a .dsp that
       will not parse is now a failure to change anything, rather than one
       that left the old graph playing with no PatchFile describing it -- no
       tab contents, no filename, and nothing able to unload it. newPatch
       makes the same promise in the same words. */
    if (synth->loadTree(resolveDsp(doc.dsp).c_str(), chan,
                        TH_DEFAULT_CHAN_AMP) == NULL)
    {
        fprintf(stderr, "%s: could not load the graph '%s'\n",
                filename.c_str(), doc.dsp.c_str());
        return false;
    }

    delete patches_[chan];

    patches_[chan] = new PatchFile;
    patches_[chan]->filename = filename;
    patches_[chan]->dspFile = doc.dsp;
    patches_[chan]->info = doc.info;
    patches_[chan]->dirty = false;

    /* The side, clamped: out of range is no side rather than a refused patch,
     * since what is lost is a sidechain and the instrument still plays. A
     * channel naming itself is the same kind of wrong and has to be caught
     * here rather than left to loadEffect, which answers a cycle with NULL --
     * the effect would be dropped from a patch that is otherwise fine, and
     * then written back out without it the next time the patch was saved.
     *
     * Not in the document, because how many channels there are is not
     * something a file can know; see thPatchDoc::side. */
    const int side =
        (doc.side >= 0 && doc.side < numPatches_ && doc.side != chan)
        ? doc.side : -1;

    if (!doc.effect.empty())
    {
        /* Resolved for opening, remembered as given -- resolveDsp's rule. */
        if (synth->loadEffect(resolveDsp(doc.effect).c_str(), chan,
                              side) == NULL)
        {
            /* Not a failed patch. The instrument is up and playable; what is
               missing is a delay. Saying so beats refusing a patch somebody
               can still use. */
            fprintf(stderr, "%s: could not load the effect '%s'; the patch is "
                    "loaded without it\n", filename.c_str(),
                    doc.effect.c_str());
        }
        else
        {
            patches_[chan]->effectFile = doc.effect;
            patches_[chan]->effectSide = side;
        }
    }

    for (map<string, vector<float> >::const_iterator j = doc.args.begin();
         j != doc.args.end(); ++j)
    {
        const string &key = j->first;
        const vector<float> &values = j->second;

        if (values.empty())
            continue;

        patches_[chan]->args[key] = values[0];

        thArg *arg = synth->getChanArg(chan, key);

        if (arg == NULL)
        {
            /* An unknown `fx.' name is not invented. See thSynth::setChanArg:
               the tolerance for names no graph declares belongs to the
               instrument's side, where the corpus has a history of them, and
               an invented one here would land in a map nothing reads. */
            if (key.compare(0, strlen(TH_EFFECT_PREFIX),
                            TH_EFFECT_PREFIX) == 0)
            {
                fprintf(stderr, "%s: no effect parameter called '%s'\n",
                        filename.c_str(), key.c_str());
                continue;
            }

            synth->setChanArg(chan, new thArg(key, &values[0],
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
            synth->setChanArg(chan, new thArg(key, &values[0],
                                              (int)values.size()));
        }
    }

    return true;
}

/* Every value an arg holds, not only its first.
 *
 * A thArg has always held a list and the format has always written
 * `value[,value]', but the writer took `(*arg)[0]' and the reader took the
 * first field -- so a parameter declared as a list was quietly flattened by
 * the first Save anybody pressed. The browser's parser kept all of them and
 * said in a comment that it meant to; this is that reading, on both sides of
 * the file now. No shipped .patch has a multi-value line, which is what makes
 * it safe to take. */
static vector<float> allValues (thArg *arg)
{
    vector<float> out;

    for (unsigned int i = 0; i < arg->len(); i++)
        out.push_back((*arg)[i]);

    return out;
}

bool gthPatchManager::savePatch (const string &filename, int chan)
{
    /* The guard every other method here carries, and the one place it was
       missing: the next line is `patches_[chan]'. */
    if ((chan < 0) || (chan >= numPatches_) || (patches_[chan] == NULL))
        return false;

    /* What the file will say: the patch's own record of what it is, and the
     * channel's live values. The bytes come from thPatchCompose, so what a
     * Save writes and what a load reads are one description of the format --
     * which is what makes scripts/patchcheck's round trip a claim about this
     * function rather than about a test fixture. */
    thPatchDoc doc;

    doc.dsp = patches_[chan]->dspFile;
    doc.effect = patches_[chan]->effectFile;
    doc.side = patches_[chan]->effectSide;
    doc.info = patches_[chan]->info;

    thArgMap args = getChannelArgs(chan);

    for (thArgMap::iterator j = args.begin(); j != args.end(); j++)
        if (j->second && j->second->widgetType() != j->second->HIDE)
            doc.args[j->first] = allValues(j->second);

    /* And the effect's, under the name the rest of the engine addresses them
     * by. A second map on the synth's side, so a patch that sets `a' and an
     * effect that declares one are two lines and two numbers.
     *
     * Only where there is an effect line to hold them: values with no file to
     * attach them to are values the reader refuses one by one, since it has no
     * effect on the channel to look their names up in. thPatchCompose drops
     * them for the same reason, so this is the cheaper half of one rule. */
    if (!doc.effect.empty())
    {
        thArgMap fxargs = thSynth::instance()->getEffectArgs(chan);

        for (thArgMap::iterator j = fxargs.begin(); j != fxargs.end(); j++)
            if (j->second && j->second->widgetType() != j->second->HIDE)
                doc.args[string(TH_EFFECT_PREFIX) + j->first] =
                    allValues(j->second);
    }

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

    patches_[chan]->filename = filename;
    patches_[chan]->dirty = false;

    m_signal_patch_dirty(chan);
    m_signal_patches_changed();

    return true;
}

void gthPatchManager::markDirty (int chan)
{
    if ((chan < 0) || (chan >= numPatches_) || patches_[chan] == NULL)
        return;

    if (patches_[chan]->dirty)
        return;

    patches_[chan]->dirty = true;

    m_signal_patch_dirty(chan);
}

bool gthPatchManager::isDirty (int chan)
{
    if ((chan < 0) || (chan >= numPatches_) || patches_[chan] == NULL)
        return false;

    return patches_[chan]->dirty;
}
