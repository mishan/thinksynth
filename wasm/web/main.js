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
 * Two things to play. A patch is one .dsp, played from the keyboard. A piece
 * is a .gen: the scheduler in the worklet composes it, the knobs it declared
 * are sliders, and what it delivers comes back as the tape and is drawn on a
 * roll.
 *
 * They are modes and not two panels side by side, because a piece takes the
 * channels it asks for and the first of those is channel 0, where the
 * keyboard's patch was -- thinkweb.cpp, tw_piece_load, says why that is
 * deliberate. In piece mode the keyboard still plays: into the piece,
 * through `input midi', which is the path a peer's keyboard will take. It
 * arrives on a channel the page picks, because that is what a chain's
 * `input midi' is matched against.
 *
 * WHAT A CHANNEL SOUNDS LIKE is the piece's to say, and where the piece is
 * silent on it, the defaults' -- never what the page did before. A piece
 * whose sinks name channels is asking the reader to aim them, which nine of
 * the shipped seventeen do, and a page that aimed nothing played them
 * through whatever the last mode had left lying there, or through nothing
 * at all. So a load is followed by the aiming and never preceded by it, and
 * the aiming is a function of the piece and of what somebody chose by hand.
 * patch.js holds the rule and the defaults; what a .patch means is the
 * module's (src/PatchFile.h).
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
 * knobs are drawn as, are keyboard.js's and panel.js's: the room page
 * wants them identical, and two copies of a thing two pages have to agree
 * on is how they stop agreeing.
 */

import { createComposerView } from './composerview.js';
import { createSeqView } from './seqview.js';
import { createSynth } from './host.js';
import { createNodeView } from './nodeview.js';
import { TapeDiff } from './tapediff.js';
import { Keyboard, TypingKeys, noteName, showRange } from './keyboard.js';
import { createKeyFocus } from './keyfocus.js';
import { createPanes } from './panes.js';
import { numberIn, showPanel } from './panel.js';
import * as patch from './patch.js';
import { Roll } from './roll.js';

const $ = (id) => document.getElementById(id);

const VELOCITY = 100;

/* The panels this page tiles, in the order the document has them.
 *
 * A list of ids and nothing else: what each one is called, how narrow it
 * may be made and which element it is are the markup's answers
 * (`data-pane' in index.html), so this does not describe the page twice.
 * The room page keeps a list of its own, and panecheck.mjs holds the two
 * against each other where they overlap.
 */
const PANES = ['roll', 'seqview', 'composerview', 'knobs', 'channelbox',
               'paramview', 'nodeview', 'keyboard', 'patchsource',
               'piecesource', 'detail'];

/* Which of them belong to which mode. Everything not named here is in
   all three -- the keys, the parameters, the graph, the numbers.

   A sequence is a piece the page wrote, so its panes are a subset of the
   piece's: the tracks, the roll, the channels they are heard on, and the
   .gen it came out as. What it deliberately has not got is the composer
   canvas -- somebody laying down a pattern does not need to be shown
   that it is a chain of stages, and the mode exists to not tell them. */
const PIECE_PANES = ['roll', 'seqview', 'composerview', 'knobs',
                     'channelbox', 'piecesource'];
const SEQ_PANES = ['roll', 'seqview', 'channelbox', 'piecesource'];
const PATCH_PANES = ['patchsource'];

/* What each mode makes unavailable: every pane of the other two that is
   not also one of its own. */
const modePanes = {
    seq: SEQ_PANES, piece: PIECE_PANES, patch: PATCH_PANES,
};

/* Where they go, the first time somebody opens this page in a window
 * with room to tile.
 *
 * Data, and this page's: panes.js knows how to divide a window and
 * nothing about what a keyboard is. A layout names the panes it has room
 * for and the rest wait in the drawer, which is why there is one of these
 * per mode rather than one with everything in it -- a patch has no piano
 * roll to show and a piece has no .dsp of its own.
 *
 * The fractions are shares of a split and the minimums are the markup's,
 * so a default that cannot be laid out at the threshold this turns on at
 * is a default that is wrong: the two columns of each of these come to
 * about 730 pixels of minimum, and the threshold is 60em.
 */
const PATCH_LAYOUT = {
    dir: 'row', size: [0.58, 0.42], kids: [
        { dir: 'col', size: [0.64, 0.36], kids: [
            { tabs: ['nodeview'] },
            { tabs: ['keyboard'] }] },
        { dir: 'col', size: [0.44, 0.38, 0.18], kids: [
            { tabs: ['paramview'] },
            { tabs: ['patchsource'] },
            { tabs: ['detail'] }] }],
};

/* A sequence opens on the tracks and the keys, with the roll under them:
   what you drew, what you can play over it, and what came out. */
const SEQ_LAYOUT = {
    dir: 'row', size: [0.62, 0.38], kids: [
        { dir: 'col', size: [0.68, 0.32], kids: [
            { tabs: ['seqview'] },
            { tabs: ['roll'] }] },
        { dir: 'col', size: [0.4, 0.3, 0.3], kids: [
            { tabs: ['paramview'] },
            { tabs: ['keyboard'] },
            { tabs: ['channelbox', 'piecesource'] }] }],
};

const PIECE_LAYOUT = {
    dir: 'row', size: [0.6, 0.4], kids: [
        { dir: 'col', size: [0.62, 0.38], kids: [
            /* The sequencer in front of the piece's picture, and the two
               a tab apart: what somebody opens a piece to do is play
               with it, and the canvas is what they look at once they
               want to know how it is put together. */
            { tabs: ['seqview', 'composerview'] },
            { tabs: ['roll'] }] },
        { dir: 'col', size: [0.26, 0.24, 0.26, 0.24], kids: [
            { tabs: ['knobs'] },
            { tabs: ['channelbox'] },
            { tabs: ['piecesource'] },
            { tabs: ['keyboard'] }] }],
};

/* The layout. Made at the end of init(), because what it adopts has to be
   in the document and the folds the page opens by hand have to be set. */
let panes = null;

/* Where patch mode puts its one .dsp. thinkweb.cpp's tw_piece_load says
   why a piece takes this channel first, and why the two are modes. */
const PATCH_CHANNEL = 0;

let ctx = null;
let synth = null;
let keyboard = null;
let keys = null;                 /* the computer keyboard as a musical one */
let keyfocus = null;             /* and who has it, the page or the keys  */

/* note -> { count, piece, channel }: how many fingers are on it, and the
   route it went out by, which is the route its release has to take. */
const sounding = new Map();

/* The piece, as it stands: what the worklet said when it loaded, and the
   roll of what it has delivered since. */
let piece = null;
let roll = null;

/* The composer view: the piece's own picture, drawn by the mirror -- a
   second scheduler in a worker, fed the messages the worklet is fed, with
   real composer instances in it. The page's half of it is an element and
   a pointer; everything else is the same C++ the desktop draws with. */
let composer = null;

/* The same piece as tracks: the pane somebody clicks patterns into. */
let seq = null;

/* The instrument as a graph: the desktop's node editor over whichever
   .dsp this page is playing. Made on Start, since it is another
   instance of the module. */
let nodes = null;

/* The worklet's tape against the mirror's. Two instances of one module on
   one stream of messages have to compose one piece, and this is that claim
   checked continuously while somebody plays -- for nothing, since both
   tapes are already being posted. */
