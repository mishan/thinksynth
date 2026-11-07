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
 * passkeys.mjs -- WebAuthn passkeys for the relay's accounts. A passkey
 * logs in to the same session a key does, so nothing past the login knows
 * which it was. The routes, under /api/account/passkey/:
 *
 *   POST register-options { handle }             options
 *   POST register-verify  { response }           { key, session, account }
 *   POST add-options      { key }      session   options
 *   POST add-verify       { response } session   { passkey }
 *   POST login-options                           options
 *   POST login-verify     { response }           { session, account }
 *   GET  list                          session   { passkeys }
 *   POST remove           { id }       session   {}
 *
 * Registering makes the account, its first passkey and its key at once;
 * the key is then the way back in for whoever loses the passkey. Adding
 * a passkey takes the key, as everything that could hand the account to
 * someone else does: a passkey added from a borrowed browser would outlast
 * every session the owner ends. Removing one takes a session alone, since
 * the key still logs in.
 *
 * The relying party is the site the page is served from, not the relay:
 * WebAuthn binds a passkey to an RP ID, a domain the page's own origin
 * must be on. Every response must also come from that one origin.
 */

import crypto from 'node:crypto';

import { generateAuthenticationOptions, generateRegistrationOptions,
         verifyAuthenticationResponse,
         verifyRegistrationResponse } from '@simplewebauthn/server';

import { foldName, isOrigin, onRpId } from './account.js';
import { ApiError, banned, handleOf, infoOf, newKey, newSession,
         passkeyTaken, secretHash, taken } from './accounts.mjs';

/* How long a ceremony may take, from its options to its response. */
export const CHALLENGE_TTL_MS = 5 * 60 * 1000;


/* EdDSA, ES256 and RS256: the library would offer ML-DSA too where Node
   has it, whose keys alone are past a request's body cap. */
const ALGORITHMS = [-8, -7, -257];

const TRANSPORTS = ['ble', 'cable', 'hybrid', 'internal', 'nfc',
                    'smart-card', 'usb'];

const badPasskey = () =>
    new ApiError(401, 'bad_passkey', 'the passkey could not be verified');
const unknownPasskey = () =>
    new ApiError(401, 'unknown_passkey', 'no account has that passkey');
/* What an add's challenge is good for besides its purpose: the account
   and the session that asked. A new key ends every session, so it voids
   every add asked for under the old one too. */
const boundTo = (account, sessionHash) => `${account.id}\n${sessionHash}`;

const badChallenge = () =>
    new ApiError(400, 'bad_challenge', 'that sign-in has expired; try again');

/* The passkeys of a relay's environment, or null for none: PASSKEY_RP_ID
   names the site's domain, and CORS_ORIGIN, the page's origin -- just an
   origin, as a browser writes one into every response -- must be on it.
   Throws for a pair that could never work. */
export function passkeyConfig ({ PASSKEY_RP_ID: rpId,
                                 PASSKEY_RP_NAME: rpName,
                                 CORS_ORIGIN: origin })
{
    if (!rpId)
        return null;

    if (!isOrigin(origin) || !onRpId(new URL(origin).hostname, rpId))
        throw new Error(`PASSKEY_RP_ID ${rpId} needs CORS_ORIGIN to be the ` +
                        'page\'s origin, on that domain');

    return { rpId, rpName: rpName || 'thinksynth', origin };
}

/* A passkey as its owner is shown it. */
function shownOf (c)
{
    return { id: c.id, label: c.label, createdAt: c.createdAt,
             lastUsedAt: c.lastUsedAt };
}

export class Passkeys
{
    /* `origin' is the page's, which every response must name. */
    constructor ({ accounts, rpId, rpName, origin })
    {
        this.accounts = accounts;
        this.store = accounts.store;
        this.rpId = rpId;
        this.rpName = rpName;
        this.origin = origin;

        /* Challenges are signed rather than kept, so no number of them
           asked for can push out anyone else's: a challenge carries what
           it was issued for and until when, under an HMAC by a key that
           lives as long as the process. Only the ones answered are
           kept, until they lapse, so each is good for one try; that
           takes a verify request, which the register and key limits
           bound. */
        this.secret = crypto.randomBytes(32);
        this.used = new Map();          /* challenge -> until */
    }

