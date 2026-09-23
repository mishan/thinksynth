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

/* A regular file, with something in it. An install that made the directory
   and copied nothing would otherwise pass the existence test -- and so would
   a name that resolved to the directory itself. */
const present = (rel) =>
{
    try
    {
        const st = fs.statSync(path.join(dist, rel));

        return st.isFile() && st.size > 0;
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

/* ---- and what the pages ask for without being told ---------------------
 *
 * The module, the worklet and the page's own scripts are named in the HTML
 * and in import statements rather than in an index, so no loop above
 * reaches them.
 *
 * Walked rather than listed. A list here would be a third hand-kept copy of
 * what the site is made of -- after the install rules and the source list --
 * and the bug this file exists for is two such copies disagreeing. So the
 * .html files are the seed and everything is followed from there: a page's
 * <script src>, a module's imports, the URLs handed to addModule and to
 * new Worker, and the plain-string fetches. Add a module to the page and
 * this finds it without being told.
 *
 * `esm' marks the patterns whose string is an ES module specifier, where a
 * bare name is an npm package the bundler already resolved into the file --
 * not something the server answers for. In a URL or a fetch a bare name is
 * just a relative path, and is.
 */
const REFS = [
    { esm: false,
      re: /<(?:script|link|img)[^>]*?(?:src|href)\s*=\s*["']([^"']+)["']/gi },
    { esm: true,  re: /\bfrom\s*["']([^"']+)["']/g },
    { esm: true,  re: /\bimport\s*\(\s*["']([^"']+)["']\s*\)/g },
    { esm: false,
      re: /\bnew\s+URL\s*\(\s*["']([^"']+)["']\s*,\s*import\.meta\.url\s*\)/g },
    /* Both quotes, because the bundler rewrites the page's own ' to ".
       No ${ or `, so a computed URL -- `dsp/${name}' -- is left alone:
       what those name is in an index, and the loops above have it. */
    { esm: false, re: /\bfetch\s*\(\s*"([^"${}`]+)"\s*\)/g },
    { esm: false, re: /\bfetch\s*\(\s*'([^'${}`]+)'\s*\)/g },
    { esm: false, re: /["']([\w./-]+\.wasm)["']/g },
];

/* Only what this server would have to answer for. An absolute URL belongs
   to somebody else; a bare ES specifier is a package. */
const local = (spec, esm) =>
{
    if (/^[a-z][a-z0-9+.-]*:/i.test(spec) || spec.startsWith('//') ||
        spec.startsWith('#'))
        return false;

    if (esm)
        return spec.startsWith('./') || spec.startsWith('../') ||
               spec.startsWith('/');

    return true;
};

const seeds = fs.existsSync(dist)
    ? fs.readdirSync(dist).filter((n) => n.endsWith('.html'))
    : [];

const seen = new Set(seeds);
const queue = [...seeds];
const broken = [];

while (queue.length)
{
    const from = queue.shift();

    if (!/\.(html|js|mjs)$/.test(from) || !present(from))
        continue;

    const text = fs.readFileSync(path.join(dist, from), 'utf8');

    for (const { re, esm } of REFS)
    {
        re.lastIndex = 0;

        let m;

        while ((m = re.exec(text)) !== null)
        {
            const spec = m[1].split(/[?#]/)[0];

            if (!spec || !local(spec, esm))
                continue;

            /* Relative to the file that named it, then back to a name the
               server would see. */
            const rel = path.posix.normalize(
                path.posix.join(path.posix.dirname(from), spec));

            if (rel.startsWith('..') || seen.has(rel))
                continue;

            seen.add(rel);

            if (present(rel))
                queue.push(rel);
            else
                broken.push(`${rel} (named by ${from})`);
        }
    }
}

/* The walk starts at whatever .html is here, so a dist that lost a page
   loses its subtree from the walk rather than failing it -- the remaining
   pages still resolve, and the count quietly drops. The entry points are
   therefore named: two of them, which is a short enough list to keep by
   hand where the module graph was not. */
const PAGES = ['index.html', 'jam.html'];
const missingPages = PAGES.filter((n) => !present(n));

if (missingPages.length)
    fail(`the dist has no ${missingPages.join(', ')} to load`);
else
    ok(`the ${PAGES.length} page(s) are here`);

if (seeds.length === 0)
    fail('the dist has no .html in it, so there is no page to load');
else if (broken.length)
    fail(`${broken.length} file(s) the pages name are not in the dist: ` +
         broken.slice(0, 8).join(', ') + (broken.length > 8 ? ', ...' : ''));
else
    ok(`the pages and everything they load: ${seen.size} file(s), all here`);

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

/* ---- the offline copy -------------------------------------------------
 *
 * sw.js carries its own list of the site, FILES, and a file left off it
 * is one the page can load online and not offline: a patch picked on a
 * train is a 404 nobody saw on a desk. So the list is held against the
 * whole dist, both ways. What is left off on purpose is config.json, which
 * no build depends on, the room page's source map, and the worker itself.
 *
 * And the manifest's icons, which no page names, so the walk above does
 * not reach them.
 */
{
    const NETWORK = new Set(['jam.js.map', 'config.json', 'sw.js']);
    let files = null;

    try
    {
        const m = /^const FILES = (.*);$/m.exec(
            fs.readFileSync(path.join(dist, 'sw.js'), 'utf8'));

        files = m && JSON.parse(m[1]);
    }
    catch (e)
    {
        fail(`sw.js does not read: ${e.message}`);
    }

    if (files !== null && !Array.isArray(files))
        fail('sw.js has no FILES list');
    else if (files !== null)
    {
        const kept = new Set(files);
        const all = fs.readdirSync(dist, { recursive: true })
            .map((n) => n.split(path.sep).join('/'))
            .filter((n) => fs.statSync(path.join(dist, n)).isFile());
        const gone = files.filter((n) => !present(n));
        const left = all.filter((n) => !kept.has(n) && !NETWORK.has(n));

        if (gone.length)
            fail(`sw.js keeps ${gone.length} file(s) the dist does not ` +
                 `have: ${gone.slice(0, 8).join(', ')}`);
        else if (left.length)
            fail(`sw.js leaves ${left.length} file(s) out, which will not ` +
                 `load offline: ${left.slice(0, 8).join(', ')}`);
        else
            ok(`sw.js keeps all ${files.length} file(s) the pages can ` +
               'load');
    }

    try
    {
        const icons = JSON.parse(fs.readFileSync(
            path.join(dist, 'manifest.json'), 'utf8')).icons ?? [];
        const absent = icons.map((i) => i.src).filter((s) => !present(s));

        if (icons.length === 0)
            fail('manifest.json names no icons, so the page cannot install');
        else if (absent.length)
            fail(`manifest.json names icons the dist does not have: ` +
                 absent.join(', '));
        else
            ok(`manifest.json: its ${icons.length} icon(s) are here`);
    }
    catch (e)
    {
        fail(`manifest.json does not read: ${e.message}`);
    }
}

process.stdout.write(`\n${failures} failure(s)\n`);
process.exit(failures);