const diff = new TapeDiff();

/* The channels somebody has aimed by hand in this session, as channel ->
   the .patch or .dsp they chose. A piece that loads afterwards keeps
   their aiming and fills only the rest from the defaults, which is what
   makes the aiming a choice rather than a thing to redo after every
   Load. The engine's numbering, as everything held here is. */
const aimed = new Map();

/* What is on each channel now, as patch.js reported putting it there:
   channel -> { patch, dsp, title }, or the piece's own instrument by
   name. Only to draw the row; nothing is decided from it, because
   deciding from what the page did before is the bug. */
let placed = new Map();

function log (text)
{
    $('log').textContent += text + '\n';
    $('log').scrollTop = $('log').scrollHeight;
}

/* The box the status line means by "see below", brought to where it can
 * be seen.
 *
 * Two answers, because there are two layouts and each is quiet about the
 * other. In the document the box is a fold and opening it is the whole
 * of it. Tiled, the fold is open already and held that way, and what is
 * in front of the box instead is another tab -- or nothing at all,
 * because somebody closed the pane and it is waiting in the drawer. A
 * page that says see below and shows nothing is worse than one that
 * says nothing.
 *
 * Without the focus, which is the difference between raising a pane for
 * somebody and raising one at them: a .dsp that did not parse is read by
 * whoever was editing it, and taking the cursor out of the text box to
 * point at the reason costs them their place in it.
 */
