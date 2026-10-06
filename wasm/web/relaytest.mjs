#!/usr/bin/env node
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
 * relaytest.mjs -- the relay, driven from Node by the clients the page
 * will be: two room sockets and two document providers in one room.
 *
 *   node wasm/web/relaytest.mjs
 *
 * What the relay promises, each checked: a hello is answered with who is
 * here and told to the others; a seat is first-claim and released on
 * close; a ping is answered with the relay's clock; a signal reaches the
 * one peer it names and nobody else; a relayed gesture reaches one or
 * everyone; a transport start is kept for a joiner; a chat line reaches
 * the room under the name the relay knows its sender by, within a length
 * and a rate, and is kept for nobody; and the document a
 * room is seeded with reaches both providers, an edit on one reaches the
 * other, and both hash to the same revision. A switch to another shipped
 * piece replaces the document, one switch after another, and the room is
 * told who made each.
 *
 * Exit status is the number of failures.
 */

import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import WebSocket, { WebSocketServer } from 'ws';
import * as Y from 'yjs';
import { WebsocketProvider } from 'y-websocket';
import * as encoding from 'lib0/encoding';

import { dspNames, fileNames, hashOf, pieceText, readFile, seenOf, snapshot }
    from './doc.js';
import { AccountStore, runAdmin } from './accounts.mjs';
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

/* Whether a socket is refused, or closed within `ms'. */
function refused (ws, ms = 2000)
{
    return new Promise((r) =>
    {
        const timer = setTimeout(() => r(false), ms);

        ws.on('error', () => {});
        ws.on('close', () => { clearTimeout(timer); r(true); });
    });
}

/* Who a room socket is, and what lets its document in: a relay of its
   own, on a file the admin commands can open beside it, and with times
   short enough to wait out. */
