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

/*
 * midi.js -- a MIDI keyboard, through Web MIDI, onto the page's keys.
 *
 * Note on and note off, handed to the page's own press and release:
 * everything past those -- patch mode or a piece's `input midi', the key
 * channel on the solo page, the seat in a room -- is already decided there,
 * and a MIDI key is one more hand on the same keys.
 *
 * Every input is omni unless routes say otherwise. The page's channel is
 * where a note goes, as it is for a computer key: in a room a seat is a
 * channel, and a keyboard left on channel 1 would otherwise play whoever
 * sits there. A page that hands openMidi a MidiRoutes has a MIDI channel
 * its routes name go to the engine channels they say instead, and keeps
 * each such note's targets for its off; a room hands none.
 *
 * Each input keeps the notes it holds. A second note on for a note already
 * down is dropped, a note off for one not down is dropped, and an input
 * that goes away lets go of everything it held -- the note offs of an
 * unplugged keyboard are never coming.
 *
 * And the sustain pedal, controller 64, handed over as its 0..127 for the
 * page to put on the channel's SusPedal, as thSynth::handleMidiController
 * does natively. The pedal is up only once every input's is: an input
 * letting its pedal up while another's is still down is dropped, and one
 * that goes away with its pedal down puts it up only if it was the last.
 *
 * Every other controller, pitch bend and program change are ignored. A
 * controller mapped to an arg has to reach every peer at the same transport
 * time, which is a stamped knob command and not a write here.
 *
 * Web MIDI hands over one complete message per event, running status
 * already expanded, so data[0] is always a status byte.
 */

/* The sustain pedal's controller number, and the value from which it is
   down (TH_MIDI_CC_SUSTAIN, thMidiChan). */
const CC_SUSTAIN = 64;
const PEDAL_DOWN = 64;

/* Which engine channels a MIDI note goes to, as libthink's thMidiRoutes
 * says: per MIDI channel, the highest layer naming it, machine then
 * piece, decides all of it -- its routes ({ midi, engine, lo, hi }, engine
 * -1 for off) whose key range takes the note -- and a channel no layer
 * names is the base's. The base is omni onto the page's own channel, so
 * resolve() and reach() answer null for it.
 */
export class MidiRoutes
{
    constructor ()
    {
        this.layers = { machine: [], piece: [] };
    }

    set (layer, routes)
    {
        if (layer in this.layers)
            this.layers[layer] = routes.map((r) => ({ lo: 0, hi: 127, ...r }));
    }

    deciding (midi)
    {
        for (const layer of [this.layers.machine, this.layers.piece])
            if (layer.some((r) => r.midi === midi))
                return layer.filter((r) => r.midi === midi);

        return null;
    }

    /* The engine channels, each once, or null for the page's channel. */
    resolve (midi, note)
    {
        const routes = this.deciding(midi);

        return routes === null ? null
            : [...new Set(routes.filter((r) => r.engine >= 0 &&
                                               note >= r.lo && note <= r.hi)
                                .map((r) => r.engine))];
    }

    /* Every engine channel some note on `midi' goes to: the pedal's. */
    reach (midi)
    {
        const routes = this.deciding(midi);

        return routes === null ? null
            : [...new Set(routes.filter((r) => r.engine >= 0 &&
                                               Math.max(r.lo, 0) <=
                                               Math.min(r.hi, 127))
                                .map((r) => r.engine))];
    }
}

/* True where the browser has Web MIDI at all: Chromium and Firefox, over
   https or on localhost. Safari has none. */
export function midiAvailable ()
{
    return typeof navigator !== 'undefined' &&
           typeof navigator.requestMIDIAccess === 'function';
}

/* The browser's MIDIAccess, asked for without sysex: what MIDI in and MIDI
 * out (midiout.js) both start from. Rejects with a message fit to put on
 * the page. */
export async function midiAccess ()
{
    if (!midiAvailable())
        throw new Error('this browser has no Web MIDI here -- it needs ' +
                        'Chromium or Firefox, over https or on localhost');

    try
    {
        return await navigator.requestMIDIAccess({ sysex: false });
    }
    catch (e)
    {
        if (e.name === 'NotAllowedError' || e.name === 'SecurityError')
            throw new Error('MIDI access was refused');

        throw new Error(`MIDI did not open: ${e.message || e.name}`);
    }
}

/* A button that opens MIDI input and closes it again, and a line beside it
 * naming the inputs. Both pages have one; the button is left disabled for
 * the page to enable once there is a synth to play.
 *
 * Returns { forget }, which is openMidi's forget on whatever is open.
 */
export function midiToggle ({ button, status, onNoteOn, onNoteOff,
                             onPedal, routes = null })
{
    const label = button.textContent;
    let open = null;

    const say = (names) =>
    {
        status.textContent = names.length > 0
            ? names.join(', ') : 'no MIDI inputs; plug one in';
    };

    button.addEventListener('click', async () =>
    {
        if (open !== null)
        {
            open.close();
            open = null;
            button.textContent = label;
            status.textContent = '';
            return;
        }

        button.disabled = true;
        status.textContent = 'asking...';

        try
        {
            open = await openMidi({ onNoteOn, onNoteOff, onPedal, routes,
                                    onChange: say });
        }
        catch (e)
        {
            status.textContent = e.message;
            button.disabled = false;
            return;
        }

        button.textContent = `${label}: on`;
        button.disabled = false;
        say(open.names());
    });

    return { forget: () => open?.forget() };
}

