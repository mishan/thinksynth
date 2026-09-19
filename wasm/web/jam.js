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
 * jam.js -- the room page.
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
import { fileNames, files, hashOf, instrumentTexts, pieceName, pieceText,
         readFile, spliceFile } from './doc.js';
import { Editor, colourOf } from './editor.js';
import { createComposerView } from './composerview.js';
import { createNodeView } from './nodeview.js';
import { createSynth } from './host.js';
import { Keyboard, TypingKeys, showRange } from './keyboard.js';
import { setKnob, showKnobs } from './knobs.js';
import { Mesh } from './mesh.js';
import * as patch from './patch.js';
import { Roll } from './roll.js';
import { Room } from './room.js';
import { TapeDiff } from './tapediff.js';
import { tapeLine } from '../tape.mjs';

const $ = (id) => document.getElementById(id);

const VELOCITY = 100;

/* How many clock samples Play waits for. */
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

/* The piece's picture, drawn by the mirror -- a second instance of the
   module in a worker, fed the commands this page's worklet is fed --
   and the two tapes held against each other, which is a determinism
   check a room gets for nothing. */
let composer = null;
const diff = new TapeDiff();

/* The .dsp canvas over the document: the page's own instance of the
   module, the desktop's graph and writer in it, and every edit a splice
   into the shared file. Made on Start, because it is another instance of a
   600 KB module and a room nobody is playing in does not need one. */
let nodes = null;
let keyboard = null;
let keys = null;                /* the computer keyboard as a musical one */
let maker = null;
const dedupe = new Dedupe();

let piece = null;               /* the worklet's word on the loaded piece */
let listens = new Set();        /* channels the piece takes input on */

/* The shipped .dsp texts, for the channels a piece names and aims at
   nothing of its own. The document's instruments are the piece's and come
   from the document; these are the page's defaults' (patch.js). */
let dspTexts = {};
const sounding = new Map();     /* note -> { count, seat } */

/* What the numbers panel and the harness read back. Bounded, the way
   Dedupe bounds what it remembers: a knob is a command per slider tick
   per peer, and a page left in a room all afternoon would otherwise hold
   every one of them for ever. The count is what the panel shows; the
   tail is what a failure is read from. */
const KEEP = 256;

const sent = [];                /* the last commands this peer made */
const late = [];                /* the last the page saw were late */
const margins = [];             /* how early each stamped command came */
let sentCount = 0;
let lateSeen = 0;
let lateCount = 0;              /* the worklet's count */
let tapeText = '';              /* the tape since the last epoch, as text */
let tapeEpoch = -1;
let numbersDirty = false;       /* the panel is behind; the frame repaints */

function keep (list, item)
{
    list.push(item);

    if (list.length > KEEP)
        list.shift();
}

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

/* Transport seconds now, or -1 when there is no time passing to stamp
   against: no worklet yet, or a stopped transport. That is the contract
   commands.js's Maker is written to -- a command made while stopped is
   stamped -1, which is "now, on every peer", and the worklet applies it
   the moment it arrives rather than holding it for a time that will
   never come round again. TransportClock.now reports where the transport
   stopped, which is a position and not a time. */
function transportNow ()
{
    return synth === null || transport === null || !transport.running
               ? -1
               : transport.now(ctx.currentTime, performance.now());
}

function frameOfOrigin (relayMs)
{
    return frameOfRelayMs(relayMs, room.clock, audioClock);
}

/* getOutputTimestamp() beside the ping: the same second, the same
   sixteen kept. */
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
    sentCount++;
    keep(sent, cmd);
    mesh.broadcast(cmd);

    /* A start goes by the room socket too: the one command a peer must
       not miss, and what a joiner is told. */
    if (cmd.type === 'transport')
        room.transport(cmd);

    await receive(room.peer, cmd);
}

/* Every command in, ours included, by whichever path -- one at a time,
 * in the order they arrived.
 *
 * A start has work to do before the worklet hears of it: wait for the
 * document to reach the revision it names, then load the piece. Applied
 * concurrently, a knob that arrived during that reaches the worklet ahead
 * of the load that clears its queue, or ahead of the begin that gives it
 * a transport to be stamped against, and is dropped without being counted
 * anywhere. Behind the start it is simply applied to the run it was
 * stamped for. */
let applying = Promise.resolve();

function receive (from, cmd)
{
    const done = applying.then(() => applyOne(from, cmd));

    /* The queue outlives one command that threw. */
    applying = done.catch(() => {});

    return done;
}

