#!/usr/bin/env node
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
 * dspcatalogcheck.mjs -- the list the page's menus are drawn from, held
 * against the list the desktop's chooser is drawn from.
 *
 *   node wasm/web/dspcatalogcheck.mjs [BUILD_DIR] [NATIVE_BUILD_DIR]
 *
 * Every shipped .dsp declares a title and a description, and both shells now
 * read them the same way (src/DspCatalog.h). The whole value of reading them
 * once is that the two readings are of the same thing, and nothing enforces
 * that by itself: the two builds compile the same source, and a rule that
 * quietly depended on the order a directory was walked in would diverge
 * without either side noticing -- a Linux directory and a MEMFS one hand
 * their entries over in different orders.
 *
 * So scripts/dspcatalog --json scans dsp/ natively and prints the catalog;
 * this hands the module the same files the page hands it, asks the module to
 * scan its own MEMFS copy, and compares the two dumps byte for byte. The
 * grouping is in the dump, so a graph filed differently on one side fails
 * here rather than in somebody's menu.
 *
 * And the one thing a dump cannot say: that the corpus and the catalog agree
 * about which graphs are effects. The tree keeps its effect graphs in dsp/fx/
 * and everything that names one spells the directory (`effect "fx/echo.dsp"'),
 * so the directory is an independent fact about the same files -- and the
 * scan, which never looks at it, has to match it.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.join(here, '..', '..');
const build = path.resolve(process.argv[2] ?? path.join(top, 'build-web'));
const native = path.resolve(process.argv[3] ?? path.join(top, 'build'));

const harness = path.join(native, 'scripts', 'dspcatalog');

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
        `dspcatalogcheck: no dspcatalog in ${native}; build the native tree ` +
        'first -- it is what the module is compared against.\n');
    process.exit(1);
}

/* The site's own copy of the corpus, which is what the page serves and what
   the module is about to be handed. The native side is pointed at the same
   directory rather than at the source tree, so the two are scanning the same
   bytes and a difference is about the reading. */
const dspDir = path.join(build, 'dsp');
const names = JSON.parse(fs.readFileSync(path.join(dspDir, 'index.json'),
                                         'utf8'));

const wanted = execFileSync(harness, ['--json', dspDir],
                            { encoding: 'utf8' }).trim();

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

const log = [];
const M = await createThinkWeb({
    print: (s) => log.push(s),
    printErr: (s) => log.push(s),
});

/* The synth the page makes, which is also what makes /dsp: tw_instrument
   writes into a directory tw_create mkdirs. Nothing here renders. */
M._tw_create(48000, 256, 128);

/* Exactly what main.js does before it draws a menu: every text in the index
   handed over under the name it is known by. The wavs are not text and are
   not graphs, and the catalog skips them by extension -- they go in anyway,
   because the page hands them over too and a scan that tripped over one
   would be worth finding here. */
for (const name of names)
{
    const file = path.join(dspDir, name);

    if (name.endsWith('.wav'))
    {
        const bytes = new Uint8Array(fs.readFileSync(file));

        M.ccall('tw_sample', 'number', ['string', 'array', 'number'],
                [name, bytes, bytes.length]);
    }
    else
        M.ccall('tw_instrument', 'number', ['string', 'string'],
                [name, fs.readFileSync(file, 'utf8')]);
}

const got = M.UTF8ToString(M.ccall('tw_dsps_json', 'number', [], []));

check(got === wanted, 'the module\'s catalog is the desktop\'s',
      got === wanted ? '' : firstDifference(got, wanted));

function firstDifference (a, b)
{
    for (let i = 0; i < Math.max(a.length, b.length); i++)
        if (a[i] !== b[i])
            return `at ${i}: got ...${a.slice(Math.max(0, i - 40), i + 40)}` +
                   `... wanted ...${b.slice(Math.max(0, i - 40), i + 40)}...`;

    return 'lengths differ';
}

/* The catalog against the corpus. Both sides agree by now, so this is asked
   of one of them. */
const catalog = JSON.parse(got);
const entries = catalog.groups.flatMap((g) => g.entries);

check(entries.length === names.filter((n) => n.endsWith('.dsp')).length,
      'every shipped graph has a row',
      `${entries.length} rows, ${names.length} names in the index`);

for (const e of entries)
{
    if (e.name === '')
        fail(e.file, 'no title to draw');

    if (e.effect !== e.file.startsWith('fx/'))
        fail(e.file, e.effect ? 'scanned as an effect, is not under fx/'
                              : 'under fx/, did not scan as an effect');
}

ok(`${entries.length} graphs, ${catalog.groups.length} groups`);

process.exit(failures);
