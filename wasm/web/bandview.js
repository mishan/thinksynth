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

import { IDEAS as GALLERY, SOUNDS, addOps, freeName, removeOps }
    from './bandideas.js';
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
        default: return RANDOM.has(s.plugin);
    }
}

/* What another seed does to a player: 'vary' another take, 'restart' the
   same board from the top, or nothing. */
export function seededAs (stages)
{
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
        case 'euclid': return 90;
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

/* Every note name in a quoted list moved `by' octaves, unquoted -- what a
   param edit is written as; null when one would leave 0..8, or there is
   no quoted list (a scale's name, say). */
export function shiftOctave (valueText, by)
{
    if (!/^"[^"]*"$/.test(valueText.trim()))
        return null;

    let out = true;
    const text = valueText.replace(/\b([A-G][#b]?)(-?\d)\b/g, (_, n, o) =>
    {
        const to = Number(o) + by;

        if (to < 0 || to > 8)
            out = false;

        return n + to;
    });

    return out && text !== valueText ? text.trim().slice(1, -1) : null;
}

/* The players in a piece (worklet.js, piece: `chains' and `instruments'):
   every chain with a note sink, with its role, the instrument it plays
   and its idea. */
export function playersOf (piece)
{
    const dspOf = new Map((piece.instruments ?? []).map((i) => [i.name,
                                                                i.dsp]));

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
            : IDEAS[`gen::${gens[0].plugin}`] ?? gens[0].plugin;

        const picture = PICTURES.map((n) => c.stages.find(
            (s) => s.plugin === n && s.live >= 0)).find(Boolean) ?? null;

        return [{ index, name: c.name, role, sound, idea, stages: c.stages,
                  seed: c.hasSeed ? c.seed : null, picture,
                  seeded: seededAs(c.stages), instrument: sink.instrument,
                  dsp }];
    });
}

/* The param edits the pad at (x, y) makes from `base', the stages as they
   were: [{ stage, stageName, row, text }] for every param that moves. */
export function padEdits (stages, x, y)
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

                const text = spell(Math.max(0, f(n.num, t, nums)), n.unit,
                                   f.whole === true);

                /* As written stays as written, not respelled. */
                if (numberOf(text)?.num !== n.num)
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

const shapeOfStages = (stages) => JSON.stringify(stages.map((s) =>
    [s.plugin, s.name, s.live]));

/* The pane. `piece' is a getter for the page's; `params' sends param
   edits, `vary' sets a chain's seed (null: the piece's) and resolves to
   whether it went in, `keep' freezes a chain, `rest' mutes one, `say'
   tells the page's status, `touch' adds stages behind a chain (TOUCH)
   and resolves to whether they went in; `toMirror' asks the mirror to
   draw a stage and `onGesture' sends a pointer on a picture, as the
   sequencer's do; `edit' makes several edits as one (tw_gen_ops),
   `sounds' is the graphs by category (the page's catalog), `sound' puts
   an instrument on another graph, and `newBand' starts a band over. */
export function createBand ({ piece, params, vary, keep, rest, say,
                              touch, toMirror, onGesture, edit, sounds,
                              sound, newBand, seek, level, clock })
{
    /* The scene being played, while the piece plays, and the one last
       chosen, while it does not: the one a Rest is written into. */
    let playing = -1;
    let running = false;
    let chosen = 0;

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

    /* Where a scene starts and how long it is, at the tempo the transport
       has now: a length in beats is seconds only at a tempo. */
    const startOf = (s) => s.before + s.beatsBefore * 60 / clock().tempo;
    const barsOf = (s) => (s.unit === 'beats' ? s.length / clock().meter
                                              : 0);
    const sizeOf = (s) => (s.unit === 'beats'
        ? `${+barsOf(s).toFixed(2)} bars` : `${+s.length.toFixed(1)} s`);

    /* A scene's index now, by its name: an edit since a button was drawn
       may have moved it. */
    const indexOf = (name) => sectionsOf().findIndex((s) => s.name === name);

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

        canvas.className = 'bandpic';
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
                                resting: false, sent: 0, timer: null });

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
                Object.assign(st, was, { refused: true });
        });
    }

    function pad (p, st)
    {
        const box = document.createElement('div');
        const dot = document.createElement('span');

        box.className = 'bandpad';
        box.title = 'Across: calm to busy. Up: steady to wandering.';
        box.setAttribute('role', 'slider');
        box.setAttribute('aria-label', `${p.name}: calm to busy, steady to ` +
                                       'wandering');
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
            box.setAttribute('aria-valuetext',
                             `busy ${st.x.toFixed(1)}, wandering ` +
                             st.y.toFixed(1));
        };

        /* Not before what the pad moves is in the piece. */
        const send = async () =>
        {
            st.readying ??= ready(p, st).finally(() => st.readying = null);
            await st.readying;
            st.timer = null;
            st.sent = performance.now();
            params(p, padEdits(st.base, st.x, st.y));
        };

        /* Near the middle is the middle: a pad dragged across by a hand is
           not an inch up or down, and an axis left alone stays as
           written. */
        const settle = (v) =>
        {
            const c = Math.min(1, Math.max(0, v));

            return Math.abs(c - 0.5) < 0.06 ? 0.5 : c;
        };

        const to = (x, y) =>
        {
            st.x = settle(x);
            st.y = settle(y);
            place();

            if (st.timer === null)
                st.timer = setTimeout(send, Math.max(0, PAD_EVERY_MS -
                    (performance.now() - st.sent)));
        };

        const move = (e) =>
        {
            const r = box.getBoundingClientRect();

            to((e.clientX - r.left) / r.width,
               1 - (e.clientY - r.top) / r.height);
        };

        /* The arrows, a tenth at a time. */
        const KEYS = { ArrowLeft: [-0.1, 0], ArrowRight: [0.1, 0],
                       ArrowDown: [0, -0.1], ArrowUp: [0, 0.1] };

        box.tabIndex = 0;
        box.addEventListener('keydown', (e) =>
        {
            const k = KEYS[e.key];

            if (k === undefined)
                return;

            e.preventDefault();
            e.stopPropagation();
            st.refused = false;
            to(st.x + k[0], st.y + k[1]);
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

            if (held === 0 && drawLater)
                draw();
        });

        place();
        return box;
    }

    function button (text, title, on)
    {
        const b = document.createElement('button');

        b.textContent = text;
        b.title = title;
        b.addEventListener('click', on);
        return b;
    }

    /* From the stages as the piece has them now (piece().chains, read
       again after every param edit). */
    function octave (p, by)
    {
        const edits = [];
        const now = piece()?.chains?.[p.index]?.stages ?? p.stages;

        for (const s of now)
            for (const row of ['notes', 'fill'])
            {
                const q = s.params.find((v) => v.name === row);
                const text = q === undefined || s.live < 0
                    ? null : shiftOctave(q.valueText, by);

                if (text !== null)
                    edits.push({ stage: s.live, stageName: s.name, row,
                                 text });
            }

        if (edits.length === 0)
            say(`${p.name.replace(/_/g, ' ')} plays a scale by its name, ` +
                'or would leave the keyboard; its octave stays.');
        else
            params(p, edits);
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
        menu.title = 'What plays it';
        menu.setAttribute('aria-label',
                          `What plays ${p.name.replace(/_/g, ' ')}`);

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

            menu.value = p.dsp;
            sound(p, dsp);
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
        });

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
        const row = all.map((s, i) =>
        {
            const b = button(s.name, sizeOf(s), () =>
                {
                    chosen = i;
                    seek(startOf(s));
                    draw();
                });

            b.className = 'scene';
            b.classList.toggle('on', sc !== null && sc.index === i);

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

            chosen = sc === null ? 0 : indexOf(sc.name) + 1;
            edit([['section', freeName('scene', taken), length,
                   sc?.name ?? '', ...levels]]);
        });

        add.className = 'scene add';
        box.replaceChildren(...row, add);

        if (sc === null)
            return;

        const size = document.createElement('select');

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
            if (size.value !== '')
                edit([['sectionlength', now(), `${size.value} bars`]]);
        });

        const name = button('Rename', `Call ${sc.name} something else`,
                            () =>
        {
            const was = now();
            const to = prompt(`A name for ${was}:`, was)?.trim()
                .replace(/\s+/g, '_');

            if (to && to !== was)
                edit([['resection', was, to]]);
        });
        const out = button('×', `Take ${sc.name} out of the song`, () =>
            edit([['unsection', now()]]));

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
        el.setAttribute('role', 'group');
        el.setAttribute('aria-label', p.name.replace(/_/g, ' '));

        who.className = 'who';
        who.innerHTML = '<span class="role"></span><b></b>' +
                        '<span class="sound"></span><span class="idea"></span>';
        who.querySelector('.role').textContent = p.role;
        who.querySelector('b').textContent = p.name.replace(/_/g, ' ');
        who.querySelector('.sound').replaceWith(soundMenu(p));
        who.querySelector('.idea').textContent = p.idea;

        const octs = document.createElement('div');

        octs.className = 'octave';

        if (p.role !== 'drums')
            octs.append(button('▲', 'An octave up', () => octave(p, 1)),
                        button('▼', 'An octave down',
                               () => octave(p, -1)));

        for (const b of octs.children)
            b.setAttribute('aria-label', b.title);

        const moves = document.createElement('div');
        const takes = document.createElement('div');
        const seed = () => Math.floor(Math.random() * 0x100000000);

        moves.className = 'moves';

        if (p.seeded === 'vary')
            moves.append(button('Vary', 'Another take, from here on',
                                async () =>
            {
                const next = seed();

                if (!(await vary(p, next)))
                    return;

                st.takes = st.takes.slice(0, st.take + 1);
                st.takes.push(next);
                st.take = st.takes.length - 1;
                draw();
            }));
        else if (p.seeded === 'restart')
            moves.append(button('Restart', 'The board as it is written, ' +
                                           'from here on',
                                () => vary(p, p.seed === null ? seed()
                                                              : null)));

        moves.append(
            button('Keep', 'Keep what it just played as notes you can ' +
                           'edit', () => keep(p)),
            restButton(p, st));

        takes.className = 'takes';
        takes.title = 'Takes so far: click one to go back to it';
        /* One take is no choice to offer. */
        (p.seeded === 'vary' && st.takes.length > 1 ? st.takes : [])
            .forEach((taken, i) =>
        {
            const t = button(String(i + 1), i === 0 ? 'As it was'
                                                    : `Take ${i + 1}`,
                             async () =>
            {
                if (!(await vary(p, taken)))
                    return;

                st.take = i;
                draw();
            });

            t.classList.toggle('on', i === st.take);
            t.setAttribute('aria-label', t.title);
            t.setAttribute('aria-pressed', String(i === st.take));
            takes.append(t);
        });

        const out = button('×', `Take ${p.name.replace(/_/g, ' ')} out of ` +
                                'the band', () =>
            edit(removeOps(p.name, p.instrument, sectionsOf())));

        out.className = 'out';
        out.setAttribute('aria-label', out.title);
        el.append(who, out, pad(p, st), octs, moves, takes);

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
            head.className = 'role';
            head.textContent = role;
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
                    const tuned = role === 'drums' ? idea : {
                        ...idea,
                        stages: idea.stages.map(([c, p, params]) =>
                            [c, p, Object.fromEntries(
                                Object.entries(params).map(([k, v]) =>
                                    [k, inKey(v, key)]))]) };

                    g.hidden = true;
                    edit([...(key.written ? [] : [['scale', 'key',
                                                    keyNotes(key)]]),
                          ...addOps(tuned, freeName(idea.call, taken))]);
                });

                b.dataset.idea = idea.name;
                return b;
            }));

            return row;
        }));
    }

    function draw ()
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
            /* A pad that had the keys has them again on its new card. */
            const keys = document.activeElement?.classList
                .contains('bandpad')
                ? document.activeElement.closest('.player')?.dataset.chain
                : undefined;

            for (const pic of pictures)
                pic.drop();

            pictures = [];
            held = 0;
            list.replaceChildren(...players.map(card));

            if (keys !== undefined)
                list.querySelector(
                    `.player[data-chain="${CSS.escape(keys)}"] .bandpad`)
                    ?.focus();
        }

        drawnFrom = from;
        $('bandhint').textContent = players.length === 0
            ? 'This piece has no players: nothing in it plays notes.'
            : 'Press Play to hear the band. Drag a pad, or use the arrow ' +
              'keys on it: across is calm to busy, up is steady to ' +
              'wandering. Vary is another take; Keep makes the take notes.';
    }

    return {
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
        at (section, isRunning)
        {
            if (section === playing && isRunning === running)
                return;

            playing = section;
            running = isRunning;

            if (running && playing >= 0)
                chosen = playing;

            if ($('bandscenes').offsetParent !== null)
                draw();
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
            $('bandadd').addEventListener('click', () =>
            {
                const g = $('bandgallery');

                g.hidden = !g.hidden;

                if (!g.hidden)
                    gallery();
            });
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
