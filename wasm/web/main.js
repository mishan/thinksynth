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
 * are sliders, what it delivers comes back as the tape, and a second
 * scheduler in the mirror draws the piano roll -- the past it has played and
 * the future it has already decided, which is the half a tape cannot say.
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
import { micAvailable, openMic } from './mic.js';
import { midiAvailable, midiToggle } from './midi.js';
import { createNodeView } from './nodeview.js';
import { TapeDiff } from './tapediff.js';
import { Keyboard, TypingKeys, noteName, showRange } from './keyboard.js';
import { createKeyFocus } from './keyfocus.js';
import { createPanes } from './panes.js';
import { numberIn, showPanel } from './panel.js';
import { keepOffline, offerInstall } from './offline.js';
import { moveLayouts } from './layouts.js';
import * as patch from './patch.js';
import { createRollView, showClock } from './rollview.js';

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

/* A phone: a finger, on a screen that is narrow or held sideways with no
 * height -- the two shapes style.css calls small. The finger as well as
 * the shape, because a desktop window dragged narrow is not a phone: it
 * stays the document, and tiles as a desktop's once it is wide again.
 * `?phone=1' asks for the phone's layout anywhere, for trying it on a
 * desktop.
 *
 * On one of these the page is tiled too, but as a phone app rather than a
 * desktop's split: one tabbed area per mode, with the keys in a strip of
 * their own under it where a mode is for playing them. What a phone has
 * no use for -- the piece's picture, the sources, the graph editor's
 * canvas, the browser's numbers -- starts in the drawer, a tab away for
 * whoever wants it, rather than stacked into a page six screens long.
 */
const PHONE = '(pointer: coarse) and (max-width: 40em), ' +
              '(pointer: coarse) and (max-height: 30em)';

const PHONE_LAYOUTS = {
    patch: { dir: 'col', size: [0.6, 0.4], kids: [
        { tabs: ['paramview', 'nodeview'] },
        { tabs: ['keyboard'] }] },

    /* The tracks are what the sequence is for; the keys are a tab. */
    seq: { tabs: ['seqview', 'keyboard', 'paramview', 'roll'] },

    /* The roll in front: what the piece is doing is what a phone is
       for watching; the knobs are a tab away. */
    piece: { dir: 'col', size: [0.6, 0.4], kids: [
        { tabs: ['roll', 'knobs', 'seqview', 'channelbox', 'paramview'] },
        { tabs: ['keyboard'] }] },
};

/* And held sideways, where there is no height to split: one tabbed area,
   the keys in front where a mode is for playing them. */
const SIDEWAYS = '(max-height: 30em)';

const SIDEWAYS_LAYOUTS = {
    patch: { tabs: ['keyboard', 'paramview', 'nodeview'] },
    seq: { tabs: ['seqview', 'keyboard', 'paramview', 'roll'] },
    piece: { tabs: ['roll', 'keyboard', 'knobs', 'seqview', 'channelbox',
                    'paramview'] },
};

/* The two popovers, which live in the layout's overlay and are moved to
   the new one when a phone turns over. */
let popovers = [];

/* The tiler, for a desktop, a phone, or a phone held sideways, each
 * with its own layouts under its own store.
 */
function tile (phone, forced)
{
    const sideways = phone && matchMedia(SIDEWAYS).matches;

    panes = createPanes({
        root: $('panes'), catalog: PANES,
        store: sideways ? 'thinksynth:panes:touch-sideways'
             : phone ? 'thinksynth:panes:touch' : 'thinksynth:panes:solo',
        layouts: sideways ? SIDEWAYS_LAYOUTS
               : phone ? PHONE_LAYOUTS
               : { patch: PATCH_LAYOUT, piece: PIECE_LAYOUT,
                   seq: SEQ_LAYOUT },

        /* And a divider a finger can take hold of, tabs at their own
           widths in a row that scrolls, no strip over a lone tab, and a
           drawer that is where the rest are rather than what was shut. */
        ...(phone ? { media: forced ? 'all' : PHONE, split: 18,
                      strip: 'scroll', lone: false, closed: 'More:' } : {}),
        mode: mode(), on: true,
        onShow: (id, on) =>
        {
            if (id === 'composerview')
                showComposer(on && mode() === 'piece');
            else if (id === 'seqview')
                showSeq(on && composing());
            else if (id === 'roll')
                showRoll(on && composing());
            else if (id === 'nodeview')
                nodes?.show(on);
            else if (id === 'paramview' && on)
                pollParams();
        },
    });
}

/* The layout. Made at the end of init(), because what it adopts has to be
   in the document and the folds the page opens by hand have to be set. */
let panes = null;

/* Where patch mode puts its one .dsp. thinkweb.cpp's tw_piece_load says
   why a piece takes this channel first, and why the two are modes. */
const PATCH_CHANNEL = 0;

let ctx = null;
let synth = null;
let mic = null;                  /* what openMic returned, or null */
let midiIn = null;               /* the MIDI in button (midi.js)    */
let micPeak = 0;                 /* the loudest capture frame the worklet saw */
let micDropped = 0;
let keyboard = null;
let keys = null;                 /* the computer keyboard as a musical one */
let keyfocus = null;             /* and who has it, the page or the keys  */

/* note -> { count, midi, velocity, piece, channel }: how many fingers are
   on it, how many of those are MIDI keys, and the route it went out by,
   which is the route its release has to take. */
const sounding = new Map();

/* The channel a MIDI sustain pedal is down on, or null. */
let pedalChannel = null;

/* The piece, as it stands: what the worklet said when it loaded, and the
   roll the mirror draws of it -- the past it has played and the future it
   has already decided (rollview.js). */
let piece = null;
let roll = null;

/* The last tape message, kept for its clock: where transport zero is, how
   fast the clock is turned and where the output has got to. For
   window.solo, at the bottom. */
