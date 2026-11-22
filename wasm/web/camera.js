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
 * camera.js -- a webcam played as a controller, onto the page's keys.
 *
 * Moving a hand in one of nine zones of the picture strikes a note, as loud
 * as the zone was struck: the camera's own profile, an air harp
 * (hid/thhCamera.h), played by camworker.js. The notes go to the page's own
 * press and release, as a MIDI key's do (midi.js), so they play wherever
 * the keys play; the profile's channel is the page's.
 *
 * Each frame is drawn into a small canvas and its luma posted to the
 * worker, stamped with when the camera captured it where the browser says,
 * so the motion it reads is in camera time and not the page's.
 *
 * A feed that stops -- a hidden tab, a camera taken by another app -- sends
 * no frame to say so, and a zone struck when it stopped would hold its note
 * for good. After STALL_MS with no frame the worker is told the camera is
 * gone, which lets every note go; the next frame starts afresh.
 *
 * The player's profile of each, the air harp's (`camera') and the
 * theremin's (`hands'), plays over its own, edited on the camera's pane by
 * the configurator's form (profileform.js) or as text.
 */

import { wasmBytes } from './host.js';
import { MAX_PROFILE, profileForm } from './profileform.js';

/* The width frames are drawn at, as the desktop's reader decodes them
   (camera/thiCamera.h): enough for thMotion's zones, and little to post. */
const WIDTH = 160;

/* How long the tracker may take to answer before its notes are let go:
   longer than a camera's frames may be apart, since a tracker's first
   answer, or a phone's, can take a few hundred milliseconds. */
const TRACKER_STALL_MS = 1000;

/* How many frames in a row the tracker may leave unanswered, a stall watch
   apart, before it is taken for hung: ten seconds of them. */
const TRACKER_UNANSWERED = 20;

/* thMotion's MAX_GAP_SECONDS. */
const STALL_MS = 250;

/* The player's profiles, as the other devices' are kept: the air harp's
   and the theremin's. */
const profileKey = (device) => `thinksynth:hid:${device}`;

/* Looked up inside the try: a browser that refuses storage refuses it by
   throwing from the lookup itself, and the camera plays on without it. */
function kept (key)
{
    try
    {
        return localStorage.getItem(key) ?? '';
    }
    catch
    {
        return '';
    }
}

function keep (key, value)
{
    try
    {
        localStorage.setItem(key, value);
    }
    catch
    {
    }
}

/* True where the page can ask for a camera and hear of each frame. */
export function cameraAvailable ()
{
    return typeof navigator !== 'undefined' &&
           typeof navigator.mediaDevices?.getUserMedia === 'function' &&
           typeof HTMLVideoElement !== 'undefined' &&
           'requestVideoFrameCallback' in HTMLVideoElement.prototype;
}

/* A button that opens the camera and closes it again, a line beside it
 * saying how it went, and nine cells that light with the zones. The button
 * is left disabled for the page to enable once there is a synth to play.
 * `view', if given, is a camera view (cameraview.js): opened and closed
 * with the camera, and handed each frame. `mode' and `snap', if given, are
 * a <select> of harp and theremin and a checkbox, which the camera plays
 * by (openCamera, which takes `onBend' and `onAux'), opened afresh when
 * either changes while it is on; `hint()' adds to what the theremin's
 * status line says. `pane', if given, is the form's: { root, rows,
 * problem, learn, profile, apply } (the pane's <details>, its rows,
 * its line, Learn, and the text box with its button), editing the
 * profile of what `mode' plays.
 *
 * Returns { forget }, which is openCamera's forget on whatever is open.
 */
export function cameraToggle ({ button, status, zones, onNoteOn, onNoteOff,
                                view = null, mode = null, snap = null,
                                onBend, onAux, hint = () => '', pane = null })
{
    const label = button.textContent;
    const cells = [...zones.children];
    let open = null;

    const theremin = () => mode?.value === 'theremin';
    const device = () => (theremin() ? 'hands' : 'camera');
    /* Kept here too, for a browser that won't keep them. */
    const mine = { camera: kept(profileKey('camera')), hands: kept(profileKey('hands')) };
    let M = null, loading = null;
    const load = () => loading ??= import('./thinkhid.js')
        .then(({ default: createThinkHid }) => createThinkHid())
        .then((m) => { M = m; })
        .catch((e) => { loading = null; throw e; });
    /* The theremin's own profile is the unsnapped one when it plays
       unsnapped. */
    const formDevice = () => (theremin() && !(snap?.checked ?? true) ? 'hands:free' : device());
    const form = pane && profileForm({
        rows: pane.rows, problem: pane.problem, learn: pane.learn,
        form: (text) => JSON.parse(M.ccall('th_form', 'string', ['string', 'string'],
                                           [formDevice(), text])),
        edit: (text, op, control, which, fields) => pane.profile.value !== text
            ? { err: 'Your profile\'s text has changes; Apply them first.' }
            : JSON.parse(M.ccall('th_form_edit', 'string',
                ['string', 'string', 'string', 'string', 'number', 'string'],
                [formDevice(), text, op, control, which, fields])),
        args: async () => [],
        text: () => mine[device()],
        changed: (text) => setProfile(text),
    });
    const redraw = (why = '') => (pane?.root.open
        ? load().then(() => form.draw(why),
                      (e) => { pane.problem.textContent = `The form didn't load: ${e.message}`; })
        : Promise.resolve());
    const shown = () =>
    {
        if (pane === null)
            return;
        pane.profile.value = mine[device()];
        pane.profile.placeholder = `device ${device()} {\n};`;
        redraw();
    };

    /* The player's profile of what plays now made `text': kept, in the
       box, and played by a camera that is open. */
    const setProfile = (text) =>
    {
        if (text.length > MAX_PROFILE)
        {
            redraw(`Your profile is too long: ${MAX_PROFILE} characters at most.`);
            return;
        }
        mine[device()] = text;
        keep(profileKey(device()), text);
        pane.profile.value = text;
        open?.profile(text);
        redraw();
    };

    if (pane !== null)
    {
        pane.root.hidden = false;
        pane.apply.addEventListener('click', () => setProfile(pane.profile.value));
        pane.root.addEventListener('toggle', () => redraw());
        shown();
    }

    const shut = (why = '') =>
    {
        open?.close();
        open = null;
        form?.values([]);
        button.textContent = label;
        status.textContent = why;
        zones.hidden = true;
        view?.close();
    };

    button.addEventListener('click', async () =>
    {
        if (open !== null)
        {
            shut();
            return;
        }

        button.disabled = true;
        status.textContent = 'asking...';

        /* The worker can fail before the camera has finished opening. And
           what it plays is read once, before the permission prompt, which a
           change of mode made under it mustn't contradict. */
        let failed = null;
        const playing = mode?.value ?? 'harp';
        const snapped = snap?.checked ?? true;
        const startedWith = mine[playing === 'theremin' ? 'hands' : 'camera'];

        try
        {
            open = await openCamera({
                onNoteOn, onNoteOff, onBend, onAux,
                mode: playing, snap: snapped,
                profile: startedWith,
                onValues: (values) => form?.values(values),
                onProblem: (why) => redraw(why && `your profile: ${why}`),
                onHands: (left, right, video, points) =>
                    view?.hands(video, left, right, points, snapped),
                onZones: (levels, hits, down, video) =>
                {
                    for (let z = 0; z < cells.length; z++)
                        cells[z].style.opacity = String(0.15 + 0.85 * Math.min(1, levels[z] * 2));
                    view?.frame(video, levels, hits, down);
                },
                onFailed: (why) =>
                {
                    if (open === null)
                        failed = why;
                    else
                        shut(why);
                },
            });
        }
        catch (e)
        {
            status.textContent = e.message;
            button.disabled = false;
            return;
        }

        /* Where the click left it, which disabling it took away, unless the
           player has gone on to something else while the camera was asked
           for: so a view raised now goes beside the pane played in. */
        button.disabled = false;
        if (document.activeElement === document.body)
            button.focus({ preventScroll: true });
        if (failed !== null)
        {
            shut(failed);
            return;
        }
        /* Changed while it was asked for, when there was nothing yet to
           open afresh. */
        if ((mode?.value ?? 'harp') !== playing || (snap?.checked ?? true) !== snapped)
        {
            shut();
            button.click();
            return;
        }
        if (mine[device()] !== startedWith)
            open.profile(mine[device()]);

        button.textContent = `${label}: on`;
        if (playing === 'theremin')
            status.textContent = `pinch your left hand to play; raise your right ` +
                                 `for pitch, your left for volume${hint()}`;
        else
        {
            status.textContent = 'strike the zones';
            zones.hidden = false;
        }
        view?.open();
    });

    /* Shown for the theremin alone, at the start too: a browser may give
       the menu back as it was left, which no change says. */
    const offer = () =>
    {
        if (snap)
            snap.closest('label').hidden = mode.value !== 'theremin';
    };

    offer();
    for (const control of [mode, snap])
        control?.addEventListener('change', () =>
        {
            offer();
            shown();
            if (open !== null)
            {
                shut();
                button.click();
            }
        });

    return { forget: () => open?.forget() };
}

/* Asks for a camera and plays it. `onNoteOn(note, velocity)' and
 * `onNoteOff(note)' are called once per note, `onZones(levels, hits, down,
 * video)' for every frame with the nine zones' levels, the zones it struck
 * as [zone, strike], a bit for each zone that can't strike again till it
 * is still (twcamera.cpp) and the camera's <video>, and `onFailed(why)' if
 * the camera or the worker goes for good, after every note has been let go.
 *
 * `mode' 'theremin' plays the hands handworker.js finds instead of the
 * zones, snapped to a scale if `snap': `onBend(note, semitones)' and
 * `onAux(note, which, value)' are then each note's bend and aux, and
 * `onHands(left, right, video, points)' replaces onZones, each hand
 * [x, y, close, pinched] as the player sees it or null, and `points' the
 * tracker's own, as handworker.js packs them.
 *
 * `profile' is the player's (thinksynth:hid:camera, or :hands for the
 * theremin), over the camera's own: `onProblem(why)' says what of it
 * doesn't fit, or '', at the start and as it changes, and
 * `onValues(values)' each frame what the controls read, for the
 * configurator's form (profileform.js).
 *
 * Resolves to { forget, close, profile }. `forget' drops the notes held
 * without calling onNoteOff, for a page that has just released everything
 * itself. `close' lets go of everything held and lets the camera go.
 * `profile(text)' plays the player's profile as it is now.
 *
 * Rejects with a message fit to put on the page.
 */
export async function openCamera ({ onNoteOn, onNoteOff, onZones = () => {},
                                    onFailed = () => {}, mode = 'harp', snap = true,
                                    onBend = () => {}, onAux = () => {},
                                    onHands = () => {}, profile = '',
                                    onProblem = () => {}, onValues = () => {} })
{
    let stream;

    try
    {
        stream = await navigator.mediaDevices.getUserMedia({
            audio: false,
            video: { width: { ideal: 640 }, height: { ideal: 480 },
                     frameRate: { ideal: 120 } },
        });
    }
    catch (e)
    {
        if (e.name === 'NotAllowedError' || e.name === 'SecurityError')
            throw new Error('camera access was refused');
        if (e.name === 'NotFoundError' || e.name === 'OverconstrainedError')
            throw new Error('no camera');
        if (e.name === 'NotReadableError')
            throw new Error('the camera is in use');
        throw new Error(`the camera: ${e.message}`);
    }

    let bytes, worker;

    try
    {
        bytes = (await wasmBytes()).slice(0);
        worker = new Worker(new URL('camworker.js', import.meta.url),
                            { type: 'module' });
    }
    catch (e)
    {
        for (const t of stream.getTracks())
            t.stop();
        throw new Error(`the camera's module: ${e.message}`);
    }

    const [track] = stream.getVideoTracks();
    const video = document.createElement('video');

    video.muted = true;
    video.playsInline = true;
    video.srcObject = stream;

    /* The theremin's tracker, which answers one frame at a time: the next
       frame is made only once it has answered, so none queue behind it. */
    const tracker = mode === 'theremin'
        ? new Worker(new URL('handworker.js', import.meta.url), { type: 'module' })
        : null;
    let ready = false, tracking = false, asked = 0, unanswered = 0;
    let points = new Float32Array(0);
    const stallMs = tracker === null ? STALL_MS : TRACKER_STALL_MS;

    const held = new Set();
    let open = true, last = performance.now(), stalled = false, frame = 0;
    /* Advanced by forget: a reply to a frame posted before it would press
       a note the page has already let go of. */
    let generation = 0;
    let canvas = null, draw = null, watch = null;

    const letGo = () =>
    {
        for (const note of [...held])
        {
            held.delete(note);
            onNoteOff(note);
        }
    };

    const close = () =>
    {
        if (!open)
            return;

        open = false;
        clearInterval(watch);
        video.cancelVideoFrameCallback(frame);
        track.onended = null;
        for (const t of stream.getTracks())
            t.stop();
        worker.terminate();
        tracker?.terminate();
        letGo();
    };

    const fail = (why) =>
    {
        close();
        onFailed(why);
    };

    worker.onmessage = (e) =>
    {
        const m = e.data;

        if (m.type === 'failed')
        {
            fail(m.text);
            return;
        }
        if (m.type === 'problem')
        {
            onProblem(m.text);
            return;
        }
        if (!open || m.generation !== generation)
            return;

        onValues(m.values);
        if ('problem' in m)
            onProblem(m.problem);

        for (const [note, velocity] of m.notes)
            if (velocity > 0 && !held.has(note))
            {
                held.add(note);
                onNoteOn(note, velocity);
            }
            else if (velocity === 0 && held.delete(note))
                onNoteOff(note);

        if (tracker === null)
        {
            onZones(m.zones, m.hits, m.down, video);
            return;
        }
        for (const [note, semitones] of m.bends)
            onBend(note, semitones);
        for (const [note, which, value] of m.aux)
            onAux(note, which, value);
        onHands(m.left, m.right, video, points);
    };
    worker.onerror = (e) => fail(`the camera's worker: ${e.message ?? 'failed'}`);

    worker.postMessage({ type: 'start', bytes, mode, snap, profile }, [bytes]);

    if (tracker !== null)
    {
        tracker.onmessage = (e) =>
        {
            const m = e.data;

            if (m.type === 'failed')
                fail(m.text);
            else if (m.type === 'ready')
                ready = true;
            else if (m.type === 'hands')
            {
                /* Free for the next frame only once the last one sent is
                   answered: an answer to one given up on is still hands. */
                if (m.id === asked)
                    tracking = false;
                unanswered = 0;
                if (!open || m.generation !== generation)
                    return;

                /* Answered, which is what the stall watch waits for here:
                   a tracker that stops answering lets the note go, as a
                   camera that stops sending does. */
                last = performance.now();
                stalled = false;
                points = new Float32Array(m.points.slice(0));
                worker.postMessage({ type: 'hands', generation, points: m.points,
                                     count: m.count, seconds: m.seconds }, [m.points]);
            }
        };
        tracker.onerror = (e) => fail(`the hand tracker: ${e.message ?? 'failed'}`);
        tracker.postMessage({ type: 'start' });
    }

    /* Sized from the video, and again whenever its size changes: a phone
       turned on its side. */
    const sized = () =>
    {
        const height = Math.round(WIDTH * video.videoHeight / video.videoWidth);

        if (!(height >= 3))
            return false;
        if (canvas === null)
        {
            canvas = document.createElement('canvas');
            draw = canvas.getContext('2d', { willReadFrequently: true });
        }
        if (canvas.width !== WIDTH || canvas.height !== height)
        {
            canvas.width = WIDTH;
            canvas.height = height;
        }
        return true;
    };

    const onFrame = (now, meta) =>
    {
        if (!open)
            return;

        try
        {
            if (tracker !== null)
                toTracker(now);
            else if (sized())
                post(now, meta);
        }
        catch (e)
        {
            fail(`the camera: ${e.message}`);
            return;
        }
        frame = video.requestVideoFrameCallback(onFrame);
    };

    /* One clock for the tracker's times, the page's: a capture time is the
       camera's and isn't always there. */
    const toTracker = (now) =>
    {
        if (!ready || tracking)
            return;

        tracking = true;

        const taken = generation;

        createImageBitmap(video).then((bitmap) =>
        {
            if (open)
                tracker.postMessage({ type: 'frame', bitmap, seconds: now / 1000,
                                      generation: taken, id: ++asked }, [bitmap]);
            else
                bitmap.close();
        }, () => { tracking = false; });
    };

    const post = (now, meta) =>
    {
        const { width, height } = canvas;

        draw.drawImage(video, 0, 0, width, height);

        const rgba = draw.getImageData(0, 0, width, height).data;
        const luma = new Uint8Array(width * height);

        for (let i = 0, p = 0; i < luma.length; i++, p += 4)
            luma[i] = (77 * rgba[p] + 150 * rgba[p + 1] + 29 * rgba[p + 2]) >> 8;

        last = performance.now();
        stalled = false;
        worker.postMessage({ type: 'frame', generation, luma: luma.buffer,
                             width, height, seconds: (meta.captureTime ?? now) / 1000 },
                           [luma.buffer]);
    };

    watch = setInterval(() =>
    {
        if (performance.now() - last <= stallMs)
            return;

        if (!stalled)
        {
            stalled = true;
            worker.postMessage({ type: 'gone', generation });
        }

        /* And a frame the tracker hasn't answered is given up on, each time,
           or none would be sent again; but not for ever, each one waiting
           in a tracker that is never coming back. */
        if (tracking && ++unanswered >= TRACKER_UNANSWERED)
        {
            fail('the hand tracker stopped answering');
            return;
        }
        tracking = false;
    }, stallMs / 2);

    track.onended = () => fail('the camera went away');

    try
    {
        await video.play();
    }
    catch (e)
    {
        /* Already closed, the worker's failure is the one to tell. */
        if (open)
        {
            close();
            throw new Error(`the camera would not play: ${e.message}`);
        }
    }

    frame = video.requestVideoFrameCallback(onFrame);

    return {
        forget ()
        {
            held.clear();
            worker.postMessage({ type: 'gone', generation: ++generation });
        },

        close,

        profile: (text) => worker.postMessage({ type: 'profile', text, generation }),
    };
}
