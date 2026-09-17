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
 * `input midi' is matched against.
 *
 * WHAT A CHANNEL SOUNDS LIKE is the piece's to say, and where the piece is
 * silent on it, the defaults' -- never what the page did before. A piece
 * whose sinks name channels is asking the reader to aim them, which nine
 * of the shipped seventeen do, and a page that aimed nothing played them
 * through whatever the last mode had left lying there, or through nothing
 * at all. So a load is followed by the aiming and never preceded by it,
 * and the aiming is a function of the piece and of what somebody chose by
 * hand. patch.js holds the rule, the defaults and the .patch reader;
 * AIMING.md is the whole argument.
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

import { createCanvasView } from './canvasview.js';
import { createSynth } from './host.js';
import { Keyboard, TypingKeys, noteName, showRange } from './keyboard.js';
import { showKnobs } from './knobs.js';
import * as patch from './patch.js';
import { Roll } from './roll.js';

const $ = (id) => document.getElementById(id);

const VELOCITY = 100;

/* Where patch mode puts its one .dsp. thinkweb.cpp's tw_piece_load says
   why a piece takes this channel first, and why the two are modes. */
const PATCH_CHANNEL = 0;

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

/* The composer view: the piece's own picture, drawn by the mirror -- a
   second scheduler in a worker, fed the messages the worklet is fed, with
   real composer instances in it (JAM_M6.md, sections 4 and 6). The page's
   half of it is an element and a pointer; everything else is the same C++
   the desktop draws with. */
let composer = null;

/* The stages whose picture is a control, by "chain.stage", and which one
   is enlarged now. */
let composerStages = new Map();
let enlarged = { chain: -1, stage: -1 };

/* The channels somebody has aimed by hand in this session, as channel ->
   the .patch or .dsp they chose. A piece that loads afterwards keeps
   their aiming and fills only the rest from the defaults, which is what
   makes the aiming a choice rather than a thing to redo after every
   Load. The engine's numbering, as everything held here is. */
const aimed = new Map();

/* What is on each channel now, as patch.js reported putting it there:
   channel -> { patch, dsp, title }, or the piece's own instrument by
   name. Only to draw the row; nothing is decided from it, because
   deciding from what the page did before is the bug (AIMING.md). */
let placed = new Map();

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
        $('detail').open = true;
}

async function pickPatch ()
{
    $('dsp').value = await (await fetch(`dsp/${$('patch').value}`)).text();

    await loadPatch();
}

/* ---- the piece, M2 ---- */

/* The piece, and then the aiming.
 *
 * That order, and it is the whole of the fix: the piece declares what it
 * can, the page fills what the piece left to the reader, and what was on
 * a channel a moment ago decides nothing (AIMING.md, section 4.4). Both
 * inside one quietly(), because the aiming builds graphs on the audio
 * thread exactly as the load does and the context is already down.
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
            aiming = await patch.aim(synth, loaded.sinks, dspTexts, aimed);

        return loaded;
    });

    roll.clear();
    piece = it.errors.length === 0 ? it : null;
    placed = aiming.placed;

    if (piece === null)
    {
        $('status').textContent = 'That .gen did not parse; see below.';
        it.errors.forEach(log);
        $('detail').open = true;
    }
    else if (aiming.failed.length > 0)
    {
        $('status').textContent =
            `Loaded ${piece.name || $('piece').value}, but not everything ` +
            'it asked for; see below.';
        aiming.failed.forEach(log);
        $('detail').open = true;
    }
    else
        $('status').textContent =
            `Loaded ${piece.name || $('piece').value}. Press Play.`;

    $('about').textContent = piece === null ? '' : piece.description;

    for (const id of ['play', 'stop', 'rewind'])
        $(id).disabled = piece === null;

    drawKnobs();
    showChannels();
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
            line.append(chooser(channel));

        box.append(line);
    }
}

/* The menu for one aimable channel: every shipped .patch under the drawer
   it lives in, then every shipped .dsp on its own.
 *
 * A .patch is a .dsp and a preset over its knobs and is what the desktop
 * puts on a channel, so those come first; a bare .dsp is the same thing
 * at whatever values its file declares, which is what patch mode plays,
 * and is offered because a person picking an instrument by ear should not
 * have to find a .patch that happens to wrap the .dsp they wanted.
 *
 * The first entry is what the page has not chosen: for a channel the
 * piece named, the default that was put there, and for one it did not,
 * whatever is already on the channel, which the page has no business
 * naming.
 */
function chooser (channel)
{
    const sel = document.createElement('select');
    const mine = aimed.get(channel);
    const here = placed.get(channel);
    const named = (piece?.sinks ?? []).includes(channel);

    sel.setAttribute('aria-label', `Channel ${channel + 1}`);

    if (mine === undefined)
    {
        const label = !named ? 'as it is'
                    : here === undefined ? 'nothing -- the default failed'
                    : `${here.title} (the default)`;

        sel.add(new Option(label, '', true, true));
    }

    let drawer = null;
    let group = sel;

    for (const name of patchNames)
    {
        const cut = name.lastIndexOf('/');
        const dir = cut < 0 ? '' : name.slice(0, cut);

        if (dir !== drawer)
        {
            drawer = dir;
            group = sel;

            if (dir !== '')
            {
                group = document.createElement('optgroup');
                group.label = dir;
                sel.append(group);
            }
        }

        group.append(new Option(name.slice(cut + 1).replace(/\.patch$/, ''),
                                name, false, name === mine));
    }

    const dsps = document.createElement('optgroup');

    dsps.label = 'dsp';

    for (const name of dspNames)
        dsps.append(new Option(name, name, false, name === mine));

    sel.append(dsps);

    if (mine !== undefined)
        sel.value = mine;

    sel.addEventListener('change', () => aimByHand(channel, sel.value));

    return sel;
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
            () => patch.load(synth, channel, name, dspTexts));

        /* Remembered once it is actually on the channel. A choice that
           did not load is not a choice to repeat at every load of every
           piece that names this channel for the rest of the session. */
        aimed.set(channel, name);
        placed.set(channel, what);
        $('status').textContent =
            `${what.title} on channel ${channel + 1}. Play.`;
    }
    catch (e)
    {
        $('status').textContent =
            `Channel ${channel + 1}: ${e.message}; see below.`;
        log(`channel ${channel + 1}: ${e.message}`);
        $('detail').open = true;

        /* The menu is showing something that is not there; the row is
           drawn from what is. */
        showChannels();
    }
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
                                         onTape: (m) => roll.tape(m),
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
       since nothing here waits on anything else. */
    const texts = await Promise.all(
        dspNames.map((name) => fetch(`dsp/${name}`).then((r) => r.text())));

    dspNames.forEach((name, i) => synth.instrument(name, texts[i]));

    /* And kept, because they are also what a .patch's `dsp' line is
       resolved against: patch.js fetches the .patch and no more, since
       these are already here. */
    dspTexts = Object.fromEntries(dspNames.map((name, i) => [name, texts[i]]));

    /* And the default patches, before anything needs one. The aiming runs
       inside quietly(), with the context suspended, and patchText fetches
       on first use -- so without this the first piece load holds the audio
       device down for a round trip per default. Failures are not reported
       here: a default that cannot be fetched is reported by the load that
       wanted it, which is where it means something. */
    await Promise.all(
        patch.DEFAULTS.map((name) => patch.patchText(name).catch(() => {})));

    $('load').disabled = false;
    $('loadpiece').disabled = false;

    if (mode() === 'patch')
        await loadPatch();
    else
        await loadPiece();

    showComposer(mode() === 'piece');

    showLatency();
    setInterval(showLatency, 500);
}

