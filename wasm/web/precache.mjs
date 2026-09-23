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
 * precache.mjs -- sw.js, out of serviceworker.js and the files it keeps.
 *
 *   node wasm/web/precache.mjs BUILD_DIR TEMPLATE LIST
 *
 * Run by the CMake build once everything in LIST exists. LIST is the
 * files the solo page can load, one absolute path to a line, each inside
 * BUILD_DIR; CMake writes it, since CMake is what knows the site's files.
 * What comes out is BUILD_DIR/sw.js: two constants, then TEMPLATE.
 *
 *   FILES     each path relative to BUILD_DIR, which is to sw.js
 *   VERSION   the first 16 hex digits of a sha256 over every file's name
 *             and contents
 *
 * VERSION is over the contents rather than a commit or a date, so a build
 * that changes no file the page loads writes the same sw.js, and a browser
 * holding that version has nothing to fetch again.
 */

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const [build, template, list] = process.argv.slice(2);

if (list === undefined)
{
    process.stderr.write('usage: precache.mjs BUILD_DIR TEMPLATE LIST\n');
    process.exit(2);
}

const root = path.resolve(build);
const files = fs.readFileSync(list, 'utf8').split('\n')
    .filter((line) => line !== '')
    .map((abs) => path.relative(root, abs).split(path.sep).join('/'))
    .sort();

const hash = crypto.createHash('sha256');

for (const rel of files)
{
    if (rel.startsWith('..'))
    {
        process.stderr.write(`precache.mjs: ${rel} is outside ${root}\n`);
        process.exit(1);
    }

    /* The name as well as the bytes, and a NUL after each: two files
       swapping contents, or one moving, is a different site. */
    hash.update(rel).update('\0');
    hash.update(fs.readFileSync(path.join(root, rel))).update('\0');
}

const version = hash.digest('hex').slice(0, 16);

fs.writeFileSync(path.join(root, 'sw.js'),
    `const VERSION = ${JSON.stringify(version)};\n` +
    `const FILES = ${JSON.stringify(files)};\n\n` +
    fs.readFileSync(template, 'utf8'));
