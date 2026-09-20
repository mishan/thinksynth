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

    /* Audio thread. A voice the channel has run out of room for.
     *
     * Stealing used to be `retireNote' and nothing else, so a voice sounding
     * at half full scale became a zero between one sample and the next --
     * dsp/bass.dsp asks for `poly = 2', and a slide up the keyboard blew that
     * budget on every step, putting a 0.53 step at the window boundary. The
     * voice is instead mixed for `samples' more with its contribution ramped
     * to nothing, and retired when the ramp runs out.
     *
     * Deliberately not a release: `r' is the instrument's, it is as long as
     * the instrument says, and a channel out of voices cannot wait that long
     * for the room. This is the cut it always was, with a slope on it.
     *
     * Linear rather than a curve. What a ramp this short has to do is get
     * the step out of the signal; at a few milliseconds the shape of it is
     * not audible, and a straight line is the one that is obviously
     * monotonic and obviously reaches zero. */
    void beginFade (int samples);

    /* The gain this voice is mixed at, `offset' samples into the window that
       is being rendered. 1 for a voice that is not fading. */
    float fadeGain (int offset) const;

    bool fading (void) const { return fadelen_ > 0; }

    /* Audio thread. Charges a rendered window against the ramp. True once
       the ramp has run out, which is when the caller retires the voice. */
    bool advanceFade (int samples);

    void setArg (const string &name, float value);
    void setArg (const string &name, const float *value, int len);

private:
    thSynthTree synthTree_;
    int noteid_;
    /* The pitch as it was asked for. noteid_ is its integer part, which is
       what the channel keys notes_ by; this is what a retune has to preserve
       when a composer asks for something between two keys. */
    float note_;

    /* The steal ramp: how long it is, and how much of it is left. Both zero
       on a voice nothing has stolen, which is what fading() reads. */
    int fadelen_, faderemaining_;
};

#endif /* TH_MIDINOTE_H */
