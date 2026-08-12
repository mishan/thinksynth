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

#ifndef TH_SYNTH_COMMAND_H
#define TH_SYNTH_COMMAND_H 1

class thMidiNote;
class thMidiChan;
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
        SET_CHANNEL,    /* install `channel' (may be NULL) on `chan'       */
        SET_CHAN_ARG,   /* install `arg' on `chan', replacing by name      */
        SET_PROBE       /* install `probe' (may be NULL) in `probeSlot'    */
    };

    Type type;
    int chan;
    int noteId;
    int probeSlot;

    thMidiNote *note;
    thMidiChan *channel;
    thArg *arg;
    thProbe *probe;

    thSynthCommand (void)
        : type(NOTE_OFF), chan(0), noteId(0), probeSlot(-1),
          note(NULL), channel(NULL), arg(NULL), probe(NULL) { }
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
    enum Kind { NOTE, CHANNEL, ARG, PROBE };

    Kind kind;

    thMidiNote *note;
    thMidiChan *channel;
    thArg *arg;
    thProbe *probe;

    thRetired (void)
        : kind(NOTE), note(NULL), channel(NULL), arg(NULL), probe(NULL) { }
};

/* Usable capacity is one less than this. Sized for a burst of note-ons and for
   clearAll() retiring an entire channel's polyphony at once. */
#define TH_COMMAND_QUEUE_SIZE 1024
#define TH_RETIRE_QUEUE_SIZE  1024

#endif /* TH_SYNTH_COMMAND_H */
