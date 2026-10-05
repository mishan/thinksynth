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
 * in with a key, and once logged in change the handle, replace the key,
 * log out or delete the account. Opened from the join card.
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
 * The page keeps the session and the handle beside it, never the key.
 */

import { ACCOUNT_API, apiOriginOf, normalizeName } from './account.js';

/* A session is kept under the relay it is for: one relay's is never
   another's to see, nor to end. */
const STORE = 'thinksynth:account:';

/* A request slower than this is a relay that is not answering. */
const REQUEST_MS = 10000;

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
        let res;

        try
        {
            res = await fetch(`${origin}${ACCOUNT_API}${route}`, {
                method: route === '/me' ? 'GET' : 'POST',
                headers: {
                    ...(route === '/me'
                        ? {} : { 'Content-Type': 'application/json' }),
                    ...(session ? { Authorization: `Bearer ${session}` } : {}),
                },
                body: route === '/me' ? undefined : JSON.stringify(body ?? {}),
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
    };
}

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

    return { form, key, submit };
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

    const show = (...nodes) => body.replaceChildren(...nodes);
    const changed = (k) =>
    {
        kept = k;
        keep(origin, k);
        open.textContent = k === null ? 'Log in' : 'Account';
        onChange(k?.handle ?? null);
    };

    /* While a request is out, its button is not pressed again. */
    const busy = async (b, run) =>
    {
        b.disabled = true;

        try
        {
            await run();
        }
        catch (e)
        {
            say(failed(e));
        }
        finally
        {
            b.disabled = false;
        }
    };

    function loggedOut ()
    {
        const handle = el('input', { id: 'account-handle', maxLength: 64,
                                     autocomplete: 'off' });
        const create = button('Create account', () => busy(create, async () =>
        {
            const h = normalizeName(handle.value);

            if (h === null)
            {
                say('A handle needs at least one visible character.');
                return;
            }

            const res = await api.register(h);

            changed({ session: res.session, handle: res.account.handle });
            say('');
            saveKey(res.account.handle, res.key, true, () => loggedIn(res));
        }));
        const login = keyForm('login', '', 'current-password', 'Log in');

        login.form.onsubmit = (e) =>
        {
            e.preventDefault();
            busy(login.submit, async () =>
            {
                const res = await api.login(login.key.value);

                changed({ session: res.session, handle: res.account.handle });
                say(`Logged in as ${res.account.handle}.`);

                /* Gone on success: what a password manager watches for. */
                loggedIn(res);
            });
        };

        show(el('p', { textContent:
                'An account is a handle the room knows you by, which nobody ' +
                'else can take. There is no email or password: the relay ' +
                'gives you a key of eight words, and the key is the ' +
                'account. Keep it in your password manager. Without one ' +
                'you join as a guest.' }),
             section('Create an account',
                     el('label', {}, 'Handle ', handle), create),
             section('Log in', login.form));
    }

    /* A key just issued, in a form a password manager will save, with
       Show, Copy and Download beside it. `then' goes on once saved. */
    function saveKey (handle, key, isNew, then)
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

        show(section(isNew ? 'Your account key' : 'Your new key',
            el('p', { className: 'accountwarn', textContent:
                (isNew ? '' : 'The old key no longer works, and every other ' +
                              'browser is logged out. ') +
                'This key is the only way into your account, here or on any ' +
                'other browser, and it cannot be recovered. Save it in your ' +
                'password manager now.' }),
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

    function loggedIn ({ account })
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

                /* The session this page had ended with the old key. */
                changed({ session: res.session, handle: account.handle });

                say('');
                saveKey(account.handle, res.key, false,
                        () => loggedIn({ account }));
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
                         'every other browser.' }),
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
