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
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import v8 from 'node:v8';
import vm from 'node:vm';

import WebSocket, { WebSocketServer } from 'ws';
import * as Y from 'yjs';
import { WebsocketProvider } from 'y-websocket';
import * as encoding from 'lib0/encoding';
import * as awarenessProtocol from 'y-protocols/awareness';
import * as syncProtocol from 'y-protocols/sync';

import { dspNames, fileNames, hashOf, pieceName, pieceText, readFile, seenOf,
         snapshot } from './doc.js';
import { AccountStore, runAdmin } from './accounts.mjs';
import { PROTOCOL, relay } from './relay.mjs';
import { Room } from './room.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const tree = path.join(here, '..', '..');

let failures = 0;

v8.setFlagsFromString('--expose-gc');

const gc = vm.runInNewContext('gc');

/* What this process's heap holds once what nothing holds is gone: the
   relay's, besides the test's own. */
function heapUsed ()
{
    gc();
    return process.memoryUsage().heapUsed;
}

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

/* A provider's sync update: what `doc' has past state vector `sv', or
   an update as it is. */
function frame (doc, sv)
{
    const enc = encoding.createEncoder();

    encoding.writeVarUint(enc, 0);
    syncProtocol.writeUpdate(enc, doc instanceof Uint8Array
        ? doc : Y.encodeStateAsUpdate(doc, sv));
    return encoding.toUint8Array(enc);
}

/* A document socket into relay `q''s `room' on `ticket', open. */
async function docSocket (q, room, ticket)
{
    const d = new WebSocket(`ws://127.0.0.1:${q.address().port}/doc/` +
                            `${room}?ticket=${ticket}`);

    d.on('error', () => {});
    await new Promise((r) => d.on('open', r));
    return d;
}

/* Who may do what in a room: its owner, its musicians and its
   spectators, each held to it by the relay and not by the page. */
async function rolesEnforced ()
{
    const q = await relay({ port: 0, host: '127.0.0.1', tree });
    const at = `ws://127.0.0.1:${q.address().port}`;
    const join = async (name, extra = {}) =>
    {
        const c = new Client(`${at}/room/roles`, name);

        await c.open();
        c.send({ type: 'hello', name, protocol: PROTOCOL, tickets: true,
                 ...extra });
        c.welcome = await Promise.race([c.next('welcome'), c.next('error')]);
        return c;
    };
    /* The last room line a client has, once nothing more is coming. */
    const room = async (c) =>
    {
        await new Promise((r) => setTimeout(r, 100));

        const lines = c.got.filter((m) => m.type === 'room');

        return lines.at(-1) ?? c.welcome.room;
    };
    const edit = (c, text) =>
    {
        const doc = new Y.Doc();

        doc.getText('t').insert(0, text);
        return frame(doc);
    };

    try
    {
        const a = await join('Ann');
        const b = await join('Bo');
        const c = await join('Cy');
        const [A, B, C] = [a, b, c].map((x) => x.welcome.peer);

        check(a.welcome.room.owner === A &&
              a.welcome.room.visibility === 'unlisted' &&
              (await room(c)).roles[C] === 'musician',
              'the room\'s first peer owns it, it is unlisted, and a peer ' +
              'joins as a musician');

        b.send({ type: 'set', locked: true });

        const notOwner = await b.next('refused');

        check(notOwner.of === 'set' && !(await room(a)).locked,
              'only the owner changes the room');

        a.send({ type: 'role', peer: B, role: 'spectator' });
        b.send({ type: 'seat', seat: 1 });

        const seat = await b.next('refused');
        const bRoom = await room(b);

        b.send({ type: 'chat', channel: 'stage', text: 'hi', n: 1 });
        b.send({ type: 'relayed', data: { type: 'knob' } });

        const chat = await b.next('refused');

        check(bRoom.roles[B] === 'spectator' && bRoom.invite === null &&
              seat.of === 'seat' && chat.of === 'chat' && chat.n === 1 &&
              await a.none('relayed') && await a.none('chat'),
              'a spectator takes no seat, says nothing in the stage chat, ' +
              'and sends no gesture, and is not given the invite');

        /* What a spectator's page holds, answering the sync, goes; an
           edit does not. */
        const bd = await docSocket(q, 'roles', b.welcome.ticket);

        bd.send(frame(new Y.Doc()));
        bd.send(edit(b, 'spectated'));

        const bEdit = await b.next('refused');

        check(bEdit.of === 'edit' && bEdit.why === 'spectator' &&
              !q.rooms.get('roles').doc.getText('t').toString()
                  .includes('spectated'),
              'a spectator\'s edit is refused, and an empty sync is not');

        a.send({ type: 'set', locked: true });
        await a.next('room');

        const cd = await docSocket(q, 'roles', c.welcome.ticket);
        const ad = await docSocket(q, 'roles', a.welcome.ticket);

        cd.send(edit(c, 'locked out'));

        const cEdit = await c.next('refused');

        ad.send(edit(a, 'owner'));
        await new Promise((r) => setTimeout(r, 100));

        check(cEdit.why === 'locked' &&
              q.rooms.get('roles').doc.getText('t').toString() === 'owner',
              'a locked piece is edited by its owner alone');

        const piece = q.rooms.get('roles').doc.getMap('meta').get('piece');

        c.send({ type: 'switch', piece: 'mirrorball.gen' });
        c.send({ type: 'transport', data: { type: 'edit', text: '' } });

        const [cSwitch, cApply] = [await c.next('refused'),
                                   await c.next('refused')];

        check(cSwitch.of === 'switch' && cApply.of === 'transport' &&
              q.rooms.get('roles').doc.getMap('meta').get('piece') === piece,
              'nor is a locked piece switched or applied over by anyone else');

        const unlisted = (await room(a)).invite;

        a.send({ type: 'set', visibility: 'private' });

        const invite = (await room(a)).invite;
        const d0 = await join('Di');
        const old = await join('Di', { invite: unlisted });
        const d = await join('Di', { invite });

        check(d0.welcome.why === 'private' && old.welcome.why === 'private' &&
              d.welcome.type === 'welcome',
              'a private room is joined by the invite it was given when it ' +
              'was made private, and nothing else');

        a.send({ type: 'remove', peer: d.welcome.peer });

        const out = await d.next('error');
        const rotated = (await room(a)).invite;
        const d2 = await join('Di', { invite });

        check(out.why === 'removed' && rotated !== invite &&
              d2.welcome.why === 'private',
              'a removed peer is told so, and the invite they had stops ' +
              'working');

        /* Ann goes: Bo, here longer, is a spectator, so Cy has it. Cy
           joining again keeps it. */
        a.close();

        check((await room(c)).owner === C,
              'the owner gone, the longest-present musician owns the room');

        const c2 = await join('Cy', { was: c.welcome.ticket,
                                      invite: rotated });

        check((await room(c2)).owner === c2.welcome.peer &&
              (await room(c2)).roles[B] === 'spectator',
              'the owner joining again is the owner still, and a spectator ' +
              'is one still');

        for (const x of [b, c2, d2, d0, old, bd, cd, ad])
            x.close();
    }
    finally
    {
        q.shutdown();
    }
}

/* Roles for accounts: what follows an account across its tabs and its
   joining again, and an owner's grace. */
