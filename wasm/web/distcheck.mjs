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
 * distcheck.mjs -- the packaged site has what it says it has.
 *
 *   node wasm/web/distcheck.mjs [DIST_DIR]
 *
 * Every other gate in this directory runs out of the build tree, which is
 * where cmake put every file it generated. The thing a person loads is the
 * install of that tree -- `cmake --build build-web --target dist' -- and an
 * install rule is a second, hand-kept list of what the site is made of. The
 * two lists can disagree, and when they do nothing in the build says so.
 *
 * dsp/samples/*.wav is how that was found: they were copied into the build
 * tree, named in dsp/index.json like every other file, and had no install
 * rule -- so the dist carried an index promising seven wavs and no wavs.
 * The page asks for what the index names, and `fetch' does not reject on a
 * 404: the response arrives, `arrayBuffer()' hands back the error page's
 * bytes, and osc::sample is given something that is not a RIFF. No
 * exception, no console error -- just no drums, on a page whose synthesized
 * instruments all still worked.
 *
 * So: every name in every index resolves to a file that is actually here,
 * and the files the page hard-codes are here too. Nothing is rendered and
 * nothing is played; this is about what the server would answer.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const dist = path.resolve(process.argv[2] ??
                          path.join(here, '..', '..', 'build-web', 'dist'));

let failures = 0;

const fail = (what) =>
{
    process.stdout.write(`FAIL  ${what}\n`);
    failures++;
};

const ok = (what) =>
{
    process.stdout.write(`ok    ${what}\n`);
};

if (!fs.existsSync(dist))
{
    process.stdout.write(
        `FAIL  no dist at ${dist}\n` +
        `      build one: cmake --build build-web --target dist\n`);
    process.exit(1);
}

/* A file, with something in it. An install that made the directory and
   copied nothing would otherwise pass the existence test. */
const present = (rel) =>
{
    try
    {
        return fs.statSync(path.join(dist, rel)).size > 0;
    }
    catch
    {
        return false;
    }
};

/* ---- the indexes ------------------------------------------------------
 *
 * Each is a flat array of names the page joins to its own directory:
 * main.js fetches `dsp/${name}', `gen/${name}', and patch.js the same
 * under patches/. The name in the index is therefore the path on the
 * server, subdirectory and all -- `fx/echo.dsp', `samples/snare.wav'.
 */
for (const dir of ['dsp', 'gen', 'patches'])
{
    const index = path.join(dist, dir, 'index.json');
    let names;

    try
    {
        names = JSON.parse(fs.readFileSync(index, 'utf8'));
    }
    catch (e)
    {
        fail(`${dir}/index.json does not read: ${e.message}`);
        continue;
    }

    if (!Array.isArray(names) || names.length === 0)
    {
        fail(`${dir}/index.json is not a list of names`);
        continue;
    }

    const missing = names.filter((n) => !present(path.join(dir, n)));

    if (missing.length)
        fail(`${dir}/index.json names ${missing.length} file(s) the dist ` +
             `does not have: ${missing.slice(0, 8).join(', ')}` +
             (missing.length > 8 ? ', ...' : ''));
    else
        ok(`${dir}/index.json: all ${names.length} named file(s) are here`);
}

/* ---- and what the page asks for without being told ---------------------
 *
 * The module, the worklet and the page itself are named in the HTML and in
 * import statements rather than in an index, so no loop above reaches them.
 * A dist missing one of these is a blank page rather than a quiet gap, but
 * the check costs a stat.
 */
const ROOT = ['index.html', 'main.js', 'worklet.js', 'host.js', 'engine.js',
              'thinkweb.js', 'thinkweb.wasm', 'config.json', 'style.css',
              'patch.js', 'jam.html', 'jam.js'];

const missingRoot = ROOT.filter((n) => !present(n));

if (missingRoot.length)
    fail(`the dist is missing ${missingRoot.join(', ')}`);
else
    ok(`the page's own ${ROOT.length} file(s) are here`);

/* ---- the kit, specifically --------------------------------------------
 *
 * The case that was actually broken, asserted as itself rather than only
 * as a consequence of the index loop: a dist with no dsp/samples in it is
 * one where dsp/linn.dsp and dsp/orchhit.dsp play nothing, and gen/
 * acetate.gen is two thirds silent.
 */
{
    const kit = fs.existsSync(path.join(dist, 'dsp', 'samples'))
        ? fs.readdirSync(path.join(dist, 'dsp', 'samples'))
              .filter((n) => n.endsWith('.wav'))
        : [];

    if (kit.length === 0)
        fail('dsp/samples/ has no wavs in it, so every sampled instrument ' +
             'is silent');
    else
        ok(`dsp/samples/: ${kit.length} wav(s), so the sampled ` +
           `instruments have something to play`);
}

process.stdout.write(`\n${failures} failure(s)\n`);
process.exit(failures);
