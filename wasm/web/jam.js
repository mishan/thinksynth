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
 * for what crosses the wire, and host.js, rollview.js and composerview.js
 * as the solo page has them. This file is the UI and the joins between
 * them.
 */

import { WebsocketProvider } from 'y-websocket';
import * as Y from 'yjs';

import { AudioClock, TransportClock, frameOfRelayMs } from './clock.js';
import { Dedupe, GRID, KNOB_LEAD, Maker, TRANSPORT_LEAD, apply, catchUp,
         commandTag, isLate, keyAt, nextBar, replayable, tieOf }
    from './commands.js';
import { docOf, fileNames, files, hashOf, instrumentTexts, pieceName,
         pieceText, readFile, snapshot, spliceFile } from './doc.js';
import { Editor, colourOf } from './editor.js';
import { createComposerView } from './composerview.js';
import { createNodeView } from './nodeview.js';
import { createPanes } from './panes.js';
import { createSynth } from './host.js';
import { Keyboard, TypingKeys, showRange } from './keyboard.js';
import { createKeyFocus } from './keyfocus.js';
import { numberIn, showPanel } from './panel.js';
import { Mesh } from './mesh.js';
import { midiAvailable, midiToggle } from './midi.js';
import { MidiOutControls } from './midioutui.js';
import { keepOffline } from './offline.js';
import { moveLayouts } from './layouts.js';
import * as patch from './patch.js';
import { createRollView, showClock } from './rollview.js';
import { Room } from './room.js';
import { createSeqView } from './seqview.js';
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

/* The piano roll, drawn by the mirror beside the scheduler whose future
   half it shows (rollview.js). */
let roll = null;

/* The piece's grids as tracks (seqview.js), from the same mirror. */
let seq = null;

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

/* The panels this page tiles, in the order the document has them.
 *
 * A list of ids and nothing else: what each one is called, how narrow it
 * may be made and which element it is are the markup's answers
 * (`data-pane' in jam.html). Most of them are the solo page's panes under
 * the same names, which is the point -- the two documents say the same
 * things and catalogcheck.mjs holds them to it.
 */
const PANES = ['roll', 'knobs', 'composerview', 'seqview', 'keyboard',
               'documentbox', 'nodeview', 'detail'];

/* Where they go, the first time somebody opens a room in a window with
 * room to tile. Data, and this page's: panes.js knows how to divide a
 * window and nothing about what a room has in it.
 *
 * One layout rather than one per mode, because a room has one mode. The
 * three things a person here is doing at once -- watching the piece,
 * reading it and working on an instrument -- are the three panes that
 * are not in the drawer.
 */
const ROOM_LAYOUT = {
    dir: 'row', size: [0.52, 0.48], kids: [
        { dir: 'col', size: [0.44, 0.28, 0.28], kids: [
            { tabs: ['composerview', 'seqview'] },
            { tabs: ['roll'] },
            { tabs: ['knobs'] }] },
        { dir: 'col', size: [0.42, 0.34, 0.24], kids: [
            { tabs: ['nodeview'] },
            { tabs: ['documentbox'] },
            { tabs: ['keyboard'] }] }],
};

/* The layout. Made at the end of init(), because what it adopts has to
   be in the document. */
let panes = null;
let keyboard = null;
let keys = null;                /* the computer keyboard as a musical one */
let keyfocus = null;            /* and who has it, the page or the keys  */
let midiIn = null;              /* the MIDI in button (midi.js)          */
let maker = null;
const dedupe = new Dedupe();

let piece = null;               /* the worklet's word on the loaded piece */

/* The text this page's worklet is playing, as { piece, files }: what was
   loaded, with every edit applied since. What Apply sends the difference
   from -- a .dsp the document has changed since then goes with the edit. */
let playing = null;

/* The edits handed to the worklet and not yet answered for, by their tie.
   Its `edited' message says which went in, and only those change
   `playing'. */
const pendingEdits = new Map();

/* Edits this page's worklet has applied since the load: what a command
   that names something by index was made against (commands.js, Maker). */
let editsSeen = 0;

/* The synth as the room's commands reach it: what edits it is handed is
   noted on the way through, from wherever the edit came -- a peer's, this
   page's own, or one a late joiner steps through. */
let roomSynth = null;

/* The last tape message: where the transport was, in seconds and beats,
   which is what Apply picks the next bar from. */
let lastTape = null;

/* The knob panel's rows, by name and by row id: a command names a knob by
   name, and the panel knows its sliders by id. */
const knobIds = new Map();
const knobNames = new Map();
let listens = new Set();        /* channels the piece takes input on */

/* How this page's keys reach the room: 'direct', 'quantised' or 'ahead'
   (commands.js, keyAt). */
let playMode = 'direct';

/* note -> { count, midi, seat, mode, at, local, epoch }: how many hands are
   on it, how many of those are MIDI keys, the seat it went out on, the mode
   and time it was stamped with, whether this page played it itself, and the
   run it was pressed in -- a release goes the way its press went. */