let lastTape = null;

/* And the notes off that tape, for window.solo as well.
 *
 * Kept here rather than asked of the roll, which is the shape this was in
 * when the page drew its own roll by hand and held the notes to do it.
 * The roll is the mirror's now (rollview.js) and keeps nothing on this
 * side -- and it is made only when somebody opens the pane, so a harness
 * reading it was reading a pane's state for an answer about the piece.
 * The tape is where the notes were coming from either way.
 *
 * Emptied on an epoch, which is what a rewind or a load bumps: notes
 * stamped in the old run's seconds are a different piece's, and a beat
 * measured across the two is neither's. */
let tapeNotes = [];
let tapeEpoch = 0;

/* How much of it to keep. The window the hand-drawn roll showed, for no
   better reason than that nothing has since asked for more: this is a
   harness's window on the recent past, not a history. */
const TAPE_NOTE_SECONDS = 30;

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

/* The status line. `alert' is for what somebody must see -- a file that
   did not parse, a Start that failed -- and is the only kind a phone
   shows: there the line is a row of the little height there is, and
   "Loaded ladder.dsp. Play." is what the screen already says. */
function status (text, alert = false)
{
    $('status').textContent = text;
    $('status').toggleAttribute('data-alert', alert);
}

/* What a finger does to a control, in what the page says. */
const TAP = matchMedia('(pointer: coarse)').matches ? 'Tap' : 'Click';

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
   are holding it. `midi' says a MIDI keyboard is the one holding it, which
   is counted apart as well (releaseKeys). */
function press (note, velocity = VELOCITY, midi = false)
{
    if (synth === null)
        return;

    const already = sounding.get(note);

    if (already !== undefined)
    {
        already.count++;

        if (midi)
            already.midi++;

        return;
    }

    const piecing = composing();
    const channel = keyChannel();

    sounding.set(note, { count: 1, midi: midi ? 1 : 0, velocity,
                         piece: piecing, channel });

    if (piecing)
        synth.midiOn(note, velocity, -1, channel);
    else
        synth.noteOn(note, velocity);

    keyboard?.hold(note, true);
}

/* And every way of letting go. The route is the one the press took, not
   the one the page is in now: a mode or a channel changed under a held
   note must not leave it sounding for ever. */
function release (note, midi = false)
{
    const held = sounding.get(note);

    if (held === undefined)
        return;

    /* A MIDI note off for a note releaseAll already let go of. */
    if (midi)
    {
        if (held.midi === 0)
            return;

        held.midi--;
    }

    if (--held.count > 0)
        return;

    sounding.delete(note);

    if (held.piece)
        synth.midiOff(note, -1, held.channel);
    else
        synth.noteOff(note);

    keyboard?.hold(note, false);
}

/* Everything, whoever is holding it, and the pedal: a mode change is about
   to make the routes wrong. */
function releaseAll ()
{
    midiIn?.forget();
    pedal(0);

    for (const held of sounding.values())
        held.midi = 0;

    releaseKeys();
}

/* A MIDI sustain pedal, 0..127, onto SusPedal on the channel the keys play:
 * thMidiChan holds a released note while it reads 64 or more, as
 * thSynth::handleMidiController routes controller 64 natively.
 *
 * Solo page only. A chanarg command lands whenever it arrives, and a pedal
 * in a room would have to land at the same transport time on every peer.
 * The channel it went down on is the one it comes up on, whatever the page
 * has switched to since.
 */
function pedal (value)
{
    if (synth === null)
        return;

    const channel = value >= 64 ? (composing() ? keyChannel() : PATCH_CHANNEL)
                                : pedalChannel;

    if (channel === null)
        return;

    if (pedalChannel !== null && pedalChannel !== channel)
        synth.chanarg(pedalChannel, 'SusPedal', 0);

    synth.chanarg(channel, 'SusPedal', value);
    pedalChannel = value >= 64 ? channel : null;
}

/* Everything the pointer and the computer keyboard hold: a key released
   while the page was not looking never sends its keyup. A MIDI key's note
   off arrives wherever the focus is, so what a MIDI keyboard holds stays
   down. */
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

/* ---- the keyboard, on screen and off ---- */

/* What the octave keys and the two buttons do: let go of everything, move
   the keys under the hands, and say where they are now and what they will
   cost. */
function shifted (lowest)
{
    releaseKeys();
    keyboard?.setLowest(lowest);
    showRange($('range'), keyboard);
    showLatency();
}

/* ---- the live input ---- */

/* The window the synth is made with, read once at Start because thSynth takes
 * it at construction and setWindowlen is a no-op.
 *
 * It is on the page rather than a constant because of what a live input costs.
 * The worklet's quantum is 128 and a window of 256 means the capture for a
 * window is only complete after the window that wanted it has already been
 * rendered -- so a live graph hears two windows late at 256 and one at 128,
 * 10.7 ms against 2.7. gthSynthSource's header has the arithmetic.
 */
function chosenWindow ()
{
    const n = Number($('window').value);

    return Number.isFinite(n) && n > 0 ? n : 256;
}

/* What a live graph hears late at the window in use, which is the number the
   window select exists for. */
function captureLatency ()
{
    if (ctx === null || synth === null)
        return 0;

    /* One window from the source running a window ahead, and a second where
       the quantum is smaller than the window, because then the accumulation
       is never ready in time. */
    const windows = synth.windowlen > 128 ? 2 : 1;

    return windows * synth.windowlen / ctx.sampleRate;
}

/* The level, as a bar and a number, for the one job it has: a microphone with
 * no automatic gain control arrives at whatever the room gives it, so the
 * vocoder's Mic gain has to be set by hand and this is what to set it by. Talk
 * and watch it move.
 *
 * dBFS rather than the raw float, because the useful range is the quiet end --
 * 0.03 and 0.15 are both "speech" and are 14 dB apart, and as decimals they
 * look like the same small number. */
function showMicLevel ()
{
    if (mic === null)
        return;

    /* A floor rather than -Infinity on silence, and it is the floor the bar is
       drawn against too. */
    const db = micPeak > 0.0001 ? 20 * Math.log10(micPeak) : -80;
    const filled = Math.max(0, Math.min(10, Math.round((db + 60) / 6)));

    $('miclevel').textContent =
        `${'#'.repeat(filled)}${'.'.repeat(10 - filled)} ` +
        `${db <= -80 ? '--' : db.toFixed(0)} dB` +
        (micDropped > 0 ? `  ${micDropped} dropped` : '');
}

async function toggleMic ()
{
    if (synth === null)
        return;

    if (mic !== null)
    {
        mic.close();
        mic = null;
        micPeak = 0;
        $('mic').textContent = 'Live in';
        $('micstatus').textContent = '';
        $('miclevel').textContent = '';
        showLatency();
        showLiveIn();
        return;
    }

    $('mic').disabled = true;
    $('micstatus').textContent = 'asking...';

    try
    {
        mic = await openMic(ctx, synth.node);
    }
    catch (e)
    {
        $('micstatus').textContent = e.message;
        $('mic').disabled = false;
        return;
    }

    $('mic').textContent = 'Live in: on';
    $('mic').disabled = false;

    /* The device, what it costs, and anything the browser would not switch
       off -- which is worth saying out loud, because a vocoder through an
       automatic gain control sounds broken rather than absent. */
    const warnings = mic.warnings();

    $('micstatus').textContent =
        `${mic.label}, ${ms(captureLatency())} late` +
        (warnings.length > 0 ? `; ${warnings.join('; ')}` : '');

    showLatency();
}

/* ---- whether anything is listening ----
 *
 * The window and Live in are for a graph that reads the live input, and
 * one that does names it `ionode->live0' (docs/DSP_FORMAT.md). Only
 * fx/vocoder-mic.dsp does today, so everywhere but Voice the two were
 * controls that did nothing, on the strip a phone has least room in.
 *
 * So they are offered when what is in play listens: the patch box's text
 * in patch mode, and in the other two the graphs on the piece's channels
 * -- the ones it names, instrument and effect alike, and the ones the page
 * aimed. Live in stays while the input is on, so it can be switched off.
 * The window is chosen before Start and fixed by it, so it goes then.
 */
