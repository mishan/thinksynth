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
 * main.js -- the page.
 *
 * Two things to play. A patch is one .dsp, which is M1. A piece is a .gen:
 * the scheduler in the worklet composes it, the knobs it declared are
 * sliders, and what it delivers comes back as the tape and is drawn on a
 * roll. That is M2.
 *
 * They are modes and not two panels side by side, because a piece takes the
 * channels it asks for and the first of those is channel 0, where the
 * keyboard's patch was -- thinkweb.cpp, tw_piece_load, says why that is
 * deliberate. In piece mode the keyboard still plays: into the piece,
 * through `input midi', which is the path a peer's keyboard will take. It
 * arrives on a channel the page picks, because that is what a chain's
 * `input midi' is matched against, and a patch can be put on that channel
 * for pieces that route to one without declaring an instrument for it.
 *
 * TWO KINDS OF FINGER, ONE PATH. There is an on-screen keyboard
 * (keyboard.js) and there is the computer keyboard, and both go through
 * press() and release() below rather than reaching the synth themselves.
 * That is not tidiness: a note pressed on screen and then also from the
 * keys is one note, and a note released has to be released the way it was
 * pressed -- in piece mode, on the channel it arrived on, even if the
 * selector has moved since. So what is held is a map with a count and the
 * route each note went out by, and it is also what paints the keys.
 *
 * The computer keyboard's two rows are laid out by position rather than by
 * letter, so a non-QWERTY keyboard plays the same shape: Z to / is an
 * octave and a bit from C, Q to P the octave above, with the black keys on
 * the row above each. - and = move both down and up an octave, and move
 * the on-screen keyboard with them.
 */

import { createSynth } from './host.js';
import { Keyboard, noteName } from './keyboard.js';

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

/* How much of the piece the roll shows, in seconds, and the pitches it has
   room for. Notes older than this scroll off the left. */
const ROLL_SECONDS = 30;
const ROLL_LOW = 24, ROLL_HIGH = 108;

/* One colour per channel, so a piece's instruments are told apart. */
const CHANNEL_COLOURS = [
    '#e05c4a', '#e0a13c', '#c9c93a', '#6fbf4a', '#3fb8a0', '#3f96d0',
    '#5a6fd8', '#8f5ad8', '#cf4fb0', '#d9607a', '#9a8f6a', '#6a9a8f',
    '#8a8a8a', '#c07a3a', '#4a8ac0', '#a0a04a',
];

let ctx = null;
let synth = null;
let keyboard = null;
let octave = 48;                 /* MIDI note of the Z key, and of the
                                    leftmost key on screen: C3 */

const typed = new Map();         /* key code -> the note it pressed */

/* note -> { count, piece, channel }: how many fingers are on it, and the
   route it went out by, which is the route its release has to take. */
const sounding = new Map();

/* The piece, as it stands: what the worklet said when it loaded, and the
   tape it has delivered since. */
let piece = null;
let epoch = -1;
let now = 0;
let running = false;
let notes = [];

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

function mode ()
{
    return $('mode').value;
}

function keyChannel ()
{
    return Number($('keychan').value);
}

/* ---- what is sounding ---- */

/* Every way of pressing a note comes here. A second press of a note
   already down is counted and nothing else: one note, however many things
   are holding it. */
function press (note)
{
    if (synth === null)
        return;

    const already = sounding.get(note);

    if (already !== undefined)
    {
        already.count++;
        return;
    }

    const piecing = mode() === 'piece';
    const channel = keyChannel();

    sounding.set(note, { count: 1, piece: piecing, channel });

    if (piecing)
        synth.midiOn(note, VELOCITY, -1, channel);
    else
        synth.noteOn(note, VELOCITY);

    keyboard?.hold(note, true);
}

/* And every way of letting go. The route is the one the press took, not
   the one the page is in now: a mode or a channel changed under a held
   note must not leave it sounding for ever. */
function release (note)
{
    const held = sounding.get(note);

    if (held === undefined || --held.count > 0)
        return;

    sounding.delete(note);

    if (held.piece)
        synth.midiOff(note, -1, held.channel);
    else
        synth.noteOff(note);

    keyboard?.hold(note, false);
}

