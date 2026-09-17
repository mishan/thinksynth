#!/usr/bin/env node
/*
 * cairo2d -- the replayer's test, and the one that holds the two halves
 * together.
 *
 *   node test/replaytest.mjs [path to cairo2dtest]
 *
 * The recorder writes the op table and one recorded list as JSON
 * (cairo2dtest --dump); this reads both. The first half is the drift
 * check: replay.js has the opcodes and the arities written out a second
 * time, in another language, and the day they stop agreeing with the C is
 * the day every picture after the changed op is drawn from operands read
 * as opcodes. So they are compared, name by name.
 *
 * The second half replays that list onto a context that records what it
 * was asked to do instead of drawing it, and checks the calls -- in
 * particular the three places cairo and Canvas2D differ, which are the
 * three places the replayer is doing something rather than passing
 * something on: the beginPath owed after a fill or a stroke, the moveTo
 * that stands in for new_sub_path, and paint as a fillRect under the base
 * transform.
 *
 * Public domain, or CC0 where that is not a thing. Take it.
 */

import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

import { OPS, ARITY, OP_NAMES, replay } from '../replay.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const binary = process.argv[2] ?? path.join(here, 'cairo2dtest');

let failures = 0;

function fail (what) {
    process.stdout.write(`FAIL  ${what}\n`);
    failures++;
}

function ok (what) {
    process.stdout.write(`ok    ${what}\n`);
}

/* ---- the context that draws nothing ---------------------------------- */

/* Every call and every property the replayer touches, written down in
   order. A property becomes `font = bold 12px sans-serif'; a call becomes
   `fillText(hi, 4, 8)'. Numbers are rounded, because the list is float and
   the expectations are written by a person. */
function recorder () {
    const calls = [];

    const round = (v) => typeof v === 'number'
        ? String(Math.round(v * 1000) / 1000) : String(v);

    const method = (name) => (...args) =>
        calls.push(`${name}(${args.map(round).join(', ')})`);

    const ctx = {
        calls,
        beginPath: method('beginPath'),
        moveTo: method('moveTo'),
        lineTo: method('lineTo'),
        bezierCurveTo: method('bezierCurveTo'),
        arc: method('arc'),
        rect: method('rect'),
        closePath: method('closePath'),
        fill: method('fill'),
        stroke: method('stroke'),
        clip: method('clip'),
        fillRect: method('fillRect'),
        clearRect: method('clearRect'),
        fillText: method('fillText'),
        drawImage: method('drawImage'),
        save: method('save'),
        restore: method('restore'),
        translate: method('translate'),
        scale: method('scale'),
        setTransform: method('setTransform'),
        setLineDash: (d) => calls.push(`setLineDash([${d.join(', ')}])`),
    };

    for (const name of ['fillStyle', 'strokeStyle', 'lineWidth', 'lineCap',
                        'lineDashOffset', 'font', 'imageSmoothingEnabled']) {
        let held;

        Object.defineProperty(ctx, name, {
            get: () => held,
            set: (v) => { held = v; calls.push(`${name} = ${round(v)}`); },
        });
    }

    return ctx;
}

/* The calls the replayer made, with the frame's own bookkeeping -- the
   save, the transform, the clear and the restore -- left out. */
function body (ctx) {
    return ctx.calls.slice(3, -1);
}

function expect (got, want, what) {
    const g = JSON.stringify(got, null, 1);
    const w = JSON.stringify(want, null, 1);

    if (g === w) {
        ok(what);
        return;
    }

    fail(what);

    const many = Math.max(got.length, want.length);

    for (let i = 0; i < many; i++)
        if (got[i] !== want[i])
            process.stdout.write(
                `        ${i}: got ${got[i] ?? '(nothing)'}, ` +
                `wanted ${want[i] ?? '(nothing)'}\n`);
}

/* ---- the two halves agree on the table -------------------------------- */

function checkTable (table) {
    const names = Object.keys(table);

    for (const [name, { op, arity }] of Object.entries(table)) {
        if (OPS[name] === undefined) {
            fail(`replay.js has no op named ${name}`);
            continue;
        }

        if (OPS[name] !== op)
            fail(`${name} is ${op} in cairo2d.h and ${OPS[name]} in ` +
                 'replay.js');

        if (ARITY[op] !== arity)
            fail(`${name} takes ${arity} operands in cairo2d.h and ` +
                 `${ARITY[op]} in replay.js`);
    }

    for (const name of Object.keys(OPS))
        if (table[name] === undefined)
            fail(`replay.js has ${name}, which the recorder does not`);

    if (OP_NAMES.length !== names.length + 1)
        fail(`replay.js names ${OP_NAMES.length - 1} ops and the recorder ` +
             `has ${names.length}`);

    if (failures === 0)
        ok(`the opcodes and arities agree, all ${names.length} of them`);
}

