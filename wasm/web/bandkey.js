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

/* A band's key: a root and a mood, the mood a scale said the way
 * somebody would pick it rather than by its mode's name.
 *
 * The piece keeps it as a scale called `key', one octave from the root,
 * which chord players name and which is read back here. A piece without
 * one is in whatever key its pitched notes fit best.
 *
 * A new key moves every pitched player's notes there: a note goes to the
 * same step of the new scale, in the same octave of it, so a line keeps
 * its shape; where the two scales differ in size, to the nearest note of
 * the new one. Drums are left where they are, since a drum's note is
 * which drum it is.
 */

/* Each mood: its steps above the root; which step of a seven-note scale
   each of them is, so that a pentatonic's third goes to a third; and how
   far above the root the major key it is spelled like lies, for its
   flats or sharps. */
export const MOODS = [
    ['bright', [0, 2, 4, 5, 7, 9, 11], [0, 1, 2, 3, 4, 5, 6], 0],
    ['dark', [0, 2, 3, 5, 7, 8, 10], [0, 1, 2, 3, 4, 5, 6], 3],
    ['open', [0, 2, 4, 7, 9], [0, 1, 2, 4, 5], 0],
    ['bluesy', [0, 3, 5, 7, 10], [0, 2, 3, 4, 6], 3],
    ['folk', [0, 2, 3, 5, 7, 9, 10], [0, 1, 2, 3, 4, 5, 6], 10],
    ['dreamy', [0, 2, 4, 6, 7, 9, 11], [0, 1, 2, 3, 4, 5, 6], 7],
    ['exotic', [0, 1, 4, 5, 7, 8, 10], [0, 1, 2, 3, 4, 5, 6], 8],
];

export const ROOTS = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A',
                      'Bb', 'B'];

/* What a band starts in: the ideas' notes are written in it. */
export const START = { root: 9, mood: 'dark' };

const STEP = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };

