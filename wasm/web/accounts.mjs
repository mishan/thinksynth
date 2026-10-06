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
 * accounts.mjs -- the relay's accounts: one SQLite file of handles, key
 * hashes and sessions; the routes under /api/account/; and the admin
 * commands, run against the same file. The routes:
 *
 *   POST register  { handle }            { key, session, account }
 *   POST login     { key }               { session, account }
 *   GET  me                   session    { account }
 *   POST handle    { handle, key } session   { account }
 *   POST key       { key }    session    { key, session,
 *                                         passkeysRemoved }
 *   POST logout               session    {}
 *   POST delete    { key, handle } session?  {}
 *   .../passkey/...                       (passkeys.mjs)
 *
 * A session goes as `Authorization: Bearer s_...', never as a cookie, so
 * no other site's page can send one for its visitor; a failure is `{
 * error, message }'. Whatever could lose the owner the account takes the
 * key itself and not a session alone: a logged-in browser that is
 * borrowed or stolen can play as the account, and no more.
 *
 * The key is eight random words of the relay's choosing, about 103 bits,
 * so it is stored as a plain SHA-256: a slow hash only helps a secret
 * somebody could guess. Sessions are stored hashed the same way, so a
 * copy of the file logs nobody in.
 */

import crypto from 'node:crypto';
import net from 'node:net';
import { DatabaseSync } from 'node:sqlite';

import { ACCOUNT_API, KEY_WORDS_PER_KEY, foldName, normalizeKey,
         normalizeName } from './account.js';
import { KEY_WORDS } from './wordlist.mjs';

const DAY_MS = 24 * 60 * 60 * 1000;

/* How long a session lasts after it was last used. */
export const SESSION_TTL_MS = 365 * DAY_MS;

/* How often an owner may change the handle, and how long the one changed
   from stays theirs: nobody can take it up straight away and pass as them
   to the people who knew them by it. */
export const RENAME_EVERY_MS = 30 * DAY_MS;
export const HANDLE_KEPT_MS = 30 * DAY_MS;

const BODY_MAX_BYTES = 1024;

/* A WebAuthn response: an attestation, with a public key and an
   authenticator's own credential id, which may run to a KiB alone. */
const RESPONSE_MAX_BYTES = 16 * 1024;

/* A session's last use is written at most this often: it only has to be
   good to the day for a year's lapse, and a write per hello and per
   minute's check of every open room is a write the disk waits on. */
const TOUCH_EVERY_MS = 60 * 60 * 1000;

/* What the page names guests who give no name (jam.js): no handle may
   start with it, so no account can pass as one, nor take one's name. */
const GUEST_PREFIX = 'guest-';

/* Stale sessions and lapsed handles go at most this often, on the next
   registration or log in. */
const PRUNE_EVERY_MS = 60 * 60 * 1000;

/* A limit shared by all clients turning requests away is logged at most
   this often. */
const SHARED_LOG_EVERY_MS = 10 * 60 * 1000;

/* Clients a limit tracks at most; past it the least recently seen is
   forgotten, so memory stays bounded however many addresses one client
   cycles through. */
const TRACKED_MAX = 10000;

/* Token buckets -- a burst, then one request back every `refillMs' -- per
   client, per IPv6 /48 and across everyone. Key requests have no shared
   bucket: guessing a key is hopeless at any rate, and a shared one would
   let a few hundred addresses lock everybody out of logging in. */
export const LIMITS = {
    register: [{ burst: 5, refillMs: 60 * 60 * 1000 },
               { burst: 20, refillMs: 15 * 60 * 1000 },
               { burst: 100, refillMs: 10 * 1000 }],
    key: [{ burst: 10, refillMs: 30 * 1000 },
          { burst: 40, refillMs: 7500 },
          null],
    session: [{ burst: 60, refillMs: 1000 }, null, null],
};

/* Schema upgrades in order: entry i takes a file from user_version i to
   i + 1. Append only; never edit one that has shipped. handle_folded and
   kept_handles hold foldName's output (account.js), which SQL cannot
   compute: a change to foldName comes with a step here that folds every
   stored handle again in JS. */
const MIGRATIONS = [
    `CREATE TABLE accounts (
       -- AUTOINCREMENT: a deleted account's id is never handed out again,
       -- so nothing kept against it passes to a newer account.
       id            INTEGER PRIMARY KEY AUTOINCREMENT,
       handle        TEXT NOT NULL,
       handle_folded TEXT NOT NULL UNIQUE,
       key_hash      TEXT NOT NULL UNIQUE,
       created_at    INTEGER NOT NULL,
       renamed_at    INTEGER,
       banned        INTEGER NOT NULL DEFAULT 0
     ) STRICT;
     CREATE TABLE sessions (
       token_hash   TEXT PRIMARY KEY,
       account_id   INTEGER NOT NULL,
       last_used_at INTEGER NOT NULL
     ) STRICT;
     CREATE INDEX sessions_account ON sessions (account_id);
     CREATE INDEX sessions_last_used ON sessions (last_used_at);
     -- Handles their owners renamed from, theirs until \`until'.
     CREATE TABLE kept_handles (
       handle_folded TEXT PRIMARY KEY,
       account_id    INTEGER NOT NULL,
       until         INTEGER NOT NULL
     ) STRICT;`,
    `-- Passkeys (passkeys.mjs). \`id' is the credential id, base64url;
     -- \`transports' a JSON array. An account's \`user_handle' is the
     -- WebAuthn user id its passkeys are made under, kept past the last of
     -- them, so that a passkey made later replaces the ones an
     -- authenticator still holds rather than sitting beside them.
     ALTER TABLE accounts ADD COLUMN user_handle TEXT;
     CREATE TABLE credentials (
       id           TEXT PRIMARY KEY,
       account_id   INTEGER NOT NULL,
       public_key   BLOB NOT NULL,
       counter      INTEGER NOT NULL,
       transports   TEXT NOT NULL,
       created_at   INTEGER NOT NULL,
       last_used_at INTEGER,
       label        TEXT NOT NULL
     ) STRICT;
     CREATE INDEX credentials_account ON credentials (account_id);`,
];

