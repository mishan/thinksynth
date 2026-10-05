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
 * accountstest.mjs -- the relay's accounts, over HTTP as the page uses
 * them, on a clock this script turns.
 *
 *   node wasm/web/accountstest.mjs
 *
 * Keys are eight words and read back however they were typed; handles are
 * cleaned up, and clash when they fold alike; registering, logging in,
 * renaming, a new key, logging out and deleting each do what they say, and
 * end the sessions they should; a stale, revoked or banned session is
 * refused; a renamed handle stays its owner's for a while; the limits
 * refuse past their buckets, per client, per /48 and across everyone; and
 * the routes answer a CORS preflight and refuse a body that is too big.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';

import { foldName, normalizeKey, normalizeName } from './account.js';
import { AccountStore, Accounts, HANDLE_KEPT_MS, RENAME_EVERY_MS,
         SESSION_TTL_MS, accountRoutes, clientKey, forwardedAddress,
         newKey, runAdmin } from './accounts.mjs';
import { KEY_WORDS } from './wordlist.mjs';

let failures = 0;

function check (cond, what)
{
    if (cond)
        process.stdout.write(`ok    ${what}\n`);
    else
    {
        failures++;
        process.stdout.write(`FAIL  ${what}\n`);
    }
}

const DAY_MS = 24 * 60 * 60 * 1000;
const PAGE = 'https://page.example.org';

/* Limits nothing below reaches, but for the test of them. */
const ROOMY = { burst: 1000, refillMs: 1000 };
const roomy = { register: [ROOMY, ROOMY, ROOMY], key: [ROOMY, ROOMY, null],
                session: [ROOMY, null, null] };

/* An account service on `store', behind its routes on a port of its own,
   with the clock at `clock.now' and every onSessionsEnded kept. */
async function serve (store, { limits = roomy, trustProxy = 0 } = {})
{
    const clock = { now: Date.now() };
    const ended = [];
    const logged = [];
    const accounts = new Accounts({ store, now: () => clock.now, limits,
                                    onSessionsEnded: (e) => ended.push(e),
                                    log: (line) => logged.push(line) });
    const server = http.createServer(
        accountRoutes(accounts, { corsOrigin: PAGE, trustProxy }));

    await new Promise((r) => server.listen(0, '127.0.0.1', r));

    const base = `http://127.0.0.1:${server.address().port}/api/account`;

    /* A route, as { status, body, headers }. */
    const call = async (route, { body, session, method = 'POST',
                                 headers = {} } = {}) =>
    {
        const res = await fetch(`${base}/${route}`, {
            method,
            headers: { ...(method === 'POST'
                           ? { 'Content-Type': 'application/json' } : {}),
                       ...headers,
                       ...(session ? { Authorization: `Bearer ${session}` }
                                   : {}) },
            body: body === undefined ? undefined
                : typeof body === 'string' ? body : JSON.stringify(body),
        });
        const text = await res.text();

        return { status: res.status, headers: res.headers,
                 body: text === '' ? null : JSON.parse(text) };
    };

    return { clock, ended, logged, call, base,
             close: () => server.close() };
}

/* ---- keys and names ---- */

{
    const key = newKey().split('-');

    check(key.length === 8 && key.every((w) => KEY_WORDS.includes(w)),
          'a key is eight words of the list');

    const k = 'abacus-zoom-acid-aloe-wool-yarn-zen-acre';

    for (const [typed, want] of [
        [k, k],
        ['Abacus Zoom ACID aloe wool yarn zen acre', k],
        ['  abacus--zoom acid-aloe\twool yarn zen acre  ', k],
        ['abacus zoom acid aloe wool yarn zen', null],
        ['abacus zoom acid aloe wool yarn zen acre acre', null],
        ['abacus zoom acid aloe wool yarn zen acr3', null],
        [7, null]])
        check(normalizeKey(typed) === want,
              `the key ${JSON.stringify(typed)} reads as ${want}`);

    for (const [raw, want] of [
        ['  Ann  ', 'Ann'],
        ['A\u202Enn', 'Ann'],
        ['An\u200Bn\u0000', 'Ann'],
        ['two   spaces\u3164here', 'two spaces here'],
        ['\u200B\u2800', null],
        ['x'.repeat(40), 'x'.repeat(32)],
        ['e\u0301\u0301\u0301\u0301', '\u00E9\u0301\u0301'],
        [null, null]])
        check(normalizeName(raw) === want,
              `the name ${JSON.stringify(raw)} is ${JSON.stringify(want)}`);

    for (const [a, b, clash] of [
        ['Ann', 'aNN', true],
        ['stra\u00DFe', 'STRASSE', true],
        ['\u0130stanbul', 'istanbul', true],
        ['\uFF41nn', 'ann', true],
        ['\uFB01sh', 'fish', true],
        ['victor', '\u03BDictor', true],
        ['paul', 'pa\u03C5l', true],
        ['zo\u00EB', 'zo\u0451', true],
        ['ann', '\u0251nn', true],
        ['guest-1', 'g\u03C5est-1', true],
        ['NICK', '\u039DICK', true],
        ['Ian', 'lan', true],
        ['m', 'rn', true],
        ['Ann', 'Anne', false],
        ['zoe', 'zo\u00EB', false]])
        check((foldName(a) === foldName(b)) === clash,
              `${JSON.stringify(a)} and ${JSON.stringify(b)} ` +
              `${clash ? 'clash' : 'do not clash'}`);
}