const sounding = new Map();

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
/* A late joiner's begin is on its way to the worklet or being stepped up
   to the room; cleared once the worklet reports it caught up. */
let catching = false;

/* Stamped commands that came while there was no worklet to apply them to:
   a room joined, Start not yet pressed. The relay keeps the same ones for a
   late joiner, but a command can reach this page by the mesh before its
   copy reaches the relay, so these are merged with what it hands over. */
const early = [];
const EARLY_MAX = 10000;
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

    /* An edit goes by the room socket alone: a peer that missed one plays
       another piece from there, and it can be larger than a data channel
       takes. In order behind the start it was made in, too, which is what
       the run check in applyOne relies on. */
    if (cmd.type !== 'edit')
        mesh.broadcast(cmd);

    /* A start goes by the room socket too: the one command a peer must
       not miss, and what a joiner is told. Every other stamped command
       goes as a copy, which the relay keeps for whoever joins while this
       run plays. */
    if (cmd.type === 'transport' || cmd.type === 'edit')
        room.transport(cmd);
    else if (replayable(cmd))
        room.log(cmd);

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

    /* The run this page is in, as soon as it is: a knob moved from here
       on is logged under it (room.log), whichever path brought the Play,
       and what was kept for catching up belongs to the run being left. */
    const startOrStop = cmd.type === 'transport' &&
                        (cmd.op === 'start' || cmd.op === 'stop');

    if (startOrStop)
    {
        room.playing = cmd.op === 'start' ? cmd : null;
        early.length = 0;
    }

    /* An edit made in a run that has since been replaced: a peer that
       applied it before the new start would load over it anyway, so none
       does. */
    if (cmd.type === 'edit' && cmd.run !== room.runKey)
        return;

    if (synth === null)
    {
        /* Nothing to apply it to yet: a room joined before Start. What
           came after the start is kept for the catching up. */
        if (!startOrStop && replayable(cmd) && early.length < EARLY_MAX)
            early.push(cmd);

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

    /* A Play or Stop ends any catching up: the run being caught up with
       is over, and the report that would have said so never comes. */
    if (startOrStop)
        catching = false;

    await apply(cmd, { synth: roomSynth, frameOfOrigin, listens,
                       load: loadFor, self: room.peer });

    /* And what the page shows follows. */
    /* Ours moved its own slider as it was dragged. */
    if (cmd.type === 'knob' && from !== room.peer)
        setKnobValue(String(knobIds.get(cmd.knob) ?? cmd.knob), cmd.value);
    else if (cmd.type === 'edit')
        status(`${room.peers.get(from)?.name ?? from}'s edit` +
               (cmd.at >= 0 ? ` applies at ${cmd.at.toFixed(2)} s.` : '.'));
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

    runSeed = cmd.seed;
    await loadFromDoc(cmd.seed);
}

/* The seed the run playing now started with: what a seek starts again
   with, so it is the same piece from there. */
let runSeed = null;

/* A room already playing when this page pressed Start: the run as the
 * relay kept it, stepped through from its origin to now (commands.js,
 * catchUp). Queued behind whatever is being applied, and ahead of whatever
 * arrives while the relay answers, so a command the mesh brings in the
 * meantime is applied after the catching up rather than lost under it --
 * or dropped as a duplicate of the copy the relay had.
 */
function joinRun ()
{
    const wanted = room.runKey;

    const done = applying.then(async () =>
    {
        /* The origin is a relay-clock time, and turning it into a frame of
           this output needs both clocks: the same samples Play waits for. */
        if (!clocksReady())
        {
            status('The room is playing; waiting for the clocks...');

            while (!clocksReady())
                await new Promise((resolve) => setTimeout(resolve, 250));
        }

        /* A Play or Stop applied since Start has put this page where the
           room is already. */
        if (room.runKey !== wanted)
            return;

        let run;

        try
        {
            run = await room.catchUp();
        }
        catch (e)
        {
            log(`catching up: ${e.message}`);
            status('Could not catch up with the room; you will hear the ' +
                   'next Play.');
            return;
        }

        if (run.start === null || room.runKey !== wanted)
            return;

        if (run.overflowed)
        {
            status('The room has been playing too long to catch up with; ' +
                   'you will hear the next Play.');
            return;
        }

        if (!run.files.matched)
            log('the relay never saw the document the room is playing; ' +
                'catching up with what it has');

        /* The relay's log and what the mesh brought before Start, once
           each. Every one is marked seen, so a copy still in flight on the
           mesh is a duplicate when it lands. */
        const byKey = new Map();

        for (const c of [...run.log, ...early])
            byKey.set(`${c.from}#${c.seq}`, c);

        early.length = 0;

        for (const c of [run.start, ...byKey.values()])
            dedupe.accept(c);

        status(`Catching up with ${room.peers.get(run.start.from)?.name ??
                                   run.start.from}'s Play...`);
        await catchUp(run.start, [...byKey.values()], {
            synth: roomSynth, listens,
            frameOfOrigin: (ms) =>
            {
                catching = true;
                return frameOfOrigin(ms);
            },
            load: (start) =>
            {
                runSeed = start.seed;
                return loadFromDoc(start.seed, docOf(run.files));
            },
        });

    });

    applying = done.catch((e) => log(`catching up: ${e.message}`));

    return done;
}

