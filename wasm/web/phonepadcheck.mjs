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
 * phonepadcheck.mjs -- what the phone's page makes of its sensors and
 * fingers, and when it sends (phonepad.js), in Node.
 *
 *   node wasm/web/phonepadcheck.mjs
 *
 * Tilt in the screen's axes however it is held; fingers in the slots they
 * landed in; and frames at the pace and on the channels the page needs.
 *
 * Exit status is the number of failures.
 */

import { AFTER_MS, AXES, Fingers, HEARTBEAT_MS, Sender, tilt } from './phonepad.js';

let failures = 0;

function check (cond, what, detail = '')
{
    process.stdout.write(`${cond ? 'ok   ' : 'FAIL '} ${what}` +
                         `${!cond && detail ? `: ${detail}` : ''}\n`);
    failures += cond ? 0 : 1;
}

const g = 9.81;
const near = (a, b) => a.every((v, k) => Math.abs(v - b[k]) < 0.01);

for (const [what, reading, angle, sign, want] of [
    ['flat on its back is level', { x: 0, y: 0, z: g }, 0, 1, [0, 0]],
    ['held upright is all the way up', { x: 0, y: g, z: 0 }, 0, 1, [0, 1]],
    ['tipped right is right', { x: -g * Math.SQRT1_2, y: 0, z: g * Math.SQRT1_2 },
     0, 1, [Math.SQRT1_2, 0]],
    ['where the platform reads the other way, the same once its sign is given',
     { x: g * Math.SQRT1_2, y: 0, z: -g * Math.SQRT1_2 }, 0, -1, [Math.SQRT1_2, 0]],
    /* Turned 90, the device's right edge is the screen's top: gravity's
       reaction reads up it. */
    ['turned to landscape, up is the screen\'s up, not the device\'s',
     { x: g, y: 0, z: 0 }, 90, 1, [0, 1]],
    ['and the other way round', { x: -g, y: 0, z: 0 }, 270, 1, [0, 1]],
    ['shaken past gravity is held to the end', { x: -3 * g, y: 0, z: 0 }, 0, 1, [1, 0]],
])
{
    const got = tilt(reading, angle, sign);

    check(near(got, want), what, got.map((v) => v.toFixed(2)).join());
}

{
    const f = new Fingers();
    const a = f.down('a', 0.1, 0.2), b = f.down('b', 0.3, 0.4);

    f.up('a');

    const c = f.down('c', 0.5, 0.6);

    for (let id = 0; id < 4; id++)
        f.down(`more${id}`, 0, 0);
    check(a === 0 && b === 1 && c === 0 && f.down('sixth', 0, 0) === -1 &&
          f.bits === 0b11111,
          'a finger takes the lowest free slot and keeps it; a sixth has none');

    f.move('b', 0.9, 0.8);
    f.up('more0');
    check(f.axes([0.5, -0.5]).join() ===
          [0.5, -0.5, 0.5, 0.6, 0.9, 0.8, 0, 0, 0, 0, 0, 0].join() &&
          f.bits === 0b11011 && f.axes([0, 0]).length === AXES,
          'a frame\'s axes are the tilt and each slot\'s place, and a lifted ' +
          'finger\'s bit is clear', f.axes([0.5, -0.5]).join());
    f.clear();
    check(f.bits === 0, 'and the page hidden lifts them all');
}

{
    /* The pace: a frame every BUSY_MS while a finger moves, and for
       AFTER_MS once it stops; then one each HEARTBEAT_MS. */
    const s = new Sender(42);
    const axes = new Array(AXES).fill(0);
    const sent = [];
    let x = 0;

    for (let t = 0; t <= 2000; t += 4)
    {
        if (t > 0 && t <= 500)
            x = t / 1000;
        axes[2] = x;

        const r = s.next(axes, t <= 500 ? 1 : 0, t);

        if (r !== null)
            sent.push([t, r.both]);
    }

    const moving = sent.filter(([t]) => t > 0 && t <= 500).length;
    const after = sent.filter(([t]) => t > 500 && t <= 500 + AFTER_MS).length;
    const resting = sent.filter(([t]) => t > 500 + AFTER_MS + 50 && t <= 2000).length;
    const both = sent.filter(([, b]) => b).map(([t]) => t);

    check(moving >= 25 && moving <= 35,
          'a finger moving sends about 60 frames a second', `${moving} in 0.5 s`);
    check(after >= 8 && after <= 14,
          'and goes on at that pace a moment after it lifts', `${after}`);
    check(resting >= 4 && resting <= 6,
          'then a heartbeat, four a second', `${resting} in 1.25 s`);
    check(both.join() === '0,504',
          'the frames a finger lands and lifts in go on both channels, the rest ' +
          'on the one that drops', both.join());
}

{
    /* A tilt that moves less than STILL is a phone held still: it sends
       no more than one that doesn't move at all. */
    const count = (tremble) =>
    {
        const sender = new Sender(1);
        const a = new Array(AXES).fill(0);
        let sent = 0;

        for (let t = 0; t <= 1000 + HEARTBEAT_MS; t += 4)
        {
            a[0] = tremble ? (t % 8 === 0 ? 1 : -1) * 0.004 : 0;
            sent += sender.next(a, 0, t) ? 1 : 0;
        }
        return [sender, a, sent];
    };
    const [s, axes, n] = count(true);

    check(n === count(false)[2],
          'a tilt trembling under 0.01 sends no more than one held dead still',
          `${n} against ${count(false)[2]}`);

    axes[0] = 0.5;

    const r = s.next(axes, 0, 5000);

    check(r !== null && r.frame.id === 1 && r.frame.seq === n + 1,
          'one past it does, counted on from the last');
}

{
    /* Which finger moved counts: the second finger's place, while it is
       down, is a change; an empty slot's isn't. */
    const busy = (down, axis) =>
    {
        const s = new Sender(3);
        const a = new Array(AXES).fill(0);

        s.next(a, down, 0);
        a[axis] = 0.5;
        return s.next(a, down, HEARTBEAT_MS - 1) !== null;
    };

    check(busy(0b10, 4) && !busy(0b01, 4) && !busy(0, 2),
          'a finger down moving is a change, and an empty slot\'s place is not');
}

process.stdout.write(`\n${failures} failure${failures === 1 ? '' : 's'}\n`);
process.exit(failures);
