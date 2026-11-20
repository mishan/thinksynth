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
 * midiroutesui.js -- this browser's own MIDI input routes, as rows to edit
 * and kept in localStorage: a MIDI channel, the engine channel it plays or
 * off, and its keys. They are MIDI in's machine layer (midi.js), which
 * outranks a piece's `midiin' for the MIDI channels it names. The solo
 * page only: in a room, live input plays your own seat.
 */

const STORED = 'thinksynth.midiroutes';
const MIDI_CHANNELS = 16;
const ENGINE_CHANNELS = 64;

const whole = (v, lo, hi) => Number.isInteger(v) && v >= lo && v <= hi;

/* What was kept, the routes that still make sense. */
export function storedRoutes ()
{
    try
    {
        const list = JSON.parse(localStorage.getItem(STORED) ?? '[]');

        return Array.isArray(list)
            ? list.filter((r) => whole(r?.midi, 0, MIDI_CHANNELS - 1) &&
                                 whole(r?.engine, -1, ENGINE_CHANNELS - 1) &&
                                 whole(r?.lo, 0, 127) && whole(r?.hi, r.lo, 127))
            : [];
    }
    catch
    {
        return [];
    }
}

/* The table in `rows', with `add' adding a route; each edit keeps the
 * routes and hands them to `onChange'. Shows what was kept, and hands that
 * over once at the start. */
export function routesTable ({ rows, add, onChange })
{
    let routes = storedRoutes();

    /* Live whether or not the browser will keep them: a private window
       or a full store refuses, and the routes still play this page. */
    const changed = () =>
    {
        try
        {
            localStorage.setItem(STORED, JSON.stringify(routes));
        }
        catch
        {
            /* For this page only, then. */
        }

        onChange(routes);
    };

    const select = (label, options, value, set) =>
    {
        const s = document.createElement('select');

        s.setAttribute('aria-label', label);
        for (const [v, text] of options)
            s.add(new Option(text, String(v)));
        s.value = String(value);
        s.addEventListener('change', () =>
        {
            set(Number(s.value));
            changed();
        });
        return s;
    };

    /* A key field: an edit that is no key puts back what the route
       holds; one that is goes in, the route's lower key first, and the
       table is drawn again so both fields say what it holds. */
    const key = (label, r, end) =>
    {
        const k = document.createElement('input');

        k.type = 'number';
        k.min = '0';
        k.max = '127';
        k.value = String(r[end]);
        k.setAttribute('aria-label', label);
        k.addEventListener('change', () =>
        {
            const v = Math.round(Number(k.value));

            if (k.value.trim() === '' || !whole(v, 0, 127))
            {
                k.value = String(r[end]);
                return;
            }

            r[end] = v;
            [r.lo, r.hi] = [Math.min(r.lo, r.hi), Math.max(r.lo, r.hi)];
            draw();
            changed();
        });
        return k;
    };

    const draw = () =>
    {
        rows.replaceChildren(...routes.map((r, i) =>
        {
            const row = document.createElement('div');
            const of = ` of route ${i + 1}`;
            const remove = document.createElement('button');

            row.className = 'midiroute';
            remove.textContent = 'Remove';
            remove.setAttribute('aria-label', `Remove route ${i + 1}`);
            /* The keyboard stays in the table: on the route that takes
               this one's place, or the one before it, or Add route. */
            remove.addEventListener('click', () =>
            {
                routes.splice(i, 1);
                draw();
                changed();

                const at = Math.min(i, routes.length - 1);

                (at >= 0 ? rows.children[at].querySelector('select')
                         : add).focus();
            });

            row.append(
                'MIDI ',
                select(`MIDI channel${of}`,
                       [...Array(MIDI_CHANNELS).keys()].map((c) => [c, c + 1]),
                       r.midi, (v) => { r.midi = v; }),
                ' plays ',
                select(`Channel it plays${of}`,
                       [[-1, 'off'], ...[...Array(ENGINE_CHANNELS).keys()]
                           .map((c) => [c, c + 1])],
                       r.engine, (v) => { r.engine = v; }),
                ' keys ',
                key(`Lowest key${of}`, r, 'lo'),
                ' to ',
                key(`Highest key${of}`, r, 'hi'),
                ' ', remove);
            return row;
        }));
    };

    add.addEventListener('click', () =>
    {
        routes.push({ midi: 0, engine: 0, lo: 0, hi: 127 });
        draw();
        changed();
    });

    draw();
    onChange(routes);
}
