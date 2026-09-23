#!/usr/bin/env node
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
 * pagetest.mjs -- the solo page's hands, dials and pictures, in a browser.
 *
 *   cd wasm/web && npm ci && npx playwright install chromium
 *   node pagetest.mjs [BUILD_DIR]
 *
 * The other harnesses here go round the page: browsertest.mjs builds the
 * synth the way host.js does and compares samples, piececheck.mjs never
 * opens a browser at all. Nothing ran main.js. So the two things the solo
 * page and the room page share -- the computer keyboard as a musical one,
 * and the knobs a piece declared as sliders (keyboard.js, panel.js) --
 * were only ever exercised by hand, on the page a change to either is
 * most likely to break.
 *
 * A parameter panel is here for the half of it that needs a browser: that
 * the module's description of a channel's controls became elements, and
 * that moving one reaches the arg -- and the same for a composer stage's
 * params, which the popover beside a stage box now takes as well as shows.
 * What a description says, and that it is the same description the desktop
 * draws, is scripts/panelcheck's and wasm/web/panelcheck.mjs's.
 *
 * Small on purpose: the octave, the sliders, the channel's parameter panel,
 * a key down and up, and a key typed into a text box, which must play
 * nothing. A MIDI keyboard, through a stand-in for Web MIDI. Then the two
 * canvases this page has: the composer view, and the
 * instrument's graph. What sounds is browsertest.mjs's business and
 * jamtest.mjs's; this is about the page.
 *
 * And then the composer view, which is the one thing here with a whole
 * second engine behind it: the piece's picture is drawn by the mirror --
 * another instance of the module, in a worker, fed the messages the
 * worklet is fed -- and comes over as a list of ops the page replays on a
 * Canvas2D. What is checked is the round trip a finger makes: the picture
 * arrives and is replayed, a stage whose picture is a control can be
 * enlarged, a drag on it leaves as a command and comes back as a board
 * that has changed, and Escape puts it back. The transport is left stopped
 * for that, so that nothing but the drag could have changed what is drawn.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { chromium } from 'playwright';

import { serve } from './serve.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.join(here, '..', '..');
const build = path.resolve(process.argv[2] ?? path.join(top, 'build-web'));

const PIECE = 'airports.gen';

/* One with a picture that is a control: gen::life, which is what the
   composer view's gestures are tried on. */
const COMPOSER_PIECE = 'colony.gen';

/* A piece that names channels and declares no instrument for any of them,
   so the page has to aim them and offers a menu per channel. */
const AIMED_PIECE = 'fern.gen';

let failures = 0;

function check (cond, what)
{
    if (cond)
        process.stdout.write(`ok    ${what}\n`);
    else
    {
        failures++;
        process.stdout.write(`FAIL  ${what}\n`);
    }
}

if (!fs.existsSync(path.join(build, 'main.js')))
{
    process.stdout.write(`pagetest: no site in ${build}; build it first ` +
                         '-- see wasm/web/CMakeLists.txt.\n');
    process.exit(1);
}

const site = await serve(build, 0, '127.0.0.1', null);

/* The document rather than the tiled layout. Both are the page -- panes.js
   adopts what is in the markup and puts it back, and below 60em or under
   a finger the tiled one is not offered at all -- and this harness is
   about what the page plays rather than where it puts it. panecheck.mjs
   is the other one, and takes the same page across that threshold and
   back. */
const url = `http://127.0.0.1:${site.address().port}/index.html?panes=0`;
const errors = [];

const browser = await chromium.launch(
    { args: ['--autoplay-policy=no-user-gesture-required'] });