/* Everything, whoever is holding it: a key released while the page was not
   looking never sends its keyup, and a mode change is about to make the
   routes wrong. */
function releaseAll ()
{
    keyboard?.releaseAll();
    typed.clear();

    for (const [note, held] of [...sounding])
    {
        held.count = 1;
        release(note);
    }
}

/* ---- the keyboard, on screen and off ---- */

function showRange ()
{
    if (keyboard === null)
        return;

    const [low, high] = keyboard.range;

    $('range').textContent = `${noteName(low)} – ${noteName(high)}`;
}

function shiftOctave (by)
{
    octave = Math.min(96, Math.max(12, octave + by * 12));

    releaseAll();
    keyboard?.setLowest(octave);
    showRange();
    showLatency();
}

/* Typing in a text box is editing, not playing -- and before Start there is
   nothing to play into, so a key is only ever typing then. */
function typing (e)
{
    return synth === null || e.ctrlKey || e.metaKey || e.altKey ||
           (e.target instanceof Element &&
            e.target.closest('textarea, select, input') !== null);
}

function keyDown (e)
{
    if (typing(e))
        return;

    if (e.code === 'Minus' || e.code === 'Equal')
    {
        shiftOctave(e.code === 'Equal' ? 1 : -1);
        e.preventDefault();
        return;
    }

    if (!(e.code in KEYS))
        return;

    e.preventDefault();

    if (e.repeat || typed.has(e.code))
        return;

    const note = octave + KEYS[e.code];

    typed.set(e.code, note);
    press(note);
}

function keyUp (e)
{
    const note = typed.get(e.code);

    if (note === undefined)
        return;

    typed.delete(e.code);
    release(note);
}

/* ---- what the browser admits to ---- */

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

/* ---- the patch, M1 ---- */

async function loadPatch ()
{
    if (synth === null)
        return;

    const ok = await quietly(() => synth.load($('dsp').value));

    $('status').textContent = ok ? `Loaded ${$('patch').value}. Play.`
                                 : 'That .dsp did not parse; see below.';

    if (!ok)
        $('detail').open = true;
}

/* The same, onto the channel the keys are aimed at rather than onto 0. */
async function loadKeyPatch ()
{
    if (synth === null)
        return;

    const name = $('keypatch').value;
    const text = await (await fetch(`dsp/${name}`)).text();
    const channel = keyChannel();
    const ok = await quietly(() => synth.load(text, channel));

    $('status').textContent = ok ? `${name} on channel ${channel}. Play.`
                                 : `${name} did not parse; see below.`;

    if (!ok)
        $('detail').open = true;
}

async function pickPatch ()
{
    $('dsp').value = await (await fetch(`dsp/${$('patch').value}`)).text();

    await loadPatch();
}

/* ---- the piece, M2 ---- */

async function loadPiece ()
{
    if (synth === null)
        return;

    const it = await quietly(() => synth.loadPiece($('gen').value));

    notes = [];
    piece = it.errors.length === 0 ? it : null;

    if (piece === null)
    {
        $('status').textContent = 'That .gen did not parse; see below.';
        it.errors.forEach(log);
        $('detail').open = true;
    }
    else
        $('status').textContent =
            `Loaded ${piece.name || $('piece').value}. Press Play.`;

    $('about').textContent = piece === null ? '' : piece.description;

    for (const id of ['play', 'stop', 'rewind'])
        $(id).disabled = piece === null;

    showKnobs();
    draw();
}

async function pickPiece ()
{
    $('gen').value = await (await fetch(`gen/${$('piece').value}`)).text();

    await loadPiece();
}

/* A load builds graphs on the audio thread, between two quanta, and a big
   one could run past the next quantum's deadline. Suspended around it, the
   gap is a clean one rather than a glitch.
 *
 * One at a time. A second load arriving while the first holds the context
 * suspended would find it already suspended, do nothing about it, and
 * have the first one's resume land in the middle of its own graph build
 * -- so each waits for the one before. */
let quiet = Promise.resolve();