/* ---- and on what the list means --------------------------------------- */

function checkReplay (dump) {
    const ctx = recorder();
    const ops = Float32Array.from(dump.ops);

    const count = replay(ctx, ops, dump.strings, [],
                         { width: 100, height: 50, dpr: 2 });

    if (ctx.calls[1] !== 'setTransform(2, 0, 0, 2, 0, 0)')
        fail('the device pixel ratio is not the first transform');

    if (ctx.calls[2] !== 'clearRect(0, 0, 100, 50)')
        fail('the frame did not start from an empty canvas');

    expect(body(ctx), [
        /* cairo2dtest's figure, op for op ... */
        'save()',
        'translate(10, 20)',
        'scale(2, 2)',
        'fillStyle = rgba(64, 128, 191, 0.5)',
        'strokeStyle = rgba(64, 128, 191, 0.5)',
        'lineWidth = 1.5',
        'lineCap = round',
        'beginPath()',
        'moveTo(1, 2)',
        'lineTo(3, 4)',
        'bezierCurveTo(5, 6, 7, 8, 9, 10)',
        /* ... with new_sub_path become the moveTo cairo would have made
           internally, to the arc's own first point ... */
        'moveTo(14, 12)',
        'arc(11, 12, 3, 0, 3.142)',
        'rect(0, 0, 20, 30)',
        'closePath()',
        'fill()',
        'stroke()',
        'restore()',

        /* ... then the text. The move_to before it is a path op, and the
           stroke above cleared cairo's path and not Canvas2D's, so this is
           where the beginPath owed for that is paid ... */
        'beginPath()',
        'moveTo(4, 8)',
        'font = bold 12px sans-serif',
        'fillText(hi, 4, 8)',

        /* ... then a dashed line, which owes nothing: showing text does
           not clear a path in either model, so the sub-path started above
           is still open and this one joins it ... */
        'setLineDash([4, 3])',
        'lineDashOffset = 0',
        'moveTo(0, 0)',
        'lineTo(10, 10)',
        'stroke()',

        /* ... and a paint, which is the whole canvas under the base
           transform, cut down by whatever clip is in force. */
        'fillStyle = rgba(255, 0, 0, 1)',
        'strokeStyle = rgba(255, 0, 0, 1)',
        'save()',
        'setTransform(2, 0, 0, 2, 0, 0)',
        'fillRect(0, 0, 100, 50)',
        'restore()',
    ], `the list replays into ${count} Canvas2D calls`);
}

/* An op the replayer does not know cannot come out of this build's
   recorder, so the only way to see one is to write one. */
function checkUnknown () {
    const ctx = recorder();

    try {
        replay(ctx, Float32Array.from([OPS.SAVE, 250, 1, 2]), [], []);
        fail('an unknown op replayed as if it were known');
    } catch (e) {
        if (!/unknown op 250/.test(e.message))
            fail(`an unknown op said: ${e.message}`);
        else
            ok('an unknown op stops the replay and says where');
    }
}

/* ---- and a surface is blitted, in the browser's byte order ------------- */

function checkSurface () {
    if (typeof ImageData === 'undefined' ||
        (typeof OffscreenCanvas === 'undefined' &&
         typeof document === 'undefined')) {
        ok('a surface is left to the browser: no canvas here to blit on');
        return;
    }

    const ctx = recorder();
    const data = new Uint8Array(2 * 1 * 4);

    data.set([0x10, 0x20, 0x30, 0xff], 0);

    replay(ctx, Float32Array.from(
        [OPS.SET_SOURCE_SURFACE, 0, 5, 6, OPS.PAINT]), [],
        [{ index: 0, width: 2, height: 1, stride: 8, data }],
        { width: 10, height: 10, dpr: 1 });

    const drawn = body(ctx);

    if (drawn.length !== 1 || !drawn[0].startsWith('drawImage('))
        fail(`a surface paint became ${drawn.join(', ') || 'nothing'}`);
    else
        ok('a surface paint is one drawImage under the current transform');
}

/* ---- run it ------------------------------------------------------------ */

let dump;

/* Compiled for the browser, the recorder is a .js that node runs rather
   than a program the shell can start. */
const run = /\.m?js$/.test(binary)
    ? [process.execPath, [binary, '--dump']]
    : [binary, ['--dump']];

try {
    dump = JSON.parse(execFileSync(run[0], run[1], { encoding: 'utf8' }));
} catch (e) {
    process.stdout.write(`replaytest: could not run ${binary}: ${e.message}\n`);
    process.exit(1);
}

checkTable(dump.table);
checkReplay(dump);
checkUnknown();
checkSurface();

process.stdout.write(failures === 0
    ? '\nthe recorder and the replayer agree, op for op\n'
    : `\n${failures} failed\n`);

process.exitCode = failures === 0 ? 0 : 1;
