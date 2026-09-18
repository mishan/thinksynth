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

#ifndef TH_MIDINOTE_H
#define TH_MIDINOTE_H 1

#include "thExport.h"

class THINK_API thMidiNote {
public:
    thMidiNote (thSynthTree *tree, float note, float velocity);
    thMidiNote (thSynthTree *tree);
    ~thMidiNote ();
    
    thSynthTree *synthTree (void) { return &synthTree_; }
    int id (void) const { return noteid_; }
    float note (void) const { return note_; }

    /* Audio thread. Point this voice at another pitch without rebuilding it:
     * writes the io node's `note', which misc::midi2freq reads every window,
     * and re-ids the voice so the channel can re-key it.
     *
     * The rest of the tree is untouched, which is the point -- the envelopes
     * stay where they are and any state in the graph carries, so a
     * misc::slew on the frequency slides into the new pitch instead of
     * jumping to it. `velocity' is deliberately not touched: the envelopes
     * read it every sample and a step there is a click.
     *
     * Allocation-free. thArg::setArg goes through allocate(1) on an arg that
     * is already one long, which returns the buffer it has. */
    void retune (float note);

    void process (int length);

    void setArg (const string &name, float value);
    void setArg (const string &name, const float *value, int len);

private:
    thSynthTree synthTree_;
    int noteid_;
    /* The pitch as it was asked for. noteid_ is its integer part, which is
       what the channel keys notes_ by; this is what a retune has to preserve
       when a composer asks for something between two keys. */
    float note_;
};

#endif /* TH_MIDINOTE_H */
