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
 * videoview.js -- the picture, drawn live beside the sound.
 *
 * A video patch from video/ draws every note heard -- the piece's, the
 * keys', MIDI's, the camera's -- in a worker of its own (videoworker.js),
 * from what the worklet tells it on a port that skips this thread. This
 * file starts and stops that, chooses the patch and its effect, and paints
 * each finished frame when the sound it goes with is heard: the audio
 * clock (clock.js) says which frame that is at the screen's next refresh.
 *
 * It runs only while its pane is in front of somebody (panes.js's onShow,
 * which a hidden tab is not): a hidden tab gets no animation frames to
 * paint with, and a picture nobody sees is a core spent for nothing. Shown
 * again, it starts from now, with the notes already down.
 */

import { AudioClock } from './clock.js';
import { wasmBytes } from './host.js';

const WIDTH = 160, HEIGHT = 120, FPS = 60;

/* Lines a window: a note waits at most this long, 1280 pixels or about a
   millisecond, to start drawing. */
const LINES = 8;

/* How often the worker hears where the audio clock is. */
const CLOCK_EVERY_MS = 250;

/* How long "dropping frames" stays up after the last skip. */
const DROPPING_MS = 1000;

/* How many paints the lateness is kept for. */
const KEEP = 600;

/* Frames in the pool beyond those that wait out the output's latency. */
const SPARE = 4;

/* Drawn this many times smaller each way on a machine that cannot draw
   the full size in time. */
const SMALLER = 2;

/* The full size is tried again once the small one has, for this many
   seconds running (doubled for each try that fell behind, up to the
   most), dropped no frame and been busy little enough that the full
   size's SMALLER^2 pixels would fill under ROOMY of the worker's time. */
const PATIENCE_S = 4, MOST_PATIENCE_S = 30;
const ROOMY = 0.5;

/* A video .dsp's knobs: each `@name' it gives a widget, with the value,
   range, step and label the text declares. */
