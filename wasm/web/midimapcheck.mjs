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
 */

/*
 * midimapcheck.mjs -- MIDI knobs learned onto a patch's controls, from Node.
 *
 *   node wasm/web/midimapcheck.mjs
 *
 * No module, no page: midimap.js's mappings and learn, what it reads back
 * out of storage (random text included), and which messages midi.js hands
 * a page's onControl, through a fake Web MIDI input. What a knob does to a
 * control is the engine's (thSynth::setChanArgFraction, panelcheck);
 * midimaptest.mjs is the page.
 *
 * Exit status is the number of failures.
 */

import { createMidiMap, readMappings } from './midimap.js';
import { openMidi } from './midi.js';

let failures = 0;

const check = (ok, what, detail = '') =>
{
    console.log(`${ok ? 'ok   ' : 'FAIL '} ${what}${ok || !detail ? '' : `: ${detail}`}`);
    failures += ok ? 0 : 1;
};

const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

/* What storage may say, and what is kept of it. */
for (const [what, text, want] of [
    ['a list as written is read back', '[{"channel":0,"controller":74,"arg":"cutoff"}]',
     [{ channel: 0, controller: 74, arg: 'cutoff' }]],
    ['nothing stored is no mappings', '', []],
    ['text that is not JSON is none', '[{', []],
    ['nor is JSON that is not a list', '{"channel":0}', []],
    ['an entry out of range is dropped and the rest kept',
     '[{"channel":16,"controller":1,"arg":"a"},{"channel":0,"controller":120,"arg":"b"},' +
     '{"channel":0,"controller":1.5,"arg":"c"},{"channel":0,"controller":2,"arg":""},' +
     '{"channel":0,"controller":3,"arg":"d"}]',
     [{ channel: 0, controller: 3, arg: 'd' }]],
    ['the pedal is never mapped', '[{"channel":0,"controller":64,"arg":"a"}]', []],
    ['a control has one knob: the first listed', '[{"channel":0,"controller":1,"arg":"a"},' +
     '{"channel":0,"controller":2,"arg":"a"}]', [{ channel: 0, controller: 1, arg: 'a' }]],
    ['and nothing else is kept of an entry',
     '[{"channel":0,"controller":1,"arg":"a","x":1}]', [{ channel: 0, controller: 1, arg: 'a' }]],
])
    check(same(readMappings(text), want), what, JSON.stringify(readMappings(text)));

