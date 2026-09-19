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
 * doc.js -- the shared document's shape.
 *
 * One Y.Doc per room:
 *
 *   files: Y.Map<string, Y.Text>    "airports.gen", "amb01.dsp", ...
 *   meta:  Y.Map                    { piece: "airports.gen",
 *                                     seeded_from: "gen/airports.gen" }
 *
 * The piece and every .dsp it names are files in the map, by the name the
 * .gen uses, and a loader resolves `dsp "amb01.dsp"' against the map and
 * nowhere else: a piece that names a .dsp the map lacks fails on every
 * peer alike. Nothing here touches a file system or a socket -- the relay
 * seeds a room with this, the page reads and edits with this, and the
 * hash both agree on is here too.
 */

import * as Y from 'yjs';

export const DEFAULT_PIECE = 'airports.gen';

export function files (doc)
{
    return doc.getMap('files');
}

export function meta (doc)
{
    return doc.getMap('meta');
}

/* The file names, in the order everything else here uses: the map's
   keys sorted, since a Y.Map has no order of its own. */
export function fileNames (doc)
{
    return [...files(doc).keys()].sort();
}

export function readFile (doc, name)
{
    const text = files(doc).get(name);

    return text === undefined ? null : text.toString();
}

/* A file, whole: what a seed writes and what a reload of a shipped piece
   would write. An edit goes through the editor's binding, not this. */
export function putFile (doc, name, text)
{
    doc.transact(() =>
    {
        let t = files(doc).get(name);

        if (t === undefined)
        {
            t = new Y.Text();
            files(doc).set(name, t);
        }

        if (t.length > 0)
            t.delete(0, t.length);

        t.insert(0, text);
    });
}

/* An edit to a file, as the smallest splice that turns what is there into
 * `next': one delete and one insert, in the middle.
 *
 * What the node editor's edits go through. NodeEdit changes a line or
 * two of a .dsp and copies every other byte through, so the difference
 * is small and local, and a splice is what lets somebody else be typing
 * in the same file at the same time -- a whole-file write would take
 * their cursor with it, and would lose their character if it landed
 * between the read and the write.
 *
 * Computed inside the transaction, against the text as it stands at that
 * moment rather than against whatever the caller last read. That is the
 * one thing that makes it safe: a splice worked out from a stale text
 * deletes the wrong range (section 12.5).
 *
 * Returns the number of characters replaced, or -1 if there is no such
 * file.
 */
export function spliceFile (doc, name, next)
{
    let replaced = -1;

    doc.transact(() =>
    {
        const t = files(doc).get(name);

        if (t === undefined)
            return;

        const was = t.toString();

        if (was === next)
        {
            replaced = 0;
            return;
        }

        /* The common ends, in characters. Y.Text counts in the same units
           as a JavaScript string, so these indices are its. */
        let head = 0;

        while (head < was.length && head < next.length &&
               was[head] === next[head])
            head++;

        let tail = 0;

        while (tail < was.length - head && tail < next.length - head &&
               was[was.length - 1 - tail] === next[next.length - 1 - tail])
            tail++;

        replaced = was.length - head - tail;

        if (replaced > 0)
            t.delete(head, replaced);

        const inserted = next.slice(head, next.length - tail);

        if (inserted.length > 0)
            t.insert(head, inserted);
    });

    return replaced;
}

/* The piece's text, or null when the room has none. */
export function pieceName (doc)
{
    return meta(doc).get('piece') ?? null;
}

export function pieceText (doc)
{
    const name = pieceName(doc);

    return name === null ? null : readFile(doc, name);
}

/* The .dsp files a .gen names: `dsp "amb01.dsp";' in an instrument block
   and `effect "fx/echo.dsp"' inside it (docs/GEN_FORMAT.md). An effect is a
   file the loader looks up exactly as it looks up an instrument, so a room
   that carried the one and not the other would fail every peer's load. */
export function dspNames (genText)
{
    const names = new Set();

    for (const m of genText.matchAll(/\b(?:dsp|effect)\s+"([^"]+)"/g))
        names.add(m[1]);

    return [...names];
}

/* The .dsp files in the document, by name, as a load hands them to the
   worklet. */
export function instrumentTexts (doc)
{
    const out = {};

    for (const name of fileNames(doc))
        if (name.endsWith('.dsp'))
            out[name] = readFile(doc, name);

    return out;
}

/* The revision Apply names: SHA-256 over every file, in name order, each
   as its name, a NUL, its text, a NUL. A Yjs document has no revision
   number, and a load must load the same text on every peer. Hex. */
export async function hashOf (doc)
{
    const parts = [];

    for (const name of fileNames(doc))
        parts.push(name, '\0', readFile(doc, name), '\0');

    const bytes = new TextEncoder().encode(parts.join(''));
    const digest = await crypto.subtle.digest('SHA-256', bytes);

    return [...new Uint8Array(digest)]
        .map((b) => b.toString(16).padStart(2, '0')).join('');
}
