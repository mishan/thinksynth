/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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
 * panelcheck.mjs -- the parameter panel, through the browser module, held
 * against the native one.
 *
 *   node wasm/web/panelcheck.mjs [BUILD_DIR] [NATIVE_BUILD_DIR]
 *
 * A panel is described once (src/PanelModel.h) and rendered twice, and the
 * point of describing it once is that the two renderings are of the same
 * thing. Nothing enforces that by itself: the two builds compile the same
 * sources, and a rule that quietly depended on the sample rate, on a
 * compiler's idea of a double, or on which order a map iterated in would
 * diverge without either side noticing. It is the divergence the whole
 * extraction exists to stop, so it is a gate.
 *
 * So scripts/panelcheck -j loads a fixture .dsp natively and prints its
 * panel as JSON; this loads the same bytes into the module and prints the
 * module's, and the two are compared byte for byte. The shape is in the
 * dump, so a row appearing on one side alone fails here rather than in a
 * browser.
 *
 * Then the half a dump cannot show: that an edit through tw_panel_edit
 * reaches the arg, that it is refused when it should be, and that the
 * catching-up guard holds -- an intent equal to what is there is allowed
 * and moves nothing, which is what keeps a panel following a knob from
 * reporting an edit nobody made.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.join(here, '..', '..');
const build = path.resolve(process.argv[2] ?? path.join(top, 'build-web'));
const native = path.resolve(process.argv[3] ?? path.join(top, 'build'));

const harness = path.join(native, 'scripts', 'panelcheck');

let failures = 0;

const ok = (what) => process.stdout.write(`ok    ${what}\n`);

const fail = (what, detail = '') =>
{
    process.stdout.write(`FAIL  ${what}${detail ? `: ${detail}` : ''}\n`);
    failures++;
};

const check = (good, what, detail = '') =>
    good ? ok(what) : fail(what, detail);

if (!fs.existsSync(harness))
{
    process.stdout.write(
        `panelcheck: no panelcheck in ${native}; build the native tree ` +
        'first -- it is what the module is compared against.\n');
    process.exit(1);
}

/* The native side first: it writes the fixture .dsp as well as the dump,
   and the module loads that exact text. */
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'panelcheck-'));

execFileSync(harness, ['-p', path.join(native, 'plugins') + path.sep,
                       '-j', scratch],
             { stdio: ['ignore', 'ignore', 'inherit'] });

const wanted = fs.readFileSync(path.join(scratch, 'panel.json'), 'utf8');
const dsp = fs.readFileSync(path.join(scratch, 'panel.dsp'), 'utf8');

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

const log = [];
const M = await createThinkWeb({
    print: (s) => log.push(s),
    printErr: (s) => log.push(s),
});

/* thPanel::Kind, and the flag that picks a channel's second arg map. */
const CHANARG = 0;
const INSTRUMENT = 0;

/* The rate and the window the page's own synth is made with (host.js), so
   that a duration folds here exactly as it folded natively -- the one
   number a panel's arithmetic actually depends on. */
M._tw_create(44100, 256, 4096);

const loaded = M.ccall('tw_load', 'number', ['number', 'string'], [0, dsp]);

if (loaded === 0)
{
    fail('the fixture .dsp loads in the module');
    log.forEach((line) => process.stdout.write(`      ${line}\n`));
    process.exit(1);
}

ok('the fixture .dsp loads in the module');

/* ---- the dump ---------------------------------------------------------- */

/* `>>> 0' on every shape this reads.
 *
 * The shape is a 32-bit unsigned hash, and wasm has one integer type: what
 * crosses is an i32 and JS reads it as signed, so a shape past 2^31 arrives
 * negative and would never equal the number in the dump. A page comparing
 * two shapes to each other would not care; one comparing a shape to the
 * dump's does. */
const shapeNow = () => M._tw_panel_shape() >>> 0;

const shape = M._tw_panel_open(CHANARG, 0, INSTRUMENT) >>> 0;