/* ---- the routes, start to end ---- */

{
    const s = await serve(new AccountStore(':memory:'));

    try
    {
        const ann = await s.call('register', { body: { handle: ' Ann ' } });

        check(ann.status === 200 && ann.body.account.handle === 'Ann' &&
              normalizeKey(ann.body.key) === ann.body.key &&
              /^s_[0-9a-f]{32}$/.test(ann.body.session),
              'registering hands over a key, a session and the handle');
        check(ann.headers.get('access-control-allow-origin') === PAGE,
              'and says which origin may read it');

        for (const [handle, status, error] of [
            ['ANN', 409, 'handle_taken'],
            ['\u0410nn', 409, 'handle_taken'],
            ['A\u200Dnn', 409, 'handle_taken'],
            ['\u200B', 400, 'bad_handle'],
            ['guest-123', 400, 'bad_handle'],
            ['Guest-Ann', 400, 'bad_handle'],
            [7, 400, 'bad_handle']])
        {
            const r = await s.call('register', { body: { handle } });

            check(r.status === status && r.body.error === error,
                  `registering ${JSON.stringify(handle)} is ${error}`);
        }

        const typed = ann.body.key.toUpperCase().replaceAll('-', ' ');
        const login = await s.call('login', { body: { key: typed } });

        check(login.status === 200 && login.body.account.handle === 'Ann' &&
              login.body.session !== ann.body.session,
              'the key logs in however it is typed, to a new session');

        const wrong = await s.call('login', { body: {
            key: 'abacus zoom acid aloe wool yarn zen acre' } });

        check(wrong.status === 401 && wrong.body.error === 'bad_key',
              'a key that is nobody\'s logs nobody in');

        const me = await s.call('me', { method: 'GET',
                                        session: ann.body.session });
        const nobody = await s.call('me', { method: 'GET' });

        check(me.status === 200 && me.body.account.handle === 'Ann',
              'a session says whose it is');
        check(nobody.status === 401 && nobody.body.error === 'unauthorized' &&
              nobody.headers.get('www-authenticate') === 'Bearer',
              'and no session is told to log in');

        /* A rename: with the key, once a month, and the handle left
           behind stays its owner's for a while, so nobody can pass as them
           under it. */
        const keyless = await s.call('handle', { session: ann.body.session,
                                                 body: { handle: 'Annie' } });
        const renamed = await s.call('handle', {
            session: ann.body.session,
            body: { handle: 'Annie', key: ann.body.key } });
        const again = await s.call('handle', {
            session: ann.body.session,
            body: { handle: 'Ann', key: ann.body.key } });
        const squat = await s.call('register', { body: { handle: 'ann' } });

        check(keyless.status === 401 && keyless.body.error === 'bad_key',
              'a session alone cannot rename the account');
        check(renamed.status === 200 &&
              renamed.body.account.handle === 'Annie' &&
              again.status === 409 && again.body.error === 'rename_too_soon',
              'a handle changes once, then not again for a while');
        check(squat.status === 409 && squat.body.error === 'handle_taken',
              'and nobody else can take the one it was');

        s.clock.now += Math.max(HANDLE_KEPT_MS, RENAME_EVERY_MS) + DAY_MS;

        const lapsed = await s.call('register', { body: { handle: 'ann' } });

        check(lapsed.status === 200,
              'until it has been free long enough');

        /* A new key takes the current one, and ends every session --
           the one asking for it too, which is handed a new one, so that a
           copy of it taken before does not outlive the key. */
        const other = (await s.call('login', {
            body: { key: ann.body.key } })).body.session;
        const notMine = await s.call('key', {
            session: ann.body.session, body: { key: lapsed.body.key } });
        const replaced = await s.call('key', { session: ann.body.session,
                                               body: { key: ann.body.key } });
        const oldKey = await s.call('login', { body: { key: ann.body.key } });
        const newKey_ = await s.call('login', {
            body: { key: replaced.body.key } });
        const otherMe = await s.call('me', { method: 'GET', session: other });
        const thisMe = await s.call('me', { method: 'GET',
                                            session: ann.body.session });
        const current = replaced.body.session;
        const nowMe = await s.call('me', { method: 'GET', session: current });

        check(notMine.status === 401 && notMine.body.error === 'bad_key',
              'a new key wants this account\'s key, not another\'s');
        check(replaced.status === 200 && oldKey.status === 401 &&
              newKey_.status === 200,
              'a new key logs in, and the old one does not');
        check(otherMe.status === 401 && thisMe.status === 401 &&
              nowMe.status === 200 &&
              s.ended.some((e) => e.account !== undefined &&
                                  e.except !== null),
              'and every session before it is ended, the asker\'s too, ' +
              'which has a new one; and the relay is told');

        /* Logging out ends the one session. */
        const out = await s.call('logout', { session: newKey_.body.session });
        const after = await s.call('me', { method: 'GET',
                                           session: newKey_.body.session });

        check(out.status === 200 && after.status === 401 &&
              s.ended.some((e) => e.session !== undefined),
              'logging out ends the session, and the relay is told');

        /* A year unused and a session lapses; used, it does not. */
        const stale = (await s.call('login', {
            body: { key: replaced.body.key } })).body.session;

        s.clock.now += SESSION_TTL_MS / 2;
        await s.call('me', { method: 'GET', session: current });
        s.clock.now += SESSION_TTL_MS / 2 + DAY_MS;

        const staleMe = await s.call('me', { method: 'GET', session: stale });
        const usedMe = await s.call('me', { method: 'GET',
                                            session: current });

        check(staleMe.status === 401 && usedMe.status === 200,
              'a session unused for a year lapses; one in use does not');

        /* Deleting takes the key -- with a session, that session's
           account's key -- and the handle typed out. */
        const wrongDelete = await s.call('delete', {
            session: current,
            body: { key: lapsed.body.key, handle: 'Annie' } });
        const unconfirmed = await s.call('delete', {
            session: current,
            body: { key: replaced.body.key, handle: 'Ann' } });
        const deleted = await s.call('delete', {
            session: current,
            body: { key: replaced.body.key, handle: 'annie' } });
        const gone = await s.call('login', {
            body: { key: replaced.body.key } });

        check(wrongDelete.status === 401 && unconfirmed.status === 400 &&
              unconfirmed.body.error === 'confirm' &&
              deleted.status === 200 && gone.status === 401,
              'deleting takes the account\'s own key and its handle typed, ' +
              'and then it is gone');

        const reuse = await s.call('register', { body: { handle: 'Annie' } });

        s.clock.now += HANDLE_KEPT_MS + DAY_MS;

        const reused = await s.call('register', { body: { handle: 'Annie' } });

        check(reuse.status === 409 && reused.status === 200,
              'and its handle is nobody\'s until it has been free long ' +
              'enough');

        /* A page elsewhere can send a form or text/plain without asking:
           a request that is not JSON, or from another origin, is not let
           register anything. */
        const plain = await s.call('register', {
            headers: { 'Content-Type': 'text/plain' },
            body: { handle: 'Plain' } });
        const foreign = await s.call('register', {
            headers: { Origin: 'https://elsewhere.example.org' },
            body: { handle: 'Foreign' } });
        const ours = await s.call('register', {
            headers: { Origin: PAGE }, body: { handle: 'Ours' } });

        check(plain.status === 415 && foreign.status === 403 &&
              ours.status === 200,
              'only JSON from the page\'s origin registers');

        /* Past the cap with no length to refuse it by up front. */
        const chunked = await new Promise((resolve) =>
        {
            const req = http.request(`${s.base}/register`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json',
                           'Transfer-Encoding': 'chunked' },
            }, (res) => resolve(res.statusCode));

            req.on('error', () => resolve('reset'));

            for (let i = 0; i < 8; i++)
                req.write(' '.repeat(512));

            req.end('{}');
        });

        check(chunked === 413, 'a chunked body over a KiB is refused');

        const big = await s.call('register', {
            body: JSON.stringify({ handle: 'x'.repeat(2000) }) });
        const notJson = await s.call('register', { body: 'handle=x' });
        const preflight = await s.call('me', {
            method: 'OPTIONS',
            headers: { Origin: PAGE,
                       'Access-Control-Request-Headers': 'authorization' } });

        check(big.status === 413 && notJson.status === 400,
              'a body over a KiB, or not JSON, is refused');
        check(preflight.status === 204 &&
              preflight.headers.get('access-control-allow-origin') === PAGE &&
              /authorization/i.test(
                  preflight.headers.get('access-control-allow-headers')),
              'a preflight lets the page send a session');
    }
    finally
    {
        s.close();
    }
}