/* ---- the composer view ---- */

/* Everything the mirror says. Three kinds: a drawn frame to replay, a
   gesture the canvas wants sent as a command, and the tape it composed --
   which is held against the worklet's elsewhere. */
function fromMirror (m)
{
    switch (m.type)
    {
        case 'draw':
            composer?.frame(m);

            if (m.enlarged.chain !== enlarged.chain ||
                m.enlarged.stage !== enlarged.stage)
            {
                enlarged = m.enlarged;
                showComposerStatus(enlarged);
            }

            break;

        /* A piece loaded in the mirror: which chains it has, and which of
           their pictures are controls. */
        case 'piece':
            showComposerStages(m.chains);
            break;

        /* A press, drag or release the canvas took on an enlarged
           picture, already in the coordinates that picture was drawn in.
           It goes out as a command like a knob, is applied at its time,
           and reaches this page's own worklet and mirror on the way --
           which is why what was painted appears a moment later rather
           than at once (JAM_M6.md, section 5).
         *
           On a solo page there are no peers and no lead to wait out, so
           it is stamped for the next window, as this page's knobs are. */
        case 'input':
            synth?.input({ ...m, at: -1 });
            break;

        case 'log':
            log(m.text);
            break;
    }
}

/* The stages whose picture is a control, as buttons that enlarge one, and
   a line saying what is enlarged now. A double-click on the canvas does
   the same thing, and Escape puts it back; this is that for a finger, and
   it is also how anybody finds out that a picture can be painted on. */