function seeBelow ()
{
    $('detail').open = true;
    panes?.present('detail', { focus: false });
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

/* What the piece says about itself: shown when a piece is what is
   playing and it says anything at all.
 *
 * It used to sit inside the piece section and go away with it. Out in
 * the strip it has to be told, and it is worth telling: an empty
 * paragraph across the top of a tiled layout is a line charged to every
 * pane under it for nothing. */
function showAbout ()
{
    $('about').hidden = !composing() || $('about').textContent === '';
}

function keyChannel ()
{
    return Number($('keychan').value);
}

/* Whether what is playing is composed: a sequence and a piece are the
   same scheduler on the same clock, and everything that is not about
   choosing one is about that rather than about which it is. */
function composing ()
{
    return mode() !== 'patch';
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

    const piecing = composing();
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
        `octave           Z = ${noteName(keys.lowest)}\n` +
        `tape v mirror    ${diff.summary()}`;
}

/* ---- the patch ---- */

async function loadPatch ()
{
    if (synth === null)
        return;

    const ok = await quietly(() => synth.load($('dsp').value));

    /* Patch mode has taken PATCH_CHANNEL, whatever was aimed there. So the
       aiming for it is forgotten rather than left to be shown as still
       true: a row that said PhatRip over a channel holding the text box's
       .dsp would be the page reporting what it did two modes ago, which is
       the thing this whole file is against.
     *
       Only when the load worked. tw_load leaves the channel alone when the
       parse fails (thSynth::loadTree returns before the swap), so what was
       aimed there is still there. */
    if (ok)
    {
        aimed.delete(PATCH_CHANNEL);
        placed.delete(PATCH_CHANNEL);
    }

    $('status').textContent = ok ? `Loaded ${$('patch').value}. Play.`
                                 : 'That .dsp did not parse; see below.';

    if (!ok)
        seeBelow();

    await showParams();
}

async function pickPatch ()
{
    $('dsp').value = await (await fetch(`dsp/${$('patch').value}`)).text();

    showNodes();

    await loadPatch();
}

/* ---- the piece ---- */

/* The piece, and then the aiming.
 *
 * That order, and it is the whole of the fix: the piece declares what it
 * can, the page fills what the piece left to the reader, and what was on
 * a channel a moment ago decides nothing. Both inside one quietly(),
 * because the aiming builds graphs on the audio thread exactly as the
 * load does and the context is already down.
 */
async function loadPiece ()
{
    if (synth === null)
        return;

    let aiming = { placed: new Map(), failed: [] };

    const it = await quietly(async () =>
    {
        const loaded = await synth.loadPiece($('gen').value);

        if (loaded.errors.length === 0)
            aiming = await patch.aim(synth, loaded.sinks, aimed);

        return loaded;
    });

    roll.clear();
    piece = it.errors.length === 0 ? it : null;
    placed = aiming.placed;

    if (piece === null)
    {
        $('status').textContent = 'That .gen did not parse; see below.';
        it.errors.forEach(log);
        seeBelow();
    }
    else if (aiming.failed.length > 0)
    {
        $('status').textContent =
            `Loaded ${piece.name || $('piece').value}, but not everything ` +
            'it asked for; see below.';
        aiming.failed.forEach(log);
        seeBelow();
    }
    else
        $('status').textContent =
            `Loaded ${piece.name || $('piece').value}. Press Play.`;

    $('about').textContent = piece === null ? '' : piece.description;
    showAbout();

    for (const id of ['play', 'stop', 'rewind'])
        $(id).disabled = piece === null;

    /* The redraw, as one thing a harness can wait for.
     *
     * Two of these now ask the worklet for a panel and wait for the answer,
     * and that round trip is not a load: quietly() does not know about it and
     * `quiet' is stable while it is still in flight. A harness that pressed
     * on before it landed found the knob row replaced under it between a
     * focus and a keypress -- which is the thing settled() exists to stop,
     * so settled() waits for this too. */
    drawn = (async () =>
    {
        await drawKnobs();
        showChannels();
        showNodes();
        await showParams();
        roll.draw();
    })();

    await drawn;
}

/* ---- the sequence ----
 *
 * The mode this page opens on, and the one that asks least of anybody: a
 * row of tracks, a menu per track for what plays it, and cells to click.
 *
 * Its piece is written here rather than shipped in gen/, and that is the
 * point of it. A mode whose first step is "choose a piece" has already
 * asked somebody to know what a piece is and which of thirty-two of them
 * is the one to draw on; this one has four empty tracks in front of them
 * before they have chosen anything. What comes out is an ordinary .gen
 * all the same -- it is in the box under the .gen pane, it loads through
 * the same loader every shipped piece loads through, and it can be saved
 * and opened in the Composer like any other.
 *
 * No `instrument' blocks, deliberately. A channel a piece fills is the
 * piece's, and the page will not offer to replace it; a channel a piece
 * merely names is aimed by the page, from the defaults at first and from
 * the menu on the track after that. That is what puts a menu on every
 * track here and none on a shipped piece's.
 */
const SEQ_TRACKS = 4;
const SEQ_STEPS = 16;
/* Six: five degrees and the octave above them, which is a range to
   write a line in and still leaves four tracks visible at once. */
const SEQ_ROWS = 6;

function sequenceText ()
{
    const empty = Array(SEQ_ROWS).fill('.'.repeat(SEQ_STEPS)).join('/');

    /* One track with something on it, because a sequencer that makes no
       sound when it is started reads as broken rather than as empty.
       Four on the floor on the bottom row of the first track. */
    const first = empty.replace(new RegExp(`\\.{${SEQ_STEPS}}$`),
                                'x...'.repeat(SEQ_STEPS / 4));

    const track = (n) => `chain track${n} {
    stage seq gen::grid {
        notes  = pent;
        steps  = ${SEQ_STEPS};
        rows   = ${SEQ_ROWS};
        cells  = "${n === 1 ? first : empty}";
        period = 0.25 beats;
        hold   = 0.2 beats;
        vel    = 96;
        listen = 0;
    };
    sink { channel = ${n}; };
};`;

    const tracks = [];

    for (let n = 1; n <= SEQ_TRACKS; n++)
        tracks.push(track(n));

    return `# A sequence, written by the page.
#
# Four grids on four channels: rows are degrees of the ladder below,
# columns are steps. Click the cells; the menu on each track says what
# plays it. Save this file and it opens in the Composer like any other.

name "A sequence";
description "Four tracks. Click the cells; pick what plays them.";

tempo 112;

scale pent "C3 D3 E3 G3 A3";

${tracks.join('\n\n')}
`;
}

/* The mode, entered. The text is written once a session: coming back to
   it keeps whatever the box says, which is what somebody who went to look
   at a patch and came back expects to find. */
async function loadSequence ()
{
    if (!/gen::grid/.test($('gen').value))
        $('gen').value = sequenceText();

    await loadPiece();

    if (piece !== null)
        $('status').textContent =
            'Click cells to draw a pattern, then press Play.';
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

/* The tail of a load: the panels it redraws, which are asked of the worklet
   and arrive after the load itself is done with. See loadPiece. */
let drawn = Promise.resolve();

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
 * that moves it. The command carries a frame like every other, so the page
 * is the nearest peer and not a privileged one (docs/JAM.md).
 *
 * The same renderer the channel's parameters use, over the same kind of
 * description (src/KnobPanel.cpp). A knob row's id is the number the
 * command names it by, which is why the edit is a Number() of it and
 * nothing here has to hold a second list.
 */
async function drawKnobs ()
{
    if (synth === null)
        return;

    const answer = piece === null
        ? { shape: 0 } : await synth.panel(1 /* thPanel::KNOB */, 0, 0);

    if (answer.shape === 0)
    {
        $('knobs').replaceChildren();
        return;
    }

    /* tw_knob takes a number and checks nothing: a knob is delivered by a
       stamped command rather than by tw_panel_edit, so KnobPanel::propose is
       on no path between this and the write. panel.js holds what it emits
       inside the row's travel; this is the last look before it becomes a
       command every peer applies. */
    showPanel($('knobs'), JSON.parse(answer.json), (row, text) =>
    {
        const value = numberIn(text);

        if (value !== null)
            synth.knob(Number(row), value);
    });
}

/* ---- the channels row ---- */

/* Every channel this piece touches, in the file's numbering.
 *
 * Which is the union of four things, and each is there for its own
 * reason: the channels its sinks name, because those are the ones it is
 * asking somebody to aim; the channels it takes `input midi' on, because
 * that is where playing goes; the channels its own instruments took,
 * because a person looking at the row wants to see the whole piece and
 * not the half of it that is theirs; and the key channel, because that is
 * where the keys go whether or not the piece has an opinion about it.
 */
function touched ()
{
    const all = new Set([keyChannel()]);

    for (const c of piece?.sinks ?? [])
        all.add(c);

    for (const c of piece?.listens ?? [])
        all.add(c);

    for (const inst of piece?.instruments ?? [])
        all.add(inst.channel);

    return [...all].sort((a, b) => a - b);
}

/* One line per channel: its number, and what is on it. A channel the
   piece aimed itself has nothing to choose -- taking its instrument away
   would be the page overriding the piece, which is the wrong way round --
   so it says which instrument it is holding and stops there. */
function showChannels ()
{
    const box = $('channels');

    box.replaceChildren();

    if (piece === null)
        return;

    for (const channel of touched())
    {
        const line = document.createElement('div');
        const num = document.createElement('span');

        line.className = 'row channel';

        num.className = 'chan';
        num.textContent = String(channel + 1);
        line.append(num);

        const inst = piece.instruments.find((i) => i.channel === channel);

        if (inst !== undefined)
        {
            const own = document.createElement('span');

            own.className = 'own';
            own.textContent = `${inst.name}, the piece's`;
            line.append(own);
        }
        else
        {
            line.append(chooser(channel));

            /* And whether it has been edited since it was read.
             *
             * The desktop has said this for twenty years -- it is what its
             * Save button is lit by -- and the page could not, because the
             * page had no idea what was on a channel beyond the name it had
             * asked for. The slots are shared now (src/PatchSet.h), so the
             * module answers it, and a mark here is the whole of the record
             * that the file and the channel have parted company: nothing
             * writes a .patch by itself in either shell. */
            const mark = document.createElement('span');

            mark.className = 'edited';
            mark.dataset.channel = String(channel);
            mark.hidden = true;
            mark.textContent = 'edited';
            mark.title = 'This patch has been changed since it was loaded.';
            line.append(mark);

            /* And a way to keep it. The desktop has had a Save button for
               twenty years and the page had nothing: a patch tweaked here
               was a patch that lasted until the tab closed. */
            const save = document.createElement('button');

            save.className = 'save';
            save.dataset.channel = String(channel);
            save.hidden = true;
            save.textContent = 'Save';
            save.title = 'Download this patch as a .patch file.';
            save.addEventListener('click', () => savePatch(channel));
            line.append(save);
        }

        box.append(line);
    }

    /* The marks, once the rows they hang off exist. Not awaited: a row that
       has just been drawn is correct until the module says otherwise, and
       the caller has nothing to do differently either way. */
    showEdited();

    /* The tracks name their channels' instruments, and this is every
       place that can change. */
    seq?.refresh();
}

/* One channel's patch, downloaded.
 *
 * The bytes are the module's (tw_patch_compose) and are the bytes the
 * application writes -- the slot's graph, effect, side and info, and the
 * values the channel holds now. So a patch saved here opens on the desktop,
 * and the desktop will not rewrite it on its first save.
 *
 * A download and not a write: a page has nowhere to write to. What it can do
 * is hand somebody a file, which is what the desktop's Save does too from
 * where they are standing.
 */
async function savePatch (channel)
{
    if (synth === null)
        return;

    /* Where ctime()'s line goes in the banner comment. A Date is the nearest
       thing a page has, and the line is a comment either way. */
    const { text } = await synth.patchCompose(channel, new Date().toString());

    if (text === '')
    {
        $('status').textContent =
            `Channel ${channel + 1}: nothing to save.`;
        return;
    }

    /* The name it came with, or the graph's with the extension changed --
       which is what the desktop's Save As offers for a patch that has never
       had a name. Without the drawer: a browser download names a file, not a
       place to put it. */
    const was = placed.get(channel);
    const name = (was?.patch ?? `${was?.dsp ?? 'patch'}`)
        .split('/').pop().replace(/\.(patch|dsp)$/, '') + '.patch';

    const url = URL.createObjectURL(
        new Blob([text], { type: 'text/plain' }));
    const link = document.createElement('a');

    link.href = url;
    link.download = name;

    /* In the document for the click, and the URL let go on the turn after
       it. A detached anchor is a link nothing has to follow, and a blob URL
       revoked in the same turn as the click is a download that races the
       browser fetching it -- Chromium takes both and not every browser
       does. */
    document.body.append(link);
    link.click();
    link.remove();

    setTimeout(() => URL.revokeObjectURL(url), 0);

    /* Saved, as far as anything here can tell -- which is exactly as far as
       the desktop can tell, since neither of them watches the file
       afterwards. The mark goes out. */
    synth.patchSaved(channel, name);
    await showEdited();

    $('status').textContent = `Channel ${channel + 1} saved as ${name}.`;
}

/* The `edited' marks, refreshed from the module.
 *
 * Asked rather than remembered: an edit can arrive from a piece's knob
 * wired to a chanarg as readily as from somebody typing, and what the row
 * must agree with is the slot the module keeps, not a guess the page made
 * when it last drew itself.
 *
 * Only the channels that have a mark, which is the channels the page aimed:
 * one the piece filled is the piece's and has no file behind it to have
 * parted company with. */
async function showEdited ()
{
    if (synth === null)
        return;

    for (const mark of $('channels').querySelectorAll('.edited'))
    {
        const channel = Number(mark.dataset.channel);
        const { json } = await synth.patchState(channel);
        const on = json !== '';

        mark.hidden = !on || !JSON.parse(json).dirty;

        /* Save is offered for anything that is actually on a channel, not
           only for something edited: somebody may want the file for a patch
           they chose and left alone. */
        const save = $('channels').querySelector(
            `.save[data-channel="${channel}"]`);

        if (save !== null)
            save.hidden = !on;
    }
}

/* The menu for one aimable channel: the graphs, under the drawers the
 * catalog files them in.
 *
 * Graphs and not patches, and that is the whole of the altitude question
 * this menu used to get wrong. It offered seventy-seven .patch files and
 * then sixty .dsp files in one list, which is two kinds of thing in a row
 * with nothing to say which is which: a person scrolling it met
 * `AcidBass', `FatRip' and `TranceSeq' and then, without warning,
 * `bass.dsp' -- one of which is what those three are made of.
 *
 * So the two questions are asked at the two altitudes they belong to.
 * What instrument is this? -- a graph, here. Which of the ones somebody
 * already made do you want? -- a patch, in the panel over that graph's
 * own knobs, where the values a patch *is* are what is in front of you.
 *
 * What it opens on is the graph that is on the channel now, whether it
 * got there from a patch, from a piece or from this menu; a channel with
 * nothing on it says so instead.
 */
function chooser (channel)
{
    const sel = document.createElement('select');
    const here = placed.get(channel);

    sel.setAttribute('aria-label', `Channel ${channel + 1}`);

    if (here === undefined)
        sel.add(new Option('nothing yet', '', true, true));

    if (dspGroups.length > 0)
        fillCatalog(sel, here?.dsp);
    else
    {
        const dsps = document.createElement('optgroup');

        dsps.label = 'dsp';

        for (const name of playableDsps())
            dsps.append(new Option(name, name, false, name === here?.dsp));

        sel.append(dsps);
    }

    if (here !== undefined)
        sel.value = here.dsp;

    sel.addEventListener('change', () => aimByHand(channel, sel.value));

    return sel;
}

/* The patches that are for the graph on this channel: a menu of presets
 * over what is already there, rather than a second way to choose an
 * instrument.
 *
 * Which is what a .patch has always been -- `dsp ts1.dsp' and a column of
 * values under it -- and what the page had no way to say. Empty, and not
 * offered at all, for a graph nobody has saved a patch for, which is most
 * of the corpus and every graph somebody writes themselves.
 */
function presets (channel)
{
    const here = placed.get(channel);

    if (here === undefined)
        return [];

    return patchInfo.filter((p) => p.dsp === here.dsp);
}

/* Somebody chose. It goes on the channel now, and it stays theirs for the
   rest of the session: the next piece that names this channel gets this
   and not the default (patch.js, aim). */
async function aimByHand (channel, name)
{
    if (name === '')
        return;

    try
    {
        const what = await quietly(
            () => patch.place(synth, channel, name));

        /* Remembered once it is actually on the channel. A choice that
           did not load is not a choice to repeat at every load of every
           piece that names this channel for the rest of the session. */
        aimed.set(channel, name);
        placed.set(channel, what);
        $('status').textContent =
            `${what.title} on channel ${channel + 1}. Play.`;

        /* And every menu that says what is on a channel, since a choice
           made in one of them is a choice the others are showing too: the
           channels row and the track headings both draw from `placed',
           and the panel's presets are the ones for the graph that is
           there now. The row is then drawn from the slot rather than
           from any of this -- the mark belongs to the patch on the
           channel now, and Save is offered for anything that loaded. */
        showChannels();
        showPresets();
        await showEdited();
    }
    catch (e)
    {
        $('status').textContent =
            `Channel ${channel + 1}: ${e.message}; see below.`;
        log(`channel ${channel + 1}: ${e.message}`);
        seeBelow();

        /* The menu is showing something that is not there; the row is
           drawn from what is. */
        showChannels();
    }

    /* What is on the channel decides what the panel has rows for. */
    await showParams();
}

function frame ()
{
    if (composing())
        roll.draw();

    requestAnimationFrame(frame);
}

/* ---- the instrument's parameters ---- */

/* The panel that is up: which channel it is over, what the module said it
   was, and the function showPanel handed back for putting a value into one
   of its rows. */
let params = null;

/* Which channels have a panel worth offering.
 *
 * Patch mode has the one it plays. Piece mode has every channel the piece
 * touches, which is the row of them showChannels draws -- a person who has
 * just aimed a .patch at channel 4 is the person who wants its cutoff. */
function paramChannels ()
{
    return mode() === 'patch' ? [PATCH_CHANNEL] : touched();
}

/* The channel selector, kept to what is there, keeping its choice if that
   channel is still among them. Numbered the way the rest of the page
   numbers channels: the file's 1-16, not the engine's 0-15. */
function fillParamChannels ()
{
    const want = Number($('paramchan').value);
    const all = paramChannels();

    $('paramchan').replaceChildren(
        ...all.map((c) => new Option(String(c + 1), String(c))));

    $('paramchan').value = String(all.includes(want) ? want : (all[0] ?? 0));
}

function paramChannel ()
{
    return Number($('paramchan').value);
}

/* The panel, from the module, drawn.
 *
 * Everything about what a row is comes over in the JSON; nothing here
 * decides any of it. An edit leaves as a command and is applied by every
 * instance including this page's, so the number in the box moves because
 * the module set the arg and not because the box was typed in -- which is
 * the same path a peer's edit takes, and the reason there is only one. */
/* The preset row: the patches for the graph on the panel's channel, with
 * the one that is on it selected if a patch is what put it there.
 *
 * Drawn from `placed', which is what the page actually put on the
 * channel, so it follows a piece's own instrument as readily as a choice
 * of somebody's -- and hidden outright where there is nothing to offer,
 * because a menu with one entry that says "as it is" is a control that
 * does nothing.
 */
function showPresets ()
{
    const row = $('presetrow');
    const sel = $('parampatch');
    const channel = paramChannel();
    const mine = presets(channel);

    sel.replaceChildren();
    row.hidden = mine.length === 0;

    if (row.hidden)
        return;

    const here = placed.get(channel);
    const bare = !here?.patch;

    /* The graph's own values, which is where a channel starts before any
       patch is over it and what there has to be a way back to. */
    sel.add(new Option('as the graph says', '', bare, bare));

    for (const p of mine)
    {
        /* Called what the file is called, because two patches in this
           corpus give themselves the same title and a menu with two
           identical rows in it is a menu with a coin toss in it. The
           title is what the row says on hover, where a repeat costs
           nothing. */
        const option = new Option(
            p.name.split('/').pop().replace(/\.patch$/, ''),
            p.name, p.name === here?.patch, p.name === here?.patch);

        option.title = p.title ?? '';
        sel.add(option);
    }
}

/* One chosen. It goes on the channel through the same call every other
   choice takes, and it is remembered as this channel's for the session
   the same way -- a patch is a choice of instrument as much as a graph
   is, and a piece that reloads afterwards must not undo it. */
async function pickPreset ()
{
    const channel = paramChannel();
    const name = $('parampatch').value;

    /* The first entry is the graph with nothing over it, and choosing it
       is loading that graph again -- the way back from a patch, which a
       menu that only went forwards would not have. */
    await aimByHand(channel, name || placed.get(channel)?.dsp || '');
}

async function showParams ()
{
    if (synth === null)
        return;

    fillParamChannels();

    const channel = paramChannel();
    const answer = await synth.panel(0 /* thPanel::CHANARG */, channel);

    params = null;

    if (answer.shape === 0)
    {
        $('params').replaceChildren();
        $('paramwhat').textContent =
            'Nothing on this channel yet; load an instrument.';

        return;
    }

    const panel = JSON.parse(answer.json);

    $('paramwhat').textContent =
        `${panel.rows.length} parameters on channel ${channel + 1}`;

    const setValue = showPanel(
        $('params'), panel,
        async (row, text) =>
        {
            await synth.panelEdit(0, channel, 0, row, text);

            /* Moving a control is editing the patch, and the row above says
               so. Here rather than in the poll because an edit is a thing
               that happened once and a poll is a thing that runs for ever. */
            await showEdited();
        });

    params = { channel, panel, setValue };

    showPresets();
}

/* The panel following the arg.
 *
 * A control moves behind the page all the time -- a piece's knob wired to
 * a chanarg, a peer's edit, this page's own edit coming back round -- and a
 * panel that did not follow would show what was true when it opened. The
 * poll is the values alone: rows are described once and their numbers are
 * read as often as it takes, which is what a panel's shape is for.
 *
 * A shape that has changed means the rows themselves did -- something was
 * loaded onto the channel -- and the answer to that is to ask for the
 * description again rather than to push values into widgets for a panel
 * that has gone.
 */
async function pollParams ()
{
    /* Only while the panel is in front of somebody: folded away, in a
       background tab, or in the mode that is not up, its rows are a
       message each way per quarter second for nobody. panes.js answers
       all three the same way. */
    if (synth === null || params === null || !panes.visible('paramview'))
        return;

    /* Which panel this poll is about, held across the await.
     *
     * showParams() replaces `params' wholesale, and a channel switched while
     * a poll was in flight brings back the values of the channel that was
     * there before -- as many of them as that panel had rows. The shape is
     * not the check for that: two channels carrying the same patch have the
     * same shape, which is what a shape is for, so the answer would be
     * applied to the wrong channel's controls and a short one would blank the
     * rows past its end. The object's identity is the check. */
    const asked = params;

    const answer = await synth.panelValues(asked.panel.rows.length);

    if (params !== asked)
        return;

    if (answer.shape !== asked.panel.shape)
    {
        await showParams();
        return;
    }

    /* Values only. A row that holds words -- a note set, an output described
       rather than measured -- is told nothing by a number, and panel.js is
       where that is known; passing the description's own text back in every
       quarter second is how a readout came to be frozen at what it said when
       the panel opened. */
    asked.panel.rows.forEach((row, i) =>
        asked.setValue(row.id, answer.values[i]));
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
                                         onTape: (m) =>
                                         {
                                             diff.take('worklet', m);
                                             roll.tape(m);

                                             /* What a probe armed here
                                                is watching, as jam.js
                                                feeds it: without this
                                                the scope stays blank. */
                                             nodes?.feed(m.probes);
                                         },
                                         onMirror: fromMirror });
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
       since nothing here waits on anything else.

       Through a helper that looks at the status, because fetch does not:
       a 404 is a response like any other and `text()' and `arrayBuffer()'
       both resolve on one. Taken at face value that hands the module the
       error page -- a graph that will not parse, or bytes that are not a
       RIFF -- and the failure surfaces as an instrument that makes no
       sound rather than as a file that is not on the server. A dist that
       shipped dsp/index.json without dsp/samples/ is what that looked
       like: the drums silently stopped and the synthesized voices played
       on. */
    /* Declared out here because dspTexts below is built from them. */
    const graphs = textDsps();
    let texts;

    try
    {
        texts = await Promise.all(
            graphs.map((name) => served(name).then((r) => r.text())));

        graphs.forEach((name, i) => synth.instrument(name, texts[i]));

        /* And the kit, as bytes. osc::sample looks a file up on the same
           path a .dsp is looked up on, so a wav has to be in the worklet's
           MEMFS before the first note that plays one -- and a worklet can no
           more fetch a wav than it can fetch a graph. */
        const kit = sampleNames();
        const wavs = await Promise.all(
            kit.map((name) => served(name).then((r) => r.arrayBuffer())));

        kit.forEach((name, i) => synth.sample(name, new Uint8Array(wavs[i])));
    }
    catch (e)
    {
        /* The same teardown the start above does, and for the same reason:
           by here the context holds the audio device and a worklet, and a
           browser allows only so many. A file that 404s is exactly the
           failure somebody retries -- fix the server, press Start again --
           so this is the path that would leak one context per attempt. */
        if (ctx !== null)
            ctx.close().catch(() => {});

        ctx = null;
        synth = null;

        $('status').textContent = `Could not start: ${e.message}`;
        log(e.message);
        seeBelow();
        $('start').disabled = false;
        return;
    }

    /* And kept, because they are also what a .patch's `dsp' line is
       resolved against: patch.js fetches the .patch and no more, since
       these are already here. */
    dspTexts = Object.fromEntries(graphs.map((name, i) => [name, texts[i]]));

    /* And what they say about themselves, now that the module has them: the
       module scans the same MEMFS copy it was just handed, with the same
       class the desktop's chooser uses. Until here the menu shows filenames,
       because reading a header takes the module and the module takes a Start;
       from here it shows what each graph is called.

       The choice is kept across the refill -- somebody who picked a patch
       before pressing Start picked it. */
    try
    {
        dspGroups = (await synth.dsps()).catalog?.groups ?? [];
    }
    catch (e)
    {
        /* A menu that shows filenames is the menu that was there a moment
           ago, so this costs the titles and nothing else. */
        log(`dsp catalog: ${e.message}`);
    }

    if (dspGroups.length > 0)
    {
        const chosen = $('patch').value;

        $('patch').replaceChildren();
        fillCatalog($('patch'), chosen);
    }

    /* And the pieces, the same way round: the module is handed each one's
       text and reads the header, because a second reading here in
       JavaScript is the thing the .patch format taught. Nothing plays from
       these -- a piece is played by handing loadPiece its text -- so a
       failure costs the sections and nothing else. */
    try
    {
        const texts = await Promise.all(
            genNames.map((name) => fetch(`gen/${name}`).then((r) =>
            {
                if (!r.ok)
                    throw new Error(`gen/${name}: ${r.status}`);

                return r.text();
            })));

        genNames.forEach((name, i) => synth.piecefile(name, texts[i]));

        genGroups = (await synth.gens()).catalog?.groups ?? [];
    }
    catch (e)
    {
        log(`piece catalog: ${e.message}`);
    }

    if (genGroups.length > 0)
    {
        const chosen = $('piece').value;

        $('piece').replaceChildren();
        fillPieces($('piece'), chosen);
    }

    /* And the default patches, before anything needs one. The aiming runs
       inside quietly(), with the context suspended, and patchText fetches
       on first use -- so without this the first piece load holds the audio
       device down for a round trip per default. Failures are not reported
       here: a default that cannot be fetched is reported by the load that
       wanted it, which is where it means something. */
    await Promise.all(
        (await patch.defaultNames(synth))
            .map((name) => patch.patchText(name).catch(() => {})));

    /* And the button that started it goes. A synth is started once, and
       a control that can no longer do anything is a line of chrome
       charged to every layout for the rest of the session. The failures
       above leave it where it is and enabled again, since those are the
       ones somebody retries. */
    $('start').hidden = true;
    $('load').disabled = false;
    $('loadpiece').disabled = false;

    /* What the shipped patches are presets over, on its way. Not awaited:
       nothing on screen is waiting for it, and the panel that offers them
       draws itself again when it lands. */
    readPatches();

    /* The view before the load, not after. A load is answered by the
       mirror with a `piece' message, and fromMirror has nowhere to put
       one while composer is still null -- so made afterwards, the first
       Start went by with the message dropped and the "Paint ..." buttons
       never appeared. */
    showComposer(panes.visible('composerview') && mode() === 'piece');
    showSeq(panes.visible('seqview') && composing());

    if (mode() === 'seq')
        await loadSequence();
    else if (mode() === 'patch')
        await loadPatch();
    else
        await loadPiece();

    try
    {
        nodes = await createNodeView({
            files: nodeFiles,
            onStatus: (text) => { $('status').textContent = text; },
            sampleRate: ctx.sampleRate,
            probe: (channel, node, arg) => synth.probe(channel, node, arg),
            unprobe: (slot) => synth.unprobe(slot),
        });

        showNodes();
        $('nodefile').addEventListener('change',
                                       () => nodes.onChannel(nodeChannel()));
    }
    catch (e)
    {
        log(`the instrument's graph did not start: ${e.message}`);
    }

    showLatency();
    setInterval(showLatency, 500);
}

