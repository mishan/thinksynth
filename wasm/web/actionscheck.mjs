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
 * actionscheck.mjs -- a controller's actions played on the page
 * (actions.js), in Node, with a page whose frames never come, as a hidden
 * tab's don't.
 *
 *   node wasm/web/actionscheck.mjs
 *
 * Exit status is the number of failures.
 */

let failures = 0;

function check (cond, what, detail = '')
{
    process.stdout.write(`${cond ? 'ok   ' : 'FAIL '} ${what}` +
                         `${!cond && detail ? `: ${detail}` : ''}\n`);
    failures += cond ? 0 : 1;
}

/* A page: its visibility, and frames asked for that never come. */
const listeners = [];

globalThis.document = {
    hidden: false,
    addEventListener: (type, fn) => listeners.push(fn),
};
globalThis.requestAnimationFrame = () => 1;
globalThis.cancelAnimationFrame = () => {};

const { actionPlayer } = await import('./actions.js');
const played = [];
const player = actionPlayer({ onNoteOn: () => {}, onNoteOff: () => {},
                              play: (a) => played.push(a), bendChannel: () => 0 });
const value = (t) => ({ kind: 'value', channel: 0, arg: 'cutoff', t });

player([value(0.2), value(0.3)]);
await new Promise((r) => setTimeout(r, 40));
check(played.length === 0, 'shown, the values wait for the next frame');

/* Hidden with a frame asked for, which a hidden page never draws. */
document.hidden = true;
for (const fn of listeners)
    fn();
await new Promise((r) => setTimeout(r, 40));
check(played.length === 1 && played[0].t === 0.3,
      'hidden before it came, they go by a timer: the latest of each',
      JSON.stringify(played));

player([value(0.4)]);
await new Promise((r) => setTimeout(r, 40));
check(played.length === 2 && played[1].t === 0.4,
      'and while hidden, the next do too');

process.stdout.write(`\n${failures} failure${failures === 1 ? '' : 's'}\n`);
process.exit(failures);