try
{
    const page = await browser.newPage();

    page.on('pageerror', (e) => errors.push(e.message));
    page.on('console', (m) =>
    {
        if (m.type() === 'error')
            errors.push(m.text());
    });

    /* Web MIDI, faked: headless Chromium has no devices to offer. One input
       at load, and window.fakeMidi to send from it, pull it out and plug
       another in. Ports stay in the map when disconnected, as a browser's
       do. */
    await page.addInitScript(() =>
    {
        const access = { inputs: new Map(), outputs: new Map(),
                         onstatechange: null };
        const fake = {
            asked: null,

            plug (id, name)
            {
                const port = { id, name, type: 'input', state: 'connected',
                               onmidimessage: null, close: async () => {} };

                access.inputs.set(id, port);
                access.onstatechange?.({ port });
            },

            unplug (id)
            {
                const port = access.inputs.get(id);

                port.state = 'disconnected';
                access.onstatechange?.({ port });
            },

            send (id, bytes)
            {
                access.inputs.get(id).onmidimessage?.(
                    { data: new Uint8Array(bytes) });
            },
        };

        fake.plug('k1', 'Fake Keys');
        window.fakeMidi = fake;

        /* And the chanarg commands the page posts to the worklet, which is
           where a sustain pedal goes. */
        window.chanargs = [];

        const post = MessagePort.prototype.postMessage;

        MessagePort.prototype.postMessage = function (m, ...rest)
        {
            if (m?.type === 'chanarg')
                window.chanargs.push(
                    { channel: m.channel, name: m.name, value: m.values[0] });

            return post.call(this, m, ...rest);
        };
        navigator.requestMIDIAccess = async (options) =>
        {
            fake.asked = options;
            return access;
        };
    });

    await page.goto(url);
    await page.waitForFunction(
        () => document.getElementById('range').textContent !== '');

    /* The octave, before anything has started: the keys are drawn and the
       buttons move them whether or not there is a synth. */
    const range = await page.textContent('#range');

    check(/^C3/.test(range), `the range shows at load: ${range}`);

    await page.click('#up');

    const up = await page.textContent('#range');

    check(/^C4/.test(up), `an octave up: ${up}`);

    await page.click('#down');
    check(await page.textContent('#range') === range, 'and an octave back');

    /* The live-input controls before anything has started: the window is a
       choice until the synth takes one, and the microphone cannot be asked for
       until there is a node to connect it to. */
    check(await page.isDisabled('#mic') &&
          await page.isDisabled('#midi') &&
          !(await page.isDisabled('#window')),
          'the microphone and MIDI wait for a synth and the window is still '
          + 'a choice');

    /* Start, then a piece, and the knobs it declared. */
    await page.click('#start');
    await page.waitForFunction(
        () => !document.getElementById('loadpiece').disabled,
        null, { timeout: 60000 });
    check(true, 'the synth started');

    /* And after it: the window is spent, and the microphone is offerable --
       on 127.0.0.1, which is a secure context, so micAvailable() is true and
       the button is live. Clicking it is mictest.mjs's, which has a browser
       launched with a fake device to answer with; here the claim is only that
       the page put the two controls in the right state. */
    check(await page.isDisabled('#window') &&
          !(await page.isDisabled('#mic')) &&
          !(await page.isDisabled('#midi')),
          'the window is fixed once the synth has one, and the microphone is '
          + 'offered');

    /* ---- the sequencer, in the document ---- */

    /* The mode the page opens on, and the pane it opens on, untiled.
     *
     * Every other claim about the tracks is panecheck's, and panecheck is
     * about a layout: it adopts the pane into a leaf and CSS of its own
     * takes over. So the one thing it cannot see is whether the section
     * is showing in the document the layout is built from -- and it was
     * not. The markup said `hidden' where a pane that starts closed says
     * `data-pane-off', and nothing clears `hidden': panes.js will not
     * touch it on purpose, because a page that uses the attribute for
     * ordinary hiding would otherwise lose panes to the layout for good.
     * Both attributes are `display: none !important', so the Sequencer
     * was a pane with nothing in it in every window narrower than the
     * tiler turns on at.
     */
    check(await page.evaluate(
              () => document.getElementById('mode').value === 'seq' &&
                    document.getElementById('seqview').checkVisibility()),
          'the page opens on the sequencer, and it is showing');

    await page.evaluate(() => window.solo.settled());
    await page.waitForFunction(() => window.solo.tracks().length > 0,
                               null, { timeout: 60000 });

    const seqTracks = await page.evaluate(() => window.solo.tracks());

    check(seqTracks.length === 4,
          `the sequence is four tracks: ${seqTracks.length}`);

    /* A menu on every one of them, which is what the mode is for: its
       piece declares no instruments, so every channel is the page's to
       aim. A shipped piece's tracks have a name there instead. */
    check(await page.$$eval('#tracks .trackpick', (m) => m.length) === 4,
          'and each one has a menu for what plays it');

    check(await page.evaluate(() => window.solo.drawing().seq),
          'and they are asking for frames');

    /* ---- a drum track is one row ---- */

    /* An instrument that ignores the note it is sent sounds the same on
     * every row, so a ladder over one is a ladder that does nothing. The
     * page sets that track's grid to a single row -- a command, because
     * the grid is playing and `rows' is a param that is heard.
     *
     * Kick 909 and Tom 808 are both `category "Drums"'. The kick ignores
     * the note and the tom does not, which is the whole reason the rule
     * reads the graph rather than the category, and is why both are here.
     */
    const rowsOf = (n) => page.evaluate(
        (i) => window.solo.tracks()[i]?.rows ?? -1, n);

    const settle = async (i, want) =>
    {
        await page.waitForFunction(
            ([j, r]) => window.solo.tracks()[j]?.rows === r,
            [i, want], { timeout: 30000 }).catch(() => {});

        return rowsOf(i);
    };

    await page.selectOption('#tracks .track:nth-child(1) select.trackpick',
                            'kick909.dsp');
    await page.evaluate(() => window.solo.settled());

    check(await settle(0, 1) === 1,
          'a track aimed at Kick 909, which ignores the note, is one row');

    /* And the strip is the height one row asks for rather than six.
     *
       Waited for rather than read straight off: the row count comes back
       in a message and the canvas is resized in the draw loop after it,
       so the element is a frame behind the number above. */
    const heights = () => page.$$eval('#tracks .trackgrid',
        (all) => all.map((c) => Math.round(c.getBoundingClientRect().height)));

    await page.waitForFunction(() =>
    {
        const all = [...document.querySelectorAll('#tracks .trackgrid')];

        return all.length > 1 &&
               all[0].getBoundingClientRect().height <
               all[1].getBoundingClientRect().height;
    }, null, { timeout: 30000 }).catch(() => {});

    const strips = await heights();

    check(strips[0] < strips[1],
          `and its strip is shorter than a pitched one: ${strips[0]} ` +
          `against ${strips[1]}`);

    await page.selectOption('#tracks .track:nth-child(2) select.trackpick',
                            'tom808.dsp');
    await page.evaluate(() => window.solo.settled());

    check(await settle(1, 6) === 6,
          'a track aimed at Tom 808, which is a drum that reads the note, ' +
          'keeps its ladder');

    await page.selectOption('#tracks .track:nth-child(1) select.trackpick',
                            'ts1.dsp');
    await page.evaluate(() => window.solo.settled());

    check(await settle(0, 6) === 6,
          'and a synth back on the first one gives its rows back');

    /* ---- and the box keeps the page's own sequence ---- */

    /* Going to look at a piece and coming back must come back to the
     * sequence, and "does the box hold a gen::grid" was not the way to
     * ask: gen/scratch.gen is five of them. Opening it and returning
     * played a shipped piece as the sequence -- no menus, because
     * scratch declares its own instruments, and nothing the mode is
     * about.
     */
    /* And the other way: the page opened on the sequence, so entering a
       piece must load the piece the menu names rather than the sequence
       still in the box -- or the menu says ebb.gen over the sequence, and
       choosing ebb.gen does nothing because the menu already says so. */
    await page.selectOption('#mode', 'piece');
    await page.evaluate(() => window.solo.settled());

    check(await page.evaluate(() =>
              document.getElementById('piece').value === 'ebb.gen' &&
              /^name "Ebb";/m.test(document.getElementById('gen').value) &&
              /Loaded Ebb/.test(
                  document.getElementById('status').textContent)),
          'entering a piece loads the one the menu names: ebb.gen');

    await page.selectOption('#piece', 'scratch.gen');
    await page.evaluate(() => window.solo.settled());

    await page.selectOption('#mode', 'seq');
    await page.evaluate(() => window.solo.settled());
    await page.waitForFunction(() => window.solo.tracks().length > 0,
                               null, { timeout: 60000 });

    check(await page.evaluate(() => window.solo.tracks().length) === 4 &&
          await page.$$eval('#tracks .trackpick', (m) => m.length) === 4,
          'and scratch.gen, which is grids too, does not become the ' +
          'sequence');

    check(await page.evaluate(() => window.solo.drawing().seq),
          'and the tracks draw again on the way back');

    /* ---- the tempo ---- */

    /* A sequencer with no tempo is a sequencer that plays at one speed,
     * and the page had none at all -- the number was in the text it wrote
     * and nowhere a person could reach.
     *
     * What is checked is what is *played*, off the note tape, because a
     * control that moved a number in a box and nothing else would pass
     * every check that asks the box. The sequence's first track is four on
     * the floor over `period = 0.25 beats', so the gap between its onsets
     * is one beat: 60/112 at the tempo the page writes, and twice that at
     * half of it.
     */
    check(await page.evaluate(
              () => document.getElementById('tempo').value === '112' &&
                    !document.getElementById('tempo').disabled),
          'the sequence offers its tempo, at what the piece says: 112');

    /* The median gap between onsets, which is a beat. Median rather than
       the first gap: four tracks sink to four channels and the tape holds
       all of them, so what is wanted is the spacing that repeats. */
    const beat = async () =>
    {
        await page.waitForFunction(() => window.solo.notes().length >= 6,
                                   null, { timeout: 30000 });

        const at = await page.evaluate(
            () => window.solo.notes().map((n) => n.at).sort((a, b) => a - b));
        const gaps = at.slice(1)
                       .map((v, i) => v - at[i])
                       .filter((g) => g > 1e-6)
                       .sort((a, b) => a - b);

        return gaps[Math.floor(gaps.length / 2)];
    };

    await page.click('#play');

    const fast = await beat();

    check(Math.abs(fast - 60 / 112) < 0.02,
          `and a beat is that long: ${fast.toFixed(3)}s, 60/112 is ` +
          `${(60 / 112).toFixed(3)}`);

    await page.click('#stop');
    await page.click('#rewind');
    await page.fill('#tempo', '56');
    await page.dispatchEvent('#tempo', 'change');
    await page.evaluate(() => window.solo.settled());
    await page.click('#play');

    const slow = await beat();

    check(Math.abs(slow - 60 / 56) < 0.04,
          `half the tempo is twice the beat: ${slow.toFixed(3)}s, 60/56 ` +
          `is ${(60 / 56).toFixed(3)}`);

    await page.click('#stop');

    /* And it is written down. A tempo that moved what was playing and
       left the document saying something else would come back at the
       document's the moment anything reloaded -- a mode switch is
       enough. */
    check(/tempo\s+56\s*;/.test(await page.inputValue('#gen')),
          'and the piece\'s own text says so');

    await page.selectOption('#mode', 'patch');
    await page.evaluate(() => window.solo.settled());

    check(await page.evaluate(
              () => document.getElementById('tempo').disabled),
          'a patch has no tempo, and the control says so');

    await page.selectOption('#mode', 'seq');
    await page.evaluate(() => window.solo.settled());

    check(await page.evaluate(
              () => document.getElementById('tempo').value === '56'),
          'and coming back to the sequence comes back at 56');

    /* A number past either end of the range, which a number input will
       hold quite happily. Taken to the end rather than ignored: a box
       left reading 500 over a piece still going at 56 is a control saying
       something that is not so, and nothing on the strip says which of
       the two is playing. */
    await page.fill('#tempo', '500');
    await page.dispatchEvent('#tempo', 'change');
    await page.evaluate(() => window.solo.settled());

    check(await page.evaluate(
              () => document.getElementById('tempo').value === '300'),
          'a tempo past the top of the range is taken to the top');

    check(/tempo\s+300\s*;/.test(await page.inputValue('#gen')),
          'and that is what the text says too');

    /* And a box somebody emptied is not an instruction. */
    await page.fill('#tempo', '');
    await page.dispatchEvent('#tempo', 'change');
    await page.evaluate(() => window.solo.settled());

    check(await page.evaluate(
              () => document.getElementById('tempo').value === '300'),
          'an empty box goes back to what is playing');

    await page.fill('#tempo', '56');
    await page.dispatchEvent('#tempo', 'change');
    await page.evaluate(() => window.solo.settled());

    /* A piece written entirely in seconds is one the tempo cannot reach,
       which is most of the corpus. Offered where it means something and
       dimmed where it does not, the way the desktop's spinner has always
       been (ComposerWindow.cpp). */
    await page.selectOption('#mode', 'piece');
    await page.selectOption('#piece', 'ebb.gen');
    await page.evaluate(() => window.solo.settled());

    check(await page.evaluate(() =>
          {
              const box = document.getElementById('tempo');

              return box.disabled &&
                     /seconds/.test(
                         document.getElementById('tempolabel').title);
          }),
          'a piece written in seconds dims it, and says why');

    /* ---- and the speed, which every piece has ---- */

    /* The control that reaches the piece the tempo cannot. A tempo scales
     * beat-valued durations and leaves `period = 0.25 s' where it is, so
     * dimming the box and saying so is only half an answer: the other
     * half is a control that turns the transport itself.
     *
     * Measured against the wall clock, because that is what it means. The
     * clock reads transport seconds, so at 2x twice as many of them pass
     * in a second of real time -- and the piece under it is ebb.gen,
     * whose every duration is in seconds and which the tempo above could
     * not move at all.
     */
    check(await page.evaluate(
              () => !document.getElementById('speed').disabled),
          'and the speed beside it is live where the tempo is not');

    /* Transport seconds per real second. The clock reads to a tenth, so
       the window is long enough that a tenth is not the answer. */
    const advance = async (x) =>
    {
        const clock = () => page.evaluate(() =>
        {
            const [m, sec] =
                document.getElementById('clock').textContent.split(':');

            return Number(m) * 60 + Number(sec);
        });

        await page.fill('#speed', String(x));
        await page.dispatchEvent('#speed', 'input');
        await page.waitForTimeout(250);

        const a = await clock();

        await page.waitForTimeout(1500);

        return (await clock() - a) / 1.5;
    };

    await page.click('#play');

    const atOne = await advance(1);
    const atTwo = await advance(2);
    const atHalf = await advance(0.5);

    check(Math.abs(atTwo / atOne - 2) < 0.3,
          `twice the speed is twice the clock: ${atTwo.toFixed(2)} ` +
          `against ${atOne.toFixed(2)} transport seconds a second`);

    check(Math.abs(atHalf / atOne - 0.5) < 0.2,
          `and half is half: ${atHalf.toFixed(2)} against ` +
          `${atOne.toFixed(2)}`);

    /* And turning it does not move the playhead.
     *
     * The clock is pinned to the output as a line -- a transport time at
     * a frame, and a slope -- and a speed change turns that line through
     * where it has got to. Setting the slope without moving the pin
     * would rescale the whole run back to its origin, and the piece would
     * skip or repeat a stretch of itself on every nudge. Ten seconds in
     * at 1x, a change to 3x would land the playhead at 30. */
    await advance(1);

    const wasAt = await page.evaluate(
        () => document.getElementById('clock').textContent);

    await page.fill('#speed', '3');
    await page.dispatchEvent('#speed', 'input');
    await page.waitForTimeout(120);

    const nowAt = await page.evaluate(
        () => document.getElementById('clock').textContent);

    const secs = (t) =>
    {
        const [m, sec] = t.split(':');

        return Number(m) * 60 + Number(sec);
    };

    check(secs(nowAt) - secs(wasAt) < 1,
          `and turning it does not move the playhead: ${wasAt} to ` +
          `${nowAt}`);

    const reading = await page.evaluate(
        () => document.getElementById('speedis').textContent);

    check(reading === '3.00\u00d7',
          `the reading says where the slider is: ${reading}`);

    /* And the clock the page would stamp a command with is still the
     * clock the module is running.
     *
     * Transport zero, the speed and the frame the output has reached are
     * one line, and clock.js walks it from the other end (TransportClock)
     * to turn a transport time into a frame. Turning the speed pins that
     * line where the clock has got to, so transport zero stops being the
     * frame the run was started at -- and a tape message that carried the
     * pin instead of the zero, or the zero without the speed, would leave
     * every stamp off by whatever the slider was moved to. Checked at 3x,
     * where a factor of three is not a rounding error. */
    const line = await page.evaluate(() => window.solo.transport());

    check(Math.abs((line.frame - line.origin) * line.speed / line.rate -
                   line.now) < 0.05,
          `and the tape's clock is one line: (${Math.round(line.frame)} - ` +
          `${Math.round(line.origin)}) * ${line.speed} / ${line.rate} is ` +
          `${((line.frame - line.origin) * line.speed /
              line.rate).toFixed(2)}, now is ${line.now.toFixed(2)}`);

    await page.click('#stop');

    /* It belongs to the listener rather than to the piece: no statement
       writes it, nothing reads it back out of a file, and choosing
       another piece leaves it where it was put. */
    await page.fill('#speed', '1.5');
    await page.dispatchEvent('#speed', 'input');
    await page.selectOption('#piece', 'colony.gen');
    await page.evaluate(() => window.solo.settled());

    check(await page.evaluate(
              () => document.getElementById('speed').value === '1.5'),
          'and it survives a piece being chosen: it is not the piece\'s');

    await page.fill('#speed', '1');
    await page.dispatchEvent('#speed', 'input');

    await page.selectOption('#mode', 'seq');
    await page.evaluate(() => window.solo.settled());

    /* ---- the instrument's parameters ---- */

    /* The panel this page has never had. Patch mode, because that is the
       simplest thing it can be over: one .dsp on one channel, loaded by
       Start.

       What is checked here is the half that needs a browser -- that the
       module's description became elements, and that moving one of them
       reaches the arg. That the description itself is right, and is the
       same description the desktop draws, is panelcheck's and
       panelcheck.mjs's. */
    await page.waitForSelector('#params .panelrow', { timeout: 60000 });

    const chanPanel = await page.evaluate(() => window.solo.chanParams());
    const drawn = await page.evaluate(() => ({
        rows: document.querySelectorAll('#params .panelrow').length,
        groups: document.querySelectorAll('#params .panelgroup').length,
        sliders: document.querySelectorAll(
            '#params input[type="range"]').length,
    }));

    check(chanPanel !== null && drawn.rows === chanPanel.rows.length,
          `the channel's parameters drew: ${drawn.rows} rows of ` +
          `${chanPanel?.rows?.length}`);

    check(drawn.groups === chanPanel.groups.length,
          `and a foldable block per group: ${drawn.groups} of ` +
          `${chanPanel.groups.length} (${chanPanel.groups.join(', ')})`);

    /* Every row of the .dsp this page loads is a slider; a selector would
       be a <select> and is counted out here so the claim stays exact. */
    const ranges = chanPanel.rows.filter((r) => r.kind === 0).length;

    check(drawn.sliders === ranges,
          `and a slider for each of the ${ranges} that is one`);

    /* A nudge, with the keyboard, because that is a real input event from
       the browser rather than a synthesised one.
     *
       Then the poll, which pushes what the module holds back into the
       widgets. That is the assertion: if the edit had not reached the arg
       the slider would snap back to where it was, since the page draws
       what the module says and never what it typed. */
    await page.evaluate(() =>
        document.querySelectorAll('#params input[type="range"]')[0].focus());

    const wasParam = await page.evaluate(() =>
        document.querySelectorAll('#params input[type="range"]')[0].value);

    await page.keyboard.press('ArrowRight');
    await page.keyboard.press('ArrowRight');

    const nudged = await page.evaluate(() =>
        document.querySelectorAll('#params input[type="range"]')[0].value);

    await page.evaluate(() => window.solo.pollChanParams());

    const polled = await page.evaluate(() =>
    {
        const range = document.querySelectorAll(
            '#params input[type="range"]')[0];

        return { value: range.value,
                 shown: range.nextElementSibling.value };
    });

    check(nudged !== wasParam && polled.value === nudged,
          `moving one reaches the arg and survives the poll: ` +
          `${wasParam} -> ${nudged}, and the module says ${polled.value}`);

    check(Number(polled.shown) === Number(nudged),
          `and the number box beside it agrees: ${polled.shown}`);

    /* And the poll does not type over the person.
     *
       The module's value arrives four times a second whether or not anyone
       asked, and it used to be written into every box on the panel. A number
       takes longer than a quarter second to type, so the digits were being
       replaced by the value that was still there -- an edit that could not be
       made at all rather than one that failed. Half a number is left in the
       box here, deliberately unconfirmed. */
    await page.evaluate(() =>
        document.querySelector('#params .value').focus());

    await page.keyboard.press('Control+A');
    await page.keyboard.type('0.12');

    await page.evaluate(() => window.solo.pollChanParams());
    await page.evaluate(() => window.solo.pollChanParams());

    const typing = await page.evaluate(() =>
        document.querySelector('#params .value').value);

    check(typing === '0.12',
          `a half-typed number survives the poll: "${typing}"`);

    /* While it is being typed into, the box is not showing the module: it
       holds what was typed and the module still holds something else. */
    const apart = await page.evaluate(() =>
    {
        const box = document.querySelector('#params .value');

        return { shown: box.value, range: box.previousElementSibling.value };
    });

    check(Number(apart.shown) !== Number(apart.range),
          `and holds what was typed, not what the module has: box ` +
          `${apart.shown}, slider ${apart.range}`);

    /* And it is let go of the moment it stops being typed into. Leaving the
       box is what confirms the number, so this is the edit landing and the
       box going back to showing what the module has -- which is the same
       thing, and is why the two agree again. */
    await page.evaluate(() => document.querySelector('#params .value').blur());
    await page.evaluate(() => window.solo.pollChanParams());

    const together = await page.evaluate(() =>
    {
        const box = document.querySelector('#params .value');

        return { shown: box.value, range: box.previousElementSibling.value };
    });

    check(Number(together.shown) === Number(together.range),
          `and the box follows the arg again once it is left alone: box ` +
          `${together.shown}, slider ${together.range}`);

    await page.selectOption('#mode', 'piece');
    await page.selectOption('#piece', PIECE);

    /* There are three loads on the way here, and every one of them ends
       by replacing every element in the knobs row: the mode switch loads
       what piece mode is about to play, choosing a piece loads that, and
       Load loads it again -- deliberately, since Load re-reads the .gen
       in the textarea, which is editable.

       This used to wait for "a slider that is not the one the first load
       drew", on the reasoning that there were two. There are three, and
       the handle the wait compares against can already be stale when the
       wait first runs, in which case it returns at once having waited for
       nothing. Then the third load lands in the middle of the nudge
       below, the focused slider is replaced under it, the arrow key goes
       to <body>, and the value has not moved -- which is what CI printed,
       on a machine slow enough to let the load get that far behind.

       So the page is asked instead. It knows what it has queued; nothing
       out here can know it by counting. */
    await page.waitForSelector('#knobs input[type="range"]',
                               { timeout: 60000 });
    await page.click('#loadpiece');
    await page.evaluate(() => window.solo.settled());

    /* Drawn by panel.js off the module's own description of them
       (src/KnobPanel.cpp), so a row carries the number a command names it
       by as its id and the value spelled at the resolution its range asks
       for -- not at whatever three significant figures came to. */
    const knobs = await page.evaluate(() =>
        [...document.querySelectorAll('#knobs .panelrow')].map((line) =>
        {
            const range = line.querySelector('input[type="range"]');

            return { label: line.querySelector('label').textContent,
                     value: range.value,
                     shown: range.nextElementSibling.value };
        }));

    check(knobs.length > 0 &&
          knobs.every((k) => k.shown !== '' && k.label !== ''),
          `${PIECE}'s knobs drew, each with its label and its value: ` +
          knobs.map((k) => `${k.label}=${k.shown}`).join(', '));

    /* Moving a slider moves the number beside it -- the box a remote
       peer's move writes to on the room page as well.
     *
       With the keyboard, because that is a real input event from the
       browser: fill() sets the value itself and synthesises one, which
       is a weaker claim about a range input and a poor one to debug.
       Both numbers go in the message, since a slider that did not move
       and a number that did not follow it are different bugs. */
    const firstKnob = '#knobs .panelrow input[type="range"]';

    await page.focus(firstKnob);

    const was = await page.inputValue(firstKnob);

    await page.keyboard.press('ArrowRight');

    let now = await page.inputValue(firstKnob);

    /* At the top of its range there is nowhere rightwards to go. */
    if (now === was)
    {
        await page.keyboard.press('ArrowLeft');
        now = await page.inputValue(firstKnob);
    }

    const shown = await page.evaluate(
        (sel) => document.querySelector(sel).nextElementSibling.value,
        firstKnob);

    check(now !== was && Number(shown) === Number(now),
          `a nudge moves the slider and the number beside it: ` +
          `${was} -> ${now}, showing ${shown}`);

    /* An emptied box is not a knob set to zero.
     *
       A knob is delivered by a stamped command and not by tw_panel_edit, so
       nothing between the box and the write reads what is in it: Number('')
       is 0, and 0 is outside the range of plenty of a piece's knobs. Nor is
       a number typed past the end of the range applied unheld while the
       slider beside it clamps. */
    const knobBox = '#knobs .panelrow .value';

    await page.fill(knobBox, '');
    await page.evaluate((sel) => document.querySelector(sel).blur(), knobBox);
    await new Promise((r) => setTimeout(r, 300));

    const emptied = await page.evaluate((sel) =>
    {
        const box = document.querySelector(sel);

        return { shown: box.value,
                 range: box.previousElementSibling.value };
    }, knobBox);

    check(Number(emptied.range) === Number(now) &&
          Number(emptied.shown) === Number(now),
          `emptying a knob's box moves nothing: still ${emptied.shown}`);

    /* And past the end of its travel it goes to the end and not past it. */
    const knobMax = await page.evaluate((sel) =>
        document.querySelector(sel).previousElementSibling.max, knobBox);

    await page.fill(knobBox, String(Number(knobMax) * 10 + 1));
    await page.evaluate((sel) => document.querySelector(sel).blur(), knobBox);
    await new Promise((r) => setTimeout(r, 300));

    const past = await page.evaluate((sel) =>
    {
        const box = document.querySelector(sel);

        return { shown: box.value,
                 range: box.previousElementSibling.value };
    }, knobBox);

    check(Number(past.shown) === Number(knobMax) &&
          Number(past.range) === Number(knobMax),
          `and a number past its end is held to it: ${past.shown} of ` +
          `${knobMax}`);

    /* A computer key holds an on-screen key and lets it go.
     *
       The Escape is how the page hands the keyboard back from a box that
       is being typed into, and it is all this needs now. It used to
       reach in and blur whatever had the focus, because a slider dragged
       or a list chosen from kept the letters for itself; neither does
       any more (keyfocus.js). */
    await page.keyboard.press('Escape');
    await page.keyboard.down('z');
    await new Promise((r) => setTimeout(r, 200));

    const held = await page.evaluate(
        () => document.querySelectorAll('#keys .held').length);

    await page.keyboard.up('z');
    await new Promise((r) => setTimeout(r, 200));

    const released = await page.evaluate(
        () => document.querySelectorAll('#keys .held').length);

    check(held === 1 && released === 0,
          'a computer key holds one key on screen and lets it go ' +
          `(${held} down, ${released} up)`);

    /* And the same key typed into a text box plays nothing. */
    await page.evaluate(() =>
    {
        const d = document.getElementById('piecesource');

        d.hidden = false;
        d.open = true;
    });
    await page.click('#gen');
    await page.keyboard.down('z');
    await new Promise((r) => setTimeout(r, 200));

    const typed = await page.evaluate(
        () => document.querySelectorAll('#keys .held').length);

    await page.keyboard.up('z');
    check(typed === 0,
          'and a key typed into the source is editing, not a note');

    /* And a list touched with the pointer does not take them.
     *
       Choosing a piece or a patch left the focus on the <select>, and
       from then on every letter went to the list -- a keyboard that had
       to be won back by finding somewhere harmless to click. The Escape
       is for the browser's own popup, which the click opens and which
       owns the keyboard while it is up; the list keeps the focus after
       it, which is the state that used to be silent. What the rule is,
       and what happens to a list that is tabbed to rather than clicked,
       is keycheck.mjs's. */
    await page.keyboard.press('Escape');
    await page.click('#piece');
    await page.keyboard.press('Escape');

    await page.keyboard.down('z');
    await new Promise((r) => setTimeout(r, 200));

    const afterList = await page.evaluate(() => ({
        held: document.querySelectorAll('#keys .held').length,
        focused: document.activeElement?.id ?? '',
    }));

    await page.keyboard.up('z');

    check(afterList.held === 1,
          'and a key after the piece list is a note, with the list still ' +
          `focused (${afterList.focused || 'nothing'})`);

    /* ---- a MIDI keyboard ---- */

    await page.click('#midi');
    await page.waitForFunction(
        () => document.getElementById('midistatus').textContent !== 'asking...');

    check(await page.textContent('#midistatus') === 'Fake Keys' &&
          await page.evaluate(() => window.fakeMidi.asked?.sysex === false),
          'MIDI in asks without sysex and names the input: ' +
          await page.textContent('#midistatus'));

    const midiSend = (bytes) =>
        page.evaluate((b) => window.fakeMidi.send('k1', b), bytes);
    const sounding = () => page.evaluate(() => window.solo.sounding());
    const keyChannel = Number(await page.inputValue('#keychan'));

    /* Channel 4 on the wire, and the page's channel in the synth. */
    await midiSend([0x93, 60, 37]);
    await midiSend([0x93, 60, 90]);

    const midiDown = await sounding();

    check(midiDown.length === 1 && midiDown[0].note === 60 &&
          midiDown[0].velocity === 37 && midiDown[0].count === 1 &&
          midiDown[0].channel === keyChannel,
          'a MIDI note on plays its note at its velocity on the page\'s ' +
          'channel, and a repeat is dropped: ' + JSON.stringify(midiDown));

    /* The focus moving is the computer keys' loss and not the MIDI
       keyboard's: its note off arrives wherever the focus is. */
    await page.evaluate(() => window.dispatchEvent(new Event('blur')));
    await page.keyboard.press('Escape');

    check((await sounding()).length === 1,
          'and it stays down across a blur and an Escape');

    await midiSend([0x93, 60, 0]);

    check((await sounding()).length === 0,
          'a note on at velocity 0 lets it go');

    /* The sustain pedal, onto the key channel's SusPedal. */
    const pedals = () => page.evaluate(() => window.chanargs
        .filter((c) => c.name === 'SusPedal')
        .map((c) => `${c.channel}:${c.value}`).join(' '));

    await midiSend([0xb3, 64, 127]);
    await midiSend([0xb3, 64, 0]);

    check(await pedals() === `${keyChannel}:127 ${keyChannel}:0`,
          'the sustain pedal goes down and up on the key channel\'s ' +
          `SusPedal: ${await pedals()}`);

    await midiSend([0xb3, 64, 127]);

    await midiSend([0x80, 60, 0]);
    await midiSend([0x90, 62, 100]);
    await page.evaluate(() => window.fakeMidi.unplug('k1'));

    check((await sounding()).length === 0 &&
          (await pedals()).endsWith(`${keyChannel}:127 ${keyChannel}:0`) &&
          /no MIDI inputs/.test(await page.textContent('#midistatus')),
          'an unplugged keyboard lets go of what it held, pedal included: ' +
          await page.textContent('#midistatus'));

    await page.evaluate(() => window.fakeMidi.plug('k2', 'Other Keys'));
    await page.evaluate(() => window.fakeMidi.send('k2', [0x90, 64, 100]));

    check((await sounding()).length === 1 &&
          await page.textContent('#midistatus') === 'Other Keys',
          'one plugged in later is listened to');

    await page.click('#midi');

    check((await sounding()).length === 0 &&
          await page.textContent('#midi') === 'MIDI in',
          'and closing MIDI in lets go of it');

    /* ---- the composer view ---- */

    /* A piece with a picture that is a control: colony's Life board.
       Choosing it loads it; the mirror is sent the same load and says
       what it has, which is where the buttons below come from. */
    await page.selectOption('#piece', COMPOSER_PIECE);
    await page.waitForSelector('#composerstages button', { timeout: 60000 });

    /* The first frame that reaches the page: the canvas is sized to the
       drawing and the drawing is on it. A blank canvas of the right size
       would mean the list arrived and replayed into nothing. */
    await page.waitForFunction(() =>
    {
        const c = document.getElementById('composer');

        return c.width > 0 && c.height > 0;
    }, null, { timeout: 60000 });

    const ink = () => page.evaluate(() =>
    {
        const c = document.getElementById('composer');
        const d = c.getContext('2d').getImageData(0, 0, c.width, c.height)
                   .data;
        let sum = 0;

        for (let i = 0; i < d.length; i += 4)
            sum += d[i] + d[i + 1] + d[i + 2];

        return sum;
    });

    const size = await page.$eval('#composer',
                                  (c) => ({ w: c.width, h: c.height }));

    check(await ink() > 0,
          `the piece's picture drew, ${size.w} by ${size.h} device pixels`);

    /* And it is drawn to the width of the view, not squeezed into its
       height. A composer canvas is one row per chain, so fitting both
       dimensions lets a tall piece decide the zoom -- ten chains in a box
       half a screen tall came out at a quarter scale, which is what this
       is here to stop (CanvasContent::zoomToWidth). */
    const fitted = await page.evaluate(() =>
    {
        const c = document.getElementById('composer');
        const s = document.getElementById('composerscroll');

        return { wide: s.scrollWidth > s.clientWidth + 1,
                 width: parseFloat(c.style.width),
                 box: s.clientWidth };
    });

    check(!fitted.wide && fitted.width > fitted.box * 0.5,
          `and to the width of the view: ${fitted.width} in ${fitted.box}, ` +
          `${fitted.wide ? 'scrolling sideways' : 'no sideways scroll'}`);

    const stages = await page.$$eval('#composerstages button',
                                     (bs) => bs.map((b) => b.textContent));

    check(stages.length > 0,
          `${COMPOSER_PIECE} offers its controls: ${stages.join(', ')}`);

    await page.click('#composerstages button');
    await page.waitForFunction(
        () => /^Painting /.test(
            document.getElementById('composerstatus').textContent),
        null, { timeout: 60000 });
    check(true, `${stages[0]} enlarged`);

    /* A drag across the enlarged board. Nothing is playing, so the board
       changes only if the drag reached the composer -- which it can only
       do by leaving the canvas as a gesture, being stamped as a command,
       and being applied in the mirror at its time. */
    const before = await ink();

    /* The scroller's box and not the canvas's: the element is as big as
       the whole drawing and the scroller clips it, so a point outside
       what is on screen is a point some other element receives. The
       enlarged picture fills the visible part by construction -- it
       follows the viewport, which is the reason the content is told what
       the viewport is at all -- so the middle of this is the middle of
       the board. */
    await page.locator('#composerscroll').scrollIntoViewIfNeeded();

    const box = await page.$eval('#composerscroll', (d) =>
    {
        const r = d.getBoundingClientRect();

        return { x: r.x, y: r.y, w: d.clientWidth, h: d.clientHeight };
    });

    await page.mouse.move(box.x + box.w * 0.3, box.y + box.h * 0.5);
    await page.mouse.down();

    for (let i = 1; i <= 10; i++)
    {
        await page.mouse.move(box.x + box.w * (0.3 + 0.04 * i),
                              box.y + box.h * 0.5);
        await new Promise((r) => setTimeout(r, 40));
    }

    await page.mouse.up();
    await new Promise((r) => setTimeout(r, 1000));

    const after = await ink();

    check(before !== after,
          'a drag on the enlarged board went out as commands and came ' +
          'back as a board that has changed');

    await page.keyboard.press('Escape');
    await page.waitForFunction(
        () => !/^Painting /.test(
            document.getElementById('composerstatus').textContent),
        null, { timeout: 60000 });
    check(true, 'and Escape puts it back');

    /* A stage's params handle -- the three little sliders in its title
       bar -- asks for a popover beside the box, and what goes in it comes
       from the piece the mirror is holding: the values as it is playing
       them, the units the plugin declared, and the piece knob a param is
       read through where there is one. The canvas is asked where the
       handle is, since the layout is its own. */
    const handle = await page.evaluate(() => window.solo.handleOf(0, 0));

    if (handle.x < 0)
        check(false, 'the first stage has no params handle');
    else
    {
        await page.mouse.click(box.x + handle.x, box.y + handle.y);
        await page.waitForFunction(
            () => !document.getElementById('composerparams').hidden,
            null, { timeout: 15000 });

        const title = await page.textContent('#composerparams .menutitle');
        const rows = await page.evaluate(() => window.solo.params());

        check(rows.length > 0,
              `the params handle opens ${title}: ${rows.join(', ')}`);

        /* And the rows can be typed into, which is the thing this panel
         * could not do at all.
         *
         * A number box, found by asking the DOM rather than by knowing
         * which stage the piece opens with: what the rows are is the
         * module's and is checked where the module is
         * (wasm/web/panelcheck.mjs). What is checked here is the round
         * trip -- an element takes a number, the edit leaves as a command,
         * every instance applies it, and the description that comes back
         * carries it.
         */
        const box2 = await page.$(
            '#composerparams .panelrow input[type="number"]:not([disabled])');

        if (box2 === null)
            check(false, 'the popover has a number to type into');
        else
        {
            const row = await box2.evaluate(
                (e) => e.closest('.panelrow').dataset.row);
            const was = await box2.inputValue();
            const want = String(Number(was) + 1);
            const genWas = await page.inputValue('#gen');

            await box2.fill(want);
            await box2.press('Enter');

            /* The panel follows the piece rather than the box: what is
               waited for is the description coming back with the new
               number in it, which is the module having taken the edit. */
            await page.waitForFunction(
                ([id, value]) => document.querySelector(
                    `#composerparams .panelrow[data-row="${id}"] ` +
                    'input[type="number"]')?.value === value,
                [row, want], { timeout: 15000 });

            check(true, `a stage's ${row} took ${want} and came back with ` +
                        'it');

            /* And the box has it, which is what a Load, a mode switch and
               a Save read: an edit that stayed in the worklet's copy of the
               piece was gone at the next of those. */
            const genNow = await page.waitForFunction(
                (was) => document.getElementById('gen').value !== was,
                genWas, { timeout: 15000 })
                .then(() => page.inputValue('#gen'), () => genWas);
            const wasLines = genWas.split('\n');
            const changed = genNow.split('\n')
                .filter((l, i) => l !== wasLines[i]);

            check(changed.length === 1 && changed[0].includes(row) &&
                  changed[0].includes(want),
                  `and the piece's text carries it: ` +
                  `${JSON.stringify(changed)}`);
        }

        /* A value typed and then clicked away from, rather than entered.
         *
           Which is how most numbers get committed: a box reports on
           `change', and `change' fires when the focus leaves. The press
           that takes the focus away is also the press that closes the
           popover, and the popover closing used to throw away what the
           panel was about -- so the edit went to a TypeError instead of to
           the piece, and the number somebody had just typed vanished with
           the popover. Read back by opening it again, since by then there
           is nothing on screen to read. */
        const box3 = await page.$(
            '#composerparams .panelrow input[type="number"]:not([disabled])');

        if (box3 !== null)
        {
            const row = await box3.evaluate(
                (e) => e.closest('.panelrow').dataset.row);
            const want = String(Number(await box3.inputValue()) + 1);

            await box3.fill(want);

            /* Somewhere that is not the popover: this both blurs the box
               and closes it. */
            await page.mouse.click(box.x + box.w / 2, box.y + box.h - 4);

            await page.waitForFunction(
                () => document.getElementById('composerparams').hidden,
                null, { timeout: 15000 });

            await page.mouse.click(box.x + handle.x, box.y + handle.y);
            await page.waitForFunction(
                () => !document.getElementById('composerparams').hidden,
                null, { timeout: 15000 });

            const kept = await page.waitForFunction(
                ([id, value]) => document.querySelector(
                    `#composerparams .panelrow[data-row="${id}"] ` +
                    'input[type="number"]')?.value === value,
                [row, want], { timeout: 15000 }).then(() => true, () => false);

            check(kept,
                  `a stage's ${row} typed and clicked away from still ` +
                  `reached the piece: ${want}`);
        }

        /* And it goes away with the next press somewhere else. */
        await page.mouse.click(box.x + box.w / 2, box.y + box.h - 4);
        check(await page.$eval('#composerparams', (e) => e.hidden),
              'and the next press closes it');
    }

    /* ---- the piano roll ----
     *
     * The other canvas the mirror draws, and the one this page did not
     * have until it was drawn there: the desktop's RollCanvas over the
     * mirror's scheduler, in place of thirty seconds of tape drawn by
     * hand. What is checked here is what no headless gate can be --
     * that the pane is wired end to end, that a pointer on it reaches
     * the canvas in the worker, and that what comes back changes the
     * view.
     */
    await page.waitForFunction(() =>
    {
        const c = document.getElementById('rollcanvas');

        return c.width > 0 && c.height > 0;
    }, null, { timeout: 60000 });

    const rollInk = () => page.evaluate(() =>
    {
        const c = document.getElementById('rollcanvas');
        const d = c.getContext('2d').getImageData(0, 0, c.width, c.height)
                   .data;
        let sum = 0;

        for (let i = 0; i < d.length; i += 4)
            sum += d[i] + d[i + 1] + d[i + 2];

        return sum;
    });

    const rollSize = await page.$eval('#rollcanvas',
                                      (c) => ({ w: c.width, h: c.height }));

    check(await rollInk() > 0,
          `the piano roll drew, ${rollSize.w} by ${rollSize.h} device pixels`);

    /* The drawing is exactly the view: no scrollbar, and the element as
       wide as the box around it. A roll with a scroller would be a second,
       silent answer to where "now" is (src/RollCanvas.h). */
    const rollFit = await page.evaluate(() =>
    {
        const d = document.getElementById('roll');

        return { over: d.scrollWidth > d.clientWidth + 1 ||
                       d.scrollHeight > d.clientHeight + 1,
                 w: parseFloat(
                     document.getElementById('rollcanvas').style.width),
                 box: d.clientWidth };
    });

    check(!rollFit.over && Math.abs(rollFit.w - rollFit.box) <= 1,
          `and fills its box exactly: ${rollFit.w} in ${rollFit.box}, ` +
          `${rollFit.over ? 'and scrolls' : 'nothing to scroll'}`);

    /* A few seconds of the piece, so there is history to scrub through
       and a future to have been drawn ahead of the now-line. */
    await page.click('#play');
    await new Promise((r) => setTimeout(r, 2500));

    const live = await page.evaluate(() => window.solo.roll());

    check(live !== null && live.following && live.now > 0.5,
          `and follows the transport: the now-line is at ${live?.now
              ?.toFixed(2)}s`);

    /* A drag to the right pulls the past back under the now-line, which
       is what drops it out of follow mode. The pointer goes to the
       mirror, the canvas there decides what it meant, and what comes
       back is where the view ended up -- so this is the whole round
       trip, not a number the page kept for itself. */
    const rollBox = await page.$eval('#roll', (d) =>
    {
        const r = d.getBoundingClientRect();

        return { x: r.x, y: r.y, w: d.clientWidth, h: d.clientHeight };
    });

    await page.mouse.move(rollBox.x + rollBox.w * 0.4,
                          rollBox.y + rollBox.h * 0.5);
    await page.mouse.down();

    for (let i = 1; i <= 6; i++)
    {
        await page.mouse.move(rollBox.x + rollBox.w * (0.4 + 0.01 * i),
                              rollBox.y + rollBox.h * 0.5);
        await new Promise((r) => setTimeout(r, 40));
    }

    await page.mouse.up();
    await new Promise((r) => setTimeout(r, 400));

    const scrubbed = await page.evaluate(() => window.solo.roll());

    check(!scrubbed.following && scrubbed.now < live.now,
          `a drag scrubs back and drops out of follow: ${live.now.toFixed(2)}` +
          `s -> ${scrubbed.now.toFixed(2)}s`);

    /* And a double-click puts it back on the live edge, which is the way
       out of a scrub that does not need the edge found by hand. */
    await page.mouse.dblclick(rollBox.x + rollBox.w * 0.5,
                              rollBox.y + rollBox.h * 0.5);
    await page.waitForFunction(() => window.solo.roll()?.following === true,
                               null, { timeout: 15000 });
    check(true, 'and a double-click goes back to live');

    /* The wheel is time here, not scale: the roll has nothing to scroll,
       so a bare wheel means the span (canvasview.js, `wheelZooms'). */
    const spanBefore = (await page.evaluate(() => window.solo.roll())).spanPast;

    await page.mouse.move(rollBox.x + rollBox.w * 0.5,
                          rollBox.y + rollBox.h * 0.5);
    await page.mouse.wheel(0, -120);
    await page.waitForFunction(
        (was) => window.solo.roll()?.spanPast !== was,
        spanBefore, { timeout: 15000 });

    const spanAfter = (await page.evaluate(() => window.solo.roll())).spanPast;

    check(spanAfter < spanBefore,
          `and the wheel is time: ${spanBefore.toFixed(1)}s of history -> ` +
          `${spanAfter.toFixed(1)}s`);

    await page.click('#stop');

    /* ---- a channel the piece left for the page to aim ---- */

    /* A piece that names channels and declares no instrument of its own is
     * the case patch.js exists for: what sounds there is the page's default,
     * and a person may choose something else. Both halves go through the
     * module now -- it reads the .patch and puts it on the channel -- so what
     * is checked here is that a choice arrives, which is the one part of that
     * path no headless gate walks.
     */
    await page.selectOption('#mode', 'piece');
    await page.selectOption('#piece', AIMED_PIECE);
    await page.waitForSelector('#channels select', { timeout: 60000 });
    await page.evaluate(() => window.solo.settled());

    const rows = await page.$$eval('#channels .channel', (all) => all.length);

    check(rows > 0, `${AIMED_PIECE} left ${rows} channels for the page to aim`);

    /* A channel this piece actually aimed, with the parameters pane moved
     * onto it so that the patch chosen below and the controls moved after
     * it are the same channel.
     *
     * Not simply the pane's own channel: the keys' channel is in the list
     * whether or not the piece named it, and a channel with nothing on it
     * has no graph, no patches over that graph, and nothing for the rest
     * of this to be about. */
    const chan = await page.evaluate(() =>
    {
        const row = [...document.querySelectorAll('#channels .channel')]
            .find((r) => r.querySelector('select')?.value !== '');

        return Number(row?.querySelector('.edited')?.dataset.channel ?? -1);
    });

    check(chan >= 0, `${AIMED_PIECE} left an instrument on a channel`);

    await page.selectOption('#paramchan', String(chan));
    await page.waitForFunction(
        (c) => document.getElementById('paramwhat').textContent
                       .endsWith(`channel ${c + 1}`),
        chan, { timeout: 60000 });

    /* The first .patch the panel offers over this channel's graph, chosen.
     *
     * The panel and not the channels row: a patch is a .dsp and a column
     * of values over its knobs, so the menus ask the two questions at the
     * two altitudes -- which graph, in the row that aims the channel, and
     * which of the patches somebody saved over it, here among the
     * controls those values set.
     *
     * The status line says what went on by the title the file gives
     * itself, which only something that has read the file knows and
     * nothing on the page reads one. */
    const chosen = await page.evaluate(() =>
        [...document.getElementById('parampatch').options]
            .find((o) => o.value.endsWith('.patch'))?.value ?? '');

    check(chosen !== '', `channel ${chan + 1} offers a .patch to choose`);

    if (chosen !== '')
    {
        await page.selectOption('#parampatch', chosen);
        await page.waitForFunction(
            () => /on channel \d+\. Play\.$/.test(
                document.getElementById('status').textContent),
            null, { timeout: 60000 });
        await page.evaluate(() => window.solo.settled());

        check(true, `choosing ${chosen} loads it: ` +
                    `${await page.textContent('#status')}`);

        /* And the row says whether what is on the channel is still what the
         * file says. A patch just read is not edited; moving one of its
         * controls makes it so. The desktop has lit a Save button off this
         * since 2004, and the page could not say it at all until the slots
         * were shared (src/PatchSet.h).
         */
        const edited = () => page.$eval(
            `#channels .edited[data-channel="${chan}"]`, (m) => !m.hidden);

        check(await edited() === false,
              'and the row does not say it has been edited');

        /* Through the panel, which is how a person would do it: the page
           has no other door to a chanarg. An arrow key on the slider, for
           the reason the nudge above uses one -- it is one edit, and it is
           the one a person makes. */
        await page.evaluate(() => window.solo.pollChanParams());
        await page.focus('#params input[type="range"]');
        await page.keyboard.press('ArrowRight');

        await page.waitForFunction(
            (c) => !document.querySelector(
                `#channels .edited[data-channel="${c}"]`).hidden,
            chan, { timeout: 60000 });

        check(true, 'and moving one of its controls says it has');

        /* And Save hands the file over. A page has nowhere to write to, so
         * what a Save is here is a download -- which is what the desktop's
         * is too, from where a person is standing. The bytes are the
         * application's (thPatchCapture, thPatchCompose), so what comes
         * down opens there.
         */
        const [download] = await Promise.all([
            page.waitForEvent('download', { timeout: 60000 }),
            page.click(`#channels .save[data-channel="${chan}"]`),
        ]);

        const saved = fs.readFileSync(await download.path(), 'utf8');

        check(saved.startsWith('# Thinksynth Patch File\n') &&
              /^dsp \S+$/m.test(saved),
              `Save downloads ${download.suggestedFilename()}, ` +
              `${saved.split('\n').length} lines of .patch`);

        /* And what came down is what is on the channel, edit and all: the
           value moved above is in the file, not the one it was loaded
           with. */
        const wrote = await page.evaluate(
            (c) => window.solo.chanParams()?.rows
                .find((r) => r.editable && r.kind === 0)?.id, chan);

        check(wrote === undefined ||
              new RegExp(`^${wrote} `, 'm').test(saved),
              `and it carries ${wrote}, the control that was moved`);

        /* Saved is not edited. Neither shell watches the file afterwards,
           so this is as far as either of them can tell. */
        await page.waitForFunction(
            (c) => document.querySelector(
                `#channels .edited[data-channel="${c}"]`).hidden,
            chan, { timeout: 60000 });

        check(true, 'and the row stops saying it has been edited');

        /* And a patch chosen by hand takes the row with it. The mark says
           what is on the channel now, and what is on it after a choice is a
           file just read -- so the mark goes out whatever the patch before
           it had become. */
        const another = await page.evaluate(({ c, was }) =>
        {
            const row = [...document.querySelectorAll('#channels .channel')]
                .find((r) => r.querySelector(`.edited[data-channel="${c}"]`));
            const all = [...(row?.querySelectorAll('select option') ?? [])]
                .map((o) => o.value)
                .filter((v) => v !== '' && v !== was);

            return all.find((v) => v.endsWith('.patch')) ?? all[0] ?? '';
        }, { c: chan, was: chosen });

        if (another !== '')
        {
            await page.evaluate(() => window.solo.pollChanParams());
            await page.focus('#params input[type="range"]');
            await page.keyboard.press('ArrowRight');
            await page.waitForFunction(
                (c) => !document.querySelector(
                    `#channels .edited[data-channel="${c}"]`).hidden,
                chan, { timeout: 60000 });

            await page.selectOption(
                `#channels .channel:has(.edited[data-channel="${chan}"]) ` +
                'select', another);
            await page.waitForFunction(
                () => /on channel \d+\. Play\.$/.test(
                    document.getElementById('status').textContent),
                null, { timeout: 60000 });
            await page.evaluate(() => window.solo.settled());

            check(await edited() === false,
                  `and choosing ${another} over an edited patch leaves a ` +
                  'row that does not say edited');
        }
    }

    /* ---- the instrument as a graph ---- */

    /* The node editor on the solo page, which has no shared document: the
       files are whatever the page is playing, and an edit rewrites the
       .dsp in the text box and reloads it -- the canvas and the box being
       two views of one text, as they are in the desktop's editor. */
    await page.selectOption('#mode', 'patch');
    await page.evaluate(() =>
    {
        document.getElementById('nodeview').open = true;
    });

    await page.waitForFunction(() => window.solo.node()?.boxes > 0,
                               null, { timeout: 60000 });

    const graph = await page.evaluate(() => window.solo.node());
    const offered = await page.$$eval('#nodefile option',
                                      (os) => os.map((o) => o.value));

    check(graph.boxes > 0 && offered.length > 0,
          `${offered.join(', ')} drew ${graph.boxes} boxes`);

    /* And it opens at 1:1 rather than fitted. A patch is wide -- ts1 is
       1888 pixels of graph -- so fitting it into a page-width box halves
       every label, and half-size labels are a picture of a patch rather
       than a patch to work on. Fit is the button for the other question,
       and after it the whole graph is in the box. */
    const opened = await page.evaluate(() =>
    {
        const s = document.getElementById('nodescroll');

        return { wide: s.scrollWidth > s.clientWidth + 1,
                 box: s.clientHeight };
    });

    await page.click('#nodefit');
    await new Promise((r) => setTimeout(r, 500));

    const whole = await page.evaluate(() =>
    {
        const s = document.getElementById('nodescroll');

        return s.scrollWidth <= s.clientWidth + 1;
    });

    check(opened.wide && opened.box > 200 && whole,
          `it opens at 1:1 in a ${opened.box}-pixel box and scrolls, and ` +
          'Fit brings the whole graph in');

    /* A node with a plain number on it, clicked, and the number typed
       into. The canvas is asked where the box is: the layout is its
       own. */
    const node = await page.evaluate(() =>
    {
        const n = window.solo.node();

        for (let i = 0; i < n.boxes; i++)
        {
            const b = n.box(i);

            if (b.kind === 0 && b.settable)
                return b;
        }

        return null;
    });

    if (node === null)
        check(false, 'nothing in the patch has a value to set');
    else
    {
        await page.locator('#nodescroll').scrollIntoViewIfNeeded();

        const at = await page.$eval('#nodecanvas', (c) =>
        {
            const r = c.getBoundingClientRect();

            return { x: r.x, y: r.y };
        });

        await page.mouse.click(at.x + node.x + 8, at.y + node.y + 8);
        await page.waitForFunction(() => window.solo.node().selected >= 0,
                                   null, { timeout: 15000 });

        const was = await page.inputValue('#dsp');
        const field = await page.$('#nodeparams input');

        if (field === null)
            check(false, `${node.name} has nothing to type into`);
        else
        {
            /* The row carries its own identity; the control inside it
               is just a control. panel.js puts the row's id on the row,
               which for a node's panel is the arg's name. */
            const arg = await field.evaluate(
                (i) => i.closest('.panelrow').dataset.row);

            await field.fill('0.234');
            await field.press('Enter');
            await page.waitForFunction(
                (before) =>
                    document.getElementById('dsp').value !== before,
                was, { timeout: 15000 }).catch(() => {});

            const now = await page.inputValue('#dsp');
            const line = now.split('\n').find(
                (l, i) => l !== was.split('\n')[i]);

            check(now !== was && /0\.234/.test(line ?? ''),
                  `setting ${node.name}.${arg} rewrote the .dsp: ` +
                  `${(line ?? '').trim()}`);
        }
    }

    for (const e of errors)
        check(false, `page error: ${e}`);
}
catch (e)
{
    check(false, `threw: ${e.message.split('\n')[0]}`);
}

await browser.close();
site.closeAllConnections();
site.close();

process.stdout.write(`\n${failures === 0
                          ? 'the solo page\'s keys, knobs, parameters, ' +
                            'composer view, piano roll and instrument ' +
                            'graph still work\n'
                          : `${failures} failed\n`}`);
process.exitCode = failures;
