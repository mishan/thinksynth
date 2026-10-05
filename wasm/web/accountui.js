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
 * accountui.js -- the room page's account dialog: create an account, log
 * in with a passkey or a key, and once logged in add and remove passkeys,
 * change the handle, replace the key, log out or delete the account.
 * Opened from the join card.
 *
 * The key is the account and the relay picks it, so the dialog's real job
 * is getting the key into a password manager. Managers save what is
 * submitted in a login-shaped form and fill it back on the same site, so
 * the key is shown in one: the handle as the username and the key as a
 * password (`new-password' when issued, `current-password' wherever it is
 * asked for). Submitting the form, then the form going away, is what makes
 * a manager offer to save it. Show, Copy and Download cover people without
 * one.
 *
 * Where the relay and the browser have passkeys, a passkey is the way in
 * and the key is for when it is lost: an account is made with both at
 * once, and the key is saved the same way.
 *
 * The page keeps the session and the handle beside it, never the key.
 */

import { WebAuthnAbortService, browserSupportsWebAuthn,
         browserSupportsWebAuthnAutofill, startAuthentication,
         startRegistration } from '@simplewebauthn/browser';

import { ACCOUNT_API, apiOriginOf, normalizeName, onRpId } from './account.js';

/* A session is kept under the relay it is for: one relay's is never
   another's to see, nor to end. */
const STORE = 'thinksynth:account:';

/* A request slower than this is a relay that is not answering. */
const REQUEST_MS = 10000;

/* The autofill offer's challenge lapses after five minutes on the relay
   (passkeys.mjs, CHALLENGE_TTL_MS), so a fresh one is asked for before
   then; and again after one fails, shortly the first time and twice as
   long each time after, up to the same four minutes. */
const AUTOFILL_REARM_MS = 4 * 60 * 1000;
const AUTOFILL_RETRY_MS = 2000;

function load (origin)
{
    try
    {
        const kept = JSON.parse(localStorage.getItem(STORE + origin));

        return typeof kept?.session === 'string' ? kept : null;
    }
    catch
    {
        return null;
    }
}

function keep (origin, kept)
{
    try
    {
        if (kept === null)
            localStorage.removeItem(STORE + origin);
        else
            localStorage.setItem(STORE + origin, JSON.stringify(kept));
    }
    catch
    {
        /* No storage: logged in for as long as the page is open. */
    }
}

/* A failed request: the relay's { error, message }, or `network'. */
class AccountError extends Error
{
    constructor (code, message)
    {
        super(message);
        this.code = code;
    }
}

/* The account routes on the relay at `origin'. */
function client (origin)
{
    const call = async (route, { body, session } = {}) =>
    {
        const get = route === '/me' || route === '/passkey/list';
        let res;

        try
        {
            res = await fetch(`${origin}${ACCOUNT_API}${route}`, {
                method: get ? 'GET' : 'POST',
                headers: {
                    ...(get ? {} : { 'Content-Type': 'application/json' }),
                    ...(session ? { Authorization: `Bearer ${session}` } : {}),
                },
                body: get ? undefined : JSON.stringify(body ?? {}),
                signal: AbortSignal.timeout(REQUEST_MS),
            });
        }
        catch
        {
            throw new AccountError('network', 'the relay is not answering');
        }

        const out = await res.json().catch(() => null);

        if (typeof out !== 'object' || out === null)
            throw new AccountError('network', 'the relay has no accounts');

        if (!res.ok)
            throw new AccountError(String(out.error), String(out.message));

        return out;
    };

    return {
        register: (handle) => call('/register', { body: { handle } }),
        login: (key) => call('/login', { body: { key } }),
        me: (session) => call('/me', { session }),
        rename: (session, handle, key) =>
            call('/handle', { session, body: { handle, key } }),
        replaceKey: (session, key) => call('/key', { session, body: { key } }),
        logout: (session) => call('/logout', { session }),
        remove: (session, key, handle) =>
            call('/delete', { session, body: { key, handle } }),

        /* Each passkey ceremony: the relay's options, the browser's
           answer to them, and the answer back to the relay. */
        registerPasskey: async (handle) => call('/passkey/register-verify', {
            body: { response: await startRegistration({ optionsJSON:
                await call('/passkey/register-options',
                           { body: { handle } }) }) } }),
        addPasskey: async (session, key) => call('/passkey/add-verify', {
            session, body: { response: await startRegistration({ optionsJSON:
                await call('/passkey/add-options',
                           { session, body: { key } }) }) } }),
        /* `still' says whether the ceremony is still wanted once the
           options are in: starting one aborts any other that is out. */
        loginPasskey: async (useBrowserAutofill, still = () => true) =>
        {
            const optionsJSON = await call('/passkey/login-options');

            if (!still())
                throw new DOMException('no longer wanted', 'AbortError');

            const response = await startAuthentication({ useBrowserAutofill,
                                                         optionsJSON });

            try
            {
                return await call('/passkey/login-verify',
                                  { body: { response } });
            }
            catch (e)
            {
                e.picked = true;

                /* Where the browser can, it stops offering one that no
                   account has. */
                if (e.code === 'unknown_passkey')
                    PublicKeyCredential.signalUnknownCredential?.({
                        rpId: optionsJSON.rpId, credentialId: response.id })
                        .catch(() => {});

                throw e;
            }
        },
        passkeys: (session) => call('/passkey/list', { session }),
        removePasskey: (session, id) =>
            call('/passkey/remove', { session, body: { id } }),
    };
}