const COLUMNS = 'id, handle, handle_folded, created_at, renamed_at, banned';
const CREDENTIALS = 'SELECT c.*, a.user_handle FROM credentials c ' +
                    'JOIN accounts a ON a.id = c.account_id';

/* What is stored of a key or a session: its SHA-256, in hex. */
export function secretHash (secret)
{
    return crypto.createHash('sha256').update(secret, 'utf8').digest('hex');
}

export function newKey ()
{
    return Array.from({ length: KEY_WORDS_PER_KEY },
                      () => KEY_WORDS[crypto.randomInt(KEY_WORDS.length)])
        .join('-');
}

/* 128 bits, marked as a session so it cannot be mistaken for a key or a
   document ticket wherever one turns up. */
export function newSession ()
{
    return `s_${crypto.randomBytes(16).toString('hex')}`;
}

const isSession = (s) => typeof s === 'string' && /^s_[0-9a-f]{32}$/.test(s);

/* ---- the file ---- */

export class AccountStore
{
    /* `file' is a path, or ':memory:'. `busyMs' is how long a lock is
       waited out once the file is open. */
    constructor (file, { busyMs = 5000 } = {})
    {
        this.db = new DatabaseSync(file);

        /* WAL lets a backup, or the admin commands, read beside the
           relay's writes; a lock is waited out rather than failed on, and
           the timeout comes first so that switching to WAL waits too. */
        this.db.exec('PRAGMA busy_timeout = 5000; PRAGMA journal_mode = WAL;');

        /* Under WAL, NORMAL loses at most the last commits to a power cut
           and never corrupts the file; FULL waits on the disk each time. */
        this.db.exec('PRAGMA synchronous = NORMAL;');
        this.migrate();

        const q = (sql) => this.db.prepare(sql);

        this.q = {
            insert: q('INSERT INTO accounts (handle, handle_folded, ' +
                      'key_hash, created_at) VALUES (?, ?, ?, ?)'),
            byId: q(`SELECT ${COLUMNS} FROM accounts WHERE id = ?`),
            byKey: q(`SELECT ${COLUMNS} FROM accounts WHERE key_hash = ?`),
            byFolded: q(`SELECT ${COLUMNS} FROM accounts ` +
                        'WHERE handle_folded = ?'),
            keptBy: q('SELECT account_id FROM kept_handles ' +
                      'WHERE handle_folded = ? AND until > ?'),
            keep: q('INSERT OR REPLACE INTO kept_handles ' +
                    '(handle_folded, account_id, until) VALUES (?, ?, ?)'),
            free: q('DELETE FROM kept_handles WHERE account_id = ?'),
            keepAll: q('UPDATE kept_handles SET until = max(until, ?) ' +
                       'WHERE account_id = ?'),
            unkeep: q('DELETE FROM kept_handles WHERE handle_folded = ?'),
            lapsed: q('DELETE FROM kept_handles WHERE until <= ?'),
            addSession: q('INSERT INTO sessions (token_hash, account_id, ' +
                          'last_used_at) VALUES (?, ?, ?)'),
            session: q('SELECT account_id, last_used_at FROM sessions ' +
                       'WHERE token_hash = ?'),
            touch: q('UPDATE sessions SET last_used_at = ? ' +
                     'WHERE token_hash = ?'),
            endSession: q('DELETE FROM sessions WHERE token_hash = ?'),
            endStale: q('DELETE FROM sessions WHERE last_used_at < ?'),
            endOthers: q('DELETE FROM sessions ' +
                         'WHERE account_id = ? AND token_hash != ?'),
            endAll: q('DELETE FROM sessions WHERE account_id = ?'),
            setKey: q('UPDATE accounts SET key_hash = ? WHERE id = ?'),
            setHandle: q('UPDATE accounts SET handle = ?, ' +
                         'handle_folded = ?, ' +
                         'renamed_at = coalesce(?, renamed_at) WHERE id = ?'),
            setBanned: q('UPDATE accounts SET banned = ? WHERE id = ?'),
            remove: q('DELETE FROM accounts WHERE id = ?'),
            userHandle: q('SELECT user_handle FROM accounts WHERE id = ?'),
            claimUserHandle: q('UPDATE accounts SET user_handle = ' +
                               'coalesce(user_handle, ?) WHERE id = ?'),
            addCredential: q('INSERT OR IGNORE INTO credentials (id, ' +
                             'account_id, public_key, counter, ' +
                             'transports, created_at, label) ' +
                             'VALUES (?, ?, ?, ?, ?, ?, ?)'),
            credential: q(`${CREDENTIALS} WHERE c.id = ?`),
            credentials: q(`${CREDENTIALS} WHERE c.account_id = ? ` +
                           'ORDER BY c.created_at, c.id'),
            useCredential: q('UPDATE credentials SET counter = ?, ' +
                             'last_used_at = ? WHERE id = ? AND ' +
                             '(counter < ? OR (counter = 0 AND ? = 0))'),
            removeCredential: q('DELETE FROM credentials ' +
                                'WHERE id = ? AND account_id = ?'),
            removeCredentials: q('DELETE FROM credentials ' +
                                 'WHERE account_id = ?'),
        };

        this.db.exec(`PRAGMA busy_timeout = ${Number(busyMs)};`);
    }

