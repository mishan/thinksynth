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
 * midi.js -- a MIDI keyboard, through Web MIDI, onto the page's keys.
 *
 * Note on and note off, handed to the page's own press and release:
 * everything past those -- patch mode or a piece's `input midi', the key
 * channel on the solo page, the seat in a room -- is already decided there,
 * and a MIDI key is one more hand on the same keys.
 *
 * THE DEVICE'S CHANNEL IS IGNORED. Every input is omni. The page's channel
 * is where a note goes, as it is for a computer key: in a room a seat is a
 * channel, and a keyboard left on channel 1 would otherwise play whoever
 * sits there.
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

/* True where the browser has Web MIDI at all: Chromium and Firefox, over
   https or on localhost. Safari has none. */
export function midiAvailable ()
{
    return typeof navigator !== 'undefined' &&
           typeof navigator.requestMIDIAccess === 'function';
}

/* A button that opens MIDI input and closes it again, and a line beside it
 * naming the inputs. Both pages have one; the button is left disabled for
 * the page to enable once there is a synth to play.
 *
 * Returns { forget }, which is openMidi's forget on whatever is open.
 */
export function midiToggle ({ button, status, onNoteOn, onNoteOff,
                             onPedal })
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
            open = await openMidi({ onNoteOn, onNoteOff, onPedal,
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
 * `onNoteOn(note, velocity)' and `onNoteOff(note)' are called once per key,
 * `onPedal(value)' for every sustain pedal message but a let-up while
 * another input's pedal is still down, and `onChange(names)' with the
 * connected inputs' names whenever one comes or goes.
 *
 * Resolves to { names, forget, close }. `forget' drops what every input
 * holds, pedal included, without calling onNoteOff or onPedal, for a page
 * that has just released everything itself. `close' lets go of everything
 * held and stops listening.
 *
 * Rejects with a message fit to put on the page.
 */
export async function openMidi ({ onNoteOn, onNoteOff, onPedal = () => {},
                                  onChange = () => {} })
{
    if (!midiAvailable())
        throw new Error('this browser has no Web MIDI here -- it needs ' +
                        'Chromium or Firefox, over https or on localhost');

    let access;

    try
    {
        access = await navigator.requestMIDIAccess({ sysex: false });
    }
    catch (e)
    {
        if (e.name === 'NotAllowedError' || e.name === 'SecurityError')
            throw new Error('MIDI access was refused');

        throw new Error(`MIDI did not open: ${e.message || e.name}`);
    }

    /* input id -> the notes it holds */
    const held = new Map();

    /* The inputs whose pedal is down. */
    const pedaling = new Set();

    const noteOn = (id, note, velocity) =>
    {
        const down = held.get(id);

        if (down === undefined || down.has(note))
            return;

        down.add(note);
        onNoteOn(note, velocity);
    };

    const noteOff = (id, note) =>
    {
        const down = held.get(id);

        if (down === undefined || !down.delete(note))
            return;

        onNoteOff(note);
    };

    const letGo = (id) =>
    {
        const down = held.get(id);

        if (down === undefined)
            return;

        for (const note of [...down])
            noteOff(id, note);

        if (pedaling.delete(id) && pedaling.size === 0)
            onPedal(0);
    };

    const attach = (input) =>
    {
        if (!held.has(input.id))
            held.set(input.id, new Set());

        /* Assigning the handler is also what opens the port. */
        input.onmidimessage = (e) =>
        {
            const data = e.data;

            if (data.length < 3)
                return;

            const kind = data[0] & 0xf0;
            const data1 = data[1] & 0x7f;
            const data2 = data[2] & 0x7f;

            /* A note on at velocity 0 is a note off, which is how a
               keyboard using running status sends most of them. */
            if (kind === 0x90 && data2 > 0)
                noteOn(input.id, data1, data2);
            else if (kind === 0x80 || kind === 0x90)
                noteOff(input.id, data1);
            else if (kind === 0xb0 && data1 === CC_SUSTAIN)
            {
                if (data2 >= PEDAL_DOWN)
                    pedaling.add(input.id);
                else
                    pedaling.delete(input.id);

                /* Another input's pedal still holds it down. */
                if (data2 < PEDAL_DOWN && pedaling.size > 0)
                    return;

                onPedal(data2);
            }
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