/* `Eb4' as 63, or null for anything that is not a note. */
export function pitchOf (name)
{
    const m = /^([A-G])([#b]?)(-?\d)$/.exec(name);

    if (m === null)
        return null;

    return 12 * (Number(m[3]) + 1) + STEP[m[1]] +
           (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0);
}

/* 63 as `Eb4', in flats or sharps as key `key' would be written. */
export function nameOf (pitch, key)
{
    const major = (key.root + mood(key.mood)[3]) % 12;
    const flats = [5, 10, 3, 8, 1, 6].includes(major);
    const names = flats
        ? ['C', 'Db', 'D', 'Eb', 'E', 'F', 'Gb', 'G', 'Ab', 'A', 'Bb', 'B']
        : ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

    return names[((pitch % 12) + 12) % 12] + (Math.floor(pitch / 12) - 1);
}

const mood = (name) => MOODS.find(([m]) => m === name);

/* The words of a note list, which may be parted by spaces or commas. */
const wordsOf = (text) => text.replace(/"/g, '').trim().split(/[\s,]+/)
    .filter((w) => w !== '');

/* The scale a piece writes for `key', one octave from the root. */
export function keyNotes (key)
{
    return mood(key.mood)[1].map((s) => nameOf(48 + key.root + s, key))
        .join(' ');
}

/* A quoted note list's pitches, or null when it is not one: a rest is
   kept as null in its place. */
function listOf (valueText)
{
    const m = /^"([^"]*)"$/.exec(valueText.trim());

    if (m === null)
        return null;

    const words = wordsOf(m[1]);
    const pitches = words.map((w) => (w === '.' ? null : pitchOf(w)));

    return words.length > 0 && pitches.every((p, i) => p !== null ||
                                                       words[i] === '.')
        ? pitches : null;
}

/* The key of a piece: its `key' scale, or the root and mood its pitched
   notes fit best, or where a band starts. */
export function keyOf (piece, pitched)
{
    const written = (piece.scales ?? []).find((s) => s.name === 'key');

    const count = new Array(12).fill(0);
    const heard = notesIn(piece, pitched);

    if (written !== undefined)
    {
        const pitches = wordsOf(written.notes).map(pitchOf);
        const root = ((pitches[0] % 12) + 12) % 12;
        const set = [...new Set(pitches.map((p) =>
            (((p - pitches[0]) % 12) + 12) % 12))].sort((a, b) => a - b)
            .join();
        const named = MOODS.find(([, s]) => s.join() === set)?.[0];

        if (named !== undefined && !pitches.includes(null))
            return { root, mood: named, written: true };

        /* A key of the piece's own that is none of these moods still says
           what it sounds in. */
        heard.push(...pitches.filter((p) => p !== null));
    }

    if (heard.length === 0)
        return { ...START, written: false };

    for (const p of heard)
        count[((p % 12) + 12) % 12]++;

    const lowest = ((Math.min(...heard) % 12) + 12) % 12;

    let best = { ...START, score: -Infinity };

    /* Notes inside the scale count for it and notes outside twice
       against it; a step it has that is never played counts a little
       against it, so a pentatonic tune is not taken for a whole scale; a
       relative major and minor are told apart by the lowest note, which
       is the bass's root more often than not. */
    const total = count.reduce((a, b) => a + b, 0);

    for (let root = 0; root < 12; root++)
        for (const [mood, s] of MOODS)
        {
            const inside = s.reduce((n, step) =>
                n + count[(root + step) % 12], 0);
            const unused = s.filter((step) =>
                count[(root + step) % 12] === 0).length;
            const score = inside - 2 * (total - inside) - 0.3 * unused +
                          (root === lowest ? 0.2 : 0) + 0.01 * count[root];

            if (score > best.score)
                best = { root, mood, score };
        }

    return { root: best.root, mood: best.mood, written: false };
}

/* Every pitch the pitched players and the scales they name play. */
function notesIn (piece, pitched)
{
    const out = [];

    for (const { stage } of pitchedParams(piece, pitched))
        for (const p of stage)
            out.push(p);

    for (const s of scalesUsed(piece, pitched))
        for (const w of wordsOf(s.notes))
            if (pitchOf(w) !== null)
                out.push(pitchOf(w));

    return out;
}

/* The quoted note lists of the pitched players' stages, as
   { chain, docStage, row, stage: [pitches] }. */
function pitchedParams (piece, pitched)
{
    const out = [];

    for (const c of piece.chains ?? [])
    {
        if (!pitched.has(c.name))
            continue;

        c.stages.forEach((s, docStage) =>
        {
            for (const p of s.params)
            {
                const list = listOf(p.valueText);

                if (list !== null)
                    out.push({ chain: c.name, docStage, row: p.name,
                               stage: list.filter((x) => x !== null),
                               list });
            }
        });
    }

    return out;
}

/* The piece's scales a pitched player names and no drum does, but `key'
   itself: a drum's scale moved would be other drums. */
function scalesUsed (piece, pitched)
{
    const named = new Set(), drums = new Set();

    for (const c of piece.chains ?? [])
        for (const s of c.stages)
            for (const p of s.params)
                (pitched.has(c.name) ? named : drums)
                    .add(p.valueText.trim());

    return (piece.scales ?? []).filter((s) => s.name !== 'key' &&
                                              named.has(s.name) &&
                                              !drums.has(s.name));
}

/* Where a root sits: within a tritone of where a band starts, whatever
   way round it was reached, so that a key changed and changed back puts
   every note back where it was. */
const place = (root) => ((root - START.root + 18) % 12) - 6;

/* `pitch' from key `from' to key `to': the same step of the new scale --
   by its place in a seven-note scale, so a pentatonic's third is a third
   -- in the same octave of it. A step the new scale has not got goes to
   its nearest, either way round the octave; a note between two steps
   keeps its distance above the lower one. A note out of MIDI's range is
   brought back by octaves. */
export function retune (pitch, from, to)
{
    const [, a, aDeg] = mood(from.mood), [, b, bDeg] = mood(to.mood);
    const rel = pitch - from.root;
    const octave = Math.floor(rel / 12);
    const pos = rel - 12 * octave;
    let lower = a.length - 1;

    while (a[lower] > pos)
        lower--;

    const offset = pos - a[lower];
    const step = b[bDeg.indexOf(aDeg[lower])] ??
        b.flatMap((s) => [s - 12, s, s + 12]).reduce((x, y) =>
            (Math.abs(y - a[lower]) < Math.abs(x - a[lower]) ? y : x));
    let out = from.root + place(to.root) - place(from.root) + 12 * octave +
              step + offset;

    while (out < 0)
        out += 12;

    while (out > 127)
        out -= 12;

    return out;
}

/* The edits that move the pitched players from key `from' to key `to',
   for tw_gen_ops: their note lists, the scales they name, and `key'. */
export function keyOps (piece, pitched, from, to)
{
    const move = (p) => retune(p, from, to);
    const ops = [['scale', 'key', keyNotes(to)]];

    for (const { chain, docStage, row, list } of pitchedParams(piece,
                                                                pitched))
        ops.push(['param', chain, String(docStage), row,
                  '"' + list.map((p) => (p === null ? '.'
                                                    : nameOf(move(p), to)))
                      .join(' ') + '"']);

    /* A scale is a set of notes: two steps moved onto one are one. */
    for (const s of scalesUsed(piece, pitched))
        ops.push(['scale', s.name,
                  [...new Set(wordsOf(s.notes).map((w) => (
                      pitchOf(w) === null ? w
                                          : nameOf(move(pitchOf(w)), to))))]
                      .join(' ')]);

    return ops;
}

/* A quoted note list in an idea, written in START, moved to `to'. */
export function inKey (valueText, to)
{
    const list = listOf(valueText);

    if (list === null)
        return valueText;

    return '"' + list.map((p) => (p === null
        ? '.' : nameOf(retune(p, START, to), to))).join(' ') + '"';
}