    /* One step a transaction, the version read again under the write
       lock: the admin commands may be migrating the same file at once. */
    migrate ()
    {
        const version = () =>
            this.db.prepare('PRAGMA user_version').get().user_version;

        while (version() < MIGRATIONS.length)
            this.transaction(() =>
            {
                const v = version();

                if (v < MIGRATIONS.length)
                {
                    this.db.exec(MIGRATIONS[v]);
                    this.db.exec(`PRAGMA user_version = ${v + 1}`);
                }
            });
    }

    /* IMMEDIATE takes the write lock up front, so what is read inside is
       what is written against. */
    transaction (body)
    {
        this.db.exec('BEGIN IMMEDIATE');

        try
        {
            const out = body();

            this.db.exec('COMMIT');
            return out;
        }
        catch (e)
        {
            this.db.exec('ROLLBACK');
            throw e;
        }
    }

    byId (id)
    {
        return accountOf(this.q.byId.get(id));
    }

    byKey (keyHash)
    {
        return accountOf(this.q.byKey.get(keyHash));
    }

    byHandle (folded)
    {
        return accountOf(this.q.byFolded.get(folded));
    }

    /* The account a folded name is taken by -- its handle, or one it was
       renamed from and still keeps -- or null. */
    holder (folded, now)
    {
        return this.q.byFolded.get(folded)?.id ??
               this.q.keptBy.get(folded, now)?.account_id ?? null;
    }

    /* A new account and its first session, and its first passkey if
       `credential', or null if the handle is somebody's; under the write
       lock, so nobody takes it in between. */
    create ({ handle, keyHash, sessionHash, now, credential = null })
    {
        const folded = foldName(handle);

        return this.transaction(() =>
        {
            if (this.holder(folded, now) !== null)
                return null;

            const id = Number(this.q.insert.run(handle, folded, keyHash,
                                                now).lastInsertRowid);

            this.q.addSession.run(sessionHash, id, now);

            if (credential !== null)
            {
                this.q.claimUserHandle.run(credential.userHandle, id);

                if (!this.addCredential(id, credential))
                    throw passkeyTaken();
            }

            return this.byId(id);
        });
    }

    /* The account's WebAuthn user handle; `fresh' becomes it if the
       account has none yet. */
    userHandle (id, fresh)
    {
        this.q.claimUserHandle.run(fresh, id);
        return this.q.userHandle.get(id).user_handle;
    }

    /* False if the credential id is taken already: an authenticator picks
       it, so it may be anyone's choosing. */
    addCredential (accountId, c)
    {
        return this.q.addCredential.run(
            c.id, accountId, c.publicKey, c.counter,
            JSON.stringify(c.transports), c.createdAt, c.label).changes === 1;
    }

    credential (id)
    {
        return credentialOf(this.q.credential.get(id));
    }

    credentials (accountId)
    {
        return this.q.credentials.all(accountId).map(credentialOf);
    }

    /* A login's new count, or false if it has not gone up since the last
       one -- a cloned authenticator, or a replay. One that never counts
       stays at 0. */
    useCredential (id, counter, now)
    {
        return this.q.useCredential.run(counter, now, id, counter,
                                        counter).changes === 1;
    }

    removeCredential (id, accountId)
    {
        return this.q.removeCredential.run(id, accountId).changes === 1;
    }

    addSession (sessionHash, id, now)
    {
        this.q.addSession.run(sessionHash, id, now);
    }

    /* The account a session is of, the session marked used: null for one
       unknown, or unused for longer than SESSION_TTL_MS (which goes). */
    useSession (sessionHash, now)
    {
        const s = this.q.session.get(sessionHash);

        if (s === undefined)
            return null;

        if (s.last_used_at < now - SESSION_TTL_MS)
        {
            this.q.endSession.run(sessionHash);
            return null;
        }

        if (now - s.last_used_at >= TOUCH_EVERY_MS)
            this.q.touch.run(now, sessionHash);

        return this.byId(s.account_id);
    }

    endSession (sessionHash)
    {
        this.q.endSession.run(sessionHash);
    }

