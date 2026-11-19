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
 */

/*
 * hid.js -- Valve Index controllers, wired or on a Watchman dongle, played
 * onto the page's keys through WebHID (docs/HID.md).
 *
 * The devices are the page's: WebHID exists only here. What their reports
 * mean is the desktop's own code, built small as thinkhid (thinkhid.cpp):
 * this feeds it each report and plays what it answers. Notes go to the
 * page's press and release, as a MIDI key's do (midi.js); a value or a bend
 * goes to the synth (host.js, hid()).
 *
 * The module is loaded only once a controller is in play -- one granted
 * before, one plugged in that was, or the button pressed -- so a page in a
 * browser without WebHID, or a player without a controller, never fetches
 * it.
 *
 * A wired controller is three devices, its IMU's, its light sensors' and
 * its input's; only the IMU's takes feature reports, so it is the one
 * woken and asked for the configuration (its hand). WebHID gives no serial
 * number to tell two wired controllers' interfaces apart, so one wired
 * controller at a time; any number of dongles. A dongle answers for its
 * controller only while one is on the radio, so it is asked each second
 * until one does, switched to the messages thinkhid reads (v2, in
 * thhIndexWireless.h), and asked again when it says the controller has
 * gone.
 */

const VALVE = 0x28de;
const WIRED = 0x2300;
const DONGLES = [0x2101, 0x2102];
const FILTERS = [WIRED, ...DONGLES].map((productId) =>
    ({ vendorId: VALVE, productId }));

/* hid/thhMapper.h's thhAction::Kind, by the names th_take writes. */
const KIND = { on: 0, off: 1, value: 2, bend: 3, aux: 4 };

/* thhHost::From. */
const FROM_INPUT = 0, FROM_IMU = 1, FROM_RADIO = 2;

/* A feature report is turned away once or more before it is answered, as
   the desktop's are (thhIndexHidraw.cpp); this many tries, this far apart. */
const TRIES = 20, TRY_MS = 3;

/* How often a dongle with no controller is asked again, and the patch's
   controller block looked at again. */
const PROBE_MS = 1000;

/* thhIndexWireless.h's THH_INDEX_WIRELESS_V2 and _PAIR, as WebHID sends
   them: the report ID apart, the rest padded to the dongle's 63. */
const V2 = Object.assign(new Uint8Array(63), [0x87, 0x03, 0x00, 0x01, 0x00]);
const PAIR = Object.assign(new Uint8Array(63), [0xad, 0x03, 0x01, 0x30, 0x75]);
const PAIR_S = 30;

/* What the browser keeps of the player's: the profile, in the language of
   the desktop's hid/index, and the hands a pair was swapped to. Either can
   be refused -- a private window, storage cleared -- and then lasts only
   as long as the page. */
const PROFILE_KEY = 'thinksynth:hid:index';
const HANDS_KEY = 'thinksynth:hid:hands';

const kept = (key) =>
{
    try
    {
        return localStorage.getItem(key) ?? '';
    }
    catch
    {
        return '';
    }
};

const keep = (key, text) =>
{
    try
    {
        localStorage.setItem(key, text);
    }
    catch
    {
        /* For this page only, then. */
    }
};

const now = () => performance.now() / 1000;
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

export function hidAvailable ()
{
    return typeof navigator !== 'undefined' && 'hid' in navigator;
}

/* The controller button and its status. `onNoteOn(note, velocity)' and
 * `onNoteOff(note)' are the page's press and release; `play(action)' a
 * value, bend or aux for the synth (host.js); `channel()' the channel the
 * controllers play, 0-based; `bendChannel(note)' the channel note sounds
 * on, or null where a bend can't reach it (a piece's keys); `controller
 * (channel)' asks what a patch there gives a controller (host.js).
 *
 * `pane' is the controller pane's elements, drawn here while it is open
 * (drawPane): its `root' (a <details>), `line', `rows' (a <tbody>), the
 * `pair', `swap' and `clear' buttons, and the profile's `profile' (a
 * <textarea>), `reload', `file' (an <input type=file>) and `problem'.
 *
 * Returns { start, forget }: start once there is a synth to play, which
 * enables the button and takes up any controller granted before; forget
 * drops every note held without calling onNoteOff, for a page that has
 * just released everything itself.
 */
