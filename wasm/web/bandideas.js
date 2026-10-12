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

/* What a band's player can be started from: per role, an idea -- a chain
 * with the stages that make it that idea -- and the sound it is first
 * heard on. Everything here is in A minor, the key a new band is in until
 * it has a key of its own to follow.
 *
 * An idea becomes edits of the piece's text (thinkweb.cpp, tw_gen_ops):
 * an instrument, and a chain playing it.
 */

const KEY = {
    bass: '"A1 C2 D2 E2 G2"',
    mid: '"A3 C4 D4 E4 G4 A4 C5"',
    high: '"A4 C5 D5 E5 G5 A5 C6 D6"',
    scale: '"A3 B3 C4 D4 E4 F4 G4"',
};

export const IDEAS = {
    drums: [
        { call: 'kick', name: 'Four on the floor',
          say: 'a kick on every beat', sound: 'kick909.dsp', amp: '34',
          stages: [['gen', 'euclid', { steps: '4', fills: '4',
                                       notes: '"C2"', period: '1 beats',
                                       hold: '0.2 beats', vel: '110' }]] },
        { call: 'snare', name: 'Backbeat', say: 'a snare on two and four',
          sound: 'snare808.dsp', amp: '24',
          stages: [['gen', 'euclid', { steps: '4', fills: '2', rotate: '1',
                                       notes: '"D2"', period: '1 beats',
                                       hold: '0.2 beats', vel: '96' }]] },
        { call: 'hats', name: 'Eighth hats', say: 'a hat on every half beat',
          sound: 'hat808.dsp', amp: '15',
          stages: [['gen', 'euclid', { steps: '8', fills: '8',
                                       notes: '"F#2"', period: '0.5 beats',
                                       hold: '0.1 beats', vel: '70' }]] },
        { call: 'beat', name: 'Broken beat',
          say: 'five hits spread over a bar', sound: 'kick909.dsp',
          amp: '34',
          stages: [['gen', 'euclid', { steps: '16', fills: '5',
                                       notes: '"C2"', period: '0.25 beats',
                                       hold: '0.2 beats', vel: '104' }]] },
        { call: 'drum', name: 'Draw a beat', say: 'a row of steps to click',
          sound: 'snare808.dsp', amp: '24',
          stages: [['gen', 'grid', { notes: '"D2"', steps: '16', rows: '1',
                                     period: '0.25 beats',
                                     hold: '0.2 beats', vel: '92' }]] },
    ],
    bass: [
        { call: 'bass', name: 'Root pulse', say: 'the root, pushed along',
          sound: 'ebass.dsp', amp: '26',
          stages: [['gen', 'euclid', { steps: '8', fills: '3',
                                       notes: '"A1 A1 E2"',
                                       period: '0.5 beats',
                                       hold: '0.4 beats', vel: '100' }]] },
        { call: 'bass', name: 'Wandering bass',
          say: 'slow notes that never line up', sound: 'ebass.dsp',
          amp: '26',
          stages: [['gen', 'eno_line', { notes: KEY.bass,
                                         period: '2 beats',
                                         jitter: '0.5 beats', prob: '0.8',
                                         hold: '1.5 beats', vel: '96' }]] },
        { call: 'bass', name: 'Draw a bassline',
          say: 'a grid of steps and notes to click', sound: 'ebass.dsp',
          amp: '26',
          stages: [['gen', 'grid', { notes: KEY.bass, steps: '16',
                                     rows: '5', period: '0.25 beats',
                                     hold: '0.4 beats', vel: '96' }]] },
    ],
    chords: [
        { call: 'chords', name: 'Slow changes',
          say: 'a chord every two bars', sound: 'strings.dsp', amp: '20',
          stages: [['gen', 'progression', { scale: KEY.scale,
                                            every: '8 beats',
                                            hold: '7.8 beats', vel: '72',
                                            phrase: '4' }],
                   ['xform', 'harmonize', { scale: KEY.scale,
                                            voices: '3', step: '2' }]] },
        { call: 'chords', name: 'Pulsing chords',
          say: 'a chord on every other beat', sound: 'juno.dsp', amp: '20',
          stages: [['gen', 'progression', { scale: KEY.scale,
                                            every: '2 beats',
                                            hold: '1.5 beats', vel: '72',
                                            phrase: '4' }],
                   ['xform', 'harmonize', { scale: KEY.scale,
                                            voices: '3', step: '2' }]] },
    ],
    melody: [
        { call: 'keys', name: 'Tape loops',
          say: 'notes on loops that never line up', sound: 'rhodes.dsp',
          amp: '21',
          stages: [['gen', 'eno_line', { notes: KEY.mid, period: '3 s',
                                         jitter: '1 s', prob: '0.7',
                                         hold: '2 s', vel: '70' }]] },
        { call: 'lead', name: 'Growing phrase',
          say: 'a motif that branches', sound: 'epiano.dsp', amp: '22',
          stages: [['gen', 'lsystem', { axiom: '"F_+F_-F+F__"',
                                        rules: '"F=F+F-"', depth: '2',
                                        notes: KEY.mid, step: '0.5 beats',
                                        hold: '0.4 beats', vel: '80' }]] },
        { call: 'keys', name: 'Draw a melody',
          say: 'a grid of steps and notes to click', sound: 'rhodes.dsp',
          amp: '21',
          stages: [['gen', 'grid', { notes: KEY.mid, steps: '16',
                                     rows: '7', period: '0.25 beats',
                                     hold: '0.5 beats', vel: '80' }]] },
    ],
    texture: [
        { call: 'air', name: 'Game of Life',
          say: 'a glider across a board you can paint', sound: 'bowl.dsp',
          amp: '18',
          stages: [['gen', 'life', {
              board: '"........../........../........../........../' +
                     '........../.O......../..OO....../.OO......."',
              width: '10', height: '8', scatter: '0', wrap: '1',
              notes: KEY.high, period: '0.5 beats', hold: '0.4 beats',
              vel: '50' }]] },
        { call: 'air', name: 'A soup of cells',
          say: 'a random board, another every take', sound: 'cloud.dsp',
          amp: '18',
          stages: [['gen', 'life', { width: '10', height: '8',
                                     scatter: '0.3', wrap: '1',
                                     notes: KEY.high, period: '0.5 beats',
                                     hold: '0.4 beats', vel: '50' }]] },
    ],
};

