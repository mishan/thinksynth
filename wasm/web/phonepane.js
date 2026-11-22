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
 * phonepane.js -- a phone paired with this page and played as a controller
 * (docs/PHONE.md).
 *
 * The Phone button asks the relay for a pairing (pair.js) and shows its
 * link, and a QR code of it, for the phone to open (phone.html). Once the
 * phone has joined, its frames come over the link (mesh.js) into the
 * controller module (thinkhid.cpp's th_phone_*), whose actions are played
 * as the Index's are (actions.js). The pairing's code and this page's
 * token are kept for the tab, so a reload comes back to the same pairing.
 * The pane's form (phoneform.js) and text box edit the player's profile,
 * paired or not.
 */

import { actionPlayer } from './actions.js';
import { Mesh } from './mesh.js';
import { Pair, pairUrl } from './pair.js';
import { profileForm } from './phoneform.js';
import qrcode from './qrcode.js';

/* The player's changes to the phone's own profile, as hid.js keeps the
   Index's. */
const PROFILE_KEY = 'thinksynth:hid:phone';
/* The pairing this tab is in. */
const PAIRING_KEY = 'thinksynth:phone:pairing';

/* How often the module is asked whether the phone has gone quiet. */
const TICK_MS = 100;

const AXES = 12;

/* The longest profile handed the module, whose strings go on its stack:
   at most three bytes a character, well inside it. */
const MAX_PROFILE = 16384;

/* `session' for the tab's storage, else the browser's. Looked up inside
   the try: a browser that refuses storage refuses it by throwing from the
   lookup itself, and the phone plays on without it. */
function kept (key, session = false)
{
    try
    {
        return (session ? sessionStorage : localStorage).getItem(key) ?? '';
    }
    catch
    {
        return '';
    }
}

function keep (key, value, session = false)
{
    try
    {
        const store = session ? sessionStorage : localStorage;

        if (value === null)
            store.removeItem(key);
        else
            store.setItem(key, value);
    }
    catch
    {
    }
}

/* `pane' is { root, qr, link, end, again }: the details the code and the
   pairing's state show in; and { form, rows, problem, learn, profile,
   apply }, the form's details, its rows, its line, Learn, and the text
   box with its button. `controller(channel)' is the patch's, as hid.js
   has it, for its knobs' names. */
export function phoneToggle ({ button, status, pane, onNoteOn, onNoteOff, play,
                               channel, bendChannel, controller })
{
    const played = actionPlayer({ onNoteOn, onNoteOff, play, bendChannel });
    let M = null, chan = null;
    /* The pairing under way, from the click that starts it: every handler
       checks it is still the one, so one ended or superseded can't touch
       the next. */
    let current = null;

    const now = () => performance.now() / 1000;
    const say = (text) => { status.textContent = text; };
    const take = () =>
        played(JSON.parse(M.ccall('th_phone_take', 'string', [], [])));
    const track = () =>
    {
        const ch = channel();

        if (ch !== chan)
            M.ccall('th_channel', null, ['number'], [chan = ch]);
    };
    /* The player's profile: kept, and here too, for a browser that won't
       keep it. */
    let mine = kept(PROFILE_KEY);
    const profile = () =>
        M.ccall('th_phone_profile', 'string', ['string'], [mine]);
    /* One module, however many ask for it while it loads. */
    let loading = null;
    const load = () => loading ??= import('./thinkhid.js')
        .then(({ default: createThinkHid }) => createThinkHid())
        .then((m) => { M = m; })
        .catch((e) => { loading = null; throw e; });

    const form = profileForm({
        rows: pane.rows, problem: pane.problem, learn: pane.learn,
        form: (text) => JSON.parse(M.ccall('th_phone_form', 'string', ['string'], [text])),
        edit: (text, op, control, which, fields) => pane.profile.value !== text
            ? { err: 'Your profile\'s text has changes; Apply them first.' }
            : JSON.parse(M.ccall('th_phone_edit', 'string',
                ['string', 'string', 'string', 'number', 'string'],
                [text, op, control, which, fields])),
        args: async () => (await controller(channel())).args,
        text: () => mine,
        changed: (text) => setProfile(text),
    });

    /* The player's profile made `text': kept, in the box, and played by a
       phone that is there. */
    const setProfile = (text) =>
    {
        if (text.length > MAX_PROFILE)
        {
            redraw(`Your profile is too long: ${MAX_PROFILE} characters at most.`);
            return;
        }
        mine = text;
        keep(PROFILE_KEY, text);
        pane.profile.value = text;
        if (current !== null && M !== null)
        {
            const problem = profile();

            take();
            if (problem)
            {
                redraw(`your phone profile: ${problem}`);
                return;
            }
        }
        redraw();
    };
    const redraw = (why) => load().then(() => form.draw(why));

    const show = (code) =>
    {
        const url = new URL(`phone.html#pair=${code}`, location.href).href;
        const qr = qrcode(0, 'M');

        qr.addData(url);
        qr.make();
        pane.qr.innerHTML = qr.createSvgTag({
            cellSize: 4, margin: 2, scalable: true,
            title: 'The pairing link as a QR code, for a phone to scan' });
        pane.qr.hidden = false;
        pane.link.href = url;
        pane.link.textContent = url;
        pane.link.hidden = false;
        pane.again.hidden = false;
    };

    const hide = () =>
    {
        pane.qr.hidden = true;
        pane.link.hidden = true;
        pane.again.hidden = true;
    };

    /* The pairing over, by either side or the relay: everything the phone
       held let go, and the button for another. */
    const over = (run, why) =>
    {
        if (run !== current)
            return;
        current = null;
        clearInterval(run.tick);
        run.mesh?.close();
        keep(PAIRING_KEY, null, true);
        if (M !== null)
        {
            M.ccall('th_phone_release', null, [], []);
            take();
        }
        hide();
        pane.end.hidden = true;
        button.textContent = 'Phone';
        say(why);
    };

    const heard = (run, cmd) =>
    {
        if (run !== current || cmd?.type !== 'phone' ||
            !Array.isArray(cmd.axes) || cmd.axes.length !== AXES)
            return;
        track();
        M.ccall('th_phone_frame', 'number',
                ['number', 'number', 'array', 'number', 'number'],
                [cmd.id >>> 0, cmd.seq >>> 0,
                 new Uint8Array(Float32Array.from(cmd.axes).buffer),
                 cmd.down >>> 0, now()]);
        take();
    };

    /* What the relay or pair.js said, as the page says it: pair.js's own
       reasons are sentences already. */
    const refused = (why) => why === 'busy' || why === 'rate'
        ? 'Too many pairings at once; try again in a minute.'
        : / /.test(why) ? `The phone couldn't pair: ${why}.`
        : `The relay said no (${why}).`;

    /* `back': { code, token, joined } kept from before a reload, or null
       for a new pairing. */
    const start = async (back) =>
    {
        const run = { pair: null, mesh: null, tick: 0, code: back?.code ?? null,
                      joined: back?.joined ?? false };

        current = run;
        pane.root.hidden = false;
        pane.root.open = true;
        button.textContent = 'Phone: on';
        pane.end.hidden = false;
        say('Asking the relay...');
        await load();

        const url = await pairUrl();

        if (run !== current)
            return;

        const problem = profile();

        take();
        if (problem)
            say(`your phone profile: ${problem}`);

        const remember = () => keep(PAIRING_KEY, JSON.stringify(
            { code: run.code, token: run.pair.token, joined: run.joined }), true);
        const pair = new Pair(url, back === null
            ? { make: true }
            : { code: back.code, token: back.token, host: true });

        run.pair = pair;
        run.mesh = new Mesh(pair, (from, cmd) => heard(run, cmd));
        pair.on('paired', (code) =>
        {
            run.code = code;
            remember();
            show(code);
            say('Open this on your phone, or scan it.');
        });
        /* Back after a reload: the code again, for a phone that hasn't
           opened it yet. */
        pair.on('token', () =>
        {
            if (!run.joined)
            {
                show(run.code);
                say('Open this on your phone, or scan it.');
            }
        });
        pair.on('peer', (here, what) =>
        {
            if (run !== current)
                return;
            if (here)
            {
                run.joined = true;
                remember();
                hide();
                say(`A phone is playing${what ? ` (${what})` : ''}.`);
            }
            else if (run.joined)
                say('The phone has gone; it can come back.');
        });
        pair.on('ended', (why) => over(run, why === 'expired'
            ? 'Nobody opened the code in time.' : 'The phone is unpaired.'));
        pair.on('refused', (why) => over(run, refused(why)));
        run.tick = setInterval(() =>
        {
            M.ccall('th_phone_tick', 'number', ['number'], [now()]);
            take();
        }, TICK_MS);
    };

    const fresh = () =>
        start(null).catch((e) => over(current, `The phone couldn't pair: ${e.message}`));

    button.addEventListener('click', () =>
    {
        /* One pairing at a time: a click while one starts is that one. */
        if (current === null)
            fresh();
        else if (current.pair !== null)
            current.pair.end();
    });
    pane.end.addEventListener('click', () => current?.pair?.end());
    pane.again.addEventListener('click', () =>
    {
        if (current?.pair === null)
            return;
        current?.pair.end();
        fresh();
    });

    let showing = 0, opened = 0;

    pane.profile.value = mine;
    pane.apply.addEventListener('click', () => setProfile(pane.profile.value));
    pane.form.addEventListener('toggle', async () =>
    {
        const run = ++opened;

        clearInterval(showing);
        showing = 0;
        if (!pane.form.open)
            return;
        await load();
        await form.draw();
        /* Closed, or opened again, while it drew. */
        if (run !== opened)
            return;
        showing = setInterval(() => form.values(JSON.parse(
            M.ccall('th_phone_values', 'string', ['number'], [now()]))), TICK_MS);
    });

    return {
        start ()
        {
            button.disabled = false;
            pane.root.hidden = false;

            let back = null;

            try
            {
                back = JSON.parse(kept(PAIRING_KEY, true) || 'null');
            }
            catch
            {
            }
            if (back?.code && back?.token)
                start(back).catch(() => over(current, ''));
        },

        /* Every note the phone held let go without telling the page, which
           has just released everything itself. A pairing under way, or
           starting, goes on. */
        forget ()
        {
            if (M === null)
                return;
            M.ccall('th_phone_release', null, [], []);
            M.ccall('th_phone_take', 'string', [], []);
            played.forget();
            if (current !== null)
                profile();
        },
    };
}