/* Asks for MIDI access and listens on every input there is, and on every
 * input plugged in afterwards.
 *
 * `onNoteOn(note, velocity, targets)' and `onNoteOff(note, targets)' are
 * called once per key, `onPedal(value, targets)' for every sustain pedal
 * message but a let-up while another input's pedal is still down, and
 * `onChange(names)' with the connected inputs' names whenever one comes
 * or goes. `targets' is null for the page's channel, or the engine
 * channels `routes' sent the note or the pedal to: a note's off goes
 * where its on went, and a pedal's let-up where it went down as well.
 *
 * Resolves to { names, forget, close }. `forget' drops what every input
 * holds, pedal included, without calling onNoteOff or onPedal, for a page
 * that has just released everything itself. `close' lets go of everything
 * held and stops listening.
 *
 * Rejects with a message fit to put on the page.
 */
export async function openMidi ({ onNoteOn, onNoteOff, onPedal = () => {},
                                  onChange = () => {}, routes = null })
{
    const access = await midiAccess();

    /* input id -> the notes it holds, by `channel note', each to the
       targets its on had: null for the page's channel. */
    const held = new Map();

    /* The inputs whose pedal is down on the page's channel. */
    const pedaling = new Set();

    /* input id -> MIDI channel -> the engine channels a routed pedal went
       down on. */
    const pedaled = new Map();

    /* What an input holds for a note: on its channel, or where it is
       omni, on any. */
    const holding = (down, channel, note) =>
    {
        if (down.has(`${channel} ${note}`))
            return `${channel} ${note}`;

        for (const [key, targets] of down)
            if (targets === null && key.endsWith(` ${note}`))
                return key;

        return null;
    };

    /* A second on for a key down goes where the first went, which is
       nowhere new: it is dropped. */
    const noteOn = (id, channel, note, velocity) =>
    {
        const down = held.get(id);
        const targets = routes?.resolve(channel, note) ?? null;

        if (down === undefined ||
            down.has(`${channel} ${note}`) ||
            (targets === null && holding(down, channel, note) !== null))
            return;

        down.set(`${channel} ${note}`, targets);
        onNoteOn(note, velocity, targets);
    };

    const noteOff = (id, channel, note) =>
    {
        const down = held.get(id);
        const key = down === undefined ? null : holding(down, channel, note);

        if (key === null)
            return;

        const targets = down.get(key);

        down.delete(key);
        onNoteOff(note, targets);
    };

    /* Down on where the routes reach now, or the page's channel; up where
       it went down, and where the routes reach now as well. */
    const pedal = (id, channel, value) =>
    {
        const reach = routes?.reach(channel) ?? null;

        if (!pedaled.has(id))
            pedaled.set(id, new Map());

        const mine = pedaled.get(id);
        const went = mine.get(channel);

        if (value >= PEDAL_DOWN && reach !== null)
        {
            mine.set(channel, [...new Set([...(went ?? []), ...reach])]);
            onPedal(value, reach);
            return;
        }

        if (value < PEDAL_DOWN && (went !== undefined || reach !== null))
        {
            mine.delete(channel);
            onPedal(value, [...new Set([...(went ?? []), ...(reach ?? [])])]);

            /* And the page's channel too, if this input held it down. */
            if (!pedaling.has(id))
                return;
        }

        if (value >= PEDAL_DOWN)
            pedaling.add(id);
        else
            pedaling.delete(id);

        /* Another input's pedal still holds it down. */
        if (value < PEDAL_DOWN && pedaling.size > 0)
            return;

        onPedal(value, null);
    };

    const letGo = (id) =>
    {
        const down = held.get(id);

        if (down === undefined)
            return;

        for (const [key, targets] of [...down])
        {
            down.delete(key);
            onNoteOff(Number(key.split(' ')[1]), targets);
        }

        if (pedaling.delete(id) && pedaling.size === 0)
            onPedal(0, null);

        for (const went of pedaled.get(id)?.values() ?? [])
            onPedal(0, went);

        pedaled.delete(id);
    };

    const attach = (input) =>
    {
        if (!held.has(input.id))
            held.set(input.id, new Map());

        /* Assigning the handler is also what opens the port. */
        input.onmidimessage = (e) =>
        {
            const data = e.data;

            if (data.length < 3)
                return;

            const kind = data[0] & 0xf0;
            const channel = data[0] & 0x0f;
            const data1 = data[1] & 0x7f;
            const data2 = data[2] & 0x7f;

            /* A note on at velocity 0 is a note off, which is how a
               keyboard using running status sends most of them. */
            if (kind === 0x90 && data2 > 0)
                noteOn(input.id, channel, data1, data2);
            else if (kind === 0x80 || kind === 0x90)
                noteOff(input.id, channel, data1);
            else if (kind === 0xb0 && data1 === CC_SUSTAIN)
                pedal(input.id, channel, data2);
        };
    };

    const detach = (input) =>
    {
        letGo(input.id);
        held.delete(input.id);
        input.onmidimessage = null;
    };

    const names = () => [...access.inputs.values()]
        .filter((input) => input.state === 'connected')
        .map((input) => input.name || 'MIDI input');

    for (const input of access.inputs.values())
        attach(input);

    /* Fires for an output too, and for an input opening or closing as well
       as for one being plugged in or pulled out; `state' is the one that
       says whether the device is there. */
    access.onstatechange = (e) =>
    {
        const port = e.port;

        if (port.type !== 'input')
            return;

        if (port.state === 'connected')
            attach(port);
        else
            detach(port);

        onChange(names());
    };

    let open = true;

    return {
        names,

        forget ()
        {
            for (const down of held.values())
                down.clear();

            pedaling.clear();
            pedaled.clear();
        },

        close ()
        {
            if (!open)
                return;

            open = false;
            access.onstatechange = null;

            for (const input of access.inputs.values())
            {
                detach(input);

                if (typeof input.close === 'function')
                    input.close().catch(() => {});
            }
        },
    };
}
