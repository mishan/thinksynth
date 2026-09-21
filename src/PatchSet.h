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

#ifndef PATCH_SET_H
#define PATCH_SET_H 1

/*
 * Sixteen slots, each holding what a channel was given.
 *
 * gthPatchManager minus the filesystem. What is left once PatchFile has the
 * format and PatchApply has the loading is bookkeeping -- which channel holds
 * which document, whether it has been edited since it was read, what it was
 * called, and telling anybody watching when one of those changes -- and none
 * of that needs a file or a synth either.
 *
 * INSTANTIATED, NOT A SINGLETON. The singleton is the application's idea and
 * stays in the application: gthPatchManager still has an instance() because
 * every window in src/gui/ reaches the patches through one. The browser makes
 * its own, in thinkweb.cpp, and a harness makes one per test. A class that
 * could only exist once would have been the reason this could not be shared.
 *
 * WHAT A SLOT IS FOR, beyond holding a document. Three fields the format has
 * no place for:
 *
 *   filename   what the slot was given, as given -- `leads/SuperRes.patch',
 *              not the path it resolved to, so a config stays portable
 *              across a save and across an install that has moved.
 *
 *   dirty      anything changed since it was read or written. Nothing here
 *              saves by itself, so this is what stands between a session's
 *              work and losing it, and it is what a Save button is for: with
 *              nothing changed there is nothing to write, and a button that
 *              is always live says nothing about whether it is worth
 *              pressing.
 *
 *   generation which load this is, counted once across the program. Identity,
 *              for anything that has to know whether the patch on a channel
 *              is still the one it put there. The filename cannot answer
 *              that -- somebody who loads their own copy of amb01.dsp onto a
 *              channel a piece filled has still replaced it, and a composer
 *              comparing the name would decide the patch was its own and take
 *              it away from them. Nor can the slot's address, which the
 *              allocator is free to hand out again. A number that only ever
 *              goes up cannot be mistaken for a previous one.
 */

#include <sigc++/sigc++.h>

#include <string>
#include <vector>

#include "PatchFile.h"

/* Everything changed at once: a patch loaded, unloaded or replaced. What
   redraws a tab list. */
typedef sigc::signal<void()> type_signal_patches_changed;

/* Which channel's patch has been edited, or has just been saved and so has
   not. Both windows show a Save button and neither owns the patch. */
typedef sigc::signal<void(int)> type_signal_patch_dirty;

/* A file that would not load, by the name it was asked for. */
typedef sigc::signal<void(const char*)> type_signal_patch_load_error;

class thPatchSet
{
public:
    /* One channel's worth. A slot exists or it does not; there is no empty
       document, because a document with no `dsp' in it is not a patch. */
    struct Slot
    {
        thPatchDoc doc;
        string filename;
        bool dirty;
        unsigned generation;

        Slot (void);
    };

    explicit thPatchSet (int slots = 16);
    ~thPatchSet (void);

    int count (void) const { return (int)slots_.size(); }

    /* What is on a channel, or NULL. The pointer is the set's and stays
       valid until that channel is given something else or emptied. */
    Slot *get (int chan);
    const Slot *get (int chan) const;

    bool loaded (int chan) const { return get(chan) != NULL; }

    /* Puts a document in a slot, replacing whatever was there and taking a
     * fresh generation.
     *
     * `dirty' is false for a document that came out of a file and true for
     * one that did not: a patch given a graph and nothing else has been
     * changed by definition, since there is no file holding what is on
     * screen. Returns the slot, or NULL for a channel this set has not got.
     */
    Slot *put (int chan, const thPatchDoc &doc, const string &filename,
               bool dirty);

    /* Empties one. False for a channel that was already empty, so a caller
       can tell "there was nothing to do" from "it is done". */
    bool clear (int chan);

    /* Cheap and idempotent: emits only on the change, so a slider drag does
       not fire per pixel. */
    void markDirty (int chan);
    bool isDirty (int chan) const;

    /* Saved, under this name. The other half of markDirty, and the reason
       the name is an argument: a Save As gives the slot a new one. */
    void markSaved (int chan, const string &filename);

    type_signal_patches_changed signal_patches_changed (void)
    {
        return m_signal_patches_changed;
    }
    type_signal_patch_dirty signal_patch_dirty (void)
    {
        return m_signal_patch_dirty;
    }
    type_signal_patch_load_error signal_patch_load_error (void)
    {
        return m_signal_patch_load_error;
    }

    /* For a shell that has done something the set cannot see -- the
       application's newPatch and setEffect go through the synth themselves
       and then say so. */
    void emitChanged (void) { m_signal_patches_changed(); }
    void emitLoadError (const string &name)
    {
        m_signal_patch_load_error(name.c_str());
    }

private:
    /* Pointers, so that an empty channel is a null rather than a document
       with a flag on it saying to ignore it. */
    vector<Slot *> slots_;

    type_signal_patches_changed  m_signal_patches_changed;
    type_signal_patch_dirty      m_signal_patch_dirty;
    type_signal_patch_load_error m_signal_patch_load_error;
};

#endif /* PATCH_SET_H */