/* The piece from the document into the worklet, and what the page shows
   of it: the knobs, the seats, the channels it listens on. `seed' is
   the master seed, or -1 to draw one. `from' is the document to read, the
   room's own unless a late joiner is loading the revision a run started
   from. */
async function loadFromDoc (seed = -1, from = doc)
{
    const gen = pieceText(from);

    if (gen === null)
    {
        status('The room has no piece.');
        return false;
    }

    for (const [name, text] of Object.entries(instrumentTexts(from)))
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

    loadedGen = gen;
    playing = snapshot(from);
    editsSeen = 0;
    pendingEdits.clear();

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
        const aiming = await patch.aim(synth, it.sinks);

        aiming.failed.forEach(log);
    }

    tapeText = '';
    ownParams = [];
    piece = it.errors.length === 0 ? it : null;

    if (piece === null)
    {
        status(`${pieceName(from)} did not parse; see the numbers.`);
        it.errors.forEach(log);

        /* The fold, for the document, and the pane, for the layout: a
           box tiled behind another tab is open already and still not
           where anybody can read it. Neither takes the focus -- what
           went wrong went wrong in somebody else's edit. */
        $('detail').open = true;
        panes?.present('detail', { focus: false });
        listens = new Set();
    }
    else
    {
        listens = new Set(piece.listens);
        $('about').textContent = piece.description;
    }

    await drawKnobs();
    showSeats();
    showMidiOut();
    showNodeChannel();
    enable();

    return piece !== null;
}

/* The piece text this peer last loaded, with the edits applied since,
   which is what its canvas draws and what a drop on the canvas is numbered
   against. */
let loadedGen = null;

/* A stage box dropped elsewhere in its chain: the document spliced, by
 * this peer, the way its own param edits are, and applied if the room is
 * playing -- a structural edit is an edit, and the room's edit is Apply, at
 * the next bar. Stopped, the next Play takes it.
 *
 * Only against the document as this peer loaded it: the drop is numbered
 * by the canvas, and a document that has moved on since -- a peer's edit,
 * a move not yet applied -- may not have that stage there. Not retried
 * for the same reason. */
async function moveStage (chainName, from, to)
{
    const name = pieceName(doc);
    const was = name === null ? null : readFile(doc, name);

    if (was === null || was !== loadedGen)
    {
        log('the piece has changed since it was loaded; Apply it before ' +
            'moving a stage');
        return;
    }

    const { text } = await synth.genMoveStage(was, chainName, from, to);

    if (text === '')
    {
        log(`could not move that stage in ${chainName}`);
        return;
    }

    if (readFile(doc, name) !== was)
    {
        log('the piece changed while that stage was being moved');
        return;
    }

    spliceFile(doc, name, text);

    if (transport?.running)
        await applyEdit();
    else
        log('stage moved; Play applies it');
}

/* A chain's F: the frozen chain spliced into the document by this peer,
 * on moveStage's terms, and applied if the room is playing. The original
 * is left playing: the room's mix is set by commands, and M is one. */
async function freezeChain (chain, chainName)
{
    const name = pieceName(doc);
    const was = name === null ? null : readFile(doc, name);

    if (was === null || was !== loadedGen)
    {
        log('the piece has changed since it was loaded; Apply it before ' +
            'freezing a chain');
        return;
    }

    const { text, why } = await synth.genFreeze(was, chain, 2);

    if (text === '')
    {
        log(`could not freeze ${chainName}: ${why}`);
        return;
    }

    if (readFile(doc, name) !== was)
    {
        log('the piece changed while that chain was being frozen');
        return;
    }

    spliceFile(doc, name, text);

    if (transport?.running)
        await applyEdit();
    else
        log(`${chainName} frozen; Play applies it`);
}

/* ---- transport ---- */

/* Apply: the document as it stands, into the piece that is playing, at the
 * next bar -- what a stage keeps and what is built again is thcGenDiff's
 * rule, the same on every peer. With the .gen goes every .dsp the document
 * has changed since this page's worklet last loaded one. While stopped
 * there is no bar to wait for, and it is a Play from the top.
 */
async function applyEdit ()
{
    if (doc === null || synth === null)
        return;

    if (!transport?.running || lastTape === null || playing === null ||
        piece === null)
        return play();

    const text = pieceText(doc);

    if (text === null)
        return;

    const changed = {};

    for (const [name, t] of Object.entries(instrumentTexts(doc)))
        if (playing.files[name] !== t)
            changed[name] = t;

    const at = nextBar(transportNow(), lastTape, maker.transportLead);

    await send({ ...maker.edit(at, text, changed), run: room.runKey });
}

/* An edit has been applied here: the piece is what it now says, and the
   knobs, the seats and the channels it listens on follow. The text this
   page believes is playing moves by the edits that went in, and not by one
   that was refused -- whose files then still count as changed at the next
   Apply. */