    routes ()
    {
        return {
            '/passkey/register-options':
                ['POST', (c, a, body) => this.registerOptions(c, body)],
            '/passkey/register-verify':
                ['POST', (c, a, body) => this.registerVerify(c, body), true],
            '/passkey/add-options':
                ['POST', (c, a, body) => this.addOptions(c, a, body)],
            '/passkey/add-verify':
                ['POST', (c, a, body) => this.addVerify(c, a, body), true],
            '/passkey/login-options':
                ['POST', (c) => this.loginOptions(c)],
            '/passkey/login-verify':
                ['POST', (c, a, body) => this.loginVerify(c, body), true],
            '/passkey/list': ['GET', (c, a) => this.list(c, a)],
            '/passkey/remove':
                ['POST', (c, a, body) => this.remove(c, a, body)],
        };
    }

    /* The handle is asked after now, so a taken one is said before the
       person makes a passkey for it; it is held only once registered.
       On the register limits, so asking after handles is no cheaper here
       than registering, and so is every account made: a verify needs a
       challenge from here, and each is good for one try. */
    async registerOptions (client, body)
    {
        this.accounts.limits.register.take(client);

        const handle = handleOf(body.handle);

        if (this.store.holder(foldName(handle), this.accounts.now()) !== null)
            throw taken(handle);

        const userHandle = crypto.randomBytes(16).toString('base64url');

        return this.creation(handle, userHandle, [],
                             this.issue('register', { handle, userHandle }));
    }

    async registerVerify (client, body)
    {
        this.accounts.limits.key.take(client);

        const { challenge, handle, userHandle } =
            this.take(body.response, 'register');
        const info = await this.registered(body.response, challenge);
        const key = newKey();
        const session = newSession();
        const now = this.accounts.now();

        this.accounts.prune(now);

        const account = this.store.create({
            handle, keyHash: secretHash(key), sessionHash: secretHash(session),
            now, credential: this.credentialOf(info, userHandle, now) });

        if (account === null)
            throw taken(handle);

        return { key, session, account: infoOf(account) };
    }

    async addOptions (client, authorization, body)
    {
        const { account, sessionHash } =
            this.accounts.session(client, authorization);

        this.accounts.limits.key.take(client);
        this.accounts.ownKey(account, body.key);

        const existing = this.store.credentials(account.id);

        /* One user handle an account, for good, so an authenticator
           holding one of its passkeys -- even one removed, or gone with
           an old key -- replaces it rather than keeping two. */
        const userHandle = this.store.userHandle(
            account.id, crypto.randomBytes(16).toString('base64url'));

        return this.creation(account.handle, userHandle, existing,
                             this.issue('add', { userHandle },
                                        boundTo(account, sessionHash)));
    }

    async addVerify (client, authorization, body)
    {
        const { account, sessionHash } =
            this.accounts.session(client, authorization);
        const entry = this.take(body.response, 'add',
                                boundTo(account, sessionHash));
        const info = await this.registered(body.response, entry.challenge);

        /* Asked again: the session may have ended while that was out. */
        this.accounts.session(client, authorization);

        const now = this.accounts.now();
        const credential = this.credentialOf(info, entry.userHandle, now);

        if (!this.store.addCredential(account.id, credential))
            throw passkeyTaken();

        return { passkey: shownOf(credential) };
    }

    /* Discoverable: no credentials named, so the browser offers whichever
       of this site's passkeys it holds, and nothing says which accounts
       exist. */
    async loginOptions (client)
    {
        this.accounts.limits.session.take(client);

        return generateAuthenticationOptions({
            rpID: this.rpId, timeout: CHALLENGE_TTL_MS,
            userVerification: 'preferred', challenge: this.issue('login') });
    }

    async loginVerify (client, body)
    {
        this.accounts.limits.key.take(client);

        const response = body.response;
        const { challenge } = this.take(response, 'login');
        const stored = typeof response.id === 'string'
            ? this.store.credential(response.id) : null;

        if (stored === null)
            throw unknownPasskey();

        /* As the spec asks: an authenticator's user handle, where it names
           one, is the one the passkey was made under. The signature is
           what says the passkey is this one. */
        const userHandle = response.response?.userHandle;

        if ((userHandle ?? null) !== null && userHandle !== stored.userHandle)
            throw badPasskey();

        const { authenticationInfo: info } = await this.verified(
            verifyAuthenticationResponse, {
                response, expectedChallenge: challenge,
                credential: { id: stored.id, publicKey: stored.publicKey,
                              counter: stored.counter,
                              transports: stored.transports } });

        const account = this.store.byId(stored.accountId);

        if (account === null)
            throw unknownPasskey();

        if (account.banned)
            throw banned();

        const now = this.accounts.now();

        /* The counter checked again as it is written: two logins verified
           against one count are one replay. */
        if (!this.store.useCredential(stored.id, info.newCounter, now))
            throw badPasskey();

        const session = newSession();

        this.accounts.prune(now);
        this.store.addSession(secretHash(session), account.id, now);
        return { session, account: infoOf(account) };
    }