    /* Every session of an account's, or every one but `keep'. */
    endSessions (id, keep = null)
    {
        if (keep === null)
            this.q.endAll.run(id);
        else
            this.q.endOthers.run(id, keep);
    }

    prune (now)
    {
        this.q.endStale.run(now - SESSION_TTL_MS);
        this.q.lapsed.run(now);
    }

    /* A new key, every session ended, and `sessionHash' the one left;
       how many passkeys went with the old key. */
    replaceKey (id, keyHash, sessionHash, now)
    {
        return this.transaction(() =>
        {
            this.q.setKey.run(keyHash, id);
            this.endSessions(id);
            this.q.addSession.run(sessionHash, id, now);
            return Number(this.q.removeCredentials.run(id).changes);
        });
    }

    /* A new handle, or null if it is somebody else's. An owner's own
       rename (`now' given) is counted, and the handle it leaves stays
       theirs for HANDLE_KEPT_MS; a moderator's is neither. */
    rename (id, handle, at, now = at)
    {
        const folded = foldName(handle);

        return this.transaction(() =>
        {
            const was = this.byId(id);
            const holder = this.holder(folded, now);

            if (was === null || (holder !== null && holder !== id))
                return null;

            this.q.setHandle.run(handle, folded, at, id);
            this.q.unkeep.run(folded);

            if (at !== null && was.folded !== folded)
                this.q.keep.run(was.folded, id, now + HANDLE_KEPT_MS);

            return this.byId(id);
        });
    }

    setBanned (id, banned)
    {
        this.transaction(() =>
        {
            this.q.setBanned.run(banned ? 1 : 0, id);

            if (banned)
                this.endSessions(id);
        });
    }

    /* An account gone, and with it its sessions. Its handles -- the one
       it had and those it was renamed from -- stay nobody else's for
       HANDLE_KEPT_MS from now, so that a deleted name cannot be taken up
       to pass as its owner -- unless `free', a moderator's word. */
    remove (id, now, { free = false } = {})
    {
        this.transaction(() =>
        {
            const was = this.byId(id);

            this.endSessions(id);
            this.q.removeCredentials.run(id);
            this.q.remove.run(id);

            if (free)
                this.q.free.run(id);
            else if (was !== null)
            {
                this.q.keepAll.run(now + HANDLE_KEPT_MS, id);
                this.q.keep.run(was.folded, id, now + HANDLE_KEPT_MS);
            }
        });
    }

    close ()
    {
        this.db.close();
    }
}

function accountOf (row)
{
    return row === undefined ? null : {
        id: row.id, handle: row.handle, folded: row.handle_folded,
        createdAt: row.created_at, renamedAt: row.renamed_at,
        banned: row.banned !== 0,
    };
}

function credentialOf (row)
{
    return row === undefined ? null : {
        id: row.id, accountId: row.account_id, userHandle: row.user_handle,
        publicKey: row.public_key, counter: row.counter,
        transports: JSON.parse(row.transports), createdAt: row.created_at,
        lastUsedAt: row.last_used_at, label: row.label,
    };
}

/* An account as its owner is shown it. */
export function infoOf (account)
{
    return {
        handle: account.handle,
        createdAt: account.createdAt,
        renameAt: account.renamedAt === null
            ? account.createdAt : account.renamedAt + RENAME_EVERY_MS,
    };
}

/* ---- refusals and limits ---- */

export class ApiError extends Error
{
    constructor (status, code, message, headers = {})
    {
        super(message);
        this.status = status;
        this.code = code;
        this.headers = headers;
    }
}

const unauthorized = () =>
    new ApiError(401, 'unauthorized', 'log in again',
                 { 'WWW-Authenticate': 'Bearer' });
const badKey = (message = 'no account has that key') =>
    new ApiError(401, 'bad_key', message);
export const banned = () =>
    new ApiError(403, 'banned', 'this account is banned');
export const taken = (handle) =>
    new ApiError(409, 'handle_taken', `${JSON.stringify(handle)} is taken`);
export const passkeyTaken = () =>
    new ApiError(409, 'passkey_taken', 'that passkey is registered already');

/* A token bucket: `burst' at once, then one back every `refillMs', by
   the clock `now'. */
export class Bucket
{
    constructor ({ burst, refillMs }, now)
    {
        this.burst = burst;
        this.refillMs = refillMs;
        this.now = now;
        this.tokens = burst;
        this.at = now();
    }

    refill ()
    {
        const now = this.now();

        /* A wall clock stepped back must not drain the bucket. */
        this.tokens = Math.min(this.burst, this.tokens +
                               Math.max(0, now - this.at) / this.refillMs);
        this.at = now;
    }

    take (n = 1)
    {
        this.refill();

        if (this.tokens < n)
            return false;

        this.tokens -= n;
        return true;
    }

    waitMs ()
    {
        this.refill();

        return this.tokens >= 1
            ? 0 : Math.ceil((1 - this.tokens) * this.refillMs);
    }
}

/* One token bucket per key, least recently seen first. */
class RateLimiter
{
    constructor (limit, now)
    {
        this.limit = limit;
        this.now = now;
        this.buckets = new Map();
    }

