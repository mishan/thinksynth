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

/* The band: a piece as the players in it rather than the chains it is
 * written as.
 *
 * A player is a chain that plays notes, named for its role -- drums, bass,
 * chords, melody, texture -- and what it plays is steered rather than
 * set: a pad whose across is calm to busy and whose up is steady to
 * wandering, an octave either way, another take (Vary), the take kept as
 * notes (Keep), and a rest. Each is an edit of the piece's text by the
 * same doors the canvas uses, so what the band does is in the .gen like
 * anything else.
 *
 * The pad moves params the chain has. RECIPES says which, per plugin,
 * and how far: the middle of the pad is the piece as it was when the band
 * first showed it, the corners as far as each param sensibly goes. A chain
 * with nothing on an axis -- a drawn grid -- is given stages for it the
 * first time the pad moves (TOUCH), which play what is written loosely or
 * tightly rather than rewriting it.
 */

import { IDEAS as GALLERY, PHRASE, SOUNDS, addOps, freeName, ideaNamed,
         removeOps } from './bandideas.js';
import { MOODS, ROOTS, inKey, keyNotes, keyOf, keyOps } from './bandkey.js';
import { replay } from './replay.js';
import { gestureOn } from './seqview.js';

const $ = (id) => document.getElementById(id);

/* Below the middle toward `lo', above it toward `hi'. */
const toward = (lo, base, hi, t) =>
    t < 0.5 ? lo + (base - lo) * t * 2 : base + (hi - base) * (t - 0.5) * 2;

/* A time between events, halved at busy and doubled at calm. */
const faster = (base, t) => base * 2 ** ((0.5 - t) * 2);

/* As much again at most, and never past 1: a chance. */
const more = (floor) => (base, t) =>
    toward(0, base, Math.min(1, Math.max(2 * base, floor)), t);

/* A count: spelled as a whole number. */
const whole = (f) => Object.assign((base, t, p) => Math.round(f(base, t, p)),
                                   { whole: true });

/* Births only toward calm, every live cell toward busy. */
const everyCell = whole((b, t) => (t > 0.75 ? 1 : t < 0.25 ? 0 : b));

/* Wolfram rules from those that settle into stripes to those that boil:
   the CA's own rule is where the middle of the pad is, wherever it falls,
   and the rule moves while the automaton runs (gen::ca keeps its row). */
const RULES = [4, 108, 50, 90, 150, 105, 110, 54, 45, 30];

function calmerRule (base, t)
{
    const ladder = RULES.includes(base) ? RULES
                 : [...RULES.slice(0, 5), base, ...RULES.slice(5)];
    const at = ladder.indexOf(base);
    const i = t < 0.5 ? at * t * 2 : at + (ladder.length - 1 - at) *
                                         (t - 0.5) * 2;

    return ladder[Math.round(i)];
}

/* Per plugin, per axis, per param: the value at `t' from the value as
   written, `base', with the stage's other numbers in `p'. */
export const RECIPES = {
    'gen::euclid': {
        busy: { fills: whole((b, t, p) => toward(1, b, p.steps ?? b, t)) } },
    'gen::lsystem': {
        busy: { step: faster },
        wander: { depth: whole((b, t) => Math.max(Math.min(b, 1),
                                                  toward(b - 1, b, b + 1,
                                                         t))) } },
    'gen::eno_line': {
        busy: { period: faster, prob: (b, t) => toward(b / 3, b, 1, t) },
        wander: { jitter: (b, t, p) =>
            toward(0, b, Math.max(2 * b, (p.period ?? 0) / 2), t) } },
    'gen::progression': {
        busy: { every: faster },
        wander: { wander: more(0.6) } },
    'gen::markov': {
        busy: { period: faster },
        wander: { order: whole((b, t) => toward(2, Math.min(b, 2), 1,
                                                t)) } },
    'gen::arp': {
        busy: { period: faster },
        wander: { octaves: whole((b, t) => toward(1, b, Math.max(b, 3),
                                                  t)) } },
    'gen::life': { busy: { period: faster, trigger: everyCell } },
    'gen::ca': {
        busy: { period: faster, trigger: everyCell },
        wander: { rule: whole(calmerRule) } },
    'xform::chance': { busy: { prob: (b, t) => toward(b / 2, b, 1, t) } },
    'xform::ratchet': { busy: { prob: more(0.3) } },
    'xform::humanize': {
        wander: { time: (b, t) => toward(0, b, Math.max(2 * b, 0.03), t),
                  vel: (b, t) => toward(0, b, Math.max(2 * b, 20), t) } },
    'xform::cloud': { wander: { pitch: (b, t) => toward(0, b,
                                                        Math.max(2 * b, 7),
                                                        t) } },
    'xform::vary': {
        wander: Object.fromEntries(
            ['rest', 'leap', 'push', 'double', 'approach', 'ornament']
                .map((n) => [n, more(0.2)])) },
};

/* What a stage's idea is, said the way somebody would say it. */
const IDEAS = {
    'gen::euclid': 'a rhythm ring', 'gen::grid': 'a drawn pattern',
    'gen::lsystem': 'a growing phrase', 'gen::eno_line': 'tape loops',
    'gen::markov': 'learns and wanders', 'gen::progression': 'chord changes',
    'gen::arp': 'an arpeggio', 'gen::life': 'the Game of Life',
    'gen::ca': 'a cellular pattern', 'gen::evolve': 'an evolving phrase',
    'gen::steps': 'a step pattern', 'gen::walk': 'a drifting sound',
    'gen::breed': 'a bred sound', 'gen::grow': 'a growing sound',
    'gen::reshape': 'a reshaped sound', 'gen::pump': 'a pumping swell',
    'gen::morph': 'a morphing sound', 'gen::stutter': 'a stuttered end',
    'gen::swap': 'changing sounds',
};

/* What a player's card shows of it: the first stage that draws, one that
   takes a pointer before one that does not, and a board before the grid
   that seeds it -- a grid is a sequencer track on the card, a Life board
   one to paint on. */
const PICTURES = ['life', 'ca', 'grid', 'euclid', 'accent', 'evolve',
                  'lsystem', 'markov', 'morph', 'breed'];

/* The ones of those that take a pointer (composer_input); the rest are
   only looked at, and leave a swipe to scroll the list. */
const CONTROLS = new Set(['life', 'ca', 'grid', 'euclid', 'accent',
                          'evolve']);

/* The plugins that draw from the seed, whose takes another seed changes,
   and when: Life and CA only when they scatter -- a drawn board plays the
   same way every time, and another seed only starts it over -- an arp
   only in its random pattern, a progression only on the stage's seed. */
const RANDOM = new Set(['arp', 'cloud', 'humanize', 'chance', 'evolve',
                        'stutter', 'eno_line', 'markov', 'walk', 'swap',
                        'progression', 'ratchet', 'vary', 'breed', 'run',
                        'grow', 'life', 'ca']);

/* An xform::vary's rows that draw from the seed. */
const VARIES = ['rest', 'leap', 'push', 'double', 'approach', 'ornament',
                'pan', 'tone', 'attack'];

const valueOf = (s, row, or) =>
    numberOf(s.params.find((p) => p.name === row)?.valueText ?? '')?.num ??
    or;

function drawsFromSeed (s)
{
    switch (s.plugin)
    {
        case 'life': case 'ca': return valueOf(s, 'scatter', 0) > 0;
        case 'arp': return valueOf(s, 'pattern', 0) === 3;
        case 'progression': return valueOf(s, 'seed', 0) === 0;

        /* As TOUCH and TAKES write them at rest, they pass every note
           as it came, whatever the seed. */
        case 'chance': return valueOf(s, 'prob', 0.75) < 1;
        case 'ratchet': return valueOf(s, 'prob', 0.25) > 0;
        case 'humanize':
            return valueOf(s, 'time', 0.02) > 0 || valueOf(s, 'vel', 0) > 0;
        case 'vary':
            return VARIES.some((row) => valueOf(s, row, 0) !== 0);
        default: return RANDOM.has(s.plugin);
    }
}

/* What another seed does to a player: 'vary' another take, 'restart' the
   same board from the top, or nothing. */
export function seededAs (stages)
{
    stages = stages.filter((s) => !isEnergyStage(s));

    if (stages.some(drawsFromSeed))
        return 'vary';

    return stages.some((s) => s.plugin === 'life' || s.plugin === 'ca')
        ? 'restart' : null;
}

