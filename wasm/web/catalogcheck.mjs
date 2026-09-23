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
 * catalogcheck.mjs -- the panes each page declares, against the panes each
 * page lists.
 *
 *   node wasm/web/catalogcheck.mjs
 *
 * A tiled layout is written down in two places on purpose: the markup says
 * what a pane is -- its element, its title, how narrow it may be made --
 * and the page's own script says which of them this page has and where
 * they go to begin with. Neither can be derived from the
 * other, so both are written by hand, and what nothing catches by itself
 * is the two drifting apart: a pane marked in the HTML
 * and listed by nobody shows up nowhere, and a name listed by a page and
 * marked in no element is a pane that quietly never appears.
 *
 * And the defaults, which are the other half of the same drift: a layout
 * naming a pane that is not in the catalog shows an empty box, and one
 * whose columns come to more than the window it is offered in overflows
 * the moment somebody opens the page. Both are arithmetic over what the
 * markup said, so both are done here.
 *
 * No browser and no build. It reads the two pages and the two scripts as
 * text, which is all it takes and is why this can be a gate on every
 * push rather than something the browser jobs get round to.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));

let failures = 0;

const check = (cond, what) =>
{
    process.stdout.write(`${cond ? 'ok   ' : 'FAIL '} ${what}\n`);

    if (!cond)
        failures++;
};

/* ---- reading the markup ---- */

/* Every element in a page marked `data-pane', as what the tiler will make
 * of it: its id, the title its tab will carry and the width below which a
 * divider will not go.
 *
 * A tag scan rather than a parse. The markup being read is this tree's
 * own two pages, the attributes are on the opening tag, and a parser for
 * it would be a dependency this directory does not have -- package.json
 * says the devDependencies are the tests' browsers and the relay's, and
 * one HTML parser for one check is not worth becoming the exception.
 */
function declared (html)
{
    const panes = new Map();
    const tags = /<([a-z][a-z0-9]*)\s([^>]*?\sdata-pane(?:\s|=|>)[^>]*?)>/gis;

    for (const m of html.matchAll(tags))
    {
        const [, tag, attrs] = m;
        const attr = (name) =>
        {
            const a = new RegExp(`\\b${name}="([^"]*)"`, 'i').exec(attrs);

            return a === null ? null : a[1];
        };

        /* A <details>'s summary is its title unless the attribute says
           otherwise -- it is already written and already right. */
        const summary = tag.toLowerCase() === 'details'
            ? /<summary[^>]*>([\s\S]*?)<\/summary>/i.exec(
                  html.slice(m.index))
            : null;

        panes.set(attr('id'), {
            id: attr('id'),
            title: attr('data-pane-title') ?? summary?.[1].trim() ?? null,
            min: attr('data-pane-min'),
        });
    }

    return panes;
}

/* ---- reading the script ---- */

/* The value of `const NAME = <literal>;' in a page's script.
 *
 * The catalog and the default layouts are data -- a list of ids, a tree of
 * fractions -- and they live in the page they belong to rather than in
 * mullion, which ships no opinion about where the keyboard goes. So they
 * are read out of the source here: the initializer is found by balancing
 * its brackets and evaluated with nothing at all in scope, which is safe
 * for a literal and fails loudly for anything that is not one.
 */
function literal (source, name)
{
    const at = source.search(new RegExp(`^const ${name} =`, 'm'));

    if (at < 0)
        return undefined;

    const from = source.indexOf('=', at) + 1;
    let depth = 0;

    for (let i = from; i < source.length; i++)
    {
        if ('[{('.includes(source[i]))
            depth++;
        else if (']})'.includes(source[i]))
            depth--;

        if (depth === 0 && source[i] === ';')
            return vm.runInNewContext(`(${source.slice(from, i)})`);
    }

    return undefined;
}

/* ---- what a default layout comes to ---- */

/* A divider's thickness and a leaf's floor, mullion's numbers, and the
   width the tiler turns itself on at. A default that cannot be laid out
   at that width is a default nobody at the threshold can use. */