function showComposerStages (chains)
{
    const row = $('composerstages');

    row.replaceChildren();
    composerStages = new Map();

    for (const chain of chains)
        for (const stage of chain.stages)
        {
            if (!stage.takesInput)
                continue;

            const button = document.createElement('button');

            composerStages.set(`${chain.chain}.${stage.stage}`,
                               `${stage.name} in ${chain.name}`);

            button.textContent = `Paint ${stage.name} in ${chain.name}`;
            button.addEventListener('click', () => synth?.toMirror(
                { type: 'enlarge', chain: chain.chain, stage: stage.stage }));
            row.append(button);
        }

    showComposerStatus({ chain: -1, stage: -1 });
}

function showComposerStatus (enlarged)
{
    const which = composerStages.get(`${enlarged.chain}.${enlarged.stage}`);

    $('composerstatus').textContent = which !== undefined
        ? `Painting ${which}. Drag on it; Escape puts it back. What you ` +
          'paint goes out as a command and arrives at its time, here as ' +
          'on every peer.'
        : composerStages.size > 0
            ? 'Double-click a picture that is a control to enlarge it.'
            : '';
}

function showComposer (on)
{
    if (composer === null)
        composer = createCanvasView({
            scroller: $('composerscroll'),
            canvas: $('composer'),
            send: (m) => synth?.toMirror(m),
        });

    composer.show(on && !$('composerview').hidden &&
                  $('composerview').open);
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

    showComposer(piecing);

    releaseAll();

    /* Each mode loads what it plays as it is entered: the other one's is
       still on the channels until it does. */
    if (piecing)
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

/* The shipped .patch files by relative name, `leads/SuperRes.patch' --
   the names the desktop's thinkrc uses -- for the channels row's menus. */
let patchNames = [];

function fill (select, names, preferred)
{
    for (const name of names)
        select.add(new Option(name, name, name === preferred,
                              name === preferred));
}

async function init ()
{
    const [dsps, gens, patchList] = await Promise.all([
        ...['dsp', 'gen'].map((d) => fetch(`${d}/index.json`)
                                         .then((r) => r.json())),
        patch.index(),
    ]);

    dspNames = dsps;
    patchNames = patchList;
    fill($('patch'), dsps, 'ts1.dsp');
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

    /* The key channel is one of the channels the row shows, so moving the
       keys moves which line is there. Nothing is loaded by it: where the
       keys go and what a channel sounds like are two questions. */
    $('keychan').addEventListener('change', showChannels);

    /* Folded away, the view stops asking for frames: a picture nobody is
       looking at is a piece's worth of drawing per animation frame for
       nobody. */
    $('composerview').addEventListener(
        'toggle', () => showComposer(mode() === 'piece'));

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
