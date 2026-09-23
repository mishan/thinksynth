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
 * file in the build directory; and the solo page's code editor, as another.
 *
 *   node wasm/web/bundle.mjs BUILD_DIR
 *
 * Run by the CMake build after the wasm link. The solo page has no
 * dependencies and is copied as it is; the room page has an editor, a
 * CRDT and a provider, which come from node_modules and go nowhere a
 * browser could fetch them from, so they are bundled into jam.js with
 * everything they touch. The worklet imports only the module and tape.js
 * and is not bundled.
 *
 * The solo page stays a set of modules loaded as they are, except
 * sourcebox.js: its editor is CodeMirror, from the same node_modules, so
 * it is bundled under its own name and main.js imports it as a module
 * like any other. Each output carries its own copy of CodeMirror; the two
 * pages never load both.
 *
 * Four imports have to be pointed somewhere else. The tape reader, the
 * cairo stand-in's replayer and the tiler live outside this directory --
 * the first in wasm/, shared with the Node host, the others in the
 * cairo-canvas2d and mullion packages, which have no knowledge of this
 * tree at all -- and the build copies each in beside the pages under the
 * name the browser loads it by. A module that is loaded both ways, bundled here and fetched there,
 * therefore imports the copied name; the plugin below is how the bundler
 * finds the original.
 */

import { createRequire } from 'node:module';
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

/* The packages are resolved rather than spelled out, so that the paths
   stay npm's business and not a guess about where node_modules is. */
const require = createRequire(import.meta.url);
const COPIED_IN = {
    './replay.js': require.resolve('cairo-canvas2d'),
    './panes.js': require.resolve('mullion'),
    './popover.js': require.resolve('mullion/popover.js'),
    './tape.js': path.join(here, '..', 'tape.mjs'),
};

const copiedIn = {
    name: 'copied-in',
    setup (build)
    {
        build.onResolve({ filter: /^\.\/(replay|panes|popover|tape)\.js$/ }, (args) =>
            ({ path: COPIED_IN[args.path] }));
    },
};

await esbuild.build({
    entryPoints: [path.join(here, 'jam.js'), path.join(here, 'sourcebox.js')],
    plugins: [copiedIn],
    bundle: true,

    /* Emscripten's glue stays a file of its own: it is generated, it
       finds its .wasm from its own URL, and the node editor imports it at
       runtime for the page's own instance of the module (nodeview.js). */
    external: ['./thinkweb.js'],
    format: 'esm',
    target: ['es2022'],
    sourcemap: true,
    outdir: out,
    logLevel: 'warning',
});