function quietly (what)
{
    const run = quiet.then(async () =>
    {
        const wasRunning = ctx.state === 'running';

        if (wasRunning)
            await ctx.suspend();

        const r = await what();

        if (wasRunning)
            await ctx.resume();

        return r;
    });

    quiet = run.catch(() => {});

    return run;
}

/* One row per knob the piece declared, each bound straight to the command
   that moves it. The command carries a frame like every other, so the page
   is the nearest peer and not a privileged one (JAM.md, section 3). */
function showKnobs ()
{
    const box = $('knobs');

    box.replaceChildren();

    for (const k of piece?.knobs ?? [])
    {
        const label = document.createElement('label');
        const input = document.createElement('input');
        const shown = document.createElement('span');

        label.textContent = k.label || k.name;
        label.htmlFor = `knob-${k.name}`;

        input.id = `knob-${k.name}`;
        input.type = 'range';
        input.min = k.min;
        input.max = k.max;
        input.step = k.step > 0 ? k.step : (k.max - k.min) / 1000;
        input.value = k.value;

        shown.className = 'value';
        shown.textContent = Number(k.value).toPrecision(3);

        input.addEventListener('input', () =>
        {
            shown.textContent = Number(input.value).toPrecision(3);
            synth.knob(k.knob, Number(input.value));
        });

        /* Label, value, slider: the order a narrow screen wants, where
           the slider takes a line of its own under the two of them. A wide
           one puts the slider between them, which style.css does by
           placing it in the middle column rather than by a second
           ordering here. */
        box.append(label, shown, input);
    }
}

/* The tape, as it arrives. Only notes are drawn -- a chanarg write moves a
   filter and has nothing to put on a roll -- and only the last
   ROLL_SECONDS of them are kept. */
function tape (m)
{
    now = m.now;
    running = m.running;

    /* A load or a rewind: `at' starts again from zero and everything drawn
       so far is about a piece that is no longer running. */
    if (m.epoch !== epoch)
    {
        epoch = m.epoch;
        notes = [];
    }

    for (const e of m.events)
        if (e.kind === 'N')
            notes.push(e);

    /* Whatever has ended before the left edge, wherever it sits: a long
       note at the front must not keep everything after it alive. */
    const first = now - ROLL_SECONDS;

    if (notes.some((e) => e.at + e.duration < first))
        notes = notes.filter((e) => e.at + e.duration >= first);
}

function draw ()
{
    const c = $('roll');
    const g = c.getContext('2d');
    const w = c.width, h = c.height;

    g.clearRect(0, 0, w, h);

    /* The last ROLL_SECONDS, with now at the right edge. */
    const first = now - ROLL_SECONDS;
    const x = (t) => (t - first) / ROLL_SECONDS * w;
    const y = (n) => h - (n - ROLL_LOW) / (ROLL_HIGH - ROLL_LOW) * h;

    g.strokeStyle = getComputedStyle(c).getPropertyValue('--line') || '#ddd';
    g.lineWidth = 1;

    for (let n = ROLL_LOW; n <= ROLL_HIGH; n += 12)
    {
        g.beginPath();
        g.moveTo(0, Math.round(y(n)) + 0.5);
        g.lineTo(w, Math.round(y(n)) + 0.5);
        g.stroke();
    }

    const tall = Math.max(2, h / (ROLL_HIGH - ROLL_LOW));

    for (const e of notes)
    {
        const left = x(e.at);
        const wide = Math.max(2, (e.duration || 0.05) / ROLL_SECONDS * w);

        g.globalAlpha = 0.25 + 0.75 * Math.min(1, e.velocity / 110);
        g.fillStyle = CHANNEL_COLOURS[e.channel & 15];
        g.fillRect(left, y(e.note) - tall / 2, wide, tall);
    }

    g.globalAlpha = 1;

    const secs = Math.max(0, now);

    $('clock').textContent =
        `${Math.floor(secs / 60)}:` +
        `${(secs % 60).toFixed(1).padStart(4, '0')}` +
        (running ? '' : ' (stopped)');
}

function frame ()
{
    if (mode() === 'piece')
        draw();

    requestAnimationFrame(frame);
}

/* ---- starting, and switching ---- */