/* ---- the admin commands, on a file ---- */

{
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'accountstest-'));
    const file = path.join(dir, 'relay.db');
    const s = await serve(new AccountStore(file));
    const admin = (...args) =>
    {
        const store = new AccountStore(file);
        const lines = [];
        const status = runAdmin(args, store, (l) => lines.push(l));

        store.close();
        return { status, lines };
    };

    try
    {
        const bo = (await s.call('register', { body: { handle: 'Bo' } })).body;
        const shown = admin('account', 'BO');
        const banned = admin('ban', 'bo');
        const login = await s.call('login', { body: { key: bo.key } });
        const me = await s.call('me', { method: 'GET', session: bo.session });

        check(shown.status === 0 && /"Bo"/.test(shown.lines[0]),
              'the admin commands find an account by its handle, folded');
        const selfDelete = await s.call('delete', {
            body: { key: bo.key, handle: 'Bo' } });

        check(banned.status === 0 && login.body.error === 'banned' &&
              me.status === 401,
              'a ban ends the sessions and refuses the key');
        check(selfDelete.body?.error === 'banned',
              'and a banned account cannot delete itself to free its ' +
              'handle');

        admin('unban', 'bo');

        const session = (await s.call('login', { body: { key: bo.key } }))
            .body?.session;

        check(session !== undefined, 'and an unban lets it log in again');

        admin('revoke', 'bo');
        check((await s.call('me', { method: 'GET', session })).status === 401,
              'revoking ends every session');

        check(admin('rename', 'bo', 'guest-9').status === 1,
              'a moderator cannot rename anyone into a guest\'s name');

        const renamed = admin('rename', 'bo', 'Bob');
        const asOwner = await s.call('login', { body: { key: bo.key } });

        check(renamed.status === 0 &&
              asOwner.body.account.handle === 'Bob' &&
              asOwner.body.account.renameAt <= s.clock.now &&
              (await s.call('register', { body: { handle: 'bo' } }))
                  .status === 200,
              'a moderator\'s rename leaves the owner\'s free, and keeps ' +
              'nothing');

        check(admin('delete', 'bob').status === 0 &&
              (await s.call('login', { body: { key: bo.key } })).status ===
              401 && admin('account', 'bob').status === 1 &&
              (await s.call('register', { body: { handle: 'Bob' } }))
                  .status === 409,
              'deleting from the admin commands deletes, and keeps the ' +
              'handle');

        const cy = (await s.call('register', { body: { handle: 'Cy' } }))
            .body;

        check(cy.key !== undefined &&
              admin('delete', 'cy', '--free').status === 0 &&
              (await s.call('register', { body: { handle: 'Cy' } }))
                  .status === 200,
              'unless told to free it');
        check(admin('frobnicate', 'x').status === 2,
              'an unknown command says how to use them');
    }
    finally
    {
        s.close();
        fs.rmSync(dir, { recursive: true, force: true });
    }
}