/* ---- the composer view ---- */

/* Everything the mirror says. The view takes the frames it draws, the
   piece it loaded and the gestures its canvas wants sent; the tape is
   held against the worklet's here, and anything else is a line in the
   log. */
function fromMirror (m)
{
    if (seq !== null && seq.fromMirror(m))
        return;

    if (composer !== null && composer.fromMirror(m))
        return;

    if (m.type === 'patchinfo')
    {
        patchInfo = m.items;

        /* The panel was drawn before there was a list; the row that shows
           it is empty until this lands, so it is drawn again now. */
        showPresets();
        return;
    }

    if (m.type === 'tape')
        diff.take('mirror', m);
    else if (m.type === 'log')
        log(m.text);
}

/* ---- the instrument as a graph ----
 *
 * The room page's edits are splices into a shared document; this page has
 * no document, so the files are whatever it is playing. In patch mode
 * that is the .dsp in the text box, and an edit reloads it -- the canvas
 * and the box are two views of one text, which is what the desktop's
 * editor is too. In piece mode they are the piece's instruments, and an
 * edit is heard at the next Load, which is what the .gen box already
 * means here.
 *
 * And the telling is done here too. The room page has a document, and its
 * observer is what tells the node editor that a file it is showing has
 * moved -- including when the move was the editor's own, since an edit
 * there is a splice that comes back round. With no document there is no
 * observer, and without one a wire cut on the canvas rewrote the text and
 * the canvas went on drawing the graph from before the cut.
 */

