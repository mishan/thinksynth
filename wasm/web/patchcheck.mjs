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
 * patchcheck.mjs -- a .patch, read by the browser module, held against the
 * native one.
 *
 *   node wasm/web/patchcheck.mjs [BUILD_DIR] [NATIVE_BUILD_DIR]
 *
 * The format is read once (src/PatchFile.h) and compiled twice, and the
 * point of reading it once is that the two readings are of the same thing.
 * Nothing enforces that by itself, and the last time nothing enforced it the
 * two readings drifted: the page dropped every `effect' line on the floor and
 * sent `side' to the engine as a chanarg. It is the divergence the whole
 * extraction exists to stop, so it is a gate.
 *
 * So scripts/patchcheck -j writes each awkward fixture and the document it
 * read from it; this hands the same bytes to tw_patch_read and compares the
 * two dumps byte for byte. The refusals are compared too -- text that is not
 * a patch has to be not a patch on both sides, and for the same stated
 * reason.
 *
 * Then the half a dump cannot show: that tw_patch_apply actually puts one on
 * a channel -- the graph it names, and its overrides on that graph's
 * chanargs -- and that a patch naming a graph the module does not have is
 * refused with the channel left alone.
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

const harness = path.join(native, 'scripts', 'patchcheck');

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
        `patchcheck: no patchcheck in ${native}; build the native tree ` +
        'first -- it is what the module is compared against.\n');
    process.exit(1);
}

/* The native side first: it writes each fixture's bytes as well as its
   document, and the module reads that exact text. */
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'patchcheck-'));

execFileSync(harness, ['-j', scratch],
             { stdio: ['ignore', 'ignore', 'inherit'] });

const names = fs.readFileSync(path.join(scratch, 'index.txt'), 'utf8')
    .split('\n').filter((n) => n !== '');

check(names.length > 0, 'the native harness wrote its fixtures');

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

const log = [];
const M = await createThinkWeb({
    print: (s) => log.push(s),
    printErr: (s) => log.push(s),
});

/* The same rate and window the page's own synth is made with (host.js).
   Nothing a document says depends on either, which is the point: if a
   reading ever starts to, this is where it shows. */
M._tw_create(44100, 256, 4096);

/* ---- the reading ---- */

for (const name of names)
{
    const text = fs.readFileSync(path.join(scratch, `${name}.patch`), 'utf8');

    /* The native dump is the document and then the refusal, a line each --
       one of the two is always empty. */
    const [wantJson, wantWhy] =
        fs.readFileSync(path.join(scratch, `${name}.json`), 'utf8')
            .split('\n');

    const gotJson = M.ccall('tw_patch_read', 'string', ['string'], [text]);
    const gotWhy = M.ccall('tw_patch_why', 'string', [], []);

    check(gotJson === wantJson, `${name}: reads the same`,
          gotJson === wantJson ? ''
              : `\n    native ${wantJson}\n    wasm   ${gotJson}`);
    check(gotWhy === wantWhy, `${name}: refuses for the same reason`,
          `native "${wantWhy}", wasm "${gotWhy}"`);
}

/* ---- the first-run configuration ---- */

/* The four patches a channel gets when nothing has aimed it, and the rule
 * for a channel above the last of them. The page had a copy of both, with a
 * comment saying the list was gthPrefs.cpp's "exactly" -- which is the kind
 * of promise nothing keeps, so now it is this.
 */
{
    const wanted = fs.readFileSync(path.join(scratch, 'defaults.txt'), 'utf8')
        .split('\n').slice(0, -1);

    const got = wanted.map((_, c) =>
        M.ccall('tw_patch_default', 'string', ['number'], [c]));

    check(got.join('\n') === wanted.join('\n'),
          'the first-run defaults are the same on both sides',
          `\n    native ${wanted.join(', ')}\n    wasm   ${got.join(', ')}`);

    check(M._tw_patch_default_count() === new Set(wanted).size,
          'and there are as many of them as there are distinct names');
}

/* ---- and onto a channel ---- */

/* The graph a shipped patch names, and the shipped patch itself. The module
   looks a `dsp' line up among the instruments the page handed over, which is
   the same lookup the application makes under DSP_PATH -- so handing this one
   graph over stands in for the page's Start.

   The corpus rather than a fixture, because what is being asked here is
   whether a real .patch reaches a real channel: the fixtures above have
   already said everything there is to say about awkward text. */
M.ccall('tw_instrument', 'number', ['string', 'string'],
        ['ts1.dsp',
         fs.readFileSync(path.join(top, 'dsp', 'ts1.dsp'), 'utf8')]);

const SHIPPED = fs.readFileSync(
    path.join(top, 'patches', 'leads', 'SuperRes.patch'), 'utf8');

const apply = (channel, text, name = '') =>
    M.ccall('tw_patch_apply', 'number', ['number', 'string', 'string'],
            [channel, text, name]) !== 0;

check(apply(0, SHIPPED, 'leads/SuperRes.patch'),
      'a shipped patch goes on a channel');

check(M.ccall('tw_patch_why', 'string', [], []) === '',
      'and has nothing to say about it');

/* The overrides landed, which is the half of loading a patch that used to be
   a separate call the page made in the right order by hand. */
