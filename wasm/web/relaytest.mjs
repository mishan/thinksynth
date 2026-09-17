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
 * relaytest.mjs -- the relay, driven from Node by the clients the page
 * will be: two room sockets and two document providers in one room.
 *
 *   node wasm/web/relaytest.mjs
 *
 * What the relay promises (JAM_M3.md, section 3), each checked: a hello is
 * answered with who is here and told to the others; a seat is first-claim
 * and released on close; a ping is answered with the relay's clock; a
 * signal reaches the one peer it names and nobody else; a relayed gesture
 * reaches one or everyone; a transport start is kept for a joiner; and
 * the document a room is seeded with reaches both providers, an edit on
 * one reaches the other, and both hash to the same revision.
 *
 * Exit status is the number of failures.
 */

import path from 'node:path';
import { fileURLToPath } from 'node:url';

import WebSocket, { WebSocketServer } from 'ws';
import * as Y from 'yjs';
import { WebsocketProvider } from 'y-websocket';

import { dspNames, fileNames, hashOf, pieceText, readFile } from './doc.js';
import { PROTOCOL, relay } from './relay.mjs';
import { Room } from './room.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const tree = path.join(here, '..', '..');

let failures = 0;

function fail (what)
{
    failures++;
    process.stdout.write(`FAIL  ${what}\n`);
}

function ok (what)
{
    process.stdout.write(`ok    ${what}\n`);
}

function check (cond, what)
{
    if (cond)
        ok(what);
    else
        fail(what);
}

/* A room socket as the page will hold one: every message kept, and a
   way to wait for the next one of a type. */
class Client
{
    constructor (url, name)
    {
        this.ws = new WebSocket(url);
        this.name = name;
        this.got = [];
        this.waiting = [];
        this.ws.on('message', (d) =>
        {
            const m = JSON.parse(d.toString());

            this.got.push(m);

            for (const w of this.waiting.splice(0))
                if (w.type === m.type)
                    w.resolve(m);
                else
                    this.waiting.push(w);
        });
    }

    open ()
    {
        return new Promise((r) => this.ws.on('open', r));
    }

    send (m)
    {
        this.ws.send(JSON.stringify(m));
    }

    /* The next message of a type, already received or yet to come. */
    next (type, ms = 2000)
    {
        const k = this.got.findIndex((m) => m.type === type);

        if (k >= 0)
            return Promise.resolve(this.got.splice(k, 1)[0]);

        return new Promise((resolve, reject) =>
        {
            const timer = setTimeout(
                () => reject(new Error(`${this.name}: no '${type}' in ` +
                                       `${ms} ms`)), ms);

            this.waiting.push({ type, resolve: (m) =>
            {
                clearTimeout(timer);
                this.got.splice(this.got.indexOf(m), 1);
                resolve(m);
            } });
        });
    }

    /* Nothing of a type arrives in a while. */
    async none (type, ms = 300)
    {
        await new Promise((r) => setTimeout(r, ms));

        return !this.got.some((m) => m.type === type);
    }

    close ()
    {
        this.ws.close();
    }
}

const server = await relay({ port: 0, host: '127.0.0.1', tree });
const port = server.address().port;
const base = `ws://127.0.0.1:${port}`;

