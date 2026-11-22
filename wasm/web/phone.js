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
 * phone.js -- the phone's side (phone.html, docs/PHONE.md): opened by the
 * link the page shows, it joins that page's pairing and sends it the
 * phone's tilt and fingers, as phonepad.js makes them, for the page to
 * play as the device `phone'.
 */

import { Mesh } from './mesh.js';
import { Pair, pairUrl } from './pair.js';
import { Fingers, Sender, tilt } from './phonepad.js';

const pad = document.getElementById('pad');
const startButton = document.getElementById('start');
const status = document.getElementById('status');
const say = (text) => { status.textContent = text; };

const code = new URLSearchParams(location.hash.slice(1)).get('pair');
const tokenKey = `thinksynth:phone:${code}`;

/* How often the sender is asked: it decides what goes out (phonepad.js). */
const TICK_MS = 1000 / 60;

const fingers = new Fingers();
const sender = new Sender();
const marks = new Map();            /* pointer id -> its mark on the pad */
let gravity = { x: 0, y: 0, z: 9.81 };
let angle = 0;
let mesh = null, pair = null, lock = null, ticker = 0;

const screenAngle = () => screen.orientation?.angle ?? window.orientation ?? 0;

const send = () =>
{
    const sent = mesh === null ? null
        : sender.next(fingers.axes(tilt(gravity, angle)), fingers.bits,
                      performance.now());

    if (sent !== null)
        mesh.broadcast({ type: 'phone', ...sent.frame }, sent.both);
};

/* Where a pointer is, as the pad's x across and y up, 0 to 1. */
const place = (e) =>
    [Math.min(1, Math.max(0, e.clientX / innerWidth)),
     Math.min(1, Math.max(0, 1 - e.clientY / innerHeight))];

const mark = (e) =>
{
    let m = marks.get(e.pointerId);

    if (m === undefined)
    {
        m = document.createElement('div');
        m.className = 'finger';
        pad.append(m);
        marks.set(e.pointerId, m);
    }
    m.style.left = `${e.clientX}px`;
    m.style.top = `${e.clientY}px`;
};

const unmark = (id) =>
{
    marks.get(id)?.remove();
    marks.delete(id);
};

/* The screen's turn, held while a finger is down so that turning the
   phone doesn't jump a note. */
const turned = () =>
{
    if (fingers.bits === 0)
        angle = screenAngle();
};

pad.addEventListener('pointerdown', (e) =>
{
    if (pair === null || e.target === startButton)
        return;
    e.preventDefault();
    turned();
    if (fingers.down(e.pointerId, ...place(e)) >= 0)
    {
        pad.setPointerCapture?.(e.pointerId);
        mark(e);
    }
    send();
});
pad.addEventListener('pointermove', (e) =>
{
    fingers.move(e.pointerId, ...place(e));
    if (marks.has(e.pointerId))
        mark(e);
});
for (const type of ['pointerup', 'pointercancel', 'lostpointercapture'])
    pad.addEventListener(type, (e) =>
    {
        fingers.up(e.pointerId);
        unmark(e.pointerId);
        send();
    });

addEventListener('devicemotion', (e) =>
{
    const g = e.accelerationIncludingGravity;

    if (g?.x != null)
        gravity = { x: g.x, y: g.y, z: g.z ?? 0 };
});
screen.orientation?.addEventListener?.('change', turned);

/* Hidden or closing, the phone lets its fingers go itself, a frame the
   sender puts on both channels, rather than leave the page to wait for a
   heartbeat that won't come. */
const away = () =>
{
    fingers.clear();
    for (const id of [...marks.keys()])
        unmark(id);
    send();
};

document.addEventListener('visibilitychange', () =>
{
    if (document.hidden)
        away();
    else if (pair !== null)
        wake();
});
addEventListener('pagehide', away);

/* Kept awake while paired: tilt alone is played with no finger on the
   screen, and a screen gone dark is a phone gone. */
const wake = async () =>
{
    try
    {
        lock = await navigator.wakeLock?.request('screen');
    }
    catch
    {
    }
};

/* Both of iOS's, inside the tap that asked: a page may read motion only
   once the player has said it may. */
const motion = async () =>
{
    const asks = [globalThis.DeviceMotionEvent, globalThis.DeviceOrientationEvent]
        .filter((k) => typeof k?.requestPermission === 'function')
        .map((k) => k.requestPermission().catch(() => 'denied'));
    const answers = await Promise.all(asks);

    return answers.every((a) => a === 'granted');
};

startButton.addEventListener('click', async () =>
{
    startButton.hidden = true;

    const moving = await motion();

    say(moving ? 'Joining...' : 'Joining, with touch only: motion isn\'t allowed.');
    wake();

    let token = null;

    try
    {
        token = sessionStorage.getItem(tokenKey);
    }
    catch
    {
    }

    const what = navigator.userAgentData?.platform ||
                 (/iPhone|iPad|Android/.exec(navigator.userAgent)?.[0] ?? '');

    pair = new Pair(await pairUrl(), token ? { code, token } : { code, what });
    mesh = new Mesh(pair, () => {});
    pair.on('token', (t) =>
    {
        try
        {
            sessionStorage.setItem(tokenKey, t);
        }
        catch
        {
        }
    });
    pair.on('peer', (here) => say(here ? 'Playing: touch, slide and tilt.'
                                       : 'The page has gone; waiting for it.'));
    const done = (text) =>
    {
        clearInterval(ticker);
        mesh?.close();
        mesh = pair = null;
        lock?.release?.();
        say(text);
    };
    pair.on('ended', (why) => done(why === 'opened elsewhere'
        ? 'Playing from another tab now.' : 'Unpaired. Scan the page\'s code again to play.'));
    pair.on('refused', (why) => done(why === 'unknown' || why === 'spent'
        ? 'That code is used or gone: scan the page\'s code again.'
        : / /.test(why) ? `Couldn't join: ${why}.`
        : `The relay said no (${why}).`));
    ticker = setInterval(() => send(), TICK_MS);
});

if (code === null)
{
    startButton.hidden = true;
    say('Open this from the code a thinksynth page shows.');
}
