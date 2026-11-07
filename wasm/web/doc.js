/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

/* The files the piece uses: itself and every .dsp it names that the map
   has, in fileNames' order. What a person here is shown -- a file the .gen
   has stopped naming stays in the document, which is everybody's, until a
   switch takes it out (relay.mjs, seedFiles), but it is no longer anything
   the piece plays. */
export function pieceFiles (doc)
{
    const gen = pieceText(doc);

    if (gen === null)
        return fileNames(doc);

    const used = new Set([pieceName(doc), ...dspNames(gen)]);

    return fileNames(doc).filter((name) => used.has(name));
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

/* Every file and which one is the piece, as plain text: what the relay
   keeps of the revision a start named, for whoever joins while it plays.
   The document itself moves on -- a param edit is spliced into it by the
   peer that made it -- and a late joiner has to load what the others
   loaded, not what is there now. */
export function snapshot (doc)
{
    const out = { piece: pieceName(doc), files: {} };

    for (const name of fileNames(doc))
        out.files[name] = readFile(doc, name);

    return out;
}

/* And back: a document of its own holding a snapshot, for the functions
   above to read. */
export function docOf ({ piece, files: texts })
{
    const doc = new Y.Doc();

    doc.transact(() =>
    {
        for (const [name, text] of Object.entries(texts))
            putFile(doc, name, text);

        meta(doc).set('piece', piece);
    });

    return doc;
}

/* The longest `seen' a start carries, in characters: a document edited
   for long enough has deleted in enough places to pass it, and its starts
   then carry none and wait for their revision as a start always did. */
export const SEEN_MAX = 64 * 1024;

/* Where the document is, as a Yjs snapshot -- what each writer has written
   and what has been deleted -- in base64: what a start carries beside its
   hash. Both halves, since a delete moves no writer's clock. Undefined
   past SEEN_MAX. */
export function seenOf (doc)
{
    const bytes = Y.encodeSnapshot(Y.snapshot(doc));
    let text = '';

    for (let i = 0; i < bytes.length; i += 0x8000)
        text += String.fromCharCode(...bytes.subarray(i, i + 0x8000));

    const seen = btoa(text);

    return seen.length <= SEEN_MAX ? seen : undefined;
}

/* A start's `seen', read once: null if it is not one. */
export function readSeen (seen)
{
    if (typeof seen !== 'string' || seen.length > SEEN_MAX)
        return null;

    try
    {
        return Y.decodeSnapshot(
            Uint8Array.from(atob(seen), (c) => c.charCodeAt(0)));
    }
    catch
    {
        return null;
    }
}

/* Whether `doc' has everything a snapshot (readSeen) has: every write and
   every delete. One that has, and does not hash to the snapshot's
   revision, has gone past it and will never come back to it. */
export function hasSeen (doc, snap)
{
    const sv = Y.decodeStateVector(Y.encodeStateVector(doc));

    for (const [client, clock] of snap.sv)
        if ((sv.get(client) ?? 0) < clock)
            return false;

    const ds = Y.createDeleteSetFromStructStore(doc.store);

    for (const [client, deletes] of snap.ds.clients)
    {
        const have = ds.clients.get(client) ?? [];

        for (const d of deletes)
            if (!have.some((h) => h.clock <= d.clock &&
                                  d.clock + d.len <= h.clock + h.len))
                return false;
    }

    return true;
}

/* The revision Apply names: SHA-256 over every file, in name order, each
   as its name, a NUL, its text, a NUL. A Yjs document has no revision
   number, and a load must load the same text on every peer. Hex. */
export async function hashOf (doc)
{
    return hashOfFiles(snapshot(doc).files);
}

/* The same, of a snapshot's files: what the relay hashes, so that the text
   it hands a joiner is the text it hashed, however the document moves while
   the digest is worked out. */
export async function hashOfFiles (texts)
{
    const parts = [];

    for (const name of Object.keys(texts).sort())
        parts.push(name, '\0', texts[name], '\0');

    const bytes = new TextEncoder().encode(parts.join(''));
    const digest = await crypto.subtle.digest('SHA-256', bytes);

    return [...new Uint8Array(digest)]
        .map((b) => b.toString(16).padStart(2, '0')).join('');
}
