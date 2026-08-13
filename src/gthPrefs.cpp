/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <filesystem>
#include <system_error>

#include <glibmm/miscutils.h>

#include "think.h"

#include "gthPrefs.h"
#include "gthPatchfile.h"
#include "gui-util.h"

namespace fs = std::filesystem;

gthPrefs *gthPrefs::instance_ = NULL;

#if 0
static void remove_string(char *line, int index, int numchars)
{
    /* removes a specific number of chars from a string starting 
     * at the position index. */
    unsigned int i;
    
    for (i = index; i + numchars < strlen(line); i++)
        line[i] = line[i + numchars];
    line[i] = '\0';
}
#endif

/* Where the preferences live.
 *
 * This was `getenv("HOME") + "/" + ".thinkrc"', with the getenv result
 * dereferenced unchecked -- HOME is not guaranteed to be set, and on Windows
 * it usually is not. Glib::get_user_config_dir() is the portable answer and
 * is already a dependency: it gives $XDG_CONFIG_HOME or ~/.config on Unix,
 * ~/Library/Application Support on macOS, and %LOCALAPPDATA% on Windows.
 *
 * That does move the file, so legacyPrefsPath() below keeps the old location
 * readable. Nothing writes there any more.
 */
string gthPrefs::prefsPath (void)
{
    return (fs::path(Glib::get_user_config_dir()) / PACKAGE_NAME / PREFS_FILE)
           .string();
}

/* ~/.thinkrc, or "" if HOME is unset. Read-only, and only when the current
   location has nothing in it -- otherwise a saved master gain, patch path and
   MIDI map would silently vanish on upgrade. */
string gthPrefs::legacyPrefsPath (void)
{
    const char *home = getenv("HOME");

    if (home == NULL || *home == 0)
        return "";

    return (fs::path(home) / LEGACY_PREFS_FILE).string();
}

gthPrefs::gthPrefs (void)
{
    prefsPath_ = prefsPath();

    if (instance_ == NULL)
        instance_ = this;
}

gthPrefs::gthPrefs (const string &path)
{
    prefsPath_ = path;

    if (instance_ == NULL)
        instance_ = this;
}

gthPrefs::~gthPrefs (void)
{
    /* The map owns every array in it -- Set() and Load() both hand ownership
       over -- so this is where they end. Not strictly necessary for a
       singleton that dies with the process, but leaving it out means the
       ownership rule is true everywhere except the one place that would
       demonstrate it, and it puts noise in front of anyone running this under
       LeakSanitizer. */
    for (map<string, string**>::iterator it = prefs_.begin();
         it != prefs_.end(); ++it)
    {
        if (it->second == NULL)
            continue;

        for (int i = 0; it->second[i] != NULL; i++)
            delete it->second[i];

        delete [] it->second;
    }

    prefs_.clear();

    if (instance_ == this)
        instance_ = NULL;
}

void gthPrefs::Load (void)
{
    FILE *prefsFile;
    char buffer[256];

    debug("loading preferences");

    if ((prefsFile = fopen(prefsPath_.c_str(), "r")) == NULL)
    {
        /* Nothing at the current location. Before falling back to the
           system-wide defaults, look where preferences used to live -- a
           saved master gain, patch path and MIDI map should survive the move
           to the XDG directory rather than silently reverting. */
        const string legacy = legacyPrefsPath();

        if (!legacy.empty() &&
            (prefsFile = fopen(legacy.c_str(), "r")) != NULL)
        {
            printf("migrating preferences from %s\n", legacy.c_str());
            printf("  (they will be saved to %s)\n", prefsPath_.c_str());
        }
        else if ((prefsFile = fopen(DEFAULT_THINKRC, "r")) == NULL)
        {
            /* just give up */
            fprintf(stderr, "could not open %s or " DEFAULT_THINKRC "\n",
                prefsPath_.c_str());
            return;
        }
        else
            printf("opened default configuration " DEFAULT_THINKRC "\n");
    }

    while (fgets(buffer, 256, prefsFile) != NULL)
    {
        trim_leadspc(buffer);
        buffer[strlen(buffer)-1] = '\0';

        if (buffer[0] == '\n' || buffer[0] == '#')
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

            for (comCnt=strchr(argPtr,',');comCnt;comCnt = strchr(++comCnt,','))
            {
                len++;
            }

            string **values = new string *[len+1];

            for (int i = 0; i < len; i++)
            {
                char *comPtr = strchr(argPtr, ',');
                if (comPtr)
                    *comPtr = '\0';

                values[i] = new string(argPtr);

                /* No comma means this was the last value, and there is
                   nothing after it to point at. `argPtr = comPtr + 1' ran
                   anyway, which is arithmetic on a null pointer: undefined,
                   and it produced a garbage pointer that the next strchr
                   would have read had the loop not been about to end.
                   `len' is the comma count plus one, so this is always the
                   final iteration and no value is skipped.

                   Not new -- every single-valued preference takes this path,
                   dspdir and autoconnect included -- but mastergain is the
                   one that made someone look. */
                if (comPtr == NULL)
                    break;

                argPtr = comPtr + 1;
            }

            values[len] = NULL;

            /* XXX: handle specific cases here for now */
            bool consumed = false;

            if (key == "mastergain" && values[0])
            {
                thSynth *s = thSynth::instance();

                if (s)
                    s->setMasterGain(atof(values[0]->c_str()));

                consumed = true;
            }
            else if (key == "channel" && values[0] && values[1])
            {
                int chan = atoi(values[0]->c_str());
                gthPatchManager *patchMgr = gthPatchManager::instance();
                patchMgr->loadPatch(*values[1], chan);

                /* Amplitude setting */
                if (values[2] != NULL)
                {
                    thSynth *s = thSynth::instance();
                    thArg *arg = new thArg("amp", atof(values[2]->c_str()));

                    s->setChanArg(chan, arg);
                }

                consumed = true;
            }

            if (consumed)
            {
                /* Acted on rather than stored -- Save() writes mastergain
                   back from the synth and channels from the patch manager.
                   Nothing else owns the array, so it has to go here; both
                   branches used to walk away from it. */
                for (int i = 0; i < len; i++)
                    delete values[i];

                delete[] values;
            }
            else
            {
                prefs_[key] = values;
            }
        }
    }
}