/* The graph categories a role's sounds are offered from, nearest first. */
export const SOUNDS = {
    drums: ['Drums'],
    bass: ['Bass', 'Synths'],
    chords: ['Strings and pads', 'Keys', 'Synths'],
    melody: ['Keys', 'Leads and stabs', 'Plucked', 'Synths'],
    texture: ['Strings and pads', 'Experiments', 'Keys'],
};

/* A name not among `taken': `call', or `call2', `call3'... */
export function freeName (call, taken)
{
    let name = call;

    for (let n = 2; taken.has(name); n++)
        name = `${call}${n}`;

    return name;
}

/* The edits that add `idea' as a player called `name', on an instrument of
   the same name. */
export function addOps (idea, name)
{
    const pairs = (params) =>
        Object.entries(params).map(([k, v]) => `${k}=${v}`);
    const [first, ...rest] = idea.stages;

    return [
        ['instrument', name, idea.sound, `amp=${idea.amp}`],
        ['chain', name, name, 'src', first[0], first[1],
         ...pairs(first[2])],
        ...rest.map((s, i) => ['stage', name, `s${i + 2}`, s[0], s[1],
                               ...pairs(s[2])]),
    ];
}

/* The edits that take a player out: its chain, and its instrument unless
   another chain still plays it. */
export function removeOps (chain, instrument)
{
    return [['unchain', chain],
            ...(instrument ? [['uninstrument', instrument]] : [])];
}

/* What a new band starts as: a beat, a bass and keys. */
export const NEW_BAND = {
    text: 'name "A new band";\ntempo 100;\n',
    ideas: [['drums', 'Four on the floor'], ['drums', 'Backbeat'],
            ['drums', 'Eighth hats'], ['bass', 'Root pulse'],
            ['melody', 'Tape loops']],
};

export function ideaNamed (role, name)
{
    return IDEAS[role].find((i) => i.name === name);
}
