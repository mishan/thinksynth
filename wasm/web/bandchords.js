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

/* Chords that follow a kept phrase: a chord a bar, the one in the key that
 * holds most of what the phrase plays in that bar, and a bass on its root
 * and fifth.
 *
 * A chord is what xform::harmonize stacks on a step of the key -- that
 * step, two above it and four above it -- so the chords chosen here are
 * the chords that will sound, in a pentatonic mood as in a seven-note one.
 * A note counts for how long it is held, and more on the bar's first beat
 * and on a beat than between them. A bar with nothing in it keeps the
 * chord before it.
 *
 * Both players are grids, as the phrase is: a grid's step is a time, so
 * all three keep to one bar line however the band was started, and a
 * chord that is not the one wanted is a cell to click.
 */

import { MOODS, nameOf, pitchOf } from './bandkey.js';

const stepsOf = (key) => MOODS.find(([m]) => m === key.mood)[1];

/* Which steps of the key a chord on step `d' has. */
const chordOf = (d, n) => [d, (d + 2) % n, (d + 4) % n];

/* Between chords that fit equally: home, then the two either side of it,
   then the rest. */
const PREFER = [0, 3, 4, 5, 1, 6, 2];

/* A kept grid -- its `notes', `cells' and `steps' as the piece writes
   them -- as the key step of the chord for each bar. */
export function fitChords (grid, key, meter)
{
    const steps = stepsOf(key);
    const n = steps.length;
    const notes = grid.notes.replace(/"/g, '').trim().split(/\s+/)
        .map(pitchOf);
    const rows = grid.cells.replace(/"/g, '').trim().split('/');
    const perBar = meter * 4;
    const bars = Math.max(1, Math.ceil(Number(grid.steps) / perBar));
    const heard = Array.from({ length: bars }, () => new Array(12).fill(0));

    rows.forEach((cells, r) =>
    {
        const pitch = notes[rows.length - 1 - r];

        if (pitch === null || pitch === undefined)
            return;

        for (let s = 0; s < cells.length; s++)
        {
            if (cells[s] !== 'x' && cells[s] !== 'X')
                continue;

            let len = 1;

            while (cells[s + len] === '-' || cells[s + len] === '_')
                len++;

            const at = s % perBar;
            const weight = at === 0 ? 2 : at % 4 === 0 ? 1.5 : 1;
            const bar = Math.floor(s / perBar);

            if (bar < bars)
                heard[bar][((pitch % 12) + 12) % 12] += len * weight;
        }
    });

    const chords = [];

    for (const w of heard)
    {
        const total = w.reduce((a, b) => a + b, 0);

        if (total === 0)
        {
            chords.push(chords.at(-1) ?? 0);
            continue;
        }

        let best = 0, bestScore = -Infinity;

        for (let d = 0; d < n; d++)
        {
            const pcs = chordOf(d, n).map((k) => (key.root + steps[k]) % 12);
            const inside = pcs.reduce((a, pc) => a + w[pc], 0);
            const score = inside - 0.5 * (total - inside) +
                          0.25 * w[pcs[0]] +
                          0.01 * (PREFER.length - PREFER.indexOf(d % 7));

            if (score > bestScore)
            {
                best = d;
                bestScore = score;
            }
        }

        chords.push(best);
    }

    return chords;
}

/* One column a step, a row a rung of the ladder, top rung first. */
function cellsOf (rungs, length, hits)
{
    const rows = Array.from({ length: rungs }, () =>
        new Array(length).fill('.'));

    for (const [step, rung] of hits)
        rows[rungs - 1 - rung][step] = 'x';

    return '"' + rows.map((r) => r.join('')).join('/') + '"';
}

/* The two players for chords `chords' (fitChords's), as band ideas. */
export function followers (chords, key, meter)
{
    const steps = stepsOf(key);
    const n = steps.length;
    const bars = chords.length;

    /* The chords' roots a step of the key each, from the third octave. */
    const ladder = steps.map((s) => nameOf(48 + key.root + s, key));

    /* The bass's two octaves, so a fifth is above its root; the fifth is
       the step nearest seven semitones up, which in a pentatonic is not
       four steps. */
    const low = [...steps, ...steps.map((s) => s + 12)];
    const fifth = (d) => low.reduce((best, s, i) =>
        (Math.abs(s - steps[d] - 7) < Math.abs(low[best] - steps[d] - 7)
            ? i : best), d);
    const eighths = meter * 2;
    const pattern = [0, 3, 6].filter((p) => p < eighths);
    const bass = chords.flatMap((d, b) => pattern.map((p, i) =>
        [b * eighths + p, i === pattern.length - 1 && i > 0 ? fifth(d) : d]));

    return {
        chords: { call: 'chords', name: 'Chords to your phrase',
                  say: 'a chord a bar, fitted to what you played',
                  sound: 'strings.dsp', amp: '20',
                  stages: [['gen', 'grid', {
                      notes: `"${ladder.join(' ')}"`, steps: String(bars),
                      rows: String(n),
                      cells: cellsOf(n, bars, chords.map((d, b) => [b, d])),
                      period: `${meter} beats`,
                      hold: `${meter - 0.2} beats`, vel: '72' }],
                           ['xform', 'harmonize', { scale: 'key',
                                                    voices: '3',
                                                    step: '2' }]] },
        bass: { call: 'bass', name: 'Bass to your phrase',
                say: 'the root of each chord, and its fifth',
                sound: 'ebass.dsp', amp: '26',
                stages: [['gen', 'grid', {
                    notes: '"' + low.map((s) =>
                        nameOf(24 + key.root + s, key)).join(' ') + '"',
                    steps: String(bars * eighths), rows: String(low.length),
                    cells: cellsOf(low.length, bars * eighths, bass),
                    period: '0.5 beats', hold: '0.4 beats',
                    vel: '100' }]] },
    };
}