const LIVE = /\bionode->live\d/;

async function listens ()
{
    if (mode() === 'patch')
        return LIVE.test($('dsp').value);

    const names = new Set();

    for (const p of placed.values())
        if (p?.dsp)
            names.add(p.dsp);

    /* The piece's own names, as doc.js reads them for the room: the
       `dsp' of an instrument block and each `effect' in one. */
    if (mode() === 'piece')
        for (const m of $('gen').value.matchAll(
                 /\b(?:dsp|effect)\s+"([^"]+)"/g))
            names.add(m[1]);

    const texts = await Promise.all([...names].map(
        (n) => dspTexts[n] ?? patch.graphText(n).catch(() => '')));

    return texts.some((t) => LIVE.test(t));
}

/* Asked after anything that changes what is in play; only the latest
   answer is drawn. */
let liveAsked = 0;

async function showLiveIn ()
{
    const asked = ++liveAsked;
    const on = mic !== null || await listens().catch(() => false);

    if (asked !== liveAsked)
        return;

    $('livein').hidden = !on;
    $('windowlabel').hidden = synth !== null;
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
        `live in          ${mic === null ? 'off'
                                        : `${ms(captureLatency())} late`}\n` +
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

    status(ok ? `Loaded ${$('patch').value}. Play.`
              : 'That .dsp did not parse; see below.', !ok);

    showLiveIn();

    if (!ok)
        seeBelow();

    await showParams();
}

/* The same fetch before the same kind of load, so the same `picking' --
   see pickPiece. */
async function pickPatch ()
{
    const run = (async () =>
    {
        $('dsp').value =
            await (await fetch(`dsp/${$('patch').value}`)).text();

        showNodes();

        await loadPatch();
    })();

    picking = run.catch(() => {});

    await run;
}

/* ---- the tempo ---- */

/* The text the box held when the piece playing now was loaded.
 *
 * A tempo change is two things -- what is heard, and what a reload comes
 * back at -- and the second writes the `tempo' statement into the
 * document. The document is a box somebody may have been typing in, so
 * the write happens only while the box still holds what was loaded.
 * Anything else and the tempo is live and the text is theirs, which is
 * the pair of answers that loses nobody's work. */
let loadedText = '';

/* The control, put where the piece that is playing leaves it.
 *
 * Offered only where it means something. The tempo scales beat-valued
 * durations and nothing else, so on a piece written entirely in seconds it
 * is a control that does nothing -- which is most of the corpus, and which
 * the desktop's spinner has said for as long as it has been dimmed
 * (ComposerWindow.cpp). The sequence this page writes is in beats
 * throughout, which is what makes it the one mode where this is the
 * control somebody reaches for first.
 */
function showTempo ()
{
    const box = $('tempo');
    const live = piece !== null && composing() && piece.beats;

    box.disabled = !live;
    $('tempolabel').title = live
        ? 'Beats per minute. This piece writes durations in beats, so ' +
          'everything moves together.'
        : piece === null
            ? 'Load a piece to set its tempo.'
            : 'This piece writes every duration in seconds, which the ' +
              'tempo does not scale. Speed, beside this, turns the clock ' +
              'itself and moves it; or write a duration as `4 beats\' to ' +
              'put a stage on the tempo.';

    /* Not while it is being typed in: a box that rewrote itself under the
       caret would make 90 unreachable on the way to 900. */
    if (piece !== null && document.activeElement !== box)
        box.value = String(Math.round(piece.tempo));

    showSpeed();
}