    take (key, n = 1)
    {
        let b = this.buckets.get(key);

        if (b === undefined)
        {
            b = new Bucket(this.limit, this.now);

            if (this.buckets.size >= TRACKED_MAX)
                this.buckets.delete(this.buckets.keys().next().value);
        }
        else
            this.buckets.delete(key);

        this.buckets.set(key, b);
        return b.take(n);
    }

    waitMs (key)
    {
        return this.buckets.get(key)?.waitMs() ?? 0;
    }
}

/* A request's limits, narrowest first, so that a client already over its
   own spends nothing of the shared ones. */
export class TieredLimit
{
    constructor (tiers, now, onShared)
    {
        [this.own, this.site, this.everyone] =
            tiers.map((t) => (t === null ? null : new RateLimiter(t, now)));
        this.onShared = onShared;
    }

    take (client, n = 1)
    {
        const site = siteKey(client);

        for (const [limiter, key] of [[this.own, client],
                                      [site === null ? null : this.site, site],
                                      [this.everyone, '*']])
        {
            if (limiter === null || limiter.take(key, n))
                continue;

            if (key === '*')
                this.onShared();

            throw new ApiError(429, 'rate_limited',
                               'too many requests; slow down',
                               { 'Retry-After': String(Math.max(1, Math.ceil(
                                   limiter.waitMs(key) / 1000))) });
        }
    }

    /* How long until `client' may take one, with nothing taken: 0 if it
       may now. */
    waitMs (client)
    {
        const site = siteKey(client);

        return Math.max(this.own?.waitMs(client) ?? 0,
                        site === null ? 0 : this.site?.waitMs(site) ?? 0,
                        this.everyone?.waitMs('*') ?? 0);
    }
}

/* The key a client is limited by: an IPv4 address (an IPv4-mapped one
   too), or an IPv6 /64, since one host usually holds a whole /64 and can
   hop between its addresses at will. Anything else shares one bucket. */
export function clientKey (address)
{
    let a = String(address).trim().toLowerCase();

    /* An IPv6 zone goes. Cut by index: the address can come from a
       header a client writes, and a pattern for it is quadratic in '%'s. */
    if (a.includes('%'))
        a = a.slice(0, a.indexOf('%'));
    const version = net.isIP(a);

    if (version === 4)
        return a;

    if (version !== 6)
        return 'unknown';

    /* An IPv4-mapped address is its IPv4 one, written either way. */
    const mapped = /^\[::ffff:([0-9a-f]{1,4}):([0-9a-f]{1,4})\]$/.exec(
        new URL(`http://[${a}]/`).hostname);

    if (mapped !== null)
        return mapped.slice(1).map((g) => parseInt(g, 16))
            .flatMap((g) => [g >> 8, g & 255]).join('.');

    /* An embedded IPv4 tail stands for two groups; it is past the /64. */
    const groups = (part) => (part === '' ? [] : part.split(':').flatMap(
        (g) => (g.includes('.') ? ['0', '0'] : [g])));
    const gap = a.indexOf('::');
    let all = groups(a);

    if (gap >= 0)
    {
        const head = groups(a.slice(0, gap));
        const tail = groups(a.slice(gap + 2));

        all = [...head, ...Array(Math.max(0, 8 - head.length - tail.length))
            .fill('0'), ...tail];
    }

    return `${all.slice(0, 4).map((g) => (parseInt(g, 16) || 0).toString(16))
        .join(':')}::/64`;
}

/* The IPv6 /48 a client's /64 is in, which a site usually holds whole;
   null for an IPv4 client. */
export function siteKey (client)
{
    return client.endsWith('::/64')
        ? `${client.split(':').slice(0, 3).join(':')}::/48` : null;
}

/* The address `hops' entries from the right of X-Forwarded-For -- the one
   the outermost trusted proxy saw, since each appends whom it was reached
   from -- or null with no proxies trusted, or nothing usable there. A
   header shorter than that was not all written by trusted proxies, and
   its leftmost entry is whatever the client said. */
export function forwardedAddress (header, hops)
{
    if (hops < 1 || header === undefined)
        return null;

    const entries = [header].flat().join(',').split(',');

    if (entries.length < hops)
        return null;

    let a = entries[entries.length - hops].trim();
    const bracketed = /^\[([^\]]*)\](?::\d{1,5})?$/.exec(a);

    if (bracketed !== null)
        a = bracketed[1];
    else if (/^[\d.]+:\d{1,5}$/.test(a))
        a = a.slice(0, a.lastIndexOf(':'));

    return net.isIP(a) === 0 ? null : a;
}

/* ---- the service ---- */

/*
 * `onSessionsEnded({ session } | { account, except }, why)' is told
 * whenever sessions end here -- one, or all of an account's but `except'
 * -- so that the sockets made with them can go too.
 */
export class Accounts
{
    constructor ({ store, now = Date.now, onSessionsEnded = () => {},
                   limits = LIMITS,
                   log = (line) => process.stderr.write(`${line}\n`) })
    {
        this.store = store;
        this.now = now;
        this.onSessionsEnded = onSessionsEnded;
        this.log = log;
        this.lastPrune = -Infinity;
        this.shared = new Map();        /* what -> { count, at } */
        this.limits = Object.fromEntries(Object.entries(limits).map(
            ([what, tiers]) => [what, new TieredLimit(
                tiers, now, () => this.sharedRefusal(what))]));
    }

