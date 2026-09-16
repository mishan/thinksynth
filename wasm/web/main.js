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
 * main.js -- the page: one .dsp in a text box, the computer keyboard as the
 * keyboard, and the latency the browser admits to.
 *
 * Two rows of keys, laid out by position rather than by letter so a
 * non-QWERTY keyboard plays the same shape: Z to / is an octave and a bit
 * from C, Q to P the octave above, with the black keys on the row above
 * each. - and = move both down and up an octave.
 */

import { createSynth } from './host.js';

const $ = (id) => document.getElementById(id);

const KEYS = {
    KeyZ: 0, KeyS: 1, KeyX: 2, KeyD: 3, KeyC: 4, KeyV: 5, KeyG: 6, KeyB: 7,
    KeyH: 8, KeyN: 9, KeyJ: 10, KeyM: 11, Comma: 12, KeyL: 13, Period: 14,
    Semicolon: 15, Slash: 16,
    KeyQ: 12, Digit2: 13, KeyW: 14, Digit3: 15, KeyE: 16, KeyR: 17,
    Digit5: 18, KeyT: 19, Digit6: 20, KeyY: 21, Digit7: 22, KeyU: 23,
    KeyI: 24, Digit9: 25, KeyO: 26, Digit0: 27, KeyP: 28,
};

const VELOCITY = 100;

let ctx = null;
let synth = null;
let octave = 48;                 /* MIDI note of the Z key: C3 */
const held = new Map();          /* key code -> the note it pressed */

function log (text)
{
    $('log').textContent += text + '\n';
    $('log').scrollTop = $('log').scrollHeight;
}

function ms (seconds)
{
    return seconds === undefined ? 'not reported'
                                 : `${(seconds * 1000).toFixed(1)} ms`;
}

/* What the browser says it adds, and what the synth adds: a window, and
   the quantum the worklet is asked for. The network, when there is one,
   comes on top (JAM.md, section 2). */
function showLatency ()
{
    if (ctx === null)
        return;

    const rate = ctx.sampleRate;

    $('latency').textContent =
        `sample rate      ${rate} Hz\n` +
        `base latency     ${ms(ctx.baseLatency)}\n` +
        `output latency   ${ms(ctx.outputLatency)}\n` +
        `synth window     ${synth.windowlen} frames, ` +
        `${ms(synth.windowlen / rate)}\n` +
        `worklet quantum  128 frames, ${ms(128 / rate)}\n` +
        `octave           Z = ${noteName(octave)}`;
}

function noteName (n)
{
    const names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A',
                   'A#', 'B'];

    return names[n % 12] + (Math.floor(n / 12) - 1);
}

async function loadPatch ()
{
    if (synth === null)
        return;

    /* The graph is built on the audio thread, between two quanta, and a
       big one could run past the next quantum's deadline. Suspended around
       the load, the gap a reload makes is a clean one rather than a
       glitch. */
    const wasRunning = ctx.state === 'running';

    if (wasRunning)
        await ctx.suspend();

    const ok = await synth.load($('dsp').value);

    if (wasRunning)
        await ctx.resume();

    $('status').textContent = ok ? `Loaded ${$('patch').value}. Play.`
                                 : 'That .dsp did not parse; see below.';
}

async function start ()
{
    $('start').disabled = true;
    $('status').textContent = 'Starting...';

    try
    {
        ctx = new AudioContext({ latencyHint: 'interactive' });
        synth = await createSynth(ctx, { windowlen: 256, onLog: log });
        synth.node.connect(ctx.destination);
        await ctx.resume();
    }
    catch (e)
    {
        /* A context that got this far holds the audio device and perhaps a
           worklet, and a browser allows only so many contexts; it goes
           before a retry makes another. */
        if (ctx !== null)
            ctx.close().catch(() => {});

        ctx = null;
        synth = null;

        $('status').textContent = `Could not start: ${e.message}`;
        $('start').disabled = false;
        return;
    }

    $('load').disabled = false;
    await loadPatch();

    showLatency();
    setInterval(showLatency, 500);
}

async function pickPatch ()
{
    const r = await fetch(`dsp/${$('patch').value}`);

    $('dsp').value = await r.text();

    await loadPatch();
}

/* Typing in the text box is editing, not playing. */
function playing (e)
{
    return synth !== null && !e.target.closest('textarea, select, input') &&
           !e.ctrlKey && !e.metaKey && !e.altKey;
}

function keyDown (e)
{
    if (!playing(e))
        return;

    if (e.code === 'Minus' || e.code === 'Equal')
    {
        octave = Math.min(96, Math.max(12, octave +
                                       (e.code === 'Equal' ? 12 : -12)));
        showLatency();
        e.preventDefault();
        return;
    }

    if (!(e.code in KEYS))
        return;

    e.preventDefault();

    if (e.repeat || held.has(e.code))
        return;

    const note = octave + KEYS[e.code];

    held.set(e.code, note);
    synth.noteOn(note, VELOCITY);
}

function keyUp (e)
{
    const note = held.get(e.code);

    if (note === undefined)
        return;

    held.delete(e.code);
    synth.noteOff(note);
}

/* A key released while the page was not looking never sends its keyup. */
function releaseAll ()
{
    if (synth === null)
        return;

    for (const note of held.values())
        synth.noteOff(note);

    held.clear();
}

async function init ()
{
    const names = await (await fetch('dsp/index.json')).json();

    for (const name of names)
        $('patch').add(new Option(name, name, name === 'ts1.dsp',
                                  name === 'ts1.dsp'));

    const r = await fetch(`dsp/${$('patch').value}`);

    $('dsp').value = await r.text();

    $('start').addEventListener('click', start);
    $('load').addEventListener('click', loadPatch);
    $('patch').addEventListener('change', pickPatch);

    window.addEventListener('keydown', keyDown);
    window.addEventListener('keyup', keyUp);
    window.addEventListener('blur', releaseAll);
}

init();
