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
 * jam.js -- the room page (JAM_M3.md, section 7).
 *
 * Several people, one piece, each browser rendering the whole of it. The
 * score crosses the network; the audio never does. What this page does
 * beyond the solo page's is join a room, share the piece's text through
 * the relay, connect to the other peers, agree on a clock, and turn every
 * input -- its own included -- into a command stamped with the transport
 * time it applies at, sent to everyone and applied here through the same
 * path the others apply it through. The page is the nearest peer and not
 * a privileged one.
 *
 * The pieces: room.js for the relay's room socket and the relay clock,
 * mesh.js for the data channels between peers, doc.js and editor.js for
 * the document, clock.js for the maps between the clocks, commands.js
 * for what crosses the wire, and host.js and roll.js as the solo page has
 * them. This file is the UI and the joins between them.
 */

import { WebsocketProvider } from 'y-websocket';
import * as Y from 'yjs';

import { AudioClock, TransportClock, frameOfRelayMs } from './clock.js';
import { Dedupe, KNOB_LEAD, Maker, TRANSPORT_LEAD, apply, isLate }
    from './commands.js';
import { hashOf, instrumentTexts, pieceName, pieceText } from './doc.js';
import { Editor, colourOf } from './editor.js';
import { createSynth } from './host.js';
import { Keyboard, noteName } from './keyboard.js';
import { Mesh } from './mesh.js';
import { Roll } from './roll.js';
import { Room } from './room.js';
import { tapeLine } from '../tape.mjs';

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

/* How many clock samples Play waits for (JAM_M3.md, section 7). */
const ENOUGH_SAMPLES = 4;

/* Where the relay is: the URL's `relay', then the build's config.json,
   then the page's own host on the relay's usual port. */
async function relayUrl (params)
{
    if (params.get('relay'))
        return params.get('relay');

    try
    {
        const cfg = await (await fetch('config.json')).json();

        if (cfg.relay)
            return cfg.relay;
    }
    catch
    {
        /* No config: the default below. */
    }

    return `ws://${location.hostname}:8787`;
}

/* ---- state ---- */

let room = null;
let mesh = null;
let doc = null;
let provider = null;
let editor = null;
let ctx = null;
let synth = null;
let audioClock = null;
let transport = null;
let roll = null;
let keyboard = null;
let maker = null;
const dedupe = new Dedupe();

let piece = null;               /* the worklet's word on the loaded piece */
let listens = new Set();        /* channels the piece takes input on */
let octave = 48;
const typed = new Map();
const sounding = new Map();     /* note -> { count, seat } */

const sent = [];                /* every command this peer made */
const late = [];                /* commands the page saw were late */
const margins = [];             /* how early each stamped command came */
let lateCount = 0;              /* the worklet's count */
let tapeText = '';              /* the tape since the last epoch, as text */
let tapeEpoch = -1;
let pending = null;             /* a start waiting for the document */

function log (text)
{
    $('log').textContent += text + '\n';
    $('log').scrollTop = $('log').scrollHeight;
}

function status (text)
{
    $('status').textContent = text;
}

/* ---- the clocks ---- */

function transportNow ()
{
    return synth === null ? -1
                          : transport.now(ctx.currentTime, performance.now());
}

function frameOfOrigin (relayMs)
{
    return frameOfRelayMs(relayMs, room.clock, audioClock);
}

/* getOutputTimestamp() beside the ping: the same second, the same
   sixteen kept (JAM_M3.md, section 6.2). */
function sampleAudioClock ()
{
    if (ctx === null || ctx.state !== 'running')
        return;

    const t = ctx.getOutputTimestamp();

    if (t.contextTime !== undefined && t.performanceTime !== undefined)
        audioClock.sample(t.contextTime, t.performanceTime);
}

function clocksReady ()
{
    return room !== null && audioClock !== null &&
           room.clock.count >= ENOUGH_SAMPLES &&
           audioClock.count >= ENOUGH_SAMPLES;
}