    register (client, body)
    {
        this.limits.register.take(client);

        const handle = handleOf(body.handle);
        const key = newKey();
        const session = newSession();
        const now = this.now();

        this.prune(now);

        const account = this.store.create({ handle, keyHash: secretHash(key),
                                            sessionHash: secretHash(session),
                                            now });

        if (account === null)
            throw taken(handle);

        return { key, session, account: infoOf(account) };
    }

    login (client, body)
    {
        this.limits.key.take(client);

        const account = this.byKey(body.key);

        if (account.banned)
            throw banned();

        const session = newSession();
        const now = this.now();

        this.prune(now);
        this.store.addSession(secretHash(session), account.id, now);
        return { session, account: infoOf(account) };
    }

    me (client, authorization)
    {
        const { account } = this.session(client, authorization);

        return { account: infoOf(account) };
    }

    /* With the key, not a session alone: a borrowed browser renaming the
       account would leave its handle to whoever takes it next. */
    rename (client, authorization, body)
    {
        const { account } = this.session(client, authorization);

        this.limits.key.take(client);
        this.ownKey(account, body.key);

        const handle = handleOf(body.handle);
        const now = this.now();
        const { renameAt } = infoOf(account);

        if (handle === account.handle)
            return { account: infoOf(account) };

        if (now < renameAt)
            throw new ApiError(409, 'rename_too_soon',
                               'the handle can next be changed at ' +
                               new Date(renameAt).toISOString());

        const renamed = this.store.rename(account.id, handle, now);

        if (renamed === null)
            throw taken(handle);

        return { account: infoOf(renamed) };
    }

    /* A new key for the current one, ending every other session: a
       session alone that could make one would be a stolen browser's way
       to take the account and then delete it. Every passkey goes too:
       whoever had the old key could have added any of them. */
    /* The caller's session goes too, for a new one: a copy of it taken
       before would otherwise outlive the key that made it. */
    replaceKey (client, authorization, body)
    {
        const { account } = this.session(client, authorization);

        this.limits.key.take(client);

        this.ownKey(account, body.key);

        const key = newKey();
        const session = newSession();
        const sessionHash = secretHash(session);

        const passkeysRemoved = this.store.replaceKey(
            account.id, secretHash(key), sessionHash, this.now());

        this.onSessionsEnded({ account: account.id, except: sessionHash },
                             'the account\'s key was replaced');
        return { key, session, passkeysRemoved };
    }

    logout (client, authorization)
    {
        this.limits.session.take(client);

        const sessionHash = secretHash(bearer(authorization));

        this.store.endSession(sessionHash);
        this.onSessionsEnded({ session: sessionHash }, 'logged out');
        return {};
    }

    /* The key, not a session alone, so a borrowed browser cannot; and
       with a session, the key must be its account's, so a password
       manager offering the wrong entry cannot delete another account.
       The handle, typed, is the owner saying they mean it. A banned
       account cannot: deleting would free its handle for it to take
       again. */
    remove (client, authorization, body)
    {
        const own = authorization === undefined
            ? null : this.session(client, authorization).account;

        this.limits.key.take(client);

        const account = this.byKey(body.key);

        if (own !== null && own.id !== account.id)
            throw badKey('that key is another account\'s');

        if (account.banned)
            throw banned();

        const typed = normalizeName(body.handle);

        if (typed === null || foldName(typed) !== account.folded)
            throw new ApiError(400, 'confirm',
                               'type the account\'s handle to delete it');

        this.store.remove(account.id, this.now());
        this.onSessionsEnded({ account: account.id, except: null },
                             'the account was deleted');
        return {};
    }

    /* For the relay: the account a room socket's session is of, as `{ id,
       handle, sessionHash }', or null for a session that has ended or an
       account that is banned. A hash, for a socket checked again later. */
    sessionAccount ({ session, sessionHash = isSession(session)
                                                ? secretHash(session) : null })
    {
        const account = sessionHash === null
            ? null : this.store.useSession(sessionHash, this.now());

        return account === null || account.banned
            ? null : { id: account.id, handle: account.handle, sessionHash };
    }

    /* Whether a guest may go by `name': not if it is an account's handle,
       or one an account still keeps. */
    nameFree (name)
    {
        return this.store.holder(foldName(name), this.now()) === null;
    }

    ownKey (account, raw)
    {
        if (this.byKey(raw).id !== account.id)
            throw badKey('that key is another account\'s');
    }

    byKey (raw)
    {
        const key = normalizeKey(raw);
        const account = key === null ? null
                                     : this.store.byKey(secretHash(key));

        if (account === null)
            throw badKey();

        return account;
    }

    session (client, authorization)
    {
        this.limits.session.take(client);

        const sessionHash = secretHash(bearer(authorization));
        const account = this.store.useSession(sessionHash, this.now());

        if (account === null)
            throw unauthorized();

        if (account.banned)
            throw banned();

        return { account, sessionHash };
    }