export function knobsOf (text)
{
    const knobs = new Map();

    for (const [, name, field, value] of
         text.matchAll(/^\s*@(\w+)(?:\.(\w+))?\s*=\s*("[^"]*"|[^;]*);/gm))
    {
        if (!knobs.has(name))
            knobs.set(name, { name, label: name, value: 0, min: 0, max: 1, step: 0,
                              widget: false });

        const k = knobs.get(name);

        if (field === undefined)
            k.value = Number(value);
        else if (field === 'label')
            k.label = value.replace(/^"|"$/g, '');
        else if (field === 'widget')
            k.widget = Number(value) !== 0;
        else if (field === 'min' || field === 'max' || field === 'step')
            k[field] = Number(value);
    }

    return [...knobs.values()].filter((k) => k.widget);
}

/* `canvas' is painted; `frame' is what goes fullscreen; `patch' and `fx'
 * are the two selects, `knobs' where the patch's sliders go, `status' a
 * line saying how it is going, `full' the fullscreen button. `delay' is
 * the MIDI delay in milliseconds a device's channel is heard by.
 *
 * Returns { attach(synth, ctx), show(on), stats() }.
 */
export function createVideoView ({ canvas, frame, patch, fx, knobs, status, full,
                                   delay = () => 0 })
{
    const g = canvas.getContext('2d');
    const texts = new Map();
    let synth = null, ctx = null, shown = false, failed = false;
    let run = null, starting = false;
    let listed = null, opens = 0, narrow = '';
    const late = [];
    let painted = 0;

    /* Compiled once for every worker this page starts. */
    let compiled = null;

    canvas.width = WIDTH;
    canvas.height = HEIGHT;

    /* Said once, not again each time: the line is read out as it changes. */
    const say = (text) =>
    {
        if (status.textContent !== text)
            status.textContent = text;
    };

    const text = async (name) =>
    {
        if (!texts.has(name))
        {
            const r = await fetch(`video/${name}`);

            if (!r.ok)
                throw new Error(`${name}: ${r.status}`);
            texts.set(name, await r.text());
        }
        return texts.get(name);
    };

    /* The patches and the effects, from video/index.json. */
    const list = () => listed ??= (async () =>
    {
        const r = await fetch('video/index.json');

        if (!r.ok)
            throw new Error(`the video patches: ${r.status}`);

        const names = await r.json();
        const option = (name) => new Option(name.replace(/^fx\//, '').replace(/\.dsp$/, ''), name);

        patch.replaceChildren(...names.filter((n) => !n.startsWith('fx/')).map(option));
        fx.replaceChildren(new Option('none', ''),
                           ...names.filter((n) => n.startsWith('fx/')).map(option));
        if (names.includes('moire.dsp'))
            patch.value = 'moire.dsp';
    })().catch((e) =>
    {
        listed = null;
        throw e;
    });

    /* The patch and its effect onto the worker, and their sliders: as they
       were set if `keep', else as the patch has them. */
    const open = async (r, keep = false) =>
    {
        const name = patch.value, effect = fx.value, mine = ++opens;
        const dsp = await text(name);
        const fxText = effect ? await text(effect) : '';

        if (run !== r || mine !== opens)
            return;

        /* A frame waits on the page until its sound is heard. */
        const latency = (ctx.outputLatency || 0) + (ctx.baseLatency || 0);

        /* A canvas resized is cleared, even to the size it was. */
        if (canvas.width !== WIDTH / r.scale)
        {
            r.frames.length = 0;
            canvas.width = WIDTH / r.scale;
            canvas.height = HEIGHT / r.scale;
        }
        narrow = r.scale > 1 ? `drawn at ${canvas.width}x${canvas.height}: this machine ` +
                               `cannot draw ${WIDTH}x${HEIGHT} in time` : '';
        say(narrow);
        r.worker.postMessage({ type: 'open', width: canvas.width, height: canvas.height,
                               fps: FPS, lines: LINES, rate: ctx.sampleRate,
                               pool: Math.ceil(latency * FPS) + SPARE,
                               patch: { name, text: dsp },
                               fx: effect ? { name: effect, text: fxText } : null });

        const set = new Map(keep ? [...knobs.querySelectorAll('input')]
                                       .map((i) => [i.dataset.knob, i.value]) : []);

        knobs.replaceChildren();
        for (const k of [...knobsOf(dsp), ...knobsOf(fxText)])
        {
            const label = document.createElement('label');
            const input = document.createElement('input');

            input.type = 'range';
            input.min = String(k.min);
            input.max = String(k.max);
            input.step = k.step > 0 ? String(k.step) : 'any';
            input.value = String(k.value);
            input.dataset.knob = k.name;
            input.addEventListener('input', () =>
                run?.worker.postMessage({ type: 'set', name: k.name,
                                          value: Number(input.value) }));
            label.append(k.label, ' ', input);
            knobs.append(label);
            if (set.has(k.name))
            {
                input.value = set.get(k.name);
                input.dispatchEvent(new Event('input'));
            }
        }
    };

    const end = () =>
    {
        if (run === null)
            return;

        synth?.video(false);
        run.worker.terminate();
        ctx.removeEventListener('statechange', run.restart);
        clearInterval(run.timer);
        cancelAnimationFrame(run.raf);
        run = null;
        g.clearRect(0, 0, canvas.width, canvas.height);
    };

    const fail = (why) =>
    {
        end();
        failed = true;
        say(why);
    };

    /* The newest finished frame the audio has reached at the next
       refresh, painted; the ones it passed over, and it, back to the
       worker's pool. */
    const paint = (now) =>
    {
        const r = run;

        if (r === null)
            return;
        r.raf = requestAnimationFrame(paint);

        const step = Math.min(34, Math.max(4, now - r.last));

        r.last = now;

        const heard = r.clock.frameAt(now + step);
        let pick = null;

        while (r.frames.length > 0 && r.frames[0].at <= heard)
        {
            if (pick !== null)
                r.worker.postMessage({ type: 'back', buffer: pick.buffer }, [pick.buffer]);
            pick = r.frames.shift();
        }
        if (pick !== null)
        {
            g.putImageData(new ImageData(new Uint8ClampedArray(pick.buffer), canvas.width,
                                         canvas.height), 0, 0);
            painted++;
            late.push(Math.floor((heard - pick.at) * FPS / ctx.sampleRate));
            if (late.length > KEEP)
                late.shift();
            r.worker.postMessage({ type: 'back', buffer: pick.buffer }, [pick.buffer]);
        }
        if (r.dropping > 0 && now - r.dropping > DROPPING_MS)
        {
            r.dropping = 0;
            say(narrow);
        }
    };

    const heard = (r, m) =>
    {
        if (run !== r)
            return;
        /* One drawn before a change of size is not this size's. */
        if (m.type === 'frame')
        {
            if (m.buffer.byteLength === 4 * canvas.width * canvas.height)
                r.frames.push(m);
        }
        else if (m.type === 'failed')
            fail(m.text);
        else if (m.type === 'narrow')
            say(narrow = [narrow, m.text].filter((t) => t !== '').join('; '));
        /* Behind twice in a second is a machine too slow for the size,
           not a hiccup. */
        else if (m.type === 'dropping' && r.scale === 1 && r.dropping > 0)
        {
            r.scale = SMALLER;
            r.dropping = 0;
            r.patience = Math.min(r.patience * 2 || PATIENCE_S, MOST_PATIENCE_S);
            r.roomy = 0;
            open(r, true).catch((e) => fail(`the video: ${e.message}`));
        }
        else if (m.type === 'dropping')
        {
            r.dropping = performance.now();
            /* A stall outside drawing is not in the busy fraction. */
            r.roomy = 0;
            say('dropping frames: this machine cannot draw it in time');
        }
        else if (m.type === 'busy' && r.scale > 1)
        {
            r.roomy = m.fraction * r.scale * r.scale < ROOMY ? r.roomy + 1 : 0;
            if (r.roomy >= r.patience)
            {
                r.scale = 1;
                r.roomy = 0;
                open(r, true).catch((e) => fail(`the video: ${e.message}`));
            }
        }
    };

    const begin = async () =>
    {
        let module, worker;

        starting = true;
        try
        {
            await list();
            compiled ??= wasmBytes().then((bytes) => WebAssembly.compile(bytes));
            module = await compiled.catch((e) =>
            {
                compiled = null;
                throw e;
            });
            worker = new Worker(new URL('videoworker.js', import.meta.url), { type: 'module' });
        }
        catch (e)
        {
            fail(`the video: ${e.message}`);
            return;
        }
        finally
        {
            starting = false;
        }

        if (run !== null || !wanted())
        {
            worker.terminate();
            return;
        }

        const r = { worker, clock: new AudioClock(ctx.sampleRate), frames: [], raf: 0,
                    timer: 0, last: performance.now(), dropping: 0, scale: 1,
                    patience: 0, roomy: 0 };
        const { port1, port2 } = new MessageChannel();
        const tick = () =>
        {
            const t = ctx.getOutputTimestamp();

            r.clock.sample(t.contextTime, t.performanceTime);
            if (r.clock.count > 0)
                worker.postMessage({ type: 'clock', offset: r.clock.offset,
                                     timeOrigin: performance.timeOrigin,
                                     rate: ctx.sampleRate });
        };

        /* A suspended context's time stands still -- a piece load suspends
           it -- and the clock's samples from before are from another line. */
        r.restart = () =>
        {
            r.clock = new AudioClock(ctx.sampleRate);
            tick();
        };
        ctx.addEventListener('statechange', r.restart);
        run = r;
        say('');
        worker.onmessage = (e) => heard(r, e.data);
        worker.onerror = (e) =>
        {
            if (run === r)
                fail(`the video's worker: ${e.message ?? 'failed'}`);
        };
        worker.postMessage({ type: 'start', module });
        worker.postMessage({ type: 'port', port: port2 }, [port2]);
        try
        {
            await open(r);
        }
        catch (e)
        {
            if (run === r)
                fail(`the video: ${e.message}`);
            return;
        }
        if (run !== r)
            return;

        synth.video(true, port1, delay());
        tick();
        r.timer = setInterval(tick, CLOCK_EVERY_MS);
        r.raf = requestAnimationFrame(paint);
    };

    const wanted = () => shown && !failed && synth !== null;

    const update = () =>
    {
        if (wanted() && run === null && !starting)
            begin();
        else if (!wanted())
            end();
    };

    const reopen = () =>
    {
        /* Another patch gets its chance at the full size. */
        if (run !== null)
        {
            run.scale = 1;
            run.dropping = 0;
            run.patience = 0;
            open(run).catch((e) => fail(`the video: ${e.message}`));
        }
        else if (failed && shown)
        {
            failed = false;
            update();
        }
    };

    patch.addEventListener('change', reopen);
    fx.addEventListener('change', reopen);
    full.hidden = !document.fullscreenEnabled;
    full.addEventListener('click', () =>
    {
        if (document.fullscreenElement === frame)
            document.exitFullscreen();
        else
            frame.requestFullscreen?.().catch((e) => say(`fullscreen: ${e.message}`));
    });
    list().catch((e) => say(e.message));

    return {
        /* A new synth, or none (null): the picture follows it, whatever
           happened to the last. */
        attach (s, c)
        {
            end();
            failed = false;
            synth = s;
            ctx = c;
            update();
        },

        /* The MIDI delay changed: the worklet stamps a device's notes by
           the new one. */
        delayed ()
        {
            if (run !== null && !starting)
                synth.video(true, null, delay());
        },

        show (on)
        {
            shown = on;
            if (!on)
                failed = false;
            update();
        },

        /* Whether it is running, its line, how many frames it has painted,
           and how late each of the last KEEP was, in frames (0: the frame
           being heard) -- what to read to measure it. */
        stats: () => ({ running: run !== null, painted, late: [...late],
                        status: status.textContent }),
    };
}