const json = M.ccall('tw_patch_json', 'string', ['number'], [0]);

check(json !== '', 'and the channel says what is on it');

if (json !== '')
{
    const doc = JSON.parse(json);

    check(doc.dsp === 'ts1.dsp', 'by the name the file gave',
          `got ${doc.dsp}`);
    /* 1.040810, the `cutoff' line in SuperRes.patch, through a float and
       back out as a double: the value the file gives, not a rounding of it
       and not the 4 the graph declares. */
    check(doc.args.cutoff?.[0] === Math.fround(1.040810),
          'with its overrides in it',
          `got ${JSON.stringify(doc.args.cutoff)}`);
}

/* ---- and what the slot remembers ---- */

/* The three things a slot knows that the file does not. They are what the
 * page draws its channel row from, and what the desktop has been able to say
 * for twenty years: which file this is, whether it has been edited since it
 * was read, and which load it is.
 */
if (json !== '')
{
    const slot = JSON.parse(json);

    check(slot.name === 'leads/SuperRes.patch',
          'the slot is named by the file it came from', `got ${slot.name}`);
    check(slot.dirty === false, 'and a patch just read is not edited');
    check(slot.generation > 0, 'and carries a generation',
          `got ${slot.generation}`);

    /* A chanarg edit is what makes it dirty, and it arrives the way every
       edit on the page does: as a command, applied through tw_panel_edit. */
    const edited = M.ccall('tw_panel_edit', 'number',
                           ['number', 'number', 'number', 'string', 'string'],
                           [0, 0, 0, 'cutoff', '2.0']);

    check(edited === 1, 'moving a control is an edit',
          M.ccall('tw_panel_why', 'string', [], []));
    check(M.ccall('tw_patch_dirty', 'number', ['number'], [0]) === 1,
          'and the channel says its patch has been edited');

    const after = JSON.parse(
        M.ccall('tw_patch_json', 'string', ['number'], [0]));

    check(after.dirty === true, 'which is in the row the page draws');
    check(after.generation === slot.generation,
          'and editing one is not loading another');
}

/* ---- and back out ---- */

/* The bytes a Save would write, which is the first time a browser can save a
 * patch at all. They are the bytes the desktop writes -- both halves are
 * shared -- so what is asserted here is that they come back in: compose,
 * read, and the document is the one the channel holds, edit and all.
 */
{
    const text = M.ccall('tw_patch_compose', 'string', ['number', 'string'],
                         [0, 'Wed Jan  1 00:00:00 2025']);

    check(text.startsWith('# Thinksynth Patch File\n'),
          'a channel composes back into a .patch');

    const back = M.ccall('tw_patch_read', 'string', ['string'], [text]);

    check(back !== '', 'which reads as a patch');

    if (back !== '')
    {
        const doc = JSON.parse(back);
        const now = JSON.parse(
            M.ccall('tw_patch_json', 'string', ['number'], [0]));

        check(doc.dsp === now.dsp, 'naming the graph the channel is playing',
              `got ${doc.dsp}`);

        /* The edit above, not the 1.040810 the file gave: what a Save is for
           is writing down what somebody has moved. */
        check(doc.args.cutoff?.[0] === 2, 'with the value that was typed, ' +
              'not the one the file gave', `got ${doc.args.cutoff}`);
    }

    /* And a page that kept them says so, which is what puts the mark out. */
    check(M.ccall('tw_patch_saved', 'number', ['number', 'string'],
                  [0, 'mine.patch']) === 1, 'a patch can be marked saved');
    check(M.ccall('tw_patch_dirty', 'number', ['number'], [0]) === 0,
          'and is no longer edited');
    check(JSON.parse(M.ccall('tw_patch_json', 'string', ['number'], [0]))
              .name === 'mine.patch',
          'under the name it was saved as');

    check(M.ccall('tw_patch_compose', 'string', ['number', 'string'],
                  [9, '']) === '',
          'a channel with nothing on it composes nothing');
}

/* A graph nothing has handed over. The refusal is the point, and so is what
   it leaves behind: the channel is not touched, so whatever was playing is
   still playing and the patch that is recorded there is still the last one
   that loaded. */
check(!apply(0, 'dsp nosuchgraph.dsp\n'),
      'a patch naming a graph the module does not have is refused');

check(M.ccall('tw_patch_why', 'string', [], []) !== '', 'and says why');

const untouched = M.ccall('tw_patch_json', 'string', ['number'], [0]);

check(untouched === M.ccall('tw_patch_json', 'string', ['number'], [0]) &&
      JSON.parse(untouched).generation === JSON.parse(json).generation,
      'and leaves the channel as it was');

/* Text that is not a patch at all reaches here as a refusal and not as a
   half-loaded channel. */
check(!apply(1, 'this is not a patch\n'),
      'text that names no graph is refused');

check(M.ccall('tw_patch_why', 'string', [], []) === 'names no dsp',
      'with the reading\'s own words');

process.stdout.write(`\n${failures} failure${failures === 1 ? '' : 's'}\n`);

if (failures > 0 && log.length > 0)
    process.stdout.write(`\nthe module said:\n${log.join('\n')}\n`);

process.exit(failures);