/* ---- a deleted account's old handles, and a failing sweep ---- */

{
    const s = await serve(new AccountStore(':memory:'));

    try
    {
        /* Renamed from twenty days before the account goes: still kept
           thirty days after it went. */
        const eve = (await s.call('register', { body: { handle: 'Eve' } }))
            .body;

        await s.call('handle', { session: eve.session,
                                 body: { handle: 'Eva', key: eve.key } });
        s.clock.now += 20 * DAY_MS;
        await s.call('delete', { body: { key: eve.key, handle: 'Eva' } });
        s.clock.now += 20 * DAY_MS;

        check((await s.call('register', { body: { handle: 'Eve' } }))
                  .status === 409,
              'a deleted account\'s old handle is kept 30 days from the ' +
              'delete');
    }
    finally
    {
        s.close();
    }

    const store = new AccountStore(':memory:');

    store.prune = () => { throw new Error('database is locked'); };

    const t = await serve(store);

    try
    {
        const r = await t.call('register', { body: { handle: 'Fen' } });

        check(r.status === 200 && normalizeKey(r.body.key) === r.body.key,
              'a sweep that fails does not lose a new account its key');
    }
    finally
    {
        t.close();
    }
}

/* ---- the limits ---- */

{
    const s = await serve(new AccountStore(':memory:'), {
        trustProxy: 1,
        limits: { register: [{ burst: 2, refillMs: 60000 },
                             { burst: 3, refillMs: 60000 },
                             { burst: 6, refillMs: 60000 }],
                  key: [{ burst: 2, refillMs: 60000 }, ROOMY, null],
                  session: [ROOMY, null, null] } });
    const from = (address) => ({ 'X-Forwarded-For': `9.9.9.9, ${address}` });
    let n = 0;
    const register = (address) => s.call('register', {
        headers: from(address), body: { handle: `h${n++}` } });
    const statuses = async (addresses) =>
    {
        const out = [];

        for (const a of addresses)
            out.push((await register(a)).status);

        return out.join(' ');
    };

    try
    {
        /* Two each, three a /48 and six in all. */
        check(await statuses(['10.0.0.1', '10.0.0.1', '10.0.0.1']) ===
              '200 200 429', 'a client is refused past its own bucket');
        check(await statuses(['2001:db8:1:1::1', '2001:db8:1:1::2',
                              '2001:db8:1:1::3', '2001:db8:1:2::1',
                              '2001:db8:1:3::1']) ===
              '200 200 429 200 429',
              'a /64 is one client, and a /48 shares a bucket');
        check(await register('192.0.2.7').then((r) => r.status) === 200,
              'another client still gets in');

        const last = await register('192.0.2.8');

        check(last.status === 429 &&
              Number(last.headers.get('retry-after')) >= 1,
              'and everyone shares one, which says when to try again');

        const keys = [];

        for (let i = 0; i < 3; i++)
            keys.push((await s.call('login', { headers: from('10.0.0.9'),
                                               body: { key: 'x' } })).status);

        check(keys.join(' ') === '401 401 429',
              'key requests are limited per client too');

        /* Behind a proxy that does not say who the client is, said once:
           everyone is sharing the proxy's buckets. */
        check(s.logged.every((l) => !/X-Forwarded-For/.test(l)),
              'nothing is said while the proxy says who the client is');

        for (let i = 0; i < 2; i++)
            await s.call('me', { method: 'GET' });

        check(s.logged.filter((l) => /sends no X-Forwarded-For/.test(l))
                  .length === 1,
              'a trusted proxy that sends no X-Forwarded-For is reported, ' +
              'once');
    }
    finally
    {
        s.close();
    }

    for (const [address, want] of [
        ['192.0.2.1', '192.0.2.1'],
        ['::ffff:192.0.2.1', '192.0.2.1'],
        ['::ffff:c000:201', '192.0.2.1'],
        ['2001:db8:a:b:c:d:e:f', '2001:db8:a:b::/64'],
        ['2001:DB8::1', '2001:db8:0:0::/64'],
        ['fe80::1%eth0', 'fe80:0:0:0::/64'],
        ['not an address', 'unknown']])
        check(clientKey(address) === want,
              `${address} is limited as ${want}`);

    for (const [header, hops, want] of [
        ['1.1.1.1, 2.2.2.2', 1, '2.2.2.2'],
        ['1.1.1.1, 2.2.2.2', 2, '1.1.1.1'],
        ['1.1.1.1', 3, null],
        ['1.1.1.1', 0, null],
        ['[2001:db8::1]:443', 1, '2001:db8::1'],
        ['1.1.1.1:80', 1, '1.1.1.1'],
        ['forged', 1, null]])
        check(forwardedAddress(header, hops) === want,
              `X-Forwarded-For ${JSON.stringify(header)} behind ${hops} ` +
              `is ${want}`);
}

process.stdout.write(`\n${failures === 0 ? 'accounts do what they say'
                                          : `${failures} failed`}\n`);
process.exitCode = failures;