/* A passkey ceremony the person called off, or another took over: nothing
   to say about it. */
const calledOff = (e) => e.name === 'NotAllowedError' ||
                         e.name === 'AbortError';

/* A failed request, in the words of the person who made it. */
function failed (e)
{
    switch (e.code)
    {
        case 'network':
            return 'Cannot reach the relay right now.';
        case 'rate_limited':
            return 'Too many tries; wait a little and try again.';
        case 'bad_key':
            return /another/.test(e.message)
                ? 'That is another account\'s key.'
                : 'That key does not match any account.';
        case 'handle_taken':
            return 'That handle is taken.';
        case 'bad_handle':
            return 'A handle needs at least one visible character.';
        case 'confirm':
            return 'Type your handle to delete the account.';
        case 'banned':
            return 'This account is banned.';
        case 'unknown_passkey':
            return 'No account has that passkey; it may have been removed.';
        case 'bad_passkey':
            return 'That passkey was not accepted.';
        case 'bad_challenge':
            return 'That took too long; try again.';
        case 'passkey_taken':
            return 'That passkey is registered already.';
        default:
            return e.message;
    }
}

function el (tag, props = {}, ...children)
{
    const e = Object.assign(document.createElement(tag), props);

    e.append(...children);
    return e;
}

function button (text, onclick)
{
    return el('button', { type: 'button', textContent: text, onclick });
}

function section (heading, ...children)
{
    return el('section', { className: 'accountpart' },
              el('h2', { textContent: heading, dir: 'auto' }), ...children);
}

/* A login-shaped form: the handle as the username, the key as the
   password. `id' keeps several on one screen apart. */
function keyForm (id, handle, autocomplete, submitText)
{
    const user = el('input', { id: `account-${id}-user`, name: 'username',
                               autocomplete: 'username', value: handle });
    const key = el('input', { id: `account-${id}-key`, name: 'password',
                              type: 'password', autocomplete,
                              required: true, spellcheck: false,
                              autocapitalize: 'none',
                              placeholder: 'eight words' });
    const submit = el('button', { type: 'submit', textContent: submitText });
    const form = el('form', { method: 'post', autocomplete: 'on' },
                    el('label', {}, 'Handle ', user),
                    el('label', {}, 'Key ', key), submit);

    return { form, user, key, submit };
}

/* A key as a text file, for whoever has no password manager. */
function download (handle, key)
{
    const url = URL.createObjectURL(new Blob([
        `thinksynth account\n\nHandle: ${handle}\nKey: ${key}\n` +
        `Site: ${location.origin}\n\nThe key is the account: anyone with ` +
        'it can play as you, and a lost one cannot be replaced.\n'],
                                             { type: 'text/plain' }));

    el('a', { href: url, download: 'thinksynth-key.txt' }).click();

    /* Revoked at once, some browsers cancel the download. */
    setTimeout(() => URL.revokeObjectURL(url), 30000);
}

/*
 * `open' is the join card's button, `dialog' the <dialog> the screens go
 * in, and `relays()' resolves to `{ url, home }': the relay this page
 * joins and the one the site names, the very values the join uses.
 * `onChange(handle)' is called whenever who this page is changes: a
 * handle, or null for a guest.
 *
 * Accounts are the home relay's only. A page sent to another one (the
 * URL's `relay') joins it as a guest, with no button: a relay that is not
 * the site's has no business seeing its session, nor a key typed into a
 * dialog it could stand behind. The button stays hidden, too, on a relay
 * without accounts.
 */