/* How tall a picture is drawn at `w' CSS pixels wide: a grid's rows big
   enough to hit, a board's cells square, a ring room to be a ring. A param
   the piece does not write is at the plugin's default. */
function pictureHeight (s, w)
{
    const n = (row, or) => valueOf(s, row, or);

    switch (s.plugin)
    {
        case 'grid': return Math.min(160, Math.max(26, n('rows', 8) * 16));
        case 'life':
            return Math.round(Math.max(40, Math.min(16, w / n('width', 10)) *
                                           n('height', 8)));
        case 'euclid': return 70;
        default: return 64;
    }
}

/* By whole words of a chain's, instrument's and graph's names; chords
   before bass, so a `pad_low' is a pad. */
const ROLES = [
    ['drums', /^(kick|snare|hats?|clap|toms?|drums?|perc\w*|rim|cymbal|ride|crash|tamb\w*|shaker|conga|bongo|cowbell|kit)$/],
    ['chords', /^(chords?|pads?|strings?|organ|choir|comp|section)$/],
    ['bass', /^(\w*bass|sub)$/],
    ['melody', /^(keys|lead|melody|piano|rhodes|epiano|bells?|tune|hook)$/],
    ['texture', /^(air|texture|drones?|ambient|noise|wash|cloud|swoop|fx|atmo\w*)$/],
];

/* `0.25 beats' as 0.25 and ` beats'; anything that is not a number --
   a knob, a node, a note list, a scale's name -- as null. */
export function numberOf (valueText)
{
    const m = /^(-?\d+(?:\.\d+)?)(\s*[a-z%]*)$/.exec(valueText.trim());

    return m === null ? null : { num: Number(m[1]), unit: m[2] };
}

function spell (num, unit, integer)
{
    const text = integer ? String(Math.round(num))
                         : String(Number(num.toPrecision(4)));

    return text + unit;
}

/* The stages the band adds behind a player for a control of the card's
   own, each with the one param it sets: they work on whatever the
   chain's notes come from. The volume is a note's level at the mix, not
   its velocity, which a hat reads as how open it is. */
const OWN = {
    octave: { name: 'octave', plugin: 'transpose', row: 'semitones' },
    volume: { name: 'volume', plugin: 'level', row: 'gain' },
    take: { name: 'take', plugin: 'vary' },
};

/* One of them on a chain -- under its name, or a number after it where
   the name was taken -- and its param, null with none. */
const ownStage = (stages, own) => stages.find((s) =>
    s.plugin === own.plugin && s.live >= 0 &&
    new RegExp(`^${own.name}\\d*$`).test(s.name));

const ownValue = (stages, own) => numberOf(ownStage(stages, own)?.params
    .find((q) => q.name === own.row)?.valueText ?? '')?.num ?? null;

/* How many octaves the band has moved a player's notes. */
export const octavesOf = (stages) =>
    Math.round((ownValue(stages, OWN.octave) ?? 0) / 12);

/* A player's volume, 1 as written; up to VOLUME_MAX. */
export const volumeOf = (stages) => ownValue(stages, OWN.volume) ?? 1;

const VOLUME_MAX = 2;

/* How far either way the octave buttons go. */
const OCTAVES = 3;

const octaveWords = (n) => n === 0 ? 'as written'
    : `${Math.abs(n)} octave${Math.abs(n) > 1 ? 's' : ''} ` +
      (n > 0 ? 'up' : 'down');

/* The players in a piece (worklet.js, piece: `chains' and `instruments'):
   every chain with a note sink, with its role, the instrument it plays
   and its idea. */
export function playersOf (piece)
{
    const dspOf = new Map((piece.instruments ?? []).map((i) => [i.name,
                                                                i.dsp]));
    const channelOf = new Map((piece.instruments ?? []).map((i) =>
        [i.name, i.channel]));

    return (piece.chains ?? []).flatMap((c, index) =>
    {
        const sink = c.sinks.find((s) => s.chanarg === '' && s.knob === '');

        if (sink === undefined)
            return [];

        const sound = sink.instrument !== ''
            ? sink.instrument : `channel ${sink.channel}`;
        const dsp = dspOf.get(sink.instrument) ?? '';
        const gens = c.stages.filter((s) => s.category === 'gen');
        const plugins = c.stages.map((s) => `${s.category}::${s.plugin}`);
        /* The chain's own name first: a graph says what kind of sound it
           is, and the name what the player is for. */
        const words = (t) => t.toLowerCase().split(/[^a-z]+/);
        const named = (t) => ROLES.find(([, re]) =>
            words(t).some((w) => re.test(w)))?.[0];
        let role = named(c.name) ?? named(`${sound} ${dsp}`);

        if (role === undefined)
            role = plugins.some((p) => /progression|harmonize/.test(p))
                ? 'chords'
                : plugins.some((p) => /life|::ca$|cloud|eno_line/.test(p))
                    ? 'texture' : 'melody';

        const idea = gens.length === 0
            ? (c.input ? 'what you play' : 'what comes in')
            : IDEAS[`gen::${gens[0].plugin}`] ?? 'a pattern';

        const picture = PICTURES.map((n) => c.stages.find(
            (s) => s.plugin === n && s.live >= 0)).find(Boolean) ?? null;

        return [{ index, name: c.name, role, sound, idea, stages: c.stages,
                  input: c.input === true,
                  seed: c.hasSeed ? c.seed : null, picture,
                  seeded: seededAs(c.stages), instrument: sink.instrument,
                  dsp, channel: sink.instrument === ''
                      ? sink.channel - 1 : channelOf.get(sink.instrument) }];
    });
}

/* The param edits the pad at (x, y) makes from `base', the stages as they
   were: [{ stage, stageName, row, text }] for every param whose text is not
   what `said' (`stage:row' to text) says was sent last, or else as
   written -- or for every param, with `said' null. */
export function padEdits (stages, x, y, said = new Map())
{
    const edits = [];

    for (const s of stages)
    {
        const recipe = RECIPES[`${s.category}::${s.plugin}`];

        if (recipe === undefined || s.live < 0)
            continue;

        const nums = {};

        for (const p of s.params)
        {
            const n = numberOf(p.valueText);

            if (n !== null)
                nums[p.name] = n.num;
        }

        for (const [axis, t] of [['busy', x], ['wander', y]])
            for (const [row, f] of Object.entries(recipe[axis] ?? {}))
            {
                const p = s.params.find((q) => q.name === row);
                const n = p === undefined ? null : numberOf(p.valueText);

                if (n === null)
                    continue;

                const moved = spell(Math.max(0, f(n.num, t, nums)), n.unit,
                                    f.whole === true);

                /* Back where it was written is as written, not respelled. */
                const text = numberOf(moved)?.num === n.num ? p.valueText
                                                           : moved;

                if (said === null ||
                    text !== (said.get(`${s.name}:${row}`) ?? p.valueText))
                    edits.push({ stage: s.live, stageName: s.name, row,
                                 text });
            }
    }

    return edits;
}

/* Which axes `stages' have anything on. */
export function axesOf (stages)
{
    const has = { busy: false, wander: false };

    for (const s of stages)
        for (const axis of ['busy', 'wander'])
            if (Object.keys(RECIPES[`${s.category}::${s.plugin}`]?.[axis] ??
                            {}).some((row) => s.params.some(
                                (p) => p.name === row &&
                                       numberOf(p.valueText) !== null)))
                has[axis] = true;

    return has;
}

/* What the band puts behind a player whose pad has nothing to move on an
 * axis -- a drawn grid, a plain ring -- so that the pad plays what is
 * written rather than rewriting it: across, notes dropped toward calm and
 * rolled toward busy; up, the line loosened. At the middle of the pad each
 * passes every note as it came. A drum is loosened in time and touch only,
 * since a note moved in pitch is another drum.
 */
const TOUCH = {
    busy: () => [
        { name: 'thin', plugin: 'chance', params: { prob: '1' } },
        { name: 'rolls', plugin: 'ratchet', params: { prob: '0' } }],
    wander: (drums) => drums
        ? [{ name: 'loose', plugin: 'humanize',
             params: { time: '0 s', vel: '0' } }]
        : [{ name: 'loose', plugin: 'vary',
             params: { grid: '0.25 beats', rest: '0', leap: '0', push: '0',
                       double: '0' } }],
};

/* What the band puts behind a player with nothing random in it -- a
   drawn grid, a plain ring, a written phrase -- for Vary to have takes
   of: a note now and then left out or played twice, and a pitched one
   now and then an octave away, which keeps it in key. */