/* ---- commands ---- */

/* A command of our own: applied here, sent to everyone. */
async function send (cmd)
{
    sent.push(cmd);
    mesh.broadcast(cmd);

    /* A start goes by the room socket too: the one command a peer must
       not miss, and what a joiner is told (JAM_M3.md, section 5.3). */
    if (cmd.type === 'transport')
        room.transport(cmd);

    await receive(room.peer, cmd);
}

/* Every command in, ours included, by whichever path. */
async function receive (from, cmd)
{
    if (typeof cmd !== 'object' || cmd === null || !dedupe.accept(cmd))
        return;

    if (synth === null)
    {
        /* Nothing to apply it to yet: a room joined before Start. A
           start is remembered so Start can catch up. */
        if (cmd.type === 'transport' && cmd.op === 'start')
            room.playing = cmd;

        return;
    }

    if (isLate(cmd, transportNow()))
        late.push(cmd);

    /* How far ahead of its time it came: the margin the lead left, which
       is what to look at before turning the lead down. */
    if (cmd.at >= 0)
        margins.push({ from, seq: cmd.seq, type: cmd.type,
                       margin: cmd.at - transportNow() });

    await apply(cmd, { synth, frameOfOrigin, listens, load: loadFor });

    /* And what the page shows follows. */
    if (cmd.type === 'knob')
    {
        const input = document.querySelector(`#knobs input[data-knob="${cmd.knob}"]`);

        if (input !== null && from !== room.peer)
        {
            input.value = cmd.value;
            input.nextElementSibling?.remove();
            input.previousElementSibling.textContent =
                Number(cmd.value).toPrecision(3);
        }
    }
    else if (cmd.type === 'transport')
    {
        if (cmd.op === 'start')
            status(`Playing from ${room.peers.get(from)?.name ?? from}'s ` +
                   'Play.');
        else if (cmd.op === 'stop')
            status('Stopped.');
        else if (cmd.op === 'tempo' && from !== room.peer)
            $('tempo').value = cmd.bpm;
    }

    showNumbers();
}

/* The load a start asks for: the document at the revision the start
   names, then the piece into the worklet with the start's seed.
 *
 * The document may not have caught up with the sender yet (JAM_M3.md,
 * section 4.3): wait for updates, re-hashing on each, until it matches or
 * the origin has passed, at which point it is late and counted -- a
 * counted divergence rather than a silent one. */
async function loadFor (cmd)
{
    const deadline = room.clock.localOf(cmd.origin);

    while (await hashOf(doc) !== cmd.piece.hash)
    {
        if (performance.now() >= deadline)
        {
            log(`the document had not caught up with ${cmd.from}'s Play ` +
                'by its origin; loading what is here');
            late.push(cmd);
            break;
        }

        await new Promise((resolve) =>
        {
            const timer = setTimeout(done, Math.max(
                10, Math.min(250, deadline - performance.now())));

            function done ()
            {
                clearTimeout(timer);
                doc.off('update', done);
                resolve();
            }

            doc.on('update', done);
        });
    }

    await loadFromDoc(cmd.seed);
}

/* The piece from the document into the worklet, and what the page shows
   of it: the knobs, the seats, the channels it listens on. `seed' is
   the master seed, or -1 to draw one. */
async function loadFromDoc (seed = -1)
{
    const gen = pieceText(doc);

    if (gen === null)
    {
        status('The room has no piece.');
        return false;
    }

    for (const [name, text] of Object.entries(instrumentTexts(doc)))
        synth.instrument(name, text);

    /* Not suspended around the load, as the solo page does it. Every
       peer loads at the same moment -- when the start arrives, before
       its origin -- and a context suspended for the load's length comes
       back that far behind the wall clock, and behind the other peers'
       transports, for the rest of the run: a knob stamped a lead ahead
       of this peer's transport would already be in theirs' past. A
       dropout while the old run finishes is the price, and the audio is
       nobody's tape. */
    const it = await synth.loadPiece(gen, seed);

    roll.clear();
    tapeText = '';
    piece = it.errors.length === 0 ? it : null;

    if (piece === null)
    {
        status(`${pieceName(doc)} did not parse; see the numbers.`);
        it.errors.forEach(log);
        $('detail').open = true;
        listens = new Set();
    }
    else
    {
        listens = new Set(piece.listens);
        $('about').textContent = piece.description;
    }

    showKnobs();
    showSeats();
    enable();

    return piece !== null;
}

