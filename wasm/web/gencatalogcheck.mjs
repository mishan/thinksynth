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
 * gencatalogcheck.mjs -- the piece menu, held against the Composer's Open.
 *
 *   node wasm/web/gencatalogcheck.mjs [BUILD_DIR] [NATIVE_BUILD_DIR]
 *
 * dspcatalogcheck.mjs's argument, for the other corpus. Every shipped .gen
 * declares a title, a description and the section it belongs to, and both
 * shells read them the same way -- through thcGenEdit, the .gen reader the
 * Composer edits pieces with (src/GenCatalog.h). The value of reading them
 * once is that the two readings are of the same thing, and a rule that
 * quietly depended on the order a directory was walked in would diverge
 * without either side noticing: a Linux directory and a MEMFS one hand their
 * entries over in different orders.
 *
 * So scripts/gencheck --json scans gen/ natively and prints the catalog; this
 * hands the module the same files the page hands it, asks it to scan its own
 * copy, and compares byte for byte.
 *
 * And the one thing a dump cannot say: that every shipped piece is filed
 * under one of gen/README.md's nine sections. scripts/gencheck fails a piece
 * that is not, which is the gate; this asks the page's own copy of the corpus
 * the same question, because what the page serves is a separate tree from
 * what the harness swept.
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

const harness = path.join(native, 'scripts', 'gencheck');

/* gen/README.md's sections, which are what scripts/gencheck holds the corpus
   to. Written out again here rather than parsed out of the harness: a list
   the gate agrees with by construction would not be a second opinion. */
const SECTIONS = [
    'Start here', 'Playing it yourself', 'Algorithms', 'Timbre as material',
    'Pieces', 'Game music', 'The floor', 'The eighties', 'Disco',
];

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
        `gencatalogcheck: no gencheck in ${native}; build the native tree ` +
        'first -- it is what the module is compared against.\n');
    process.exit(1);
}

/* The site's own copy of the corpus: what the page serves and what the
   module is about to be handed. The native side is pointed at the same
   directory, so a difference is about the reading and not about the bytes. */
const genDir = path.join(build, 'gen');
const names = JSON.parse(fs.readFileSync(path.join(genDir, 'index.json'),
                                         'utf8'));

const wanted = execFileSync(harness, ['--json', genDir],
                            { encoding: 'utf8' }).trim();

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

const log = [];
const M = await createThinkWeb({
    print: (s) => log.push(s),
    printErr: (s) => log.push(s),
});

/* A synth, because tw_gen_file writes into MEMFS and nothing renders here. */
M._tw_create(48000, 256, 128);

for (const name of names)
    M.ccall('tw_gen_file', 'number', ['string', 'string'],
            [name, fs.readFileSync(path.join(genDir, name), 'utf8')]);

const got = M.UTF8ToString(M.ccall('tw_gens_json', 'number', [], []));

check(got === wanted, 'the module\'s piece list is the Composer\'s',
      got === wanted ? '' : firstDifference(got, wanted));

function firstDifference (a, b)
{
    for (let i = 0; i < Math.max(a.length, b.length); i++)
        if (a[i] !== b[i])
            return `at ${i}: got ...${a.slice(Math.max(0, i - 40), i + 40)}` +
                   `... wanted ...${b.slice(Math.max(0, i - 40), i + 40)}...`;

    return 'lengths differ';
}

const catalog = JSON.parse(got);
const entries = catalog.groups.flatMap((g) => g.entries);

check(entries.length === names.length, 'every shipped piece has a row',
      `${entries.length} rows, ${names.length} names in the index`);

for (const e of entries)
{
    if (e.name === '')
        fail(e.file, 'no title to draw');

    if (!SECTIONS.includes(e.category))
        fail(e.file, `filed under '${e.category}', which is no section of ` +
                     'gen/README.md');
}

ok(`${entries.length} pieces, ${catalog.groups.length} sections`);

process.exit(failures);