    prune (now)
    {
        if (now - this.lastPrune < PRUNE_EVERY_MS)
            return;

        this.lastPrune = now;

        /* Before the request's own write, and never its failure: one
           that failed after an account was made would lose its key. */
        try
        {
            this.store.prune(now);
        }
        catch (e)
        {
            this.log(`accounts: pruning: ${e.message}`);
        }
    }

    sharedRefusal (what)
    {
        const now = this.now();
        const seen = this.shared.get(what) ?? { count: 0, at: -Infinity };

        seen.count++;
        this.shared.set(what, seen);

        if (now - seen.at < SHARED_LOG_EVERY_MS)
            return;

        this.log(`accounts: the ${what} limit shared by everyone refused ` +
                 `${seen.count} request${seen.count === 1 ? '' : 's'}`);
        seen.count = 0;
        seen.at = now;
    }
}

export function handleOf (raw)
{
    const handle = normalizeName(raw);

    if (handle === null)
        throw new ApiError(400, 'bad_handle',
                           'a handle needs a visible character');

    if (foldName(handle).startsWith(foldName(GUEST_PREFIX)))
        throw new ApiError(400, 'bad_handle',
                           `a handle may not start with ${GUEST_PREFIX}`);

    return handle;
}

function bearer (authorization)
{
    const m = /^Bearer +(\S+)\s*$/i.exec(authorization ?? '');

    if (m === null || !isSession(m[1]))
        throw unauthorized();

    return m[1];
}

/* What a request is limited as (clientKey), behind `trustProxy' proxies
   that append to X-Forwarded-For. Behind a proxy that sends none, every
   client is the proxy's address and shares its buckets: a few
   registrations, or rooms, would hold off everyone's. Said once,
   loudly, with `log'. */
export function clientOf (trustProxy, log)
{
    let unforwarded = false;

    return (req) =>
    {
        const header = req.headers['x-forwarded-for'];

        if (trustProxy > 0 && header === undefined && !unforwarded)
        {
            unforwarded = true;
            log('relay: TRUST_PROXY is set but the proxy sends no ' +
                'X-Forwarded-For; every client shares one rate limit ' +
                'until it does (docs/RELAY.md)');
        }

        return clientKey(forwardedAddress(header, trustProxy) ??
                         req.socket.remoteAddress ?? 'unknown');
    };
}

/* ---- the routes ---- */

/* A request listener for everything under ACCOUNT_API. `corsOrigin' is
   the page's origin, or `*' for any (a harness); a request from any other
   origin is refused, and one with none (curl) is let through. The relay
   serves none of this without one (relay.mjs);
   `trustProxy' how many proxies in front append to X-Forwarded-For. Only
   count proxies that set it, or a client picks its own rate limit.
   `passkeys' is a Passkeys (passkeys.mjs), or null for none. `client' is
   clientOf's, for one shared with the relay's rooms. */
export function accountRoutes (accounts, { corsOrigin = null,
                                           trustProxy = 0,
                                           log = accounts.log,
                                           passkeys = null,
                                           client: keyOf =
                                               clientOf(trustProxy, log) }
                                   = {})
{
    const routes = {
        '/register': ['POST', (c, a, body) => accounts.register(c, body)],
        '/login': ['POST', (c, a, body) => accounts.login(c, body)],
        '/me': ['GET', (c, a) => accounts.me(c, a)],
        '/handle': ['POST', (c, a, body) => accounts.rename(c, a, body)],
        '/key': ['POST', (c, a, body) => accounts.replaceKey(c, a, body)],
        '/logout': ['POST', (c, a) => accounts.logout(c, a)],
        '/delete': ['POST', (c, a, body) => accounts.remove(c, a, body)],
        ...passkeys?.routes(),
    };

    return async (req, res) =>
    {
        const send = (status, body, headers = {}) =>
        {
            const text = JSON.stringify(body);

            res.writeHead(status, {
                'Content-Type': 'application/json; charset=utf-8',
                'Content-Length': Buffer.byteLength(text),
                'Cache-Control': 'no-store',
                ...headers,
            });
            res.end(text);
        };

        if (corsOrigin)
        {
            res.setHeader('Access-Control-Allow-Origin', corsOrigin);
            res.setHeader('Vary', 'Origin');
        }

        try
        {
            const route = routes[new URL(req.url, 'http://relay').pathname
                .slice(ACCOUNT_API.length)];

            if (route === undefined)
                throw new ApiError(404, 'not_found', 'not found');

            /* A preflight: a JSON POST from the page's origin needs one,
               and so does anything carrying a session. */
            if (req.method === 'OPTIONS')
            {
                res.writeHead(204, {
                    'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
                    'Access-Control-Allow-Headers':
                        'Content-Type, Authorization',
                    'Access-Control-Max-Age': '86400',
                });
                res.end();
                return;
            }

            /* A route's third entry: whether it takes a WebAuthn
               response. */
            const [method, run, large = false] = route;

            if (req.method !== method)
                throw new ApiError(405, 'method_not_allowed', `use ${method}`,
                                   { Allow: `${method}, OPTIONS` });

            /* A page elsewhere may send a form or text/plain to any site
               without asking first: refused here, or a page anywhere could
               register handles from its visitors' addresses. JSON from
               another origin is preflighted, and so is refused by the
               browser unless the origin is the page's. */
            const origin = req.headers.origin;

            if (origin !== undefined && corsOrigin !== '*' &&
                origin !== corsOrigin)
                throw new ApiError(403, 'bad_origin',
                                   `${origin} may not use this relay's ` +
                                   'accounts');

            if (method === 'POST' &&
                !/^application\/json\s*(;|$)/i.test(
                    req.headers['content-type'] ?? ''))
                throw new ApiError(415, 'bad_request',
                                   'the body must be application/json',
                                   { Connection: 'close' });

            const client = keyOf(req);
            const body = method === 'POST'
                ? await readJson(req, large ? RESPONSE_MAX_BYTES
                                            : BODY_MAX_BYTES)
                : {};

            send(200, await run(client, req.headers.authorization, body));
        }
        catch (e)
        {
            if (e instanceof ApiError)
                send(e.status, { error: e.code, message: e.message },
                     e.headers);
            else if (!res.headersSent)
            {
                process.stderr.write(`accounts: ${e.stack}\n`);
                send(500, { error: 'internal', message: 'internal error' });
            }
        }
    };
}