export function createAccounts ({ open, dialog, relays, onChange })
{
    let origin = null;
    let kept = null;
    let api = null;
    let passkeys = false;
    const status = el('p', { className: 'hint', role: 'status' });
    const body = el('div', { className: 'accountbody' });

    /* What closing must not lose: a key on screen that has not been
       saved, which cannot be shown again. Closing a second time does. */
    let unsaved = false;

    const say = (text) => { status.textContent = text; };
    const close = () =>
    {
        if (!unsaved)
        {
            dialog.close();
            return;
        }

        unsaved = false;
        say('Your key cannot be shown again once this closes. Close again ' +
            'to close it anyway.');
    };

    dialog.append(
        el('div', { className: 'accounthead' },
           el('span', { className: 'accounttitle', textContent: 'Account' }),
           Object.assign(button('\u00d7', close),
                         { ariaLabel: 'Close', id: 'accountclose' })),
        status, body);
    dialog.addEventListener('cancel', (e) =>
    {
        e.preventDefault();
        close();
    });

    /* A passkey offered in autofill is the logged-out screen's only. */
    dialog.addEventListener('close',
                            () => WebAuthnAbortService.cancelCeremony());

    const show = (...nodes) =>
    {
        const prefilled = body.querySelector('#account-passkey-key');

        /* A key filled in for adding a passkey goes with the screen. */
        if (prefilled !== null)
            prefilled.value = '';

        WebAuthnAbortService.cancelCeremony();
        body.replaceChildren(...nodes);
    };
    const changed = (k) =>
    {
        kept = k;
        keep(origin, k);
        open.textContent = k === null ? 'Log in' : 'Account';
        onChange(k?.handle ?? null);
    };

    /* While a request is out, its button is not pressed again.
       `refused' is what to say when a passkey ceremony the button started
       ends in NotAllowedError: the browser's word for the person calling
       it off, and for a timeout or an authenticator that cannot make a
       resident passkey alike. */
    const busy = async (b, run, refused = null) =>
    {
        b.disabled = true;

        try
        {
            await run();
        }
        catch (e)
        {
            if (e.name === 'NotAllowedError' && refused !== null)
                say(refused);
            else if (!calledOff(e))
                say(failed(e));
        }
        finally
        {
            b.disabled = false;
        }
    };

    const loggedInBy = (res) =>
    {
        changed({ session: res.session, handle: res.account.handle });
        say(`Logged in as ${res.account.handle}.`);

        /* Gone on success: what a password manager watches for. */
        loggedIn(res);
    };

    function loggedOut ()
    {
        const handle = el('input', { id: 'account-handle', maxLength: 64,
                                     autocomplete: 'off' });
        const make = (b, withPasskey) => busy(b, async () =>
        {
            const h = normalizeName(handle.value);

            if (h === null)
            {
                say('A handle needs at least one visible character.');
                return;
            }

            const res = withPasskey ? await api.registerPasskey(h)
                                    : await api.register(h);

            changed({ session: res.session, handle: res.account.handle });
            say('');
            saveKey(res.account.handle, res.key,
                    withPasskey ? 'recovery' : 'new', () => loggedIn(res));
        }, withPasskey ? 'No passkey was made: it was canceled, or this ' +
                         'device cannot make one. Create with a key only ' +
                         'makes the account without one.'
                       : null);
        const login = keyForm('login', '', 'current-password', 'Log in');
        let rearm;
        let retries = 0;
        let ceremony = false;

        /* Asked again after each wait: an offer started under a ceremony
           of the screen's own would abort it, and one past the screen
           would be offered on another. */
        const offering = () => !ceremony && dialog.open &&
                               body.contains(login.form);

        /* A passkey offered beside the handle as the browser fills it in,
           where it can, for as long as this screen is up. A ceremony of
           this screen's own aborts it, and puts it back once over. */
        const arm = () =>
        {
            clearTimeout(rearm);

            if (!passkeys || !offering())
                return;

            rearm = setTimeout(arm, AUTOFILL_REARM_MS);
            browserSupportsWebAuthnAutofill().then(async (can) =>
            {
                if (!can || !offering())
                {
                    clearTimeout(rearm);
                    return;
                }

                login.user.autocomplete = 'username webauthn';
                loggedInBy(await api.loginPasskey(true, offering));
            }).catch((e) =>
            {
                /* Any failure once the person picked a passkey is said;
                   the offer's own failure to be made is not, over
                   whatever the status line was saying. */
                if (e.picked)
                    say(failed(e));

                /* Not for an abort: whatever aborted it re-arms. */
                if (e.name !== 'AbortError')
                {
                    clearTimeout(rearm);
                    rearm = setTimeout(arm, Math.min(
                        AUTOFILL_RETRY_MS * 2 ** retries++,
                        AUTOFILL_REARM_MS));
                }
            });
        };
        /* A ceremony of the screen's own: no offer is made while it is
           out, and one is made again once it is over. */
        const own = async (run) =>
        {
            ceremony = true;
            clearTimeout(rearm);

            try
            {
                await run();
            }
            finally
            {
                ceremony = false;
                arm();
            }
        };
        const create = button('Create account',
                              () => own(() => make(create, passkeys)));
        const keyOnly = button('Create with a key only',
                               () => make(keyOnly, false));
        const withPasskey = button('Log in with a passkey',
                                   () => own(() => busy(withPasskey,
                                       async () => loggedInBy(
                                           await api.loginPasskey(false)),
                                       'No passkey was used: it was ' +
                                       'canceled, it took too long, or ' +
                                       'none here is for this site.')));

        login.form.onsubmit = (e) =>
        {
            e.preventDefault();
            busy(login.submit,
                 async () => loggedInBy(await api.login(login.key.value)));
        };

        show(el('p', { textContent:
                'An account is a handle the room knows you by, which nobody ' +
                'else can take. There is no email or password: ' +
                (passkeys ? 'a passkey logs you in, and the relay gives you ' +
                            'a key of eight words for when the passkey is ' +
                            'lost. '
                          : 'the relay gives you a key of eight words, and ' +
                            'the key is the account. ') +
                'Keep the key in your password manager. Without an account ' +
                'you join as a guest.' }),
             section('Create an account',
                     el('label', {}, 'Handle ', handle),
                     ...(passkeys ? [el('div', { className: 'row' },
                                        create, keyOnly)]
                                  : [create])),
             section('Log in', ...(passkeys ? [withPasskey] : []),
                     login.form));
        arm();
    }

    /* A key just issued, in a form a password manager will save, with
       Show, Copy and Download beside it: a new account's (`new'), one's
       beside its first passkey (`recovery'), or a replacement
       (`replaced'). `then' goes on once saved. */
    function saveKey (handle, key, kind, then)
    {
        const form = keyForm('save', handle, 'new-password', 'Save key');
        const note = el('span', { className: 'hint' });
        const onward = () =>
        {
            unsaved = false;
            then();
        };

        unsaved = true;
        form.key.value = key;
        form.form.onsubmit = (e) =>
        {
            e.preventDefault();

            /* A browser's own suggested password must not be what the
               manager saves. */
            if (form.key.value !== key)
            {
                form.key.value = key;
                say('That was your browser\'s password, not your key. The ' +
                    'key is back; save it again.');
                return;
            }

            say('Saved. Your password manager should offer to keep it.');
            onward();
        };

        const showKey = button('Show', () =>
        {
            const shown = form.key.type === 'text';

            form.key.type = shown ? 'password' : 'text';
            showKey.textContent = shown ? 'Show' : 'Hide';
        });

        show(section({ new: 'Your account key', recovery: 'Your recovery key',
                       replaced: 'Your new key' }[kind],
            el('p', { className: 'accountwarn', textContent:
                kind === 'recovery'
                    ? 'Your passkey logs you in. This key is the way in ' +
                      'without it, here or on any other browser, and it ' +
                      'cannot be shown again or recovered. Save it in your ' +
                      'password manager now.'
                    : (kind === 'new' ? '' : 'The old key no longer works, ' +
                                             'and every other browser is ' +
                                             'logged out. ') +
                      'This key is the only way into your account, here or ' +
                      'on any other browser, and it cannot be recovered. ' +
                      'Save it in your password manager now.' }),
            form.form,
            el('div', { className: 'row' }, showKey,
               button('Copy', () => navigator.clipboard.writeText(key).then(
                   () => { note.textContent = 'Copied.'; },
                   () => { note.textContent = 'Could not copy: Show it, ' +
                                              'and copy it by hand.'; })),
               button('Download', () => download(handle, key)), note),
            button('I have saved it; continue', onward)));
        form.submit.focus();
    }

    /* `key' is one just issued, for the passkey form to add with. */
    function loggedIn ({ account }, key = '')
    {
        const session = kept.session;
        const waiting = account.renameAt > Date.now();

        /* A new handle takes the key too: a borrowed browser renaming the
           account would leave its handle to whoever takes it next. */
        const rename = keyForm('rename', account.handle, 'current-password',
                               'Change');
        const handle = el('input', { id: 'account-newhandle', maxLength: 64,
                                     autocomplete: 'off' });

        rename.form.prepend(el('label', {}, 'New ', handle));
        rename.form.onsubmit = (e) =>
        {
            e.preventDefault();
            busy(rename.submit, async () =>
            {
                const h = normalizeName(handle.value);

                if (h === null)
                {
                    say('A handle needs at least one visible character.');
                    return;
                }

                const res = await api.rename(session, h, rename.key.value);

                changed({ session, handle: res.account.handle });
                say(`You are now ${res.account.handle}. ${account.handle} ` +
                    'stays yours for 30 days, and the saved key still works.');
                loggedIn(res);
            });
        };
        const replace = keyForm('replace', account.handle, 'current-password',
                                'Replace key');
        const remove = keyForm('delete', account.handle, 'current-password',
                               'Delete for good');

        /* Typed out, not filled in: a password manager fills the key. */
        const confirm = el('input', { id: 'account-confirm', maxLength: 64,
                                      autocomplete: 'off' });

        remove.form.insertBefore(el('label', {}, 'Type it ', confirm),
                                 remove.submit);
        const logout = button('Log out', () => busy(logout, async () =>
        {
            /* Logged out here even if the relay cannot be told. */
            await api.logout(session).catch(() => {});
            changed(null);
            say('Logged out.');
            loggedOut();
        }));

        rename.form.inert = waiting;
        replace.form.onsubmit = (e) =>
        {
            e.preventDefault();
            busy(replace.submit, async () =>
            {
                const res = await api.replaceKey(session, replace.key.value);
                const lost = res.passkeysRemoved > 0;

                /* The session this page had ended with the old key. */
                changed({ session: res.session, handle: account.handle });

                say(lost ? 'Your passkeys went with the old key.' : '');
                saveKey(account.handle, res.key, 'replaced', () =>
                {
                    loggedIn({ account }, res.key);

                    if (lost && passkeys)
                        say('Your passkeys went with the old key. Add one ' +
                            'below: the new key is filled in.');
                });
            });
        };
        remove.form.onsubmit = (e) =>
        {
            e.preventDefault();
            busy(remove.submit, async () =>
            {
                await api.remove(session, remove.key.value, confirm.value);
                changed(null);
                say('Your account is deleted.');
                loggedOut();
            });
        };

        show(section(`Logged in as ${account.handle}`,
                     el('p', { textContent: 'Since ' + new Date(
                         account.createdAt).toLocaleDateString() })),
             ...(passkeys ? [passkeyPart(session, account, key)] : []),
             section('Change handle',
                     el('p', { className: 'hint', textContent: waiting
                         ? 'You can change it again on ' + new Date(
                             account.renameAt).toLocaleDateString() + '.'
                         : 'Once every 30 days. The old one stays yours ' +
                           'for 30 days after.' }),
                     rename.form),
             section('Key',
                     el('p', { className: 'hint', textContent:
                         'A new key needs the current one, and logs out ' +
                         'every other browser' + (passkeys
                             ? ' and removes your passkeys, since whoever ' +
                               'had the old key could have added one.'
                             : '.') }),
                     replace.form),
             section('Log out',
                     el('p', { className: 'hint', textContent:
                         'Logged out, you join as a guest.' }), logout),
             section('Delete account',
                     el('p', { className: 'hint', textContent:
                         'This needs the key itself and your handle typed ' +
                         'out, and cannot be undone. The handle stays ' +
                         'nobody\'s for 30 days.' }),
                     remove.form));
    }

    /* The account's passkeys, and a form to add one: with the key, as
       the relay asks, so a borrowed browser cannot add a way in of its
       own. */
    function passkeyPart (session, account, key)
    {
        const day = (ms) => new Date(ms).toLocaleDateString();
        const list = el('ul', { className: 'passkeys' });
        const add = keyForm('passkey', account.handle, 'current-password',
                            'Add a passkey');
        const row = (p) =>
        {
            const remove = button('Remove', () => busy(remove, async () =>
            {
                await api.removePasskey(session, p.id);
                say('Passkey removed. Your key still logs in.');
                fill();
            }));

            const used = p.lastUsedAt === null
                ? 'not used yet' : `last used ${day(p.lastUsedAt)}`;

            return el('li', {}, `${p.label}, added ${day(p.createdAt)}, ` +
                      `${used} `, remove);
        };
        const fill = () => api.passkeys(session).then(
            ({ passkeys: all }) => list.replaceChildren(
                ...(all.length === 0 ? [el('li', { textContent: 'None yet.' })]
                                     : all.map(row))),
            (e) => say(failed(e)));

        add.key.value = key;
        add.form.onsubmit = (e) =>
        {
            e.preventDefault();
            busy(add.submit, async () =>
            {
                await api.addPasskey(session, add.key.value);
                say('Passkey added.');
                add.key.value = '';
                fill();
            }, 'No passkey was added: it was canceled, or this device ' +
               'cannot make one.');
        };
        fill();

        return section('Passkeys',
                       el('p', { className: 'hint', textContent:
                           'A passkey logs you in without the key. Adding ' +
                           'one takes the key; removing one leaves the key ' +
                           'working, and logs nobody out: a new key, below, ' +
                           'logs out every other browser.' }),
                       list, add.form);
    }

    open.addEventListener('click', async () =>
    {
        say('');
        dialog.showModal();

        if (kept === null)
        {
            loggedOut();
            return;
        }

        show();
        say('Loading...');

        try
        {
            const res = await api.me(kept.session);

            say('');
            changed({ ...kept, handle: res.account.handle });
            loggedIn(res);
        }
        catch (e)
        {
            if (e.code !== 'unauthorized' && e.code !== 'banned')
            {
                say(failed(e));
                return;
            }

            changed(null);
            say(e.code === 'banned' ? failed(e)
                                    : 'Your session has ended; log in again.');
            loggedOut();
        }
    });

    /* The relays the page worked out (jam.js, relaysOf), in use: which is
       home, what is kept for it, and whether it has accounts. A join
       hands them in again, and they differ only when the first look at
       config.json failed and the join's worked. */
    let using = null;
    let usingFor = null;

    const use = (where) =>
    {
        const o = apiOriginOf(where.url);

        /* Home is what config.json says, and only once it has said it. */
        const home = where.read && o !== null && o === apiOriginOf(where.home)
            ? o : null;

        if (using !== null && home === usingFor)
            return using;

        usingFor = home;
        origin = home;
        kept = home === null ? null : load(home);
        api = null;
        passkeys = false;
        open.hidden = true;

        /* Whether the relay has accounts, as its health line says. Only
           then is the name the handle, and the button shown: a relay
           without them -- rolled back, or with no CORS_ORIGIN -- leaves
           the page a guest with a name of its own. A kept session is
           asked after, and only this relay's refusal of it logs the page
           out; a relay merely out of reach does not. */
        const asking = (async () =>
        {
            if (origin === null)
                return;

            const health = await (await fetch(`${origin}/`)).json();

            if (health.accounts !== true || usingFor !== home)
                return;

            api = client(origin);
            open.hidden = false;

            /* WebAuthn refuses a page off the RP ID's domain: 127.0.0.1
               for a relay on localhost, or a copy of the site served
               elsewhere. */
            passkeys = typeof health.passkeys === 'string' &&
                       onRpId(location.hostname, health.passkeys) &&
                       browserSupportsWebAuthn();

            if (kept === null)
                return;

            onChange(kept.handle);
            api.me(kept.session).then(
                (res) => changed({ ...kept, handle: res.account.handle }),
                (e) =>
                {
                    if (e.code === 'unauthorized' || e.code === 'banned')
                        changed(null);
                });
        })().catch(() =>
        {
            /* Out of reach: asked again at the next use. */
            if (using === asking)
                using = null;
        });

        using = asking;
        return using;
    };

    relays().then(use, () => {});

    return {
        /* The session a join to `where' sends, or null to join as a
           guest: only to the home relay, and only once it has said it has
           accounts. */
        session: async (where) =>
        {
            await use(where);
            return api === null ? null : kept?.session ?? null;
        },

        /* The relay has refused `session': this page is a guest there
           now, unless another tab has kept a newer one since, which is
           this page's from here on. */
        ended: (session) =>
        {
            if (origin === null)
                return;

            const now = load(origin) ?? kept;

            if (now === null || now.session === session)
                changed(null);
            else
            {
                kept = now;
                onChange(now.handle);
            }
        },
    };
}