async function start ()
{
    $('start').disabled = true;
    $('status').textContent = 'Starting...';

    try
    {
        ctx = new AudioContext({ latencyHint: 'interactive' });
        synth = await createSynth(ctx, { windowlen: 256, onLog: log,
                                         onTape: tape });
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

    /* The instruments a piece may name, before any piece asks for one: a
       worklet has no file system of its own and cannot fetch. All at once,
       since nothing here waits on anything else. */
    const texts = await Promise.all(
        dspNames.map((name) => fetch(`dsp/${name}`).then((r) => r.text())));

    dspNames.forEach((name, i) => synth.instrument(name, texts[i]));

    $('load').disabled = false;
    $('loadpiece').disabled = false;
    $('loadkeypatch').disabled = false;

    if (mode() === 'patch')
        await loadPatch();
    else
        await loadPiece();

    showLatency();
    setInterval(showLatency, 500);
}

async function pickMode ()
{
    const piecing = mode() === 'piece';

    $('patchmode').hidden = piecing;
    $('patchsource').hidden = piecing;
    $('piecemode').hidden = !piecing;
    $('piecesource').hidden = !piecing;

    if (synth === null)
        return;

    releaseAll();

    /* Each mode loads what it plays as it is entered: the other one's is
       still on the channels until it does. */
    if (piecing)
        await loadPiece();
    else
    {
        synth.transport('stop');
        piece = null;
        await loadPatch();
    }
}

/* The shipped .dsp files, as start() hands them to the worklet. */
let dspNames = [];

function fill (select, names, preferred)
{
    for (const name of names)
        select.add(new Option(name, name, name === preferred,
                              name === preferred));
}

async function init ()
{
    const [dsps, gens] = await Promise.all(
        ['dsp', 'gen'].map((d) => fetch(`${d}/index.json`)
                                      .then((r) => r.json())));

    dspNames = dsps;
    fill($('patch'), dsps, 'ts1.dsp');
    fill($('piece'), gens, 'ebb.gen');
    fill($('keypatch'), dsps, 'rpiano0.dsp');

    [$('dsp').value, $('gen').value] = await Promise.all([
        fetch(`dsp/${$('patch').value}`).then((r) => r.text()),
        fetch(`gen/${$('piece').value}`).then((r) => r.text()),
    ]);

    /* Sixteen is all there are. The engine's numbering, which a .gen
       file's is not: a file writes `channel = 1' for the first one and the
       loader hands over 0, and it is the loader's number that both a
       chain's `input midi' and this are matched against. So 0 is where a
       piece's first instrument lands, and where hands.gen's arpeggiator
       listens. */
    for (let c = 0; c < 16; c++)
        $('keychan').add(new Option(String(c), c, c === 0, c === 0));

    keyboard = new Keyboard($('keys'),
                            { onPress: press, onRelease: release });
    keyboard.setLowest(octave);
    keyboard.fit();
    showRange();

    $('start').addEventListener('click', start);
    $('mode').addEventListener('change', pickMode);

    $('load').addEventListener('click', loadPatch);
    $('patch').addEventListener('change', pickPatch);

    $('loadpiece').addEventListener('click', loadPiece);
    $('piece').addEventListener('change', pickPiece);
    $('loadkeypatch').addEventListener('click', loadKeyPatch);

    $('play').addEventListener('click', () => synth.transport('start'));
    $('stop').addEventListener('click', () => synth.transport('stop'));
    $('rewind').addEventListener('click', () => synth.transport('rewind'));

    $('down').addEventListener('click', () => shiftOctave(-1));
    $('up').addEventListener('click', () => shiftOctave(1));

    window.addEventListener('keydown', keyDown);
    window.addEventListener('keyup', keyUp);
    window.addEventListener('blur', releaseAll);

    /* A screen with room for the source next to the keys opens it; one
       without keeps it folded, since on a phone it is most of the page.
       Asked once, at load: this is a starting point and not a rule, and
       the fold is the reader's from here on. */
    if (matchMedia('(min-width: 60em)').matches)
        $('patchsource').open = $('piecesource').open = true;

    requestAnimationFrame(frame);
}

init();