/* ---- transport ---- */

/* Play, and Apply: a start from a new origin, with the document as it
   stands, from a seed the file pins or this peer picks. */
async function play ()
{
    if (!clocksReady() || doc === null)
        return;

    const hash = await hashOf(doc);
    const origin = room.relayNow() + maker.transportLead * 1000;
    const seed = piece?.seeded ? piece.seed
                               : Math.floor(Math.random() * 0x100000000);

    await send(maker.start(origin, hash, seed));
}

async function stop ()
{
    await send(maker.stop());
}

async function tempo ()
{
    const bpm = Number($('tempo').value);

    if (bpm > 0)
        await send(maker.tempo(bpm));
}

/* ---- keys ---- */

function press (note)
{
    if (synth === null || room.seat === null)
        return;

    const already = sounding.get(note);

    if (already !== undefined)
    {
        already.count++;
        return;
    }

    sounding.set(note, { count: 1, seat: room.seat });
    send(maker.note(room.seat, note, VELOCITY));
    keyboard.hold(note, true);
}

function release (note)
{
    const held = sounding.get(note);

    if (held === undefined || --held.count > 0)
        return;

    sounding.delete(note);
    send(maker.noteoff(held.seat, note));
    keyboard.hold(note, false);
}

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

function typing (e)
{
    return synth === null || e.ctrlKey || e.metaKey || e.altKey ||
           (e.target instanceof Element &&
            e.target.closest('textarea, select, input, .cm-editor') !== null);
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

function shiftOctave (by)
{
    octave = Math.min(96, Math.max(12, octave + by * 12));
    releaseAll();
    keyboard.setLowest(octave);
    showRange();
}

function showRange ()
{
    const [low, high] = keyboard.range;

    $('range').textContent = `${noteName(low)} – ${noteName(high)}`;
}

/* ---- what the page shows ---- */

function showPeers ()
{
    const box = $('peers');

    box.replaceChildren();

    for (const [id, p] of room.peers)
    {
        const el = document.createElement('span');
        const me = id === room.peer;

        el.className = me ? 'peer me' : 'peer';
        el.textContent = p.name + (p.seat === null ? ''
                                                   : ` (seat ${p.seat})`);

        if (!me && mesh !== null)
        {
            const s = mesh.status(id);
            const path = document.createElement('span');

            path.className = 'path';
            path.textContent = s.path +
                (Number.isNaN(s.rtt) ? '' : ` ${s.rtt.toFixed(0)} ms`);
            el.append(path);
        }

        box.append(el);
    }

    /* Our seat, as the relay has it. */
    $('seat').value = room.seat === null ? '' : String(room.seat);
}

/* The seats are the piece's instruments, by name with their channel. */
function showSeats ()
{
    const sel = $('seat');
    const was = sel.value;

    sel.replaceChildren(new Option('none', ''));

    for (const inst of piece?.instruments ?? [])
        sel.add(new Option(`${inst.name} (channel ${inst.channel})`,
                           String(inst.channel)));

    sel.value = was;

    if (sel.value !== was)
        sel.value = '';
}

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
        input.dataset.knob = k.knob;
        input.type = 'range';
        input.min = k.min;
        input.max = k.max;
        input.step = k.step > 0 ? k.step : (k.max - k.min) / 1000;
        input.value = k.value;

        shown.className = 'value';
        shown.textContent = Number(k.value).toPrecision(3);

        /* A command, like everything else, heard knobLead later here
           and everywhere. */
        input.addEventListener('input', () =>
        {
            shown.textContent = Number(input.value).toPrecision(3);
            send(maker.knob(k.knob, Number(input.value)));
        });

        box.append(label, shown, input);
    }
}

