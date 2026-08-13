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
#include <system_error>

#include "think.h"

#include "gthPatchfile.h"
#include "gui-util.h"

gthPatchManager *gthPatchManager::instance_ = NULL;

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

    if (patches_[chan])
    {
        /* keep copy of amplitude */
        amparg = new thArg (synth->getChanArg(chan, "amp"));
        delete patches_[chan];
        patches_[chan] = NULL;
    }

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
        r = false;
    }
    else
    {
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

    synth->removeChan(chan);
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

/* XXX: add error checking */
bool gthPatchManager::parse (const string &filename, int chan)
{
    FILE *prefsFile;
    char buffer[256];
    bool seen_dsp = false;
    thSynth *synth = thSynth::instance();
    PatchFileArgs arglist;

    /* Opened by the resolved path, recorded by the name as given -- see
       resolvePatch. A thinkrc that says "leads/SuperRes.patch" stays saying
       that across a save rather than being rewritten to wherever this
       particular install happens to keep its patches. */
    if ((prefsFile = fopen(resolvePatch(filename).c_str(), "r")) == NULL)
    {
        return false;
    }

    if (patches_[chan])
        delete patches_[chan];

    patches_[chan] = new PatchFile;
    patches_[chan]->filename = filename;
    patches_[chan]->dirty = false;

    while (fgets(buffer, 256, prefsFile) != NULL)
    {
        trim_leadspc(buffer);

        /* Strip the trailing newline -- but strlen can be 0 (a line starting
           with a NUL byte), and buffer[-1] is not ours to write. */
        size_t len_ = strlen(buffer);

        if (len_ > 0 && buffer[len_ - 1] == '\n')
            buffer[len_ - 1] = '\0';

        if (buffer[0] == '\0' || buffer[0] == '#')
            continue;

        char *argPtr = strchr(buffer, ' ');
        if (argPtr == NULL)
            continue;

        *argPtr++ = '\0';
        string key = buffer;

        trim_leadspc(argPtr);

        if (*argPtr)
        {
            int len = 1;
            char *comCnt;

            /* Parse special info field */
            if (key == "info")
            {
                /* first find the prop name */
                char* p = strchr(argPtr, ' ');

                /* An `info' line with no third field (`info foo') gave a NULL
                   here and the write below went through it. */
                if (p == NULL)
                {
                    goto owned;
                }

                *p++ = '\0';

                if (*p)
                {
                    /* Replace escaped newlines. */
                    string t = p;

                    /* NB: size_type, not unsigned int. find() returns a 64-bit
                       size_t; truncating npos to 32 bits gives 0xFFFFFFFF,
                       which compares unequal to npos, so the "not found" case
                       entered the loop and replace() threw out_of_range. This
                       worked in 2005 because size_t was 32 bits. */
                    string::size_type i;

                    while ((i = t.find ("\\n")) != string::npos)
                        t.replace (i, 2, "\n");

                    /* Now argPtr is the property name */
                    patches_[chan]->info[argPtr] = t;
                }
                else
                {
                    goto owned;
                }
            }
            else
            {
            
            for (comCnt=strchr(argPtr,',');comCnt;comCnt = strchr(++comCnt,','))
            {
                len++;
            }

            /* Was a `new string*[len+1]' of individually `new'ed strings, freed
               on no path at all -- one leak per comma-separated field per line
               of every patch file, including the three `goto owned' exits. A
               vector cleans up even when the goto jumps out of this block. */
            vector<string> values;
            values.reserve(len);

            for (int i = 0; i < len; i++)
            {
                char *comPtr = strchr(argPtr, ',');
                if (comPtr)
                    *comPtr = '\0';

                values.push_back(string(argPtr));

                /* `argPtr = comPtr+1' ran before this check, so on the last
                   field it formed NULL+1 (undefined) before bailing out. */
                if (comPtr == NULL)
                {
                    break;
                }

                argPtr = comPtr+1;
            }

            if (values.empty())
            {
                /* erroneous directive ... */
                goto owned;
            }

            arglist[key] = strtof(values[0].c_str(), NULL);

            /* XXX: handle specific cases here for now */
            if (key == "dsp")
            {
                patches_[chan]->dspFile = values[0];

                const string f = resolveDsp(values[0]);

                if (synth->loadTree(f.c_str(), chan,
                                    TH_DEFAULT_CHAN_AMP) == NULL)
                    goto owned;

                seen_dsp = true;
            }
            else
            {
                thArg *arg = synth->getChanArg(chan, key);
                if (arg == NULL)
                {
                    thArg *arg = new thArg(key, arglist[key]);
                    synth->setChanArg(chan, arg);
                }
                else
                {
                    arg->setValue(arglist[key]);
                }
            }
            }
        }
        
    }

    /* OK, as far as we can tell */
    if (seen_dsp)
    {
        fclose(prefsFile);
        patches_[chan]->args = arglist;
    
        return true;
    }

owned:
    fclose(prefsFile);
    delete patches_[chan];
    patches_[chan] = NULL;
    return false;
}

bool gthPatchManager::savePatch (const string &filename, int chan)
{
    FILE *prefsFile;
    thArgMap args = getChannelArgs(chan);
    time_t t = time(NULL);

    if (patches_[chan] == NULL)
        return false;

    if ((prefsFile = fopen(filename.c_str(), "w")) == NULL)
        return false;

    printf("Saving %s\n", filename.c_str());
    fprintf(prefsFile,
        "# Thinksynth Patch File\n#\n# Generated by Thinksynth %s\n# %s\n\n",
           PACKAGE_VERSION, ctime(&t));
    fprintf(prefsFile, "dsp %s\n\n", patches_[chan]->dspFile.c_str());

    for (PatchFileInfo::iterator k = patches_[chan]->info.begin();
        k != patches_[chan]->info.end(); k++)
    {
        /* replace with \n */
        string t = k->second;

        /* size_type, not unsigned int -- see the matching note in parse(). */
        string::size_type i;

        if (t.size() > 0)
        {
            while ((i = t.find("\n")) != string::npos)
                t.replace(i, 1, "\\n");
    
            fprintf(prefsFile, "info %s %s\n", k->first.c_str(), t.c_str());
        }
    }
    
    for (thArgMap::iterator j = args.begin();
         j != args.end(); j++)
    {
        if (j->second->widgetType() != j->second->HIDE)
            fprintf(prefsFile, "%s %f\n", j->first.c_str(), (*j->second)[0]);
    }

    fclose(prefsFile);

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
