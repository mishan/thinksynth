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
 * bundle.mjs -- the room page's script and everything it imports, as one
 * file in the build directory.
 *
 *   node wasm/web/bundle.mjs BUILD_DIR
 *
 * Run by the CMake build after the wasm link. The solo page has no
 * dependencies and is copied as it is; the room page has an editor, a
 * CRDT and a provider, which come from node_modules and go nowhere a
 * browser could fetch them from, so they are bundled into jam.js with
 * everything they touch. The worklet has no imports beyond tape.mjs and
 * is not bundled.
 */

import path from 'node:path';
import { fileURLToPath } from 'node:url';

import * as esbuild from 'esbuild';

const here = path.dirname(fileURLToPath(import.meta.url));
const out = process.argv[2];

if (out === undefined)
{
    process.stderr.write('usage: bundle.mjs BUILD_DIR\n');
    process.exit(2);
}

await esbuild.build({
    entryPoints: [path.join(here, 'jam.js')],
    bundle: true,
    format: 'esm',
    target: ['es2022'],
    sourcemap: true,
    outfile: path.join(out, 'jam.js'),
    logLevel: 'warning',
});
