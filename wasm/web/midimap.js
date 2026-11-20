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
 * midimap.js -- MIDI knobs onto a patch's controls, learned on its panel.
 *
 * A mapping is a MIDI channel and controller onto a control's name, not
 * onto one patch's control: `cutoff' on whatever patch the keys play, over
 * that control's own travel (thSynth::setChanArgFraction, which also lands
 * a menu on a named entry and a whole-number control on a whole number).
 * Any input's knob of that number plays it. A control has one knob, so
 * learning another replaces it; a knob may move several controls.
 *
 * Learn is a control moved and a knob turned, in either order. A knob is
 * the one once it has moved LEARN_MOVE from where it was first heard, so a
 * mod wheel's jitter or a fader resting on the desk doesn't take it.
 *
 * The pedal (64) is never offered: the page holds it on every input and
 * routed channel (midi.js), and a mapping taking its let-up would leave
 * notes hanging. Solo page only; in a room a knob would have to reach
 * every peer at one transport time.
 */

const KEY = 'thinksynth:midimap';
const LEARN_MOVE = 4;

/* What a stored list says, each entry checked and a bad one dropped: the
   text is whatever was in the browser's storage. */
export function readMappings (text)
{
    let list;

    try
    {
        list = JSON.parse(text);
    }
    catch
    {
        return [];
    }

    if (!Array.isArray(list))
        return [];

    const seen = new Set();

    return list.filter((m) => m !== null && typeof m === 'object' &&
                              Number.isInteger(m.channel) &&
                              m.channel >= 0 && m.channel < 16 &&
                              Number.isInteger(m.controller) &&
                              m.controller >= 0 && m.controller < 120 &&
                              m.controller !== 64 &&
                              typeof m.arg === 'string' && m.arg !== '' &&
                              !seen.has(m.arg) && seen.add(m.arg))
        .map(({ channel, controller, arg }) => ({ channel, controller, arg }));
}

/* The mappings and learn, over `text' as readMappings reads it; onChange
 * after each change, for the page to keep and draw.
 *
 * control(channel, controller, value) is a knob heard, and returns the
 * controls it moves, which it still does while learn is on. moved(id) is
 * a panel control moved, which only learn listens to.
 */
export function createMidiMap ({ text = '', onChange = () => {} } = {})
{
    let list = readMappings(text);
    let learning = false, row = null, knob = null;
    const first = new Map();

    const reset = (on) =>
    {
        learning = on;
        row = knob = null;
        first.clear();
        onChange();
    };

    const finish = () =>
    {
        if (row === null || knob === null)
            return;

        list = [...list.filter((m) => m.arg !== row), { ...knob, arg: row }];
        reset(false);
    };

    return {
        mappings: () => list.map((m) => ({ ...m })),
        text: () => JSON.stringify(list),
        learning: () => learning,
        learn: reset,

        moved: (id) =>
        {
            if (!learning)
                return;

            row = id;
            finish();
        },

        control: (channel, controller, value) =>
        {
            const key = `${channel} ${controller}`;

            if (learning && knob === null && !first.has(key))
                first.set(key, value);
            else if (learning && knob === null &&
                     Math.abs(value - first.get(key)) >= LEARN_MOVE)
            {
                knob = { channel, controller };
                finish();
            }

            return list.filter((m) => m.channel === channel &&
                                      m.controller === controller)
                .map((m) => m.arg);
        },

        remove: (i) =>
        {
            list = list.filter((_, j) => j !== i);
            onChange();
        },
    };
}

/* The map kept in the browser's storage, and drawn: a Learn button, a
 * line saying what learn waits for, and a list of what is mapped, each
 * with a Remove button and dimmed where has(arg) says the patch the keys
 * play has no such control.
 *
 * Returns the map, with draw() for when the patch changes.
 */
export function midiMapView ({ button, status, list, has })
{
    let map = null, was = '[]';

    const name = (m) => `Ch ${m.channel + 1} CC ${m.controller} -> ${m.arg}`;
    const draw = () =>
    {
        const now = map.mappings();
        const added = now.find((m) => !was.includes(JSON.stringify(m)));

        was = JSON.stringify(now);
        button.textContent = map.learning() ? 'Cancel learn' : 'Learn knob';

        /* Said, not only shown, for a reader who can't see the list. */
        status.textContent = map.learning()
            ? 'Move a control on the patch panel and turn a knob, with MIDI in on.'
            : added ? `Mapped: ${name(added)}.` : '';
        list.hidden = now.length === 0;
        list.replaceChildren(...now.map((m, i) =>
        {
            const item = document.createElement('li');
            const remove = document.createElement('button');

            item.textContent = `${name(m)}${has(m.arg) ? '' : ' (not on this patch)'} `;
            item.classList.toggle('missing', !has(m.arg));
            remove.textContent = 'Remove';
            remove.ariaLabel = `Remove ${name(m)}`;
            remove.addEventListener('click', () => map.remove(i));
            item.append(remove);
            return item;
        }));
    };

    let text = '';

    try
    {
        text = localStorage.getItem(KEY) ?? '';
    }
    catch
    {
        /* Storage blocked: the map lasts the session. */
    }

    map = createMidiMap({ text, onChange: () =>
    {
        try
        {
            localStorage.setItem(KEY, map.text());
        }
        catch
        {
            /* As above. */
        }
        draw();
    } });
    button.addEventListener('click', () => map.learn(!map.learning()));
    was = map.text();
    draw();
    return { ...map, draw };
}
