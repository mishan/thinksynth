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
 * And then the other two providers the module carries. A piece's knobs,
 * whose rows are numbered by the command that moves them and which
 * tw_panel_edit refuses outright, because a knob is heard and its delivery
 * is a stamp. And a composer stage's params, which are compared against a
 * second native dump -- they are read out of a .gen rather than off a live
 * object, so they are the rows with the most to disagree about -- and then
 * edited through tw_param, which is the stamped door a param has for the
 * same reason a knob does.
 *
 * And the one rule the page does implement itself. A row arrives spelled,
 * but a value that moves is spelled on the page as it moves, so panel.js
 * carries thPanelSpell in JavaScript -- the one piece of the description
 * that exists twice, and therefore the one that can disagree. Comparing two
 * C++ dumps cannot see that: both are the same function. So the native side
 * also writes what it spells a table of awkward numbers as, and panel.js is
 * held against it.
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
const wantedStage = fs.readFileSync(path.join(scratch, 'stage.json'),
                                    'utf8');
const gen = fs.readFileSync(path.join(scratch, 'panel.gen'), 'utf8');

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

const log = [];
const M = await createThinkWeb({
    print: (s) => log.push(s),
    printErr: (s) => log.push(s),
});

/* thPanel::Kind, and the flag that picks a channel's second arg map. */
const CHANARG = 0;
const KNOB = 1;
const GEN_PARAM = 2;
const INSTRUMENT = 0;

/* A shipped piece with knobs in it, for the knob panel below. */
const PIECE = 'airports.gen';

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

/* Two dumps, held against each other.
 *
 * Where they differ rather than both of them: a dump is a few thousand
 * characters and a diff nobody can find the place in is a diff nobody
 * reads. */
const same = (got, want, what) =>
{
    if (got === want)
    {
        ok(what);
        return;
    }

    let at = 0;

    while (at < got.length && at < want.length && got[at] === want[at])
        at++;

    fail(what,
         `first difference at ${at}\n` +
         `      native: ...${want.slice(Math.max(0, at - 40), at + 60)}\n` +
         `      module: ...${got.slice(Math.max(0, at - 40), at + 60)}`);
};

const got = `${M.UTF8ToString(M._tw_panel_json())}\n`;

same(got, wanted,
     'the module describes the panel exactly as the native build does');

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

/* The spelling, which the page does for itself.
 *
 * printf rounds a tie to the even digit and JavaScript's toFixed rounds it
 * up, so 0.25 at one decimal is "0.2" in the module and was "0.3" on the
 * page -- and a slider put back where it started would then read as an edit,
 * for ever, since the comparison it loses is the catching-up guard. */
{
    const { spell, numberIn, hold } =
        await import(pathToFileURL(path.join(here, 'panel.js')));

    const table = JSON.parse(
        fs.readFileSync(path.join(scratch, 'spell.json'), 'utf8'));

    let wrong = null;

    for (const [value, decimals, text] of table)
        if (spell(value, decimals) !== text && wrong === null)
            wrong = `${value} at ${decimals}: native "${text}", ` +
                    `page "${spell(value, decimals)}"`;

    check(table.length > 0 && wrong === null,
          `panel.js spells all ${table.length} of them as the module does`,
          wrong ?? '');

    /* And what it refuses. Number('') is 0 and Number('  ') is 0, and for a
       knob the page's check is the only one there is: its delivery is a
       stamped command, which carries a number nothing looks at again. */
    const refuses = ['', '   ', '\t', 'loud', '4k', '1.2.3', 'NaN',
                     'Infinity', '0x10', '1,5', '+', '-'];
    const takes = [['0', 0], ['4000', 4000], [' 2.5 ', 2.5], ['-0.5', -0.5],
                   ['1e3', 1000], ['.5', 0.5], ['+7', 7]];

    const badRefusal = refuses.find((t) => numberIn(t) !== null);
    const badTake = takes.find(([t, v]) => numberIn(t) !== v);

    check(badRefusal === undefined && badTake === undefined,
          'panel.js takes a number for a number and nothing else for one',
          badRefusal !== undefined
              ? `took "${badRefusal}" as ${numberIn(badRefusal)}`
              : (badTake ? `read "${badTake[0]}" as ${numberIn(badTake[0])}`
                         : ''));

    /* hold() is the three rules ArgPanel::propose applies, applied before an
       edit leaves the page: a text that is not a number, the travel, and the
       resolution. */
    const row = { lo: 0, hi: 20000, step: 1, decimals: 0, value: 7000 };

    check(hold(row, '') === null && hold(row, '   ') === null &&
          hold(row, '99999') === '20000' && hold(row, '-1') === '0' &&
          hold(row, '12345.678') === '12346',
          'and holds one inside the row it came from',
          [hold(row, ''), hold(row, '99999'), hold(row, '-1'),
           hold(row, '12345.678')].join(' '));
}

/* ---- a piece's knobs --------------------------------------------------- */

