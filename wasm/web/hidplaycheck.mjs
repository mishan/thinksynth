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
 * hidplaycheck.mjs -- what a Valve Index controller plays on the browser's
 * synth, from Node: the worklet's half of it (thinkweb.cpp, tw_controller
 * and tw_hid, through engine.js's 'hid').
 *
 *   node wasm/web/hidplaycheck.mjs [BUILD_DIR]
 *
 * A patch's controller block and its args are what the module says for
 * its channel; a bend reaches the note it names, sent with the note or
 * after it, and no other; a value is written onto the arg it names, in
 * the profile's range where it gives one, and one the patch hasn't got is
 * dropped.
 * Each heard by rendering the same note twice, once with the action. The
 * native half is scripts/hidplaycheck.
 *
 * Exit status is the number of failures.
 */

import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { apply } from './engine.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ??
                           path.join(here, '..', '..', 'build-web'));

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

/* hid/thhMapper.h's thhAction::Kind. */
const VALUE = 2, BEND = 3;

/* A sine at the note's pitch, at @level, played until it is let go of. */
const GRAPH =
    '@level = 0.5;\n@level.min = 0;\n@level.max = 1;\n' +
    'node ionode { channels = 1; out0 = amp->out; play = 1; };\n' +
    'io ionode;\n' +
    'node freq misc::midi2freq { note = ionode->note; };\n' +
    'node osc osc::simple { freq = freq->out; };\n' +
    'node amp math::mul { in0 = osc->out; in1 = @level; };\n' +
    'controller index {\n    a = note 3;\n};\n';

let failed = 0;
const report = (name, why) =>
{
    console.log(why ? `FAIL  ${name}\n  ${why}` : `ok    ${name}`);
    failed += why ? 1 : 0;
};

/* A fresh synth playing note 60, the action given once the note sounds
   or, `early', in the window the note is pressed in, before it sounds as
   the page's are; and the next 4096 frames' samples. */
const heard = async (action, early = false) =>
{
    const M = await createThinkWeb({ print: () => {}, printErr: () => {} });

    M._tw_create(48000, 256, 128);
    M.ccall('tw_load', 'number', ['number', 'string'], [0, GRAPH]);
    apply(M, { type: 'on', frame: -1, channel: 0, note: 60, velocity: 100 });
    if (!early)
        M._tw_render(128);
    if (action)
        apply(M, { type: 'hid', frame: -1, channel: 0, ...action });
    if (early)
        M._tw_render(128);

    const out = [];

    for (let done = 0; done < 16384; done += 128)
    {
        const p = M._tw_render(128) >> 2;

        out.push(...M.HEAPF32.subarray(p, p + 256));
    }
    return { M, out };
};
const same = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);

/* How often the left channel rises through zero: the pitch, near enough
   to tell two semitones up (x 1.12) from down. */
const rises = (out) =>
{
    let n = 0;

    for (let i = 2; i < out.length; i += 2)
        n += out[i - 2] < 0 && out[i] >= 0 ? 1 : 0;
    return n;
};
const upTwo = (bent, plain) =>
{
    const ratio = rises(bent) / rises(plain);

    return ratio > 1.08 && ratio < 1.17 ? '' : `the pitch went x ${ratio.toFixed(3)}`;
};

const plain = await heard(null);
const said = JSON.parse(plain.M.ccall('tw_controller', 'string', ['number'],
                                      [0]));

report('a patch\'s controller block and its args, for its channel',
       said.block.includes('a = note 3') && said.args.includes('level') ? '' :
       `said ${JSON.stringify(said)}`);

const again = await heard(null);

report('the same note rendered twice is the same', same(plain.out, again.out)
       ? '' : 'it differs, so nothing below can be heard');

report('a bend reaches the note it names, two semitones up',
       upTwo((await heard({ kind: BEND, note: 60, t: 2 })).out, plain.out));
report('a bend sent with its note, before it sounds, reaches it',
       upTwo((await heard({ kind: BEND, note: 60, t: 2 }, true)).out,
             (await heard(null, true)).out));
report('a bend of a note not sounding moves nothing',
       same(plain.out, (await heard({ kind: BEND, note: 61, t: 2 })).out)
       ? '' : 'the note sounding moved');
report('a value is written onto its arg',
       !same(plain.out, (await heard({ kind: VALUE, arg: 'level', t: 1 })).out)
       ? '' : 'the level is as it was');
report('a value is put in the profile\'s range where it gives one',
       same((await heard({ kind: VALUE, arg: 'level', t: 0.375 })).out,
            (await heard({ kind: VALUE, arg: 'level', t: 0.25, min: 0.25,
                           max: 0.75 })).out)
       ? '' : 'the arg\'s own range was used');
report('a value for an arg the patch hasn\'t got is dropped',
       same(plain.out, (await heard({ kind: VALUE, arg: 'nothere', t: 1 })).out)
       ? '' : 'something moved');

console.log(`\n${failed} failure${failed === 1 ? '' : 's'}`);
process.exit(failed);