async function rolesForAccounts ()
{
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'relaytest-'));
    const q = await relay({ port: 0, host: '127.0.0.1', tree,
                            db: path.join(dir, 'relay.db'),
                            corsOrigin: 'https://page.example.org' });
    const at = `127.0.0.1:${q.address().port}`;
    const session = async (handle) => (await (await fetch(
        `http://${at}/api/account/register`,
        { method: 'POST', headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ handle }) })).json()).session;
    const join = async (roomName, m) =>
    {
        const c = new Client(`ws://${at}/room/${roomName}`, m.name ?? 'x');

        await c.open();
        c.send({ type: 'hello', protocol: PROTOCOL, tickets: true, ...m });
        c.welcome = await Promise.race([c.next('welcome'), c.next('error')]);
        return c;
    };
    const settle = () => new Promise((r) => setTimeout(r, 100));
    const owner = (name) => q.rooms.get(name).owner;
    const [ann, bo, dee, eve] = await Promise.all(
        ['Ann', 'Bo', 'Dee', 'Eve'].map(session));
    const open = [];

    try
    {
        /* An owner's page reloading its private room has no invite. */
        const a = await join('acc', { session: ann });

        a.send({ type: 'set', visibility: 'private' });
        await settle();
        a.close();
        await settle();

        const a2 = await join('acc', { session: ann });

        check(a2.welcome.type === 'welcome' &&
              owner('acc') === a2.welcome.peer,
              'an account owner is let back into its private room without ' +
              'the invite');
        a2.send({ type: 'set', visibility: 'unlisted' });

        /* Two tabs of one account are one person. */
        const b1 = await join('acc', { session: bo });
        const b2 = await join('acc', { session: bo });

        a2.send({ type: 'role', peer: b1.welcome.peer, role: 'spectator' });
        await settle();

        const roles = q.rooms.get('acc');

        check(roles.peers.get(b2.welcome.peer)?.role === 'spectator',
              'an account made a spectator in one tab is one in all');

        a2.send({ type: 'remove', peer: b1.welcome.peer });

        const out = await b2.next('error');
        const b3 = await join('acc', { session: bo });
        const b4 = await join('acc', { session: bo, was: b2.welcome.ticket });

        check(out.why === 'removed' && b3.welcome.why === 'removed' &&
              b4.welcome.why === 'removed',
              'an account removed in one tab is out of all, and stays out');

        /* Somebody else's ticket takes over nothing. */
        const g = await join('acc', { name: 'Gus', was: a2.welcome.ticket });

        await settle();
        check(g.welcome.type === 'welcome' &&
              owner('acc') === a2.welcome.peer &&
              q.rooms.get('acc').peers.has(a2.welcome.peer),
              'a hello naming another\'s ticket is a peer of its own');

        /* The owner's grace outlasts whoever held the room meanwhile, an
           account or a stranger in an empty room. */
        const o = await join('grace', { session: ann });
        const d = await join('grace', { session: dee });
        const c = await join('grace', { name: 'Cy' });

        o.close();
        await settle();
        d.close();
        await settle();

        const o2 = await join('grace', { session: ann });

        check(owner('grace') === o2.welcome.peer,
              'an owner back within its grace has the room again after two ' +
              'others held it');

        c.close();
        o2.close();
        await settle();

        const e = await join('grace', { session: eve });
        const o3 = await join('grace', { session: ann });

        check(owner('grace') === o3.welcome.peer,
              'and after a stranger found the room empty');
        open.push(a2, b1, b3, b4, g, e, o3);
    }
    finally
    {
        open.forEach((x) => x.close());
        q.shutdown();
    }
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

        /* And one reset as soon as it is refused. */
        {
            const [host, port] = at.split(':');
            const s = net.connect(Number(port), host);

            await new Promise((r) => s.on('connect', r));
            s.write('GET /doc/nosuchroom HTTP/1.1\r\nHost: x\r\n' +
                    'Upgrade: websocket\r\nConnection: Upgrade\r\n' +
                    'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n' +
                    'Sec-WebSocket-Version: 13\r\n\r\n');
            await new Promise((r) => s.once('data', r));
            s.resetAndDestroy();
            await new Promise((r) => setTimeout(r, 200));
            check((await fetch(`http://${at}/`)).ok,
                  'and one its client resets once refused, and the relay ' +
                  'lives');
        }

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

            /* What is kept of a state is what a page's holds. */
            {
                const s = await opened();
                const at = { type: { client: 1, clock: 2 }, item: null,
                             tname: null, assoc: 0 };

                s.send(states([[4343, {
                    user: { name: 'X', pad: [{}] },
                    cursor: { anchor: { ...at, pad: [{}] }, head: at, pad: 1 },
                    pad: Array(250).fill({}) }]]));
                await new Promise((r) => setTimeout(r, 300));
                check(JSON.stringify(watcher.awareness.getStates().get(4343)) ===
                      JSON.stringify({ user: { name: 'Hob (guest)',
                                               account: false },
                                       cursor: { anchor: at, head: at } }),
                      'a state is kept as the fields a page\'s has, and ' +
                      'nothing else it carries');
                s.close();
            }

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

            /* A cursor is a few hundred bytes: one far longer is cut, and
               not kept for the room. */
            {
                const s = await opened();
                const cut = refused(s);

                s.send(states([[8001, { pad: 'x'.repeat(64 * 1024) }]]));
                check(await cut && !watcher.awareness.getStates().has(8001),
                      'a socket sending a cursor far longer than a page\'s ' +
                      'is cut, and the cursor not kept');
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

            /* Ids made up, said to be there and then gone, one after
               another: each is forgotten as it goes. */
            {
                const s = await opened();
                const { meta } = acct.rooms.get('cursors').awareness;
                const before = meta.size;
                const at = (client, clock, state) =>
                {
                    const u = encoding.createEncoder();
                    const e = encoding.createEncoder();

                    encoding.writeVarUint(u, 1);
                    encoding.writeVarUint(u, client);
                    encoding.writeVarUint(u, clock);
                    encoding.writeVarString(u, JSON.stringify(state));
                    encoding.writeVarUint(e, 1);
                    encoding.writeVarUint8Array(e, encoding.toUint8Array(u));
                    return encoding.toUint8Array(e);
                };

                for (let i = 8000; i < 8040; i++)
                {
                    s.send(at(i, 1, {}));
                    s.send(at(i, 2, null));
                }

                await new Promise((r) => setTimeout(r, 300));
                check(meta.size === before,
                      'clients made up and said to be gone are forgotten ' +
                      `(${meta.size - before} kept)`);
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
            const moved = { type: { client: 9, clock: 9 }, item: null,
                            tname: null, assoc: 0 };
            const seen = () => pw.awareness.getStates().get(id);
            const movedBy = () => seen()?.cursor?.anchor.type?.clock === 9;
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
                { user: { name: 'Jo' },
                  cursor: { anchor: moved, head: moved } }));
            encoding.writeVarUint(enc, 1);
            encoding.writeVarUint8Array(enc, encoding.toUint8Array(update));
            again.send(encoding.toUint8Array(enc));
            await new Promise((r) => setTimeout(r, 200));

            const taken = movedBy();

            /* And the old one dies as dead sockets do: no last word. */
            pj.shouldConnect = false;
            pj.ws._socket.destroy();
            await new Promise((r) => setTimeout(r, 200));

            check(taken && movedBy(),
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
            c.welcome = await c.next('welcome');
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

        /* A catch-up answered is counted, `start: null' too, and one
           whose asker left before the answer is not. */
        {
            b.send({ type: 'catchup' });
            await b.next('catchup');

            const answered = (await scrape()).catchups;
            const doc = new Y.Doc();
            const prov = new WebsocketProvider(
                `ws://${at}/doc`, 'metrics', doc,
                { WebSocketPolyfill: WebSocket,
                  params: { ticket: a.welcome.ticket } });

            await new Promise((r) => prov.synced ? r()
                                                 : prov.once('synced', r));

            const ahead = new Y.Doc();

            Y.applyUpdate(ahead, Y.encodeStateAsUpdate(doc));
            ahead.getMap('files').get('airports.gen').insert(0, '# ahead\n');
            a.send({ type: 'transport',
                     data: { type: 'transport', op: 'start', origin: 1,
                             piece: { hash: await hashOf(ahead) }, seed: 5,
                             from: a.welcome.peer, seq: 0, at: -1 } });

            const c = await join('C');

            c.send({ type: 'catchup' });
            await new Promise((r) => setTimeout(r, 100));
            c.close();
            await new Promise((r) => setTimeout(r, 200));
            doc.getMap('files').get('airports.gen').insert(0, '# ahead\n');
            await new Promise((r) => setTimeout(r, 300));

            const left = (await scrape()).catchups;

            check(answered === 1 && left === answered,
                  `catch-ups are counted as answered (${answered}, then ` +
                  `${left} after one whose asker left)`);
            prov.destroy();
            prov.awareness.destroy();
            doc.destroy();
        }

        /* A metrics port taken is a misconfiguration, and the relay says
           so rather than start without it. */
        {
            const taken = m.metrics.address().port;
            const r = spawnSync(process.execPath,
                                [path.join(here, 'relay.mjs'), '--port', '0'],
                                { env: { ...process.env, DB: ':memory:',
                                         METRICS_PORT: String(taken) },
                                  timeout: 1500, encoding: 'utf8' });

            check(r.status === 2 &&
                  r.stderr.includes(`METRICS_PORT ${taken}`),
                  'a relay whose METRICS_PORT is taken stops, naming it ' +
                  `(${r.status ?? r.signal}: ${r.stderr.split('\n')[0]})`);
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

/* What the relay queues for a socket is bounded, for each and for all of
   them, on a relay with caps small enough to reach. */
async function queuesBounded ()
{
    const q = await relay({ port: 0, host: '127.0.0.1', tree,
                            queuedMaxBytes: 1024 * 1024,
                            queuedTotalMaxBytes: 4 * 1024 * 1024,
                            limits: { fanout: [2 ** 30, 2 ** 30],
                                      roomIn: [2 ** 30, 2 ** 30] } });
    let at = `ws://127.0.0.1:${q.address().port}`;
    const join = async (room, name) =>
    {
        const c = new Client(`${at}/room/${room}`, name);

        await c.open();
        c.send({ type: 'hello', name, protocol: PROTOCOL, tickets: true });
        c.welcome = await c.next('welcome');
        return c;
    };
    const until = async (cond, ms = 5000) =>
    {
        for (const end = performance.now() + ms; !cond();)
        {
            if (performance.now() > end)
                return false;

            await new Promise((r) => setTimeout(r, 20));
        }

        return true;
    };
    const relayed = (c) => c.got.filter((m) => m.type === 'relayed').length;
    const pad = 'x'.repeat(64 * 1024);

    try
    {
        /* One that stops reading: cut, and told why once it reads again;
           the one that reads has everything. Gestures alone do not cut
           it: a peer half way to its cap misses them instead. */
        {
            const a = await join('slow', 'A');
            const b = await join('slow', 'B');
            const c = await join('slow', 'C');

            const closed = refused(a.ws, 8000);
            const transports = () =>
                c.got.filter((m) => m.type === 'transport').length;

            /* As fast as the one that reads takes them, which is under
               its cap: a burst would be over it too. */
            const paced = async (m, got) =>
            {
                for (let i = 0; i < 400; i += 20)
                {
                    for (let k = i; k < i + 20; k++)
                        b.send(m(k));

                    await until(() => got() >= i + 20);
                }
            };

            a.ws._socket.pause();
            await paced((i) => ({ type: 'relayed', data: { i, pad } }),
                        () => relayed(c));

            const spared = a.ws.readyState === WebSocket.OPEN &&
                           q.rooms.get('slow').peers.has(a.welcome.peer);

            await paced((i) => ({ type: 'transport',
                                  data: { type: 'transport', op: 'tempo', i,
                                          pad } }), transports);

            const left = await c.next('left', 5000);

            a.ws._socket.resume();

            const why = await a.next('error', 5000);
            const all = relayed(c) === 400 && transports() === 400;

            check(spared, 'a socket that stops reading misses gestures ' +
                          'past half its cap, and is not cut for them');
            check(all && left.peer === a.welcome.peer &&
                  why.why === 'slow' && await closed &&
                  b.ws.readyState === WebSocket.OPEN,
                  'a socket that stops reading is cut past its cap and told ' +
                  'why, and the others are not');
            b.close();
            c.close();
        }

        /* A catch-up larger than the cap, to a socket that reads it. */
        {
            const f = await join('big', 'F');
            const g = await join('big', 'G');
            const hash = await hashOf(q.rooms.get('big').doc);
            const key = `${f.welcome.peer}#0`;

            f.send({ type: 'transport',
                     data: { type: 'transport', op: 'start', origin: 1,
                             piece: { hash }, seed: 1, seq: 0, at: -1 } });

            for (let i = 1; i <= 32; i++)
                f.send({ type: 'log', run: key,
                         data: { type: 'knob', at: i, seq: i, pad } });

            await new Promise((r) => setTimeout(r, 300));
            g.send({ type: 'catchup' });

            const run = await g.next('catchup');

            check(run.log?.length === 32 &&
                  g.ws.readyState === WebSocket.OPEN,
                  'a catch-up larger than a socket\'s cap reaches it');

            /* Two asked for at once are one answer, which the page takes
               for both: two would be over the cap. */
            g.ws._socket.cork();
            g.send({ type: 'catchup' });
            g.send({ type: 'catchup' });
            g.ws._socket.uncork();
            await g.next('catchup');
            check(await g.none('catchup', 500) &&
                  g.ws.readyState === WebSocket.OPEN,
                  'two catch-ups asked for at once are answered once');

            /* Two at once are the same bytes, and one after another
               command has it as well. */
            const raw = async (c) =>
            {
                const got = new Promise((r) => c.ws.on('message', (d) =>
                {
                    if (JSON.parse(d).type === 'catchup')
                        r(d);
                }));

                c.send({ type: 'catchup' });
                return got;
            };
            const h = await join('big', 'H');
            const [x, y] = await Promise.all([raw(g), raw(h)]);

            f.send({ type: 'log', run: key,
                     data: { type: 'knob', at: 33, seq: 33 } });
            await new Promise((r) => setTimeout(r, 200));

            const z = JSON.parse(await raw(h));

            check(x.equals(y) && z.log.map((c) => c.seq).join() ===
                      Array.from({ length: 33 }, (_, i) => i + 1).join(),
                  'two catch-ups at once are the same bytes, and a later ' +
                  'one has the commands since');

            f.close();
            g.close();
            h.close();
        }
    }
    finally
    {
        q.shutdown();
    }

    /* A catch-up asked for while the last answer drains, after a Play
       has replaced its run, is answered with the run since once that has
       drained. */
    const k = await relay({ port: 0, host: '127.0.0.1', tree,
                            limits: { roomIn: [2 ** 30, 2 ** 30] } });

    at = `ws://127.0.0.1:${k.address().port}`;

    try
    {
        const f = await join('drains', 'F');
        const g = await join('drains', 'G');
        const hash = await hashOf(k.rooms.get('drains').doc);
        const start = (seq) => f.send({
            type: 'transport',
            data: { type: 'transport', op: 'start', origin: 1,
                    piece: { hash }, seed: 1, seq, at: -1 } });
        const answers = () => g.got.filter((m) => m.type === 'catchup');
        const big = 'x'.repeat(500 * 1024);

        start(0);

        for (let i = 1; i <= 14; i++)
            f.send({ type: 'log', run: `${f.welcome.peer}#0`,
                     data: { type: 'knob', at: i, seq: i, pad: big } });

        await new Promise((r) => setTimeout(r, 500));
        g.ws._socket.pause();
        g.send({ type: 'catchup' });
        await new Promise((r) => setTimeout(r, 300));

        const held = [...k.rooms.get('drains').peers.values()]
            .find((p) => p.name === 'G').ws.heldBytes > 0;

        start(1);
        await new Promise((r) => setTimeout(r, 200));
        g.send({ type: 'catchup' });
        await new Promise((r) => setTimeout(r, 200));
        g.ws._socket.resume();
        await until(() => answers().length === 2, 5000);
        check(held && answers().map((m) => m.start.seq).join() === '0,1',
              'a catch-up asked for while the last drains is answered with ' +
              'the run as it is once that has drained');
        f.close();
        g.close();
    }
    finally
    {
        k.shutdown();
    }

    /* Two that stop reading, each under its own cap: over the relay-wide
       total, the one holding more is cut, and the other is left. */
    const w = await relay({ port: 0, host: '127.0.0.1', tree,
                            queuedTotalMaxBytes: 4 * 1024 * 1024 });

    at = `ws://127.0.0.1:${w.address().port}`;

    try
    {
        /* What one turn sends a socket goes in one write, and in the
           order it was sent, small frames and large alike. */
        {
            const x = await join('order', 'X');
            const y = await join('order', 'Y');
            const sizes = Array.from({ length: 300 },
                                     (_, i) => (i % 50 === 7 ? 100000 : i));

            sizes.forEach((n, i) =>
            {
                x.send({ type: 'relayed', data: { i, pad: 'x'.repeat(n) } });

                if (i % 30 === 0)
                    y.send({ type: 'ping', t0: i });
            });

            await until(() => relayed(y) === sizes.length);

            const order = y.got.filter((m) => m.type === 'relayed')
                .map((m) => m.data.i);
            const pongs = y.got.filter((m) => m.type === 'pong');

            check(order.join() === sizes.map((n, i) => i).join() &&
                  pongs.map((m) => m.t0).join() ===
                      sizes.map((n, i) => i).filter((i) => i % 30 === 0)
                          .join(),
                  'what a socket is sent arrives in order, written a turn ' +
                  'at a time');
            x.close();
            y.close();
        }

        const a = await join('both', 'A');
        const b = await join('both', 'B');
        const s = await join('both', 'S');
        const queued = (c) =>
            w.rooms.get('both').peers.get(c.welcome.peer)?.ws
                .bufferedAmount ?? 0;
        const fill = (c, bytes) => until(() =>
        {
            s.send({ type: 'relayed', to: c.welcome.peer, data: { pad } });
            return queued(c) > bytes || s.got.some((m) => m.type === 'left');
        }, 20000);

        a.ws._socket.pause();
        b.ws._socket.pause();
        await fill(a, 3 * 1024 * 1024);
        await fill(b, 4 * 1024 * 1024);

        const left = await s.next('left');

        check(left.peer === a.welcome.peer && await s.none('left', 300) &&
              w.rooms.get('both').peers.has(b.welcome.peer),
              'over the relay-wide total, the socket holding the most is ' +
              'cut');
        a.ws.terminate();
        b.ws.terminate();
        s.close();
    }
    finally
    {
        w.shutdown();
    }

    /* A socket cut is not counted again before its close comes: a
       second look at once cuts nobody else. */
    const x = await relay({ port: 0, host: '127.0.0.1', tree,
                            limits: { fanout: [2 ** 30, 2 ** 30],
                                      roomIn: [2 ** 30, 2 ** 30] } });

    at = `ws://127.0.0.1:${x.address().port}`;

    try
    {
        const [a, b, s] = await Promise.all(['A', 'B', 'S'].map((n) =>
            join('twice', n)));
        const room = x.rooms.get('twice');
        const queued = (c) =>
            room.peers.get(c.welcome.peer)?.ws.bufferedAmount ?? 0;
        const fill = (c, bytes) => until(() =>
        {
            s.send({ type: 'relayed', to: c.welcome.peer, data: { pad } });
            return queued(c) > bytes;
        }, 20000);

        a.ws._socket.pause();
        b.ws._socket.pause();
        await fill(a, 2 * 1024 * 1024);
        await fill(b, 1024 * 1024);
        room.ctx.queues.totalMax = queued(a) + queued(b) - 1;
        room.ctx.queues.trim();
        room.ctx.queues.trim();
        await new Promise((r) => setTimeout(r, 300));
        check(!room.peers.has(a.welcome.peer) &&
              room.peers.has(b.welcome.peer),
              'a socket cut for the relay-wide total is not counted ' +
              'again before it closes');
        a.ws.terminate();
        b.ws.terminate();
        s.close();
    }
    finally
    {
        x.shutdown();
    }

    /* What a socket has had queued for others lately is not what it
       holds: over the total, one holding an edit it does not read goes,
       and not one that sent others what they read. The total is above
       one gesture to both readers, and each is read before the next is
       sent, so only the edit takes the relay over it however slowly the
       readers drain. */
    const t = await relay({ port: 0, host: '127.0.0.1', tree,
                            queuedTotalMaxBytes: 2.5 * 1024 * 1024 });

    at = `ws://127.0.0.1:${t.address().port}`;

    try
    {
        const [a, c, e, f, g] = await Promise.all(
            ['A', 'C', 'E', 'F', 'G'].map((n) => join('blame', n)));
        const cd = await docSocket(t, 'blame', c.welcome.ticket);
        const ad = await docSocket(t, 'blame', a.welcome.ticket);
        const big = 'x'.repeat(900 * 1024);

        await new Promise((r) => setTimeout(r, 200));
        cd._socket.pause();

        for (let i = 1; i <= 3; i++)
        {
            e.send({ type: 'relayed', to: [f.welcome.peer, g.welcome.peer],
                     data: { big } });
            await until(() => relayed(f) === i && relayed(g) === i);
        }

        const y = new Y.Doc();

        y.getText('t').insert(0, 'y'.repeat(3000 * 1000));
        ad.send(frame(y));

        /* A paused socket reads no close: gone is the relay's to say. */
        const room = t.rooms.get('blame');
        const gone = await until(() => ![...room.docConns].some((x) =>
            room.docOwner.get(x) === room.peers.get(c.welcome.peer)));

        await until(() => relayed(f) === 3 && relayed(g) === 3);
        check(gone && e.ws.readyState === WebSocket.OPEN &&
              relayed(f) === 3 && relayed(g) === 3,
              'over the relay-wide total, a socket holding an edit it ' +
              'does not read is cut, and not one that sent others what ' +
              'they read');
        ad.close();
        cd.terminate();

        for (const x of [a, c, e, f, g])
            x.close();
    }
    finally
    {
        t.shutdown();
    }
}

/* A room is made by a hello the relay welcomes and by nothing else, and
   only so many, by count, by what they hold and by who asks. */
async function roomsBounded ()
{
    const open = async (q, room, query = '') =>
    {
        const c = new Client(`ws://127.0.0.1:${q.address().port}/room/` +
                             `${room}${query}`, room);

        await c.open();
        return c;
    };
    const hello = async (q, room, query) =>
    {
        const c = await open(q, room, query);

        c.send({ type: 'hello', name: room, protocol: PROTOCOL,
                 tickets: true });
        c.said = await Promise.race([c.next('welcome'), c.next('error')]);
        return c;
    };
    const synced = (p) => new Promise((r) =>
    {
        const timer = setTimeout(() => r(false), 5000);

        p.synced ? r(true) : p.once('synced', () =>
        {
            clearTimeout(timer);
            r(true);
        });
    });
    const q = await relay({ port: 0, host: '127.0.0.1', tree, roomsMax: 2,
                            roomLimits: [{ burst: 3, refillMs: 60000 },
                                         null, null] });

    try
    {
        const bare = await open(q, 'bare');
        const wrong = await open(q, 'wrong');
        const doc = new WebSocket(`ws://127.0.0.1:${q.address().port}` +
                                  '/doc/nodoc');

        wrong.send({ type: 'hello', protocol: PROTOCOL + 1, tickets: true });
        await wrong.next('error');
        await refused(doc);
        await new Promise((r) => setTimeout(r, 200));
        check(q.rooms.size === 0,
              'a room socket with no hello, one with a hello the relay ' +
              'refuses, and a document socket make no room');
        bare.close();

        const a = await hello(q, 'one');
        const b = await hello(q, 'two');
        const c = await hello(q, 'three');
        const again = await hello(q, 'one');

        check(a.said.type === 'welcome' && b.said.type === 'welcome' &&
              c.said.why === 'rooms' && /as many rooms/.test(c.said.text) &&
              again.said.type === 'welcome',
              'past the most rooms a new one is refused, and one already ' +
              'open is joined');

        b.close();
        await a.next('left', 500).catch(() => null);
        await new Promise((r) => setTimeout(r, 200));

        const d = await hello(q, 'four');

        check(d.said.type === 'welcome' && !q.rooms.has('two') &&
              q.rooms.has('one'),
              'and an empty room goes to make room for a new one');

        const e = await hello(q, 'five');

        check(e.said.why === 'rooms' &&
              /too many new rooms/.test(e.said.text) && e.said.retryMs > 0 && e.said.retryMs <= 60000,
              'a client that makes rooms too fast is refused another, and ' +
              'told when to try again; one refused for the most rooms was ' +
              'not one of its new rooms');

        for (const x of [a, again, d, e, c])
            x.close();
    }
    finally
    {
        q.shutdown();
    }

    /* So many joins from one address, into a room already open as into a
       new one: each is told to the whole room. */
    const j = await relay({ port: 0, host: '127.0.0.1', tree,
                            joinLimits: [{ burst: 3, refillMs: 60000 },
                                         null, null] });

    try
    {
        const joins = [];

        for (let i = 0; i < 4; i++)
            joins.push(await hello(j, 'joins'));

        const [last] = joins.slice(-1);

        check(joins.slice(0, 3).every((c) => c.said.type === 'welcome') &&
              last.said.why === 'flood' &&
              /too many joins/.test(last.said.text) &&
              last.said.retryMs > 0 && last.said.retryMs <= 60000,
              'a client that joins too often is refused, and told when to ' +
              'try again');

        /* Before its hello is read for anything else. */
        const wrong = await open(j, 'joins');

        wrong.send({ type: 'hello', protocol: PROTOCOL + 1, tickets: true });
        check((await wrong.next('error')).why === 'flood',
              'and a hello past its joins is refused for them before ' +
              'anything in it is looked at');
        joins.push(wrong);

        for (const c of joins)
            c.close();
    }
    finally
    {
        j.shutdown();
    }

    /* So many people in a room and no more, a page joining again
       aside. */
    const p = await relay({ port: 0, host: '127.0.0.1', tree, peersMax: 2 });

    try
    {
        const a = await hello(p, 'full');
        const b = await hello(p, 'full');
        const c = await hello(p, 'full');
        const back = await open(p, 'full');

        back.send({ type: 'hello', name: 'back', protocol: PROTOCOL,
                    tickets: true, was: b.said.ticket });

        check(c.said.why === 'full' && /2 people/.test(c.said.text) &&
              (await back.next('welcome')).peers.length === 2,
              'a room of the most people refuses another, and lets one ' +
              'join again');

        for (const x of [a, b, c, back])
            x.close();
    }
    finally
    {
        p.shutdown();
    }

    /* A room is charged for what its document took, and no more than an
       eighth of the budget: an edit that could take it past that is
       refused, and the room is joined, read and edited as before. A
       frame Yjs cannot read costs its socket and charges nothing. And
       empty rooms go only as many as make room for a new one. */
    const e = await relay({ port: 0, host: '127.0.0.1', tree, roomsMax: 3,
                            roomsMaxBytes: 128 * 1024,
                            roomMaxBytes: 64 * 1024 });

    try
    {
        const a = await hello(e, 'edits');
        const j = await docSocket(e, 'edits', a.said.ticket);
        const junk = new Uint8Array(16 * 1024).fill(0xff);
        const room = e.rooms.get('edits');
        const before = room.bytes;
        const big = new Y.Doc();
        const text = big.getText('t');
        let sv = Y.encodeStateVector(big);
        const typed = () =>
        {
            const f = frame(big, sv);

            sv = Y.encodeStateVector(big);
            return f;
        };

        junk.set([0, 2]);
        j.send(junk);

        const junked = await refused(j) && room.bytes === before;
        const d = await docSocket(e, 'edits', a.said.ticket);

        text.insert(0, 'x'.repeat(10 * 1024));
        d.send(typed());
        await new Promise((r) => setTimeout(r, 200));

        const grown = room.bytes;

        /* Keystrokes, each splitting the text it lands in: an item, and
           the piece of the text after it. */
        for (let i = 0; i < 10; i++)
        {
            text.insert(i * 500, 'k');
            d.send(typed());
        }

        await new Promise((r) => setTimeout(r, 200));

        const keyed = room.bytes - grown;
        const n = Math.floor((64 * 1024 - room.bytes) / 2) - 2048;

        text.insert(0, 'y'.repeat(n));
        d.send(typed());
        await new Promise((r) => setTimeout(r, 200));

        const took = room.doc.getText('t').toString().startsWith('y');

        text.insert(0, 'z'.repeat(20 * 1024));
        d.send(typed());

        const told = await a.next('refused');

        check(junked && grown > before + 20 * 1024 &&
              keyed > 10 * 2 * 256 && took && told.of === 'edit' &&
              told.why === 'big' && await refused(d) &&
              a.ws.readyState === WebSocket.OPEN &&
              room.bytes <= 64 * 1024 &&
              !room.doc.getText('t').toString().includes('z'),
              'a room is charged for what its document takes, and not ' +
              'for a frame it could not read, and an edit that could take ' +
              'it past its most is refused: the page is told, and its ' +
              'document socket closed and its room socket not');

        const b = await hello(e, 'edits');
        const bd = new Y.Doc();
        const pb = new WebsocketProvider(`ws://127.0.0.1:${e.address().port}` +
                                         '/doc', 'edits', bd,
                                         { WebSocketPolyfill: WebSocket,
                                           params: { ticket: b.said.ticket },
                                           disableBc: true });

        const bSynced = await synced(pb);

        bd.getText('t').insert(0, 'ok');
        await new Promise((r) => setTimeout(r, 300));
        check(bSynced && b.ws.readyState === WebSocket.OPEN &&
              room.doc.getText('t').toString().startsWith('oky') &&
              bd.getText('t').toString() === room.doc.getText('t').toString(),
              'and the room is joined, read and edited after the refusal');
        pb.destroy();
        bd.destroy();
        b.close();
        await new Promise((r) => setTimeout(r, 200));

        const [x, y, z] = [await hello(e, 'x'), await hello(e, 'y'),
                           await hello(e, 'z')];

        x.close();
        await new Promise((r) => setTimeout(r, 200));
        y.close();
        await new Promise((r) => setTimeout(r, 200));

        const huge = await hello(e, 'huge', '?piece=sunrise.gen');

        check(huge.said.why === 'rooms' && e.rooms.has('x') &&
              e.rooms.has('y'),
              'a room the empty ones would not make room for is refused, ' +
              'and they stay');

        const w = await hello(e, 'w');

        check(w.said.type === 'welcome' && !e.rooms.has('x') &&
              e.rooms.has('y'),
              'and a new room lets go of the empty ones it needs, oldest ' +
              'first, and no more');

        for (const c of [z, huge, w])
            c.close();
    }
    finally
    {
        e.shutdown();
    }

    /* A room already past its most -- seeded with more than that -- is
       joined and read by every page, and only what would add to it is
       refused. */
    const o = await relay({ port: 0, host: '127.0.0.1', tree,
                            roomMaxBytes: 16 * 1024 });

    try
    {
        const join = async () =>
        {
            const c = await hello(o, 'over');
            const doc = new Y.Doc();
            const p = new WebsocketProvider(
                `ws://127.0.0.1:${o.address().port}/doc`, 'over', doc,
                { WebSocketPolyfill: WebSocket,
                  params: { ticket: c.said.ticket }, disableBc: true });

            const ok = await synced(p);

            await new Promise((r) => setTimeout(r, 300));
            return { c, doc, p, ok };
        };
        const x = await join();
        const y = await join();
        const read = [x, y].every(({ c, doc, ok }) =>
            ok && c.ws.readyState === WebSocket.OPEN &&
            readFile(doc, 'airports.gen') ===
                readFile(o.rooms.get('over').doc, 'airports.gen'));

        x.doc.getMap('files').get('airports.gen')?.insert(0, '# more\n');

        const told = await x.c.next('refused');

        check(read && told.of === 'edit' && told.why === 'big' &&
              x.c.ws.readyState === WebSocket.OPEN &&
              !readFile(o.rooms.get('over').doc, 'airports.gen')
                  .startsWith('# more') &&
              y.c.ws.readyState === WebSocket.OPEN,
              'a room past its most is joined and read, and an edit that ' +
              'adds to it is refused');

        /* A switch leaves what it replaced in the document, and is not
           read before it is made: a room past its most makes none. */
        y.c.send({ type: 'switch', piece: 'ebb.gen' });

        const unswitched = await y.c.next('refused');

        check(unswitched.of === 'switch' &&
              /as large as/.test(unswitched.why) &&
              pieceName(o.rooms.get('over').doc) === 'airports.gen',
              'and so is a switch');

        /* An edit that frees more than it splits is taken. */
        const was = o.rooms.get('over').bytes;
        const gen = y.doc.getMap('files').get('airports.gen');

        gen.delete(10, gen.length - 20);
        await new Promise((r) => setTimeout(r, 300));
        check(readFile(o.rooms.get('over').doc, 'airports.gen').length === 20 &&
              o.rooms.get('over').bytes < was && await y.c.none('refused'),
              'and an edit that cuts it down is taken');

        for (const { c, doc, p } of [x, y])
        {
            p.destroy();
            doc.destroy();
            c.close();
        }
    }
    finally
    {
        o.shutdown();
    }

    /* What an update makes the document hold can be many times its size:
       deleting every other character splits a text into a struct for
       each, at 2 B of update apiece, and an empty object in an array, a
       client or a root type is a few bytes of one. None takes a room past
       its most, by what each of them holds of the heap as measured. */
    const m = await relay({ port: 0, host: '127.0.0.1', tree,
                            roomMaxBytes: 1024 * 1024 });

    try
    {
        const N = 100000;
        const text = new Y.Doc();

        text.getText('t').insert(0, 'x'.repeat(N));

        /* Every other character of `text', deleted, in updates of
           `per'. */
        const split = (per) =>
        {
            const out = [];

            for (let start = 1; start < N; start += 2 * per)
            {
                const e = encoding.createEncoder();
                const end = Math.min(N, start + 2 * per);

                encoding.writeVarUint(e, 0);
                encoding.writeVarUint(e, 1);
                encoding.writeVarUint(e, text.clientID);
                encoding.writeVarUint(e, Math.ceil((end - start) / 2));

                for (let i = start; i < end; i += 2)
                {
                    encoding.writeVarUint(e, i);
                    encoding.writeVarUint(e, 1);
                }

                out.push(encoding.toUint8Array(e));
            }

            return out;
        };
        const made = (f) =>
        {
            const d = new Y.Doc();

            d.transact(() => f(d));
            return [Y.encodeStateAsUpdate(d)];
        };
        const structs = (doc) => [...doc.store.clients.values()]
            .reduce((sum, s) => sum + s.length, 0);

        for (const [what, updates, holds] of [
            ['a text with every other character deleted',
             [Y.encodeStateAsUpdate(text), ...split(N / 8)],
             (doc) => 256 * structs(doc)],
            ['an array of empty objects',
             made((d) => d.getArray('a').push(
                 Array.from({ length: N }, () => ({})))),
             (doc) => 66 * doc.getArray('a').length],
            ['a client for every key of a map',
             [Y.mergeUpdates(Array.from({ length: N / 5 }, () => made(
                 (d) => d.getMap('m').set('k', 1))[0]))],
             (doc) => 490 * doc.store.clients.size],
            ['a root type for every item',
             made((d) =>
             {
                 for (let i = 0; i < N / 5; i++)
                     d.getMap(`r${i}`).set('k', 1);
             }),
             (doc) => 300 * doc.share.size],
            ['an XML element with a long name',
             made((d) => d.getXmlFragment('x').insert(
                 0, [new Y.XmlElement('e'.repeat(6 * N))])),
             (doc) => 2 * doc.getXmlFragment('x').toArray()
                 .reduce((sum, e) => sum + e.nodeName.length, 0)],
            ['a document in the document',
             made((d) => d.getArray('d').insert(
                 0, [new Y.Doc({ guid: 'g'.repeat(6 * N) })])),
             (doc) => [...doc.subdocs]
                 .reduce((sum, sub) => sum + 2 * sub.guid.length, 0)]])
        {
            const name = what.replaceAll(' ', '-');
            const c = await hello(m, name);
            const d = await docSocket(m, name, c.said.ticket);
            const room = m.rooms.get(name);

            for (const u of updates)
                if (d.readyState === WebSocket.OPEN)
                {
                    d.send(frame(u));
                    await new Promise((r) => setTimeout(r, 200));
                }

            check(room.bytes <= 1024 * 1024 &&
                  holds(room.doc) <= 1024 * 1024 &&
                  (await fetch(`http://127.0.0.1:${m.address().port}/`)).ok,
                  `${what} takes a room no further than its most`);
            d.close();
            c.close();
        }

        /* A root type an update makes is charged once it is made. */
        {
            const c = await hello(m, 'roots');
            const d = await docSocket(m, 'roots', c.said.ticket);
            const room = m.rooms.get('roots');
            const [roots, before] = [room.doc.share.size, room.bytes];
            d.send(frame(made((doc) =>
            {
                for (let i = 0; i < 10; i++)
                    doc.getMap(`r${i}`).set('k', 1);
            })[0]));
            await new Promise((r) => setTimeout(r, 200));
            check(room.doc.share.size === roots + 10 &&
                  room.bytes - before >= 10 * 1024,
                  'a root type an update makes is charged');
            d.close();
            c.close();
        }

        /* A document brought back whole to a room the relay lost, two
           clients' keystrokes in turn, splits nothing, and is not
           charged as if each of its items split two structs. */
        {
            const c = await hello(m, 'restored', '?piece=');
            const d = await docSocket(m, 'restored', c.said.ticket);
            const doc = new Y.Doc();

            for (let i = 0; i < 2000; i++)
            {
                doc.clientID = 1 + i % 2;
                doc.getText('t').insert(i, 'x');
            }

            d.send(frame(doc));
            await new Promise((r) => setTimeout(r, 300));
            check(m.rooms.get('restored').doc.getText('t').length === 2000,
                  'a document brought back whole is charged only the ' +
                  'splits it can make ' +
                  `(${Math.round(m.rooms.get('restored').bytes / 1024)} KiB)`);
            d.close();
            c.close();
        }

        /* Nor does an update whose type is written in two bytes, which
           would make its first bytes a sync step 1's. */
        const c = await hello(m, 'long');
        const d = await docSocket(m, 'long', c.said.ticket);
        const long = encoding.createEncoder();
        const cut = refused(d);

        encoding.writeUint8(long, 0x80);
        encoding.writeUint8(long, 0);
        encoding.writeVarUint(long, syncProtocol.messageYjsUpdate);
        encoding.writeVarUint8Array(long, made((doc) =>
            doc.getText('t').insert(0, 'x'.repeat(1024 * 1024)))[0]);
        d.send(encoding.toUint8Array(long));
        check(await cut && m.rooms.get('long').doc.getText('t').length === 0,
              'and nor does one whose type is written long');
        c.close();
    }
    finally
    {
        m.shutdown();
    }

    /* The rooms' budget holds as they grow, as when they are made: an
       edit lets go of the rooms empty longest that it needs, and with
       too few to let go it is refused. */
    const g = await relay({ port: 0, host: '127.0.0.1', tree,
                            roomsMaxBytes: 272 * 1024,
                            roomMaxBytes: 224 * 1024 });

    try
    {
        const total = () => [...g.rooms.values()]
            .reduce((sum, r) => sum + r.bytes, 0);
        const edit = async (name) =>
        {
            const c = await hello(g, name);
            const d = await docSocket(g, name, c.said.ticket);
            const doc = new Y.Doc();
            let sv = Y.encodeStateVector(doc);

            c.errors = [];
            c.ws.on('message', (m) =>
            {
                if (JSON.parse(m).type === 'refused')
                    c.errors.push(JSON.parse(m));
            });
            c.type = async (kib) =>
            {
                doc.getText('t').insert(0, 'x'.repeat(kib * 1024));
                d.send(frame(doc, sv));
                sv = Y.encodeStateVector(doc);
                await new Promise((r) => setTimeout(r, 200));
                return g.rooms.get(name)?.doc.getText('t').length / 1024;
            };
            return c;
        };
        const e = await hello(g, 'idle');

        e.close();
        await new Promise((r) => setTimeout(r, 200));

        const a = await edit('a');
        const b = await edit('b');
        const took = [await a.type(40), await b.type(40)];

        await a.type(40);

        const told = a.errors[0] ?? {};
        const under = total() <= 272 * 1024 && g.rooms.has('idle');

        check(took.join() === '40,40' && told.why === 'rooms' && under &&
              g.rooms.get('a').doc.getText('t').length === 40 * 1024,
              'an edit that would take the rooms past their budget is ' +
              'refused, and no empty room goes for it in vain');
        check(await b.type(12) === 52 && !g.rooms.has('idle') &&
              total() <= 272 * 1024,
              'and one an empty room going makes room for lets it go');

        for (const c of [a, b])
            c.close();
    }
    finally
    {
        g.shutdown();
    }

    /* An update that builds on what the room does not have is held
       until that arrives, and charged while it is; held past a little,
       it goes, and so does the socket that sent it. */
    const h = await relay({ port: 0, host: '127.0.0.1', tree });

    try
    {
        const c = await hello(h, 'held');
        const d = await docSocket(h, 'held', c.said.ticket);
        const room = h.rooms.get('held');
        const doc = new Y.Doc();
        const text = doc.getText('t');
        const before = room.bytes;
        const closed = refused(d);
        let sv;
        const after = async (insert) =>
        {
            sv = Y.encodeStateVector(doc);
            insert();
            d.send(frame(doc, sv));
            await new Promise((r) => setTimeout(r, 200));
        };

        text.insert(0, 'withheld');
        await after(() => text.insert(0, 'k'.repeat(1024)));

        const small = room.bytes - before;
        const open = d.readyState === WebSocket.OPEN;

        await after(() => text.insert(0, 'k'.repeat(100 * 1024)));
        check(small >= 2 * 1024 && small < 4 * 1024 && open &&
              await closed && room.bytes === before &&
              room.doc.store.pendingStructs === null,
              'an update held for what it builds on is charged, and one ' +
              'that leaves much held goes, and its socket with it');

        /* Held past the most by one socket's frame, most of it another's:
           the other goes. */
        const holder = async (kib) =>
        {
            const p = await hello(h, 'held');
            const s = await docSocket(h, 'held', p.said.ticket);
            const own = new Y.Doc();

            own.getText('t').insert(0, 'withheld');

            const base = Y.encodeStateVector(own);

            own.getText('t').insert(0, 'k'.repeat(kib * 1024));
            s.send(frame(own, base));
            await new Promise((r) => setTimeout(r, 200));
            return s;
        };
        const most = await holder(60);
        const cut = refused(most);
        const last = await holder(8);

        check(await cut && last.readyState === WebSocket.OPEN &&
              room.doc.store.pendingStructs === null,
              'and of the sockets holding it, the one that holds the most ' +
              'goes');
        last.close();
        c.close();
    }
    finally
    {
        h.shutdown();
    }

    /* What a run keeps is charged to its room: past what the room or
       the rooms may be charged, the run overflows, and an edit to the
       document that needs what it holds overflows it. */
    const l = await relay({ port: 0, host: '127.0.0.1', tree,
                            roomsMaxBytes: 150 * 1024,
                            roomMaxBytes: 128 * 1024 });

    try
    {
        const pad = 'x'.repeat(20 * 1024);
        const play = async (name) =>
        {
            const c = await hello(l, name);

            c.send({ type: 'transport',
                     data: { type: 'transport', op: 'start', seq: 0 } });
            c.log = async (n) =>
            {
                for (let i = 0; i < n; i++)
                    c.send({ type: 'log', run: `${c.said.peer}#0`,
                             data: { type: 'knob', pad } });

                await new Promise((r) => setTimeout(r, 200));
                return l.rooms.get(name).run;
            };
            return c;
        };
        const a = await play('la');
        const b = await play('lb');
        const doc = (await hello(l, 'la')).said;
        const before = l.rooms.get('la').bytes;
        const kept = (await a.log(4)).length;
        const charged = l.rooms.get('la').bytes - before;
        const seeded = l.rooms.get('lb').bytes;
        const spilled = (await b.log(1)).overflowed;

        check(kept === 4 && charged > 80 * 1024 && spilled &&
              l.rooms.get('lb').bytes === seeded,
              'a run\'s commands are charged to its room, and one past ' +
              'what the rooms may be charged overflows the run');

        /* An edit the room has no room for even without them leaves
           them be. */
        const big = (await hello(l, 'la')).said;
        const bd = await docSocket(l, 'la', big.ticket);
        const huge = new Y.Doc();

        huge.getText('h').insert(0, 'h'.repeat(100 * 1024));
        bd.send(frame(huge));
        await new Promise((r) => setTimeout(r, 200));
        check(!l.rooms.get('la').run.overflowed &&
              l.rooms.get('la').run.length === 4 &&
              l.rooms.get('la').doc.getText('h').length === 0,
              'an edit too large for the room even without the run\'s ' +
              'commands is refused, and leaves them be');

        const d = await docSocket(l, 'la', doc.ticket);
        const ydoc = new Y.Doc();

        ydoc.getText('t').insert(0, 'y'.repeat(20 * 1024));
        d.send(frame(ydoc));
        await new Promise((r) => setTimeout(r, 200));
        check(l.rooms.get('la').run.overflowed &&
              l.rooms.get('la').doc.getText('t').length === 20 * 1024 &&
              l.rooms.get('la').bytes < before + 50 * 1024,
              'and an edit the room has room for only without the run\'s ' +
              'commands overflows the run, and is taken');
        d.close();
        a.close();
        b.close();
    }
    finally
    {
        l.shutdown();
    }

    /* And it holds them as what they are charged: parsed, a command of
       empty objects holds twenty times its JSON. */
    const ob = await relay({ port: 0, host: '127.0.0.1', tree });

    try
    {
        const c = await hello(ob, 'objects');
        const room = ob.rooms.get('objects');
        const lines = Array.from({ length: 8 }, (_, i) => JSON.stringify(
            { type: 'log', run: `${c.said.peer}#0`,
              data: { type: 'knob', i, x: Array(100000).fill({}) } }));

        c.send({ type: 'transport',
                 data: { type: 'transport', op: 'start', seq: 0 } });
        await new Promise((r) => setTimeout(r, 200));

        const heap = heapUsed();
        const before = room.bytes;

        for (const line of lines)
            c.ws.send(line);

        await new Promise((r) => setTimeout(r, 500));

        const held = heapUsed() - heap;
        const charged = room.bytes - before;

        check(room.run.length === 8 && held < 2 * charged + 1024 * 1024,
              'a run\'s commands hold about what they are charged ' +
              `(${Math.round(held / 1024)} KiB held, ` +
              `${Math.round(charged / 1024)} KiB charged)`);
        c.close();

        /* And its start is the fields a page reads of one. */
        const s = await hello(ob, 'start');
        const start = JSON.stringify(
            { type: 'transport',
              data: { type: 'transport', op: 'start', seq: 0,
                      x: Array(300000).fill({}) } });
        const was = heapUsed();

        s.ws.send(start);
        await new Promise((r) => setTimeout(r, 300));

        const kept = heapUsed() - was;

        check(ob.rooms.get('start').playing?.seq === 0 &&
              !('x' in ob.rooms.get('start').playing) && kept < 1024 * 1024,
              'a start is kept as the fields a page reads of one ' +
              `(${Math.round(kept / 1024)} KiB held)`);
        s.close();
    }
    finally
    {
        ob.shutdown();
    }

    /* A room waits for one start's document at a time, however many
       starts come: each would look at the whole document at every update
       for as long as it waited. A stop ends the wait. */
    const k = await relay({ port: 0, host: '127.0.0.1', tree });

    try
    {
        const c = await hello(k, 'starts');
        const room = k.rooms.get('starts');
        const waits = () => room.doc._observers.get('update').size;
        const idle = waits();

        for (let i = 0; i < 100; i++)
            c.send({ type: 'transport',
                     data: { type: 'transport', op: 'start', seq: i,
                             piece: { hash: `h${i}` } } });

        await new Promise((r) => setTimeout(r, 300));

        const playing = waits();

        c.send({ type: 'transport',
                 data: { type: 'transport', op: 'stop' } });
        await new Promise((r) => setTimeout(r, 200));
        check(room.playing === null && playing === idle + 1 &&
              waits() === idle,
              'a room waits for one start\'s document at a time, and a ' +
              'stop ends the wait');
        c.close();
    }
    finally
    {
        k.shutdown();
    }

    /* A run is charged the document its start named, a copy of every
       text kept for late joiners, and that as a catch-up's JSON once one
       is asked for; a stop gives them back. */
    const v = await relay({ port: 0, host: '127.0.0.1', tree });

    try
    {
        const c = await hello(v, 'kept');
        const room = v.rooms.get('kept');
        const texts = Object.values(snapshot(room.doc).files)
            .reduce((n, t) => n + t.length, 0);
        const idle = room.bytes;

        c.send({ type: 'transport',
                 data: { type: 'transport', op: 'start', seq: 0,
                         piece: { hash: await hashOf(room.doc) } } });
        await new Promise((r) => setTimeout(r, 300));

        const named = room.bytes - idle;

        c.send({ type: 'catchup' });
        await c.next('catchup');

        const asked = room.bytes - idle;

        c.send({ type: 'transport',
                 data: { type: 'transport', op: 'stop' } });
        await new Promise((r) => setTimeout(r, 200));
        check(named >= 2 * texts && asked >= named + texts &&
              room.bytes === idle,
              'a run is charged the document its start named, and its ' +
              'catch-up, until it stops');
        c.close();
    }
    finally
    {
        v.shutdown();
    }

    /* A room let go of while a switch in it waits to be played is
       charged nothing after: the switch's Play would give back its run's
       commands a second time. */
    const s = await relay({ port: 0, host: '127.0.0.1', tree,
                            roomsMaxBytes: 160 * 1024,
                            roomMaxBytes: 128 * 1024 });

    try
    {
        const b = await hello(s, 'grows');
        const a = await hello(s, 'goes');
        const d = await docSocket(s, 'grows', b.said.ticket);
        const pad = 'x'.repeat(20 * 1024);
        const ydoc = new Y.Doc();

        a.send({ type: 'transport',
                 data: { type: 'transport', op: 'start', seq: 0 } });

        for (let i = 0; i < 2; i++)
            a.send({ type: 'log', run: `${a.said.peer}#0`,
                     data: { type: 'knob', pad } });

        await new Promise((r) => setTimeout(r, 200));

        const logged = s.rooms.get('goes').run.length;

        a.send({ type: 'switch', piece: 'airports.gen' });
        a.ws.terminate();
        await new Promise((r) => setTimeout(r, 15));
        ydoc.getText('t').insert(0, 'y'.repeat(40 * 1024));
        d.send(frame(ydoc));
        await new Promise((r) => setTimeout(r, 300));

        const room = s.rooms.get('grows');

        check(logged === 2 && !s.rooms.has('goes') &&
              room.doc.getText('t').length === 40 * 1024 &&
              room.ctx.costs.bytes === room.bytes,
              'a room let go of while a switch in it is played is charged ' +
              'nothing after');
        d.close();
        b.close();
    }
    finally
    {
        s.shutdown();
    }

    /* A ticket past its life, not yet swept, lets in nobody past the
       most. */
    const t = await relay({ port: 0, host: '127.0.0.1', tree, peersMax: 1,
                            ticketMs: 1000 });

    try
    {
        const a = await hello(t, 'lapsed');

        await new Promise((r) => setTimeout(r, 1050));

        const late = await Promise.all([1, 2, 3].map(async () =>
        {
            const c = await open(t, 'lapsed');

            c.send({ type: 'hello', name: 'late', protocol: PROTOCOL,
                     tickets: true, was: a.said.ticket });
            c.said = await Promise.race([c.next('welcome'), c.next('error')]);
            return c;
        }));

        check(late.every((c) => c.said.why === 'full') &&
              t.rooms.get('lapsed').peers.size === 1 &&
              a.ws.readyState === WebSocket.OPEN,
              'a page joining again with a lapsed ticket counts against ' +
              'the most people');

        for (const x of [a, ...late])
            x.close();
    }
    finally
    {
        t.shutdown();
    }

    /* A seeded room is charged for what it was seeded with; one made and
       seeded and then refused for it is one of its client's new rooms. */
    const w = await relay({ port: 0, host: '127.0.0.1', tree,
                            roomsMaxBytes: 200 * 1024,
                            roomLimits: [{ burst: 3, refillMs: 60000 },
                                         null, null] });

    try
    {
        const big = await hello(w, 'big', '?piece=sunrise.gen');
        const bigger = await hello(w, 'bigger', '?piece=sunrise.gen');
        const small = await hello(w, 'small', '?piece=');

        check(big.said.type === 'welcome' && bigger.said.why === 'rooms' &&
              small.said.type === 'welcome',
              'rooms are charged for the piece they are seeded with');

        const again = await hello(w, 'again', '?piece=sunrise.gen');

        check(/too many new rooms/.test(again.said.text),
              'and a room made and refused for it counts as one of its ' +
              'client\'s new rooms');

        for (const x of [big, bigger, small, again])
            x.close();
    }
    finally
    {
        w.shutdown();
    }
}

/* Every message a room socket sends is limited by type, as chat is: past
   its rate it is dropped and the page told, and a socket that keeps on is
   cut. A document socket's frames are limited too. */
async function ratesLimited ()
{
    const types = ['ping', 'signal', 'relayed', 'log', 'transport', 'seat',
                   'switch', 'catchup', 'chat', 'other'];
    const q = await relay({ port: 0, host: '127.0.0.1', tree,
                            limits: { room: Object.fromEntries(
                                          types.map((t) => [t, [3, 0.001]])),
                                      drops: [5, 0.001], doc: [3, 0.001] } });
    const at = `127.0.0.1:${q.address().port}`;
    const join = async (room, name) =>
    {
        const c = new Client(`ws://${at}/room/${room}`, name);

        await c.open();
        c.send({ type: 'hello', name, protocol: PROTOCOL, tickets: true });
        c.welcome = await c.next('welcome');
        return c;
    };
    const count = (c, type) => c.got.filter((m) => m.type === type).length;

    try
    {
        /* Each type: what is sent, and what three of it come to. */
        for (const [type, m, seen] of [
            ['ping', { type: 'ping', t0: 1 }, (a) => count(a, 'pong')],
            ['signal', (b) => ({ type: 'signal', to: b, data: {} }),
             (a, b) => count(b, 'signal')],
            ['relayed', { type: 'relayed', data: {} },
             (a, b) => count(b, 'relayed')],
            ['log', { type: 'log', data: {}, run: 'x#0' }, null],
            ['transport', { type: 'transport', data: { op: 'tempo' } },
             (a, b) => count(b, 'transport')],
            ['seat', { type: 'seat', seat: 1 }, (a) => count(a, 'seats')],
            ['switch', { type: 'switch', piece: 'ebb.gen' },
             (a) => count(a, 'switched')],
            ['catchup', { type: 'catchup' }, (a) => count(a, 'catchup')],
            ['chat', { type: 'chat', channel: 'stage', text: 'hi' },
             (a, b) => count(b, 'chat')],
            ['other', { type: 'hello', protocol: PROTOCOL, tickets: true },
             null],
            ['other', { type: 'constructor' }, null]])
        {
            const a = await join(`rate-${type}`, 'A');
            const b = await join(`rate-${type}`, 'B');

            for (let i = 0; i < 5; i++)
                a.send(typeof m === 'function' ? m(b.welcome.peer) : m);

            await new Promise((r) => setTimeout(r, 300));

            /* A catch-up is told of each time: the page waits on each. */
            const told = a.got.filter((x) => x.type === 'refused');
            const tells = type === 'catchup' ? 2 : 1;

            check(told.length === tells &&
                  told.every((x) => x.of === type) &&
                  /too fast/.test(told[0].why) &&
                  (seen === null || seen(a, b) === 3) &&
                  a.ws.readyState === WebSocket.OPEN,
                  `${type === 'other' ? `${m.type} (as other)` : type} ` +
                  'messages past their rate are dropped, and the ' +
                  `page told ${tells === 1 ? 'once' : 'of each'}`);
            a.close();
            b.close();
        }

        /* A Play past its rate is played all the same, the mesh having
           carried it, but not the one whose refusal cuts the socket. */
        {
            const a = await join('rate-cut', 'A');

            for (let seq = 0; seq < 9; seq++)
                a.send({ type: 'transport',
                         data: { type: 'transport', op: 'start', seq } });

            const cut = await refused(a.ws);

            check(cut && q.rooms.get('rate-cut').playing?.seq === 7,
                  'a Play that gets its socket cut for flooding does not ' +
                  `change the run (${q.rooms.get('rate-cut').playing?.seq})`);
        }

        /* A catch-up past its rate is refused, and the page's wait for
           it ends then. */
        {
            const r = new Room(`ws://${at}`, 'rate-room', 'R');

            await r.connect();

            for (let i = 0; i < 3; i++)
                await r.catchUp(2000);

            const t0 = performance.now();
            const why = await r.catchUp(5000).then(() => '', (e) => e.message);

            check(/refused a catchup: too fast/.test(why) &&
                  performance.now() - t0 < 1000,
                  'a catch-up the relay refuses ends the page\'s wait for ' +
                  `it (${why || 'answered'})`);
            r.close();
        }

        /* A start past its rate is not passed on, and is the run all
           the same: the mesh carried it. */
        {
            const a = await join('rate-start', 'A');
            const b = await join('rate-start', 'B');

            for (let seq = 0; seq < 5; seq++)
                a.send({ type: 'transport',
                         data: { type: 'transport', op: 'start', seq } });

            await new Promise((r) => setTimeout(r, 300));
            check(count(b, 'transport') === 3 &&
                  q.rooms.get('rate-start').playing?.seq === 4,
                  'a start past its rate is not passed on, and is the run ' +
                  'all the same');
            a.close();
            b.close();
        }

        /* Each chat line the page counts is told of, past the first. */
        {
            const a = await join('rate-chat-n', 'A');

            for (let n = 1; n <= 5; n++)
                a.send({ type: 'chat', channel: 'stage', text: 'hi', n });

            await new Promise((r) => setTimeout(r, 300));
            check(a.got.filter((x) => x.type === 'refused')
                      .map((x) => x.n).join() === '4,5',
                  'every counted chat line past the rate is refused with ' +
                  'its count, more than one a second');
            a.close();
        }

        /* A switch refused names its piece, whatever was sent as one. */
        {
            const a = await join('rate-evil', 'A');

            for (let i = 0; i < 5; i++)
                a.send({ type: 'switch', piece: { toString: 1, valueOf: 1 } });

            await new Promise((r) => setTimeout(r, 300));
            check(a.got.some((x) => x.type === 'refused' &&
                                    /too fast/.test(x.why)) &&
                  a.ws.readyState === WebSocket.OPEN &&
                  (await fetch(`http://${at}/`)).ok,
                  'a switch past its rate whose piece throws is refused, ' +
                  'and the relay lives');
            a.close();
        }

        /* Before a hello, as after it. */
        {
            const w = new WebSocket(`ws://${at}/room/early`);

            await new Promise((r) => w.on('open', r));

            const cut = refused(w);

            for (let i = 0; i < 10; i++)
                w.send(i % 2 ? '{' : JSON.stringify({ type: 'ping', t0: i }));

            check(await cut && !q.rooms.has('early'),
                  'a socket that goes on sending something before its ' +
                  'hello is cut');
        }

        /* And one that goes on is cut, and told why. */
        for (const m of [{ type: 'ping', t0: 1 },
                         { type: 'chat', channel: 'stage', text: 'hi' }])
        {
            const a = await join(`flood-${m.type}`, 'A');

            for (let i = 0; i < 12; i++)
                a.send(m);

            const e = await a.next('error');

            check(e.why === 'flood' && await refused(a.ws),
                  `a socket that keeps on past its rate is cut (${m.type})`);
        }

        /* A cursor past the document socket's rate is dropped; an update
           past it cuts the socket. */
        {
            const a = await join('docrate', 'A');
            const d = await docSocket(q, 'docrate', a.welcome.ticket);
            const step1 = encoding.createEncoder();
            const cursor = encoding.createEncoder();
            const aw = new awarenessProtocol.Awareness(new Y.Doc());

            encoding.writeVarUint(step1, 0);
            syncProtocol.writeSyncStep1(step1, new Y.Doc());
            aw.setLocalState({});
            encoding.writeVarUint(cursor, 1);
            encoding.writeVarUint8Array(cursor,
                awarenessProtocol.encodeAwarenessUpdate(aw, [aw.clientID]));

            const closed = refused(d);

            for (let i = 0; i < 5; i++)
                d.send(encoding.toUint8Array(i < 3 ? step1 : cursor));

            await new Promise((r) => setTimeout(r, 300));

            const open = d.readyState === WebSocket.OPEN;

            d.send(encoding.toUint8Array(step1));
            check(open && await closed,
                  'a cursor past the document socket\'s rate is dropped, ' +
                  'and an update past it cuts the socket');

            /* The rate is the peer's: a socket opened again on its
               ticket has none of it back. */
            const e = await docSocket(q, 'docrate', a.welcome.ticket);
            const doc = new Y.Doc();
            const cut = refused(e);

            doc.getText('t').insert(0, 'again');
            e.send(frame(doc));
            check(await cut &&
                  q.rooms.get('docrate').doc.getText('t').length === 0,
                  'and one opened again in its place has none of the rate ' +
                  'back');
            aw.destroy();
            a.close();
        }
    }
    finally
    {
        q.shutdown();
    }
}

/* What one socket's messages have queued for others is bounded in bytes,
   however its `to' is written. */
async function fanoutBounded ()
{
    const f = await relay({ port: 0, host: '127.0.0.1', tree,
                            limits: { fanout: [2 * 1024 * 1024, 1024],
                                      runFanout: [2 * 1024 * 1024, 1024] } });
    const join = async (name) =>
    {
        const c = new Client(`ws://127.0.0.1:${f.address().port}/room/fan`,
                             name);

        await c.open();
        c.send({ type: 'hello', name, protocol: PROTOCOL, tickets: true });
        c.welcome = await c.next('welcome');
        return c;
    };

    try
    {
        const [s, v, w] = [await join('S'), await join('V'), await join('W')];

        s.send({ type: 'relayed', to: Array(1000).fill(v.welcome.peer),
                 data: { once: true } });
        await v.next('relayed');
        check(await v.none('relayed', 300),
              'a relayed gesture naming a peer many times reaches it once');

        const pad = 'x'.repeat(900 * 1024);

        s.send({ type: 'relayed', data: { pad } });
        s.send({ type: 'relayed', data: { pad } });

        const told = await s.next('refused');

        await new Promise((r) => setTimeout(r, 300));
        check(told.of === 'relayed' &&
              [v, w].every((c) => c.got.filter((m) =>
                  m.type === 'relayed').length === 1 &&
                  c.ws.readyState === WebSocket.OPEN) &&
              s.ws.readyState === WebSocket.OPEN,
              'past its bytes for others a socket\'s gesture goes to ' +
              'nobody, and the page is told');

        /* A start past its bytes goes to nobody from here, and is the
           run here all the same: the mesh carried it. An edit, which only
           goes from here, leaves the run as it was. */
        const start = (seq) => s.send({ type: 'transport',
                                        data: { type: 'transport',
                                                op: 'start', seq,
                                                piece: { hash: pad } } });
        const edit = (text) => s.send({ type: 'transport',
                                        run: `${s.welcome.peer}#2`,
                                        data: { type: 'edit', text } });

        start(1);
        start(2);

        const no = await s.next('refused');

        edit('x');
        edit(pad);
        await new Promise((r) => setTimeout(r, 300));
        check(no.of === 'transport' && no.op === 'start' &&
              f.rooms.get('fan').playing?.seq === 2 &&
              f.rooms.get('fan').run.length === 1 &&
              [v, w].every((c) => c.got.filter((m) =>
                  m.type === 'transport').length === 2),
              'past its bytes for others a transport command goes to ' +
              'nobody, and the page is told which; a start is the run ' +
              'all the same, and an edit is not kept');

        s.close();
        check((await v.next('left')).peer === s.welcome.peer,
              'and its leaving is still told to the others');

        for (const c of [v, w])
            c.close();
    }
    finally
    {
        f.shutdown();
    }
}

/* What a socket sends is bounded in bytes before it is read: a frame as
   long as the relay takes goes through, and a socket sending them back to
   back is cut. */
async function inBytesBounded ()
{
    const g = await relay({ port: 0, host: '127.0.0.1', tree,
                            limits: { roomIn: [2 * 1024 * 1024, 1024],
                                      docIn: [64 * 1024, 1024],
                                      drops: [40, 0.001] } });
    const at = `127.0.0.1:${g.address().port}`;
    const join = async (name) =>
    {
        const c = new Client(`ws://${at}/room/in`, name);

        await c.open();
        c.send({ type: 'hello', name, protocol: PROTOCOL, tickets: true });
        c.welcome = await c.next('welcome');
        return c;
    };

    try
    {
        const [s, v] = [await join('S'), await join('V')];
        const line = { type: 'relayed', data: 'x'.repeat(1023 * 1024) };

        s.send(line);
        await v.next('relayed');
        check(s.ws.readyState === WebSocket.OPEN,
              'a room line near the longest the relay takes goes through');

        for (let i = 0; i < 6; i++)
            s.send(line);

        const e = await s.next('error');

        check(e.why === 'flood' && await refused(s.ws) &&
              s.got.filter((m) => m.type === 'refused' &&
                                  m.of === 'bytes').length === 1 &&
              v.got.filter((m) => m.type === 'relayed').length === 1,
              'a socket sending the longest lines back to back is refused ' +
              'past its bytes, and then cut');

        const d = await docSocket(g, 'in', v.welcome.ticket);
        const doc = new Y.Doc();
        const text = doc.getText('t');
        const closed = refused(d);

        text.insert(0, 'x'.repeat(40 * 1024));
        d.send(frame(doc));
        await new Promise((r) => setTimeout(r, 200));

        const took = g.rooms.get('in').doc.getText('t').length;

        text.insert(0, 'x'.repeat(40 * 1024));
        d.send(frame(doc));
        check(took === 40 * 1024 && await closed &&
              g.rooms.get('in').doc.getText('t').length === took,
              'a document update past the socket\'s bytes cuts it');
        doc.destroy();
        v.close();
    }
    finally
    {
        g.shutdown();
    }

    /* And an address's peers' updates between them: one past what they
       have sent together is cut, however little its own peer has. */
    const u = await relay({ port: 0, host: '127.0.0.1', tree,
                            updateLimits: [{ burst: 64 * 1024,
                                             refillMs: 1000 }, null, null] });

    try
    {
        const edit = async (name) =>
        {
            const c = new Client(`ws://127.0.0.1:${u.address().port}/room/up`,
                                 name);

            await c.open();
            c.send({ type: 'hello', name, protocol: PROTOCOL, tickets: true });

            const { ticket } = await c.next('welcome');
            const d = await docSocket(u, 'up', ticket);
            const doc = new Y.Doc();
            const cut = refused(d, 500);

            doc.getText(name).insert(0, 'x'.repeat(40 * 1024));
            d.send(frame(doc));
            return [c, await cut];
        };
        const [a, aCut] = await edit('a');
        const [b, bCut] = await edit('b');
        const { doc } = u.rooms.get('up');

        check(!aCut && bCut && doc.getText('a').length === 40 * 1024 &&
              doc.getText('b').length === 0,
              'an update past what its address\'s peers have sent ' +
              'between them cuts its socket');
        a.close();
        b.close();
    }
    finally
    {
        u.shutdown();
    }
}

/* A sync step 1 is answered with the whole document, so a document
   socket may ask for one only so often. */
async function syncAsksBounded ()
{
    const y = await relay({ port: 0, host: '127.0.0.1', tree });

    try
    {
        const at = `127.0.0.1:${y.address().port}`;
        const a = new Client(`ws://${at}/room/asks`, 'A');

        await a.open();
        a.send({ type: 'hello', name: 'A', protocol: PROTOCOL,
                 tickets: true });

        const { ticket } = await a.next('welcome');
        const d = await docSocket(y, 'asks', ticket);
        const step1 = encoding.createEncoder();
        let answers = 0;

        encoding.writeVarUint(step1, 0);
        syncProtocol.writeSyncStep1(step1, new Y.Doc());
        d.on('message', (x) =>
        {
            const b = new Uint8Array(x);

            if (b[0] === 0 && b[1] === syncProtocol.messageYjsSyncStep2)
                answers++;
        });

        const closed = refused(d);

        for (let i = 0; i < 10; i++)
            d.send(encoding.toUint8Array(step1));

        check(await closed && answers === 4,
              `a document socket asking for the document again and again ` +
              `is answered ${answers} times, and closed`);

        const e = await docSocket(y, 'asks', ticket);
        let more = 0;

        e.on('message', (x) =>
        {
            const b = new Uint8Array(x);

            if (b[0] === 0 && b[1] === syncProtocol.messageYjsSyncStep2)
                more++;
        });

        const shut = refused(e);

        e.send(encoding.toUint8Array(step1));
        check(await shut && more === 0,
              'and one opened again on the same ticket is not answered ' +
              'again');
        a.close();
    }
    finally
    {
        y.shutdown();
    }
}

/* Past its share of memory, whatever the rooms are charged, the relay
   sheds load a thing at a time: the empty rooms, then what a run keeps
   for late joiners, and only then the room charged the most, whose
   people are told why. */
async function memoryShed ()
{
    const q = await relay({ port: 0, host: '127.0.0.1', tree, metricsPort: 0,
                            memoryMaxBytes: 2 ** 40 });
    const at = `127.0.0.1:${q.address().port}`;
    const join = async (room, name) =>
    {
        const c = new Client(`ws://${at}/room/${room}`, name);

        await c.open();
        c.send({ type: 'hello', name, protocol: PROTOCOL, tickets: true });
        c.welcome = await c.next('welcome');
        return c;
    };
    const until = async (cond) =>
    {
        for (let i = 0; i < 100 && !cond(); i++)
            await new Promise((r) => setTimeout(r, 50));
    };
    const usage = process.memoryUsage;
    const write = process.stderr.write;
    const shed = [];

    try
    {
        (await join('idle', 'E')).close();

        const [b, p] = [await join('big', 'B'), await join('played', 'P')];
        const d = await docSocket(q, 'big', b.welcome.ticket);
        const doc = new Y.Doc();

        doc.getText('t').insert(0, 'x'.repeat(100 * 1024));
        d.send(frame(doc));
        p.send({ type: 'transport',
                 data: { type: 'transport', op: 'start', seq: 0 } });

        for (let i = 0; i < 4; i++)
            p.send({ type: 'log', run: `${p.welcome.peer}#0`,
                     data: { type: 'knob', pad: 'x'.repeat(1024) } });

        await until(() => q.rooms.get('played').run?.length === 4);
        await until(() => q.rooms.get('idle').empty);

        process.stderr.write = (line) =>
            (/shedding/.test(line) ? shed.push(line)
                                   : write.call(process.stderr, line));
        process.memoryUsage = () => ({ ...usage(), external: 2 ** 40 });
        await until(() => shed.length >= 3);
        process.memoryUsage = usage;
        process.stderr.write = write;

        const scraped = await (await fetch(
            `http://127.0.0.1:${q.metrics.address().port}/`)).json();

        const told = await b.next('error');

        check(/the empty rooms \(1\)/.test(shed[0]) &&
              /room played's \d+ B kept for late joiners/.test(shed[1]) &&
              /room big/.test(shed[2]) && !q.rooms.has('idle') &&
              !q.rooms.has('big') &&
              scraped.shed.rooms >= 2 && scraped.shed.runs === 1,
              'past its share of memory the relay sheds the empty rooms, ' +
              'then a run\'s log, then the largest room ' +
              `(${shed.map((l) => l.replace(/^.*shedding /, '').trim())
                  .join('; ')})`);
        check(told.why === 'memory' && told.retryMs > 0 &&
              /short of memory/.test(told.text),
              'and the people in a room cut for memory are told to join ' +
              `again shortly (${JSON.stringify(told)})`);
        p.close();
    }
    finally
    {
        process.memoryUsage = usage;
        process.stderr.write = write;
        q.shutdown();
    }
}

/* The backstop where little is left to shed. */
async function memoryShedEdges ()
{
    const q = await relay({ port: 0, host: '127.0.0.1', tree, metricsPort: 0,
                            memoryMaxBytes: 2 ** 40 });
    const usage = process.memoryUsage;
    const write = process.stderr.write;
    const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

    try
    {
        const p = new Client(`ws://127.0.0.1:${q.address().port}/room/played`,
                             'P');

        await p.open();
        p.send({ type: 'hello', name: 'P', protocol: PROTOCOL, tickets: true });

        const w = await p.next('welcome');

        p.send({ type: 'transport',
                 data: { type: 'transport', op: 'start', seq: 0 } });
        p.send({ type: 'log', run: `${w.peer}#0`,
                 data: { type: 'knob', v: 1 } });

        for (let i = 0; i < 40 && q.rooms.get('played').run?.length !== 1; i++)
            await sleep(25);

        p.close();

        for (let i = 0; i < 40 && !q.rooms.get('played').empty; i++)
            await sleep(25);

        process.stderr.write = (line) =>
            (/shedding/.test(line) ? true : write.call(process.stderr, line));
        process.memoryUsage = () => ({ ...usage(), external: 2 ** 40 });
        await sleep(600);
        check(!q.rooms.has('played'),
              'past its share of memory the relay lets go of an empty room ' +
              'that is still playing, and goes on');

        let [last, worst] = [Date.now(), 0];
        const tick = setInterval(() =>
        {
            worst = Math.max(worst, Date.now() - last);
            last = Date.now();
        }, 10);

        await sleep(2000);
        clearInterval(tick);

        const { shed } = await (await fetch(
            `http://127.0.0.1:${q.metrics.address().port}/`)).json();

        check(worst < 500 && shed.streak === 0,
              'and with nothing left to shed it looks again as often, ' +
              `counting no more to shed (${worst} ms, ${shed.streak})`);
    }
    finally
    {
        process.memoryUsage = usage;
        process.stderr.write = write;
        q.shutdown();
    }
}

/* One page that stops reading, its queue most of what the relay holds
   and under the queues' caps: its room is the one shed, and the memory
   goes with it, so that no other room is. */
async function memoryShedQueued ()
{
    const memoryMaxBytes = 100 * 1024 * 1024;
    const q = await relay({ port: 0, host: '127.0.0.1', tree, memoryMaxBytes,
                            queuedMaxBytes: 2 ** 27,
                            queuedTotalMaxBytes: 2 ** 30,
                            limits: { fanout: [2 ** 30, 2 ** 30],
                                      roomIn: [2 ** 30, 2 ** 30],
                                      room: { relayed: [1e6, 1e6] } } });
    const join = async (room, name) =>
    {
        const c = new Client(`ws://127.0.0.1:${q.address().port}/room/${room}`,
                             name);

        await c.open();
        c.send({ type: 'hello', name, protocol: PROTOCOL, tickets: true });
        c.welcome = await c.next('welcome');
        return c;
    };
    const usage = process.memoryUsage;
    const write = process.stderr.write;
    const others = [];

    try
    {
        /* Nothing held until the queue is: this process's own heap would
           otherwise count against the relay's memory while it fills. */
        process.memoryUsage = () => ({ ...usage(), heapUsed: 0, external: 0 });

        for (let i = 0; i < 12; i++)
            others.push(await join(`other${i}`, `O${i}`));

        const [a, s] = [await join('slow', 'A'), await join('slow', 'S')];
        const pad = 'x'.repeat(256 * 1024);
        const queued = () =>
            q.rooms.get('slow').peers.get(a.welcome.peer).ws.bufferedAmount;

        a.ws._socket.pause();

        while (queued() < 40 * 1024 * 1024)
        {
            s.send({ type: 'relayed', to: a.welcome.peer, data: { pad } });
            await new Promise((r) => setImmediate(r));
        }

        const { heapUsed, external } = (gc(), usage());
        const offset = 0.8 * memoryMaxBytes - heapUsed - external;

        process.stderr.write = (line) =>
            (/shedding/.test(line) ? true : write.call(process.stderr, line));
        process.memoryUsage = () =>
        {
            const u = usage();

            return { ...u, external: u.external + offset };
        };
        await new Promise((r) => setTimeout(r, 3000));
        process.memoryUsage = usage;
        process.stderr.write = write;

        const kept = others.filter((c, i) => q.rooms.has(`other${i}`));

        check(!q.rooms.has('slow') && kept.length === others.length,
              'a page that stops reading, its queue most of the relay\'s ' +
              'memory, costs its own room and no other ' +
              `(${others.length - kept.length} others shed)`);
        s.close();
    }
    finally
    {
        process.memoryUsage = usage;
        process.stderr.write = write;
        others.forEach((c) => c.close());
        q.shutdown();
    }
}

/* Room sockets that do not say hello: one address holds no more than
   UNWELCOMED_MAX of them open (relay.mjs), and each is cut once its time
   to say hello is up. */
async function unwelcomedBounded ()
{
    const q = await relay({ port: 0, host: '127.0.0.1', tree });
    const at = `ws://127.0.0.1:${q.address().port}/room/quiet`;
    const open = (ws) => new Promise((r) =>
    {
        ws.on('open', () => r(true));
        ws.on('error', () => r(false));
    });

    try
    {
        const held = [];

        for (let i = 0; i < 128; i++)
            held.push(new WebSocket(at));

        const opened = await Promise.all(held.map(open));
        const over = new WebSocket(at);
        const refusedOver = !(await open(over));
        const t0 = Date.now();
        const cut = await Promise.all(held.map((ws) => refused(ws, 8000)));
        const cutMs = Date.now() - t0;
        const after = new WebSocket(at);
        const openAfter = await open(after);

        check(opened.every(Boolean) && refusedOver,
              'one address holds no more than 128 room sockets open that ' +
              'have not said hello');
        check(cut.every(Boolean) && cutMs > 3000 && openAfter,
              'and each is cut once its time to say hello is up, which ' +
              `frees its place (${cutMs} ms)`);
        after.terminate();
    }
    finally
    {
        q.shutdown();
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

/* Its bytes in unbounded: the log's tests send a long run's commands at
   once. */
const logs = await relay({ port: 0, host: '127.0.0.1', tree,
                           limits: { roomIn: [2 ** 30, 2 ** 30] } });
const logsBase = `ws://127.0.0.1:${logs.address().port}`;

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

    const unlisted = await (await fetch(`http://127.0.0.1:${port}/`)).json();

    a.send({ type: 'set', visibility: 'public' });
    await a.next('room');

    const health = await (await fetch(`http://127.0.0.1:${port}/`)).json();
    const listed = health.rooms.find((r) => r.name === 'test');

    check(!unlisted.rooms.some((r) => r.name === 'test'),
          'a new room is unlisted');
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

    /* A link gone quiet sends no close either way: a room socket whose
       pings go unanswered is given up on, and a join nobody answers
       rejects as out of reach, both while TCP would still be waiting.
       Side by side, since each takes its wait in real time. */
    {
        const quiet = new WebSocketServer({ port: 0, host: '127.0.0.1' });
        const mute = net.createServer(() => {});

        await Promise.all([new Promise((r) => quiet.on('listening', r)),
                           new Promise((r) => mute.listen(0, '127.0.0.1', r))]);

        quiet.on('connection', (ws) => ws.once('message', () =>
            ws.send(JSON.stringify({ type: 'welcome', peer: 'p', peers: [],
                                     playing: null }))));

        const t0 = performance.now();
        const [closedS, unreachable] = await Promise.all([
            (async () =>
            {
                const room = new Room(
                    `ws://127.0.0.1:${quiet.address().port}`, 'test', 'Fay');
                const closed = new Promise((r) => room.on('close', r));

                await room.connect();
                await closed;
                return (performance.now() - t0) / 1000;
            })(),
            new Room(`ws://127.0.0.1:${mute.address().port}`, 'test', 'Gus')
                .connect().then(() => false, (e) => e.unreachable === true),
        ]);

        check(closedS > 4 && closedS < 8,
              `a room socket whose pings go unanswered closes (${
                  closedS.toFixed(1)} s)`);
        check(unreachable && performance.now() - t0 < 12000,
              'a join nobody answers rejects as out of reach');
        quiet.close();
        mute.close();
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
        const f = new Client(`${logsBase}/room/logcap`, 'F');
        const g = new Client(`${logsBase}/room/logcap`, 'G');

        await Promise.all([f.open(), g.open()]);
        f.send({ type: 'hello', name: 'Fay', protocol: PROTOCOL,
                 tickets: true });

        const wf = await f.next('welcome');

        g.send({ type: 'hello', name: 'Gil', protocol: PROTOCOL,
                 tickets: true });
        await g.next('welcome');

        const hash = await hashOf(logs.rooms.get('logcap').doc);
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

        const many = await caughtUp(4, Array.from({ length: 17 },
                                                  (_, i) => edit(5 + i, 510)));

        check(many.overflowed === true && many.log.length === 0 &&
              many.files?.piece === 'airports.gen',
              'and so do more commands than the run keeps bytes for, and ' +
              'none of them is sent, but the document is');

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

        /* A refused line counts against the rate as well: a full bucket
           first. */
        await new Promise((r) => setTimeout(r, 1000));

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
        /* The last one's close can reach this side before the relay has
           counted it gone: past DOCS_PER_PEER the ticket is refused
           until it has. */
        let bad = null;

        for (let tries = 0; bad === null && tries < 50; tries++)
        {
            const w = new WebSocket(`${base}/doc/test?ticket=${ticket}`);

            if (await new Promise((r) =>
                {
                    w.on('open', () => r(true));
                    w.on('error', () => r(false));
                }))
                bad = w;
            else
                await new Promise((r) => setTimeout(r, 50));
        }

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
    await rolesEnforced();
    await rolesForAccounts();
    await metricsServed();
    await queuesBounded();
    await roomsBounded();
    await ratesLimited();
    await fanoutBounded();
    await inBytesBounded();
    await syncAsksBounded();
    await memoryShed();
    await memoryShedEdges();
    await memoryShedQueued();
    await unwelcomedBounded();
}
catch (e)
{
    fail(`threw: ${e.message}`);
}

server.shutdown();
logs.shutdown();

process.stdout.write(`\n${failures === 0 ? 'the relay does what it says'
                                          : `${failures} failed`}\n`);
process.exitCode = failures;