/* Random text: anything at all comes back as a list of good entries. */
{
    let seed = 1;
    const random = () => (seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648;
    const pick = (list) => list[Math.floor(random() * list.length)];
    const value = (depth) => pick([
        () => Math.floor(random() * 200) - 20, () => random() * 20, () => null,
        () => pick(['', 'cutoff', 'a b', '\u0000']), () => random() < 0.5,
        () => depth > 2 ? 0 : Array.from({ length: Math.floor(random() * 4) }, () => value(depth + 1)),
        () => depth > 2 ? 0 : Object.fromEntries(['channel', 'controller', 'arg', 'x']
            .filter(() => random() < 0.7).map((k) => [k, value(depth + 1)])),
    ])();
    let bad = '';

    for (let i = 0; i < 5000 && bad === ''; i++)
    {
        let text = JSON.stringify(value(0));

        if (random() < 0.2)
            text = text.slice(0, Math.floor(random() * text.length));

        try
        {
            const got = readMappings(text);
            const ok = Array.isArray(got) &&
                got.every((m) => Number.isInteger(m.channel) && m.channel >= 0 && m.channel < 16 &&
                                 Number.isInteger(m.controller) && m.controller >= 0 &&
                                 m.controller < 120 && m.controller !== 64 &&
                                 typeof m.arg === 'string' && m.arg !== '') &&
                new Set(got.map((m) => m.arg)).size === got.length;

            bad = ok ? '' : `${text} -> ${JSON.stringify(got)}`;
        }
        catch (e)
        {
            bad = `${text} threw ${e.message}`;
        }
    }
    check(bad === '', 'random stored text reads as good entries or none', bad);
}

/* Learn. A knob is turned as a run of values from `from', `by' a step. */
const turn = (map, channel, controller, from, steps, by = 1) =>
{
    let moves = [];

    for (let i = 0; i <= steps; i++)
        moves = map.control(channel, controller, from + i * by);
    return moves;
};

{
    const map = createMidiMap();

    map.learn(true);
    map.moved('cutoff');
    turn(map, 0, 74, 10, 5);
    check(same(map.mappings(), [{ channel: 0, controller: 74, arg: 'cutoff' }]) && !map.learning(),
          'a control moved, then a knob turned, maps the knob and ends learn',
          JSON.stringify(map.mappings()));
    check(same(map.control(0, 74, 90), ['cutoff']), 'and the knob moves the control');
    check(same(map.control(1, 74, 90), []) && same(map.control(0, 75, 90), []),
          'and no other channel or controller does');
}

{
    const map = createMidiMap();

    map.learn(true);
    turn(map, 2, 1, 60, 5);
    map.moved('wave');
    check(same(map.mappings(), [{ channel: 2, controller: 1, arg: 'wave' }]),
          'a knob turned, then a control moved, maps them too', JSON.stringify(map.mappings()));
}

{
    const map = createMidiMap();

    map.learn(true);
    map.moved('cutoff');
    turn(map, 0, 1, 64, 3);
    turn(map, 0, 1, 64, 3, -1);
    check(map.learning() && map.mappings().length === 0,
          'a knob moving less than a few steps is not the one learned');
    turn(map, 0, 7, 0, 4);
    check(same(map.mappings(), [{ channel: 0, controller: 7, arg: 'cutoff' }]),
          'and one that moves far enough is', JSON.stringify(map.mappings()));
}

{
    const map = createMidiMap({ text: '[{"channel":0,"controller":74,"arg":"cutoff"}]' });

    map.learn(true);
    map.moved('cutoff');
    turn(map, 0, 71, 0, 4);
    check(same(map.mappings(), [{ channel: 0, controller: 71, arg: 'cutoff' }]),
          'learning another knob for a control replaces its knob', JSON.stringify(map.mappings()));

    map.learn(true);
    map.moved('level');
    turn(map, 0, 71, 0, 4);
    check(same(map.control(0, 71, 50), ['cutoff', 'level']),
          'and one knob may move two controls', JSON.stringify(map.control(0, 71, 50)));

    map.learn(true);
    check(same(turn(map, 0, 71, 0, 4), ['cutoff', 'level']),
          'a knob still plays while learn waits for a control');

    map.learn(false);
    map.moved('pan');
    check(map.mappings().length === 2, 'a control moved with learn off maps nothing');

    map.learn(true);
    map.moved('pan');
    map.learn(true);
    turn(map, 0, 20, 0, 4);
    check(map.learning() && map.mappings().length === 2,
          'learn started again forgets the control it had', JSON.stringify(map.mappings()));

    map.learn(false);
    map.remove(0);
    check(same(map.mappings(), [{ channel: 0, controller: 71, arg: 'level' }]),
          'a mapping removed is gone, and the rest kept');
    check(same(createMidiMap({ text: map.text() }).mappings(), map.mappings()),
          'and what is kept reads back as it was');
}

{
    /* What the page keeps is the text onChange finds: each change's own. */
    let kept = '';
    const map = createMidiMap({ onChange: () => { kept = map.text(); } });
    const keptIs = () => same(readMappings(kept), map.mappings());

    map.learn(true);
    map.moved('cutoff');
    turn(map, 0, 74, 0, 4);
    const learned = keptIs() && map.mappings().length === 1;

    map.remove(0);
    check(learned && keptIs() && kept === '[]', 'what is kept follows each mapping learned and removed');
}

/* midi.js: which control changes a page's onControl hears. */
{
    const input = { id: 'k1', name: 'Keys', type: 'input', state: 'connected', onmidimessage: null };
    const access = { inputs: new Map([['k1', input]]), outputs: new Map(), onstatechange: null };

    Object.defineProperty(globalThis, 'navigator', {
        value: { requestMIDIAccess: async () => access }, configurable: true });

    const heard = [], pedals = [], notes = [];
    const open = await openMidi({
        onNoteOn: (note) => notes.push(`on ${note}`),
        onNoteOff: (note) => notes.push(`off ${note}`),
        onPedal: (value) => pedals.push(value),
        onControl: (channel, controller, value) => heard.push(`${channel} ${controller} ${value}`),
    });
    const send = (...bytes) => input.onmidimessage({ data: Uint8Array.from(bytes) });

    send(0xb0, 74, 100);
    send(0xb3, 1, 0);
    send(0xb0, 119, 5);
    send(0xb0, 64, 127);
    send(0xb0, 64, 0);
    send(0xb0, 120, 0);
    send(0xb0, 123, 0);
    send(0xb0, 127, 0);
    send(0x90, 60, 90);
    send(0x80, 60, 0);
    send(0xe0, 0, 64);
    check(same(heard, ['0 74 100', '3 1 0', '0 119 5']),
          'onControl hears controllers 0..119 but the pedal, on their channel', JSON.stringify(heard));
    check(same(pedals, [127, 0]) && same(notes, ['on 60', 'off 60']),
          'and the pedal and the notes go where they went', JSON.stringify([pedals, notes]));

    /* Random messages, as a broken device or a sysex torn apart sends:
       nothing throws, and only a knob reaches onControl. */
    let seed = 7, bad = '';
    const byte = () => ((seed = (seed * 1103515245 + 12345) % 2147483648) >> 16) & 255;

    heard.length = 0;
    for (let i = 0; i < 20000 && bad === ''; i++)
    {
        const bytes = Array.from({ length: byte() % 5 }, byte);

        try
        {
            send(...bytes);
        }
        catch (e)
        {
            bad = `[${bytes}] threw ${e.message}`;
        }
    }
    bad ||= heard.map((h) => h.split(' ').map(Number))
        .filter(([c, cc, v]) => !(c < 16 && cc < 120 && cc !== 64 && v < 128))
        .slice(0, 5).map(String).join('; ');
    check(bad === '' && heard.length > 0, 'random messages throw nothing, and only knobs reach onControl',
          bad || 'none reached it');
    open.close();

    const quiet = await openMidi({ onNoteOn: () => {}, onNoteOff: () => {} });

    let threw = '';

    try
    {
        send(0xb0, 74, 100);
    }
    catch (e)
    {
        threw = e.message;
    }
    check(threw === '', 'a page with no onControl, a room, is handed none', threw);
    quiet.close();
}

console.log(`\n${failures} failure${failures === 1 ? '' : 's'}`);
process.exit(failures);