async function edited (m)
{
    editsSeen = m.count;

    for (const { tie, went } of m.results ?? [])
    {
        const e = pendingEdits.get(tie);

        pendingEdits.delete(tie);

        if (e === undefined || !went || playing === null)
            continue;

        playing.files = { ...playing.files, ...e.files };
        playing.files[playing.piece] = e.text;
        loadedGen = e.text;
    }

    m.errors.forEach((e) => log(`edit: ${e}`));

    if (m.errors.length > 0)
        status('The edit did not go in whole; see the log.');
    else
        status('Edited.');

    piece = m;
    listens = new Set(m.listens);
    $('about').textContent = m.description;

    (await patch.aim(synth, m.sinks)).failed.forEach(log);

    await drawKnobs();
    showSeats();
    showMidiOut();
    showNodeChannel();
    enable();
}

/* Play: a start from a new origin, with the document as it stands, from a
   seed the file pins or this peer picks. */
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

/* A seek, as a room has it: a start from time `from', with the seed of
 * the run playing, so every peer plays the same piece up to there at the
 * same frame and goes on together. */
async function seekTo (from)
{
    if (!clocksReady() || doc === null)
        return;

    const hash = await hashOf(doc);
    const origin = room.relayNow() + maker.transportLead * 1000;
    const seed = runSeed ?? (piece?.seeded ? piece.seed
                                           : Math.floor(Math.random() *
                                                        0x100000000));

    await send(maker.start(origin, hash, seed, from));
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

/* As on the solo page (main.js): `midi' says a MIDI keyboard is the one
   holding the note, which releaseKeys leaves down. */
function press (note, velocity = VELOCITY, midi = false)
{
    /* Catching up, the worklet would play it at the past transport time
       it has stepped to, silently, into a piece the room has gone past. */
    if (synth === null || room.seat === null || catching)
        return;

    const already = sounding.get(note);

    if (already !== undefined)
    {
        already.count++;

        if (midi)
            already.midi++;

        return;
    }

    const seat = room.seat;
    const at = keyAt(playMode, transportNow(), lastTape, maker.knobLead);
    const mode = at < 0 ? 'direct' : playMode;

    /* A bar ahead onto a channel: heard here now, and by everyone else a
       bar from now. Into the piece it waits for its time here too, or
       this page's piece would compose from it a bar early. */
    const local = mode === 'ahead' && !listens.has(seat);

    if (local)
        synth.noteOn(note, velocity, -1, seat);

    sounding.set(note, { count: 1, midi: midi ? 1 : 0, seat, mode, at, local,
                         epoch: lastTape?.epoch });
    send(maker.note(seat, note, velocity, mode, at < 0 ? null : at, local));
    keyboard.hold(note, true);
}

function release (note, midi = false)
{
    const held = sounding.get(note);

    if (held === undefined)
        return;

    if (midi)
    {
        if (held.midi === 0)
            return;

        held.midi--;
    }

    if (--held.count > 0)
        return;

    sounding.delete(note);

    /* A quantised release a grid line after its press at the least, so
       a quick tap is a sixteenth and not a note let go before it began.
       A press from a run since replaced by a Play or a seek is no floor:
       its time is in that run's seconds. */
    const grid = lastTape?.tempo > 0 ? GRID * 60 / lastTape.tempo : 0;
    const after = held.epoch !== lastTape?.epoch ? -1
                : held.mode === 'quantised' ? held.at + grid : held.at;
    const at = held.mode === 'direct'
        ? -1
        : keyAt(held.mode, transportNow(), lastTape, maker.knobLead, after);

    if (held.local)
        synth.noteOff(note, -1, held.seat);

    send(maker.noteoff(held.seat, note, at < 0 ? 'direct' : held.mode,
                       at < 0 ? null : at));
    keyboard.hold(note, false);
}

/* Everything, whoever is holding it: a seat change is about to aim the
   keys somewhere else. */
function releaseAll ()
{
    midiIn?.forget();

    for (const held of sounding.values())
        held.midi = 0;

    releaseKeys();
}

/* What the pointer and the computer keyboard hold, and not what a MIDI
   keyboard does: its note off arrives wherever the focus is. */
function releaseKeys ()
{
    keyboard?.releaseAll();
    keys?.forget();

    for (const [note, held] of [...sounding])
    {
        if (held.midi > 0)
            held.count = held.midi;
        else
        {
            held.count = 1;
            release(note);
        }
    }
}

/* What the octave keys and the two buttons do: let go of everything,
   move the keys under the hands, and say where they are now. */
function shifted (lowest)
{
    releaseKeys();
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

/* MIDI out's button, delay and pickers (midioutui.js). */
let midiOutUI = null;

/* A row per instrument the piece names a MIDI port for: where it plays on
   this machine, and why. */
function showMidiOut ()
{
    const box = $('midioutlist');

    box.replaceChildren();

    for (const inst of piece?.instruments ?? [])
    {
        if (!inst.midi)
            continue;

        const row = document.createElement('div');
        const name = document.createElement('span');

        row.className = 'row midiinst';
        name.textContent = inst.name;
        row.append(name, ...midiOutUI.picker(inst));
        box.append(row);
    }
}

/* The seats are the piece's instruments, by name with their channel. */
function showSeats ()
{
    const sel = $('seat');
    const was = sel.value;

    sel.replaceChildren(new Option('none', ''));

    /* Every channel the piece plays, each once: its instruments by name,
       then the channels it takes input on, then the rest its sinks name.
       A piece built on `input midi' declares no instruments and would
       otherwise offer nobody a seat. */
    const offered = new Set();
    const offer = (channel, label) =>
    {
        if (offered.has(channel))
            return;

        offered.add(channel);
        sel.add(new Option(`${label} (channel ${channel})`, String(channel)));
    };

    for (const inst of piece?.instruments ?? [])
        offer(inst.channel, inst.name);

    for (const channel of piece?.listens ?? [])
        offer(channel, 'input');

    for (const channel of piece?.sinks ?? [])
        offer(channel, 'part');

    sel.value = was;

    if (sel.value !== was)
        sel.value = '';
}

/* A knob moved here is a command like everything else, heard knobLead
 * later on this page and on every other.
 *
 * The same renderer the solo page uses, over the same description
 * (src/KnobPanel.cpp): a knob row's id is the number the command names it
 * by. `setKnobValue' is what showPanel hands back, and is how a peer's move
 * reaches the slider without going out again as an edit of our own. */
let setKnobValue = () => {};

async function drawKnobs ()
{
    const answer = piece === null
        ? { shape: 0 } : await synth.panel(1 /* thPanel::KNOB */, 0, 0);

    setKnobValue = () => {};
    knobIds.clear();
    knobNames.clear();

    if (answer.shape === 0)
    {
        $('knobs').replaceChildren();
        return;
    }

    const panel = JSON.parse(answer.json);

    for (const row of panel.rows)
    {
        knobIds.set(row.knob, String(row.id));
        knobNames.set(String(row.id), row.knob);
    }

    /* Held before it is sent, and not after: this one goes out to the room
       as a stamped command and every peer applies it to the same knob. See
       the same handler in main.js. By name, which survives an edit that
       adds a knob or takes one away (commands.js). */
    setKnobValue = showPanel($('knobs'), panel,
                             (row, text) =>
                             {
                                 const value = numberIn(text);

                                 if (value !== null)
                                     send(maker.knob(knobNames.get(String(row)),
                                                     value));
                             },
                             (row, text) =>
                             {
                                 const value = numberIn(text);

                                 if (value !== null)
                                     writeKnob(Number(row), value);
                             });
}

/* Beside the choice, what it costs: the round trip to the relay, which is
   about what a direct key takes to reach the others, and what the other two
   add at the tempo playing. */
function showModeNote ()
{
    const rtt = room?.clock.rtt;
    const tempo = lastTape?.tempo > 0 ? lastTape.tempo : 0;
    const said = [Number.isNaN(rtt) || rtt === undefined
                      ? 'round trip not yet'
                      : `round trip ${rtt.toFixed(0)} ms`];

    if (tempo > 0 && playMode === 'quantised')
        said.push(`a key lands on the next sixteenth, up to ` +
                  `${(maker.knobLead * 1000 +
                      GRID * 60000 / tempo).toFixed(0)} ms on`);
    else if (tempo > 0 && playMode === 'ahead')
        said.push(`the others hear a key ` +
                  `${(lastTape.meter * 60 / tempo).toFixed(2)} s later`);

    $('modenote').textContent = said.join('; ');
}

function showNumbers ()
{
    if (room === null)
        return;

    showModeNote();

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
    lastTape = m;
    diff.take('worklet', m);
    showClock($('clock'), m);
    nodes?.feed(m.probes);
    transport.report(m, performance.now());
    lateCount = m.late;

    /* Caught up: the begin has landed and the stepping is done, or a stop
       in the log ended the run. The load ahead of the begin unpinned the
       origin, so a report from before the begin has none. */
    if (catching && Number.isFinite(m.origin) && !m.catching)
    {
        catching = false;
        status('Caught up with the room.');
    }

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
    if (seq !== null && seq.fromMirror(m))
        return;

    if (composer !== null && composer.fromMirror(m))
        return;

    if (roll !== null && roll.fromMirror(m))
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

/* This peer's param edits that have gone out and not yet been written.
 *
 * Every peer applies an edit to its own copy of the piece, and the next
 * Start reloads from the document, so the edit has to reach the document
 * too -- once. Two peers splicing the same change insert it twice, so it is
 * the peer who made it that writes it, and this is how that peer knows the
 * edit the worklet reports is its own. */
let ownParams = [];

const paramKey = (e) =>
    JSON.stringify([e.at, e.chain, e.stage, e.row, e.text]);

/* A picture's edit written at a gesture's end, and a knob's value at a
   drag's end: by the command's own name, which the edit carries back as
   its tag. Not by stamp, which every command made while stopped shares. */
const inputKey = (e) => JSON.stringify(['input', e.tag ?? commandTag(e)]);
const knobKey = (e) => JSON.stringify(['knob', e.tag ?? commandTag(e)]);

/* This peer's knob, let go of: a command every peer applies to its own
   file, and this peer's to write into the document. */
function writeKnob (knob, value)
{
    const cmd = maker.knobWrite(knob, value);

    ownParams.push(knobKey(cmd));
    send(cmd);
}

/* The same for an arrangement edit: the command's fields, and the written
   edit's -- whose level is the text it was written with. */
const sectionKey = (e) =>
    JSON.stringify(['section', e.at, e.section, e.chain,
                    e.level ?? Number(e.valueText)]);

/* What the worklet wrote, and of it, what this peer made: into the document.
 *
 * Applied to the document as it now stands rather than copied from the
 * worklet's file, which is the piece as it was loaded plus whatever edits
 * have been applied here -- a document that has moved on since is given this
 * edit, not reverted to that file. */
async function paramsEdited ({ edits })
{
    const released = new Set();

    for (const e of edits)
    {
        const isSection = e.section >= 0;
        const isKnob = e.knob >= 0;
        const key = isSection ? sectionKey(e) : isKnob ? knobKey(e)
            : e.input ? inputKey(e) : paramKey(e);
        const at = ownParams.indexOf(key);

        if (at < 0)
            continue;

        /* A release's key stays for the rest of what it wrote -- one
           gesture on a euclid ring writes fills and rotate -- and goes
           with the batch, which holds all of it. */
        if (!e.input)
            ownParams.splice(at, 1);
        else
            released.add(key);

        /* Its stage was edited away or renamed: nothing was written. */
        if (e.param === '')
            continue;

        const name = pieceName(doc);

        /* The document may move while the worklet works: a splice is made
           against the text it was worked out from, or it would undo what
           arrived in between. */
        for (let tries = 0; name !== null && tries < 4; tries++)
        {
            const was = readFile(doc, name);

            if (was === null)
                break;

            const { text } = isSection
                ? await synth.genSetSection(was, e.param, e.chainName,
                                            Number(e.valueText))
                : isKnob
                    ? await synth.genSetKnob(was, e.param,
                                             Number(e.valueText))
                    : await synth.genSetParam(was, e);

            if (text === '')
            {
                log(`${e.param}: not written to ${name}`);
                break;
            }

            if (readFile(doc, name) === was)
            {
                spliceFile(doc, name, text);
                break;
            }
        }
    }

    for (const key of released)
    {
        const at = ownParams.indexOf(key);

        if (at >= 0)
            ownParams.splice(at, 1);
    }
}

function sendGesture (g)
{
    const cmd = maker.input(g.chain, g.stage, g.kind, g.x, g.y, g.w, g.h,
                            g.button,
                            { chainName: g.chainName,
                              stageName: g.stageName });

    /* A gesture's end is where a picture that edits its params writes
       them (THC_INPUT_EDITS); what it writes, this peer puts in the
       document, since it made it. */
    if (g.kind === 2)
        ownParams.push(inputKey(cmd));

    send(cmd);
}

function showComposer (on)
{
    /* Made when it is first wanted and never for a pane nobody has
       looked at: onShow says `no' for every pane at load, and a view
       built to be told that would have started a worker for nothing. */
    if (composer === null && (!on || synth === null))
        return;

    /* A gesture is a command like a knob: stamped with the knob lead,
       broadcast, and applied at the time it names on every peer, this one
       included. So a Life board somebody paints on is the same board
       everywhere from that beat. */
    composer ??= createComposerView({
        toMirror: (m) => synth?.toMirror(m),
        onGesture: sendGesture,

        /* A stage's param, out to the room and back at its time -- to this
           peer as to every other, which is what keeps one piece one
           piece. */
        onParamEdit: (chain, stage, row, text, names) =>
        {
            const cmd = maker.param(chain, stage, row, text, names);

            ownParams.push(paramKey(cmd));
            send(cmd);
        },

        /* A chain's M or S: the room's mix, so every peer hears the same
           chains. */
        onMix: (type, chain, on) => send(maker[type](chain, on)),

        /* A knob node dragged on the canvas: the room's knob command. The
           release repeats the last value and is not sent. */
        onKnob: (knob, value, commit, name) =>
        {
            if (commit)
            {
                writeKnob(knob, value);
                return;
            }

            send(maker.knob(name, value));

            /* A peer's own move does not come back through the strip's
               follower (it skips this peer), so it is shown here. */
            setKnobValue(knobIds.get(name) ?? String(knob), value);
        },

        onMove: moveStage,
        onFreeze: freezeChain,

        /* A section's block: the room's transport there -- a start from
           that time (seekTo). */
        onSeek: seekTo,

        /* An arrangement cell: the room's command, written into the
           document by this peer when it comes back, as a param is. */
        onSection: (section, chain, level) =>
        {
            const cmd = maker.section(section, chain, level);

            ownParams.push(sectionKey(cmd));
            send(cmd);
        },
    });

    composer.show(on);
}

/* The tracks. Made on Start, visible or not, unlike the composers: the
   `piece' message a load sends comes once, and a pane opened after it
   would otherwise have no tracks until the next. A click on one is the
   same command as a click on the composers' picture. */
function showSeq (on)
{
    if (seq === null && synth === null)
        return;

    seq ??= createSeqView({ toMirror: (m) => synth?.toMirror(m),
                            onGesture: sendGesture });

    seq.show(on);
}

/* The piano roll, on the same terms: made when it is first wanted, and
   drawing only while somebody is looking at it. Nothing about it is a
   command -- it reports what this peer's mirror composed, which is what
   the tape comparison is about. */
function showRoll (on)
{
    if (roll === null && (!on || synth === null))
        return;

    roll ??= createRollView({ toMirror: (m) => synth?.toMirror(m) });

    roll.show(on);
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

    maker = new Maker(room.peer, transportNow, { edits: () => editsSeen });
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
                ? ' The room is playing; Start joins it where it is.'
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
                                         onParamEdits: paramsEdited,
                                         onMidi: (msgs) => midiOutUI.take(msgs),
                                         onMidiState: (list) =>
                                             midiOutUI.state(list),
                                         onEdited: edited,
                                         onMirror: fromMirror });
        roomSynth = {
            ...synth,
            edit: (at, text, files, tie) =>
            {
                pendingEdits.set(tie, { text, files });
                synth.edit(at, text, files, tie);
            },
        };
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
    midiOutUI.start(synth, ctx, audioClock);

    $('midi').disabled = !midiAvailable();

    if (!midiAvailable())
        $('midistatus').textContent = 'needs Chromium or Firefox, over https '
                                      + 'or on localhost';

    /* What the defaults are made of. Fetched once, here, because the
       aiming below happens inside a load and a load has no time to wait
       for the network: every peer loads at the same moment, when the
       start arrives and before its origin. */
    try
    {
        /* The index carries the kit beside the patches, as
           `samples/<name>' wavs for osc::sample, which go over as bytes
           (main.js's start() does the same). */
        const names = await (await fetch('dsp/index.json')).json();
        const kit = names.filter((n) => n.startsWith('samples/'));
        const graphs = names.filter((n) => !n.startsWith('samples/'));
        const [texts, wavs] = await Promise.all([
            Promise.all(graphs.map(
                (n) => fetch(`dsp/${n}`).then((r) => r.text()))),
            Promise.all(kit.map(
                (n) => fetch(`dsp/${n}`).then((r) => r.arrayBuffer()))),
        ]);

        kit.forEach((n, i) => synth.sample(n, new Uint8Array(wavs[i])));

        /* Into the module's own MEMFS, which is where a .patch's `dsp'
           line is resolved from -- the same handover the solo page does,
           and the same one a piece's `instrument' block relies on. It
           used to be a map kept here, because patch.js read the .patch
           itself and handed the graph's text over a channel at a time;
           the module reads the .patch now and looks the graph up by name,
           so what it needs is the graphs, not a map of them.

           A document's own instruments are written over these at load
           (above), which is what a piece carrying its own amb01.dsp
           means. */
        graphs.forEach((n, i) => synth.instrument(n, texts[i]));

        await Promise.all(
            (await patch.defaultNames(synth)).map((n) => patch.patchText(n)));
    }
    catch (e)
    {
        log(`the default instruments are not available: ${e.message}`);
    }

    sampleAudioClock();
    setInterval(() => { sampleAudioClock(); showNumbers(); enable(); },
                1000);

    /* The stage popover, kept up to date. Four times a second, which is
       about the desktop's draw timer and far below an animation frame: a
       peer's edit of the line this is showing arrives at its stamp, and a
       param read through a knob moves whenever the knob does. Nothing
       happens while the popover is down. */
    setInterval(() => composer?.pollParams(), 250);

    showComposer(panes.visible('composerview'));
    showSeq(panes.visible('seqview'));
    showRoll(panes.visible('roll'));

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
        nodes.show(panes.visible('nodeview'));
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

    if (room.playing !== null)
        await joinRun();
}

function init ()
{
    const params = new URLSearchParams(location.search);

    $('room').value = params.get('room') ?? 'lobby';
    $('name').value = params.get('name') ?? '';

    keyboard = new Keyboard($('keys'), { onPress: press, onRelease: release });
    /* Who has the keyboard (keyfocus.js). The room page's code editor
       is a div full of text boxes rather than a <textarea>, so it is
       named here; everything else it works out for itself. */
    keyfocus = createKeyFocus({ editing: '.cm-editor',
                                indicator: $('keysstate'),
                                onRelease: releaseKeys });
    keys = new TypingKeys({
        press, release, shifted, focus: keyfocus,
        playable: () => synth !== null,
    });
    keyboard.setLowest(keys.lowest);
    keyboard.fit();
    showRange($('range'), keyboard);

    midiIn = midiToggle({
        button: $('midi'), status: $('midistatus'),
        onNoteOn: (note, velocity) => press(note, velocity, true),
        onNoteOff: (note) => release(note, true),
    });

    midiOutUI = new MidiOutControls({
        button: $('midiout'), delay: $('midioutdelay'),
        status: $('midioutstatus'), clock: $('midiclock'),
        instruments: () => piece?.instruments ?? [],
        onChange: showMidiOut,
    });

    $('join').addEventListener('click', join);
    $('start').addEventListener('click', start);
    $('play').addEventListener('click', play);
    $('apply').addEventListener('click', applyEdit);
    $('stop').addEventListener('click', stop);
    $('tempo').addEventListener('change', tempo);
    $('export').addEventListener('click', exportTape);
    $('seat').addEventListener('change', () =>
    {
        releaseAll();
        room.claim($('seat').value === '' ? null : Number($('seat').value));
    });
    $('playmode').addEventListener('change', () =>
    {
        /* Whatever is held was stamped the old way and is let go that
           way; the next key goes the new one. */
        releaseAll();
        playMode = $('playmode').value;
        showModeNote();
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
    window.addEventListener('blur', releaseKeys);

    /* And the layout, over what is in the document now.
     *
     * onShow is the whole of what tiling asks of this page: a pane in a
     * background tab, folded away or zoomed off the screen is a pane
     * whose work can stop, and the two canvases here are what that is
     * worth stopping. The editor is where a key means editing rather
     * than a command, as it is for the keys. A layout left under the
     * store's old name is moved to the new one first. */
    moveLayouts('panes:jam', 'thinksynth:panes:jam', ['room']);

    panes = createPanes({
        root: $('panes'), catalog: PANES, store: 'thinksynth:panes:jam',
        layouts: { room: ROOM_LAYOUT }, mode: 'room', on: true,
        editing: '.cm-editor', reset: 'Reset layout',
        onShow: (id, on) =>
        {
            if (id === 'composerview')
                showComposer(on);
            else if (id === 'seqview')
                showSeq(on);
            else if (id === 'roll')
                showRoll(on);
            else if (id === 'nodeview')
                nodes?.show(on);
        },
    });

    /* The two popovers, out of the panes and over them: each is placed
       in page coordinates beside a box on a canvas, and a pane is a box
       that scrolls -- so one left inside a pane would be clipped by it
       the moment it reached the edge. */
    panes.overlay().append($('composerparams'), $('nodemenu'));

    /* The numbers, once an animation frame and only when something
       moved. The two canvases ask for their own frames (canvasview.js)
       and stop asking while nobody is looking at them. */
    requestAnimationFrame(function frame ()
    {
        if (numbersDirty)
            showNumbers();

        requestAnimationFrame(frame);
    });

    /* Hooks for jamtest.mjs, which drives two of these from a script:
       nothing here that a person could not do with the page. */
    window.jam = {
        join, start, play, stop,

        /* A key, as the on-screen keys press one, and the way keys go. */
        press: (note, velocity) => press(note, velocity),
        release: (note) => release(note),
        mode: (m) =>
        {
            $('playmode').value = m;
            $('playmode').dispatchEvent(new Event('change'));
        },
        /* A knob by name, or by its row's id as the panel numbers it. */
        knob: (knob, value) => send(maker.knob(
            typeof knob === 'string' ? knob
                                     : knobNames.get(String(knob)), value)),
        apply: () => applyEdit(),

        /* A file of the document replaced, as a splice (doc.js): what a
           harness types with, without a keyboard. */
        setFile: (name, text) => spliceFile(doc, name, text),

        /* Edits this page's worklet has applied since the load. */
        edits: () => lastTape?.edits ?? 0,
        tempo: (bpm) => send(maker.tempo(bpm)),
        seat: (seat) => room.claim(seat),
        seatNow: () => room.seat,
        tape: () => tapeText,

        /* The panes this page has and the layout they are in, for
           panecheck: the catalog written down once. */
        panes: () => PANES,
        layout: () => panes.layout(),
        sent: () => sent,
        late: () => ({ worklet: lateCount, page: late, seen: lateSeen }),
        margins: () => margins,
        ready: () => synth !== null && piece !== null && clocksReady(),

        /* A late joiner still stepping up to the room (joinRun). */
        catching: () => catching,

        /* A file as the document has it now. What a harness checks an
           edit against, and what one page holds the other's document
           against. */
        file: (name) => readFile(doc, name),
        piece: () => pieceName(doc),

        /* The composer canvas's params popover: where a stage's handle is,
           so a harness can press one rather than aim at a guess, and what
           the popover ended up showing. The layout is the canvas's, which
           makes asking it the only honest way. */
        handleOf: (chain, stage) => composer?.handleOf(chain, stage),
        params: () => composer?.params() ?? [],

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

/* A new version takes over by itself until somebody has joined or
   started; after that it waits for the button (offline.js). */
keepOffline({
    busy: () => room !== null || synth !== null,
    offer: (go) =>
    {
        $('updaterow').hidden = false;
        $('update').onclick = async () =>
        {
            $('update').disabled = true;

            if (!await go())
            {
                $('updatewhy').textContent =
                    'Close thinksynth\'s other tabs and windows first.';
                $('update').disabled = false;
            }
        };
    },
}).catch((e) => console.warn('no offline copy:', e));