check(shape !== 0, 'the channel has a panel');

const got = `${M.UTF8ToString(M._tw_panel_json())}\n`;

if (got === wanted)
    ok('the module describes the panel exactly as the native build does');
else
{
    /* Where, rather than both of them: the dump is a few thousand
       characters and a diff nobody can find the place in is a diff nobody
       reads. */
    let at = 0;

    while (at < got.length && at < wanted.length && got[at] === wanted[at])
        at++;

    fail('the module describes the panel exactly as the native build does',
         `first difference at ${at}\n` +
         `      native: ...${wanted.slice(Math.max(0, at - 40), at + 60)}\n` +
         `      module: ...${got.slice(Math.max(0, at - 40), at + 60)}`);
}

const panel = JSON.parse(got);

check(panel.rows.length > 0 && panel.shape === shape,
      'and the shape it reported is the shape in the dump');

/* ---- the poll ---------------------------------------------------------- */

{
    const cut = panel.rows.findIndex((r) => r.id === 'cutoff');

    check(cut >= 0 && Math.abs(M._tw_panel_value(cut) - 0.25) < 1e-9,
          'a row polls the value its dump carried',
          String(M._tw_panel_value(cut)));

    /* A value moving is not a shape change. That is what lets a panel
       follow a knob without being torn down and built again. */
    M.ccall('tw_panel_edit', 'number',
            ['number', 'number', 'number', 'string', 'string'],
            [CHANARG, 0, INSTRUMENT, 'cutoff', '0.75']);

    check(shapeNow() === shape,
          'a value moving does not change the shape');
    check(cut >= 0 && Math.abs(M._tw_panel_value(cut) - 0.75) < 1e-9,
          'and the poll sees the new one', String(M._tw_panel_value(cut)));
}

/* ---- edits ------------------------------------------------------------- */

const edit = (row, text, b = INSTRUMENT) =>
{
    const moved = M.ccall('tw_panel_edit', 'number',
                          ['number', 'number', 'number', 'string', 'string'],
                          [CHANARG, 0, b, row, text]);

    return { moved, why: M.UTF8ToString(M._tw_panel_why()) };
};

{
    const r = edit('decay', '1000');

    check(r.moved === 1 && r.why === '',
          'a duration typed in milliseconds is an edit', r.why);

    /* Folded on the way in at the rate the synth was made with: 1000 ms is
       44100 samples, and the row still reads 1000. */
    shapeNow();

    const at = panel.rows.findIndex((r2) => r2.id === 'decay');

    check(Math.abs(M._tw_panel_value(at) - 1000) < 1e-6,
          'and it reads back in the unit it was typed in',
          String(M._tw_panel_value(at)));
}

{
    const same = edit('decay', '1000');

    check(same.moved === 0 && same.why === '',
          'an edit equal to what is there is allowed and moves nothing',
          same.why);
}

{
    const r = edit('wave', 'Triangle');

    check(r.moved === 1, 'a selector takes the value\'s name', r.why);

    const bad = edit('wave', 'Sqare');

    check(bad.moved === 0 && bad.why !== '',
          'a name that is not on the list is refused, with a reason',
          bad.why);

    const hole = edit('wave', '1');

    check(hole.moved === 0 && hole.why !== '',
          'and so is a value the plugin does not implement', hole.why);
}

{
    const r = edit('nosuchcontrol', '1');

    check(r.moved === 0 && r.why !== '',
          'an edit naming a control that is not there is refused', r.why);
}

{
    /* The panel the page has open is not the panel an edit names: an
       intent arrives from a peer and is applied wherever it points. */
    const before = shapeNow();
    const r = edit('cutoff', '0.5');

    check(r.moved === 1 && shapeNow() === before,
          'applying an edit does not disturb the panel that is open', r.why);
}

fs.rmSync(scratch, { recursive: true, force: true });

process.stdout.write(`\n${failures} failure(s)\n`);

process.exit(failures);
