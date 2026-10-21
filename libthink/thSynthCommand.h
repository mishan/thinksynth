/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#ifndef TH_SYNTH_COMMAND_H
#define TH_SYNTH_COMMAND_H 1

class thMidiNote;
class thMidiChan;
class thChanEffect;
class thArg;
class thProbe;

/* GUI thread -> audio thread.
 *
 * The rule this enforces: the audio thread is the only thing that mutates
 * anything reachable from thSynth::midiChannels_. The GUI thread never reaches
 * in. It does all the *allocation* -- building a thMidiNote copy-constructs an
 * entire synth tree, which has no business happening in an audio callback --
 * and hands the finished object over, so applying a command is only pointer and
 * container manipulation.
 */
struct thSynthCommand {
    enum Type {
        NOTE_ON,        /* install `note' on `chan'                        */
        NOTE_OFF,       /* release `noteId' on `chan'                      */
        ALL_NOTES_OFF,  /* every channel                                   */
        SILENCE,        /* every voice and every effect, faded to nothing  */
        SET_CHANNEL,    /* install `channel' (may be NULL) on `chan'       */
        SET_CHAN_ARG,   /* install `arg' on `chan', replacing by name      */
        SET_EFFECT,     /* install `effect' (may be NULL) on `chan'        */
        SET_MASTER_EFFECT, /* install `effect' (may be NULL) on the mix     */
        SET_PROBE       /* install `probe' (may be NULL) in `probeSlot'    */
    };

    Type type;
    int chan;
    int noteId;
    int probeSlot;

    thMidiNote *note;
    thMidiChan *channel;
    thChanEffect *effect;
    thArg *arg;
    thProbe *probe;

    /* SET_CHAN_ARG's other half: the arg of that name `arg' displaces, or
       NULL. The GUI thread swaps the channel's map once the command is
       queued, so from then on the map owns `arg' and the command owns this
       -- the audio thread retires it once no voice points at it, and a
       command never applied deletes it. A command that could not be queued
       owns `arg' instead, and the map was never touched. */
    thArg *replaced;

    thSynthCommand (void)
        : type(NOTE_OFF), chan(0), noteId(0), probeSlot(-1),
          note(NULL), channel(NULL), effect(NULL), arg(NULL), probe(NULL),
          replaced(NULL) { }
};

/* Audio thread -> GUI thread.
 *
 * Whatever the audio thread displaces -- a note that finished playing, the
 * channel a patch load replaced, an arg that was overwritten -- it hands back
 * here rather than freeing it. Destroying a thMidiNote means tearing down a
 * whole synth tree, so this keeps that off the callback as well as making the
 * lifetime safe.
 *
 * Because the audio thread only lets go of a pointer once it is genuinely
 * unreachable from midiChannels_, there is no grace period to observe and no
 * epoch to track: whatever comes out of this queue is safe to delete.
 */
struct thRetired {
    enum Kind { NOTE, CHANNEL, EFFECT, ARG, PROBE };

    Kind kind;

    thMidiNote *note;
    thMidiChan *channel;
    thChanEffect *effect;
    thArg *arg;
    thProbe *probe;

    /* SET_CHAN_ARG's other half: the arg of that name `arg' displaces, or
       NULL. The GUI thread swaps the channel's map once the command is
       queued, so from then on the map owns `arg' and the command owns this
       -- the audio thread retires it once no voice points at it, and a
       command never applied deletes it. A command that could not be queued
       owns `arg' instead, and the map was never touched. */
    thArg *replaced;

    thRetired (void)
        : kind(NOTE), note(NULL), channel(NULL), effect(NULL), arg(NULL),
          probe(NULL) { }

    /* Deletes what `kind' names, and only that: an item reused for a second
       kind still holds the first one's pointer. */
    void destroy (void);
};

/* Usable capacity is one less than this. Sized for a burst of note-ons and for
   clearAll() retiring an entire channel's polyphony at once. */
#define TH_COMMAND_QUEUE_SIZE 1024
#define TH_RETIRE_QUEUE_SIZE  1024

typedef thRing<thRetired, TH_RETIRE_QUEUE_SIZE> thRetireQueue;

/* Audio thread: `item' onto `queue', or freed here when the queue is full.
   That free tears a synth tree down inside the callback, so it is counted
   (thSynth::retireFrees) for a test to hold at zero. A NULL queue is a
   caller with nobody to hand it to, and is freed uncounted. */
void thRetire (thRetireQueue *queue, thRetired &item);

#endif /* TH_SYNTH_COMMAND_H */
