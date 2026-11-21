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
 * phoneplaycheck.mjs -- a phone played through the page's controller
 * module (thinkhid.cpp's th_phone_*), in Node.
 *
 *   node wasm/web/phoneplaycheck.mjs [BUILD_DIR]
 *
 * Its frames, as its page sends them, played by the phone's own profile:
 * a finger's note, bend and loudness, on the keyboard's channel; the phone
 * gone quiet let go of by the tick, and a pairing ended let go of at
 * once; a frame that isn't one refused; and a profile of the player's
 * that doesn't fit said so, with the phone's own still playing.
 *
 * Exit status is the number of failures.
 */

import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ?? path.join(here, '..', '..', 'build-web'));

let failures = 0;

function check (cond, what, detail = '')
{
    process.stdout.write(`${cond ? 'ok   ' : 'FAIL '} ${what}` +
                         `${!cond && detail ? `: ${detail}` : ''}\n`);
    failures += cond ? 0 : 1;
}

const { default: createThinkHid } =
    await import(pathToFileURL(path.join(build, 'thinkhid.js')).href);
const M = await createThinkHid();

/* hid/thhPhone.h's axes: tilt_x, tilt_y, then each finger's x and y. As
   the page hands them over: their bytes. */
const AXES = 12;
const send = (id, n, a, down, now) =>
    M.ccall('th_phone_frame', 'number',
            ['number', 'number', 'array', 'number', 'number'],
            [id, n, new Uint8Array(a.buffer), down, now]);
let seq = 0;

/* A frame with one finger at (x, y), or none. */
const frame = (now, down, x = 0, y = 0, tilt = 0) =>
{
    const a = new Float32Array(AXES);

    a[0] = tilt;
    a[2] = x;
    a[3] = y;
    return send(7, ++seq, a, down ? 1 : 0, now);
};
const take = () => JSON.parse(M.ccall('th_phone_take', 'string', [], []));
const tick = (now) => M.ccall('th_phone_tick', 'number', ['number'], [now]);
const profile = (text) => M.ccall('th_phone_profile', 'string', ['string'], [text]);

check(profile('') === '', 'the phone\'s own profile binds');
take();

M.ccall('th_channel', null, ['number'], [2]);
frame(0, true, 0.2, 0.9, 0.5);

const first = take();
const on = first.find((a) => a.kind === 'on');
const bend = first.find((a) => a.kind === 'bend');
const hush = first.find((a) => a.kind === 'aux' && a.aux === 3);

check(on?.note === 48 && on.channel === 2,
      'a finger plays C3 on the keyboard\'s channel', JSON.stringify(on));
check(Math.round(bend?.t) === 7 && Math.abs(hush?.t - 0.1) < 0.01,
      'from its first moment a fifth up, a fifth of the way across, and ' +
      'loud near the top', JSON.stringify(first));

frame(0.02, false, 0.2, 0.9);
check(take().some((a) => a.kind === 'off' && a.note === 48),
      'and lifting it ends it');

/* Quiet: held, then nothing for a second. */
frame(0.1, true, 0.5, 0.5);
take();
check(tick(0.5) === 1 && !take().some((a) => a.kind === 'off'),
      'half a second without a frame is nothing');
check(tick(1.2) === 0 && take().some((a) => a.kind === 'off'),
      'a second is the phone gone, and its note let go');

/* A gap with no tick in it, then a frame: the quiet second is still the
   phone gone, the held note let go before the frame plays afresh. */
frame(1.5, true, 0.5, 0.5);
take();
frame(2.8, true, 0.5, 0.5);

const gap = take().map((a) => a.kind).join();

check(gap.startsWith('off') && gap.includes('on'),
      'a frame after a quiet second with no tick lets the held note go first',
      gap);

/* A pairing ended: everything let go at once, not a second later, and a
   frame still on its way after it plays nothing. */
frame(3, true, 0.5, 0.5);
take();
M.ccall('th_phone_release', null, [], []);
check(take().some((a) => a.kind === 'off'),
      'a pairing ended lets the phone\'s notes go at once');
check(tick(3.002) === 0 && !take().some((a) => a.kind === 'on'),
      'and a tick after the end plays nothing');
frame(3.005, false, 0.6, 0.5);
frame(3.01, true, 0.6, 0.5);
check(!take().some((a) => a.kind === 'on'),
      'and frames still on their way after the end, a finger lifting and ' +
      'landing again, play nothing');

/* A new pairing, and a player's profile: what it binds is what plays. */
profile('device phone {\n    touch_1 = note 2;\n};\n');
take();
frame(3.5, true, 0.2, 0.5);
check(take().some((a) => a.kind === 'on' && a.note === 50),
      'a pairing started plays from nothing held, by the player\'s profile ' +
      'over the phone\'s own');
frame(3.52, false, 0.2, 0.5);
take();
profile('');
take();

check(send(8, 1, new Float32Array(AXES).fill(NaN), 1, 3) === 0 &&
      !take().some((a) => a.kind === 'on'),
      'a frame that isn\'t numbers is refused, and plays nothing');

const bad = profile('device phone {\n    nope = note 0;\n};\n');

frame(4, true, 0.2, 0.5);
check(/nope/.test(bad) && take().some((a) => a.kind === 'on' && a.note === 48),
      'a player\'s profile that doesn\'t fit is said so, and the phone\'s ' +
      'own still plays', bad);

process.stdout.write(`\n${failures} failure${failures === 1 ? '' : 's'}\n`);
process.exit(failures);