/* Who wants to hear that a file changed, by name. */
const nodeWatchers = new Map();

function nodeFileChanged (name)
{
    for (const onChange of [...(nodeWatchers.get(name) ?? [])])
        onChange();
}

const nodeFiles = {
    names: () =>
    {
        if (mode() === 'patch')
            return $('patch').value === '' ? [] : [$('patch').value];

        /* The .dsp each of the piece's instruments plays: what there is
           to edit while a piece is loaded. */
        return [...new Set((piece?.instruments ?? [])
            .map((i) => i.dsp)
            .filter((n) => n !== '' && dspTexts[n] !== undefined))];
    },

    read: (name) => (mode() === 'patch' ? $('dsp').value
                                        : dspTexts[name] ?? ''),

    write: (name, next) =>
    {
        if (mode() === 'patch')
        {
            /* Assigning to .value fires nothing, so the `input' listener
               below -- the only thing watching in patch mode -- never
               hears an edit the canvas made. Told directly instead. */
            $('dsp').value = next;
            loadPatch();
            nodeFileChanged(name);
            return;
        }

        /* The worklet resolves an instrument by name against what it was
           handed, so the new text goes over there before anything can
           play it -- and the piece picks it up at the next load. */
        dspTexts[name] = next;
        synth?.instrument(name, next);
        $('status').textContent =
            `${name} changed. Load the piece again to hear it.`;
        nodeFileChanged(name);
    },

    /* The text box is the other view of the same patch, so typing in it
       rebuilds the canvas -- and so does an edit made on the canvas,
       which arrives through nodeFileChanged rather than through an event
       the box never raises. Piece mode has only the second, which is why
       it is watched here too and not only in patch mode. */
    watch: (name, onChange) =>
    {
        let who = nodeWatchers.get(name);

        if (who === undefined)
            nodeWatchers.set(name, who = new Set());

        who.add(onChange);

        if (mode() === 'patch')
            $('dsp').addEventListener('input', onChange);

        return () =>
        {
            who.delete(onChange);
            $('dsp').removeEventListener('input', onChange);
        };
    },
};