try
{
    /* ---- the room socket ---- */

    const a = new Client(`${base}/room/test?piece=airports.gen`, 'A');

    await a.open();
    a.send({ type: 'hello', name: 'Ann', protocol: PROTOCOL });

    const wa = await a.next('welcome');

    check(typeof wa.peer === 'string' && wa.peers.length === 1 &&
          wa.peers[0].name === 'Ann' && wa.piece === 'airports.gen' &&
          wa.playing === null,
          'a hello is welcomed with the room as it is');

    const b = new Client(`${base}/room/test`, 'B');

    await b.open();
    b.send({ type: 'hello', name: 'Bo', protocol: PROTOCOL });

    const wb = await b.next('welcome');
    const ja = await a.next('joined');

    check(wb.peers.length === 2 && ja.peer === wb.peer && ja.name === 'Bo',
          'a second peer is told who is here, and the first is told');

    /* Seats: first claim wins. */
    a.send({ type: 'seat', seat: 0 });

    const sa = await a.next('seats');

    check(sa.seats[0] === wa.peer, 'a free seat is claimed');

    await b.next('seats');
    b.send({ type: 'seat', seat: 0 });

    const sb = await b.next('seats');

    check(sb.seats[0] === wa.peer, 'a taken seat stays with its holder');

    b.send({ type: 'seat', seat: 3 });

    const sb2 = await b.next('seats');

    check(sb2.seats[3] === wb.peer && sb2.seats[0] === wa.peer,
          'another seat is free to take');

    await a.next('seats');
    await a.next('seats');

    /* The clock. */
    const t0 = performance.now();

    a.send({ type: 'ping', t0 });

    const pong = await a.next('pong');

    check(pong.t0 === t0 && typeof pong.t1 === 'number' && pong.t1 > 0,
          'a ping is answered with the relay\'s clock');

    /* Signalling: to one peer, opaque. */
    a.send({ type: 'signal', to: wb.peer, data: { sdp: 'offer' } });

    const sig = await b.next('signal');

    check(sig.from === wa.peer && sig.data.sdp === 'offer',
          'a signal reaches the peer it names');
    check(await a.none('signal'), 'and not its sender');

    /* Relayed gestures: to one, and to everyone. */
    b.send({ type: 'relayed', to: wa.peer, data: { type: 'knob' } });

    const rel = await a.next('relayed');

    check(rel.from === wb.peer && rel.data.type === 'knob',
          'a relayed gesture reaches one peer');

    a.send({ type: 'relayed', data: { type: 'note' } });

    const rel2 = await b.next('relayed');

    check(rel2.from === wa.peer && rel2.data.type === 'note',
          'a relayed gesture reaches everyone else');

    /* A transport start is kept for a joiner. */
    a.send({ type: 'transport', data: { op: 'start', origin: 12345 } });

    const tr = await b.next('transport');

    check(tr.from === wa.peer && tr.data.origin === 12345,
          'a transport start reaches the others');

    const c = new Client(`${base}/room/test`, 'C');

    await c.open();
    c.send({ type: 'hello', name: 'Cy', protocol: PROTOCOL });

    const wc = await c.next('welcome');

    check(wc.playing?.origin === 12345 && wc.peers.length === 3 &&
          wc.seats[0] === wa.peer && wc.seats[3] === wb.peer,
          'a joiner is told what is playing and who sits where');

    await a.next('joined');
    await b.next('joined');

    /* And to several peers at once: what a page sends when more than one
       of its channels is down, so the relay serialises the command once
       rather than once per peer. */
    b.send({ type: 'relayed', to: [wa.peer, wc.peer],
             data: { type: 'tempo' } });

    const rel3 = await a.next('relayed');
    const rel4 = await c.next('relayed');

    check(rel3.from === wb.peer && rel3.data.type === 'tempo' &&
          rel4.from === wb.peer && rel4.data.type === 'tempo',
          'a relayed gesture reaches every peer a list names');

    /* Leaving releases the seat. */
    b.close();

    const left = await a.next('left');
    const seats = await a.next('seats');

    check(left.peer === wb.peer && seats.seats[3] === undefined,
          'a peer that leaves gives up its seat');

    /* A wrong protocol is refused. */
    const d = new Client(`${base}/room/test`, 'D');

    await d.open();
    d.send({ type: 'hello', name: 'Di', protocol: PROTOCOL + 1 });

    const err = await d.next('error');

    check(/protocol/.test(err.text), 'a peer speaking another protocol is told');

    a.close();
    c.close();
    d.close();

    /* And the page's side of that: the relay says its piece and closes,
       which is a clean close and fires no `error' event at all. A
       connect() that only rejected from `error' left Join awaiting a
       promise that never settled, with the button disabled. */
    {
        const refuser = new WebSocketServer({ port: 0, host: '127.0.0.1' });

        await new Promise((r) => refuser.on('listening', r));

        refuser.on('connection', (ws) =>
        {
            ws.send(JSON.stringify(
                { type: 'error', text: 'protocol 2; this relay speaks 1' }));
            ws.close();
        });

        let said = null;

        try
        {
            await new Room(`ws://127.0.0.1:${refuser.address().port}`,
                           'test', 'Di').connect();
        }
        catch (e)
        {
            said = e.message;
        }
        finally
        {
            refuser.close();
        }

        check(said !== null && /protocol 2/.test(said),
              'a room socket closed without a welcome rejects the join, ' +
              'with the relay\'s reason');
    }

    /* ---- the document socket ---- */

    const docA = new Y.Doc();
    const docB = new Y.Doc();
    const provA = new WebsocketProvider(base + '/doc', 'test', docA,
                                        { WebSocketPolyfill: WebSocket });
    const provB = new WebsocketProvider(base + '/doc', 'test', docB,
                                        { WebSocketPolyfill: WebSocket });

    const synced = (p) => new Promise((r) =>
        p.synced ? r() : p.once('synced', r));

    await synced(provA);
    await synced(provB);

    const gen = pieceText(docA);
    const named = gen === null ? [] : dspNames(gen);

    check(gen !== null && /^seed 1978;/m.test(gen),
          'the room was seeded with the piece');
    check(named.length > 0 &&
          named.every((n) => readFile(docA, n) !== null),
          `and with every .dsp it names (${named.join(', ')})`);
    check(fileNames(docB).join() === fileNames(docA).join(),
          'a second provider sees the same files');

    /* An edit on one reaches the other. */
    const before = await hashOf(docA);

    docA.getMap('files').get('airports.gen').insert(0, '# edited\n');

    await new Promise((r) => setTimeout(r, 300));

    check(readFile(docB, 'airports.gen').startsWith('# edited\n'),
          'an edit on one peer reaches the other');

    const [ha, hb] = await Promise.all([hashOf(docA), hashOf(docB)]);

    check(ha === hb && ha !== before,
          'both hash the document to the same revision, and it moved');

    /* Awareness: a cursor set on one is seen on the other. */
    provA.awareness.setLocalStateField('user', { name: 'Ann' });

    await new Promise((r) => setTimeout(r, 300));

    const seen = [...provB.awareness.getStates().values()]
        .some((s) => s.user?.name === 'Ann');

    check(seen, 'presence set on one provider is seen on the other');

    /* A malformed document frame costs its own socket and nothing else.
     *
       The bytes on that socket are untrusted and the decoder throws on
       an empty or a truncated one; `ws' emits `message' synchronously,
       so unguarded the exception left the process and took every room,
       document and peer on the relay with it. */
    for (const [what, bytes] of [['an empty', new Uint8Array(0)],
                                 ['a truncated', new Uint8Array([0, 200])],
                                 ['a garbage', new Uint8Array([255, 255, 255,
                                                               255, 255])]])
    {
        const bad = new WebSocket(`${base}/doc/test`);

        await new Promise((r) => bad.on('open', r));
        bad.send(bytes);

        const closed = await new Promise((r) =>
        {
            const timer = setTimeout(() => r(false), 2000);

            bad.on('close', () => { clearTimeout(timer); r(true); });
        });

        check(closed, `${what} document frame closes its own socket`);
    }

    /* And the relay is still here to say so. */
    {
        const e = new Client(`${base}/room/test`, 'E');

        await e.open();
        e.send({ type: 'hello', name: 'Eve', protocol: PROTOCOL });

        const we = await e.next('welcome');

        check(typeof we.peer === 'string',
              'and the relay is still serving the room');
        e.close();
    }

    check(readFile(docB, 'airports.gen') !== null,
          'and the providers still have the document');

    /* The providers' own awareness keeps a timer the provider does not
       stop; the page never minds, a process that wants to exit does. */
    for (const [p, d] of [[provA, docA], [provB, docB]])
    {
        p.destroy();
        p.awareness.destroy();
        d.destroy();
    }
}
catch (e)
{
    fail(`threw: ${e.message}`);
}

server.shutdown();

process.stdout.write(`\n${failures === 0 ? 'the relay does what it says'
                                          : `${failures} failed`}\n`);
process.exitCode = failures;