async function accountsInRooms ()
{
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'relaytest-'));
    const db = path.join(dir, 'relay.db');
    const acct = await relay({ port: 0, host: '127.0.0.1', tree, db,
                               corsOrigin: 'https://page.example.org',
                               ticketMs: 1000, sessionCheckMs: 200,
                               heartbeatMs: 300 });
    const at = `127.0.0.1:${acct.address().port}`;
    const post = async (route, body, session) =>
    {
        const res = await fetch(`http://${at}/api/account/${route}`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json',
                       ...(session ? { Authorization: `Bearer ${session}` }
                                   : {}) },
            body: JSON.stringify(body),
        });

        return res.json();
    };
    const hello = async (room, m) =>
    {
        const c = new Client(`ws://${at}/room/${room}`, m.name ?? 'session');

        await c.open();
        c.send({ type: 'hello', protocol: PROTOCOL, tickets: true, ...m });
        return c;
    };
    const docSocket = (room, ticket) =>
        new WebSocket(`ws://${at}/doc/${room}` +
                      (ticket === undefined ? '' : `?ticket=${ticket}`))
            .on('error', () => {});

    try
    {
        const ann = await post('register', { handle: 'Ann' });
        const a = await hello('acct', { name: 'not Ann',
                                        session: ann.session });
        const wa = await a.next('welcome');

        check(wa.identity?.name === 'Ann' && wa.identity.account === true &&
              wa.peers[0].name === 'Ann' && wa.peers[0].account === true,
              'a hello with a session plays under the account\'s handle');

        const g = await hello('acct', { name: 'Gus' });
        const [wg, joined] = await Promise.all([g.next('welcome'),
                                                a.next('joined')]);

        check(wg.identity.name === 'Gus' && wg.identity.account === false &&
              joined.name === 'Gus' && joined.account === false &&
              wg.peers.find((p) => p.peer === wa.peer)?.account === true,
              'a guest is welcomed as one, and the room is told which is ' +
              'which');

        g.send({ type: 'chat', channel: 'stage', text: 'hi', n: 1 });

        const line = await a.next('chat');

        check(line.name === 'Gus' && line.account === false,
              'a guest\'s chat line says it is a guest\'s');

        /* A session the relay does not know is refused, and says why:
           joining as a guest instead would leave somebody believing they
           were logged in. */
        for (const [what, session] of [
            ['an unknown', `s_${'0'.repeat(32)}`],
            ['a malformed', 'nonsense'],
            ['a logged-out', (await post('login', { key: ann.key })).session]])
        {
            if (what === 'a logged-out')
                await post('logout', undefined, session);

            const c = await hello('acct', { name: 'Ann', session });
            const e = await c.next('error');

            check(e.why === 'session' && /log in again/.test(e.text) &&
                  await refused(c.ws),
                  `${what} session is refused with a reason`);
        }

        /* A guest may not go by a handle, folded however. */
        for (const name of ['Ann', 'ANN', ' \uFF41nn '])
        {
            const c = await hello('acct', { name });
            const e = await c.next('error');

            check(e.why === 'name' && await refused(c.ws),
                  `a guest named ${JSON.stringify(name)} is refused`);
        }

        /* Document sockets: in with the room's ticket, and refused without
           one, with another room's, or with one that has lapsed. */
        const other = await hello('elsewhere', { name: 'Oz' });
        const { ticket: otherTicket } = await other.next('welcome');
        const open = docSocket('acct', wa.ticket);

        check(await new Promise((r) =>
        {
            open.on('open', () => r(true));
            open.on('error', () => r(false));
        }), 'a document socket with its room\'s ticket is let in');

        for (const [what, ticket] of [['no', undefined],
                                      ['a made-up', `t_${'0'.repeat(32)}`],
                                      ['another room\'s', otherTicket]])
            check(await refused(docSocket('acct', ticket)),
                  `a document socket with ${what} ticket is refused`);

        await new Promise((r) => setTimeout(r, 1100));

        const fresh = a.got.filter((m) => m.type === 'ticket').at(-1);

        check(await refused(docSocket('acct', wa.ticket)),
              'a document socket with a lapsed ticket is refused');
        check(!await refused(docSocket('acct', fresh.ticket), 300),
              'and the one handed out before it lapsed lets it in');

        /* Sessions ended over HTTP close the sockets made with them. */
        const opened = await new Promise((r) =>
        {
            const d = docSocket('acct', fresh.ticket);

            d.on('open', () => r(d));
        });

        const docGone = refused(opened);

        await post('logout', undefined, ann.session);

        const ended = await a.next('error');

        check(ended.why === 'session' && await refused(a.ws) &&
              await docGone,
              'logging out closes the room socket made with the session, ' +
              'and its document sockets');
        check(!await refused(g.ws, 300), 'and leaves the guest\'s alone');

        /* And ended from another process -- the admin commands -- within a
           check of the relay's. */
        const bo = await post('register', { handle: 'Bo' });
        const b = await hello('acct', { session: bo.session });

        await b.next('welcome');

        const store = new AccountStore(db);

        runAdmin(['ban', 'bo'], store, () => {});
        store.close();

        const banned = await b.next('error', 2000);

        check(banned.why === 'session' && await refused(b.ws),
              'a ban from the admin commands closes the account\'s sockets');

        const back = await post('login', { key: bo.key });

        check(back.error === 'banned', 'and its key no longer logs in');

        /* The file held by another process's write: a request fails at
           once rather than holding every room up while it waits. */
        {
            const holder = new AccountStore(db);

            holder.db.exec('BEGIN IMMEDIATE');

            const t0 = performance.now();
            const res = await post('login', { key: ann.key });
            const waited = performance.now() - t0;

            holder.db.exec('ROLLBACK');
            holder.close();
            check(res.error === 'internal' && waited < 1000,
                  `a write the file is locked against fails in ` +
                  `${Math.round(waited)} ms`);
        }

        check((await (await fetch(`http://${at}/`)).json()).accounts === true,
              'a relay with a page origin offers accounts');

        /* A document socket to a room nobody has opened, with no ticket:
           refused, and the relay still here. */
        check(await refused(docSocket('nosuchroom')) &&
              (await fetch(`http://${at}/`)).ok,
              'a document socket for a room that is not there is refused, ' +
              'and the relay lives');

        /* A field made to throw when read as a string or a number, in each
           message that reads one: that socket is cut, and the relay lives. */
        {
            const evil = { toString: 1, valueOf: 1 };

            for (const [what, m] of [['seat', { type: 'seat', seat: evil }],
                                     ['signal', { type: 'signal', to: evil }],
                                     ['relayed', { type: 'relayed',
                                                   to: [evil] }],
                                     ['hello', null]])
            {
                const c = await hello('evil', m === null ? { name: evil }
                                                         : { name: 'Eve' });

                if (m !== null)
                {
                    await c.next('welcome');
                    c.send(m);
                }

                check(await refused(c.ws) &&
                      (await fetch(`http://${at}/`)).ok,
                      `a ${what} whose field throws costs its socket, and ` +
                      'the relay lives');
            }
        }

        /* A cursor's name is the relay's to say: the room socket's name,
           a guest's marked as one, whatever the page set. And no socket
           speaks for a client another one does. */
        {
            const cy = await post('register', { handle: 'Cy' });
            const c = await hello('cursors', { session: cy.session });
            const { ticket: ct } = await c.next('welcome');
            const h = await hello('cursors', { name: 'Hob' });
            const { ticket: ht } = await h.next('welcome');
            const docs = [];
            const provider = (ticket) =>
            {
                const d = new Y.Doc();
                /* No BroadcastChannel: in one process it would hand the
                   pages' own states to each other past the relay. */
                const p = new WebsocketProvider(`ws://${at}/doc`, 'cursors', d,
                                                { WebSocketPolyfill: WebSocket,
                                                  params: { ticket },
                                                  disableBc: true });

                docs.push([p, d]);
                return p;
            };
            const pc = provider(ct);
            const ph = provider(ht);
            const watcher = provider(ct);
            const names = () => [...watcher.awareness.getStates().values()]
                .filter((st) => st.user !== undefined)
                .map((st) => `${st.user.name}/${st.user.account}`).sort()
                .join(' ');

            await Promise.all([pc, ph, watcher].map((p) => new Promise((r) =>
                p.synced ? r() : p.once('synced', r))));
            pc.awareness.setLocalStateField('user', {
                name: 'Admin', color: 'red;background:url(//x)',
                colorLight: 'hsl(10 70% 45% / 0.25)' });
            ph.awareness.setLocalStateField('user', { name: 'Cy' });
            await new Promise((r) => setTimeout(r, 300));

            check(names() === 'Cy/true Hob (guest)/false',
                  `a cursor goes by its room socket's name (${names()})`);

            const shown = [...watcher.awareness.getStates().values()]
                .find((st) => st.user?.name === 'Cy').user;

            check(!('color' in shown) &&
                  shown.colorLight === 'hsl(10 70% 45% / 0.25)',
                  'and its colors only in the shape the page draws them');

            /* The guest's socket, sending the account's client as its
               own. */
            const raw = docSocket('cursors', ht);

            await new Promise((r) => raw.on('open', r));

            const id = pc.awareness.clientID;
            const update = encoding.createEncoder();
            const enc = encoding.createEncoder();

            encoding.writeVarUint(update, 1);
            encoding.writeVarUint(update, id);
            encoding.writeVarUint(update,
                                  pc.awareness.meta.get(id).clock + 1);
            encoding.writeVarString(update,
                                    JSON.stringify({ user: { name: 'X' } }));
            encoding.writeVarUint(enc, 1);
            encoding.writeVarUint8Array(enc, encoding.toUint8Array(update));
            raw.send(encoding.toUint8Array(enc));
            await new Promise((r) => setTimeout(r, 300));

            check(names() === 'Cy/true Hob (guest)/false',
                  'and one socket cannot speak for another\'s cursor');

            raw.close();

            /* An update of ids made up, `entries' at a time. */
            const states = (entries) =>
            {
                const u = encoding.createEncoder();
                const e = encoding.createEncoder();

                encoding.writeVarUint(u, entries.length);

                for (const [client, state] of entries)
                {
                    encoding.writeVarUint(u, client);
                    encoding.writeVarUint(u, 1);
                    encoding.writeVarString(u, JSON.stringify(state));
                }

                encoding.writeVarUint(e, 1);
                encoding.writeVarUint8Array(e, encoding.toUint8Array(u));
                return encoding.toUint8Array(e);
            };
            const opened = async () =>
            {
                const s = docSocket('cursors', h.got.filter(
                    (m) => m.type === 'ticket').at(-1)?.ticket ?? ht);

                await new Promise((r) => s.on('open', r));
                return s;
            };

            /* A state with no user on it is still the relay's to name. */
            const bare = await opened();

            bare.send(states([[4242, {}]]));
            await new Promise((r) => setTimeout(r, 300));

            const named = watcher.awareness.getStates().get(4242)?.user;

            check(named?.name === 'Hob (guest)' && named.account === false,
                  'a cursor that names nobody is named by the relay');
            bare.close();

            /* A page echoes every state it hears: three cursors other
               sockets hold, in one update, are passed over, not cut. */
            {
                const s = await opened();
                const cut = refused(s, 500);

                s.send(states([pc, ph, watcher].map(
                    (p) => [p.awareness.clientID, {}])));
                check(!await cut,
                      'a socket echoing three others\' cursors is not cut');
                s.close();
            }

            /* A page is one client: a socket claiming three, in one update
               or one after another, is cut, and none of them is kept. */
            for (const [what, frames] of [
                ['in one update', [[[5001, {}], [5002, {}], [5003, {}]]]],
                ['one at a time', [[[6001, {}]], [[6002, {}]], [[6003, {}]]]]])
            {
                const s = await opened();
                const cut = refused(s);

                for (const f of frames)
                    s.send(states(f));

                check(await cut && !watcher.awareness.getStates().has(5003) &&
                      !watcher.awareness.getStates().has(6003),
                      `a socket claiming three clients ${what} is cut`);
            }

            /* Clients nobody has, said to be gone: nothing to forget, and
               nothing for the relay to keep a note of. */
            {
                const s = await opened();
                const { meta } = acct.rooms.get('cursors').awareness;
                const before = meta.size;

                for (let i = 7000; i < 7040; i += 2)
                    s.send(states([[i, null], [i + 1, null]]));

                await new Promise((r) => setTimeout(r, 300));
                check(meta.size === before,
                      'clients nobody has, said to be gone, leave nothing ' +
                      `kept (${meta.size - before} kept)`);
                s.close();
            }

            for (const [p, d] of docs)
            {
                p.destroy();
                p.awareness.destroy();
                d.destroy();
            }

            c.close();
            h.close();
        }

        /* A page's document socket reconnecting takes its cursor over
           from the one it replaces, which nothing has yet found dead; and
           one that stops answering pings is cut, cursor and all. */
        {
            const jo = await post('register', { handle: 'Jo' });
            const j = await hello('takeover', { session: jo.session });
            const { ticket: jt } = await j.next('welcome');
            const w = await hello('takeover', { name: 'Wes' });
            const { ticket: wt } = await w.next('welcome');
            const opts = (ticket) => ({ WebSocketPolyfill: WebSocket,
                                        params: { ticket }, disableBc: true });
            const dj = new Y.Doc();
            const dw = new Y.Doc();
            const pj = new WebsocketProvider(`ws://${at}/doc`, 'takeover', dj,
                                             opts(jt));
            const pw = new WebsocketProvider(`ws://${at}/doc`, 'takeover', dw,
                                             opts(wt));

            await Promise.all([pj, pw].map((p) => new Promise((r) =>
                p.synced ? r() : p.once('synced', r))));
            pj.awareness.setLocalStateField('user', { name: 'Jo' });
            await new Promise((r) => setTimeout(r, 200));

            const id = pj.awareness.clientID;
            const seen = () => pw.awareness.getStates().get(id)?.user;
            const again = docSocket('takeover', jt);
            const update = encoding.createEncoder();
            const enc = encoding.createEncoder();

            /* The old socket as a dead one is: hearing nothing, so that
               the page behind it does not answer for its own client. */
            await new Promise((r) => again.on('open', r));
            pj.ws._socket.pause();
            encoding.writeVarUint(update, 1);
            encoding.writeVarUint(update, id);
            encoding.writeVarUint(update, pj.awareness.meta.get(id).clock + 1);
            encoding.writeVarString(update, JSON.stringify(
                { user: { name: 'Jo', at: 'again' } }));
            encoding.writeVarUint(enc, 1);
            encoding.writeVarUint8Array(enc, encoding.toUint8Array(update));
            again.send(encoding.toUint8Array(enc));
            await new Promise((r) => setTimeout(r, 200));

            const taken = seen()?.at === 'again';

            /* And the old one dies as dead sockets do: no last word. */
            pj.shouldConnect = false;
            pj.ws._socket.destroy();
            await new Promise((r) => setTimeout(r, 200));

            check(taken && seen()?.at === 'again',
                  'a document socket of the same peer takes its cursor ' +
                  'over, ' +
                  'and keeps it when the old one closes');

            again._socket.pause();
            await new Promise((r) => setTimeout(r, 900));

            check(seen() === undefined,
                  'and a socket that stops answering pings is cut, and its ' +
                  'cursor goes with it');

            /* Four document sockets a peer -- the provider's and three
               more -- and a fifth is refused. */
            const live = w.got.filter((m) => m.type === 'ticket').at(-1)
                ?.ticket ?? wt;
            const four = [1, 2, 3].map(() => docSocket('takeover', live));

            await Promise.all(four.map((s) => new Promise((r) =>
                s.on('open', r))));
            check(await refused(docSocket('takeover', live)),
                  'a fifth document socket for one peer is refused');

            for (const s of four)
                s.close();

            pj.destroy();
            pw.destroy();
            pj.awareness.destroy();
            pw.awareness.destroy();
            j.close();
            w.close();
        }

        /* A frame past a megabyte on a room socket closes it. */
        {
            const big = await hello('acct', { name: 'Big' });

            await big.next('welcome');
            big.send({ type: 'chat', channel: 'stage',
                       text: 'x'.repeat(2 * 1024 * 1024), n: 1 });
            check(await refused(big.ws), 'a room frame over a MiB is refused');
        }

        /* Closing is final. A hello that is refused, then another at once
           before the close completes, joins nobody; and a session that
           ends leaves the room at once, its socket heard no more. */
        {
            const ghost = new Client(`ws://${at}/room/acct`, 'Ghost');

            await ghost.open();
            ghost.send({ type: 'hello', protocol: PROTOCOL, tickets: true,
                         session: `s_${'1'.repeat(32)}` });
            ghost.send({ type: 'hello', protocol: PROTOCOL, tickets: true,
                         name: 'Ghost' });

            const heard = async (pred, ms) =>
            {
                await new Promise((r) => setTimeout(r, ms));
                return g.got.some(pred);
            };

            check(!await heard((m) => m.type === 'joined' &&
                                      m.name === 'Ghost', 400),
                  'a second hello after a refused one joins nobody');

            const kim = await post('register', { handle: 'Kim' });
            const k = await hello('acct', { session: kim.session });
            const { peer } = await k.next('welcome');

            k.ws._socket.pause();
            await post('logout', undefined, kim.session);

            const left = await heard((m) => m.type === 'left' &&
                                            m.peer === peer, 100);

            k.send({ type: 'chat', channel: 'stage', text: 'still here',
                     n: 9 });

            check(left && !await heard((m) => m.type === 'chat' &&
                                              m.text === 'still here', 400),
                  'an ended session leaves the room at once, and is not ' +
                  'heard after');
            k.ws.terminate();
        }

        /* Ended sessions take their tickets at once, not when a client
           that has stopped answering lets the close complete. */
        {
            const dee = await post('register', { handle: 'Dee' });
            const d = await hello('acct', { session: dee.session });
            const { ticket: dt } = await d.next('welcome');

            d.ws._socket.pause();
            await post('logout', undefined, dee.session);

            check(await refused(docSocket('acct', dt), 1000),
                  'a logged-out session\'s ticket is refused at once');

            d.ws.terminate();
        }

        for (const c of [g, other])
            c.close();

        /* The same file under a relay without accounts: the handles in it
           hold no names, since nobody there can be the account. */
        const off = await relay({ port: 0, host: '127.0.0.1', tree, db });

        try
        {
            const k = new Client(
                `ws://127.0.0.1:${off.address().port}/room/acct`, 'Kim');

            await k.open();
            k.send({ type: 'hello', name: 'Kim', protocol: PROTOCOL,
                     tickets: true });

            const w = await k.next('welcome');

            check(w.identity.name === 'Kim' && w.identity.account === null,
                  'without accounts a guest may go by a handle the file ' +
                  'still has');
            k.close();
        }
        finally
        {
            off.shutdown();
        }
    }
    finally
    {
        acct.shutdown();
        fs.rmSync(dir, { recursive: true, force: true });
    }
}