export function hidToggle ({ button, status, onNoteOn, onNoteOff, play,
                             channel, bendChannel, controller, pane })
{
    let M = null, loading = null;
    let block = null, chan = null, ticking = 0, flushing = 0;
    const open = new Map();         /* HIDDevice -> its record             */
    const values = new Map();       /* "channel arg" -> latest value       */
    let wired = null;               /* the wired controller's slot id      */
    let pairUntil = 0;              /* when the dongle stops listening     */
    let problem = '';               /* what is wrong with the profile      */
    let profile = kept(PROFILE_KEY);

    const say = (text) => { status.textContent = text; };

    /* A load that fails -- offline before the module was ever kept -- is
       tried again the next time a controller is in play. */
    const load = () => loading ??= (async () =>
    {
        const { default: createThinkHid } = await import('./thinkhid.js');

        M = await createThinkHid();
        M.ccall('th_remembered', null, ['string'], [kept(HANDS_KEY)]);
        problem = M.ccall('th_profile', 'string', ['string'], [profile]);
        setInterval(() =>
        {
            if (wired !== null || [...open.values()].some((r) => r.id !== null))
                follow().catch(() => {});
        }, PROBE_MS);
    })().catch((e) =>
    {
        loading = null;
        throw e;
    });

    const tellStatus = () =>
    {
        const dongles = [...open.values()].filter((r) => r.kind === 'dongle');
        const playing = dongles.filter((r) => r.id !== null).length +
                        (wired !== null ? 1 : 0);

        say(now() < pairUntil && dongles.some((r) => r.id === null)
            ? 'Pairing: turn the controller off, then hold its System and B ' +
              'buttons until its light blinks blue.'
            : playing === 0 && dongles.length > 0
            ? 'No controller on the dongle: turn one on, or pair it.'
            : playing === 1 ? 'A Valve Index controller is playing.'
            : playing > 1 ? `${playing} Valve Index controllers are playing.`
            : '');
    };

    /* The values, the latest of each, once a frame: a controller's axes
       move every few milliseconds, and the synth needs only what is new. */
    const flush = () =>
    {
        flushing = 0;
        for (const a of values.values())
            play(a);
        values.clear();
    };

    const take = () =>
    {
        for (const a of JSON.parse(M.ccall('th_take', 'string', [], [])))
        {
            if (a.kind === 'on')
                onNoteOn(a.note, a.velocity);
            else if (a.kind === 'off')
                onNoteOff(a.note);
            else if (a.kind === 'value')
            {
                values.set(`${a.channel} ${a.arg}`,
                           { ...a, kind: KIND.value });
                flushing ||= requestAnimationFrame(flush);
            }
            else
            {
                const on = bendChannel(a.note);

                if (on !== null)
                    play({ ...a, kind: KIND[a.kind], channel: on,
                           frame: -1 });
            }
        }

        /* The loops, played every few milliseconds while there are any. */
        if (!ticking && M.ccall('th_tick', 'number', ['number'], [now()]))
            ticking = setInterval(() =>
            {
                const more = M.ccall('th_tick', 'number', ['number'], [now()]);

                take();
                if (!more)
                {
                    clearInterval(ticking);
                    ticking = 0;
                }
            }, 5);
    };

    /* The channel the bindings that name none play, kept as the page
       changes it: read with every report, as cheap as it is. */
    const track = () =>
    {
        const ch = channel();

        if (ch !== chan)
        {
            chan = ch;
            M.ccall('th_channel', null, ['number'], [ch]);
        }
        return ch;
    };

    /* And the patch's controller block there, which only a question of
       the worklet can say: asked once a second while one plays. */
    const follow = async () =>
    {
        const c = await controller(track());
        const said = JSON.stringify(c);

        if (said !== block)
        {
            block = said;
            M.ccall('th_patch', 'string', ['string', 'string', 'string'],
                    [c.block, c.name, c.args.join('\n')]);
            take();
        }
    };

    /* Each device's feature reports one after another: a probe's reads and
       a switch must not interleave. A job is dropped once its device has
       gone, and one that fails doesn't stop the jobs after it. */
    const queued = (r, job) => (r.queue = r.queue
        .then(() => r.closed ? undefined : job())
        .catch(() => {}));

    const feature = async (r, id) =>
    {
        for (let k = 0; k < TRIES; k++)
        {
            try
            {
                return new Uint8Array((await r.device.receiveFeatureReport(id))
                                      .buffer);
            }
            catch
            {
                await sleep(TRY_MS);
            }
        }
        return null;
    };

    /* The configuration, a feature report at a time (0x10 says how long,
       each 0x11 a length and that many bytes), inflated, and what it says:
       hand and serial (th_config). Null if it isn't answered. */
    const configure = async (r) =>
    {
        const start = await feature(r, 0x10);

        if (start === null)
            return null;

        const length = (start[1] << 8) | start[2];
        const bytes = [];

        while (bytes.length < length)
        {
            const part = await feature(r, 0x11);

            if (part === null || part[1] === 0)
                break;
            bytes.push(...part.subarray(2, 2 + part[1]));
        }

        const json = await new Response(new Blob([new Uint8Array(bytes)])
            .stream().pipeThrough(new DecompressionStream('deflate'))).text()
            .catch(() => '');
        const [hand, serial] = M.ccall('th_config', 'string', ['string'],
                                       [json]).split('\n');

        return { hand, serial };
    };

    const add = ({ hand, serial }) =>
        M.ccall('th_add', 'number', ['string', 'string', 'number'],
                [hand, serial, now()]);

    const drop = (id) =>
    {
        M.ccall('th_drop', null, ['number', 'number'], [id, now()]);
        take();
    };

    /* A dongle asked until its controller answers: switched, its hand
       read, and played. One try at a time, each queued, so a pair asked
       for meanwhile goes between two tries rather than after them all. */
    const probe = (r) => queued(r, async () =>
    {
        if (r.closed || r.id !== null)
            return;
        try
        {
            if (await feature(r, 0x10) !== null)
            {
                await r.device.sendFeatureReport(0xff, V2);

                const said = await configure(r);

                await follow();
                if (said !== null && !r.closed)
                {
                    pairUntil = 0;
                    r.id = add(said);
                    take();
                    tellStatus();
                    return;
                }
            }
        }
        catch
        {
            /* Turned down, or gone: tried again below, or not at all. */
        }
        tellStatus();
        setTimeout(() => probe(r), PROBE_MS);
    });

    const report = (r, e) =>
    {
        const id = r.kind === 'dongle' ? r.id : wired;

        if (id === null || r.kind === 'light')
            return;

        const data = new Uint8Array(e.data.buffer, e.data.byteOffset,
                                    e.data.byteLength);
        const bytes = e.reportId === 0 ? data
                      : Uint8Array.of(e.reportId, ...data);

        track();

        const gone = M.ccall('th_report', 'number',
                             ['number', 'number', 'array', 'number', 'number'],
                             [id, r.kind === 'dongle' ? FROM_RADIO
                                  : r.kind === 'imu' ? FROM_IMU : FROM_INPUT,
                              bytes, bytes.length, now()]);

        take();
        if (gone && r.kind === 'dongle')
        {
            r.id = null;
            drop(id);
            tellStatus();
            probe(r);
        }
    };

    const kindOf = (device) =>
        DONGLES.includes(device.productId) ? 'dongle'
        : device.collections.some((c) =>
              c.featureReports.some((f) => f.reportId === 0x10)) ? 'imu'
        : device.collections.some((c) =>
              c.inputReports.some((i) => i.reportId === 0)) ? 'input'
        : 'light';

    const takeUp = async (device) =>
    {
        if (device.vendorId !== VALVE || open.has(device))
            return;

        const r = { device, kind: kindOf(device), id: null, closed: false,
                    queue: Promise.resolve(), listen: (e) => report(r, e) };

        /* A second wired controller's interfaces would play as the first's:
           nothing in WebHID tells the two apart. */
        if (r.kind !== 'dongle' &&
            [...open.values()].some((o) => o.kind === r.kind))
        {
            say('Two wired controllers can\'t be told apart here: play the ' +
                'second through a Watchman dongle.');
            return;
        }

        open.set(device, r);
        try
        {
            await load();
            if (!device.opened)
                await device.open();
        }
        catch (e)
        {
            open.delete(device);
            say(`Can't open the controller: ${e.message}`);
            return;
        }

        device.addEventListener('inputreport', r.listen);

        if (r.kind === 'dongle')
            probe(r);
        else if (r.kind === 'imu')
            queued(r, async () =>
            {
                await device.sendFeatureReport(0x04, new Uint8Array(4));
                await device.sendFeatureReport(0x07, Uint8Array.of(3, 0, 0, 0));

                const said = await configure(r);

                await follow();
                if (r.closed)
                    return;
                wired = add(said ?? { hand: '', serial: '' });
                take();
                tellStatus();
            });
    };

    const gone = (device) =>
    {
        const r = open.get(device);

        if (r === undefined)
            return;
        open.delete(device);
        r.closed = true;
        device.removeEventListener('inputreport', r.listen);
        if (r.kind === 'dongle' && r.id !== null)
            drop(r.id);
        else if (r.kind !== 'dongle' && wired !== null)
        {
            const id = wired;

            wired = null;
            drop(id);
        }
        tellStatus();
    };

    /* The first dongle hearing no controller, listening for one in pairing
       mode, queued with its probe so the two don't interleave. */
    const pair = () =>
    {
        const r = [...open.values()].find((o) => o.kind === 'dongle' &&
                                                 o.id === null);

        if (r === undefined)
            return;
        pairUntil = now() + PAIR_S;
        queued(r, () => r.device.sendFeatureReport(0xff, PAIR).catch((e) =>
        {
            pairUntil = 0;
            say(`Can't pair with the dongle: ${e.message}`);
        }));
        tellStatus();
        setTimeout(tellStatus, PAIR_S * 1000);
    };

    /* The pane, from thinkhid's (thhHost::pane): the desktop's rows, said
       the desktop's way (src/gui/ControllerPane.cpp). */
    const drawPane = () =>
    {
        const p = M === null ? { hands: [], rows: [], canSwap: false,
                                 loops: '', patch: '' }
                             : JSON.parse(M.ccall('th_pane', 'string', [], []));
        const lines = [p.hands.length > 1 && `${p.hands.join(' and ')} playing.`,
                       'Profile: the controller\'s own' +
                       (profile.trim() ? ', with your changes below'
                                       : ' (no changes of yours yet)'),
                       p.patch && `Patch: ${p.patch}`,
                       p.loops && `Loops: ${p.loops}`].filter(Boolean);

        pane.line.textContent = lines.join('\n');
        pane.pair.hidden = !(now() >= pairUntil &&
                             [...open.values()].some((r) => r.kind === 'dongle' &&
                                                            r.id === null));
        pane.swap.hidden = !p.canSwap;
        pane.clear.hidden = p.loops === '';
        pane.problem.textContent = problem;

        const cells = p.rows.map((r) =>
        {
            const keys = r.target.startsWith('the keyboard\'s');

            return [(r.layer ? `${r.layer}: ` : '') + r.control, r.value,
                    r.action + (r.target === '' || keys ? '' : `, on ${r.target}`) +
                    (r.yours ? ' (yours)' : r.patch ? ' (the patch\'s)' : ''),
                    r.warning];
        });

        while (pane.rows.rows.length > cells.length)
            pane.rows.deleteRow(-1);
        cells.forEach((row, i) =>
        {
            const tr = pane.rows.rows[i] ?? pane.rows.insertRow();

            row.forEach((text, k) =>
            {
                const td = tr.cells[k] ?? tr.insertCell();

                if (td.textContent !== text)
                    td.textContent = text;
            });
        });
    };

    const setProfile = (text) =>
    {
        profile = text;
        keep(PROFILE_KEY, text);
        if (M !== null)
        {
            problem = M.ccall('th_profile', 'string', ['string'], [text]);
            take();
        }
        drawPane();
    };

    const wirePane = () =>
    {
        let drawing = 0;

        pane.profile.value = profile;
        pane.root.addEventListener('toggle', () =>
        {
            clearInterval(drawing);
            drawing = pane.root.open ? setInterval(drawPane, 100) : 0;
            drawPane();
        });
        pane.pair.addEventListener('click', () => { pair(); drawPane(); });
        pane.swap.addEventListener('click', () =>
        {
            M.ccall('th_swap', null, [], []);
            keep(HANDS_KEY, M.ccall('th_kept', 'string', [], []));
            take();
            drawPane();
        });
        pane.clear.addEventListener('click', () =>
        {
            M.ccall('th_clear_loops', null, [], []);
            take();
            drawPane();
        });
        pane.reload.addEventListener('click', () => setProfile(pane.profile.value));
        pane.file.addEventListener('change', async () =>
        {
            const f = pane.file.files[0];

            if (f === undefined)
                return;
            pane.profile.value = await f.text();
            setProfile(pane.profile.value);
            pane.file.value = '';
        });
    };

    return {
        async start ()
        {
            if (!hidAvailable())
            {
                say('Controllers need Chrome or Edge.');
                return;
            }

            button.disabled = false;
            pane.root.hidden = false;
            wirePane();
            button.addEventListener('click', async () =>
            {
                let picked = [];

                try
                {
                    picked = await navigator.hid.requestDevice(
                        { filters: FILTERS });
                }
                catch (e)
                {
                    say(`Can't ask for a controller: ${e.message}`);
                }
                for (const device of picked)
                    await takeUp(device);
            });

            navigator.hid.addEventListener('connect', (e) => takeUp(e.device));
            navigator.hid.addEventListener('disconnect', (e) => gone(e.device));

            for (const device of await navigator.hid.getDevices())
                if (FILTERS.some((f) => f.productId === device.productId &&
                                        f.vendorId === device.vendorId))
                    await takeUp(device);
        },

        forget ()
        {
            if (M === null)
                return;
            M.ccall('th_release', null, ['number'], [now()]);
            M.ccall('th_take', 'string', [], []);
            values.clear();
        },
    };
}