/* A JSON object of at most `maxBytes', or none at all; past the cap the
   rest is left unread and the connection goes with it. */
function readJson (req, maxBytes)
{
    const tooLarge = () =>
        new ApiError(413, 'too_large',
                     `the body is over ${maxBytes} bytes`,
                     { Connection: 'close' });

    return new Promise((resolve, reject) =>
    {
        if (Number(req.headers['content-length']) > maxBytes)
        {
            reject(tooLarge());
            return;
        }

        const chunks = [];
        let size = 0;

        req.on('data', (chunk) =>
        {
            size += chunk.length;

            if (size > maxBytes)
            {
                reject(tooLarge());
                req.pause();
            }
            else
                chunks.push(chunk);
        });
        req.on('error', reject);
        req.on('end', () =>
        {
            const text = Buffer.concat(chunks).toString('utf8');
            let body;

            try
            {
                body = text.trim() === '' ? {} : JSON.parse(text);
            }
            catch
            {
                reject(new ApiError(400, 'bad_request',
                                    'the body is not JSON'));
                return;
            }

            if (typeof body !== 'object' || body === null ||
                Array.isArray(body))
                reject(new ApiError(400, 'bad_request',
                                    'the body is not a JSON object'));
            else
                resolve(body);
        });
    });
}

/* ---- the admin commands ---- */

export const ADMIN_USAGE = `usage: relay.mjs admin <command>
  account <handle>         show an account
  rename <handle> <new>    change its handle (the owner's own renames are
                           unaffected, and the old handle is free at once)
  ban <handle>             end its sessions and refuse it until unbanned
  unban <handle>           let it log in again
  revoke <handle>          end its sessions; its key and passkeys still
                           log in
  delete <handle> [--free] delete it; its handles stay nobody's for 30
                           days, as when its owner deletes it, unless
                           --free frees them now`;

/* One command, its lines to `out'; returns the exit status. The relay
   notices sessions ended here within a minute (relay.mjs). */
export function runAdmin (args, store, out, now = Date.now())
{
    const [command, raw, ...rest] = args;
    const usage = () =>
    {
        out(ADMIN_USAGE);
        return 2;
    };

    const free = command === 'delete' && rest[0] === '--free';

    if (raw === undefined ||
        rest.length !== (command === 'rename' || free ? 1 : 0))
        return usage();

    const name = normalizeName(raw);
    const account = name === null ? null : store.byHandle(foldName(name));
    const quoted = JSON.stringify(account?.handle ?? raw);

    if (!['account', 'rename', 'ban', 'unban', 'revoke', 'delete']
        .includes(command))
        return usage();

    if (account === null)
    {
        out(`no account ${quoted}`);
        return 1;
    }

    switch (command)
    {
        case 'account':
        {
            const day = (ms) => new Date(ms).toISOString().slice(0, 10);

            out(`#${account.id}  ${quoted}  ` +
                `created ${day(account.createdAt)}` +
                (account.renamedAt === null
                    ? '' : `  renamed ${day(account.renamedAt)}`) +
                (account.banned ? '  [banned]' : ''));
            return 0;
        }

        case 'rename':
        {
            let handle;

            try
            {
                handle = handleOf(rest[0]);
            }
            catch (e)
            {
                out(`${JSON.stringify(rest[0])}: ${e.message}`);
                return 1;
            }

            const renamed = store.rename(account.id, handle, null, now);

            if (renamed === null)
            {
                out(`${JSON.stringify(handle)} is taken`);
                return 1;
            }

            out(`${quoted} is now ${JSON.stringify(renamed.handle)}`);
            return 0;
        }

        case 'ban':
        case 'unban':
            store.setBanned(account.id, command === 'ban');
            out(`${quoted} ${command === 'ban' ? 'banned' : 'unbanned'}`);
            return 0;

        case 'revoke':
            store.endSessions(account.id);
            out(`${quoted}'s sessions ended`);
            return 0;

        default:
            store.remove(account.id, now, { free });
            out(`${quoted} deleted` + (free ? '; its handles are free' : ''));
            return 0;
    }
}