const TAKES = (drums) => ({
    name: 'take', plugin: 'vary',
    params: { rest: '0.1', double: '0.12', leap: drums ? '0' : '0.08' } });

/* Those, set for another take or, `on' false, to pass every note as
   written: the take the card had before. */
const takeEdits = (s, drums, on) => Object.entries(TAKES(drums).params)
    .map(([row, text]) => ({ stage: s.live, stageName: s.name, row,
                             text: on ? text : '0' }));

/* The band's energy: one knob, 0 to 1 and as written at a half, that
 * every player follows through stages of its own -- below the half a
 * note now and then left out, down to one in two, and quieter; above it
 * a drum now and then a roll and a pitched note now and then played
 * twice, and louder. Arithmetic on the knob, so the piece plays it as
 * written wherever it is played, and a pad, which moves numbers, leaves
 * it alone.
 */
const ENERGY = 'energy';

const energyStages = (drums) => [
    { name: 'energy', plugin: 'chance', params: { prob: '0.5 + @energy' } },
    drums ? { name: 'energy_fill', plugin: 'ratchet',
              params: { prob: '@energy * 0.6 - 0.3' } }
          : { name: 'energy_fill', plugin: 'vary',
              params: { double: '@energy * 0.4 - 0.2' } },
    { name: 'energy_level', plugin: 'level',
      params: { gain: '0.7 + @energy * 0.6' } },
];

const isEnergyStage = (s) => /^energy(_fill|_level)?$/.test(s.name);

/* Those `stages' has not got, as ops adding them behind chain `name'
   (tw_gen_ops): a kept player has the level, which a freeze carries,
   and not the others. */
const energyOps = (name, drums, stages = []) => energyStages(drums)
    .filter((st) => !stages.some((s) => s.name === st.name))
    .map((st) => ['stage', name, st.name, 'xform', st.plugin,
                  ...Object.entries(st.params).map(([k, v]) =>
                      `${k}=${v}`)]);

/* And with scenes, the knob moved by a chain of its own: a gen::steps a
   beat a step, each scene's energy on its first beat and held. */
const CURVE = 'energy';

/* An idea's notes moved into `key'; a drum's are which drum it is. */
const tunedTo = (idea, role, key) => role === 'drums' ? idea : {
    ...idea,
    stages: idea.stages.map(([c, p, params]) =>
        [c, p, Object.fromEntries(Object.entries(params).map(([k, v]) =>
            [k, inKey(v, key)]))]) };

/* A kept grid's notes as often as each was struck: its ladder is a pitch
   a row, the lowest row last, and a cell `x' a note struck. What a key
   is fitted to, so that a passing note counts for less than the tonic. */