void gthPrefs::Save (void)
{
    gthPatchManager *patchMgr = gthPatchManager::instance();
    FILE *prefsFile;

    /* $HOME always existed; a per-application subdirectory of the config
       directory may not, and fopen will not make one. */
    std::error_code ec;
    fs::create_directories(fs::path(prefsPath_).parent_path(), ec);

    if ((prefsFile = fopen(prefsPath_.c_str(), "w")) == NULL)
    {
        fprintf(stderr, "%s: %s\n", prefsPath_.c_str(), strerror(errno));
        return;
    }

    debug("writing to '%s'", prefsPath_.c_str());

    fprintf(prefsFile, "# %s configuration file\n", PACKAGE_STRING);
    fprintf(prefsFile, "# lines beginning with '#' are comments\n\n");
    
    /* save variables here */
    for (map<string, string**>::const_iterator i = prefs_.begin();
         i != prefs_.end(); i++)
    {
        string key = i->first;
        string **values = i->second;
        
        if (values == NULL)
            continue;
        
        fprintf(prefsFile, "%s ", key.c_str());

        for (int j = 0; values[j]; j++)
        {
            fprintf(prefsFile, "%s", values[j]->c_str());

            if (values[j+1])
                fprintf(prefsFile, ",");
        }

        fprintf(prefsFile, "\n");
    }

    /* master output gain */
    {
        thSynth *synth = thSynth::instance();

        if (synth)
            fprintf(prefsFile, "mastergain %f\n", synth->masterGain());
    }

    /* save channel mappings */
    {
        int chans = patchMgr->numPatches();
        thSynth *synth = thSynth::instance();

        for (int i = 0; i < chans; i++)
        {
            gthPatchManager::PatchFile *patch = patchMgr->getPatch(i);

            if (patch == NULL)
                continue;

            string file = patch->filename;

            /* after all, the .dsp file is the determining factor in a
               channel */
            if (file.length() > 0)
            {
                thArg *amp = synth->getChanArg(i, "amp");

                /* getChanArg returns NULL for a channel with no amp, and this
                   runs on every exit including from the signal handler. */
                fprintf(prefsFile, "channel %d,%s,%d\n", i, file.c_str(),
                        amp ? (int)((*amp)[0]) : MIDIVALMAX);
            } 
        }
    }

    fclose(prefsFile);
}

string **gthPrefs::Get (const string &key)
{
    return prefs_[key];
}

/* Takes ownership of `vals', and lets go of whatever was under that key.
 *
 * It always took ownership -- nothing else keeps a pointer to the array -- but
 * it used to overwrite the entry and leak the old one. Harmless for a key set
 * once at exit; not harmless for the ones set over and over while the program
 * runs. "window" is written on every resize, "patchdir" and "dspdir" on every
 * browse, and "theme" on every pick from the Appearance menu, which is what
 * made someone look.
 *
 * Fixed here rather than at the six call sites: they would all have needed the
 * same few lines, and the next one added would have been the next leak.
 */
void gthPrefs::Set (const string &key, string **vals)
{
    map<string, string**>::iterator it = prefs_.find(key);

    /* Guarded against a caller handing back the array it was already given,
       which would otherwise free what it is about to store. */
    if (it != prefs_.end() && it->second != NULL && it->second != vals)
    {
        for (int i = 0; it->second[i] != NULL; i++)
            delete it->second[i];

        delete [] it->second;
    }

    prefs_[key] = vals;
}