/* The metrics port counts what a peer sends and is sent, and is a port of
   its own: the public one's health line has none of it. */
async function metricsServed ()
{
    const m = await relay({ port: 0, host: '127.0.0.1', tree, metricsPort: 0 });
    const at = `127.0.0.1:${m.address().port}`;
    const scrape = async (query = '') =>
        (await fetch(`http://127.0.0.1:${m.metrics.address().port}/${query}`))
            .json();

    try
    {
        const join = async (name) =>
        {
            const c = new Client(`ws://${at}/room/metrics`, name);

            await c.open();
            c.send({ type: 'hello', name, protocol: PROTOCOL, tickets: true });
            await c.next('welcome');
            return c;
        };
        const a = await join('A');
        const b = await join('B');

        for (let i = 0; i < 3; i++)
        {
            a.send({ type: 'ping', t0: i });
            await a.next('pong');
        }

        a.send({ type: 'relayed', data: { type: 'knob', value: 0.5 } });
        await b.next('relayed');
        a.send({ type: 'made-up' });
        a.ws.send('not JSON at all');
        await a.next('error');

        const s = await scrape();

        check(m.metrics.address().address === '127.0.0.1' &&
              s.rooms === 1 && s.peers === 2 && s.roomSockets === 2 &&
              s.room.hello.in === 2 && s.room.welcome.out === 2 &&
              s.room.ping.in === 3 && s.room.pong.out === 3 &&
              s.room.relayed.in === 1 && s.room.relayed.out === 1 &&
              s.room.relayed.outBytes > 0 && s.room.other.in === 2 &&
              s.eventLoopDelayMs.max >= 0 && s.memoryBytes.rss > 0,
              'the metrics port counts a peer\'s messages by type, on ' +
              '127.0.0.1');

        /* A loop held up is seen, however few times the histogram's
           timer ran meanwhile, and so is the CPU it spent: by every
           scrape until one resets the window, and by none after. */
        {
            await scrape('?reset');

            const until = performance.now() + 300;

            while (performance.now() < until)
                ;

            const held = await scrape();
            const again = await scrape('?reset');
            const after = await scrape();

            check(held.timerLagMs.max >= 250 && held.cpuPercent > 50 &&
                  again.timerLagMs.max >= 250 && again.window === held.window &&
                  after.window === held.window + 1 &&
                  after.timerLagMs.max < 250,
                  'the metrics say how long the loop was held up, and ' +
                  'how busy it was, until a scrape resets them');
        }

        const health = await (await fetch(`http://${at}/`)).json();

        check(health.thinksynth === 'relay' && !('room' in health) &&
              !('eventLoopDelayMs' in health) &&
              (await fetch(`http://${at}/metrics`)).status === 404,
              'and the public port serves none of it');

        a.close();
        b.close();
    }
    finally
    {
        m.shutdown();
    }
}