/* Which channel what the canvas is showing is playing on, and so which
   one a probe is armed on: patch mode has one, and in piece mode the
   piece says. */
function nodeChannel ()
{
    if (mode() === 'patch')
        return PATCH_CHANNEL;

    return piece?.instruments?.find(
        (i) => i.dsp === $('nodefile').value)?.channel ?? -1;
}

function showNodes ()
{
    if (nodes === null)
        return;

    nodes.offer(nodeFiles.names());
    nodes.onChannel(nodeChannel());
    nodes.show(panes.visible('nodeview'));
}

function showComposer (on)
{
    /* Made when it is first wanted and never for a pane nobody has
       looked at: onShow says `no' for every pane at load, and a view
       built to be told that would have started a worker for nothing. */
    if (composer === null && (!on || synth === null))
        return;

    /* On a solo page there are no peers and no lead to wait out, so a
       gesture is stamped for the next window, as this page's knobs are.
       It still goes the long way round -- out as a command, back in at
       its time -- because that is the one path a piece is composed
       from. */
    composer ??= createComposerView({
        toMirror: (m) => synth?.toMirror(m),
        onGesture: (g) => synth?.input({ ...g, at: -1 }),
    });

    composer.show(on);
}

/* What is on a channel, in the words a track's heading wants: the name
 * the piece gave its own instrument, or the file somebody aimed there by
 * hand, or nothing -- a channel with nothing on it says so by saying the
 * chain's name instead, which is at least what the track is called. */
