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
 * The pad moves params the chain already has. RECIPES says which, per
 * plugin, and how far: the middle of the pad is the piece as it was when
 * the band first showed it, the corners as far as each param sensibly
 * goes. A chain with nothing on an axis shows the axis as not there.
 */

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

/* By whole words of a chain's, instrument's and graph's names; chords
   before bass, so a `pad_low' is a pad. */
const ROLES = [
    ['drums', /^(kick|snare|hats?|clap|toms?|drums?|perc\w*|rim|cymbal|ride|crash|tamb\w*|shaker|conga|bongo|cowbell|kit)$/],
    ['chords', /^(chords?|pads?|strings?|organ|choir|comp|section)$/],
    ['bass', /^(\w*bass|sub)$/],
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
        const words = `${c.name} ${sound} ${dsp}`.toLowerCase()
            .split(/[^a-z]+/);
        let role = ROLES.find(([, re]) => words.some((w) => re.test(w)))
            ?.[0];

        if (role === undefined)
            role = plugins.some((p) => /progression|harmonize/.test(p))
                ? 'chords'
                : plugins.some((p) => /life|::ca$|cloud|eno_line/.test(p))
                    ? 'texture' : 'melody';

        const idea = gens.length === 0
            ? (c.input ? 'what you play' : 'what comes in')
            : IDEAS[`gen::${gens[0].plugin}`] ?? gens[0].plugin;

        return [{ index, name: c.name, role, sound, idea, stages: c.stages,
                  seed: c.hasSeed ? c.seed : null }];
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

/* The pane. `synth' and `piece' are getters for the page's; `params'
   sends param edits, `vary' sets a chain's seed (null: the piece's),
   `keep' freezes a chain, `rest' mutes one. */
export function createBand ({ piece, params, vary, keep, rest, say })
{
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

    const PAD_EVERY_MS = 80;

    /* Another chain under the same name -- another piece, stages added
       or taken out -- is another player. */
    const shapeOf = (p) => JSON.stringify(p.stages.map((s) =>
        [s.plugin, s.name, s.live]));

    function stateOf (p)
    {
        if (state.get(p.name)?.shape !== shapeOf(p))
            state.set(p.name, { shape: shapeOf(p), base: p.stages, x: 0.5,
                                y: 0.5, takes: [p.seed], take: 0,
                                resting: false, sent: 0, timer: null });

        return state.get(p.name);
    }

    function pad (p, st, axes)
    {
        const box = document.createElement('div');
        const dot = document.createElement('span');

        box.className = 'bandpad';
        box.classList.toggle('nobusy', !axes.busy);
        box.classList.toggle('nowander', !axes.wander);
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

        const send = () =>
        {
            st.timer = null;
            st.sent = performance.now();
            params(p, padEdits(st.base, axes.busy ? st.x : 0.5,
                               axes.wander ? st.y : 0.5));
        };

        const to = (x, y) =>
        {
            st.x = Math.min(1, Math.max(0, x));
            st.y = Math.min(1, Math.max(0, y));
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
            to(st.x + k[0], st.y + k[1]);
        });

        box.addEventListener('pointerdown', (e) =>
        {
            box.setPointerCapture(e.pointerId);
            move(e);
        });
        box.addEventListener('pointermove', (e) =>
        {
            if (box.hasPointerCapture(e.pointerId))
                move(e);
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

    function card (p)
    {
        const st = stateOf(p);
        const axes = axesOf(st.base);
        const el = document.createElement('div');
        const who = document.createElement('div');

        el.className = `player ${p.role}`;
        el.classList.toggle('resting', st.resting);
        el.dataset.chain = p.name;

        who.className = 'who';
        who.innerHTML = '<span class="role"></span><b></b>' +
                        '<span class="sound"></span><span class="idea"></span>';
        who.querySelector('.role').textContent = p.role;
        who.querySelector('b').textContent = p.name.replace(/_/g, ' ');
        who.querySelector('.sound').textContent =
            p.sound === p.name ? '' : p.sound;
        who.querySelector('.idea').textContent = p.idea;

        const octs = document.createElement('div');

        octs.className = 'octave';

        if (p.role !== 'drums')
            octs.append(button('▲', 'An octave up', () => octave(p, 1)),
                        button('▼', 'An octave down',
                               () => octave(p, -1)));

        const moves = document.createElement('div');
        const takes = document.createElement('div');

        moves.className = 'moves';
        moves.append(
            button('Vary', 'Another take, from here on', async () =>
            {
                const seed = Math.floor(Math.random() * 0x100000000);

                if (!(await vary(p, seed)))
                    return;

                st.takes = st.takes.slice(0, st.take + 1);
                st.takes.push(seed);
                st.take = st.takes.length - 1;
                draw();
            }),
            button('Keep', 'Keep what it just played as notes you can ' +
                           'edit', () => keep(p)),
            button(st.resting ? 'Play' : 'Rest', st.resting
                ? 'Bring it back in' : 'Leave it out', () =>
            {
                st.resting = !st.resting;
                rest(p, st.resting);
                draw();
            }));

        takes.className = 'takes';
        takes.title = 'Takes so far: click one to go back to it';
        st.takes.forEach((seed, i) =>
        {
            const t = button(String(i + 1), i === 0 ? 'As it was'
                                                    : `Take ${i + 1}`,
                             async () =>
            {
                if (!(await vary(p, seed)))
                    return;

                st.take = i;
                draw();
            });

            t.classList.toggle('on', i === st.take);
            takes.append(t);
        });

        el.append(who, pad(p, st, axes), octs, moves, takes);
        return el;
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

        for (const p of players)
            if (restNext.delete(p.name))
                stateOf(p).resting = true;

        const from = JSON.stringify(players.map((p) =>
        {
            const st = stateOf(p);

            return [p.name, p.index, st.shape, p.role, p.sound, p.idea,
                    st.takes, st.take, st.resting];
        }));

        if (from !== drawnFrom || list.childElementCount === 0)
            list.replaceChildren(...players.map(card));

        drawnFrom = from;

        /* Again after every load or edit: a load makes the chains anew. */
        for (const p of players)
            if (stateOf(p).resting)
                rest(p, true);
        $('bandhint').textContent = players.length === 0
            ? 'This piece has no players: nothing in it plays notes.'
            : 'Drag a pad: across is calm to busy, up is steady to ' +
              'wandering. Vary is another take; Keep makes the take notes.';
    }

    return {
        /* A piece to show: another one starts the band afresh. */
        show (name)
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

        /* Another document, whatever the menu says: every player anew. */
        forget: () => state.clear(),

        /* Where a player's pad is, for a harness. */
        padOf: (name) => state.get(name) ?? null,
    };
}
