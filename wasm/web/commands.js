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
 * commands.js -- what crosses the network, and how every peer applies it
 * (JAM_M3.md, sections 5.3 and 5.4).
 *
 * A command is a plain object: `type', `at' in transport seconds, `from'
 * the peer that made it, `seq' that peer's counter, and the fields of its
 * kind. The sender applies its own commands through the same apply() the
 * receivers use, so its own knob is heard `knobLead' late -- which is what
 * keeps its tape equal to everyone else's. The local page has no
 * privileged path to the scheduler; it is the nearest peer.
 *
 * Every command is sent ahead of its time. A knob is stamped `now +
 * knobLead', a transport change `now + transportLead', so that it reaches
 * every peer before the transport gets there. One that still arrives late
 * is applied at once by the worklet and counted there (thinkweb.cpp).
 *
 *   transport  { at, op: 'start', origin, piece: { hash }, seed }
 *   transport  { at, op: 'stop' }
 *   transport  { at, op: 'tempo', bpm }
 *   knob       { at, knob, value }
 *   input      { at, chain, stage, kind, x, y, w, h, button }
 *   note       { at, seat, note, velocity }
 *   noteoff    { at, seat, note }
 *
 * Nothing here reads a clock or touches a socket: the page and the
 * harness hand in what to stamp with and what to send through.
 */

/* Starting values, in seconds, both shown on the page and both
   adjustable there (JAM_M3.md, section 1). */
export const KNOB_LEAD = 0.150;
export const TRANSPORT_LEAD = 0.500;

/* Makes commands for one peer: numbered, stamped, and from it. */
export class Maker
{
    /* `peer' is this peer's id. `transportNow' is called for the stamp,
       and returns transport seconds -- or a negative number while the
       transport is stopped, when a knob is for "now" on every peer
       rather than for a time that is not passing. */
    constructor (peer, transportNow,
                 { knobLead = KNOB_LEAD, transportLead = TRANSPORT_LEAD } = {})
    {
        this.peer = peer;
        this.transportNow = transportNow;
        this.knobLead = knobLead;
        this.transportLead = transportLead;
        this.seq = 0;
    }

    make (type, fields, lead)
    {
        const now = this.transportNow();
        const at = now < 0 ? -1 : now + lead;

        return { type, at, from: this.peer, seq: this.seq++, ...fields };
    }

    /* Play. `origin' is a relay-clock time; the caller has already put it
       `transportLead' ahead. */
    start (origin, hash, seed)
    {
        return this.make('transport', { op: 'start', origin,
                                        piece: { hash }, seed },
                         this.transportLead);
    }

    stop ()
    {
        return this.make('transport', { op: 'stop' }, this.transportLead);
    }

    tempo (bpm)
    {
        return this.make('transport', { op: 'tempo', bpm },
                         this.transportLead);
    }

    knob (knob, value)
    {
        return this.make('knob', { knob, value }, this.knobLead);
    }

    /* A gesture on a stage's picture: which stage, what kind of gesture,
       and where in the picture, with the size it was drawn at.
     *
       Stamped with the knob's lead and for the same reason: it reaches
       the composer's state, and every peer has to reach it at the same
       point in the piece or their boards part. The clicker sees their own
       cell fill a lead late, as they hear their own knob late. A Life
       board's period is half a second and up, so there is room.
     *
       The stage is named by chain and stage index, which is the canvas's
       own key and is the same on every peer holding the same revision of
       the document. The coordinates are draw's, not the page's: the
       conversion is done by the code that drew the rectangle, on every
       platform (JAM_M6.md, section 5). */
    input (chain, stage, kind, x, y, w, h, button = 1)
    {
        return this.make('input', { chain, stage, kind, x, y, w, h, button },
                         this.knobLead);
    }

    /* A key. Stamped with now and no lead: direct mode plays it on
       arrival, wherever that falls (JAM_M3.md, section 5.4). */
    note (seat, note, velocity)
    {
        return this.make('note', { seat, note, velocity }, 0);
    }