const SPLIT = 6;
const LEAF = 64;
const THRESHOLD = 60 * 16;

const isLeaf = (node) => Array.isArray(node.tabs);

/* Every pane a layout names. */
function named (node, into = [])
{
    if (isLeaf(node))
        into.push(...node.tabs);
    else
        node.kids.forEach((k) => named(k, into));

    return into;
}

/* How narrow it may be made: a pane's own minimum, a row's the sum of
   its children's with the dividers between them, a column's the widest
   of them -- and the same the other way up, where a leaf asks for a
   header and a line. mullion does this arithmetic to refuse a drag; it
   is done here to refuse a default. */
function narrowest (node, panes, row)
{
    if (isLeaf(node))
        return row
            ? Math.max(...node.tabs.map((id) => Number(panes.get(id)?.min)))
            : LEAF;

    const mins = node.kids.map((k) => narrowest(k, panes, row));

    return (node.dir === 'row') === row
        ? mins.reduce((a, b) => a + b, 0) + (node.kids.length - 1) * SPLIT
        : Math.max(...mins);
}

/* ---- the two pages ---- */

const read = (f) => fs.readFileSync(path.join(here, f), 'utf8');

const pages = [
    { what: 'the solo page', html: 'index.html', script: 'main.js',
      list: 'PANES' },
    { what: 'the room page', html: 'jam.html', script: 'jam.js',
      list: 'PANES' },
];

for (const page of pages)
{
    page.panes = declared(read(page.html));
    page.catalog = literal(read(page.script), page.list) ?? [];

    /* What a pane is, in full. A tab with no text on it and a divider
       with no floor to stop at are both things that look like they work
       until somebody drags one. */
    for (const p of page.panes.values())
    {
        check(p.id !== null && p.id !== '',
              `${page.what}: every data-pane has an id`);
        check(p.title !== null && p.title !== '',
              `${page.what}: ${p.id} has a title`);
        check(p.min !== null && Number(p.min) > 0,
              `${page.what}: ${p.id} has a minimum width (${p.min})`);
    }

    /* And the two lists, each way round. */
    for (const id of page.catalog)
        check(page.panes.has(id),
              `${page.what}: ${id} is listed and marked data-pane`);

    for (const id of page.panes.keys())
        check(page.catalog.includes(id),
              `${page.what}: ${id} is marked data-pane and listed`);

    check(page.catalog.length > 0,
          `${page.what}: ${page.catalog.length} panes`);

    /* And the layouts this page opens on, one per mode. */
    const source = read(page.script);

    for (const [, name] of source.matchAll(/^const (\w+_LAYOUT) =/gm))
    {
        const layout = literal(source, name);
        const ids = named(layout);

        check(ids.every((id) => page.catalog.includes(id)),
              `${page.what}: ${name} names only panes this page has`);

        if (!ids.every((id) => page.panes.has(id)))
            continue;

        const across = narrowest(layout, page.panes, true);

        check(across <= THRESHOLD,
              `${page.what}: ${name} lays out in ${across} pixels, which ` +
              `the ${THRESHOLD} the tiler turns on at has room for`);
    }
}

/* ---- and what the two share ---- */

/* The same pane on both pages is the same pane: the markup is written
 * twice because there are two documents, and a minimum that drifted would
 * mean the graph could be squeezed to 300 pixels in a room and not on
 * your own. The titles are each page's own -- the composer view is the
 * piece on one and the composers on the other, and those are both right.
 */
const [solo, jam] = pages;

for (const [id, p] of solo.panes)
{
    const q = jam.panes.get(id);

    if (q === undefined)
        continue;

    check(p.min === q.min,
          `both pages ask ${id} for at least ${p.min} pixels`);
}

process.stdout.write(`\n${failures === 0
    ? 'the two pages\' panes are declared and listed the same way\n'
    : `${failures} failed\n`}`);
process.exitCode = failures;
