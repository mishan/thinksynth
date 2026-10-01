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
 */

/* MIDI out: the page's half.
 *
 * The worklet plays a piece's MIDI instruments by handing the page stamped
 * messages (twMidiOut in thinkweb.cpp): the bytes, the output port by its
 * index in the list this sent it, and the AudioContext time, in
 * microseconds, of the frame the note sounds at. Here that time becomes
 * performance.now()'s, through an AudioClock fed getOutputTimestamp(),
 * plus the delay a person sets, and MIDIOutput.send(bytes, time) is told
 * exactly when.
 *
 * A message is handed to send() only a little before it is due (HORIZON),
 * not as soon as it arrives. Once handed over it cannot be taken back --
 * Chromium has no MIDIOutput.clear() -- and a stop has to be able to take
 * back what has not sounded yet.
 *
 * What each device holds is kept here, since only here is it known what
 * went out: per port, MIDI channel and key, which engine channel struck
 * it. A note-on for a key already held ends it first; a note-off for a key
 * another channel has since taken over is not sent; and a flush -- a stop,
 * a rewind, a route changing, the worklet's FLUSH and DETACH records --
 * ends exactly one channel's notes, no earlier than they were scheduled to
 * start, so an off never overtakes its own on.
 */

import { midiAccess } from './midi.js';

/* twMidiOut's record kinds. */
const SEND = 0;

/* How far ahead of its time a message is handed to send(). Enough for a
   timer that fires late; short enough that a stop takes back nearly
   everything not yet heard. */
const HORIZON = 30;

const PUMP_EVERY = 5;

export class MidiSender
{
    /* `clock' is an AudioClock; `onPorts(names)' hears the output port
       list whenever it changes, for the page to hand the worklet. */
    constructor ({ clock, onPorts = () => {}, delay = 0,
                   now = () => performance.now() })
    {
        this.clock = clock;
        this.onPorts = onPorts;
        this.delay = delay;
        this.now = now;
        this.access = null;
        this.outputs = [];
        this.queue = [];
        this.seq = 0;

        /* `port:midichannel:key' -> { channel, at } */
        this.held = new Map();
        this.timer = null;
    }

    /* Asks for MIDI access, lists the outputs and starts sending. Rejects
       with a message fit for the page. */
    async open ()
    {
        this.access = await midiAccess();
        this.access.onstatechange = (e) =>
        {
            if (e.port?.type === 'output')
                this.listPorts();
        };

        this.listPorts();
        this.timer = setInterval(() => this.pump(), PUMP_EVERY);
    }

    /* Everything held ends, nothing more is sent, and the worklet is told
       there are no ports, by the page, through onPorts. */
    close ()
    {
        this.flush(-1);
        clearInterval(this.timer);
        this.timer = null;

        if (this.access !== null)
            this.access.onstatechange = null;

        this.access = null;
        this.outputs = [];
    }

    get names ()
    {
        return this.outputs.map((o) => o.name);
    }

    listPorts ()
    {
        const outputs = [...this.access.outputs.values()]
            .filter((o) => o.state !== 'disconnected');

        /* What every queued message and held key names is an index into
           the old list; they are not carried across. The worklet applies
           every MIDI instrument again on the new list, flushing each. */
        this.flush(-1);
        this.outputs = outputs;
        this.onPorts(this.names);
    }

    /* What the worklet posted (host.js onMidi). */
    take (msgs)
    {
        for (const m of msgs)
        {
            if (m.kind !== SEND)
            {
                this.flush(m.channel);
                continue;
            }

            const at = this.clock.perfAt(m.when / 1e6);

            this.queue.push({
                due: (Number.isNaN(at) ? this.now() : at) + this.delay,
                seq: this.seq++,
                channel: m.channel,
                port: m.port,
                bytes: m.bytes,
            });
        }

        this.queue.sort((a, b) => a.due - b.due || a.seq - b.seq);
        this.pump();
    }

    /* Hands send() what is due within HORIZON. */
    pump ()
    {
        const now = this.now();
        let n = 0;

        while (n < this.queue.length && this.queue[n].due <= now + HORIZON)
            this.handOff(this.queue[n++], now);

        if (n > 0)
            this.queue.splice(0, n);
    }

    handOff (m, now)
    {
        const out = this.outputs[m.port];

        if (out === undefined)
            return;

        const at = Math.max(m.due, now);
        const status = m.bytes[0] & 0xf0;
        const key = `${m.port}:${m.bytes[0] & 0x0f}:${m.bytes[1]}`;

        if (status === 0x90)
        {
            /* One voice per key: a retrigger ends the note it replaces,
               whichever channel struck it, and the key is this one's. */
            if (this.held.has(key))
                this.send(out, [0x80 | (m.bytes[0] & 0x0f), m.bytes[1], 64],
                          at);

            this.held.set(key, { channel: m.channel, at });
        }
        else if (status === 0x80)
        {
            const held = this.held.get(key);

            /* Only the note this channel struck. */
            if (held === undefined || held.channel !== m.channel)
                return;

            this.held.delete(key);
        }

        this.send(out, m.bytes, at);
    }

    send (out, bytes, at)
    {
        try
        {
            out.send(bytes, at);
        }
        catch (e)
        {
            /* A device gone between the list and the send; the state
               change that says so lists the ports again. */
        }
    }

    /* Nothing queued for `channel' (-1: any) goes out, and what it holds
       ends now -- or when it was scheduled to start, if that is later, so
       the off comes after its on. */
    flush (channel)
    {
        const now = this.now();

        this.queue = this.queue.filter(
            (m) => channel >= 0 && m.channel !== channel);

        for (const [key, held] of this.held)
        {
            if (channel >= 0 && held.channel !== channel)
                continue;

            const [port, midiChannel, note] = key.split(':').map(Number);
            const out = this.outputs[port];

            if (out !== undefined)
                this.send(out, [0x80 | midiChannel, note, 64],
                          Math.max(now, held.at + 1));

            this.held.delete(key);
        }
    }
}