/* CORS_ORIGIN is an origin or nothing: the relay will not start on one
   with a path, which no request's Origin would ever match, or on `*'. */
for (const [value, status] of [['https://page.example.org', null],
                               ['*', 2],
                               ['https://page.example.org/', 2],
                               ['https://page.example.org/jam', 2],
                               ['page.example.org', 2]])
{
    const r = spawnSync(process.execPath,
                        [path.join(here, 'relay.mjs'), '--port', '0'],
                        { env: { ...process.env, CORS_ORIGIN: value,
                                 DB: ':memory:' },
                          timeout: 1500, encoding: 'utf8' });

    check(r.status === status,
          `the relay ${status === 2 ? 'refuses' : 'takes'} CORS_ORIGIN ` +
          `${value} (${r.status ?? r.signal})`);
}

const server = await relay({ port: 0, host: '127.0.0.1', tree });
const port = server.address().port;
const base = `ws://127.0.0.1:${port}`;

try
{
    /* ---- the room socket ---- */

    const a = new Client(`${base}/room/test?piece=airports.gen`, 'A');

    await a.open();
    a.send({ type: 'hello', name: 'Ann', protocol: PROTOCOL, tickets: true });

    const wa = await a.next('welcome');

    check(typeof wa.peer === 'string' && wa.peers.length === 1 &&
          wa.peers[0].name === 'Ann' && wa.piece === 'airports.gen' &&
          wa.playing === null,
          'a hello is welcomed with the room as it is');

    const b = new Client(`${base}/room/test`, 'B');

    await b.open();
    b.send({ type: 'hello', name: 'Bo', protocol: PROTOCOL, tickets: true });

    const wb = await b.next('welcome');
    const ja = await a.next('joined');

    check(wb.peers.length === 2 && ja.peer === wb.peer && ja.name === 'Bo',
          'a second peer is told who is here, and the first is told');

    const health = await (await fetch(`http://127.0.0.1:${port}/`)).json();
    const listed = health.rooms.find((r) => r.name === 'test');

    check(listed?.peers === 2 && listed.piece === 'airports.gen' &&
          listed.playing === false,
          'the health line lists the room, its two people and its piece');
    check(health.accounts === false && health.passkeys === null,
          'and offers no accounts with no page origin to serve them to');

    /* Nor any routes for them, from a page or not, and a hello's session
       is nothing it knows. */
    {
        const r = await fetch(
            `http://127.0.0.1:${port}/api/account/register`,
            { method: 'POST', headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify({ handle: 'Curl' }) });
        const s = new Client(`${base}/room/nosessions`, 'S');

        await s.open();
        s.send({ type: 'hello', name: 'Sid', protocol: PROTOCOL,
                 tickets: true, session: `s_${'2'.repeat(32)}` });

        const w = await s.next('welcome');

        check(r.status === 404 && w.identity.account === null &&
              w.identity.name === 'Sid' &&
              w.peers.every((p) => p.account === null),
              'without accounts the routes are not there, a session in a ' +
              'hello joins as anyone does, and nobody is marked a guest');
        s.close();
    }

    /* A page from before tickets is told to reload, not let in to wait on
       a document it cannot open. */
    {
        const old = new Client(`${base}/room/test`, 'Old');

        await old.open();
        old.send({ type: 'hello', name: 'Old', protocol: PROTOCOL });

        const e = await old.next('error');

        check(/older than the relay: press Update above/.test(e.text) &&
              await refused(old.ws),
              'a hello without tickets is told the page is old');
    }

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
    c.send({ type: 'hello', name: 'Cy', protocol: PROTOCOL, tickets: true });

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

    /* A page joining again after its network dropped, its old socket
       still open here: naming its last ticket lets the old peer go, seat
       and all, and a made-up ticket lets nobody go. */
    {
        const join = async (label, extra = {}) =>
        {
            const cl = new Client(`${base}/room/drop`, label);

            await cl.open();
            cl.send({ type: 'hello', name: label, protocol: PROTOCOL,
                      tickets: true, ...extra });
            return [cl, await cl.next('welcome')];
        };
        const [w, ww] = await join('Wes');
        const [p, wp] = await join('Pat');

        p.send({ type: 'seat', seat: 5 });
        await w.next('joined');
        await w.next('seats');

        const [x] = await join('Xan', { was: `t_${'0'.repeat(32)}` });

        await w.next('joined');

        const cut = refused(p.ws);
        const [q, wq] = await join('Pat', { was: wp.ticket });
        const gone = await w.next('left');

        q.send({ type: 'seat', seat: 5 });

        const sq = await q.next('seats');

        check(gone.peer === wp.peer && await cut &&
              sq.seats[5] === wq.peer && wq.peers.length === 3 &&
              wq.peers.some((o) => o.peer === ww.peer),
              'a page joining again with its last ticket replaces the peer ' +
              'it was, and takes its seat back; a made-up ticket ends ' +
              'nobody');

        for (const cl of [w, x, q])
            cl.close();
    }

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

    /* A relay from before `catchup' welcomes and never answers one: the
       wait ends, or Start would queue every command behind it for good.
       So does the socket closing under it. */
    {
        const old = new WebSocketServer({ port: 0, host: '127.0.0.1' });

        await new Promise((r) => old.on('listening', r));

        old.on('connection', (ws) => ws.on('message', (data) =>
        {
            if (JSON.parse(data).type === 'hello')
                ws.send(JSON.stringify({ type: 'welcome', peer: 'p',
                                         peers: [], playing: null }));
        }));

        const room = new Room(`ws://127.0.0.1:${old.address().port}`,
                              'test', 'Eve');

        await room.connect();

        const timedOut = await room.catchUp(200).then(() => false,
                                                       () => true);
        const pending = room.catchUp(60 * 1000).then(() => false,
                                                     () => true);

        for (const ws of old.clients)
            ws.close();

        check(timedOut && await pending,
              'a catchup the relay never answers rejects, at the wait or ' +
              'when the socket closes');
        old.close();
    }

    /* ---- the document socket ---- */

    /* Let in by a ticket the room socket's welcome hands out. */
    const k = new Client(`${base}/room/test`, 'K');

    await k.open();
    k.send({ type: 'hello', name: 'Kim', protocol: PROTOCOL, tickets: true });

    const { ticket } = await k.next('welcome');
    const docA = new Y.Doc();
    const docB = new Y.Doc();
    const provA = new WebsocketProvider(base + '/doc', 'test', docA,
                                        { WebSocketPolyfill: WebSocket,
                                          params: { ticket } });
    const provB = new WebsocketProvider(base + '/doc', 'test', docB,
                                        { WebSocketPolyfill: WebSocket,
                                          params: { ticket } });

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

    /* The run a late joiner catches up with: the start, the document as
       the start named it, and the stamped commands since. The start names
       a revision the relay has not seen yet -- the starter's edit is still
       on its way over the other socket -- and the relay waits for it
       rather than keeping what it has. */
    {
        const ahead = new Y.Doc();

        Y.applyUpdate(ahead, Y.encodeStateAsUpdate(docA));
        ahead.getMap('files').get('airports.gen').insert(0, '# at Play\n');

        const hash = await hashOf(ahead);
        const f = new Client(`${base}/room/test`, 'F');
        const g = new Client(`${base}/room/test`, 'G');

        await Promise.all([f.open(), g.open()]);
        f.send({ type: 'hello', name: 'Fay', protocol: PROTOCOL,
                 tickets: true });
        g.send({ type: 'hello', name: 'Gil', protocol: PROTOCOL,
                 tickets: true });

        const wf = await f.next('welcome');

        await g.next('welcome');

        const key = `${wf.peer}#0`;

        f.send({ type: 'transport',
                 data: { type: 'transport', op: 'start', origin: 777,
                         piece: { hash }, seed: 5, from: wf.peer,
                         seq: 0, at: -1 } });
        f.send({ type: 'log', data: { type: 'knob', at: 1.5, knob: 0,
                                      value: 0.3, from: wf.peer, seq: 1 },
                 run: key });
        f.send({ type: 'transport',
                 data: { type: 'transport', op: 'tempo', bpm: 90, at: 2,
                         from: wf.peer, seq: 2 }, run: key });

        /* A straggler from a run that is over: kept out. */
        f.send({ type: 'log', data: { type: 'knob', at: 9, knob: 0,
                                      value: 0.9, from: wf.peer, seq: 3 },
                 run: 'somebody#41' });

        await new Promise((r) => setTimeout(r, 200));
        g.send({ type: 'catchup' });
        docA.getMap('files').get('airports.gen').insert(0, '# at Play\n');

        const run = await g.next('catchup');

        check(run.start?.origin === 777 && run.files?.matched === true &&
              run.files.files['airports.gen']?.startsWith('# at Play\n') &&
              run.files.piece === 'airports.gen',
              'a late joiner is handed the document at the revision the ' +
              'start named, once the relay has it');
        check(run.log?.map((c) => c.seq).join() === '1,2',
              'and the stamped commands since, a copied knob and a tempo, ' +
              'and not one stamped for another run');

        f.send({ type: 'transport',
                 data: { type: 'transport', op: 'stop', at: 3,
                         from: wf.peer, seq: 3 } });
        await new Promise((r) => setTimeout(r, 200));
        g.send({ type: 'catchup' });

        const none = await g.next('catchup');

        check(none.start === null, 'and nothing once the run has stopped');

        /* A Play while the joiner waits for the last one's document: the
           answer is the run playing now. */
        const later = new Y.Doc();

        Y.applyUpdate(later, Y.encodeStateAsUpdate(docA));
        later.getMap('files').get('airports.gen').insert(0, '# later\n');

        f.send({ type: 'transport',
                 data: { type: 'transport', op: 'start', origin: 888,
                         piece: { hash: await hashOf(later) }, seed: 5,
                         from: wf.peer, seq: 4, at: -1 } });
        await new Promise((r) => setTimeout(r, 200));
        g.send({ type: 'catchup' });
        await new Promise((r) => setTimeout(r, 200));
        f.send({ type: 'transport',
                 data: { type: 'transport', op: 'start', origin: 999,
                         piece: { hash: await hashOf(docA) }, seed: 5,
                         from: wf.peer, seq: 5, at: -1 } });
        await new Promise((r) => setTimeout(r, 200));
        docA.getMap('files').get('airports.gen').insert(0, '# later\n');

        const current = await g.next('catchup');

        check(current.start?.origin === 999 && current.files?.matched,
              'a Play during the wait is answered with that Play\'s run');

        f.close();
        g.close();
    }

    /* What a run keeps for a late joiner is bounded in bytes, by the
       command and in all, past which it is a run that cannot be caught up
       with; and who made a start or a command is the relay's to say. */
    {
        const f = new Client(`${base}/room/logcap`, 'F');
        const g = new Client(`${base}/room/logcap`, 'G');

        await Promise.all([f.open(), g.open()]);
        f.send({ type: 'hello', name: 'Fay', protocol: PROTOCOL,
                 tickets: true });

        const wf = await f.next('welcome');

        g.send({ type: 'hello', name: 'Gil', protocol: PROTOCOL,
                 tickets: true });
        await g.next('welcome');

        const hash = await hashOf(server.rooms.get('logcap').doc);
        const caughtUp = async (seq, log) =>
        {
            f.send({ type: 'transport',
                     data: { type: 'transport', op: 'start', origin: 1,
                             piece: { hash }, seed: 1, from: 'forged',
                             seq, at: -1 } });

            for (const data of log)
                f.send({ type: 'log', data, run: `${wf.peer}#${seq}` });

            while (f.ws.bufferedAmount > 0)
                await new Promise((r) => setTimeout(r, 50));

            await new Promise((r) => setTimeout(r, 500));
            g.send({ type: 'catchup' });
            return g.next('catchup', 10000);
        };
        const edit = (seq, kib) => ({ type: 'edit', at: -1, from: 'forged',
                                      seq, text: 'x'.repeat(kib * 1024) });
        const small = await caughtUp(0, [edit(1, 1)]);

        check(small.start?.from === wf.peer &&
              small.log?.[0]?.from === wf.peer && !small.overflowed,
              'a start and a command are the sender\'s, whoever they say ' +
              'made them');

        const one = await caughtUp(2, [edit(3, 600)]);

        check(one.overflowed === true && one.log.length === 0,
              'a command too big to keep makes the run one that cannot be ' +
              'caught up with');

        const wide = await caughtUp(140, [{ ...edit(141, 0),
                                            text: '\u00e9'.repeat(300 * 1024) }]);

        check(wide.overflowed === true,
              'a command is measured in the bytes it is sent as, not ' +
              'its characters');

        const many = await caughtUp(4, Array.from({ length: 66 },
                                                  (_, i) => edit(5 + i, 510)));

        check(many.overflowed === true && many.log.length < 66,
              'and so do more commands than the run keeps bytes for');

        f.close();
        g.close();
    }

    /* Chat: to everyone in the room, its sender included, under the
       name the relay knows the sender by; to nobody in another room; and
       kept for nobody who arrives later. */
    {
        const h = new Client(`${base}/room/chat`, 'H');
        const i = new Client(`${base}/room/chat`, 'I');
        const o = new Client(`${base}/room/elsewhere`, 'O');

        await Promise.all([h.open(), i.open(), o.open()]);
        h.send({ type: 'hello', name: 'Hal', protocol: PROTOCOL,
                 tickets: true });
        i.send({ type: 'hello', name: 'Ida', protocol: PROTOCOL,
                 tickets: true });
        o.send({ type: 'hello', name: 'Oz', protocol: PROTOCOL,
                 tickets: true });

        const wh = await h.next('welcome');

        await i.next('welcome');
        await o.next('welcome');

        h.send({ type: 'chat', channel: 'stage', from: 'nobody',
                 name: 'Ida', text: '  switch at 17  ', bar: '12.3', n: 1 });

        const [mh, mi] = await Promise.all([h.next('chat'), i.next('chat')]);

        check([mh, mi].every((m) => m.from === wh.peer && m.name === 'Hal' &&
                                    m.text === 'switch at 17' &&
                                    m.bar === '12.3' &&
                                    m.channel === 'stage'),
              'a chat line reaches everyone in the room, its sender too, ' +
              'trimmed and under the name the relay gave it');
        check(mh.n === 1 && mi.n === undefined,
              'and only its sender is told which of its lines it was');
        check(await o.none('chat'), 'and nobody in another room');

        h.send({ type: 'chat', channel: 'stage', text: 'where', bar: '<b>' });

        const unbarred = await i.next('chat');

        check(unbarred.text === 'where' && !('bar' in unbarred),
              'a bar that is not a bar.beat is dropped, and the line goes');
        await h.next('chat');

        for (const [what, line] of [
            ['an empty', { text: '   ' }],
            ['a 501-character', { text: 'x'.repeat(501) }],
            ['a non-string', { text: { toString: 'hi' } }],
            ['a format-characters-only', { text: '\u202e\u200b\u2066' }],
            ['a format-wrapped blank', { text: '\u200b   \u200b' }],
            ['another channel\'s', { channel: 'house', text: 'hi' }]])
        {
            h.send({ type: 'chat', channel: 'stage', n: 7, ...line });

            const r = await h.next('refused');

            check(r.of === 'chat' && typeof r.why === 'string' && r.n === 7 &&
                  await i.none('chat', 100),
                  `${what} line is refused with a reason: ${r.why}`);
        }

        h.send({ type: 'chat', channel: 'stage', text: 'y'.repeat(500) });
        await i.next('chat');
        await h.next('chat');

        for (let k = 0; k < 10; k++)
            h.send({ type: 'chat', channel: 'stage', text: `burst ${k}` });

        const refusal = await h.next('refused');

        await new Promise((r) => setTimeout(r, 300));

        const reached = i.got.filter((m) => m.type === 'chat').length;

        check(reached > 0 && reached <= 5 && /fast/.test(refusal.why),
              `a burst is cut off past the rate: ${reached} of 10 went, ` +
              `and the rest were refused (${refusal.why})`);

        const late = new Client(`${base}/room/chat`, 'L');

        await late.open();
        late.send({ type: 'hello', name: 'Lou', protocol: PROTOCOL,
                    tickets: true });
        await late.next('welcome');
        check(await late.none('chat'),
              'and a peer who arrives later is handed none of it');

        for (const c of [h, i, o, late])
            c.close();
    }

    /* A switch: the relay says it can; a piece it does not ship is
       refused; two switches sent at once are made one after the other,
       each told to everyone with who made it and the revision it left,
       and the document is the last one's piece and nothing else. */
    {
        const s = new Client(`${base}/room/switch`, 'S');
        const t = new Client(`${base}/room/switch`, 'T');

        await Promise.all([s.open(), t.open()]);
        s.send({ type: 'hello', name: 'Sue', protocol: PROTOCOL,
                 tickets: true });
        t.send({ type: 'hello', name: 'Tom', protocol: PROTOCOL,
                 tickets: true });

        const ws = await s.next('welcome');
        const docS = new Y.Doc();
        const provS = new WebsocketProvider(base + '/doc', 'switch', docS,
                                            { WebSocketPolyfill: WebSocket,
                                              params: { ticket: ws.ticket } });

        await Promise.all([t.next('welcome'), synced(provS)]);

        const seeded = await hashOf(docS);

        check(ws.features?.includes('switch'),
              'the welcome says the relay switches pieces');

        for (const piece of ['nosuch.gen', '../gen/ebb.gen', 'ebb.dsp', 7])
        {
            s.send({ type: 'switch', piece });

            const r = await s.next('refused');

            check(r.of === 'switch' && r.why === 'no such piece' &&
                  await t.none('switched', 100),
                  `a switch to ${JSON.stringify(piece)} is refused`);
        }

        check(await hashOf(docS) === seeded,
              'and leaves the document as it was');

        s.send({ type: 'switch', piece: 'ebb.gen' });
        t.send({ type: 'switch', piece: 'colony.gen' });

        const seen = [await t.next('switched'), await t.next('switched')];
        const own = [await s.next('switched'), await s.next('switched')];

        check(seen.map((m) => `${m.name} ${m.piece}`).join() ===
              'Sue ebb.gen,Tom colony.gen' &&
              JSON.stringify(own) === JSON.stringify(seen),
              'two switches at once are told to everyone, in order, ' +
              'with who made each');

        await new Promise((r) => setTimeout(r, 300));

        check(fileNames(docS).join() === 'amb01.dsp,colony.gen,ts1.dsp' &&
              docS.getMap('meta').get('piece') === 'colony.gen' &&
              await hashOf(docS) === seen[1].hash,
              'and the document is the last one\'s piece, at the revision ' +
              'it said');
        check(await t.none('transport', 200),
              'stopped, nobody plays a switch');

        /* Playing, two switches made together are played once, by the
           relay, at the last one's revision, and that is the run a joiner
           is handed. */
        const colony = { hash: seen[1].hash, seen: seenOf(docS) };

        s.send({ type: 'transport',
                 data: { type: 'transport', op: 'start', origin: 1,
                         piece: colony, seed: 5, from: ws.peer, seq: 0,
                         at: -1 } });
        await t.next('transport');
        s.send({ type: 'switch', piece: 'ebb.gen' });
        t.send({ type: 'switch', piece: 'village.gen' });
        await t.next('switched');

        const last = await t.next('switched');

        await new Promise((r) => setTimeout(r, 500));

        const played = [s, t].map((c) =>
            c.got.filter((m) => m.type === 'transport'));
        const start = played[1][0]?.data;

        check(played.every((p) => p.length === 1 && p[0].from === 'relay') &&
              start.op === 'start' && start.from === 'relay' &&
              start.piece.hash === last.hash,
              'playing, two switches made together are played once, by ' +
              'the relay, at the last one\'s revision');

        const u = new Client(`${base}/room/switch`, 'U');

        await u.open();
        u.send({ type: 'hello', name: 'Una', protocol: PROTOCOL,
                 tickets: true });

        const wu = await u.next('welcome');

        u.send({ type: 'catchup' });

        const run = await u.next('catchup');

        check(wu.playing?.seq === start.seq &&
              run.start?.from === 'relay' && run.start.seq === start.seq &&
              run.files.matched && run.files.piece === 'village.gen',
              'and a joiner is handed that run, at that revision');

        /* A start at a revision the document has gone past: the run's
           document is answered at once, as not the one it named, rather
           than after the wait for one that will not come. */
        s.send({ type: 'transport',
                 data: { type: 'transport', op: 'start', origin: 2,
                         piece: colony, seed: 5, from: ws.peer, seq: 1,
                         at: -1 } });
        await new Promise((r) => setTimeout(r, 200));

        const asked = Date.now();

        u.send({ type: 'catchup' });

        const passed = await u.next('catchup');

        check(passed.start?.origin === 2 && !passed.files.matched &&
              Date.now() - asked < 1000,
              'a start the document has gone past is answered at once');

        const startOf = (seq, origin, piece) => s.send({
            type: 'transport',
            data: { type: 'transport', op: 'start', origin, piece, seed: 5,
                    from: ws.peer, seq, at: -1 } });

        /* A delete moves no writer's clock: a start made past one the
           relay has not had yet waits for it, rather than taking what the
           relay has for a document gone past it. */
        const ahead = new Y.Doc();

        Y.applyUpdate(ahead, Y.encodeStateAsUpdate(docS));
        ahead.getMap('files').get('village.gen').delete(0, 1);
        startOf(2, 3, { hash: await hashOf(ahead), seen: seenOf(ahead) });
        await new Promise((r) => setTimeout(r, 200));
        u.send({ type: 'catchup' });
        check(await u.none('catchup', 500),
              'a start a delete ahead of the relay waits for the delete');
        Y.applyUpdate(docS,
                      Y.encodeStateAsUpdate(ahead, Y.encodeStateVector(docS)));

        const caught = await u.next('catchup', 3000);

        check(caught.start?.origin === 3 && caught.files.matched,
              'and is answered with it once it comes');

        /* A switch the room has answered within the gathering -- a Play at
           the switched revision, or a Stop and a Play -- is not played
           again by the relay; nor is a stop of a run since replaced. */
        const relayStarts = () => t.got.filter(
            (m) => m.type === 'transport' && m.from === 'relay').length;
        const startsWere = relayStarts();

        s.send({ type: 'switch', piece: 'colony.gen' });

        const toColony = await t.next('switched');

        startOf(3, 4, { hash: toColony.hash });
        await new Promise((r) => setTimeout(r, 300));
        check(relayStarts() === startsWere,
              'a switch Played within the gathering is not played again');
        s.send({ type: 'transport',
                 data: { type: 'transport', op: 'stop', at: 1,
                         from: ws.peer, seq: 4, run: `${ws.peer}#3` } });
        s.send({ type: 'switch', piece: 'village.gen' });

        const toVillage = await t.next('switched');

        startOf(5, 5, { hash: toVillage.hash });
        s.send({ type: 'transport',
                 data: { type: 'transport', op: 'stop', at: 1,
                         from: ws.peer, seq: 6, run: `${ws.peer}#3` } });
        await new Promise((r) => setTimeout(r, 300));
        u.send({ type: 'catchup' });

        const still = await u.next('catchup');

        check(relayStarts() === startsWere,
              'nor one made while stopped and Played at once');
        check(still.start?.origin === 5,
              'a stop of a run since replaced stops nothing');

        const forwarded = t.got.filter((m) => m.type === 'transport').length;

        s.send({ type: 'transport',
                 data: { type: 'transport', op: 'start', origin: 6,
                         piece: { hash: 'h', seen: 'not base64!' },
                         from: ws.peer, seq: 7, at: -1 } });

        const bad = await s.next('refused');

        await new Promise((r) => setTimeout(r, 200));
        check(bad.of === 'transport' &&
              t.got.filter((m) => m.type === 'transport').length ===
              forwarded,
              'a start whose snapshot is not one is refused');

        u.close();

        provS.destroy();
        provS.awareness.destroy();
        docS.destroy();
        s.close();
        t.close();
    }

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
        const bad = new WebSocket(`${base}/doc/test?ticket=${ticket}`);

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
        e.send({ type: 'hello', name: 'Eve', protocol: PROTOCOL,
                 tickets: true });

        const we = await e.next('welcome');

        check(typeof we.peer === 'string',
              'and the relay is still serving the room');
        e.close();
    }

    check(readFile(docB, 'airports.gen') !== null,
          'and the providers still have the document');

    /* A room the relay lost, joined again with no seed: what is in it
       is the document the page brought, and nothing beside it. */
    {
        const r = new Client(`${base}/room/lost?piece=`, 'R');

        await r.open();
        r.send({ type: 'hello', name: 'Rae', protocol: PROTOCOL,
                 tickets: true });

        const { ticket: t } = await r.next('welcome');
        const kept = new Y.Doc();

        Y.applyUpdate(kept, Y.encodeStateAsUpdate(docA));

        const back = new WebsocketProvider(base + '/doc', 'lost', kept,
                                           { WebSocketPolyfill: WebSocket,
                                             params: { ticket: t } });

        await synced(back);
        await new Promise((res) => setTimeout(res, 300));

        const there = snapshot(server.rooms.get('lost').doc).files;

        check(JSON.stringify(there) === JSON.stringify(snapshot(kept).files),
              'a room asked for with no seed holds the document a page ' +
              'brings, and nothing else');
        back.destroy();
        back.awareness.destroy();
        kept.destroy();
        r.close();
    }

    /* The providers' own awareness keeps a timer the provider does not
       stop; the page never minds, a process that wants to exit does. */
    for (const [p, d] of [[provA, docA], [provB, docB]])
    {
        p.destroy();
        p.awareness.destroy();
        d.destroy();
    }

    k.close();

    /* ---- accounts ---- */

    await accountsInRooms();
    await metricsServed();
}
catch (e)
{
    fail(`threw: ${e.message}`);
}

server.shutdown();

process.stdout.write(`\n${failures === 0 ? 'the relay does what it says'
                                          : `${failures} failed`}\n`);
process.exitCode = failures;
