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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * roomundocheck.mjs -- a room's Undo (roomundo.js), over random runs.
 *
 *   node wasm/web/roomundocheck.mjs [BUILD_DIR] [SEED] [RUNS]
 *
 * Two documents synced as a room's are, A's with the room's history over
 * amb01.dsp, and the module's own parser saying what parses. Each run is a
 * seeded mix of A's graph edits (a node of A's added or taken out), A's
 * typing, A's undos and redos, and B's edits (a node of B's own, fed from
 * one of A's or not, added or taken out). After every step both texts are
 * one text and it parses, a refused undo or redo having changed nothing;
 * and so through B taking out all it added and A undoing until there is
 * nothing left to undo, which a refused step asked for again cannot stop.
 *
 * Not that A ends where it began. A write is the shortest splice between
 * two texts, and where they repeat -- every node ends in `};' -- what it
 * takes out can be some of the other page's characters and what it keeps
 * some of this one's: the text is the same, whose characters are which is
 * not, and an Undo takes back only its own. Such an Undo is refused rather
 * than leaving a text that does not parse.
 *
 * A failing run prints its seed.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import * as Y from 'yjs';

import { files, putFile, readFile, spliceFile } from './doc.js';
import { GRAPH, roomHistory, undoFor } from './roomundo.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ??
                           path.join(here, '..', '..', 'build-web'));
const seed0 = Number(process.argv[3] ?? 1);
const RUNS = Number(process.argv[4] ?? 200);

if (!Number.isSafeInteger(seed0) || !Number.isSafeInteger(RUNS) || RUNS < 1)
{
    process.stderr.write('usage: roomundocheck.mjs [BUILD_DIR] [SEED] [RUNS]\n');
    process.exit(2);
}
const STEPS = 40;
const NAME = 'amb01.dsp';

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);
const M = await createThinkWeb({ print: () => {}, printErr: () => {} });

M._tw_catalog_take();

const parses = (text) =>
    M.ccall('tw_graph_parses', 'number', ['string'], [text]) === 1;
const start = fs.readFileSync(path.join(build, 'dsp', NAME), 'utf8');

/* mulberry32 */
const random = (seed) => () =>
{
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
};

/* The code view's origin, as yCollab's is to the manager. */
const TYPING = { typing: true };

function run (seed)
{
    const rnd = random(seed);
    const pick = (list) => list[Math.floor(rnd() * list.length)];
    const a = new Y.Doc();
    const b = new Y.Doc();

    /* As the provider applies them: with an origin nobody tracks. */
    a.on('update', (u, origin) => origin !== 'b' && Y.applyUpdate(b, u, 'a'));
    b.on('update', (u, origin) => origin !== 'a' && Y.applyUpdate(a, u, 'b'));
    putFile(a, NAME, start);

    const history = roomHistory(a, parses);

    undoFor(a, NAME).addTrackedOrigin(TYPING);

    let made = 0;
    const node = (who, n, from) =>
        `node ${who}${n} osc::simple {\n` +
        (from === null ? '' : `    freq = ${from}->out;\n`) + '};\n';
    const nodes = (text, who) =>
        [...text.matchAll(new RegExp(`^node (${who}\\d+) `, 'gm'))]
            .map((m) => m[1]);
    const fedFrom = (text, n) => text.includes(`= ${n}->out`);
    const remove = (text, n) =>
        text.replace(new RegExp(`node ${n} osc::simple \\{\\n` +
                                '(    freq = \\w+->out;\\n)?\\};\\n'), '');
    const trail = [];

    const steps = {
        graphAdd ()
        {
            const n = `a${made++}`;

            history.step(NAME, `add ${n}`, null, () =>
                spliceFile(a, NAME, readFile(a, NAME) + node('a', n.slice(1),
                                                             null), GRAPH));
            return `A adds ${n}`;
        },
        graphRemove ()
        {
            const text = readFile(a, NAME);
            const free = nodes(text, 'a').filter((n) => !fedFrom(text, n));

            if (free.length === 0)
                return null;

            const n = pick(free);

            history.step(NAME, `delete ${n}`, null, () =>
                spliceFile(a, NAME, remove(text, n), GRAPH));
            return `A deletes ${n}`;
        },
        type ()
        {
            /* Before a node, so that no node's text has a line in it of
               somebody else's that B's own pattern for it would miss. */
            const text = files(a).get(NAME);
            const at = pick([...text.toString().matchAll(/^node /gm)]
                .map((m) => m.index));

            /* Each a step of its own, so that a seed runs the same at any
               speed: the manager joins by the clock. */
            undoFor(a, NAME).stopCapturing();
            a.transact(() => text.insert(at, `# typed ${made++}\n`),
                       TYPING);
            return `A types at ${at}`;
        },
        undo () { return `A undoes: ${JSON.stringify(history.go(NAME, true))}`; },
        redo () { return `A redoes: ${JSON.stringify(history.go(NAME, false))}`; },
        peerAdd ()
        {
            const text = readFile(b, NAME);
            const from = rnd() < 0.7 ? pick(nodes(text, 'a')) ?? null : null;

            spliceFile(b, NAME, text + node('b', made++, from));
            return `B adds a node${from === null ? '' : ` fed from ${from}`}`;
        },
        peerRemove ()
        {
            const text = readFile(b, NAME);
            const mine = nodes(text, 'b');

            if (mine.length === 0)
                return null;

            const n = pick(mine);

            spliceFile(b, NAME, remove(text, n));
            return `B deletes ${n}`;
        },
    };
    const names = Object.keys(steps);

    /* What a step left, or why it broke. */
    const held = (before, did) =>
    {
        trail.push(did);

        const now = readFile(a, NAME);

        return now !== readFile(b, NAME) ? 'the two texts differ'
            : !parses(now) ? 'the text does not parse'
            : did.includes('"said":"Can\'t') && now !== before
                ? 'a refusal changed the text'
            : null;
    };

    for (let i = 0; i < STEPS; i++)
    {
        const before = readFile(a, NAME);
        const did = steps[pick(names)]();
        const broke = did === null ? null : held(before, did);

        if (broke !== null)
            return { seed, trail, broke };
    }

    /* B takes out all it added; A undoes until there is nothing to undo,
       or a refusal. */
    while (nodes(readFile(b, NAME), 'b').length > 0)
    {
        const before = readFile(a, NAME);
        const broke = held(before, steps.peerRemove());

        if (broke !== null)
            return { seed, trail, broke };

        if (readFile(a, NAME) === before)
            return { seed, trail, broke: 'B cannot take out its node' };
    }

    for (let i = 0; ; i++)
    {
        if (i > 10 * STEPS)
            return { seed, trail, broke: 'Undo never runs out' };

        const before = readFile(a, NAME);
        const step = history.go(NAME, true);

        if (step === null)
            return null;

        const broke = held(before, `A undoes: ${JSON.stringify(step)}`);

        if (broke !== null)
            return { seed, trail, broke };
    }
}

