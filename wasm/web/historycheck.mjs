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
 * historycheck.mjs -- TextHistory, on a clock the check moves.
 *
 *   node wasm/web/historycheck.mjs
 */

import { TextHistory } from './history.js';

let failures = 0;
let clock = 0;

function check (cond, what)
{
    if (!cond)
        failures++;

    process.stdout.write(`${cond ? 'ok  ' : 'FAIL'}  ${what}\n`);
}

const fresh = (opts = {}) =>
{
    clock = 0;
    return new TextHistory({ now: () => clock, ...opts });
};

{
    const h = fresh();

    h.record('a');
    check(!h.canUndo && h.text === 'a',
          'the first text is where it starts, with nothing to undo');

    h.record('b', 'add lp');
    h.record('b', 'add lp');
    h.record('c', 'cut lp.in');
    const u1 = h.undo();
    const u2 = h.undo();
    check(u1.text === 'b' && u1.what === 'cut lp.in' &&
          u2.text === 'a' && u2.what === 'add lp' && h.undo() === null,
          'undo walks back through each text, named, and stops at the start');

    const r = h.redo();
    check(r.text === 'b' && r.what === 'add lp' && h.redoName === 'cut lp.in',
          'redo walks forward under the same names');

    h.record('d', 'move lp');
    check(!h.canRedo && h.undoName === 'move lp',
          'an edit after an undo leaves nothing to redo');
}

{
    const h = fresh();

    h.record('a');
    for (const v of ['1', '2', '3'])
    {
        clock += 900;
        h.record(v, 'set lp.cut', 'lp.cut');
    }
    clock += 1000;
    h.record('4', 'set lp.cut', 'lp.cut');
    clock += 100;
    h.record('5', 'set lp.res', 'lp.res');

    check(h.undo().text === '4' && h.undo().text === '3' &&
          h.undo().text === 'a' && !h.canUndo,
          'edits with one join, each less than 1 s after the last, are one ' +
          'step; a gap of 1 s or another join starts the next');
}

{
    const h = fresh();

    h.record('a', '', 'lp.cut');
    h.record('b', 'set lp.cut', 'lp.cut');
    check(h.undo()?.text === 'a',
          'the first edit after the start is a step, whatever its join');
}

{
    const h = fresh();

    h.record('a');
    h.record('b', 'add lp');
    h.record('1', 'set lp.cut', 'lp.cut');
    h.undo();
    h.record('2', 'set lp.cut', 'lp.cut');
    check(h.undo().text === 'b' && h.redo().text === '2',
          'an edit after an undo is a step of its own, even with the join ' +
          'before it');
}

{
    const h = fresh({ keep: 3 });

    for (const t of ['a', 'b', 'c', 'd', 'e'])
        h.record(t, t);
    h.undo(); h.undo(); h.undo();
    check(h.text === 'b' && !h.canUndo,
          'past the cap, the oldest steps are forgotten first');

    h.clear();
    h.record('x');
    check(h.text === 'x' && !h.canUndo && !h.canRedo,
          'cleared, the next text is a new start');
}

process.stdout.write(failures === 0 ? '\nthe history holds\n'
                                    : `\n${failures} failed\n`);
process.exitCode = failures;