function describeChannel (channel)
{
    if (channel < 0)
        return '';

    const inst = piece?.instruments?.find((i) => i.channel === channel);

    if (inst !== undefined)
        return inst.name;

    return aimed.get(channel) ?? '';
}

function showSeq (on)
{
    /* Made when it is first wanted, for the reason showComposer is. */
    if (seq === null && (!on || synth === null))
        return;

    seq ??= createSeqView({
        toMirror: (m) => synth?.toMirror(m),
        onGesture: (g) => synth?.input({ ...g, at: -1 }),
        describeChannel,

        /* The same menu the channels list draws, on the track it is
           about -- and by the same rule: a channel the piece filled is
           the piece's and has nothing to choose. */
        chooserFor: (channel) =>
            (channel < 0 || !composing() ||
             piece?.instruments?.some((i) => i.channel === channel))
                ? null : chooser(channel),
    });

    seq.show(on);
}

/* For pagetest: where a stage's params handle is, and what the popover
   ended up showing. The layout is the canvas's, so asking it is the only
   honest way to press one. */
window.solo = {
    handleOf: (chain, stage) => composer?.handleOf(chain, stage),
    params: () => composer?.params() ?? [],

    /* The tracks the sequencer pane ended up with: which stage each row
       is and how tall the grid behind it said to be. */
    tracks: () => seq?.tracks() ?? [],

    /* Every load asked for so far, finished -- including the redraw each
       one ends with.
     *
       A harness about to press on a slider has to know that no load is
       still on its way to throwing that slider away: drawKnobs replaces
       every element in the row, and a load landing between a focus and a
       keypress leaves the key going to <body>. Counting the loads from
       outside is what pagetest used to do, and it got the count wrong --
       choosing a piece is one, the mode switch before it is another, and
       Load is a third. The page is what knows, so it is what is asked.

       The loop is for a load queued while an earlier one was being
       waited on: quiet is reassigned by every quietly(), so it is stable
       only once nothing has been added across an await and a frame. And
       `drawn' beside it, because the panels a load redraws are asked of the
       worklet and land after the load that quiet tracks is finished. */
    settled: async () =>
    {
        for (let was = null, drew = null; was !== quiet || drew !== drawn; )
        {
            was = quiet;
            drew = drawn;

            await quiet;
            await drawn.catch(() => {});
            await new Promise((go) => requestAnimationFrame(go));
        }

        return true;
    },

    /* The instrument's parameters: what the module said they are, so a
       harness can check a row against the description it was drawn from
       rather than against a number written twice. `params' above is the
       composer's popover, which is a different panel over a different
       thing. */
    chanParams: () => (params === null ? null : params.panel),

    /* And a poll on demand, since the timer's quarter second is a long
       time to wait on and longer still to guess at. */
    pollChanParams: () => pollParams(),

    /* The panes this page has, so a harness reads the catalog rather
       than writing the list down a second time, and the layout they are
       in, which is what a drag has to be checked against. */
    panes: () => PANES,
    layout: () => panes.layout(),

    /* And the four verbs a pane has: raise one, put one away, rename
       one. What a top-level window would become if the desktop's shell
       were ever compiled for this page. */
    pane: (what, ...args) => panes[what](...args),

    /* Which of the drawing panes is asking for frames. A pane in a
       background tab, folded away or in the mode that is not up costs
       nothing, and this is the only way to see from outside that it
       really costs nothing. */
    drawing: () => ({ composer: composer?.visible() ?? false,
                      nodes: nodes?.visible() ?? false,
                      seq: seq?.visible() ?? false }),

    /* The instrument's graph: where its boxes are, so a harness can press
       on one rather than at a guess, and what it has selected. */
    node: () => (nodes === null ? null : {
        boxes: nodes.boxes(),
        selected: nodes.selected(),
        probes: nodes.probes(),
        box: (i) => nodes.boxAt(i),
    }),
};

async function pickMode ()
{
    const which = mode();

    /* The chrome each mode has: what to play, and the transport. What
       the piece section used to wrap are panes of their own now, and a
       pane the mode does not have is unavailable rather than hidden --
       it leaves the layout without being forgotten by it, so coming back
       to a mode puts its panes where they were. */
    $('patchmode').hidden = which !== 'patch';
    $('piecemode').hidden = which !== 'piece';
    $('transport').hidden = which === 'patch';
    showAbout();

    const mine = new Set(modePanes[which] ?? []);

    for (const id of new Set([...SEQ_PANES, ...PIECE_PANES, ...PATCH_PANES]))
        panes.available(id, mine.has(id));

    panes.mode(which);

    if (synth === null)
        return;

    showComposer(panes.visible('composerview') && which === 'piece');
    showSeq(panes.visible('seqview') && composing());
    showNodes();

    /* Emptied rather than left showing the other mode's channel: what
       loadPatch and loadPiece do below is fill it again from what they
       put on the channels. */
    params = null;
    $('params').replaceChildren();

    releaseAll();

    /* Each mode loads what it plays as it is entered: the last one's is
       still on the channels until it does. */
    if (which === 'seq')
        await loadSequence();
    else if (which === 'piece')
        await loadPiece();
    else
    {
        synth.transport('stop');
        piece = null;
        placed = new Map();
        showChannels();
        await loadPatch();
    }
}

/* The shipped .dsp files, as start() hands them to the worklet, and their
   texts once it has: a piece's instruments are looked up among these and
   so is the .dsp a .patch names. */
let dspNames = [];
let dspTexts = {};

/* The same graphs, as the module reads their headers: the groups a menu
   draws, each entry with the title and the description its author wrote
   (src/DspCatalog.h). Empty until the module is up and has been handed the
   texts, which is why the menus are filled twice -- filenames before Start,
   titles after it. A second reading here in JavaScript is exactly what the
   .patch format's two parsers were, so there is not one. */
let dspGroups = [];

/* The shipped pieces, and the same for them: the sections gen/README.md
   groups the corpus into, which are in the files (a `category' statement) and
   are what the Composer's Open groups by too. */
let genNames = [];
let genGroups = [];

/* The ones among them that play a note.
 *
 * The index carries the effect graphs as well, as `fx/<name>', because the
 * worklet has to be handed every file a piece may name -- but an effect has
 * no envelope and nothing to trigger it, and putting one on a channel as an
 * instrument leaves an ungated graph running for as long as it is loaded.
 * Every menu that picks an instrument asks for this list rather than the
 * index. */
function playableDsps ()
{
    return dspNames.filter((n) => !n.startsWith('fx/') &&
                                  !n.startsWith('samples/'));
}

/* The index carries the kit too, as `samples/<name>' -- the wavs
   osc::sample plays. They are not text, are not graphs and are not
   playable, so everything above filters them out and start() below
   fetches them as bytes instead. */
function sampleNames ()
{
    return dspNames.filter((n) => n.startsWith('samples/'));
}

function textDsps ()
{
    return dspNames.filter((n) => !n.startsWith('samples/'));
}