    noteoff (seat, note)
    {
        return this.make('noteoff', { seat, note }, 0);
    }
}

/* What a receiver keeps per sender: the last `seq' seen, so a duplicate
   is dropped and a gap is counted. The mesh is unordered and drops
   things; the count says how much. */
export class Dedupe
{
    constructor ()
    {
        this.seen = new Map();      /* from -> { last, gaps, duplicates } */
    }

    /* True if the command is new and should be applied. */
    accept (cmd)
    {
        let s = this.seen.get(cmd.from);

        if (s === undefined)
        {
            s = { last: -1, gaps: 0, duplicates: 0, have: new Set() };
            this.seen.set(cmd.from, s);
        }

        if (s.have.has(cmd.seq))
        {
            s.duplicates++;
            return false;
        }

        s.have.add(cmd.seq);

        /* Forgotten once well behind, so the set does not grow for ever. */
        if (s.have.size > 256)
            for (const k of s.have)
            {
                if (s.have.size <= 128)
                    break;

                s.have.delete(k);
            }

        if (cmd.seq > s.last + 1)
            s.gaps += cmd.seq - s.last - 1;
        else if (cmd.seq < s.last)
            /* Out of order rather than dropped: this one was counted as a
               gap when the seq past it arrived first, and here it is. The
               channel is unordered, so this is the common case, and a gap
               that is never reconciled reads as a drop that never
               happened. */
            s.gaps = Math.max(0, s.gaps - 1);

        s.last = Math.max(s.last, cmd.seq);

        return true;
    }

    get gaps ()
    {
        let n = 0;

        for (const s of this.seen.values())
            n += s.gaps;

        return n;
    }
}

/* Has this command's time already gone by, by the page's own reckoning?
   The worklet counts a late command when it applies it; this is how the
   page can say which one it was. `at' below zero is "now", never late. */
export function isLate (cmd, transportNow)
{
    return cmd.at >= 0 && cmd.at < transportNow;
}

/* One command into this peer's worklet, the same way whoever sent it.
 *
 * `synth' is host.js's object, or anything with its begin, transportAt,
 * knob, noteOn, noteOff, midiOn and midiOff. `frameOfOrigin' turns a
 * relay-clock origin into a frame of this peer's output (clock.js's
 * frameOfRelayMs, bound). `listens' is the set of channels the piece takes
 * `input midi' on, from the load: a key on one of those goes into the
 * piece, on any other straight onto the channel.
 *
 * A start is the one command with something to do before the worklet: the
 * piece has to be loaded, from the document at the named hash, with the
 * named seed. That is the caller's -- `load' is called with the command and
 * resolves once the piece is in -- because only the caller has the
 * document. Everything else goes straight through. */
export async function apply (cmd, { synth, frameOfOrigin, listens, load })
{
    switch (cmd.type)
    {
        case 'transport':
            switch (cmd.op)
            {
                case 'start':
                    if (load !== undefined)
                        await load(cmd);

                    synth.begin(frameOfOrigin(cmd.origin));
                    break;

                case 'stop':
                    synth.transportAt('stop', cmd.at);
                    break;

                case 'tempo':
                    synth.transportAt('tempo', cmd.at, cmd.bpm);
                    break;
            }
            break;

        case 'knob':
            synth.knob(cmd.knob, cmd.value, cmd.at);
            break;

        case 'input':
            synth.input(cmd);
            break;

        /* Direct mode: played in the next window, whenever it arrived.
           The stamp is on the wire for the record and for M4's quantised
           mode, which is where it starts to mean something. */
        case 'note':
            if (listens.has(cmd.seat))
                synth.midiOn(cmd.note, cmd.velocity, -1, cmd.seat);
            else
                synth.noteOn(cmd.note, cmd.velocity, -1, cmd.seat);
            break;

        case 'noteoff':
            if (listens.has(cmd.seat))
                synth.midiOff(cmd.note, -1, cmd.seat);
            else
                synth.noteOff(cmd.note, -1, cmd.seat);
            break;
    }
}