function struck (stage)
{
    const row = (name) => (stage.params.find((q) => q.name === name)
        ?.valueText ?? '').replace(/"/g, '').trim();
    const notes = row('notes').split(/\s+/);
    const rows = row('cells').split('/');

    return rows.flatMap((cells, r) =>
        Array(cells.split('x').length - 1)
            .fill(notes[rows.length - 1 - r]))
        .filter((n) => n !== undefined).join(' ');
}

const shapeOfStages = (stages) => JSON.stringify(stages.map((s) =>
    [s.plugin, s.name, s.live]));

/* The pane. `piece' is a getter for the page's; `params' sends param
   edits, `vary' sets a chain's seed (null: the piece's) and resolves to
   whether it went in, `keep' freezes a chain, `rest' mutes one, `say'
   tells the page's status of a trouble and `tell' of what was done,
   `touch' adds stages behind a chain (TOUCH)
   and resolves to whether they went in; `toMirror' asks the mirror to
   draw a stage and `onGesture' sends a pointer on a picture, as the
   sequencer's do; `edit' makes several edits as one (tw_gen_ops) and
   `sound' puts an instrument on another graph, each resolving to whether
   it went in; `sounds' is the graphs by category (the page's catalog),
   and `newBand' starts a band over. */
export function createBand ({ piece, params, vary, keep, rest, say, tell,
                              touch, toMirror, onGesture, edit, sounds,
                              sound, newBand, seek, seekSection, level,
                              clock, knob, knobValue, phrase, keepPhrase,
                              play })
{
    /* The scene being played, while the piece plays, and the one last
       chosen, while it does not: the one a Rest is written into. */
    let playing = -1;
    let running = false;
    let transportNow = 0;
    let chosen = 0;

    /* Where the scene playing began and how long it lasts, as played. */
    let span = { start: 0, length: 0 };

    const sectionsOf = () => piece()?.sections ?? [];

    function scene ()
    {
        const all = sectionsOf();

        if (all.length === 0)
            return null;

        const i = running && playing >= 0 ? playing
                                          : Math.min(chosen, all.length - 1);

        return { index: i, ...all[i] };
    }

    /* A player's level in the scene, 1 where the scene does not name it. */
    const levelIn = (sc, p) => sc?.levels?.[p.name] ?? 1;

    const barsOf = (s) => (s.unit === 'beats' ? s.length / clock().meter
                                              : 0);
    const sizeOf = (s) => (s.unit === 'beats'
        ? `${+barsOf(s).toFixed(2)} bars` : `${+s.length.toFixed(1)} s`);

    /* A scene's index now, by its name: an edit since a button was drawn
       may have moved it. */
    const indexOf = (name) => sectionsOf().findIndex((s) => s.name === name);

    const nameOf = (p) => p.name.replace(/_/g, ' ');

    /* ---- the energy (ENERGY) ---- */

    /* The scenes, when each is a whole number of beats: what a curve a
       beat a step can follow. */
    function curveScenes ()
    {
        const all = sectionsOf();

        return all.length > 0 && all.every((s) => s.unit === 'beats' &&
                                                  Number.isInteger(s.length))
            ? all : null;
    }

    const curveChain = () => piece()?.chains?.find((c) =>
        c.name === CURVE && c.stages[0]?.plugin === 'steps') ?? null;

    /* Each scene's energy, by name, as the curve has it. */
    function sceneEnergies ()
    {
        const row = (curveChain()?.stages[0].params.find((q) =>
            q.name === 'values')?.valueText ?? '').replace(/"/g, '').trim()
            .split(/\s+/);
        const out = new Map();
        let at = 0;

        for (const sc of curveScenes() ?? [])
        {
            let i = Math.min(at, row.length - 1);

            while (i > 0 && row[i] === '_')
                i--;

            const v = Number(row[i]);

            out.set(sc.name, Number.isFinite(v) ? v : 0.5);
            at += sc.length;
        }

        return out;
    }

    const curveRow = (all, energies) => '"' + all.flatMap((sc) =>
        [String(+(energies.get(sc.name) ?? 0.5).toFixed(2)),
         ...Array(sc.length - 1).fill('_')]).join(' ') + '"';

    /* The playing or chosen scene's on a curve; else the knob's. */
    function energyNow ()
    {
        const sc = scene();

        if (sc !== null && curveChain() !== null)
            return sceneEnergies().get(sc.name) ?? 0.5;

        return knobValue(ENERGY) ?? 0.5;
    }

    /* What the energy needs and the piece has not got: the knob, the
       stages of each player the band plays -- not one that is played by
       hand, whose notes are yours -- and with scenes the curve, at `value'
       for the scene there is now and as written for the others; and a
       curve with no scenes left to follow, taken out. A chain of that
       name the band did not write is left alone, and the energy is the
       knob's. */
    function energyMissing (value)
    {
        const now = piece();
        const ops = [];
        const all = curveScenes();
        const clash = curveChain() === null &&
                      now.chains.some((c) => c.name === CURVE);

        if (!(now.knobs ?? []).some((k) => k.name === ENERGY) &&
            knobValue(ENERGY) === null)
            ops.push(['knob', ENERGY, String(value), '0', '1', 'Energy']);

        for (const p of playersOf(now))
            if (!p.input)
                ops.push(...energyOps(p.name, p.role === 'drums', p.stages));

        if (all !== null && curveChain() === null && !clash)
            ops.push(['knobchain', CURVE, ENERGY, 'src', 'gen', 'steps',
                      `values=${curveRow(all, new Map([[scene().name,
                                                        value]]))}`,
                      'period=1 beats']);
        else if (all === null && curveChain() !== null)
            ops.push(['unchain', CURVE]);

        return ops;
    }

    /* A curve made or changed while the piece plays, put in step with
       the scenes: a chain added mid-piece starts its row where the
       transport is, and one edited goes on with the row it had sent. */
    function realign ()
    {
        if (running)
            seek(transportNow);
    }

    /* Live as it moves; let go of, into the piece -- into the scene's
       place on the curve where there is one. A move while what is
       missing goes in is made once it is in. */
    let energyAdding = false;
    let energyNext = null;

    async function setEnergy (value, commit)
    {
        if (piece() === null)
            return;

        if (energyAdding)
        {
            energyNext = { value, commit: commit || energyNext?.commit };
            return;
        }

        const ops = energyMissing(value);

        if (ops.length > 0)
        {
            energyAdding = true;

            const went = await edit(ops);
            const next = energyNext;
            /* What was written with `value' in it need not be again. */
            const wrote = ops.some(([op]) => op === 'knob' ||
                                             op === 'knobchain');

            energyAdding = false;
            energyNext = null;

            if (!went)
            {
                draw();
                return;
            }

            realign();
            setEnergy(next?.value ?? value,
                      next?.commit ?? (commit && !wrote));
            return;
        }

        const all = curveScenes();

        knob(ENERGY, value, commit && curveChain() === null);

        if (commit && all !== null && curveChain() !== null)
        {
            const energies = sceneEnergies();

            energies.set(scene().name, value);

            if (await edit([['param', CURVE, '0', 'values',
                             curveRow(all, energies)]]))
                realign();
        }
    }

    /* The scenes `ops' leave, as curveScenes says them. */
    function scenesAfter (ops)
    {
        const meter = clock().meter;
        const lengthOf = (text) =>
        {
            const m = /^([\d.]+)\s*(bars|beats|b|s)$/.exec(text.trim());
            const n = Number(m?.[1]);

            return m === null ? { length: 0, unit: '' }
                : m[2] === 's' ? { length: n, unit: 's' }
                : { length: m[2] === 'bars' ? n * meter : n, unit: 'beats' };
        };
        let all = sectionsOf().map((s) => ({ name: s.name, length: s.length,
                                            unit: s.unit }));

        for (const [op, name, length, after] of ops)
            if (op === 'section')
                all.splice(after ? all.findIndex((s) => s.name === after) + 1
                                 : all.length, 0,
                           { name, ...lengthOf(length) });
            else if (op === 'sectionlength')
                all = all.map((s) => s.name === name
                    ? { name, ...lengthOf(length) } : s);
            else if (op === 'unsection')
                all = all.filter((s) => s.name !== name);

        return all.length > 0 && all.every((s) =>
            s.unit === 'beats' && Number.isInteger(s.length)) ? all : null;
    }

    /* A scene edit, with the curve made again for the scenes it leaves in
       the same edit, one undo: each scene's energy as it was, a new one
       `to' at that of `from'; or taken out with no scenes to follow. */
    async function sceneEdit (ops, from = null, to = null)
    {
        if (curveChain() === null)
            return edit(ops);

        const energies = sceneEnergies();
        const after = scenesAfter(ops);

        if (to !== null)
            energies.set(to, energies.get(from) ?? 0.5);

        const went = await edit([...ops, after === null
            ? ['unchain', CURVE]
            : ['param', CURVE, '0', 'values', curveRow(after, energies)]]);

        if (went)
            realign();

        return went;
    }

    function showEnergy ()
    {
        const box = $('bandenergy');
        const sc = scene();

        if (document.activeElement !== box && !energyAdding)
            box.value = String(energyNow());

        box.setAttribute('aria-valuetext',
                         `${Math.round(Number(box.value) * 100)}%`);
        box.title = sc !== null && curveScenes() !== null
            ? `How much is going on in ${sc.name}: calm to full, as ` +
              'written in the middle'
            : 'How much the whole band plays: calm to full, as written ' +
              'in the middle';
    }

    /* The control to have the keys after the next draw, by its `f': a
       player just added, before it has a card. */
    let focusNext = null;

    /* Per chain name, for the piece the band was last shown: the stages as
       they were then, which the middle of the pad is; where the pad is;
       the takes so far; whether it rests. */
    let state = new Map();
    let shown = null;

    /* Chains to rest once the piece next loads: a kept take's original. */
    const restNext = new Set();

    /* What the cards were drawn from: drawn again only when that changes,
       so a pad being dragged is not taken out from under the pointer. */
    let drawnFrom = '';

    /* How many pads are held, and whether a draw waits for them. */
    let held = 0;
    let drawLater = false;

    /* The pictures on the cards, each drawn by the mirror one frame at a
       time: { chain, stage, canvas, ctx, w, h, dpr, asked, drop }. */
    let pictures = [];
    let visible = false;
    let frameId = null;
    let lastFrame = 0;

    function picture (p)
    {
        const s = p.picture;
        const canvas = document.createElement('canvas');
        const pic = { chain: p.index, stage: s.live, canvas,
                      ctx: canvas.getContext('2d'), w: 0, h: 0, dpr: 0,
                      height: (w) => pictureHeight(s, w), asked: false };

        canvas.className = `bandpic ${s.plugin}`;
        canvas.style.height = `${pic.height(320)}px`;
        pic.drop = () => {};

        if (CONTROLS.has(s.plugin))
        {
            canvas.classList.add('control');
            pic.drop = gestureOn(canvas, (kind, where, button) =>
                onGesture({ chain: p.index, stage: s.live, chainName: p.name,
                            stageName: s.name, kind, ...where, w: pic.w,
                            h: pic.h, button }));
        }
        pictures.push(pic);
        return canvas;
    }

    /* Each picture asked for again once the last one is back, at the
       card's width: the element is the draw's size, so the pointer's
       coordinates are the picture's. */
    function frame (now)
    {
        frameId = null;

        if (!visible)
            return;

        /* Thirty a second at most: a card is not a canvas to scroll, and
           ten players at the screen's rate is a worker drawing nothing
           else. */
        if (now - lastFrame < 1000 / 30)
        {
            frameId = requestAnimationFrame(frame);
            return;
        }

        lastFrame = now;

        const dpr = globalThis.devicePixelRatio || 1;

        for (const pic of pictures)
        {
            if (pic.asked)
                continue;

            const w = Math.max(1, Math.round(pic.canvas.clientWidth));

            if (w !== pic.w || dpr !== pic.dpr)
            {
                pic.w = w;
                pic.h = pic.height(w);
                pic.dpr = dpr;
                pic.canvas.style.height = `${pic.h}px`;
                pic.canvas.width = Math.round(w * dpr);
                pic.canvas.height = Math.round(pic.h * dpr);
            }

            pic.asked = true;
            toMirror({ type: 'stagedraw', chain: pic.chain,
                       stage: pic.stage, w: pic.w, h: pic.h, dpr });
        }

        frameId = requestAnimationFrame(frame);
    }

    const PAD_EVERY_MS = 80;

    /* Another chain under the same name -- another piece, stages added
       or taken out -- is another player. */
    const shapeOf = (p) => shapeOfStages(p.stages);

    function stateOf (p)
    {
        if (state.get(p.name)?.shape !== shapeOf(p))
            state.set(p.name, { shape: shapeOf(p), base: p.stages, x: 0.5,
                                y: 0.5, takes: [p.seed], take: 0,
                                resting: false, sent: 0, said: new Map(),
                                timer: null });

        return state.get(p.name);
    }

    /* The stages TOUCH adds for an axis the pad has left the middle of
     * and `p' has nothing on, written in, and taken into the player's
     * state at once as the places they will have -- after its others -- so
     * the card is not drawn again under a drag when the piece comes back
     * with them. Resolves when they are in, or have been refused and taken
     * out of the state again; refused, they are asked for again only when
     * the pad is next taken hold of.
     */
    function ready (p, st)
    {
        const has = axesOf(st.base);
        const adds = [
            ...(has.busy || st.x === 0.5 ? [] : TOUCH.busy()),
            ...(has.wander || st.y === 0.5 ? [] : TOUCH.wander(p.role ===
                                                               'drums'))];

        if (adds.length === 0 || st.refused)
            return Promise.resolve();

        return behind(p, st, adds).then((went) =>
        {
            if (!went)
                st.refused = true;
        });
    }

    /* Stages `adds' written in after `p''s others, and taken into its
       state at once as the places they will have (ready). Resolves to
       whether they went in. */
    function behind (p, st, adds)
    {
        const taken = new Set(st.base.map((s) => s.name));

        for (const a of adds)
            for (let n = 2; taken.has(a.name); n++)
                a.name = a.name.replace(/\d*$/, String(n));

        let live = st.base.filter((s) => s.live >= 0).length;
        const was = { base: st.base, shape: st.shape };

        st.base = [...st.base, ...adds.map((a) => ({
            name: a.name, category: 'xform', plugin: a.plugin, live: live++,
            params: Object.entries(a.params).map(([name, valueText]) =>
                ({ name, valueText })) }))];
        st.shape = shapeOfStages(st.base);

        return touch(p, adds).then((went) =>
        {
            if (!went)
                Object.assign(st, was);

            return went;
        });
    }

    function pad (p, st)
    {
        const box = document.createElement('div');
        const dot = document.createElement('span');

        box.className = 'bandpad';
        box.dataset.f = `${p.name}:pad`;
        box.title = 'Across, or Left and Right: calm to busy. Up, or Up ' +
                    'and Down: steady to wandering. Home: the middle.';
        box.setAttribute('role', 'slider');
        box.setAttribute('aria-valuemin', '0');
        box.setAttribute('aria-valuemax', '10');
        box.setAttribute('aria-label', `${nameOf(p)}: calm to busy, steady ` +
                                       'to wandering');
        dot.className = 'bandpaddot';
        box.append(dot);

        for (const [cls, word] of [['l', 'calm'], ['r', 'busy'],
                                   ['b', 'steady'], ['t', 'wandering']])
        {
            const w = document.createElement('i');

            w.className = cls;
            w.textContent = word;
            box.append(w);
        }

        const place = () =>
        {
            dot.style.left = `${st.x * 100}%`;
            dot.style.top = `${(1 - st.y) * 100}%`;
            /* A slider has one value: across, the one an arrow along it
               moves; the text says both. */
            box.setAttribute('aria-valuenow', String(Math.round(st.x * 10)));
            box.setAttribute('aria-valuetext',
                             `busy ${Math.round(st.x * 10)} of 10, ` +
                             `wandering ${Math.round(st.y * 10)} of 10`);
        };

        /* Not before what the pad moves is in the piece. On the way, what
           has moved; where it ends, all of it, which the chain is
           composed again from (thcScheduler::recompose). */
        const send = async (last) =>
        {
            st.readying ??= ready(p, st).finally(() => st.readying = null);
            await st.readying;
            st.timer = null;
            st.sent = performance.now();

            const edits = padEdits(st.base, st.x, st.y,
                                   last ? null : st.said);

            for (const e of edits)
                st.said.set(`${e.stageName}:${e.row}`, e.text);

            params(p, edits, !last);
        };

        /* Near the middle is the middle: a pad dragged across by a hand is
           not an inch up or down, and an axis left alone stays as
           written. */
        const settle = (v) =>
        {
            const c = Math.min(1, Math.max(0, v));

            return Math.abs(c - 0.5) < 0.06 ? 0.5 : c;
        };

        const to = (x, y, last) =>
        {
            st.x = settle(x);
            st.y = settle(y);
            place();

            if (last)
            {
                clearTimeout(st.timer);
                st.timer = null;
                send(true);
            }
            else if (st.timer === null)
                st.timer = setTimeout(() => send(false), Math.max(0,
                    PAD_EVERY_MS - (performance.now() - st.sent)));
        };

        const move = (e) =>
        {
            const r = box.getBoundingClientRect();

            to((e.clientX - r.left) / r.width,
               1 - (e.clientY - r.top) / r.height);
        };

        /* The arrows, a tenth at a time; Home, back to as written. */
        const KEYS = { ArrowLeft: [-0.1, 0], ArrowRight: [0.1, 0],
                       ArrowDown: [0, -0.1], ArrowUp: [0, 0.1],
                       Home: null };

        box.tabIndex = 0;
        box.addEventListener('keydown', (e) =>
        {
            const k = KEYS[e.key];

            if (k === undefined)
                return;

            e.preventDefault();
            e.stopPropagation();
            st.refused = false;

            if (k === null)
                to(0.5, 0.5, true);
            else
                to(st.x + k[0], st.y + k[1], true);
        });

        box.addEventListener('pointerdown', (e) =>
        {
            box.setPointerCapture(e.pointerId);
            held++;
            st.refused = false;
            move(e);
        });
        box.addEventListener('pointermove', (e) =>
        {
            if (box.hasPointerCapture(e.pointerId))
                move(e);
        });
        box.addEventListener('lostpointercapture', () =>
        {
            held--;
            to(st.x, st.y, true);

            if (held === 0 && drawLater)
                draw();
        });

        place();
        return box;
    }

    /* `f' names the control, for its focus to come back to it when it is
       drawn again (draw): a player's under its chain's name, the scenes'
       under an `@', which no name has. */
    function button (text, title, on, f)
    {
        const b = document.createElement('button');

        if (f !== undefined)
            b.dataset.f = f;

        b.textContent = text;
        b.title = title;
        b.addEventListener('click', on);
        return b;
    }

    /* The band's own stage `own' (OWN) on a player, its param set to
     * `text': where it is, a param edit; where it is not, added behind
     * the player. A value set while it is on its way is sent once it is
     * in. Into the player's state at once, which is what the card shows:
     * a param edit is not read back into it. Resolves to whether it went
     * in.
     */
    function setOwn (p, st, own, text, passing = false)
    {
        const s = ownStage(st.base, own);

        st.owning ??= new Map();

        if (s !== undefined)
            st.base = st.base.map((b) => b !== s ? b : {
                ...s, params: [{ name: own.row, valueText: text }] });

        if (st.owning.has(own.name))
        {
            st.owning.set(own.name, text);
            return st.adding;
        }

        if (s !== undefined)
        {
            params(p, [{ stage: s.live, stageName: s.name, row: own.row,
                         text }], passing);
            return Promise.resolve(true);
        }

        st.owning.set(own.name, text);
        st.adding = behind(p, st, [{ name: own.name, plugin: own.plugin,
                                     params: { [own.row]: text } }])
            .then((went) =>
        {
            const wanted = st.owning.get(own.name);

            st.owning.delete(own.name);

            if (went && wanted !== text)
                setOwn(p, st, own, wanted);

            draw();
            return went;
        });
        return st.adding;
    }

    function octave (p, st, by)
    {
        const to = octavesOf(st.base) + by;

        if (Math.abs(to) > OCTAVES)
            return;

        setOwn(p, st, OWN.octave, String(12 * to));
        tell(`${nameOf(p)}: ${octaveWords(to)}`);
        draw();
    }

    /* A slider from silent to twice as loud, as written in the middle --
       sent as it moves, at most every PAD_EVERY_MS, and held like a pad,
       so a draw does not take it out from under the pointer. */
    function volume (p, st)
    {
        const box = document.createElement('label');
        const range = document.createElement('input');
        let timer = null;
        let holding = false;

        box.className = 'volume';
        box.append(Object.assign(document.createElement('span'),
                                 { textContent: 'vol' }), range);
        range.type = 'range';
        range.min = '0';
        range.max = String(VOLUME_MAX);
        range.step = '0.05';
        range.value = String(volumeOf(st.base));
        range.dataset.f = `${p.name}:volume`;
        range.setAttribute('aria-label', `${nameOf(p)}: volume`);

        const spoken = () => range.setAttribute('aria-valuetext',
            `${Math.round(Number(range.value) * 100)}%`);
        const send = (last) =>
        {
            clearTimeout(timer);
            timer = null;
            setOwn(p, st, OWN.volume, String(Number(range.value)), !last)
                .then((went) =>
            {
                /* Refused, the card is not drawn again: nothing it is
                   drawn from has changed. */
                if (!went)
                    range.value = String(volumeOf(st.base));
            });
        };
        const release = () =>
        {
            if (!holding)
                return;

            holding = false;

            if (--held === 0 && drawLater)
                draw();
        };

        spoken();
        range.addEventListener('pointerdown', () =>
        {
            if (!holding)
            {
                holding = true;
                held++;
            }
        });
        range.addEventListener('input', () =>
        {
            spoken();

            if (timer === null)
                timer = setTimeout(() => send(false), PAD_EVERY_MS);
        });
        range.addEventListener('change', () =>
        {
            send(true);
            release();
        });
        range.addEventListener('pointerup', release);
        range.addEventListener('pointercancel', release);
        return box;
    }

    /* What plays the player, as a menu of the graphs its role is offered
       from -- or, for a chain on a channel the piece does not fill, the
       channel, which is not the band's to change. */
    function soundMenu (p)
    {
        if (p.instrument === '')
        {
            const span = document.createElement('span');

            span.className = 'sound';
            span.textContent = p.sound;
            return span;
        }

        const menu = document.createElement('select');
        const wanted = SOUNDS[p.role] ?? [];
        const rank = (g) => wanted.includes(g.name)
            ? wanted.indexOf(g.name) : wanted.length;

        menu.className = 'sound';
        menu.dataset.f = `${p.name}:sound`;
        menu.title = 'What plays it';
        menu.setAttribute('aria-label', `What plays ${nameOf(p)}`);

        for (const g of [...sounds()].sort((a, b) => rank(a) - rank(b)))
        {
            if (!wanted.includes(g.name) &&
                !g.entries.some((e) => e.file === p.dsp))
                continue;

            const og = document.createElement('optgroup');

            og.label = g.name;
            og.append(...g.entries.map((e) => new Option(e.name, e.file)));
            menu.append(og);
        }

        if (![...menu.options].some((o) => o.value === p.dsp))
            menu.prepend(new Option(p.dsp || p.sound, p.dsp));

        /* Back to what plays it until the piece says otherwise: a card is
           drawn again when its graph changes, and a refusal leaves it. */
        menu.value = p.dsp;
        menu.addEventListener('change', () =>
        {
            const dsp = menu.value;
            const name = menu.selectedOptions[0]?.text ?? dsp;

            menu.value = p.dsp;
            sound(p, dsp).then((went) =>
                went && tell(`${nameOf(p)} plays ${name}`));
        });
        return menu;
    }

    /* In a piece with scenes, a rest is the player's level in the scene
       and is written into it, and Play puts back the level it had; without,
       it is the live mute. A live mute from before there were scenes shows
       as resting, and Play lifts it. */
    function restButton (p, st)
    {
        const sc = scene();
        const live = st.resting;
        const resting = live || (sc !== null && levelIn(sc, p) === 0);
        const where = sc === null ? '' : ` in ${sc.name}`;
        const b = button('Rest', resting
            ? `Bring it back in${where}` : `Leave it out${where}`, () =>
        {
            tell(`${nameOf(p)} ${resting ? 'plays' : 'rests'}${where}`);

            if (live || sc === null)
            {
                st.resting = !st.resting;
                rest(p, st.resting);
                draw();
                return;
            }

            const at = indexOf(sc.name);
            const was = levelIn(sc, p);

            st.levels ??= {};

            if (!resting)
                st.levels[sc.name] = was;

            level(at, p.index, resting ? st.levels[sc.name] ?? 1 : 0);
        }, `${p.name}:rest`);

        b.classList.add('rest');
        b.setAttribute('aria-pressed', String(resting));
        return b;
    }

    /* The scenes: the piece's sections, a button each, the one being
       played or chosen lit; chosen, the transport goes to it. + Scene
       copies the scene after it. The one lit can be named, sized and
       taken out. */
    function scenes ()
    {
        const box = $('bandscenes');
        const all = sectionsOf();
        const sc = scene();
        const energies = curveChain() === null ? null : sceneEnergies();
        const row = all.map((s, i) =>
        {
            const b = button(s.name, sizeOf(s), () =>
                {
                    chosen = i;
                    seekSection(i);
                    draw();
                }, `@scene=${s.name}`);

            b.className = 'scene';
            b.classList.toggle('on', sc !== null && sc.index === i);

            if (energies !== null)
            {
                const e = energies.get(s.name) ?? 0.5;

                b.style.setProperty('--energy', String(e));
                b.classList.add('energized');
                b.title += `, energy ${Math.round(e * 100)}%`;
            }

            if (sc !== null && sc.index === i)
                b.setAttribute('aria-current', 'true');

            b.append(' ', Object.assign(document.createElement('small'),
                { textContent: b.title }));
            return b;
        });
        const add = button('+ Scene', sc === null
            ? 'A first scene: the band as it is'
            : `Another scene after ${sc.name}, as ${sc.name} is`, () =>
        {
            const taken = new Set(all.map((s) => s.name));
            const length = sc === null ? '8 bars' : `${sc.length} ${sc.unit}`;
            const levels = Object.entries(sc?.levels ?? {})
                .map(([c, l]) => `${c}=${l}`);

            const name = freeName('scene', taken);

            chosen = sc === null ? 0 : indexOf(sc.name) + 1;
            sceneEdit([['section', name, length, sc?.name ?? '', ...levels]],
                      sc?.name ?? null, name)
                .then((went) => went && tell(`A scene called ${name}`));
        }, '@add');

        add.className = 'scene add';
        box.replaceChildren(...row, add);

        if (sc === null)
            return;

        const size = document.createElement('select');

        size.dataset.f = '@size';
        size.title = `How long ${sc.name} lasts`;
        size.setAttribute('aria-label', size.title);

        for (const bars of [1, 2, 4, 8, 16, 32])
            size.append(new Option(`${bars} bars`, String(bars)));

        const listed = [...size.options].some((o) =>
            Number(o.value) === barsOf(sc));

        if (!listed)
            size.prepend(new Option(sizeOf(sc), ''));

        size.value = listed ? String(barsOf(sc)) : '';
        /* The scene as it is when used, which an edit since this was
           drawn -- a rename -- may have changed. */
        const now = () => scene()?.name ?? sc.name;

        size.addEventListener('change', () =>
        {
            const name = now();

            if (size.value !== '')
                sceneEdit([['sectionlength', name, `${size.value} bars`]])
                    .then((went) => went &&
                          tell(`${name} lasts ${size.value} bars`));
        });

        const name = button('Rename', `Call ${sc.name} something else`,
                            () =>
        {
            const was = now();
            const to = prompt(`A name for ${was}:`, was)?.trim()
                .replace(/\s+/g, '_');

            if (to && to !== was)
                edit([['resection', was, to]]);
        }, '@rename');
        const out = button('×', `Take ${sc.name} out of the song`, () =>
        {
            const name = now();

            sceneEdit([['unsection', name]]).then((went) =>
                went && tell(`${name} is out of the song`));
        }, '@out');

        out.setAttribute('aria-label', out.title);
        box.append(size, name, out);
    }

    function card (p)
    {
        const st = stateOf(p);
        const el = document.createElement('div');
        const who = document.createElement('div');

        el.className = `player ${p.role}`;
        el.classList.toggle('resting', st.resting ||
                                       levelIn(scene(), p) === 0);
        el.dataset.chain = p.name;
        el.dataset.channel = String(p.channel);
        el.setAttribute('role', 'group');
        el.setAttribute('aria-label', nameOf(p));

        who.className = 'who';
        who.innerHTML = '<span class="role"></span><b></b>' +
                        '<span class="sound"></span><span class="idea"></span>';
        who.querySelector('.role').textContent = p.role;
        who.querySelector('b').textContent = nameOf(p);
        who.querySelector('.sound').replaceWith(soundMenu(p));
        who.querySelector('.idea').textContent = p.idea;

        const octs = document.createElement('div');

        octs.className = 'octave';

        if (p.role !== 'drums')
        {
            const n = octavesOf(st.base);
            const at = document.createElement('span');
            const up = button('▲', 'An octave up', () => octave(p, st, 1),
                              `${p.name}:up`);
            const down = button('▼', 'An octave down',
                                () => octave(p, st, -1), `${p.name}:down`);

            at.className = 'at';
            at.classList.toggle('moved', n !== 0);
            at.textContent = n === 0 ? 'oct' : `${n > 0 ? '+' : '−'}` +
                                               `${Math.abs(n)} oct`;
            at.title = `Played ${octaveWords(n)}`;
            /* Not `disabled', which would drop the focus of a key that
               pressed it there. */
            up.setAttribute('aria-disabled', String(n >= OCTAVES));
            down.setAttribute('aria-disabled', String(n <= -OCTAVES));
            octs.append(up, at, down);
        }

        for (const b of octs.querySelectorAll('button'))
            b.setAttribute('aria-label', b.title);

        const moves = document.createElement('div');
        const takes = document.createElement('div');
        const seed = () => Math.floor(Math.random() * 0x100000000);

        moves.className = 'moves';

        /* With nothing random in it, the first Vary puts TAKES behind it,
           and the take is had once they are in; As it was sets them to
           pass every note, and another Vary back. After the seed, whose
           edit loads the piece again over any param edit sent before
           it. */
        const drums = p.role === 'drums';
        const taking = () => ownStage(st.base, OWN.take);

        if (p.seeded === 'vary' || p.seeded === null)
            moves.append(button('Vary', 'Another take, from here on',
                                async () =>
            {
                const next = seed();

                if (p.seeded === null && taking() === undefined &&
                    !(await behind(p, st, [TAKES(drums)])))
                    return;

                if (!(await vary(p, next)))
                    return;

                if (taking() !== undefined)
                    params(p, takeEdits(taking(), drums, true));

                st.takes = st.takes.slice(0, st.take + 1);
                st.takes.push(next);
                st.take = st.takes.length - 1;
                tell(`${nameOf(p)}: take ${st.takes.length}`);
                draw();
            }, `${p.name}:vary`));
        else if (p.seeded === 'restart')
            moves.append(button('Restart', 'The board as it is written, ' +
                                           'from here on',
                                () => vary(p, p.seed === null ? seed()
                                                              : null),
                                `${p.name}:restart`));

        moves.append(
            button('Keep', 'Keep what it just played as notes you can ' +
                           'edit', () => keep(p), `${p.name}:keep`),
            restButton(p, st));

        takes.className = 'takes';
        takes.title = 'Takes so far: click one to go back to it';
        /* One take is no choice to offer. */
        ((p.seeded === 'vary' || taking() !== undefined) &&
         st.takes.length > 1 ? st.takes : [])
            .forEach((taken, i) =>
        {
            const t = button(String(i + 1), i === 0 ? 'As it was'
                                                    : `Take ${i + 1}`,
                             async () =>
            {
                if (!(await vary(p, taken)))
                    return;

                if (taking() !== undefined)
                    params(p, takeEdits(taking(), drums, i !== 0));

                st.take = i;
                tell(`${nameOf(p)}: ${t.title.toLowerCase()}`);
                draw();
            }, `${p.name}:take${i}`);

            t.classList.toggle('on', i === st.take);
            t.setAttribute('aria-label', t.title);
            t.setAttribute('aria-pressed', String(i === st.take));
            takes.append(t);
        });

        const out = button('×', `Take ${nameOf(p)} out of the band`,
            () => edit(removeOps(p.name, p.instrument, sectionsOf()))
                .then((went) => went &&
                      tell(`${nameOf(p)} is out of the band`)),
            `${p.name}:out`);

        out.className = 'out';
        out.setAttribute('aria-label', out.title);
        el.append(who, out, pad(p, st), octs, moves, volume(p, st), takes);

        if (p.picture !== null)
            el.append(picture(p));

        return el;
    }

    /* The players whose notes are a key's to move: every one but the
       drums. */
    const pitchedOf = (now) => new Set(playersOf(now)
        .filter((p) => p.role !== 'drums').map((p) => p.name));

    /* The bar's key, as the piece has it; a piece with no `key' scale of
       its own shows the one its notes fit best. */
    function showKey (now)
    {
        const key = keyOf(now, pitchedOf(now));

        $('bandroot').value = String(key.root);
        $('bandmood').value = key.mood;
        $('bandkey').title = key.written
            ? 'The key every player but the drums plays in'
            : 'The key this piece sounds in; choosing one writes it';
    }

    async function chooseKey ()
    {
        const now = piece();

        if (now === null)
            return;

        const pitched = pitchedOf(now);
        const from = keyOf(now, pitched);
        const to = { root: Number($('bandroot').value),
                     mood: $('bandmood').value };

        await edit(keyOps(now, pitched, from, to));

        /* What the piece is in, whether or not the change went in. */
        if (piece() !== null)
            showKey(piece());
    }

    /* ---- start from a phrase (PHRASE) ---- */

    async function startPhrase ()
    {
        if (!(await phrase()))
            return;

        /* The keys to the keys, not to a button they would type into. */
        $('bandphrasebox').hidden = false;
        document.activeElement?.blur();
        tell('Play a phrase on the keys');
    }

    /* The last bars played kept as the player `phrase', the piece's
       count and its live input taken out, and a band added in the key the
       phrase is in. */
    async function usePhrase ()
    {
        const bars = Number($('bandphrasebars').value);
        const before = new Set(playersOf(piece()).map((p) => p.name));
        const taker = playersOf(piece()).find((p) => p.name === 'phrase');

        if (taker === undefined || !(await keepPhrase(taker, bars)))
            return;

        const now = piece();
        const kept = playersOf(now).find((p) => !before.has(p.name));

        if (kept === undefined)
            return;

        const key = keyOf({
            chains: [{ name: kept.name, stages: [{ ...kept.stages[0],
                params: [{ name: 'notes',
                           valueText: `"${struck(kept.stages[0])}"` }] }] }],
        }, new Set([kept.name]));
        const taken = new Set(['phrase', 'keys']);
        const ops = [['unchain', 'phrase'], ['unchain', 'count'],
                     ['uninstrument', 'count'],
                     ['rechain', kept.name, 'phrase'],
                     ['scale', 'key', keyNotes(key)]];

        for (const [role, name] of PHRASE.ideas)
        {
            const idea = ideaNamed(role, name);
            const call = freeName(idea.call, taken);

            taken.add(call);
            ops.push(...addOps(tunedTo(idea, role, key), call));
        }

        /* Refused, the keys are heard again for another go: the freeze
           rested what takes them. */
        if (!(await edit(ops)))
        {
            const live = playersOf(piece()).find((p) => p.name === 'phrase');

            if (live !== undefined)
            {
                stateOf(live).resting = false;
                rest(live, false);
                draw();
            }
        }
        else
        {
            /* A load, where the edit could not be made live, stops it. */
            play();
            $('bandphrasebox').hidden = true;
            tell(`Your phrase, in ${ROOTS[key.root]} ${key.mood}, with a ` +
                 'band around it');
        }
    }

    /* An idea for each role, a button each: chosen, it is a player of
       its own, on its own instrument. */
    function gallery ()
    {
        const g = $('bandgallery');

        g.replaceChildren(...Object.entries(GALLERY).map(([role, ideas]) =>
        {
            const row = document.createElement('div');
            const head = document.createElement('span');

            row.className = `ideas ${role}`;
            row.setAttribute('role', 'group');
            row.setAttribute('aria-label', role);
            head.className = 'role';
            head.textContent = role;
            head.setAttribute('aria-hidden', 'true');
            row.append(head, ...ideas.map((idea) =>
            {
                const b = button(idea.name, idea.say, () =>
                {
                    const now = piece();

                    /* A player is an instrument, and an instrument a
                       channel: there are sixteen, less those the piece's
                       chains play on by number. */
                    if (new Set([...(now?.instruments ?? [])
                                     .map((i) => i.channel),
                                 ...(now?.sinks ?? [])]).size >= 16)
                    {
                        say('The band is full: sixteen players, one for ' +
                            'each channel. Take one out to add another.');
                        return;
                    }

                    const taken = new Set([
                        ...(now?.chains ?? []).map((c) => c.name),
                        ...(now?.instruments ?? []).map((i) => i.name)]);

                    /* In the band's key, which a chord player names and so
                       must be written if the piece has none of its own. */
                    const key = keyOf(now, pitchedOf(now));
                    const tuned = tunedTo(idea, role, key);

                    const name = freeName(idea.call, taken);

                    g.hidden = true;
                    $('bandadd').setAttribute('aria-expanded', 'false');
                    focusNext = `${name}:pad`;
                    edit([...(key.written ? [] : [['scale', 'key',
                                                    keyNotes(key)]]),
                          ...addOps(tuned, name),
                          ...((now?.knobs ?? []).some((k) =>
                                  k.name === ENERGY)
                              ? energyOps(name, role === 'drums') : [])])
                        .then((went) =>
                    {
                        if (went)
                            tell(`${name.replace(/_/g, ' ')} joins the ` +
                                 'band');
                        else
                        {
                            focusNext = null;
                            $('bandadd').focus();
                        }
                    });
                });

                b.dataset.idea = idea.name;
                return b;
            }));

            return row;
        }));
    }

    /* The control that has the keys, by its `f', and the card it is on. */
    function focused ()
    {
        const e = document.activeElement;

        if (e?.dataset?.f === undefined || !$('bandview').contains(e))
            return null;

        return { f: e.dataset.f,
                 at: [...$('players').children].indexOf(e.closest('.player'))
        };
    }

    /* The keys back where they were once the cards and scenes are drawn
       anew: on the same control, or -- it gone with its player or scene --
       on the next card's pad, the scene now chosen, or Add. */
    function refocus (was)
    {
        const find = (f) => $('bandview').querySelector(
            `[data-f="${CSS.escape(f)}"]`);

        if (focusNext !== null && find(focusNext) !== null)
        {
            find(focusNext).focus();
            focusNext = null;
            return;
        }

        /* A piece reloading has no cards yet: the draw after it does. */
        if (was === null || piece() === null ||
            document.activeElement?.isConnected &&
            document.activeElement !== document.body)
            return;

        /* Not the next scene's ×, one Enter from taking that out too. */
        const cards = $('players').children;
        const card = cards[Math.min(was.at, cards.length - 1)];
        const e = (was.f === '@out' ? null : find(was.f)) ??
            (was.f.startsWith('@')
                ? $('bandscenes').querySelector('.scene.on, .scene.add')
                : card?.querySelector('.bandpad') ?? $('bandadd'));

        e?.focus();
    }

    /* What had the keys when a draw took its control away and none came
       back: a piece reloading draws no cards in between. */
    let lost = null;

    function draw ()
    {
        const was = focused() ?? lost;

        drawNow();
        refocus(was);
        lost = document.activeElement === document.body ? was : null;
    }

    function drawNow ()
    {
        const now = piece();
        const list = $('players');

        if (now === null)
        {
            list.replaceChildren();
            return;
        }

        const players = playersOf(now);

        showKey(now);

        for (const p of players)
            if (restNext.delete(p.name))
                stateOf(p).resting = true;

        scenes();
        showEnergy();

        /* Again after every load or edit: a load makes the chains anew. */
        for (const p of players)
            if (stateOf(p).resting)
                rest(p, true);

        /* Not under a pad being dragged: what the drag itself changed --
           the stages behind a player, a Vary they make worth offering --
           is drawn when it is let go. */
        drawLater = held > 0;

        if (drawLater)
            return;

        const from = JSON.stringify(players.map((p) =>
        {
            const st = stateOf(p);

            return [p.name, p.index, st.shape, p.role, p.sound, p.dsp,
                    p.idea, st.takes, st.take, st.resting, p.seeded,
                    octavesOf(st.base),
                    levelIn(scene(), p),
                    p.picture?.live, p.picture?.params];
        }));

        /* A scene moved on with every level as it was: the cards stay,
           and only their Rest buttons say which scene they are for. */
        if (from === drawnFrom && list.childElementCount > 0)
            for (const p of players)
                list.querySelector(
                    `.player[data-chain="${CSS.escape(p.name)}"] .rest`)
                    ?.replaceWith(restButton(p, stateOf(p)));

        if (from !== drawnFrom || list.childElementCount === 0)
        {
            for (const pic of pictures)
                pic.drop();

            pictures = [];
            held = 0;
            list.replaceChildren(...players.map(card));
        }

        drawnFrom = from;
        $('bandhint').textContent = players.length === 0
            ? 'This piece has no players: nothing in it plays notes.'
            : 'Drag a pad, or use its arrow keys: across is calm to busy, ' +
              'up is steady to wandering.';
    }

    /* How far the scene playing is through, as its button's fill. */
    function progress (now)
    {
        const b = $('bandscenes').querySelector('.scene.on');

        if (b === null)
            return;

        const done = running && span.length > 0
            ? Math.min(1, Math.max(0, (now - span.start) / span.length)) : 0;

        b.style.setProperty('--done', `${(done * 100).toFixed(1)}%`);
    }

    /* Each card lit as its notes sound, by channel, which a player
       resting may share with one that is not. A note after `now' is the
       run before a seek back, posted after it. */
    const LIT_MS = 120;
    const still = matchMedia('(prefers-reduced-motion: reduce)');

    function heard (events, now)
    {
        if (!visible || still.matches)
            return;

        const channels = new Set(events.filter((e) => e.kind === 'N' &&
                                                      e.at <= now)
            .map((e) => e.channel));

        for (const channel of channels)
            for (const el of $('players').querySelectorAll(
                `.player[data-channel="${channel}"]:not(.resting)`))
            {
                el.classList.add('sounding');
                clearTimeout(el.unlit);
                el.unlit = setTimeout(() => el.classList.remove('sounding'),
                                      LIT_MS);
            }
    }

    return {
        /* Notes as the tape brings them, to light the cards that play
           them. */
        heard,

        /* A piece to show: another one starts the band afresh. */
        follow (name)
        {
            if (name !== shown)
            {
                state = new Map();
                shown = name;
            }

            draw();
        },

        /* The piece changed under it -- an edit, an undo: the same players,
           drawn again from what the piece now says. */
        draw,

        restOnLoad: (name) => restNext.add(name),

        /* The section the transport is in, from each tape: the cards and
           the scenes drawn again when it moves on. */
        at (section, isRunning, now, start = 0, length = 0)
        {
            transportNow = now;
            span = { start, length };

            if (section === playing && isRunning === running)
            {
                progress(now);
                return;
            }

            playing = section;
            running = isRunning;

            if (running && playing >= 0)
                chosen = playing;

            if ($('bandscenes').offsetParent === null)
                return;

            if (running && sectionsOf()[playing] !== undefined)
                tell(`Playing ${sectionsOf()[playing].name}`);

            draw();
            progress(now);
        },

        /* The bar above the cards: New, and the gallery of ideas. */
        bar ()
        {
            $('bandroot').replaceChildren(...ROOTS.map((r, i) =>
                new Option(r, String(i))));
            $('bandmood').replaceChildren(...MOODS.map(([m]) =>
                new Option(m, m)));
            $('bandroot').addEventListener('change', chooseKey);
            $('bandmood').addEventListener('change', chooseKey);
            $('bandnew').addEventListener('click', () => newBand());
            $('bandphrase').addEventListener('click', startPhrase);
            $('bandphraseuse').addEventListener('click', usePhrase);
            $('bandphrasecancel').addEventListener('click', () =>
            {
                $('bandphrasebox').hidden = true;
                $('bandphrase').focus();
            });
            $('bandenergy').addEventListener('input', () =>
            {
                const box = $('bandenergy');

                box.setAttribute('aria-valuetext',
                                 `${Math.round(Number(box.value) * 100)}%`);
                setEnergy(Number(box.value), false);
            });
            $('bandenergy').addEventListener('change', () =>
                setEnergy(Number($('bandenergy').value), true));
            /* A click elsewhere puts the keys where it meant them. */
            document.addEventListener('pointerdown', () => { lost = null; },
                                      true);
            $('bandadd').setAttribute('aria-expanded', 'false');
            $('bandadd').setAttribute('aria-controls', 'bandgallery');
            $('bandadd').addEventListener('click', () =>
            {
                const g = $('bandgallery');

                g.hidden = !g.hidden;
                $('bandadd').setAttribute('aria-expanded', String(!g.hidden));

                if (!g.hidden)
                    gallery();
            });
            const shut = (e) =>
            {
                if (e.key !== 'Escape' || $('bandgallery').hidden)
                    return;

                /* The page's Escape would take the keys off Add too. */
                e.preventDefault();
                $('bandgallery').hidden = true;
                $('bandadd').setAttribute('aria-expanded', 'false');
                $('bandadd').focus();
            };

            $('bandadd').addEventListener('keydown', shut);
            $('bandgallery').addEventListener('keydown', shut);
        },

        /* Another document, whatever the menu says: every player anew. */
        forget: () => state.clear(),

        /* Asking for pictures only while somebody can see them. */
        show (on)
        {
            visible = on;

            if (on && frameId === null)
                frameId = requestAnimationFrame(frame);
            else if (!on && frameId !== null)
            {
                cancelAnimationFrame(frameId);
                frameId = null;

                for (const pic of pictures)
                    pic.asked = false;
            }
        },

        /* A picture the mirror drew; false for anything else. */
        fromMirror (m)
        {
            if (m.type !== 'stagedraw')
                return false;

            const pic = pictures.find((q) => q.asked && q.chain === m.chain &&
                                             q.stage === m.stage);

            if (pic === undefined)
                return false;

            pic.asked = false;

            if (m.ops !== null && m.w === pic.w && m.h === pic.h)
                replay(pic.ctx, m.ops, m.strings, m.surfaces,
                       { width: m.w, height: m.h, dpr: pic.dpr });

            return true;
        },

        /* Where a player's pad is, for a harness. */
        padOf: (name) => state.get(name) ?? null,
    };
}