/* And the clock's own speed, which every piece has.
 *
 * The tempo above is the musical control and it cannot reach a piece that
 * writes `period = 0.25 s' -- which is most of the corpus. This one turns
 * the transport itself, so everything moves whatever it is written in.
 * Offered wherever there is a piece to play rather than only where the
 * tempo means something, which is the whole point of having it.
 *
 * It is not a property of the piece and there is no statement for it: a
 * tempo is something a piece *is*, a speed is something a listener is
 * doing. So nothing is written back, and it stays across a load the way
 * the channels somebody aimed do. */
function showSpeed ()
{
    const box = $('speed');
    const live = piece !== null && composing();

    box.disabled = !live;
    $('speedlabel').title = live
        ? 'How fast the clock runs, as a multiple of real time. Every ' +
          'duration moves with it, in seconds or in beats.'
        : 'Load a piece to change how fast it plays.';

    if (piece !== null && document.activeElement !== box)
        box.value = String(piece.speed);

    saySpeed();
}

/* The reading beside the slider. Two decimals, because the step is 0.05
   and a slider you cannot read a number off is one you cannot put back. */
function saySpeed ()
{
    $('speedis').textContent = `${Number($('speed').value).toFixed(2)}\u00d7`;
}

/* Somebody dragged it.
 *
 * On `input' rather than `change', unlike the tempo box: a slider's whole
 * point is that it is heard while it moves, and a drag reports every
 * position. Each one is a stamped command, which is the same traffic a
 * knob drag already makes.
 */
function setSpeed ()
{
    const box = $('speed');
    const value = Number(box.value);

    saySpeed();

    if (synth === null || piece === null || !Number.isFinite(value) ||
        value <= 0)
        return;

    synth.speed(value);
    piece.speed = value;
}

/* Somebody moved it.
 *
 * Both halves, in the order they matter: what is playing first, because
 * that is what was asked for and it is a stamped command that lands at its
 * time on every peer; then the text, which is what a reload -- a mode
 * switch, a Load, a piece chosen and gone back on -- would come back at.
 */
async function setTempo ()
{
    const box = $('tempo');

    if (synth === null || piece === null || !piece.beats)
        return;

    /* An empty box is not an instruction, and it reads as zero rather
       than as nothing: a number input hands back "" both for a box
       somebody cleared and for one they typed letters into, and Number("")
       is 0, which would clamp to 20 and set a tempo nobody asked for. */
    const typed = box.value.trim() === '' ? NaN : Number(box.value);

    if (!Number.isFinite(typed))
    {
        box.value = String(Math.round(piece.tempo));
        return;
    }

    /* Into the range rather than ignored, which is what the desktop's
       spinner does with the same number. A box left holding 500 while the
       piece goes on at 112 is a control saying something that is not so,
       and nothing else on the strip would have said which of the two was
       true. Whole beats, because that is the step the box offers. */
    const bpm = Math.round(Math.min(Number(box.max),
                                    Math.max(Number(box.min), typed)));

    box.value = String(bpm);

    synth.transportAt('tempo', -1, bpm);
    piece.tempo = bpm;

    /* The statement, written by the .gen writer the Composer edits with
       rather than by a regular expression here: one speller of this
       format, and it puts the line where that writer's rules put it in a
       piece that never had one. */
    const was = $('gen').value;
    const { text } = await synth.pieceSetTempo(bpm);

    /* The document this edit was made against, and not `loadedText'
       alone: a load begun while the answer was in flight has already
       moved loadedText on to the piece it is loading, and the text coming
       back is the piece before it. Both, so the one thing that can be
       written here is the document that was asked about. */
    if (text === '' || $('gen').value !== was || was !== loadedText)
        return;

    $('gen').value = text;
    loadedText = text;
}

/* A stage's params, set from the popover and written into the worklet's
 * copy of the piece: into the box as well, which is what a Load, a mode
 * switch and a Save read. On the tempo's terms -- only while the box still
 * holds what was loaded, or the edit is live and the text is theirs. */