function showNumbers ()
{
    if (room === null)
        return;

    const ms = (x) => Number.isNaN(x) ? 'not yet' : `${x.toFixed(2)} ms`;
    const lines = [
        `relay round trip     ${ms(room.clock.rtt)}`,
        `relay offset spread  ${ms(room.clock.spread)}   ` +
            `(${room.clock.count} samples)`,
    ];

    if (audioClock !== null)
        lines.push(
            `audio clock fit      ${ms(audioClock.residual * 1000)} ` +
            `residual   (${audioClock.count} samples)`,
            `sample rate          ${ctx.sampleRate} Hz`,
            `base latency         ${ms(ctx.baseLatency * 1000)}`,
            `output latency       ${ms((ctx.outputLatency ?? NaN) * 1000)}`,
            `synth window         ${synth.windowlen} frames`,
            `transport            ${transportNow().toFixed(3)} s`);

    lines.push(
        `late commands        ${lateCount} applied late by the worklet` +
        (late.length > 0
             ? `; the page saw ${late.length}: ` +
               late.slice(-3).map((c) => `${c.from}#${c.seq} ${c.type}` +
                                         `${c.op ? ' ' + c.op : ''}`)
                   .join(', ')
             : ''),
        `command gaps         ${dedupe.gaps}`,
        `commands sent        ${sent.length}`);

    $('numbers').textContent = lines.join('\n');
}

function enable ()
{
    const ready = synth !== null && piece !== null && clocksReady();

    $('play').disabled = !ready;
    $('apply').disabled = !ready;
    $('stop').disabled = synth === null;
    $('tempo').disabled = !ready;
    $('export').disabled = synth === null;
}

/* ---- the tape ---- */

function tape (m)
{
    roll.tape(m);
    transport.report(m, performance.now());
    lateCount = m.late;

    if (m.epoch !== tapeEpoch)
    {
        tapeEpoch = m.epoch;
        tapeText = '';
    }

    for (const e of m.events)
        tapeText += tapeLine(e);
}

function exportTape ()
{
    const blob = new Blob([tapeText], { type: 'text/plain' });
    const a = document.createElement('a');

    a.href = URL.createObjectURL(blob);
    a.download = `${room.roomName}-${room.name}.tape`;
    a.click();
    URL.revokeObjectURL(a.href);
}

/* ---- joining, and starting ---- */

async function join ()
{
    const params = new URLSearchParams(location.search);
    const roomName = $('room').value.trim() || 'lobby';
    const name = $('name').value.trim() || `guest-${Math.floor(
        Math.random() * 1000)}`;
    const url = await relayUrl(params);

    $('join').disabled = true;
    status(`Joining ${roomName} at ${url}...`);

    room = new Room(url, roomName, name, { piece: params.get('piece') });
    room.on('peers', showPeers)
        .on('clock', () => { showNumbers(); enable(); })
        .on('transport', (from, data) => receive(from, data))
        .on('error', (text) => log(`relay: ${text}`))
        .on('close', () => status('The relay went away.'));

    try
    {
        await room.connect();
    }
    catch (e)
    {
        status(e.message);
        $('join').disabled = false;
        return;
    }

    maker = new Maker(room.peer, transportNow);
    $('knoblead').value = maker.knobLead;
    $('transportlead').value = maker.transportLead;

    /* The document. */
    doc = new Y.Doc();
    provider = new WebsocketProvider(`${url}/doc`, roomName, doc);

    const c = colourOf(name);

    provider.awareness.setLocalStateField('user', {
        name, color: c.color, colorLight: c.light,
    });

    await new Promise((resolve) =>
        provider.synced ? resolve() : provider.once('synced', resolve));

    editor = new Editor($('editor'), $('tabs'), doc, provider.awareness);

    /* The mesh. */
    mesh = new Mesh(room, (from, cmd) => receive(from, cmd));
    mesh.on('change', showPeers)
        .on('fallback', (peer, why) =>
            log(`${room.peers.get(peer)?.name ?? peer}: through the relay ` +
                `(${why})`));

    $('joinrow').hidden = true;
    $('roompanel').hidden = false;
    showPeers();
    showNumbers();

    status(`In ${roomName} as ${name}. Press Start.` +
           (room.playing !== null
                ? ' The room is playing; you will hear the next Play.'
                : ''));

    history.replaceState(null, '', `?${new URLSearchParams(
        { room: roomName, name, ...(params.get('relay')
                                        ? { relay: params.get('relay') }
                                        : {}) })}`);
}

