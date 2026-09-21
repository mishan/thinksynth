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

/*
 * The application's patches: a thPatchSet, a filesystem and a singleton.
 *
 * What a .patch says is src/PatchFile.h's, what it does to a channel is
 * src/PatchApply.h's, and which channel holds which document is
 * src/PatchSet.h's -- all three toolkit-free, and all three compiled into
 * the browser's module as well. What is left here is the part that is a
 * desktop's and nothing else's: finding the file under PATCH_PATH, opening
 * it, writing it back, and being reachable as instance() from every window
 * in src/gui/.
 */

#include "PatchSet.h"

#define NUM_PATCHES 16

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
        return patches_.count();
    }

    thArgMap getChannelArgs (int chan);

    /* A patch stores whatever name it was given -- usually a bare "ts1.dsp",
       since that is what the file selector and the .patch files carry. Turns
       one into a path that can actually be opened: absolute names and names
       that resolve from the cwd are left alone, anything else is looked for
       in DSP_PATH. Returns the input unchanged if nothing works, so callers
       can report the name the user typed.

       thPatchResolveDsp's, because the page resolves a graph the same way and
       there is no second answer to give -- see PatchApply.h for why this is
       the one lookup that crosses and resolvePatch below is not. */
    static string resolveDsp (const string &dspName);

    /* Puts `effectName' on `chan' as its channel effect, or takes the
     * effect off when it is empty. The channel has to have a patch on it
     * already: an effect belongs to a channel, and loading an instrument
     * builds a new one.
     *
     * Separate from newPatch rather than a second argument to it, because
     * the two are separate actions in the interface as well: choosing an
     * effect does not reload the instrument under it.
     *
     * `side' is the channel that effect hears besides this one -- the carrier
     * a vocoder needs, the kick a compressor is keyed from -- or -1, which is
     * every effect that hears only its own. It belongs to the effect, so a
     * channel that already has this graph on it with a different side is
     * rebuilt rather than left alone. */
    bool setEffect (int chan, const string &effectName, int side = -1);

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
     * across a save.
     *
     * Not shared with the page, and deliberately: a browser has no
     * PATCH_PATH and should not pretend to. It fetches the file and hands
     * the text in. */
    static string resolvePatch (const string &patchName);

    /* One channel's slot, or NULL. The name the application has called this
       for twenty years, over thPatchSet's slot -- which holds the document
       (`doc.dsp', `doc.info', `doc.effect'), the filename it was given, the
       dirty flag and the generation. */
    typedef thPatchSet::Slot PatchFile;

    PatchFile *getPatch (int chan)
    {
        return patches_.get(chan);
    }

    /* The slots themselves, for anything that wants to watch rather than
       ask. */
    thPatchSet &set (void) { return patches_; }

    /* Says a patch has been edited, or has just been saved and so has not.
       Both windows show a Save button and neither owns the patch. */
    type_signal_patch_dirty signal_patch_dirty (void) {
        return patches_.signal_patch_dirty();
    }

    /* Marks a channel's patch as edited. Cheap and idempotent: it emits only
       on the change, so a slider drag does not fire per pixel. */
    void markDirty (int chan) { patches_.markDirty(chan); }
    bool isDirty (int chan) { return patches_.isDirty(chan); }

    type_signal_patches_changed signal_patches_changed (void) {
        return patches_.signal_patches_changed();
    }
    type_signal_patch_load_error signal_patch_load_error (void) {
        return patches_.signal_patch_load_error();
    }

private:
    bool parse (const string &filename, int chan);

    thPatchSet patches_;
    static gthPatchManager *instance_;
};

#endif /* GTH_PATCHFILE_H */
