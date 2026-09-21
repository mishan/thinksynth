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
 * The slots. See PatchSet.h for what one holds and why it is not a
 * singleton.
 */

#include "config.h"

#include "PatchSet.h"

/* Stamped on every slot, never reused, counted once across the program --
   which is why it is here and not a member. See Slot::generation for why a
   filename or an address will not do. GUI thread only, like everything
   else here. */
static unsigned patchGeneration = 0;

thPatchSet::Slot::Slot (void)
    : dirty(false), generation(++patchGeneration)
{
}

thPatchSet::thPatchSet (int slots)
    : slots_(slots > 0 ? slots : 0, (Slot *)NULL)
{
}

thPatchSet::~thPatchSet (void)
{
    for (size_t i = 0; i < slots_.size(); i++)
        delete slots_[i];
}

thPatchSet::Slot *thPatchSet::get (int chan)
{
    if (chan < 0 || chan >= (int)slots_.size())
        return NULL;

    return slots_[chan];
}

const thPatchSet::Slot *thPatchSet::get (int chan) const
{
    if (chan < 0 || chan >= (int)slots_.size())
        return NULL;

    return slots_[chan];
}

thPatchSet::Slot *thPatchSet::put (int chan, const thPatchDoc &doc,
                                   const string &filename, bool dirty)
{
    if (chan < 0 || chan >= (int)slots_.size())
        return NULL;

    delete slots_[chan];

    slots_[chan] = new Slot;
    slots_[chan]->doc = doc;
    slots_[chan]->filename = filename;
    slots_[chan]->dirty = dirty;

    m_signal_patches_changed();

    return slots_[chan];
}

bool thPatchSet::clear (int chan)
{
    if (chan < 0 || chan >= (int)slots_.size() || slots_[chan] == NULL)
        return false;

    delete slots_[chan];
    slots_[chan] = NULL;

    m_signal_patches_changed();

    return true;
}

void thPatchSet::markDirty (int chan)
{
    Slot *slot = get(chan);

    /* Idempotent: a slider drag is one edit as far as anybody watching is
       concerned, and emitting per pixel would redraw a Save button sixty
       times a second to say what it already said. */
    if (slot == NULL || slot->dirty)
        return;

    slot->dirty = true;

    m_signal_patch_dirty(chan);
}

bool thPatchSet::isDirty (int chan) const
{
    const Slot *slot = get(chan);

    return slot != NULL && slot->dirty;
}

void thPatchSet::markSaved (int chan, const string &filename)
{
    Slot *slot = get(chan);

    if (slot == NULL)
        return;

    slot->filename = filename;
    slot->dirty = false;

    m_signal_patch_dirty(chan);
    m_signal_patches_changed();
}
