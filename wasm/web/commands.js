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
 * commands.js -- what crosses the network, and how every peer applies
 * it.
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
 *   transport  { at, op: 'start', origin, piece, seed, seek } -- a seek
 *   knob       { at, knob, value }            knob: its name
 *   knobwrite  { at, knob, value }
 *   edit       { at, text, files }            the piece's new text
 *   pick       { at, name, dsp }              an instrument's new graph
 *   input      { at, chain, stage, chainName, stageName, kind, x, y, w, h,
 *                button }
 *   param      { at, chain, stage, chainName, stageName, row, text }
 *   mute       { at, chain, on }
 *   solo       { at, chain, on }
 *   section    { at, section, chain, level }
 *   note       { at, seat, note, velocity, mode, heard }
 *   noteoff    { at, seat, note, mode }        mode: direct | quantised |
 *                                              ahead
 *
 * Nothing here reads a clock or touches a socket: the page and the
 * harness hand in what to stamp with and what to send through.
 */

/* Starting values, in seconds, both shown on the page and both
   adjustable there. */
export const KNOB_LEAD = 0.150;
export const TRANSPORT_LEAD = 0.500;

/* A command's own name: its maker and its number, which no other command
   in the room has -- unlike its stamp, which is -1 for every command made
   while the transport is stopped. */
export const commandTag = (cmd) => `${cmd.from}:${cmd.seq}`;

/* The commands that name a chain, a stage, a section or a knob by index:
   made against the piece as the maker saw it, and dropped where an edit
   has applied since (thinkweb.cpp, Scheduled's `rev'). */
const BY_INDEX = new Set(['knobwrite', 'input', 'param', 'mute', 'solo',
                          'section']);

/* Makes commands for one peer: numbered, stamped, and from it. */
export class Maker
{
    /* `peer' is this peer's id. `transportNow' is called for the stamp,
       and returns transport seconds -- or a negative number while the
       transport is stopped, when a knob is for "now" on every peer
       rather than for a time that is not passing. `edits', if given,
       returns how many edits the piece this peer is showing has had. */
    constructor (peer, transportNow,
                 { knobLead = KNOB_LEAD, transportLead = TRANSPORT_LEAD,
                   edits = null } = {})
    {
        this.edits = edits;
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

        const cmd = { type, at, from: this.peer, seq: this.seq++,
                      ...fields };

        if (this.edits !== null && BY_INDEX.has(type))
            cmd.rev = this.edits();

        return cmd;
    }

