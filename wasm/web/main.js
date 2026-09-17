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
 * the on-screen keyboard with them. That layout, and the sliders a piece's
 * knobs are drawn as, are keyboard.js's and knobs.js's: the room page
 * wants them identical, and two copies of a thing two pages have to agree
 * on is how they stop agreeing.
 */

import { createSynth } from './host.js';
import { Keyboard, TypingKeys, noteName, showRange } from './keyboard.js';
import { showKnobs } from './knobs.js';
import { Roll } from './roll.js';

const $ = (id) => document.getElementById(id);

const VELOCITY = 100;

let ctx = null;
let synth = null;
let keyboard = null;
let keys = null;                 /* the computer keyboard as a musical one */

/* note -> { count, piece, channel }: how many fingers are on it, and the
   route it went out by, which is the route its release has to take. */
const sounding = new Map();

/* The piece, as it stands: what the worklet said when it loaded, and the
   roll of what it has delivered since. */
let piece = null;
let roll = null;

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
    keys?.forget();

    for (const [note, held] of [...sounding])
    {
        held.count = 1;
        release(note);
    }
}

/* ---- the keyboard, on screen and off ---- */

/* What the octave keys and the two buttons do: let go of everything, move
   the keys under the hands, and say where they are now and what they will
   cost. */
function shifted (lowest)
{
    releaseAll();
    keyboard?.setLowest(lowest);
    showRange($('range'), keyboard);
    showLatency();
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
        `octave           Z = ${noteName(keys.lowest)}`;
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

    roll.clear();
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

    drawKnobs();
    roll.draw();
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
function drawKnobs ()
{
    showKnobs($('knobs'), piece?.knobs ?? [],
              (knob, value) => synth.knob(knob, value));
}

function frame ()
{
    if (mode() === 'piece')
        roll.draw();

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
                                         onTape: (m) => roll.tape(m) });
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

    roll = new Roll($('roll'), $('clock'));
    keyboard = new Keyboard($('keys'),
                            { onPress: press, onRelease: release });
    keys = new TypingKeys({ press, release, shifted,
                            playable: () => synth !== null });
    keyboard.setLowest(keys.lowest);
    keyboard.fit();
    showRange($('range'), keyboard);

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

    $('down').addEventListener('click', () => keys.shift(-1));
    $('up').addEventListener('click', () => keys.shift(1));

    window.addEventListener('keydown', (e) => keys.keyDown(e));
    window.addEventListener('keyup', (e) => keys.keyUp(e));
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
