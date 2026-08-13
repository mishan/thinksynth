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

#ifndef GTH_PATCHFILE_H
#define GTH_PATCHFILE_H

#define NUM_PATCHES 16

typedef sigc::signal<void()> type_signal_patches_changed;

/* Which channel's patch changed, or stopped being changed. */
typedef sigc::signal<void(int)> type_signal_patch_dirty;
typedef sigc::signal<void(const char*)> type_signal_patch_load_error;

class thArg;
class thMidiChan;

class gthPatchManager
{
public:
    gthPatchManager (int numPatches=NUM_PATCHES);
    ~gthPatchManager (void);

    static gthPatchManager *instance (void);

    bool newPatch (const string &dspName, int chan);
    bool loadPatch (const string & filename, int chan);
    bool savePatch (const string &filename, int chan);
    bool unloadPatch (int chan);
    bool isLoaded (int chan);

    int numPatches (void) {
        return numPatches_;
    }

    thArgMap getChannelArgs (int chan);

    /* A patch stores whatever name it was given -- usually a bare "ts1.dsp",
       since that is what the file selector and the .patch files carry. Turns
       one into a path that can actually be opened: absolute names and names
       that resolve from the cwd are left alone, anything else is looked for
       in DSP_PATH. Returns the input unchanged if nothing works, so callers
       can report the name the user typed. */
    static string resolveDsp (const string &dspName);

    /* The same, for the .patch itself.
     *
     * A patch used to be fopen()'d exactly as named, which is why thinkrc had
     * to spell its channels out absolutely: a relative name only worked from
     * whichever directory it happened to be relative to. Searching means a
     * config file can say "leads/SuperRes.patch" and still be right after the
     * install moves -- which for a .app, a Windows zip or a Flatpak is not a
     * hypothetical, since the path the build was configured with is a
     * directory the user has never had.
     *
     * As with resolveDsp, the name is resolved for *opening* and the name as
     * given is what gets recorded, so a portable config stays portable
     * across a save. */
    static string resolvePatch (const string &patchName);

    typedef map<string, float> PatchFileArgs;
    typedef map<string, string> PatchFileInfo;
    struct PatchFile {
        PatchFileArgs args;
        PatchFileInfo info;

        string dspFile;
        string filename;

        /* Anything changed since it was loaded or last written.
         *
         * Nothing here saves by itself -- a .patch is only ever written by
         * someone clicking Save -- so this is what stands between a session's
         * work and losing it. It is also what Save is for: with nothing
         * changed there is nothing to write, and a Save button that is always
         * live says nothing about whether it is worth pressing. */
        bool dirty;
    };

    PatchFile *getPatch (int chan)
    {
        if ((chan < 0) || (chan >= NUM_PATCHES))
            return NULL;

        return patches_[chan];
    }

    /* Says a patch has been edited, or has just been saved and so has not.
       Both windows show a Save button and neither owns the patch. */
    type_signal_patch_dirty signal_patch_dirty (void) {
        return m_signal_patch_dirty;
    }

    /* Marks a channel's patch as edited. Cheap and idempotent: it emits only
       on the change, so a slider drag does not fire per pixel. */
    void markDirty (int chan);
    bool isDirty (int chan);

    type_signal_patches_changed signal_patches_changed (void) {
        return m_signal_patches_changed;
    }
    type_signal_patch_load_error signal_patch_load_error (void) {
        return m_signal_patch_load_error;
    }
    
private:
    bool parse (const string &filename, int chan);

    int numPatches_;
    PatchFile **patches_;
    static gthPatchManager *instance_;
    type_signal_patches_changed m_signal_patches_changed;
    type_signal_patch_dirty m_signal_patch_dirty;
    type_signal_patch_load_error m_signal_patch_load_error;
};

#endif /* GTH_PATCHFILE_H */