function paramsEdited ({ piece: text })
{
    if (text === '' || $('gen').value !== loadedText)
        return;

    $('gen').value = text;
    loadedText = text;
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

    /* What the box held when this load took it. A tempo change writes the
       statement back into the text, and it may only do that to a document
       nobody has since typed into -- see showTempo. */
    loadedText = $('gen').value;

    const it = await quietly(async () =>
    {
        const loaded = await synth.loadPiece($('gen').value);

        if (loaded.errors.length === 0)
            aiming = await patch.aim(
                synth, loaded.sinks,
                mode() === 'seq' ? new Map([...sequenceVoices(), ...aimed])
                                 : aimed);

        return loaded;
    });

    piece = it.errors.length === 0 ? it : null;
    placed = aiming.placed;
    showLiveIn();

    if (piece === null)
    {
        status('That .gen did not parse; see below.', true);
        it.errors.forEach(log);
        seeBelow();
    }
    else if (aiming.failed.length > 0)
    {
        status(`Loaded ${piece.name || $('piece').value}, but not ` +
               'everything it asked for; see below.', true);
        aiming.failed.forEach(log);
        seeBelow();
    }
    else
        status(`Loaded ${piece.name || $('piece').value}. Press Play.`);

    $('about').textContent = piece === null ? '' : piece.description;
    showAbout();

    for (const id of ['play', 'stop', 'rewind'])
        $(id).disabled = piece === null;

    showTempo();

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
const SEQ_STEPS = 16;
/* Six: five degrees and the octave above them, which is a range to
   write a line in and still leaves the tracks visible at once. */
const SEQ_ROWS = 6;

/* The tracks, in channel order, and what each opens playing: a kit on the
 * first three and two voices over it, the newer instruments rather than
 * the desktop's first-run four -- which are four tonal patches, six rows
 * each, and made the opening screen on a phone four tall grids of one
 * kind of sound.
 *
 * A drum that ignores the note gets a grid one row tall (see fitTracks),
 * so the kit costs a strip each. The beat is there so that Play makes
 * something straight away; the voices start empty, for somebody to draw
 * on. These are the page's defaults for its own sequence, handed to the
 * aiming ahead of the first-run ones, and a track's menu still replaces
 * any of them.
 */
const SEQ_VOICES = [
    { dsp: 'kit_kick.dsp',  cells: 'x...x...x...x...' },
    { dsp: 'kit_snare.dsp', cells: '....x.......x...' },
    { dsp: 'kit_hat.dsp',   cells: '..x...x...x...x.' },
    { dsp: 'ebass.dsp' },
    { dsp: 'rhodes.dsp' },
];

/* Where the keys go in the sequence: the last voice, the Rhodes, rather
   than channel 1 -- which is the kick. */
const SEQ_KEYS = SEQ_VOICES.length - 1;

/* The first line of what this writes, and what tells the box's contents
 * apart from any other piece.
 *
 * By a mark of its own rather than by what the text contains. "Does this
 * have a `gen::grid' in it" was the obvious question and the wrong one:
 * gen/scratch.gen has five, so going to look at it in piece mode and
 * coming back to the sequencer kept it -- playing a shipped piece as the
 * sequence, with none of the menus this mode is for. What the guard
 * means is "is this still the page's own sequence", and only the page
 * can answer that. */
const SEQ_MARK = '# A sequence, written by the page.';

function sequenceText ()
{
    /* A drum's pattern on its one row; a voice's on the bottom row of its
       ladder, the root, where a single line of cells reads as a line. */
    const cells = ({ dsp, cells = '' }) =>
    {
        const line = cells.padEnd(SEQ_STEPS, '.');

        if (!readsNote(dsp))
            return { rows: 1, cells: line };

        const empty = '.'.repeat(SEQ_STEPS);

        return { rows: SEQ_ROWS,
                 cells: [...Array(SEQ_ROWS - 1).fill(empty), line].join('/') };
    };

    const track = (voice, n) =>
    {
        const { rows, cells: grid } = cells(voice);

        return `chain track${n} {
    input midi;

    stage seq gen::grid {
        notes  = pent;
        steps  = ${SEQ_STEPS};
        rows   = ${rows};
        cells  = "${grid}";
        period = 0.25 beats;
        hold   = 0.2 beats;
        vel    = 96;
        listen = 0;
    };
    sink { channel = ${n}; };
};`;
    };

    return `${SEQ_MARK}
#
# A kit and two voices, one grid each on a channel of its own: rows are
# degrees of the ladder below, columns are steps, and a drum that plays
# the same sound at any pitch has one row. Fill in cells; the menu on
# each track says what plays it. Save this file and it opens in the
# Composer like any other.
#
# \`input midi' on each of them is what makes the keys play a track: a
# note aimed at a channel goes through that channel's chain and out its
# sink, so what you play is heard on the instrument the track is set to.
# \`listen = 0' is what keeps it from also drawing itself on the grid --
# turn that up and playing writes what it plays.

name "A sequence";
description "A beat, a bass and keys. Fill in cells; pick what plays them.";

tempo 112;

scale pent "C3 D3 E3 G3 A3";

${SEQ_VOICES.map((v, i) => track(v, i + 1)).join('\n\n')}
`;
}

/* The sequence's own defaults, as channel -> the .dsp: what the aiming
   uses on a track nobody has chosen for. */
function sequenceVoices ()
{
    return new Map(SEQ_VOICES.map((v, i) => [i, v.dsp]));
}

/* The mode, entered. The text is written once a session: coming back to
   it keeps whatever the box says, which is what somebody who went to look
   at a patch and came back expects to find. */
async function loadSequence ()
{
    holdText('seq');

    if (!$('gen').value.includes(SEQ_MARK))
        $('gen').value = sequenceText();

    await loadPiece();

    if (piece !== null)
        status(`${TAP} cells to draw a pattern, then press Play.`);
}

/* The .gen box is one box and two modes write in it: the sequence and the
 * piece. Each mode's text is kept here while the other has the box, so
 * entering a mode puts back what it held -- the piece the menu names, or
 * the pattern somebody drew -- rather than loading whatever the last mode
 * left there. Without it, the page opening on the sequencer and then going
 * to a piece loaded the sequence under a menu that said ebb.gen, and
 * choosing ebb.gen did nothing, since the menu already said so.
 *
 * `null' is a mode that has never had the box: the sequence writes its own
 * text the first time (loadSequence), and the piece is given the menu's
 * default at init. */
const modeText = { seq: null, piece: null };
let textMode = null;             /* whose text the box is holding now */

function holdText (which)
{
    if (textMode === which)
        return;

    if (textMode !== null)
        modeText[textMode] = $('gen').value;

    textMode = which;

    if (modeText[which] !== null)
        $('gen').value = modeText[which];
}

/* The piece mode, entered: its own text back in the box, then loaded. */
async function loadPieceMode ()
{
    holdText('piece');

    await loadPiece();
}

/* A choice from the menu, in flight. The fetch comes before the load and
   the load is what quietly() tracks, so without this a harness asking
   settled() between the two was told the page had settled on the piece
   before the one it chose. */
let picking = Promise.resolve();

async function pickPiece ()
{
    const run = (async () =>
    {
        $('gen').value =
            await (await fetch(`gen/${$('piece').value}`)).text();

        await loadPiece();
        showLiveIn();
    })();

    picking = run.catch(() => {});

    await run;
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
        status(`Channel ${channel + 1}: nothing to save.`, true);
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

    status(`Channel ${channel + 1} saved as ${name}.`, true);
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
        status(`${what.title} on channel ${channel + 1}. Play.`);

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
        status(`Channel ${channel + 1}: ${e.message}; see below.`, true);
        log(`channel ${channel + 1}: ${e.message}`);
        seeBelow();

        /* The menu is showing something that is not there; the row is
           drawn from what is. */
        showChannels();
    }

    /* What is on the channel decides what the panel has rows for. */
    await showParams();
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

/* The notes out of a tape message, into tapeNotes above.
 *
 * An epoch that is not the one held empties it first: a rewind or a load
 * restarts the transport's seconds, and the worklet posts what it has
 * under the old epoch before anything from the new run joins it
 * (worklet.js), so a batch is all one run's. */
function takeTapeNotes (m)
{
    if (m.epoch !== tapeEpoch)
    {
        tapeEpoch = m.epoch;
        tapeNotes = [];
    }

    for (const e of m.events)
        if (e.kind === 'N')
            tapeNotes.push(e);

    /* Whatever ended before the window, wherever it sits: a long note at
       the front must not keep everything behind it alive. Rebuilt only
       when there is something to drop, so an idle piece is not copying
       its list every batch. */
    const first = m.now - TAPE_NOTE_SECONDS;

    if (tapeNotes.some((e) => e.at + e.duration < first))
        tapeNotes = tapeNotes.filter((e) => e.at + e.duration >= first);
}

/* ---- starting, and switching ---- */

async function start ()
{
    $('start').disabled = true;
    status('Starting...');

    try
    {
        ctx = new AudioContext({ latencyHint: 'interactive' });
        synth = await createSynth(ctx, { windowlen: chosenWindow(),
                                         onLog: log,
                                         onTape: (m) =>
                                         {
                                             micPeak = m.capture ?? 0;
                                             micDropped = m.captureDropped ?? 0;
                                             lastTape = m;
                                             takeTapeNotes(m);

                                             diff.take('worklet', m);
                                             showClock($('clock'), m);

                                             /* What a probe armed here
                                                is watching, as jam.js
                                                feeds it: without this
                                                the scope stays blank. */
                                             nodes?.feed(m.probes);
                                         },
                                         onParamEdits: paramsEdited,
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

        status(`Could not start: ${e.message}`, true);
        $('start').disabled = false;
        return;
    }

    /* Here rather than at the end of start(), because these two are about the
       synth and everything below is about what to play on it. The window is the
       synth's now, so the choice is spent; the microphone is a click away, and
       asking for one needs a gesture besides. */
    $('window').disabled = true;
    $('mic').disabled = !micAvailable();
    showLiveIn();

    if (!micAvailable())
        $('micstatus').textContent = 'needs https, or localhost';

    $('midi').disabled = !midiAvailable();

    if (!midiAvailable())
        $('midistatus').textContent = 'needs Chromium or Firefox, over https '
                                      + 'or on localhost';

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

        status(`Could not start: ${e.message}`, true);
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
    showRoll(panes.visible('roll') && composing());

    if (mode() === 'seq')
        await loadSequence();
    else if (mode() === 'patch')
        await loadPatch();
    else
        await loadPieceMode();

    try
    {
        nodes = await createNodeView({
            files: nodeFiles,
            onStatus: (text) => status(text, true),
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

    /* Faster than the rest of the chrome, because this one is being watched
       while somebody talks into it rather than read once. */
    setInterval(showMicLevel, 100);
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

    if (roll !== null && roll.fromMirror(m))
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
        status(`${name} changed. Load the piece again to hear it.`, true);
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

        /* A stage's param, set. Stamped for the next window, as this
           page's knobs and gestures are, and it still goes the long way
           round: out as a command, back in at its time, applied by every
           instance including this one. One path, whether or not there is
           anybody else in the room. */
        onParamEdit: (chain, stage, row, text) =>
            synth?.param({ at: -1, chain, stage, row, text }),
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

/* Whether a graph plays what it is sent at the pitch it is sent.
 *
 * The catalog's own reading of the file: something in the graph reads the
 * io node's `note' (src/DspCatalog.h). A graph nobody has a row for counts
 * as pitched, because a ladder that turns out to do nothing is a smaller
 * surprise than a pattern silently collapsed to one row. */
function readsNote (file)
{
    for (const group of dspGroups)
        for (const e of group.entries)
            if (e.file === file)
                return e.note;

    return true;
}

/* How tall each track's grid ought to be, given what is playing it.
 *
 * A kick, a hat, a clap ignores the note it is sent -- kick909.dsp says so
 * in its own header, "a kick is a kick" -- so a six-row ladder over one is
 * six rows that make one sound, and every cell in the column is the same
 * cell. One row is the whole of what that instrument has to say.
 *
 * The graph's answer and not its category, which is the distinction the
 * corpus insists on: `category "Drums"' holds Kick 909, which ignores the
 * note, and Tom 808, which does not. A tom keeps its ladder because a tom
 * is played at pitch.
 *
 * Sequence mode only. The piece there is the one the page wrote, and its
 * `rows' is the page's to set; a shipped piece's is its author's, and
 * aiming an instrument at one of its channels is not a licence to reshape
 * the pattern under it.
 *
 * Sent as a command rather than written into the box: the grid is playing,
 * the text is not what it is playing from, and a param that is heard lands
 * at a transport time on every peer the way a knob does.
 */
/* What each track was last asked for, and the height it was asked
   against. Kept between calls; see fitTracks. */
let asked = new Map();

function fitTracks ()
{
    if (mode() !== 'seq' || synth === null || seq === null)
        return;

    /* Rebuilt rather than edited, so a track the piece no longer has
       leaves with it. */
    const outstanding = new Map();

    for (const track of seq.tracks())
    {
        if (track.rowsParam < 0 || track.channel < 0)
            continue;

        const dsp = placed.get(track.channel)?.dsp;
        const want = dsp !== undefined && !readsNote(dsp) ? 1 : SEQ_ROWS;

        if (track.rows === want)
            continue;

        /* Asked once per height, not once per measurement. A measurement
           arrives per track and every one of them comes through here, so
           an unguarded ask goes out once per track before the first
           answer gets back -- four commands where one was wanted, on the
           queue a room relays. Both numbers are remembered, so a reload
           that puts the grid back to its file's height asks again, and so
           does a peer that moved it. A command that never takes is asked
           for once. */
        const last = asked.get(`${track.chain}:${track.stage}`);

        outstanding.set(`${track.chain}:${track.stage}`,
                        { want, rows: track.rows });

        if (last !== undefined && last.want === want &&
            last.rows === track.rows)
            continue;

        synth.stageParam({ chain: track.chain, stage: track.stage,
                           param: track.rowsParam, value: want });
    }

    asked = outstanding;
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

        /* The pane has just been told how tall its grids say to be, which
           is the moment to say whether that is the height they want. The
           answer is the module's both ways round -- the page sends a
           param and hears what it became -- so a load, a menu and a peer
           all arrive here by the same door. */
        onMeasure: fitTracks,
    });

    seq.show(on);
}

/* The piano roll, on the same terms: made when it is first wanted, and
   drawing only while somebody is looking at it. Its drawing is the
   mirror's too -- that is where the scheduler is, and the scheduler is
   where the piece's future is. */
function showRoll (on)
{
    if (roll === null && (!on || synth === null))
        return;

    roll ??= createRollView({ toMirror: (m) => synth?.toMirror(m) });

    roll.show(on);
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

    /* The notes the tape has carried, in transport seconds. What a
       harness about the tempo has to read: a control that moved a number
       in a box and nothing else would pass every check that asks the
       box. */
    notes: () => tapeNotes.map((e) => ({ at: e.at, note: e.note,
                                        channel: e.channel })),

    /* What is held down, and by how many hands and MIDI keys: the route
       and velocity each note went out with. */
    sounding: () => [...sounding].map(([note, held]) => ({ note, ...held })),

    /* The four numbers the last tape message carried about the clock, and
       the rate to read them against.
     *
       Where transport zero is, how fast the clock is turned and where the
       output has got to are one line, and clock.js walks it from the
       other end to stamp a command (TransportClock). A harness that only
       watched the readout would not see the line come apart -- the
       readout is `now', which the module hands over ready-made. */
    transport: () => lastTape === null ? null
        : { now: lastTape.now, frame: lastTape.frame,
            origin: lastTape.origin, speed: lastTape.speed,
            rate: ctx?.sampleRate ?? 0 },

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
        for (let was = null, drew = null, chose = null;
             was !== quiet || drew !== drawn || chose !== picking; )
        {
            was = quiet;
            drew = drawn;
            chose = picking;

            await picking;
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
    pollParams: () => composer?.pollParams(),

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
                      roll: roll?.visible() ?? false,
                      nodes: nodes?.visible() ?? false,
                      seq: seq?.visible() ?? false }),

    /* Where the roll's now-line is and whether it is still live, so a
       harness that dragged on it can say that it scrubbed. */
    roll: () => roll?.where() ?? null,

    /* The instrument's graph: where its boxes are, so a harness can press
       on one rather than at a guess, and what it has selected. */
    node: () => (nodes === null ? null : {
        boxes: nodes.boxes(),
        selected: nodes.selected(),
        probes: nodes.probes(),
        box: (i) => nodes.boxAt(i),
    }),
};

/* Where the keys go, kept for each mode that composes. The sequence's
   are on its Rhodes and a piece's on channel 1, where most pieces listen:
   one selector for both sent a piece's keys to the sequence's channel 5,
   which Ebb has nothing on. Whatever somebody picks in a mode is what
   that mode comes back to. */
const keyChans = { seq: SEQ_KEYS, piece: 0 };
let keysMode = null;

function keysFor (which)
{
    if (keysMode in keyChans)
        keyChans[keysMode] = keyChannel();

    if (which in keyChans)
        $('keychan').value = String(keyChans[which]);

    keysMode = which;
}

/* The panes a mode has, and its layout. A pane the mode does not have is
   unavailable rather than hidden: it leaves the layout without being
   forgotten by it, so coming back to a mode puts its panes where they
   were. */
function panesFor (which)
{
    const mine = new Set(modePanes[which] ?? []);

    for (const id of new Set([...SEQ_PANES, ...PIECE_PANES, ...PATCH_PANES]))
        panes.available(id, mine.has(id));

    panes.mode(which);
}

async function pickMode ()
{
    const which = mode();

    /* For style.css, which has rules for one mode on a phone. */
    document.body.dataset.mode = which;

    keysFor(which);

    /* The chrome each mode has: what to play, and the transport. What
       the piece section used to wrap are panes of their own now, and
       panesFor's. */
    $('patchmode').hidden = which !== 'patch';
    $('piecemode').hidden = which !== 'piece';
    $('transport').hidden = which === 'patch';
    showAbout();

    panesFor(which);
    showLiveIn();

    if (synth === null)
        return;

    showComposer(panes.visible('composerview') && which === 'piece');
    showSeq(panes.visible('seqview') && composing());
    showRoll(panes.visible('roll') && composing());
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
        await loadPieceMode();
    else
    {
        synth.transport('stop');
        piece = null;
        placed = new Map();
        showChannels();
        showTempo();               /* nor a speed: see showSpeed */
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
    /* Patch mode opens on the ladder: one of the newer graphs, a sound
       that holds for as long as the key does, and cheap to start a note
       on. Not the grand, whose note-on costs about half a 128-frame
       quantum on a desktop, which is a whole one on a phone. */
    fill($('patch'), playableDsps(), 'ladder.dsp');
    fill($('piece'), gens, 'ebb.gen');

    [$('dsp').value, $('gen').value] = await Promise.all([
        fetch(`dsp/${$('patch').value}`).then((r) => r.text()),
        fetch(`gen/${$('piece').value}`).then((r) => r.text()),
    ]);

    /* The piece the menu names, which the box holds until a mode says
       otherwise -- see holdText. */
    textMode = 'piece';

    /* Sixteen is all there are, counted the way the file counts them and
       the way the channels row does -- the engine's number is one lower
       and is what the option carries, since that is what a chain's `input
       midi' is matched against. So the file's channel 1 is where a piece's
       first instrument lands, and where hands.gen's arpeggiator listens. */
    for (let c = 0; c < 16; c++)
        $('keychan').add(new Option(String(c + 1), c, c === 0, c === 0));

    keyboard = new Keyboard($('keys'),
                            { onPress: press, onRelease: release });
    /* Who has the keyboard, and the one line on the page that says so.
       Escape hands it back, and lets go of anything it was holding on
       the way -- a note whose key-up is about to land somewhere else. */
    keyfocus = createKeyFocus({ indicator: $('keysstate'),
                                onRelease: releaseKeys });
    keys = new TypingKeys({ press, release, shifted, focus: keyfocus,
                            playable: () => synth !== null });
    keyboard.setLowest(keys.lowest);
    keyboard.fit();
    showRange($('range'), keyboard);

    $('start').addEventListener('click', start);
    $('mic').addEventListener('click', toggleMic);
    midiIn = midiToggle({
        button: $('midi'), status: $('midistatus'),
        onNoteOn: (note, velocity) => press(note, velocity, true),
        onNoteOff: (note) => release(note, true),
        onPedal: pedal,
    });
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

    /* And the composer's popover, on the same beat. It is a different panel
       over a different thing -- a stage's line in the .gen rather than an
       arg on a channel -- so it polls the instance that holds the piece's
       picture rather than the one that sounds. */
    setInterval(() => composer?.pollParams(), 250);

    $('play').addEventListener('click', () => synth.transport('start'));
    $('stop').addEventListener('click', () => synth.transport('stop'));
    $('rewind').addEventListener('click', () => synth.transport('rewind'));

    /* `change' and not `input': a number box fires input on every
       keystroke, so typing 120 over 90 would send 1, then 12, then 120 --
       two tempo commands nobody asked for, the first of them below the
       range. */
    $('tempo').addEventListener('change', setTempo);
    $('speed').addEventListener('input', setSpeed);

    $('down').addEventListener('click', () => keys.shift(-1));
    $('up').addEventListener('click', () => keys.shift(1));

    window.addEventListener('keydown', (e) => keys.keyDown(e));
    window.addEventListener('keyup', (e) => keys.keyUp(e));
    window.addEventListener('blur', releaseKeys);

    /* A screen with room for the source next to the keys opens it; one
       without keeps it folded, since on a phone it is most of the page.
       Asked once, at load: this is a starting point and not a rule, and
       the fold is the reader's from here on. */
    if (matchMedia('(min-width: 60em)').matches)
        $('patchsource').open = $('piecesource').open = true;

    /* And the parameters the other way round. On a phone they are a
       screen and more of sliders between the chrome and the keys, so the
       keys were a scroll away in every mode; folded, the summary says
       they are there. The piece's picture too, whose stages are too small
       to read at a phone's width. The same two shapes style.css calls
       small: narrow, and a phone held sideways. */
    if (matchMedia('(max-width: 40em), (max-height: 30em)').matches)
        $('paramview').open = $('composerview').open = false;

    /* The phone's fold over the speed and the clock (style.css). */
    $('more').addEventListener('click', () =>
    {
        const open = $('transport').classList.toggle('more');

        $('more').setAttribute('aria-expanded', String(open));
    });

    /* And the layout, over what is in the document now.
     *
     * onShow is the whole of what tiling asks of this page: a pane in a
     * background tab, folded away, or belonging to the mode that is not
     * up is a pane whose work can stop, and these are the three places
     * this page has work to stop. They are the same calls the folds and
     * the mode switch made before, asked for in one place.
     *
     * The store is named for the site: localStorage belongs to the whole
     * origin, and on GitHub Pages that is every repository's site.
     * A layout left under its name before that is moved to it first. */
    moveLayouts('panes:solo', 'thinksynth:panes:solo',
                ['patch', 'piece', 'seq']);

    /* A phone or not is decided at load; which way up it is, whenever it
       turns (below). */
    const forced = new URLSearchParams(location.search).get('phone') === '1';
    const phone = forced || matchMedia(PHONE).matches;

    tile(phone, forced);

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
    popovers = [$('composerparams'), $('nodemenu')];
    panes.overlay().append(...popovers);

    /* A phone turned over gets the other shape's layout for the mode it
       is in: the tiler is taken down, which puts every pane back where the
       document had it, and made again from the other set. Nothing is
       loaded again; the panes are the same elements. */
    if (phone)
        matchMedia(SIDEWAYS).addEventListener('change', (e) =>
            panes.setLayouts(
                e.matches ? SIDEWAYS_LAYOUTS : PHONE_LAYOUTS,
                { store: e.matches ? 'thinksynth:panes:touch-sideways'
                                   : 'thinksynth:panes:touch' }));
}

offerInstall($('install'));

/* After init, whose fetches are done by then -- see offline.js. */
init().finally(() =>
    keepOffline().catch((e) => console.warn('no offline copy:', e)));