    list (client, authorization)
    {
        const { account } = this.accounts.session(client, authorization);

        return { passkeys: this.store.credentials(account.id).map(shownOf) };
    }

    remove (client, authorization, body)
    {
        const { account } = this.accounts.session(client, authorization);

        if (typeof body.id !== 'string' ||
            !this.store.removeCredential(body.id, account.id))
            throw new ApiError(404, 'not_found', 'no such passkey');

        return {};
    }

    creation (handle, userHandle, existing, challenge)
    {
        return generateRegistrationOptions({
            rpName: this.rpName, rpID: this.rpId, challenge,
            userName: handle, userDisplayName: handle,
            userID: Buffer.from(userHandle, 'base64url'),
            timeout: CHALLENGE_TTL_MS, attestationType: 'none',
            excludeCredentials: existing.map(({ id, transports }) =>
                ({ id, transports })),

            /* Resident, or a login naming no credential could not find
               it. */
            authenticatorSelection: { residentKey: 'required',
                                      userVerification: 'preferred' },
            supportedAlgorithmIDs: ALGORITHMS,
        });
    }

    /* A challenge for `purpose', carrying `fields', good for
       CHALLENGE_TTL_MS: an HMAC, then the JSON it is of. `bound' is what
       the response must come with besides -- for an add, the account and
       session -- signed and not carried. */
    issue (purpose, fields = {}, bound = '')
    {
        const json = Buffer.from(JSON.stringify({
            purpose, until: this.accounts.now() + CHALLENGE_TTL_MS,
            nonce: crypto.randomBytes(16).toString('base64url'), ...fields }));

        return Buffer.concat([this.mac(json, bound), json]);
    }

    mac (json, bound)
    {
        return crypto.createHmac('sha256', this.secret)
            .update(json).update('\n').update(bound).digest();
    }

    /* What the challenge a response answers carries, if this relay issued
       it for `purpose' and `bound' and it has not lapsed nor been
       answered before. It is spent whatever comes of the response. */
    take (response, purpose, bound = '')
    {
        const now = this.accounts.now();
        let challenge;
        let fields;

        for (const [c, until] of this.used)
        {
            if (until > now)
                break;

            this.used.delete(c);
        }

        try
        {
            challenge = JSON.parse(Buffer.from(
                response.response.clientDataJSON, 'base64url')
                .toString('utf8')).challenge;

            const bytes = Buffer.from(challenge, 'base64url');
            const json = bytes.subarray(32);

            /* Read strictly: Node's base64url skips padding and stray
               characters, and a challenge spelled two ways would be two
               tries. */
            if (bytes.toString('base64url') === challenge &&
                crypto.timingSafeEqual(bytes.subarray(0, 32),
                                       this.mac(json, bound)))
                fields = JSON.parse(json.toString('utf8'));
        }
        catch
        {
            /* Refused below. */
        }

        if (fields?.purpose !== purpose || !(fields.until > now) ||
            this.used.has(challenge))
            throw badChallenge();

        this.used.set(challenge, fields.until);
        return { ...fields, challenge };
    }

    /* The library takes the id the browser names on trust; the one the
       authenticator signed is what is kept. */
    async registered (response, challenge)
    {
        const info = (await this.verified(verifyRegistrationResponse, {
            response, expectedChallenge: challenge,
            supportedAlgorithmIDs: ALGORITHMS })).registrationInfo;

        if (info.credential.id !== response.id)
            throw badPasskey();

        return info;
    }

    /* A response checked against the page's origin and the RP ID. User
       verification is asked for, not required, as the options say: a
       security key without a PIN logs in whoever holds it, as the key
       does whoever has it (RELAY.md). */
    async verified (verify, opts)
    {
        try
        {
            const out = await verify({ ...opts, expectedOrigin: this.origin,
                                       expectedRPID: this.rpId,
                                       requireUserVerification: false });

            if (out.verified)
                return out;
        }
        catch
        {
            /* Refused below, whatever was wrong. */
        }

        throw badPasskey();
    }

    credentialOf (info, userHandle, now)
    {
        const { id, publicKey, counter, transports } = info.credential;

        return {
            id, userHandle, publicKey, counter, createdAt: now,
            lastUsedAt: null,

            /* The browser's word, passed on unread. */
            transports: TRANSPORTS.filter((t) => Array.isArray(transports) &&
                                                 transports.includes(t)),
            label: info.credentialDeviceType === 'multiDevice'
                ? 'Synced passkey' : 'Passkey on one device',
        };
    }
}