/* The other provider that reaches this module, and the one that shows what
 * describing an edit and delivering it are two things for: a knob is heard,
 * so a move has to land at the same transport time on every peer, and the
 * delivery is a stamped command rather than a write. The panel describes it;
 * tw_knob moves it.
 */
{
    const gen = fs.readFileSync(path.join(top, 'gen', PIECE), 'utf8');

    /* A piece carries its own instruments, and a module has no file system
       and cannot fetch: the page hands them over before it loads anything
       and so does this. A piece whose instrument will not resolve does not
       load, and then there are no knobs to describe. */
    for (const name of JSON.parse(fs.readFileSync(
             path.join(build, 'dsp', 'index.json'), 'utf8')))
    {
        /* The index carries the kit too, and a wav read as text is not a
           wav any more. No knob is in one. */
        if (name.startsWith('samples/'))
            continue;

        M.ccall('tw_instrument', 'number', ['string', 'string'],
                [name, fs.readFileSync(path.join(build, 'dsp', name),
                                       'utf8')]);
    }

    if (M.ccall('tw_piece_load', 'number', ['string', 'number'],
                [gen, 4242]) === 0)
        fail(`${PIECE} loads in the module`);
    else
    {
        ok(`${PIECE} loads in the module`);

        const shape = M._tw_panel_open(KNOB, 0, 0) >>> 0;

        check(shape !== 0, 'the piece has a knob panel');

        const knobs = JSON.parse(M.UTF8ToString(M._tw_panel_json()));

        check(knobs.kind === KNOB && knobs.rows.length > 0,
              `and it has the piece's knobs: ` +
              knobs.rows.map((r) => `${r.id}=${r.knob}`).join(', '));

        /* A row id is the number a command names the knob by, and it has
           to be, because an intent crosses to a peer with no panel open. */
        check(knobs.rows.every(
                  (r) => String(M.ccall('tw_knob_index', 'number', ['string'],
                                        [r.knob])) === r.id),
              'a row id is the number a command names the knob by');

        /* Not through tw_panel_edit, and the refusal says so rather than
           quietly doing nothing: applying a knob move immediately here
           would be a second door to the same write, opening at a different
           moment on every peer. */
        const refused = M.ccall(
            'tw_panel_edit', 'number',
            ['number', 'number', 'number', 'string', 'string'],
            [KNOB, 0, 0, knobs.rows[0].id, String(knobs.rows[0].lo)]);
        const why = M.UTF8ToString(M._tw_panel_why());

        check(refused === 0 && why !== '',
              'a knob is not set by an edit, and the refusal says why', why);
    }
}

/* ---- a composer stage's parameters ------------------------------------- */

/* The provider that reads a file rather than a live object, and so the one
 * with the most to disagree about between two builds: what a row says is
 * what the .gen says, down to which of the three units a duration was
 * written in.
 *
 * Loaded last because a piece takes the channel the .dsp above was on
 * (thinkweb.cpp, tw_piece_load): the two are modes here as they are on the
 * page, so the channel panel's checks come first and stay above this.
 */
{
    if (M.ccall('tw_piece_load', 'number', ['string', 'number'],
                [gen, 7]) === 0)
        fail('the fixture .gen loads in the module');
    else
    {
        ok('the fixture .gen loads in the module');

        const shape = M._tw_panel_open(GEN_PARAM, 0, 0) >>> 0;

        check(shape !== 0, 'the first stage has a panel');

        same(`${M.UTF8ToString(M._tw_panel_json())}\n`, wantedStage,
             'and the module describes it exactly as the native build does');

        const stage = JSON.parse(M.UTF8ToString(M._tw_panel_json()));

        /* Not through tw_panel_edit, and the refusal says so rather than
           quietly doing nothing: a param is heard, so it goes through
           tw_param and lands at a transport time on every peer. */
        const refused = M.ccall(
            'tw_panel_edit', 'number',
            ['number', 'number', 'number', 'string', 'string'],
            [GEN_PARAM, 0, 0, 'period', '8']);
        const why = M.UTF8ToString(M._tw_panel_why());

        check(refused === 0 && why !== '',
              'a stage param is not set by an edit, and the refusal says why',
              why);

        /* The door it does go through. Stamped below zero, which is "the
           top of the next window" -- what a solo page and a stopped
           transport send -- so one step applies it. */
        const param = (row, text) =>
        {
            M.ccall('tw_param', null,
                    ['number', 'number', 'number', 'string', 'string'],
                    [-1, 0, 0, row, text]);

            M._tw_step(M._tw_frame() + 4096);
        };

        const rowOf = (id) =>
        {
            M._tw_panel_open(GEN_PARAM, 0, 0);

            return JSON.parse(M.UTF8ToString(M._tw_panel_json()))
                .rows.find((r) => r.id === id);
        };

        param('period', '8');

        check(rowOf('period').value === 8 &&
              rowOf('period').units === 'beats',
              'a number typed into the box keeps the unit the line had',
              JSON.stringify(rowOf('period')?.text));

        param('period', 'ms');

        check(rowOf('period').units === 'ms' &&
              rowOf('period').value === 8,
              'and the menu alone keeps the number');

        /* The splice is the lasting half, and the page reads it back: the
           edit is in the piece's own text, in this instance and in every
           other that applied the same command. */
        check(M.UTF8ToString(M._tw_piece_text()).includes('period = 8 ms'),
              'an edit is spliced into the piece, as the file spells it');

        param('prob', '@warmth');

        check(rowOf('prob').knob === 'warmth' && !rowOf('prob').editable,
              'a binding rebinds, and the row stops offering a number');

        param('prob', '@');

        check(rowOf('prob').knob === '' && rowOf('prob').editable &&
              rowOf('prob').value === 0.35,
              'and letting it go holds the number the knob was at',
              String(rowOf('prob').value));

        /* Refused, and nothing written. A refusal reaching the file would
           be worse than a refusal: the peers' copies would part. */
        const before = M.UTF8ToString(M._tw_piece_text());

        param('notes', 'H4');

        check(M.UTF8ToString(M._tw_piece_text()) === before,
              'a value the param cannot take changes nothing');

        check(stage.rows.some((r) => r.bindable) &&
              stage.knobs.length === 2,
              'the panel carries the knobs a bindable row may be bound to');
    }
}

fs.rmSync(scratch, { recursive: true, force: true });

process.stdout.write(`\n${failures} failure(s)\n`);

process.exit(failures);