async function start ()
{
    $('start').disabled = true;
    status('Starting...');

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
        if (ctx !== null)
            ctx.close().catch(() => {});

        ctx = null;
        synth = null;
        status(`Could not start: ${e.message}`);
        $('start').disabled = false;
        return;
    }

    audioClock = new AudioClock(ctx.sampleRate);
    transport = new TransportClock(ctx.sampleRate);

    sampleAudioClock();
    setInterval(() => { sampleAudioClock(); showNumbers(); enable(); },
                1000);

    await loadFromDoc();
    status(`Started. Claim a seat and press Play.`);
}

function init ()
{
    const params = new URLSearchParams(location.search);

    $('room').value = params.get('room') ?? 'lobby';
    $('name').value = params.get('name') ?? '';

    roll = new Roll($('roll'), $('clock'));
    keyboard = new Keyboard($('keys'), { onPress: press, onRelease: release });
    keyboard.setLowest(octave);
    keyboard.fit();
    showRange();

    $('join').addEventListener('click', join);
    $('start').addEventListener('click', start);
    $('play').addEventListener('click', play);
    $('apply').addEventListener('click', play);
    $('stop').addEventListener('click', stop);
    $('tempo').addEventListener('change', tempo);
    $('export').addEventListener('click', exportTape);
    $('seat').addEventListener('change', () =>
    {
        releaseAll();
        room.claim($('seat').value === '' ? null : Number($('seat').value));
    });
    $('knoblead').addEventListener('change', () =>
    {
        maker.knobLead = Number($('knoblead').value);
    });
    $('transportlead').addEventListener('change', () =>
    {
        maker.transportLead = Number($('transportlead').value);
    });

    $('down').addEventListener('click', () => shiftOctave(-1));
    $('up').addEventListener('click', () => shiftOctave(1));

    window.addEventListener('keydown', keyDown);
    window.addEventListener('keyup', keyUp);
    window.addEventListener('blur', releaseAll);

    requestAnimationFrame(function frame ()
    {
        roll.draw();
        requestAnimationFrame(frame);
    });

    /* Hooks for jamtest.mjs, which drives two of these from a script:
       nothing here that a person could not do with the page. */
    window.jam = {
        join, start, play, stop,
        knob: (knob, value) => send(maker.knob(knob, value)),
        tempo: (bpm) => send(maker.tempo(bpm)),
        seat: (seat) => room.claim(seat),
        tape: () => tapeText,
        sent: () => sent,
        late: () => ({ worklet: lateCount, page: late }),
        margins: () => margins,
        ready: () => synth !== null && piece !== null && clocksReady(),
        transportNow,
        peers: () => [...room.peers].map(([id, p]) =>
            ({ id, ...p, ...mesh.status(id) })),
        probe: () => ({
            performanceNow: performance.now(),
            relayNow: room.relayNow(),
            currentTime: ctx?.currentTime,
            output: ctx?.getOutputTimestamp(),
            origin: transport?.origin,
            running: transport?.running,
            reported: transport?.reported,
            transportNow: transportNow(),
            fit: audioClock?.contextTimeAt(performance.now()),
        }),
    };

    if (params.get('room') && params.get('name'))
        join();
}

init();