    /* Play. `origin' is a relay-clock time; the caller has already put it
       `transportLead' ahead. */
    start (origin, hash, seed, seek = 0)
    {
        return this.make('transport', { op: 'start', origin,
                                        piece: { hash }, seed, seek },
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

    /* By name: an edit can add a knob or take one away, and a number made
       before one names a different knob after it. */
    knob (knob, value)
    {
        return this.make('knob', { knob, value }, this.knobLead);
    }

    /* A knob's value written into the piece, at the end of a drag: with
       the knob's lead, so it lands after the drag's last move. */
    knobWrite (knob, value)
    {
        return this.make('knobwrite', { knob, value }, this.knobLead);
    }

    /* The piece's new text, and the other files the edit changed as
     * { name: text }, applied at `at' -- the next bar, which the caller
     * works out (nextBar below): stamped with a time rather than a lead,
     * because what an edit keeps and what it rebuilds happens at one
     * point in the piece on every peer, and a bar line is where a person
     * hears a change as meant. -1 while the transport is stopped.
     *
     * The text rides in the command rather than being read off the
     * document by each peer, because the document goes on moving -- the
     * next keystroke is already on its way -- and what every peer applies
     * has to be the one revision the sender pressed Apply on. */
    edit (at, text, files = {})
    {
        return { ...this.make('edit', { text, files }, 0), at };
    }

    /* Instrument `name' onto graph `dsp', at `at' -- the next bar, or -1
     * while stopped, as for an edit. Not an edit's text: two picks on one
     * bar would each carry a text without the other's, and the later would
     * undo the earlier. Each peer puts it into the piece as the piece is
     * when it applies (thinkweb.cpp, applyPick). */
    pick (at, name, dsp)
    {
        return { ...this.make('pick', { name, dsp }, 0), at };
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
       platform. */
    input (chain, stage, kind, x, y, w, h, button = 1, names = {})
    {
        return this.make('input', { chain, stage, ...names, kind, x, y, w, h,
                                    button },
                         this.knobLead);
    }

    /* A stage's parameter, set.
     *
       Stamped with the knob's lead and for the same reason: it is heard. A
       `period' that changes a window earlier on one peer than another moves
       that stage's next firing by a window, and from there the two are
       composing different pieces. The typist hears their own edit a lead
       late, as they hear their own knob.
     *
       `row' is the param's name -- not an index, because a peer a revision
       behind would then set its neighbour -- and `text' is the part of the
       line the person touched: a number, a unit, a binding, a note set.
       What it completes to is worked out on arrival, by every peer, against
       the file each of them holds (src/StagePanel.h). Completing it here
       would be the sender telling the others what their own file says. */
    param (chain, stage, row, text, names = {})
    {
        return this.make('param', { chain, stage, ...names, row, text },
                         this.knobLead);
    }

    /* A chain's mute or solo, set or cleared. Stamped with the knob's
       lead: it is heard, and a peer that muted a window early would
       drop a note the others play. */
    mute (chain, on)
    {
        return this.make('mute', { chain, on }, this.knobLead);
    }

    solo (chain, on)
    {
        return this.make('solo', { chain, on }, this.knobLead);
    }

    /* A chain's level in one section of the arrangement. Stamped with the
       knob's lead: it decides what is heard from there on. */
    section (section, chain, level)
    {
        return this.make('section', { section, chain, level },
                         this.knobLead);
    }

    /* A key, in the seat's mode (docs/JAM.md, the three ways to play).
     *
     * Direct: stamped with now and no lead, and played on arrival,
     * wherever that falls -- the least latency, and the one a note into a
     * piece cannot use, since no two peers hand it to the piece at the same
     * point.
     *
     * Quantised and ahead: stamped with `at', which the caller works out
     * (keyAt below), and applied there on every peer, the sender included
     * -- so a key into a piece composes the same thing everywhere, and is
     * logged for a late joiner like any other stamped command.
     *
     * `heard' says the sender played it on its own page as it pressed it. */
    note (seat, note, velocity, mode = 'direct', at = null, heard = false)
    {
        const cmd = this.make('note', { seat, note, velocity, mode, heard },
                              0);

        return at === null ? cmd : { ...cmd, at };
    }

    noteoff (seat, note, mode = 'direct', at = null)
    {
        const cmd = this.make('noteoff', { seat, note, mode }, 0);

        return at === null ? cmd : { ...cmd, at };
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

/* The first bar line at least `lead' seconds past `now', in transport
 * seconds, from a worklet's tape message: the transport time it reported,
 * the beat that falls on, the tempo and the meter. -1 when the transport
 * is stopped: an edit then applies on arrival, on every peer, since no
 * time is passing.
 *
 * A tempo change already stamped between now and then is not seen, so
 * the time may not be on the bar; it is still one time on every peer,
 * which is what the tape depends on.
 */
export function nextBar (now, report, lead)
{
    const { beat, tempo, meter } = report;

    if (now < 0 || !(tempo > 0) || !(meter > 0))
        return -1;

    /* The report's beat is at the report's own `now', a little behind. */
    const from = beat + (now + lead - report.now) * tempo / 60;
    const bar = Math.ceil(from / meter - 1e-9) * meter;

    return now + lead + (bar - from) * 60 / tempo;
}

/* The order among commands stamped for one time (thinkweb.cpp, Scheduled's
   `tie'): made from the sender's id and counter, so it is the same number on
   every peer and different for every command. Two Applies land on one bar
   line, and the one applied last is the text that plays; two quantised
   seats land keys on one grid line, and a piece composes from the order
   they reach it in. */
export function tieOf (cmd)
{
    let h = 0x811c9dc5;

    for (const ch of String(cmd.from))
    {
        h ^= ch.codePointAt(0);
        h = Math.imul(h, 0x01000193) >>> 0;
    }

    return 1 + (h & 0xfffff) * 0x100000 + (cmd.seq & 0xfffff);
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
 * knob, noteOn, noteOff, midiOn and midiOff, and batch for catchUp below.
 * `frameOfOrigin' turns a relay-clock origin into a frame of this peer's
 * output (clock.js's frameOfRelayMs, bound). `listens' is the set of
 * channels the piece takes `input midi' on, from the load: a key on one of
 * those goes into the piece, on any other straight onto the channel.
 *
 * A start is the one command with something to do before the worklet: the
 * piece has to be loaded, from the document at the named hash, with the
 * named seed. That is the caller's -- `load' is called with the command and
 * resolves once the piece is in -- because only the caller has the
 * document. Everything else goes straight through. */
export async function apply (cmd, { synth, frameOfOrigin, listens, load,
                                    self = null })
{
    if (cmd.type === 'transport' && cmd.op === 'start')
    {
        if (load !== undefined)
            await load(cmd);

        /* From the top, or from `seek': a room's seek is a start from a
           time, so every peer plays up to it at the same frame, and a peer
           joining later hears the start the relay kept, `seek' and all. */
        synth.begin(frameOfOrigin(cmd.origin), cmd.seek ?? 0);
        return;
    }

    applyNow(cmd, { synth, listens, self });
}

/* The commands a late joiner steps through, of those the room logged since
 * its start: the stamped ones. A key in direct mode is played where it
 * arrives rather than at its stamp, so no two peers heard it at the same
 * point in the piece and there is no one point to replay it at.
 */
export function replayable (cmd)
{
    if (cmd.type === 'note' || cmd.type === 'noteoff')
        return (cmd.mode ?? 'direct') !== 'direct';

    return !(cmd.type === 'transport' && cmd.op === 'start');
}

/* The grid a quantised key lands on, in beats: a sixteenth. */
export const GRID = 0.25;

/* Where a key pressed or let go of now lands, by mode. -1 for direct, and
 * for any mode while the transport is stopped -- there is no grid and no
 * bar then, and the key plays on arrival.
 *
 * Quantised: the first grid line at least `lead' seconds on, so that it
 * reaches every peer before its time; `after' is a floor, which is how a
 * release is kept a grid line behind its own press however quickly it came.
 * Ahead: exactly one bar on, which is where every other seat hears it --
 * NINJAM's trick, coherent against the grid and no use for call and
 * response.
 *
 * `report' is a worklet tape message: the transport time it reported, the
 * beat that falls on, the tempo and the meter (nextBar's).
 */
export function keyAt (mode, now, report, lead, after = -1)
{
    const { beat, tempo, meter } = report ?? {};

    if (mode === 'direct' || now < 0 || !(tempo > 0) || !(meter > 0))
        return -1;

    if (mode === 'ahead')
        return Math.max(after, now + meter * 60 / tempo);

    const floor = Math.max(now + lead, after);
    const from = beat + (floor - report.now) * tempo / 60;
    const line = Math.ceil(from / GRID - 1e-9) * GRID;

    return floor + (line - from) * 60 / tempo;
}

/* Joining a room that is already playing: the start's piece loaded, then
 * transport zero, or the start's `seek', put at the start's origin -- a
 * frame this peer's output went past before it was here -- and the room's
 * commands since, all in one batch, so the worklet steps through the run
 * from there with each applied at its stamp and arrives at the present
 * composing what the room is (thinkweb.cpp, catchUp). The same `load' a start is applied through;
 * `log' is what the relay kept, in any order.
 */
export async function catchUp (start, log, { synth, frameOfOrigin, listens,
                                             load })
{
    await load(start);

    const later = log.filter(replayable).sort((a, b) => a.at - b.at);

    synth.batch(() =>
    {
        synth.begin(frameOfOrigin(start.origin), start.seek ?? 0, true);

        for (const cmd of later)
            applyNow(cmd, { synth, listens });
    });
}

/* Everything but a start, which is the one with something to wait for.
   `self' is this peer's id, which a play-ahead key of its own is told
   apart by. */
function applyNow (cmd, { synth, listens, self = null })
{
    /* A stamped key: at its time, on every peer. Whether it goes into the
       piece or onto the channel is the worklet's to say when it applies,
       from the piece as it is then -- an edit stamped before the key can
       change it. What this side knows is whether the player has heard it:
       a key of this peer's own that says it was played the moment it was
       pressed (jam.js), and the bar is for everybody else's ears. One into
       the piece is applied here like anyone's, or this peer's piece would
       compose from it a bar before the others' did. */
    if ((cmd.type === 'note' || cmd.type === 'noteoff') &&
        (cmd.mode ?? 'direct') !== 'direct')
    {
        synth.noteAt(cmd.at, cmd.seat, cmd.note,
                     cmd.type === 'note' ? cmd.velocity : 0,
                     cmd.type === 'note',
                     cmd.heard === true && cmd.from === self,
                     tieOf(cmd));
        return;
    }

    switch (cmd.type)
    {
        case 'transport':
            switch (cmd.op)
            {
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

        /* Named by their maker, so the edit each writes comes back as that
           peer's own (commandTag). */
        case 'knobwrite':
            synth.knobWrite({ ...cmd, tag: commandTag(cmd) });
            break;

        case 'input':
            synth.input({ ...cmd, tag: commandTag(cmd) });
            break;

        case 'param':
            synth.param(cmd);
            break;

        case 'mute':
            synth.mute(cmd);
            break;

        case 'solo':
            synth.solo(cmd);
            break;

        case 'section':
            synth.section(cmd);
            break;

        case 'edit':
            synth.edit(cmd.at, cmd.text, cmd.files, tieOf(cmd));
            break;

        case 'pick':
            synth.pick(cmd.at, cmd.name, cmd.dsp, tieOf(cmd));
            break;

        /* Direct mode: played in the next window, whenever it arrived.
           The stamp is on the wire for the record. */
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