async function applyOne (from, cmd)
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
    {
        lateSeen++;
        keep(late, cmd);
    }

    /* How far ahead of its time it came: the margin the lead left, which
       is what to look at before turning the lead down. */
    if (cmd.at >= 0)
        keep(margins, { from, seq: cmd.seq, type: cmd.type,
                        margin: cmd.at - transportNow() });

    await apply(cmd, { synth, frameOfOrigin, listens, load: loadFor });

    /* And what the page shows follows. */
    /* Ours moved its own slider as it was dragged. */
    if (cmd.type === 'knob' && from !== room.peer)
        setKnob($('knobs'), cmd.knob, cmd.value);
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

    /* Not repainted here: a knob arrives at slider rate per peer, and
       rebuilding the whole panel on each is a lot of DOM for a number
       nobody is reading that fast. The frame below picks it up. */
    numbersDirty = true;
}

/* The load a start asks for: the document at the revision the start
   names, then the piece into the worklet with the start's seed.
 *
 * The document may not have caught up with the sender yet: wait for
 * updates, re-hashing on each, until it matches or the origin has passed,
 * at which point it is late and counted -- a counted divergence rather
 * than a silent one. */
async function loadFor (cmd)
{
    const deadline = room.clock.localOf(cmd.origin);

    while (await hashOf(doc) !== cmd.piece.hash)
    {
        if (performance.now() >= deadline)
        {
            log(`the document had not caught up with ${cmd.from}'s Play ` +
                'by its origin; loading what is here');
            lateSeen++;
            keep(late, cmd);
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

    /* And then the aiming, in that order, for the reason the solo page
       aims in that order: a channel the piece named and put nothing on
       sounds through the defaults and never through what this page did
       before.
     *
       It is this page's and not the room's. Nothing here is in the
       document and nothing is on the tape, so two peers may hear a
       piece's unaimed channels through different instruments -- which is
       exactly where two people with two thinkrc files already are, and
       what a piece that carries its own instruments is the answer to.
       What is the same on every peer is the piece, which is what the
       tapes are compared on. */
    if (it.errors.length === 0)
    {
        const aiming = await patch.aim(synth, it.sinks, dspTexts);

        aiming.failed.forEach(log);
    }

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

    drawKnobs();
    showSeats();
    showNodeChannel();
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
    keys?.forget();

    for (const [note, held] of [...sounding])
    {
        held.count = 1;
        release(note);
    }
}

/* What the octave keys and the two buttons do: let go of everything,
   move the keys under the hands, and say where they are now. */
function shifted (lowest)
{
    releaseAll();
    keyboard.setLowest(lowest);
    showRange($('range'), keyboard);
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

/* A knob moved here is a command like everything else, heard knobLead
   later on this page and on every other. */
function drawKnobs ()
{
    showKnobs($('knobs'), piece?.knobs ?? [],
              (knob, value) => send(maker.knob(knob, value)));
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
            `transport            ` +
            (transport.running ? `${transportNow().toFixed(3)} s` : 'stopped'));

    lines.push(
        `tape v mirror        ${diff.summary()}`,
        `late commands        ${lateCount} applied late by the worklet` +
        (lateSeen > 0
             ? `; the page saw ${lateSeen}: ` +
               late.slice(-3).map((c) => `${c.from}#${c.seq} ${c.type}` +
                                         `${c.op ? ' ' + c.op : ''}`)
                   .join(', ')
             : ''),
        `command gaps         ${dedupe.gaps}`,
        `commands sent        ${sentCount}`);

    $('numbers').textContent = lines.join('\n');
    numbersDirty = false;
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
    diff.take('worklet', m);
    roll.tape(m);
    nodes?.feed(m.probes);
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

/* ---- the composer view ---- */

/* Everything the mirror says: the frames it drew and the gestures its
   canvas wants sent go to the view, its tape is held against the
   worklet's, and the rest is a line in the log. */
function fromMirror (m)
{
    if (composer !== null && composer.fromMirror(m))
        return;

    if (m.type === 'tape')
        diff.take('mirror', m);
    else if (m.type === 'log')
        log(m.text);
}

/* Which channel the instrument on the canvas plays on, which is the
   channel a tap is armed on. The piece says which .dsp each of its
   instruments plays and the worklet reports that at the load; the canvas
   is showing one of those files. Called again after every load, since
   until one has happened there is nothing to ask. */
function showNodeChannel ()
{
    nodes?.onChannel(piece?.instruments?.find(
        (i) => i.dsp === $('nodefile').value)?.channel ?? -1);
}

function showComposer ()
{
    /* A gesture is a command like a knob: stamped with the knob lead,
       broadcast, and applied at the time it names on every peer, this one
       included. So a Life board somebody paints on is the same board
       everywhere from that beat. */
    composer ??= createComposerView({
        toMirror: (m) => synth?.toMirror(m),
        onGesture: (g) => send(maker.input(g.chain, g.stage, g.kind, g.x,
                                           g.y, g.w, g.h, g.button)),
    });

    composer.show(true);
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
                                         onTape: tape,
                                         onMirror: fromMirror });
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

    /* What the defaults are made of. Fetched once, here, because the
       aiming below happens inside a load and a load has no time to wait
       for the network: every peer loads at the same moment, when the
       start arrives and before its origin. */
    try
    {
        /* Without the kit: the index carries `samples/<name>' wavs beside
           the patches, and .text() on a wav is three hundred kilobytes of
           mojibake fetched on every load and kept in a map nothing looks
           it up in. The jam page does not hand samples to its worklet at
           all yet -- see the note in main.js's start(), which fetches
           them as bytes and passes them through tw_sample -- so an
           instrument built on osc::sample is silent here. Filtering is
           what this line can honestly do about that; the rest is its own
           change. */
        const names =
            (await (await fetch('dsp/index.json')).json())
                .filter((n) => !n.startsWith('samples/'));
        const texts = await Promise.all(
            names.map((n) => fetch(`dsp/${n}`).then((r) => r.text())));

        dspTexts = Object.fromEntries(names.map((n, i) => [n, texts[i]]));

        await Promise.all(patch.DEFAULTS.map((n) => patch.patchText(n)));
    }
    catch (e)
    {
        log(`the default instruments are not available: ${e.message}`);
    }

    sampleAudioClock();
    setInterval(() => { sampleAudioClock(); showNumbers(); enable(); },
                1000);

    showComposer();

    try
    {
        nodes = await createNodeView({
            /* In a room the files are the document's, and a write is a
               splice: nobody's copy is authoritative and there is no
               save. */
            files: {
                read: (name) => readFile(doc, name),
                write: (name, next) => spliceFile(doc, name, next),
                watch: (name, onChange) =>
                {
                    const text = files(doc).get(name);

                    text?.observe(onChange);

                    return () => text?.unobserve(onChange);
                },
            },
            onStatus: status,
            sampleRate: ctx.sampleRate,

            /* A tap is armed in the worklet, which is where the synth
               that is rendering lives; what comes back is a slot, and the
               samples arrive with the tape. */
            probe: (channel, node, arg) => synth.probe(channel, node, arg),
            unprobe: (slot) => synth.unprobe(slot),
        });
        nodes.offer(fileNames(doc));
        files(doc).observe(() => nodes.offer(fileNames(doc)));

        showNodeChannel();
        $('nodefile').addEventListener('change', showNodeChannel);
    }
    catch (e)
    {
        log(`the instrument canvas did not start: ${e.message}`);
    }

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
    keys = new TypingKeys({
        press, release, shifted,
        playable: () => synth !== null,
        editing: '.cm-editor',      /* the room page has a code editor */
    });
    keyboard.setLowest(keys.lowest);
    keyboard.fit();
    showRange($('range'), keyboard);

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

    $('down').addEventListener('click', () => keys.shift(-1));
    $('up').addEventListener('click', () => keys.shift(1));

    window.addEventListener('keydown', (e) => keys.keyDown(e));
    window.addEventListener('keyup', (e) => keys.keyUp(e));
    window.addEventListener('blur', releaseAll);

    requestAnimationFrame(function frame ()
    {
        roll.draw();

        if (numbersDirty)
            showNumbers();

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
        late: () => ({ worklet: lateCount, page: late, seen: lateSeen }),
        margins: () => margins,
        ready: () => synth !== null && piece !== null && clocksReady(),

        /* A file as the document has it now. What a harness checks an
           edit against, and what one page holds the other's document
           against. */
        file: (name) => readFile(doc, name),

        /* The instrument canvas: where its boxes are, so a harness can
           press on one rather than at a guess, and what it has selected. */
        node: () => (nodes === null ? null : {
            boxes: nodes.boxes(),
            selected: nodes.selected(),
            probes: nodes.probes(),
            box: (i) => nodes.boxAt(i),
        }),

        /* Which half of ready() is not true yet. A harness that waits for
           ready() and gives up has otherwise only a timeout to report,
           and the answer is usually the audio context: a machine with no
           sound card can leave resume() unresolved, and then Start never
           returns at all. */
        state: () => ({
            status: $('status').textContent,
            synth: synth !== null,
            piece: piece !== null,
            context: ctx === null ? 'none' : ctx.state,
            relaySamples: room === null ? 0 : room.clock.count,
            audioSamples: audioClock === null ? 0 : audioClock.count,
            wanted: ENOUGH_SAMPLES,
        }),
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