let failures = 0;

/* First that Undo and Redo do anything at all: one line typed, with
   nobody else about, taken back and put back on both pages. */
{
    const a = new Y.Doc();
    const b = new Y.Doc();

    a.on('update', (u) => Y.applyUpdate(b, u, 'a'));
    putFile(a, NAME, start);

    const history = roomHistory(a, parses);

    undoFor(a, NAME).addTrackedOrigin(TYPING);
    a.transact(() => files(a).get(NAME).insert(0, '# typed\n'), TYPING);

    const typed = readFile(b, NAME);
    const undid = history.go(NAME, true);
    const back = readFile(b, NAME);
    const redid = history.go(NAME, false);

    if (typed !== `# typed\n${start}` || undid?.what !== 'edit text' ||
        back !== start || redid?.what !== 'edit text' ||
        readFile(b, NAME) !== typed)
    {
        failures++;
        process.stdout.write('FAIL  a line typed is undone and redone on ' +
                             `both pages: ${JSON.stringify({ undid, redid })}\n`);
    }
}

for (let s = seed0; s < seed0 + RUNS; s++)
{
    const broken = run(s);

    if (broken !== null)
    {
        failures++;
        process.stdout.write(`FAIL  seed ${broken.seed}: ${broken.broke}, ` +
                             `after:\n      ${broken.trail.join('\n      ')}\n`);

        if (failures >= 3)
            break;
    }
}

process.stdout.write(failures === 0
    ? `ok    ${RUNS} runs of ${STEPS} steps from seed ${seed0}: one text, ` +
      'always parsing, nothing changed by a refusal, and Undo always ' +
      'runs out\n'
    : `\n${failures} failed\n`);
process.exitCode = failures;