/* One of the names above, off the server, with the status looked at.
   fetch rejects when the request could not be made and not when the
   answer was a 404 -- so without this a name the index carries and the
   site does not ship comes back as the error page's bytes, and what
   fails is the instrument rather than the fetch. */
async function served (name)
{
    const r = await fetch(`dsp/${name}`);

    if (!r.ok)
        throw new Error(`dsp/${name}: ${r.status} ${r.statusText}`);

    return r;
}

/* The shipped .patch files by relative name, `leads/SuperRes.patch' --
   the names the desktop's thinkrc uses. */
let patchNames = [];

/* And what each of them says it is: the graph it is a preset over, and
 * the title its author gave it.
 *
 * Read by the module, once, from the texts this fetches -- the format has
 * one reading and it is in C++ (src/PatchFile.h). Until the answer comes
 * back this is empty, and a panel drawn before then offers no presets,
 * which is right: it does not know of any yet. */
let patchInfo = [];

/* Every shipped patch, fetched and read. Started after Start and never
 * awaited by anything somebody is waiting on: the panel that wants it is
 * behind at least one click, and a menu that fills in a moment later is
 * better than a page that opens a moment later.
 */
async function readPatches ()
{
    const items = [];

    await Promise.all(patchNames.map(async (name) =>
    {
        try
        {
            items.push({ name, text: await patch.patchText(name) });
        }
        catch
        {
            /* One that the server will not serve is one menu entry
               missing rather than a reason for the rest to be. */
        }
    }));

    synth?.toMirror({ type: 'patchinfo', items });
}

function fill (select, names, preferred)
{
    for (const name of names)
        select.add(new Option(name, name, name === preferred,
                              name === preferred));
}

/* A piece menu out of the catalog: an optgroup per section, the title each
 * piece declares, its description as the tooltip. The value stays the
 * filename, because that is what the page fetches and what index.json
 * lists. */
function fillPieces (select, preferred)
{
    for (const group of genGroups)
    {
        if (group.entries.length === 0)
            continue;

        const optgroup = document.createElement('optgroup');

        optgroup.label = group.name;

        for (const e of group.entries)
        {
            const option = new Option(e.name, e.file, e.file === preferred,
                                      e.file === preferred);

            option.title = e.desc;
            optgroup.append(option);
        }

        select.append(optgroup);
    }
}

/* An instrument menu out of the catalog: an optgroup per group, a row per
 * graph with the title it declares and its description as the tooltip.
 *
 * The value stays the filename, because that is what everything downstream
 * asks for -- dspTexts is keyed on it, a .patch's `dsp' line says it, and a
 * piece's instrument names it. What changes is only what a person reads.
 *
 * The effect graphs are skipped for the reason playableDsps() skips them: an
 * effect has no envelope and nothing to trigger it, so putting one on a
 * channel as an instrument leaves an ungated graph running for as long as it
 * is loaded. Which graphs those are is the module's answer now rather than a
 * guess at the `fx/' prefix. */
function fillCatalog (select, preferred)
{
    for (const group of dspGroups)
    {
        const rows = group.entries.filter((e) => !e.effect);

        if (rows.length === 0)
            continue;

        const optgroup = document.createElement('optgroup');

        optgroup.label = group.name;

        for (const e of rows)
        {
            const option = new Option(e.name, e.file, e.file === preferred,
                                      e.file === preferred);

            option.title = e.desc;
            optgroup.append(option);
        }

        select.append(optgroup);
    }
}

async function init ()
{
    const [dsps, gens, patchList] = await Promise.all([
        ...['dsp', 'gen'].map((d) => fetch(`${d}/index.json`)
                                         .then((r) => r.json())),
        patch.index(),
    ]);

    dspNames = dsps;
    genNames = gens;
    patchNames = patchList;
    fill($('patch'), playableDsps(), 'ts1.dsp');
    fill($('piece'), gens, 'ebb.gen');

    [$('dsp').value, $('gen').value] = await Promise.all([
        fetch(`dsp/${$('patch').value}`).then((r) => r.text()),
        fetch(`gen/${$('piece').value}`).then((r) => r.text()),
    ]);

    /* Sixteen is all there are, counted the way the file counts them and
       the way the channels row does -- the engine's number is one lower
       and is what the option carries, since that is what a chain's `input
       midi' is matched against. So the file's channel 1 is where a piece's
       first instrument lands, and where hands.gen's arpeggiator listens. */
    for (let c = 0; c < 16; c++)
        $('keychan').add(new Option(String(c + 1), c, c === 0, c === 0));

    roll = new Roll($('roll'), $('clock'));
    keyboard = new Keyboard($('keys'),
                            { onPress: press, onRelease: release });
    /* Who has the keyboard, and the one line on the page that says so.
       Escape hands it back, and lets go of anything it was holding on
       the way -- a note whose key-up is about to land somewhere else. */
    keyfocus = createKeyFocus({ indicator: $('keysstate'),
                                onRelease: releaseAll });
    keys = new TypingKeys({ press, release, shifted, focus: keyfocus,
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

    /* The key channel is one of the channels the row shows, so moving the
       keys moves which line is there. Nothing is loaded by it: where the
       keys go and what a channel sounds like are two questions. */
    $('keychan').addEventListener('change', () =>
    {
        showChannels();
        showParams();
    });

    $('paramchan').addEventListener('change', showParams);
    $('parampatch').addEventListener('change', pickPreset);

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

    /* And the layout, over what is in the document now.
     *
     * onShow is the whole of what tiling asks of this page: a pane in a
     * background tab, folded away, or belonging to the mode that is not
     * up is a pane whose work can stop, and these are the three places
     * this page has work to stop. They are the same calls the folds and
     * the mode switch made before, asked for in one place. */
    panes = createPanes({
        root: $('panes'), catalog: PANES, store: 'panes:solo',
        layouts: { patch: PATCH_LAYOUT, piece: PIECE_LAYOUT,
                   seq: SEQ_LAYOUT },
        mode: mode(), on: true,
        onShow: (id, on) =>
        {
            if (id === 'composerview')
                showComposer(on && mode() === 'piece');
            else if (id === 'seqview')
                showSeq(on && mode() === 'piece');
            else if (id === 'nodeview')
                nodes?.show(on);
            else if (id === 'paramview' && on)
                pollParams();
        },
    });

    /* And the mode the select is showing, applied to what has just been
       built. The markup cannot be the answer: it is one arrangement and
       there are three modes, and the page opens on whichever one the
       select says -- so the chrome and the panes are put right here,
       once, by the same function that puts them right on every change.
       With no synth yet it does nothing else. */
    await pickMode();

    /* Four times a second, which is about the desktop's 50 ms draw timer
       and far below an animation frame: the poll is a message each way,
       and a panel following a knob does not need sixty of them a second.
       Only while the panel is in front of somebody, which is what the
       layout above answers -- so it is started after there is one. */
    setInterval(pollParams, 250);

    /* The two popovers, out of the panes and over them. Each is placed
       beside the box on a canvas that asked for it, in page coordinates,
       and a pane is a box that scrolls -- so a popover left inside one
       would be clipped by it the moment it reached the edge. */
    panes.overlay().append($('composerparams'), $('nodemenu'));

    requestAnimationFrame(frame);
}

init();
