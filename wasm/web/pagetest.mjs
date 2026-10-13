#!/usr/bin/env node
/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

/* Under scripts/headless.sh unless THINK_TEST_HEADLESS=0: see headless.mjs. */
import './headless.mjs';

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { chromium } from 'playwright';

import { axeFindings } from './a11y.mjs';
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
    const page = await (await browser.newContext()).newPage();

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

            /* An output, which records what is sent to it: the bytes, the
               time they were scheduled for, and when they were handed
               over. Closed until the first send opens it, which a browser
               announces as a state change, as a real one does. */
            plugOut (id, name)
            {
                const port = {
                    id, name, type: 'output', state: 'connected',
                    connection: 'closed',
                    send (bytes, at)
                    {
                        if (port.connection === 'closed')
                        {
                            port.connection = 'open';
                            access.onstatechange?.({ port });
                        }

                        window.midiSent.push({ port: name,
                                               bytes: [...bytes],
                                               at: at ?? performance.now(),
                                               handed: performance.now() });
                    },
                };

                access.outputs.set(id, port);
                access.onstatechange?.({ port });
            },
        };

        window.midiSent = [];
        fake.plug('k1', 'Fake Keys');
        fake.plugOut('o1', 'Fake Synth');
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

    /* The microphone cannot be asked for until there is a node to connect
       it to. */
    check(await page.isDisabled('#mic') &&
          await page.isDisabled('#midi'),
          'the microphone and MIDI wait for a synth');

    /* And Live in is out of the way while nothing in play listens: the
       sequence the page opens on does not, and Voice, whose vocoder reads
       the microphone, does. */
    const liveShown = () => page.evaluate(() =>
        String(document.getElementById('livein').checkVisibility()));

    const liveBefore = await liveShown();

    await page.selectOption('#mode', 'piece');
    await page.selectOption('#piece', 'voice.gen');
    await page.waitForFunction(
        () => document.getElementById('livein').checkVisibility(),
        null, { timeout: 30000 }).catch(() => {});

    const liveVoice = await liveShown();

    await page.selectOption('#piece', 'ebb.gen');
    await page.selectOption('#mode', 'seq');
    await page.waitForFunction(
        () => !document.getElementById('livein').checkVisibility(),
        null, { timeout: 30000 }).catch(() => {});

    check(liveBefore === 'false' && liveVoice === 'true' &&
          await liveShown() === 'false',
          'Live in shows only for what listens: ' +
          `${liveBefore} on the sequence, ${liveVoice} on Voice`);

    /* Start, then a piece, and the knobs it declared. */
    await page.click('#start');
    await page.waitForFunction(
        () => !document.getElementById('loadpiece').disabled,
        null, { timeout: 60000 });
    check(true, 'the synth started');

    /* And after it: the microphone is offerable -- on 127.0.0.1, which is
       a secure context, so micAvailable() is true and the button is live.
       Clicking it is mictest.mjs's, which has a browser launched with a fake
       device to answer with; here the claim is only that the page put the
       two controls in the right state. */
    check(!(await page.isDisabled('#mic')) &&
          !(await page.isDisabled('#midi')),
          'the microphone is offered once there is a synth');

    /* Evolve is the composer's, which is being rebuilt. */
    const axe = await axeFindings(page, ['#evolve']);

    check(axe.length === 0, 'axe finds nothing wrong with the started page' +
          axe.map((f) => `\n      ${f}`).join(''));

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

    check(seqTracks.length === 5,
          `the sequence is five tracks: ${seqTracks.length}`);

    /* A menu on every one of them, which is what the mode is for: its
       piece declares no instruments, so every channel is the page's to
       aim. A shipped piece's tracks have a name there instead. */
    check(await page.$$eval('#tracks .trackpick', (m) => m.length) === 5,
          'and each one has a menu for what plays it');

    /* A kit and two voices, the kit first: what plays each track is the
       sequence's own default and not the desktop's first-run four, and
       the keys go to the last voice rather than to the kick. */
    check(await page.$$eval('#tracks .trackpick',
                            (m) => m.map((s) => s.value).join(' ')) ===
              'kit_kick.dsp kit_snare.dsp kit_hat.dsp ebass.dsp rhodes.dsp' &&
          await page.inputValue('#keychan') === '4',
          'the tracks open on a kit, a bass and a Rhodes, and the keys go ' +
          'to the Rhodes');

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

    check(await settle(0, 1) === 1,
          'the kick track, which ignores the note, opens one row tall');

    await page.selectOption('#tracks .track:nth-child(4) select.trackpick',
                            'kick909.dsp');
    await page.evaluate(() => window.solo.settled());

    check(await settle(3, 1) === 1,
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

        return all.length > 4 &&
               all[3].getBoundingClientRect().height <
               all[4].getBoundingClientRect().height;
    }, null, { timeout: 30000 }).catch(() => {});

    const strips = await heights();

    check(strips[3] < strips[4],
          `and its strip is shorter than a pitched one: ${strips[3]} ` +
          `against ${strips[4]}`);

    await page.selectOption('#tracks .track:nth-child(2) select.trackpick',
                            'tom808.dsp');
    await page.evaluate(() => window.solo.settled());

    check(await settle(1, 6) === 6,
          'the snare track aimed at Tom 808, which is a drum that reads ' +
          'the note, gets a ladder');

    await page.selectOption('#tracks .track:nth-child(4) select.trackpick',
                            'ts1.dsp');
    await page.evaluate(() => window.solo.settled());

    check(await settle(3, 6) === 6,
          'and a synth back on it gives its rows back');

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

    /* And its keys go to channel 1, where Ebb listens, not to the
       sequence's Rhodes on 5, which Ebb has nothing on. */
    check(await page.inputValue('#keychan') === '0',
          'a piece keeps its own keys channel: 1, not the sequence\'s 5');

    await page.selectOption('#piece', 'scratch.gen');
    await page.evaluate(() => window.solo.settled());

    await page.selectOption('#mode', 'seq');
    await page.evaluate(() => window.solo.settled());
    await page.waitForFunction(() => window.solo.tracks().length > 0,
                               null, { timeout: 60000 });

    check(await page.evaluate(() => window.solo.tracks().length) === 5 &&
          await page.$$eval('#tracks .trackpick', (m) => m.length) === 5,
          'and scratch.gen, which is grids too, does not become the ' +
          'sequence');

    check(await page.evaluate(() => window.solo.drawing().seq),
          'and the tracks draw again on the way back');

    /* A key played into a track is on the roll, as played and apart from
       what the track made of it: held while it is down, ended once it is
       let go. The roll learns of it from the worklet's next tape, so it
       is asked until it says. */
    const rollHas = async (note, held) =>
    {
        for (let i = 0; i < 40; i++)
        {
            const played = await page.evaluate(() => window.solo.rollPlayed());

            if (played?.some(([, , n, h]) => n === note && h === held))
                return true;

            await new Promise((r) => setTimeout(r, 100));
        }

        return false;
    };

    await page.keyboard.press('Escape');
    await page.keyboard.down('z');

    const keyNote = (await page.evaluate(() => window.solo.sounding()))[0]
        ?.note;
    const keyHeld = await rollHas(keyNote, true);

    await page.keyboard.up('z');

    check(keyHeld && await rollHas(keyNote, false),
          `a key played into a track is on the roll as played: ${keyNote}`);

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
     * half of it. Its onsets alone, on channel 1: the hat plays between
     * the beats.
     */
    check(await page.evaluate(
              () => document.getElementById('tempo').value === '112' &&
                    !document.getElementById('tempo').disabled),
          'the sequence offers its tempo, at what the piece says: 112');

    /* The median gap between the kick's onsets, which is a beat. Median
       rather than the first gap, so what is wanted is the spacing that
       repeats. */
    const beat = async () =>
    {
        await page.waitForFunction(
            () => window.solo.notes().filter((n) => n.channel === 0)
                      .length >= 6,
            null, { timeout: 30000 });

        const at = await page.evaluate(
            () => window.solo.notes().filter((n) => n.channel === 0)
                      .map((n) => n.at).sort((a, b) => a - b));
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
    check(/tempo\s+56\s*;/.test(
              await page.$eval('#gen', (e) => e.value)),
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

    check(/tempo\s+300\s*;/.test(
              await page.$eval('#gen', (e) => e.value)),
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
       in the source box, which is editable.

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
    await page.waitForSelector('#composerpaint:not([hidden])',
                               { timeout: 60000 });

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

    const stages = await page.$$eval('#composerpaint option:not([value=""])',
                                     (os) => os.map((o) => o.textContent));

    check(stages.length > 0,
          `${COMPOSER_PIECE} offers its controls: ${stages.join(', ')}`);

    /* The Life board's: the euclid ring before it is a control too. With
       the view in sight, which is where a menu under it is chosen from:
       out of sight it draws nothing, and says nothing either. */
    await page.locator('#composerscroll').scrollIntoViewIfNeeded();
    await page.selectOption('#composerpaint', { label: 'life in colony' });
    await page.waitForFunction(
        () => /^Painting /.test(
            document.getElementById('composerstatus').textContent),
        null, { timeout: 60000 });
    check(true, 'life in colony enlarged, from the Paint menu');

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

    /* And the menu does both for a finger, which has no Escape: the
       picture chosen enlarges it, and the menu's first entry, the piece,
       puts it back. */
    await page.selectOption('#composerpaint', { label: 'life in colony' });
    await page.waitForFunction(
        () => /^Painting /.test(
            document.getElementById('composerstatus').textContent),
        null, { timeout: 15000 });
    await page.selectOption('#composerpaint', '');
    await page.waitForFunction(
        () => !/^Painting /.test(
            document.getElementById('composerstatus').textContent),
        null, { timeout: 15000 });
    check(await page.$eval('#composerpaint', (m) => m.value) === '',
          'the Paint menu enlarges a picture and puts it back');

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
            const genWas = await page.$eval('#gen', (e) => e.value);

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
                .then(() => page.$eval('#gen', (e) => e.value),
                      () => genWas);
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

    /* A chain's M and S. A press leaves the canvas as a `mute' or `solo'
       command and the flag changes when it comes back, so the mirror's
       own state is what is read -- polled from here, because
       waitForFunction does not wait on a promise. */
    const mixUntil = async (want, what) =>
    {
        let chains = [];

        for (let i = 0; i < 100; i++)
        {
            chains = await page.evaluate(() => window.solo.mix());

            if (want(chains))
                break;

            await new Promise((r) => setTimeout(r, 100));
        }

        check(want(chains), `${what}: ${JSON.stringify(chains)}`);
    };

    /* The scroller brought into view and measured each time: pressing
       Load scrolls the page to the button, and the canvas moves with
       whatever else a load rewrites. */
    const scroller = async () =>
    {
        await page.locator('#composerscroll').scrollIntoViewIfNeeded();

        return page.$eval('#composerscroll', (d) =>
        {
            const r = d.getBoundingClientRect();

            return { x: r.x, y: r.y };
        });
    };

    const press = async (chain, which) =>
    {
        const at = await page.evaluate(
            ([c, w]) => window.solo.chipOf(c, w), [chain, which]);
        const where = await scroller();

        if (at.x < 0)
            check(false, `chain ${chain} has no ${which ? 'S' : 'M'}`);
        else
            await page.mouse.click(where.x + at.x, where.y + at.y);
    };

    await press(0, 0);
    await mixUntil((c) => c.length > 1 && c[0].muted && !c[0].audible &&
                          c[1].audible,
                   "a chain's M mutes that chain and no other");

    await press(0, 0);
    await mixUntil((c) => c.length > 1 && !c[0].muted && c[0].audible,
                   '...and a second press unmutes it');

    await press(1, 1);
    await mixUntil((c) => c.length > 1 && c[1].soloed && c[1].audible &&
                          c.every((x, i) => i === 1 || !x.audible),
                   "a chain's S silences every other chain");

    await press(1, 1);
    await mixUntil((c) => c.length > 1 && c.every((x) => x.audible),
                   '...and a second press brings them back');

    /* Two presses faster than a command comes back are two toggles, not
       two of the same set: the button answers with what it has asked
       for, and a double-click reaches the canvas as two presses. */
    {
        const at = await page.evaluate(() => window.solo.chipOf(0, 0));
        const where = await scroller();

        await page.mouse.dblclick(where.x + at.x, where.y + at.y);
        await new Promise((r) => setTimeout(r, 1000));
        await mixUntil((c) => c.length > 1 && !c[0].muted,
                       'a double-click on M is two toggles, and leaves the ' +
                       'chain as it was');
    }

    /* Loading the same piece again keeps what was muted: a load clears
       the scheduler's flags, and the page puts them back by name. */
    await press(1, 0);
    await mixUntil((c) => c.length > 1 && c[1].muted, 'muted before a load');
    const loadsBefore = await page.evaluate(() => window.solo.pieces());

    await page.click('#loadpiece');
    await page.waitForFunction(
        (n) => window.solo.pieces() > n, loadsBefore, { timeout: 30000 });
    await mixUntil((c) => c.length > 1 && c[1].muted && !c[0].muted,
                   'a load of the same piece keeps the chain muted');
    await press(1, 0);
    await mixUntil((c) => c.length > 1 && !c[1].muted, 'and unmuted again');

    /* Polled, like the mix: `what' is asked until `want' says yes. */
    const until = async (what, want) =>
    {
        let got;

        for (let i = 0; i < 100; i++)
        {
            got = await what();

            if (want(got))
                return got;

            await new Promise((r) => setTimeout(r, 100));
        }

        return got;
    };

    /* A chain's triangle collapses it, and Expand chains opens every
       one again. */
    {
        const layout = () =>
            page.evaluate(() => window.solo.canvasLayout());
        const before = await layout();
        const where = await scroller();

        await page.mouse.click(where.x + before.chains[1].x,
                               where.y + before.chains[1].y);

        const after = await until(layout, (l) => l.chains[1].collapsed);

        check(after.chains[1].collapsed && !after.chains[0].collapsed &&
              after.chains[2].y < before.chains[2].y,
              'a chain\'s triangle collapses it and the rows below move up');

        await page.click('#expandall');

        const open = await until(layout,
                                 (l) => l.chains.every((c) => !c.collapsed));

        check(open.chains.every((c) => !c.collapsed),
              'Expand chains opens every chain again');

        /* Wide enough for more than one, the chains go in columns, and
           back to one when the view narrows again. */
        await page.addStyleTag({ content:
            '#composerscroll { width: 1500px !important; ' +
            'max-width: none !important; }' });

        const wide = await until(layout, (l) => l.columns > 1);
        const room = await page.$eval('#composerscroll',
                                      (d) => d.clientWidth);

        /* Colony's chains are about the same height, so the columns hold
           about as many each, and the drawing fits the view. */
        const tops = new Set(wide.chains.map((c) => c.x));
        const perColumn = [...tops].map(
            (x) => wide.chains.filter((c) => c.x === x).length);

        check(wide.columns > 1 && wide.width <= room &&
              Math.max(...perColumn) - Math.min(...perColumn) <= 1,
              `a view wide enough lays the chains out in ${wide.columns} ` +
              `columns of ${perColumn.join('/')}, ${wide.width} wide in ` +
              `${room}`);

        await page.evaluate(() => document.querySelectorAll('style')
            .forEach((s) => s.textContent.includes('1500px') && s.remove()));

        const narrow = await until(layout, (l) => l.columns === 1);

        check(narrow.columns === 1, '...and one again when it narrows');
    }

    /* A drag on the canvas, a frame at a time: the view sends one motion
       per animation frame, and the drag is what the motions say. */
    const drag = async (x0, y0, x1, y1) =>
    {
        const where = await scroller();

        await page.mouse.move(where.x + x0, where.y + y0);
        await page.mouse.down();

        for (let i = 1; i <= 8; i++)
        {
            await page.mouse.move(where.x + x0 + (x1 - x0) * i / 8,
                                  where.y + y0 + (y1 - y0) * i / 8);
            await new Promise((r) => setTimeout(r, 40));
        }

        await page.mouse.up();
    };

    /* A knob node's track, dragged to its end: a knob command, which the
       worklet's knob then reads back as the top of its range. */
    const knob = await page.evaluate(() => window.solo.knobAt('density'));

    if (knob[0] < 0)
        check(false, 'colony has no density knob on the canvas');
    else
    {
        await drag(knob[0] + 2, knob[2], knob[1] + 20, knob[2]);

        const values = await until(
            () => page.evaluate(() => window.solo.knobValues()),
            (v) => v.length > 0 && v[0].value === 5);

        check(values[0]?.value === 5,
              'a knob dragged on the canvas moves the piece\'s knob: ' +
              JSON.stringify(values));

        const shown = await page.$eval(
            '#knobs .panelrow input[type="number"]', (e) => e.value);

        check(Number(shown) === 5,
              `...and the knob strip shows where it went: ${shown}`);

        /* And let go of, it is in the piece: Save and Load keep it. */
        const knobText = await until(
            () => page.evaluate(() => window.solo.genText()),
            (t) => /@density\s*=\s*5\s*;/.test(t));

        check(/@density\s*=\s*5\s*;/.test(knobText),
              '...and the piece\'s text says it');
    }

    /* A wire from the knob's port onto the first stage of the second
       chain asks which param; the answer is a binding written into the
       text. */
    const bindings = (t) => (t.match(/@density\b/g) ?? []).length;
    const textBefore = await page.evaluate(() => window.solo.genText());
    const target = await page.evaluate(() => window.solo.stageAt(1, 0));

    if (knob[0] < 0 || target[0] < 0)
        check(false, 'nothing to pull a wire between');
    else
    {
        await drag(knob[3], knob[4], target[0] + target[2] / 2,
                   target[1] + target[3] / 2);
        await page.waitForSelector('#composerparams:not([hidden]) button',
                                   { timeout: 15000 });

        const row = await page.getAttribute(
            '#composerparams button', 'data-row');

        await page.click('#composerparams button');

        const text = await until(
            () => page.evaluate(() => window.solo.genText()),
            (t) => bindings(t) > bindings(textBefore));

        check(bindings(text) === bindings(textBefore) + 1 &&
              new RegExp(`${row}\\s*=\\s*@density`).test(text),
              `a wire from a knob, dropped on a stage, binds its ${row}`);
    }

    /* A stage box carried one slot right swaps it with its neighbor in
       the text, and the piece is loaded again. */
    const floorOrder = (t) =>
    {
        const from = t.indexOf('chain floor');
        const next = t.indexOf('\nchain ', from + 1);
        const chain = t.slice(from, next < 0 ? undefined : next);
        const eno = chain.indexOf('gen::eno_line');
        const harm = chain.indexOf('xform::harmonize');

        return from < 0 || eno < 0 || harm < 0 ? 'missing'
            : eno < harm ? 'eno,harm' : 'harm,eno';
    };

    const first = await page.evaluate(() => window.solo.stageAt(1, 0));

    if (first[0] < 0)
        check(false, 'the second chain has no first stage to carry');
    else
    {
        const orderBefore = floorOrder(
            await page.evaluate(() => window.solo.genText()));

        await drag(first[0] + first[2] / 2, first[1] + 10,
                   first[0] + first[2] * 1.5 + 22, first[1] + 10);

        const text = await until(
            () => page.evaluate(() => window.solo.genText()),
            (t) => floorOrder(t) !== orderBefore);

        await page.evaluate(() => window.solo.settled());

        check(orderBefore === 'eno,harm' && floorOrder(text) === 'harm,eno',
              'a stage box carried one slot right moves its stage in the ' +
              'text');
    }

    /* What the lights are drawn from: played, some chain's first stage
       has emitted, and been heard, within the last second or so of the
       mirror's transport. */
    await page.click('#play');

    const lit = await until(
        () => page.evaluate(() => window.solo.activity()),
        (a) => a.running && a.now > 2 &&
               a.chains.some((c) => c.out > a.now - 1.5 &&
                                    c.heard > a.now - 1.5));

    check(lit.running && lit.chains.some((c) => c.out > lit.now - 1.5),
          `a playing chain's stage lights follow what it emits: ` +
          JSON.stringify(lit));

    await page.click('#stop');
    await new Promise((r) => setTimeout(r, 500));

    /* And the roll by chain, which stripes each chain's name on the
       canvas in the hue its notes take. Stopped, so nothing else moves
       the picture. */
    const plain = await ink();

    await page.check('#rollbychain');

    const striped = await until(ink, (v) => v > plain);

    check(striped > plain,
          'coloring the roll by chain stripes the chains on the canvas');

    await page.uncheck('#rollbychain');

    /* The arrangement lane, on a piece that has one: a cell pressed steps
       that chain's level in that section, the running piece hears it, and
       the text says it. Then colony again, for what follows. */
    {
        const pieceLoads = await page.evaluate(() => window.solo.pieces());

        await page.selectOption('#piece', 'mirrorball.gen');
        await page.waitForFunction((n) => window.solo.pieces() > n,
                                   pieceLoads, { timeout: 60000 });
        await page.evaluate(() => window.solo.settled());

        const lane = () => page.evaluate(() => window.solo.lane());
        const before = await lane();
        /* The whole statement, which runs over more than one line. */
        const introKick = () =>
            page.evaluate(() =>
            {
                const t = window.solo.genText();
                const a = t.indexOf('section intro');

                return a < 0 ? '' : t.slice(a, t.indexOf('};', a) + 2);
            });

        const cellPress = async () =>
        {
            const at = (await lane()).cells[0][0];
            const where = await scroller();

            await page.mouse.click(where.x + at.x, where.y + at.y);
        };

        if (before.levels.length === 0 || before.levels[0][0] !== 0)
            check(false, 'mirrorball\'s intro should start with the kick ' +
                         `silent: ${JSON.stringify(before.levels[0])}`);
        else
        {
            await cellPress();

            const half = await until(lane, (l) => l.levels[0][0] === 0.5);
            const text = await until(introKick,
                                     (t) => /kick = 0\.5/.test(t));

            check(half.levels[0][0] === 0.5 && /kick = 0\.5/.test(text),
                  `an arrangement cell steps the kick's level in the ` +
                  `intro, and the text follows: ${text}`);

            /* Round again, two presses faster than a command comes back:
               the cell answers with what it asked for, so the second
               reads the first -- as written, then silent -- and the text
               ends where the second left it. */
            await cellPress();
            await cellPress();

            const back = await until(lane, (l) => l.levels[0][0] === 0);
            const again = await until(introKick,
                                      (t) => /kick = 0;/.test(t));

            check(back.levels[0][0] === 0 && /kick = 0;/.test(again),
                  'two quick presses step it twice, through as written ' +
                  `to silent: ${again}`);
        }

        /* F on a chain that has been playing: the hats, which the intro
           keeps. What they played comes back as a new chain with a grid,
           the text says so, and the hats themselves come back muted. */
        await page.click('#play');
        await new Promise((r) => setTimeout(r, 5000));

        const freezeLoads = await page.evaluate(() => window.solo.pieces());
        const f = await page.evaluate(() => window.solo.chipOf(2, 2));

        /* Below what the scroller shows at this page's height: scrolled
           to, and pressed where it now is. */
        const top = await page.$eval('#composerscroll', (d, y) =>
        {
            d.scrollTop = Math.max(0, y - d.clientHeight / 2);

            return d.scrollTop;
        }, f.y);
        const at = await scroller();

        await page.mouse.click(at.x + f.x, at.y + f.y - top);
        await page.waitForFunction((n) => window.solo.pieces() > n,
                                   freezeLoads, { timeout: 30000 })
            .catch(async () => check(false, 'F reloaded nothing: ' +
                await page.textContent('#status')));
        await page.$eval('#composerscroll', (d) => { d.scrollTop = 0; });

        const frozenText = await page.evaluate(() => window.solo.genText());
        const mixAfter = await until(
            () => page.evaluate(() => window.solo.mix()),
            (c) => c.length > 2 && c[2].muted);

        /* And it plays where the hats play, as loud: the break has them
           at 0.6. */
        const brk = frozenText.slice(frozenText.indexOf('section break'));
        const breakStmt = brk.slice(0, brk.indexOf('};'));

        check(/chain hats_frozen \{[^}]*gen::grid/.test(frozenText) &&
              /hats_frozen = 0\.6;/.test(breakStmt) &&
              mixAfter[2].muted && mixAfter.length === before.levels[0]
                  .length + 1,
              'F freezes what the hats played into hats_frozen, a grid at ' +
              'the hats\' levels in the arrangement, and mutes the hats');

        await page.click('#stop');

        /* The piece's effect, chosen from the menu: written into the text
           in place of the one it had, and loaded. */
        {
            const shown = await page.$eval('#pieceeffect', (m) => m.value);
            const loads = await page.evaluate(() => window.solo.pieces());

            await page.$eval('#pieceeffect', (m) =>
            {
                m.value = 'fx/space.dsp';
                m.dispatchEvent(new Event('change'));
            });
            await page.waitForFunction((n) => window.solo.pieces() > n,
                                       loads, { timeout: 30000 })
                .catch(() => {});

            const text = await page.evaluate(() => window.solo.genText());
            const now = await page.$eval('#pieceeffect', (m) => m.value);

            check(shown === 'fx/glue.dsp' && now === 'fx/space.dsp' &&
                  /^effect "fx\/space\.dsp";/m.test(text) &&
                  !/fx\/glue\.dsp/.test(text),
                  'the piece\'s effect menu shows fx/glue.dsp and puts ' +
                  `fx/space.dsp in its place: ${shown} -> ${now}`);
        }

        /* Undo and redo: the effect taken back and put back, stopped, as
           loads; then taken back while it plays, as an edit, which does
           not start the piece again from the top. */
        {
            /* What the menu shows is what the piece playing says. */
            const has = (fx) => page.waitForFunction(
                (f) => document.getElementById('pieceeffect').value === f &&
                       window.solo.genText().includes(`"${f}"`), fx,
                { timeout: 30000 }).then(() => true, () => false);
            const clock = () => page.$eval('#clock', (c) =>
            {
                const [m, s] = c.textContent.split(' ')[0].split(':');

                return Number(m) * 60 + Number(s);
            });

            await page.click('#undo');
            const undone = await has('fx/glue.dsp');

            await page.click('#redo');
            const redone = await has('fx/space.dsp');

            await page.click('#play');
            await page.waitForTimeout(3000);

            const before = await clock();

            await page.click('#undo');
            const live = await has('fx/glue.dsp');

            /* Long enough for a load's rewind to have shown on the clock. */
            await page.waitForTimeout(1000);
            const after = await clock();

            await page.click('#stop');
            await page.evaluate(() => window.solo.settled());

            check(undone && redone && live && after >= before && before > 1,
                  'Undo takes the effect back and Redo puts it back; Undo ' +
                  `while playing goes on from where it was: ${before} s, ` +
                  `then ${after} s`);
        }

        /* Save downloads the box's text; Open loads a file as the piece,
           named in the menu until another is chosen. */
        {
            const [download] = await Promise.all([
                page.waitForEvent('download'), page.click('#savepiece')]);
            const saved = await new Promise((resolve) =>
            {
                const parts = [];

                download.createReadStream().then((s) =>
                {
                    s.on('data', (d) => parts.push(d));
                    s.on('end', () => resolve(Buffer.concat(parts)
                                                    .toString()));
                });
            });
            const text = await page.evaluate(() => window.solo.genText());
            const was = await page.$eval('#piece', (m) => m.value);
            const mine = 'name "Mine";\nchain one {\n    stage src ' +
                         'gen::euclid { steps = 4; fills = 2; notes = "C4"; ' +
                         'period = 0.25 s; hold = 0.1 s; };\n    sink { ' +
                         'channel = 1; };\n};\n';

            await page.setInputFiles('#openfile', {
                name: 'mine.gen', mimeType: 'text/plain',
                buffer: Buffer.from(mine) });
            await page.evaluate(() => window.solo.settled());

            const opened = await page.evaluate(() => window.solo.genText());
            const shown = await page.$eval('#piece', (m) =>
                m.selectedOptions[0]?.textContent);
            const fresh = await page.$eval('#undo', (b) => b.disabled);

            check(saved === text && opened === mine && shown === 'mine.gen' &&
                  fresh,
                  'Save downloads the piece\'s text and Open loads a file ' +
                  `as the piece, with nothing to undo: menu says ${shown}`);

            await page.selectOption('#piece', was);
            await page.evaluate(() => window.solo.settled());
        }

        /* The euclid ring, enlarged and right-clicked in its middle: a hit
           fewer, which the end of the gesture writes into the piece. */
        await page.locator('#composerscroll').scrollIntoViewIfNeeded();
        await page.selectOption('#composerpaint',
                                { label: 'euclid in kick' });
        await page.waitForFunction(
            () => /^Painting /.test(
                document.getElementById('composerstatus').textContent),
            null, { timeout: 15000 });

        const ringAt = await scroller();
        const ringBox = await page.$eval('#composerscroll', (d) =>
            ({ w: d.clientWidth, h: d.clientHeight }));

        await page.mouse.click(ringAt.x + ringBox.w / 2,
                               ringAt.y + ringBox.h / 2, { button: 'right' });

        const kickOf = (t) =>
        {
            const a = t.indexOf('chain kick {');

            return a < 0 ? '' : t.slice(a, t.indexOf('\n};', a));
        };
        const ringText = await until(
            () => page.evaluate(() => window.solo.genText()),
            (t) => /fills\s*=\s*3\s*;/.test(kickOf(t)));

        check(/fills\s*=\s*3\s*;/.test(kickOf(ringText)),
              'a right-click in the euclid ring\'s middle takes a hit ' +
              'away, and the piece\'s text says so');

        /* A press on the ring let go of without a turn changes nothing,
           and writes nothing. */
        {
            const before = await page.evaluate(() => window.solo.genText());
            const r = Math.min(ringBox.w, ringBox.h) / 2 - 8;

            await page.mouse.click(ringAt.x + ringBox.w / 2,
                                   ringAt.y + ringBox.h / 2 - r);
            await new Promise((res) => setTimeout(res, 1000));

            check(await page.evaluate(() => window.solo.genText()) === before,
                  'a press on the ring with no turn leaves the text alone');
        }

        await page.keyboard.press('Escape');

        /* tamb's accent is "..x.": clearing its one mark leaves no
           pattern, and the text says so rather than keeping the old one. */
        await page.locator('#composerscroll').scrollIntoViewIfNeeded();
        await page.selectOption('#composerpaint',
                                { label: 'accent in tamb' });
        await page.waitForFunction(
            () => /^Painting /.test(
                document.getElementById('composerstatus').textContent),
            null, { timeout: 15000 });

        {
            const at = await scroller();
            const box = await page.$eval('#composerscroll', (d) =>
                ({ w: d.clientWidth, h: d.clientHeight }));

            /* The third of four steps, halfway down. */
            await page.mouse.click(at.x + box.w * 0.625, at.y + box.h * 0.45);

            const tambOf = (t) =>
            {
                const a = t.indexOf('chain tamb {');

                return a < 0 ? '' : t.slice(a, t.indexOf('\n};', a));
            };
            const cleared = await until(
                () => page.evaluate(() => window.solo.genText()),
                (t) => /pattern\s*=\s*"";/.test(tambOf(t)));

            check(/pattern\s*=\s*"";/.test(tambOf(cleared)),
                  'clearing an accent\'s last mark writes an empty pattern');
        }

        await page.keyboard.press('Escape');

        /* The `full' block, pressed while playing: the transport is there
           -- intro, groove and break later -- and plays on from it. */
        await page.click('#play');
        await new Promise((r) => setTimeout(r, 1000));

        const heads = (await lane()).heads;
        const headAt = await scroller();

        await page.mouse.click(headAt.x + heads[3].x, headAt.y + heads[3].y);

        /* The worklet's transport, which is the one heard, and the
           mirror's, which draws: both there. */
        const heard = () => page.evaluate(() => window.solo.transport());
        const jumped = await until(heard, (t) => t !== null && t.now > 50);
        const drawn = await until(lane, (l) => l.now > 50);

        await new Promise((r) => setTimeout(r, 1000));

        const later = await heard();

        check(jumped.now > 50 && drawn.now > 50 && later.now > jumped.now,
              `a section's block moves the transport to it and plays on: ` +
              `${jumped.now.toFixed(1)} s, then ${later.now.toFixed(1)} s`);

        await page.click('#stop');

        const colonyLoads = await page.evaluate(() => window.solo.pieces());

        await page.selectOption('#piece', COMPOSER_PIECE);
        await page.waitForFunction((n) => window.solo.pieces() > n,
                                   colonyLoads, { timeout: 60000 });
        await page.evaluate(() => window.solo.settled());
    }

    /* A probe: the arrow out of colony's first stage, pressed after it
       has played, hangs a small roll of what that stage let out under it --
       drawn by the mirror, which is where the notes are -- and Escape
       takes it away. */
    {
        /* Played, and stopped: what the stage let out is kept, and with
           the transport still nothing else moves the picture, so what the
           press changes is the probe. */
        await page.click('#play');
        await new Promise((r) => setTimeout(r, 3000));
        await page.click('#stop');
        await new Promise((r) => setTimeout(r, 800));

        const arrow = { chain: 0, stage: 0 };
        const at = (await page.evaluate(
            (a) => window.solo.canvasLayout(a), arrow)).arrow;
        const where = await scroller();
        const plain = await ink();

        await page.mouse.click(where.x + at.x, where.y + at.y);

        const probed = await until(
            () => page.evaluate(() => window.solo.canvasLayout()),
            (l) => l.probe.chain === 0 && l.probe.stage === 0);
        const drawn = await until(ink, (v) => v !== plain);

        check(probed.probe.chain === 0 && probed.probe.stage === 0 &&
              drawn !== plain,
              'pressing an arrow puts a probe on it, drawn under it');

        await page.focus('#composer');
        await page.keyboard.press('Escape');

        const gone = await until(
            () => page.evaluate(() => window.solo.canvasLayout()),
            (l) => l.probe.chain === -1);

        check(gone.probe.chain === -1, '...and Escape takes it away');
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

        const was = await page.$eval('#dsp', (e) => e.value);
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

            const now = await page.$eval('#dsp', (e) => e.value);
            const line = now.split('\n').find(
                (l, i) => l !== was.split('\n')[i]);

            check(now !== was && /0\.234/.test(line ?? ''),
                  `setting ${node.name}.${arg} rewrote the .dsp: ` +
                  `${(line ?? '').trim()}`);
        }
    }

    /* MIDI out (midiout.js): a piece whose instrument names a port. Before
     * access it plays its dsp, and its row says why; with MIDI out on it
     * plays on the fake output, every message scheduled for a time in
     * performance.now()'s clock and handed over before it, notes a beat
     * apart; a stop ends what it holds; and choosing this synth puts it
     * back on its dsp. */
    {
        const MIDI_PIECE =
            'name "device";\ntempo 60;\n' +
            'instrument ext { midi "Fake"; midichannel = 3;' +
            ' dsp "organ0.dsp"; };\n' +
            'chain line { stage g gen::grid { notes = "C4"; steps = 1;' +
            ' rows = 1; cells = "x"; period = 1 beats; hold = 0.5 beats; };\n' +
            '    sink { instrument = ext; }; };\n';

        await page.selectOption('#mode', 'piece');
        await page.evaluate(() => window.solo.settled());
        await page.evaluate((text) =>
        {
            document.getElementById('gen').value = text;
        }, MIDI_PIECE);
        await page.click('#loadpiece');
        await page.evaluate(() => window.solo.settled());

        const rowState = () => page.evaluate(
            () => document.querySelector('.midistate')?.textContent ?? null);

        check((await rowState())?.includes('no MIDI access'),
              `a MIDI instrument's row says why it plays its dsp: ` +
              `${await rowState()}`);

        await page.click('#midiout');
        await page.waitForFunction(
            () => document.querySelector('.midistate')?.textContent
                      .startsWith('on '), null, { timeout: 10000 })
            .catch(() => {});

        check((await rowState()) === 'on Fake Synth, channel 3',
              `with MIDI out on it plays on the device: ${await rowState()}`);
        check(await page.$eval('#midioutstatus', (e) => e.textContent) ===
              'Fake Synth', 'and the status names the output');

        /* Timed with some delay, as anyone sending to a device sets it.
           The worklet stamps a message with the time its frame sounds and
           posts it as it composes; at a delay of 0 one that crosses to the
           page a few milliseconds late is past due when it lands and goes
           out stamped `now'. Headless audio has next to no output latency
           to absorb that, so on a loaded runner the times measured below
           would be the page's arrival times rather than the piece's. */
        await page.fill('#midioutdelay', '100');
        await page.dispatchEvent('#midioutdelay', 'change');
        await page.evaluate(() => { window.midiSent.length = 0; });
        await page.click('#play');
        await page.waitForTimeout(2600);

        const sent = await page.evaluate(() => window.midiSent);
        const ons = sent.filter((m) => m.bytes[0] === 0x92 &&
                                       m.bytes[1] === 60);

        check(ons.length >= 2,
              `notes reach the output (${ons.length} note-ons, ` +
              `${sent.length} messages)`);

        /* The first note too: a port opening on its first send is a state
           change, and taking it for a new list of outputs flushed what was
           held and queued -- the note cut to a millisecond. */
        const firstOff = sent.find((m) => m.bytes[0] === 0x82 &&
                                          m.bytes[1] === 60);

        check(ons.length > 0 && firstOff !== undefined &&
              Math.abs(firstOff.at - ons[0].at - 500) < 20,
              `the first note lasts its half beat: ` +
              `${firstOff && ons[0] ? (firstOff.at - ons[0].at).toFixed(1)
                                    : '?'} ms`);

        if (ons.length >= 2)
        {
            const gap = ons[1].at - ons[0].at;

            check(Math.abs(gap - 1000) < 25,
                  `scheduled a beat apart at 60 bpm: ${gap.toFixed(1)} ms`);
        }

        await page.click('#stop');
        await page.waitForTimeout(300);

        const after = await page.evaluate(() => window.midiSent);
        const count = (s, b) => after.filter((m) => m.bytes[0] === b &&
                                                    m.bytes[1] === 60).length;

        check(count(after, 0x92) === count(after, 0x82),
              `a stop leaves nothing held: ${count(after, 0x92)} on, ` +
              `${count(after, 0x82)} off`);

        /* With a delay, a message waits in the page until shortly before
           it is due: a stop then takes back what has not been handed over
           -- the note's off, and the next note -- and ends the note that
           was. */
        await page.fill('#midioutdelay', '200');
        await page.dispatchEvent('#midioutdelay', 'change');
        await page.evaluate(() => { window.midiSent.length = 0; });
        await page.click('#play');
        await page.waitForFunction(
            () => window.midiSent.some((m) => m.bytes[0] === 0x92),
            null, { timeout: 5000 }).catch(() => {});
        await page.click('#stop');
        await page.waitForTimeout(1500);

        const delayed = await page.evaluate(() => window.midiSent);
        const dOn = delayed.filter((m) => m.bytes[0] === 0x92);
        const dOff = delayed.filter((m) => m.bytes[0] === 0x82);
        const lead = dOn.length > 0 ? dOn[0].at - dOn[0].handed : NaN;

        check(dOn.length === 1 && dOff.length === 1 &&
              dOff[0].at >= dOn[0].at,
              `a stop takes back what is queued and ends what was handed ` +
              `over: ${dOn.length} on, ${dOff.length} off`);
        check(lead > 1 && lead <= 31,
              `a delayed note waits in the page until shortly before its ` +
              `time: handed over ${lead.toFixed(1)} ms ahead`);

        await page.fill('#midioutdelay', '100');
        await page.dispatchEvent('#midioutdelay', 'change');

        await page.selectOption('select.midiroute', '@synth');
        await page.waitForFunction(
            () => document.querySelector('.midistate')?.textContent ===
                      'set to play on this synth', null, { timeout: 10000 })
            .catch(() => {});

        check((await rowState()) === 'set to play on this synth',
              `choosing this synth puts it on its dsp: ${await rowState()}`);

        /* MIDI clock to the output: Start, a tick every 24th of a beat
           -- 41.7 ms at 60 bpm -- and Stop. */
        /* A click on the element rather than at a point: what is under
           test is the check, and the row it sits in can move under a
           pointer while a loaded machine catches up. */
        await page.$eval('input.midiclock', (e) => e.click());
        check(await page.$eval('input.midiclock', (e) => e.checked),
              'a check per output turns MIDI clock on for it');
        await page.evaluate(() => { window.midiSent.length = 0; });
        await page.click('#rewind');
        await page.click('#play');
        await page.waitForTimeout(1100);
        await page.click('#stop');
        await page.waitForTimeout(300);

        const clocked = await page.evaluate(
            () => window.midiSent.filter((m) => m.bytes[0] >= 0xf0));
        const ticks = clocked.filter((m) => m.bytes[0] === 0xf8);
        const gaps = ticks.slice(1).map((m, i) => m.at - ticks[i].at)
                          .sort((a, b) => a - b);
        const gap = gaps[gaps.length >> 1] ?? NaN;

        check(clocked[0]?.bytes[0] === 0xfa && clocked.at(-1)?.bytes[0] ===
              0xfc && ticks.length >= 20 && Math.abs(gap - 1000 / 24) < 2,
              `MIDI clock goes out: Start, ${ticks.length} ticks ` +
              `${gap.toFixed(1)} ms apart, Stop`);

        await page.fill('#midioutdelay', '0');
        await page.dispatchEvent('#midioutdelay', 'change');
        await page.$eval('input.midiclock', (e) => e.click());
        await page.evaluate(() =>
        {
            localStorage.removeItem('thinksynth:midiroutes');
            localStorage.removeItem('thinksynth:midiclock');
        });
        await page.click('#midiout');
    }

    /* Export MIDI: the piece being played, composed offline by the mirror
     * and downloaded. One whose arrangement ends is exported to its end,
     * and the file has a track for its chain, named for it. */
    {
        await page.selectOption('#mode', 'piece');
        await page.evaluate(() => window.solo.settled());
        await page.evaluate((text) =>
        {
            document.getElementById('gen').value = text;
        }, 'name "exported";\ntempo 120;\nsection a 2 s { };\nsection end;\n' +
           'instrument org { dsp "organ0.dsp"; };\n' +
           'chain melody { stage g gen::grid { notes = "C4"; steps = 2;\n' +
           '    rows = 1; cells = "x."; period = 0.5 s; hold = 0.25 s; };\n' +
           '    sink { instrument = org; }; };\n');
        await page.click('#loadpiece');
        await page.evaluate(() => window.solo.settled());

        const [download] = await Promise.all([
            page.waitForEvent('download', { timeout: 15000 }).catch(() => null),
            page.click('#exportmidi'),
        ]);

        let bytes = null;

        if (download !== null)
        {
            const stream = await download.createReadStream();
            const parts = [];

            for await (const part of stream)
                parts.push(part);

            bytes = Buffer.concat(parts);
        }

        const status = await page.$eval('#exportstatus', (e) => e.textContent);

        check(bytes !== null && bytes.subarray(0, 4).toString() === 'MThd' &&
              bytes[9] === 1 && bytes[11] === 2 &&
              bytes.includes(Buffer.from('melody')) &&
              /* Track 0 named for the piece: FF 03, its length, its name. */
              bytes.includes(Buffer.concat([Buffer.from([0xff, 0x03, 8]),
                                            Buffer.from('exported')])) &&
              download.suggestedFilename() === 'exported.mid',
              `Export MIDI downloads the piece as a .mid: ` +
              `${download?.suggestedFilename()} ${bytes?.length ?? 0} bytes`);
        check(status.startsWith('2 s,'),
              `an arrangement that ends is exported to its end: ${status}`);
    }

    /* A piece that pins no seed, on a page that has loaded nothing else:
     * the worklet that plays it and the mirror that draws it compose the
     * same piece -- the tapes agree, event for event. Each used to draw a
     * seed of its own, which only a fresh page shows: once any load has
     * handed both a seed, they keep it. */
    {
        const fresh = await browser.newPage();

        fresh.on('pageerror', (e) => errors.push(e.message));
        await fresh.goto(url);
        await fresh.click('#start');
        await fresh.waitForFunction(() => window.solo?.settled, null,
                                    { timeout: 20000 });
        await fresh.selectOption('#mode', 'piece');
        await fresh.evaluate(() => window.solo.settled());
        await fresh.evaluate((text) =>
        {
            document.getElementById('gen').value = text;
        }, 'name "unseeded";\ntempo 120;\n' +
           'instrument org { dsp "organ0.dsp"; };\n' +
           'chain a { stage s gen::eno_line { period = 0.1 s; ' +
           'hold = 0.05 s; }; sink { instrument = org; }; };\n');
        await fresh.click('#loadpiece');
        await fresh.evaluate(() => window.solo.settled());

        const before = await fresh.evaluate(() => window.solo.tapeDiff());

        await fresh.click('#rewind');
        await fresh.click('#play');
        await fresh.waitForTimeout(2500);
        await fresh.click('#stop');
        await fresh.waitForTimeout(500);

        const after = await fresh.evaluate(() => window.solo.tapeDiff());
        const compared = after.compared - before.compared;
        const differ = after.disagreements - before.disagreements;

        check(compared > 10 && differ === 0,
              `an unseeded piece composes the same in the worklet and the ` +
              `mirror: ${compared} events compared, ${differ} differ`);

        await fresh.close();
    }

    /* Sample packs, served from a route rather than the build, which has
     * none: a pack of one tiny FLAC, enough for the page to offer it when
     * the sampled kit is in play, download it into its cache, remember it
     * on the next page, and take it back out. */
    {
        /* No service worker: a request it makes is not one a route sees,
           and once it controls a page the index would come from the
           build's empty one instead of the routes below. */
        const ctx = await browser.newContext({ serviceWorkers: 'block' });
        /* Five milliseconds of a sine, as FLAC: what the site serves, and
           what the page has to decode before the synth can play it. */
        const flac = Buffer.from(
            'ZkxhQ4AAACIA3QDdAAEJAAEJCsRA8AAAAN0O71C8ehPnWH8ejTPvPlJ+//hp' +
            'CADcB04AAAEAAf8C/QP4BO4F4AbM57dmX5aUUMFAoI94u5hxcADHI4HI4G41' +
            'GYxFwrFIlER8dGpiWE5E9bsVihCj07unn9R1FdZWt1fSOSymWS+YzKaTabze' +
            'bzebzaazOYy+WyqTySQ3txaV9XTUHvtw15sGVWwauiAlKi4zODwQCMTisXDE' +
            'ZjUbDccjkcjkcjgbjUaDIYC0UiYRH52bGRYUEb5y0WqEaLRt5ef1FT1lhaXN' +
            '9IZLKZXLphMppNZtN5vN5tN5rNJlMJdKpPJZDe3VrYVlNRfe/LZmxY1C5o6f' +
            'khQWmRsdiASCcVC0YDIaDYbjgcDkcjkcjcbjQZi8WioTiMClPw==', 'base64');

        /* What the site offers, which a later deploy can change. */
        let version = '1';
        const manifest = () => ({
            id: 'drums', title: 'Drum kit', license: 'CC0-1.0',
            credit: 'a test', bytes: flac.length, version,
            files: [{ name: 'drums/k_36_1.wav', url: 'drums/k_36_1.flac',
                      bytes: flac.length }],
        });

        await ctx.route('**/packs/index.json', (r) => r.fulfill({
            json: [{ id: 'drums', title: 'Drum kit', license: 'CC0-1.0',
                     credit: 'a test', bytes: flac.length, files: 1,
                     version }] }));
        await ctx.route('**/packs/drums/pack.json',
                        (r) => r.fulfill({ json: manifest() }));
        await ctx.route('**/packs/drums/k_36_1.flac',
                        (r) => r.fulfill({ body: flac }));

        const open = async () =>
        {
            const pg = await ctx.newPage();

            pg.on('pageerror', (e) => errors.push(e.message));
            await pg.goto(url);
            await pg.click('#start');
            /* Started, which the page says by dropping data-unstarted. */
            await pg.waitForFunction(() => window.solo?.settled &&
                                     !('unstarted' in document.body.dataset),
                                     null, { timeout: 20000, polling: 100 });
            await pg.selectOption('#mode', 'patch');
            await pg.evaluate(() => window.solo.settled());
            await pg.selectOption('#patch', 'sampled_drums.dsp');
            await pg.evaluate(() => window.solo.settled());

            return pg;
        };
        /* Polled on a timer rather than on animation frames, which a
           browser stops for a page in the background -- and with two pages
           open, one of them is. */
        const offered = (pg) => pg.waitForFunction(
            () => !document.getElementById('packneed').hidden, null,
            { timeout: 5000, polling: 100 }).then(() => true, () => false);
        const quiet = (pg) => pg.waitForFunction(
            () => document.getElementById('packneed').hidden, null,
            { timeout: 5000, polling: 100 }).then(() => true, () => false);

        const pg = await open();

        check(await offered(pg) &&
              (await pg.$eval('#packneedtext', (e) => e.textContent))
                  .startsWith('Needs Drum kit'),
              'a sampled instrument whose pack is missing offers it: ' +
              await pg.$eval('#packneedtext', (e) => e.textContent));

        await pg.click('#packget');

        check(await quiet(pg),
              'Download takes the offer away once the pack is in');
        check(await pg.$eval('#packlist button', (b) => b.textContent) ===
              'Remove', 'and the pane says it is there');

        /* What the synth is handed for it: the FLAC decoded to a 16-bit
           mono WAV at 44.1 kHz, of the length it was, at its level -- the
           fixture is ffmpeg's sine, an eighth of full scale. */
        const decoded = await pg.evaluate(async (b64) =>
        {
            const { wavBytes } = await import('./packs.js');
            const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
            const w = await wavBytes({ url: 'x.flac' }, bytes.buffer);
            const v = new DataView(w.buffer);
            const tag = (at) => String.fromCharCode(...w.slice(at, at + 4));
            let peak = 0;

            for (let i = 44; i + 1 < w.length; i += 2)
                peak = Math.max(peak, Math.abs(v.getInt16(i, true)));

            return { riff: tag(0), wave: tag(8), data: tag(36),
                     channels: v.getUint16(22, true),
                     rate: v.getUint32(24, true),
                     bits: v.getUint16(34, true),
                     frames: v.getUint32(40, true) / 2, peak };
        }, flac.toString('base64'));

        check(decoded.riff === 'RIFF' && decoded.wave === 'WAVE' &&
              decoded.data === 'data' && decoded.channels === 1 &&
              decoded.rate === 44100 && decoded.bits === 16 &&
              decoded.frames === 221 &&
              Math.abs(decoded.peak - 32768 / 8) < 64,
              'a pack\'s FLAC is handed over as the WAV osc::sample reads: ' +
              JSON.stringify(decoded));

        const again = await open();

        check(await quiet(again),
              'a page opened later finds the pack kept and offers nothing');

        /* A deploy that rebuilt the pack: what is kept is not it. */
        version = '2';

        const rebuilt = await open();

        const reoffered = await offered(rebuilt);
        const label = await rebuilt.$eval('#packlist button',
                                          (b) => b.getAttribute('aria-label'));

        check(reoffered && label === 'Update Drum kit',
              'a pack rebuilt since it was kept is offered again, to update: ' +
              `${reoffered ? 'offered' : 'not offered'}, ${label}`);

        await rebuilt.close();
        version = '1';

        const button = (pg, text) => pg.waitForFunction(
            (t) => document.querySelector('#packlist button')
                ?.textContent === t, text, { timeout: 10000, polling: 100 })
            .then(() => true, () => false);

        await button(again, 'Remove');
        await again.$eval('#packlist button', (b) => b.click());

        check(await button(again, 'Download') && await offered(again),
              'Remove takes it out and the offer comes back');

        await ctx.close();
    }

    /* gen::grow on the page: the worklet asks, the mirror worker grows,
       and the graph that sounds is the grown one. */
    {
        const GROW_PIECE =
            'name "growing";\nseed 11;\n' +
            'instrument pad { dsp {\n' +
            'node ionode { channels = 1; out0 = vca->out; play = env->play; };\n' +
            'io ionode;\n' +
            'node freq misc::midi2freq { note = ionode->note; };\n' +
            'node osc osc::simple { freq = freq->out; amp = ionode->velocity; };\n' +
            'node env env::adsr { a = 5 ms; d = 200 ms; s = 0.5; r = 100 ms;\n' +
            '    trigger = ionode->trigger; };\n' +
            'node vca mixer::mul { in0 = osc->out; in1 = env->out; };\n' +
            '}; };\n' +
            'chain m { stage s gen::eno_line { notes = "A3 C4"; period = 0.5 s;\n' +
            '    prob = 1; }; sink { instrument = pad; }; };\n' +
            'chain g { stage s gen::grow { every = 1 s; depth = 2; shrink = 0; };\n' +
            '    sink { instrument = pad; }; };\n';

        await page.selectOption('#mode', 'piece');
        await page.evaluate(() => window.solo.settled());
        await page.evaluate((text) =>
        {
            document.getElementById('gen').value = text;
        }, GROW_PIECE);
        await page.click('#loadpiece');
        await page.evaluate(() => window.solo.settled());
        await page.click('#play');

        const grown = await until(
            () => page.evaluate(() => window.solo.grownText('pad')),
            (t) => t !== '');

        await page.click('#stop');

        check(grown.includes('ionode') && grown !== GROW_PIECE,
              'gen::grow grows the graph the worklet plays, grown by the ' +
              'mirror worker');
    }

    /* The ear on the page: a growth toward a graph one edit away, every
       edit heard by the page's ear worker, reaches it -- where the walk
       alone, worked out here in the module at the page's rate, goes
       elsewhere. */
    {
        const body =
            'node ionode { channels = 1; out0 = vca->out; play = env->play; };\n' +
            'io ionode;\n' +
            'node freq misc::midi2freq { note = ionode->note; };\n' +
            'node osc osc::simple { freq = freq->out; amp = ionode->velocity; };\n' +
            'node env env::adsr { a = 5 ms; d = 200 ms; s = 0.5; r = 100 ms;\n' +
            '    trigger = ionode->trigger; };\n' +
            'node vca mixer::mul { in0 = osc->out; in1 = env->out; };\n';
        const piece = (aim, target) =>
            'name "listening";\nseed 11;\n' +
            `instrument pad { dsp {\n${body}}; };\n` +
            `instrument aim { dsp {\n${aim}}; };\n` +
            'chain m { stage s gen::eno_line { notes = "A3 C4";\n' +
            '    period = 0.5 s; prob = 1; }; sink { instrument = pad; }; };\n' +
            'chain g { stage s gen::grow { every = 1 s; depth = 1;\n' +
            '    shrink = 0; kinds = "voice"; listen = 16; tune = 0;\n' +
            `    target = "${target}"; }; sink { instrument = pad; }; };\n`;
        const rate = (await page.evaluate(() => window.solo.transport()))
            ?.rate || 48000;
        const { default: createThinkWeb } = await import(
            pathToFileURL(path.join(build, 'thinkweb.js')).href);
        const M = await createThinkWeb({ print: () => {}, printErr: () => {} });
        let base = null;

        M._tw_create(rate, 256, 128);
        M._tw_grow_mode(2);
        M.ccall('tw_piece_load', 'number', ['string', 'number'],
                [piece(body, ''), 1]);
        M._tw_transport(0, 0, 0);       /* TW_START */

        const grow = (a) => JSON.parse(M.ccall('tw_grow', 'string',
            ['string', 'string', 'number', 'string', 'number'],
            [a.text, a.base, a.choice, a.kinds, a.want]));

        for (let done = 0; done < rate * 1.5; done += 128)
        {
            M._tw_render(128);

            for (let ask; (ask = M.UTF8ToString(M._tw_grow_next())); )
            {
                const a = JSON.parse(ask);
                const r = grow(a);

                base ??= a.base;
                M.ccall('tw_grow_answer', null,
                        ['number', 'string', 'string', 'string'],
                        [a.ticket, r.grown, r.what, r.why]);
            }
        }

        const wander = M.ccall('tw_grown_text', 'string', ['string'],
                               ['pad']);
        const aim = base === null ? undefined
            : grow({ text: base, base, choice: 0, kinds: 'voice', want: 16 })
                .grown.split('\x1e').find((o) => o !== wander);

        await page.evaluate((text) =>
        {
            document.getElementById('gen').value = text;
        }, piece(aim ?? body, 'aim'));
        await page.click('#loadpiece');
        await page.evaluate(() => window.solo.settled());
        await page.click('#play');

        const reached = await until(
            () => page.evaluate(() => window.solo.grownText('pad')),
            (t) => t !== '');

        await page.click('#stop');

        check(aim !== undefined && reached === aim,
              'gen::grow toward a target hears its edits in the page\'s ' +
              'ear worker, and reaches the one aimed at' +
              (reached === aim ? '' : reached === wander ? ': it wandered'
                                                         : ': it did not'));
    }

    /* A grow stage's popover: the edits grown, a row to go back to the
       declared graph, and Keep, which writes the graph playing into the
       piece. */
    {
        const body =
            'node ionode { channels = 1; out0 = vca->out; play = env->play; };\n' +
            'io ionode;\n' +
            'node freq misc::midi2freq { note = ionode->note; };\n' +
            'node osc osc::simple { freq = freq->out; amp = ionode->velocity; };\n' +
            'node env env::adsr { a = 5 ms; d = 200 ms; s = 0.5; r = 100 ms;\n' +
            '    trigger = ionode->trigger; };\n' +
            'node vca mixer::mul { in0 = osc->out; in1 = env->out; };\n';
        const PIECE =
            'name "kept";\nseed 11;\n' +
            `instrument pad { dsp {\n${body}}; };\n` +
            'chain m { stage s gen::eno_line { notes = "A3 C4";\n' +
            '    period = 0.5 s; prob = 1; }; sink { instrument = pad; }; };\n' +
            'chain g { stage s gen::grow { every = 1 s; depth = 2;\n' +
            '    shrink = 0; }; sink { instrument = pad; }; };\n';
        const grownText = () =>
            page.evaluate(() => window.solo.grownText('pad'));
        const grow = async () =>
        {
            await page.click('#play');

            const grown = await until(grownText, (t) => t !== '');

            await page.click('#stop');

            return grown;
        };
        const popover = async () =>
        {
            const handle = await page.evaluate(
                () => window.solo.handleOf(1, 0));
            const at = await scroller();

            await page.mouse.click(at.x + handle.x, at.y + handle.y);
            await page.waitForFunction(
                () => !document.getElementById('composerparams').hidden &&
                      document.querySelector('#composerparams .growthrow'),
                null, { timeout: 15000 }).catch(() => {});
        };

        await page.evaluate((text) =>
        {
            document.getElementById('gen').value = text;
        }, PIECE);
        await page.click('#loadpiece');
        await page.evaluate(() => window.solo.settled());
        await grow();
        await popover();

        const rows = await page.$$eval('#composerparams .growthrow',
                                       (r) => r.map((e) => e.textContent));

        check(rows.length >= 2 && rows[0] === 'the declared graph' &&
              rows[rows.length - 1].endsWith('(playing)'),
              `a grow stage's popover lists what grew: ${rows.join(' | ')}`);

        await page.click('#composerparams button.growthrow');

        check(await until(grownText, (t) => t === '') === '',
              '...and its first row puts the declared graph back');

        await page.keyboard.press('Escape');

        const grown = await grow();
        const added = [...grown.matchAll(/node (\w+) /g)]
            .map((n) => n[1]).filter((n) => !body.includes(`node ${n} `));

        await popover();
        await page.click('#composerparams button:has-text("Keep")');

        const kept = await until(
            () => page.evaluate(() => window.solo.genText()),
            (t) => added.length > 0 && t.includes(`node ${added[0]} `));

        check(added.length > 0 && kept.includes(`node ${added[0]} `) &&
              await grownText() === '',
              '...and Keep writes the grown graph into the piece, which ' +
              'then plays it as declared');
    }

    /* Evolve toward on the page: one instrument tuned toward another's
       sound in the page's evolve workers, the result heard on its
       channel, and kept into the piece. The two differ only in their
       sustain, which a tune finds. */
    {
        const graphWith = (s) =>
            'node ionode { channels = 1; out0 = vca->out; play = env->play; };\n' +
            'io ionode;\n' +
            'node freq misc::midi2freq { note = ionode->note; };\n' +
            'node osc osc::simple { freq = freq->out; amp = ionode->velocity; };\n' +
            `node env env::adsr { a = 5 ms; d = 200 ms; s = ${s}; r = 100 ms;\n` +
            '    trigger = ionode->trigger; };\n' +
            'node vca mixer::mul { in0 = osc->out; in1 = env->out; };\n';
        const PIECE =
            'name "evolving";\n' +
            `instrument pad { dsp {\n${graphWith('0.05')}}; };\n` +
            `instrument ref { dsp {\n${graphWith('0.7')}}; };\n` +
            'chain m { stage s gen::eno_line { notes = "A3"; period = 1 s;\n' +
            '    prob = 1; }; sink { instrument = pad; }; };\n';

        await page.evaluate((text) =>
        {
            document.getElementById('gen').value = text;
        }, PIECE);
        await page.click('#loadpiece');
        await page.evaluate(() => window.solo.settled());
        await page.selectOption('#evolvewhat', 'pad');
        await page.selectOption('#evolvetoward', 'ref');

        const fetched = [];
        const asked = (r) => fetched.push(r.url());

        page.on('request', asked);
        await page.click('#evolvego');

        const ended = await page.waitForFunction(
            () => !document.getElementById('evolvekeep').disabled ||
                  /found nothing|failed|could not/.test(
                      document.getElementById('evolvestate').textContent),
            null, { timeout: 300000 }).then(() => true, () => false);
        const state = await page.textContent('#evolvestate');
        const playing = await page.evaluate(
            () => window.solo.grownText('pad'));

        page.off('request', asked);
        check(ended && playing.includes('node env') &&
              !playing.includes('s = 0.05; r'),
              `Evolve toward tunes an instrument in the page's workers and ` +
              `puts the result on its channel: ${state}`);
        check(fetched.some((u) => u.endsWith('/thinkevolve.wasm')),
              '...as one search its workers score, Chromium having JSPI');

        await page.click('#evolvekeep');

        const kept = await until(
            () => page.evaluate(() => window.solo.genText()),
            (t) => !t.includes('s = 0.05; r'));

        check(!kept.includes('s = 0.05; r') && kept.includes('instrument ref'),
              '...and Keep writes it into the piece');
    }

    /* A patch, growing: Grow adds an edit as a row and plays it, the first
       row puts the patch back, a search toward another patch adds what it
       found, the target can be heard in its place and back, and Keep
       downloads the row chosen. */
    {
        const rows = () => page.$$eval('#growlist button',
                                       (b) => b.map((e) => e.textContent));
        const box = () => page.evaluate(
            () => document.getElementById('dsp').value);

        await page.selectOption('#mode', 'grow');
        await page.selectOption('#patch', 'ladder.dsp');
        await page.evaluate(() => window.solo.settled());
        await until(rows, (r) => r.length === 1 && r[0] === 'ladder.dsp');

        const picked = await box();

        await page.click('#growstep');

        const grown = await until(rows, (r) => r.length === 2);
        const edited = await box();

        check(grown.length === 2 && grown[0] === 'ladder.dsp' &&
              edited !== picked && /Loaded/.test(
                  await page.textContent('#status')),
              `Grow adds an edit as a row and plays it: ${grown.join(' | ')}`);

        await page.click('#growlist li:first-child button');
        check(await until(box, (t) => t === picked) === picked,
              '...and the first row puts the patch back');

        await page.selectOption('#growtoward', 'pluck.dsp');
        await page.click('#growgo');

        /* Stopped once it has come nearer than where it began, which is
           the first `nearest' it says. */
        const nearest = () => page.evaluate(() => Number(/nearest (\S+)/.exec(
            document.getElementById('growstate').textContent)?.[1]));
        await page.waitForFunction(() => /nearest/.test(
            document.getElementById('growstate').textContent), null,
            { timeout: 60000 }).catch(() => {});

        const began = await nearest();

        await page.waitForFunction((b) => Number(/nearest (\S+)/.exec(
            document.getElementById('growstate').textContent)?.[1]) < b ||
            document.getElementById('growstop').disabled,
            began, { timeout: 120000 }).catch(() => {});
        await page.evaluate(() => document.getElementById('growstop').click());

        const found = await until(rows, (r) => r.length === 2 &&
                                  r[1].startsWith('toward pluck.dsp'));

        check(found[1]?.startsWith('toward pluck.dsp') &&
              await box() !== picked,
              `...a search toward another patch adds what it found: ` +
              `${found[1]}`);

        const chosen = await box();

        await page.click('#growtarget');

        const pressed = await page.getAttribute('#growtarget',
                                                'aria-pressed');

        await page.click('#growtarget');
        check(pressed === 'true' &&
              await page.getAttribute('#growtarget', 'aria-pressed') ===
                  'false' && await box() === chosen &&
              (await rows()).length === 2,
              '...the target is heard in its place and then the row again');

        const [download] = await Promise.all([
            page.waitForEvent('download', { timeout: 10000 }),
            page.click('#growkeep')]);

        check(download.suggestedFilename() === 'ladder-grown.dsp',
              `...and Keep downloads it: ${download.suggestedFilename()}`);
    }

    /* The band: pulse.gen as players; the kick's pad dragged to busy
       fills its ring, Vary gives it a seed of its own while it plays,
       and its first take takes the seed out again. */
    {
        const kickOf = () => page.evaluate(() =>
        {
            const t = window.solo.genText();
            const at = t.indexOf('chain kick');

            return t.slice(at, t.indexOf('chain', at + 5));
        });
        const card = page.locator('.player[data-chain="kick"]');

        await page.selectOption('#mode', 'band');
        await page.selectOption('#piece', 'pulse.gen');
        await page.evaluate(() => window.solo.settled());

        const roles = await page.$$eval('.player', (p) =>
            p.map((e) => `${e.dataset.chain}:${e.className.split(' ')[1]}`));

        check(roles.length === 4 && roles.includes('kick:drums'),
              `the band shows pulse.gen's players: ${roles.join(' ')}`);

        await page.click('#play');

        /* The page test's window is short: the pad has to be on it. */
        await card.locator('.bandpad').scrollIntoViewIfNeeded();
        const pad = await card.locator('.bandpad').boundingBox();

        await page.mouse.move(pad.x + pad.width / 2, pad.y + pad.height / 2);
        await page.mouse.down();
        await page.mouse.move(pad.x + pad.width - 1, pad.y + pad.height / 2,
                              { steps: 5 });
        await page.mouse.up();

        const busy = await until(kickOf, (t) => /fills = 4;/.test(t));
        const kickVaries = await card.locator('button',
                                              { hasText: 'Vary' }).count();

        await page.click('#stop');
        await page.evaluate(() => window.solo.settled());

        check(/fills = 4;/.test(busy) && kickVaries === 0,
              '...the kick\'s pad at busy fills its ring, and a ring with ' +
              `nothing random in it offers no Vary: ${/fills = \d+/.exec(busy)}`);

        /* The bell an octave up: its note list respelled, as the panel
           writes a list. */
        const bell = page.locator('.player[data-chain="bell"]');

        await bell.locator('button', { hasText: '▲' }).click();

        const up = await until(
            () => page.evaluate(() => window.solo.genText()),
            (t) => /notes = "C6 Eb6 G6 Bb6";/.test(t));

        check(/notes = "C6 Eb6 G6 Bb6";/.test(up),
              '...and the bell\'s ▲ moves its notes up an octave');
    }

    /* Vary on a player with chance in it: ebb's tape loops get a seed of
       their own while the piece plays, and the first take takes it out. */
    {
        await page.selectOption('#piece', 'ebb.gen');
        await page.evaluate(() => window.solo.settled());

        const card = page.locator('.player[data-chain="cloud_low"]');
        const chainOf = () => page.evaluate(() =>
        {
            const t = window.solo.genText();
            const at = t.indexOf('chain cloud_low');

            return t.slice(at, t.indexOf('chain', at + 5));
        });

        await page.click('#play');
        await card.locator('button', { hasText: 'Vary' }).click();

        const varied = await until(chainOf, (t) => /seed \d+;/.test(t));
        const seeds = await page.evaluate(() =>
            (window.solo.genText().match(/^[ \t]+seed \d+;/gm) ?? []).length);

        await card.locator('.takes button', { hasText: '1' }).click();

        const back = await until(chainOf, (t) => !/seed \d+;/.test(t));

        await page.click('#stop');
        await page.evaluate(() => window.solo.settled());

        check(/seed \d+;/.test(varied) && seeds === 1 && !/seed/.test(back),
              '...Vary gives a player a take of its own while it plays, ' +
              'and its first take puts the piece\'s back: ' +
              JSON.stringify({ varied: /seed \d+/.exec(varied)?.[0] ?? null,
                               seeds, back: /seed \d+/.exec(back)?.[0] ??
                                            null }));
    }

    /* The pictures: a drawn Life board is offered Restart rather than
       Vary, and a grid player's picture is a sequencer track -- a click on
       it draws a note into the piece. */
    {
        await page.selectOption('#piece', 'glider.gen');
        await page.evaluate(() => window.solo.settled());

        const walk = page.locator('.player[data-chain="walk"]');
        const moves = await walk.locator('.moves button')
            .allTextContents();
        const board = await walk.locator('canvas.bandpic.control').count();

        check(moves.includes('Restart') && !moves.includes('Vary') &&
              board === 1,
              '...a drawn Life board is offered Restart, not Vary, and ' +
              `shows its board to paint: ${moves.join(' ')}`);

        await page.selectOption('#piece', 'scratch.gen');
        await page.evaluate(() => window.solo.settled());

        const cellsOf = () => page.evaluate(() =>
        {
            const t = window.solo.genText();

            return /cells *= *"[^"]*"/.exec(t.slice(
                t.indexOf('chain drums_snare')))?.[0];
        });
        const was = await cellsOf();
        const pic = page.locator('.player[data-chain="drums_snare"] ' +
                                 'canvas.bandpic');

        await pic.scrollIntoViewIfNeeded();
        await page.waitForTimeout(500);
        const at = await pic.boundingBox();

        await page.mouse.click(at.x + at.width * 0.53, at.y + at.height / 2);

        const now = await until(cellsOf, (c) => c !== was);

        check(now !== was,
              '...and a click on a grid player\'s picture draws into its ' +
              `cells: ${was} -> ${now}`);
    }

    /* A drawn player's pad plays the drawing: its first touch puts a
       chance, a ratchet and a vary behind the grid, and the pad moves
       those, leaving the cells as drawn. */
    {
        await page.selectOption('#piece', 'scratch.gen');
        await page.evaluate(() => window.solo.settled());

        const chainOf = () => page.evaluate(() =>
        {
            const t = window.solo.genText();
            const at = t.indexOf('chain keys');

            return t.slice(at, t.indexOf('};\n};', at));
        });
        const cells = /cells *= *"[^"]*"/.exec(await chainOf())?.[0];
        const pad = page.locator('.player[data-chain="keys"] .bandpad');

        await page.click('#play');
        await pad.scrollIntoViewIfNeeded();
        const at = await pad.boundingBox();

        await page.mouse.move(at.x + at.width / 2, at.y + at.height / 2);
        await page.mouse.down();
        await page.mouse.move(at.x + at.width - 2, at.y + 2, { steps: 6 });
        await page.mouse.up();

        const keys = await until(chainOf, (t) =>
            /xform::ratchet[^}]*prob = 0\.[1-9]/.test(t) &&
            /xform::vary[^}]*leap = 0\.[1-9]/.test(t));

        await page.click('#stop');
        await page.evaluate(() => window.solo.settled());

        check(/stage thin xform::chance/.test(keys) &&
              /xform::ratchet[^}]*prob = 0\.[1-9]/.test(keys) &&
              /xform::vary[^}]*leap = 0\.[1-9]/.test(keys) &&
              keys.includes(cells),
              '...a drawn player\'s pad puts a chance, a ratchet and a ' +
              'vary behind its grid and moves them, the cells as drawn');

        /* A drum is loosened in time and touch, not pitch. */
        const kick = page.locator('.player[data-chain="drums_kick"] .bandpad');

        await kick.scrollIntoViewIfNeeded();
        await kick.focus();
        await page.keyboard.press('ArrowUp');
        await page.keyboard.press('ArrowUp');

        const drums = await until(() => page.evaluate(() =>
        {
            const t = window.solo.genText();
            const at = t.indexOf('chain drums_kick');

            return t.slice(at, t.indexOf('chain', at + 5));
        }), (t) => /xform::humanize[^}]*time = 0\.0[0-9]*[1-9]/.test(t));
        const focused = await page.evaluate(() =>
            document.activeElement?.closest('.player')?.dataset.chain);

        check(/stage loose xform::humanize/.test(drums) &&
              !/xform::vary/.test(drums) && focused === 'drums_kick',
              '...a drum\'s pad, from the keys, puts a humanize behind it ' +
              'rather than a vary, and keeps the keys');
    }

    /* A pad moved only across leaves what is up the pad as written: an
       lsystem at depth 0 -- a sequencer -- stays one. */
    {
        await page.selectOption('#piece', 'acetate.gen');
        await page.evaluate(() => window.solo.settled());

        const pad = page.locator('.player[data-chain="kickline"] .bandpad');

        await pad.scrollIntoViewIfNeeded();
        const at = await pad.boundingBox();

        await page.mouse.move(at.x + at.width / 2, at.y + at.height / 2);
        await page.mouse.down();
        await page.mouse.move(at.x + at.width * 0.9, at.y + at.height / 2,
                              { steps: 4 });
        await page.mouse.up();
        await page.waitForTimeout(1000);
        await page.evaluate(() => window.solo.settled());

        const text = await page.evaluate(() => window.solo.genText());
        const kick = text.slice(text.indexOf('chain kickline'),
                                text.indexOf('chain snareline'));

        check(/depth = 0;/.test(kick) && !/depth = 1;/.test(kick),
              '...and a pad moved across leaves a depth-0 lsystem at 0');
    }

    /* A band from nothing: New writes a beat, a bass and keys; the
       gallery adds a player on an instrument of its own; a card's menu
       puts it on another graph and its × takes it out, instrument and
       all; and the piece is kept for the next session as it changes. */
    {
        const text = () => page.evaluate(() => window.solo.genText());
        const cards = () => page.$$eval('.player', (p) =>
            p.map((e) => `${e.dataset.chain}:${e.className.split(' ')[1]}`)
                .join(' '));

        await page.selectOption('#mode', 'band');

        /* The piece before it was changed, so New asks first. */
        let asked = false;

        page.once('dialog', (d) =>
        {
            asked = true;
            d.accept();
        });
        await page.click('#bandnew');
        const band = await until(cards, (c) => c.startsWith('kick:drums'));

        check(asked && band === 'kick:drums snare:drums hats:drums ' +
                         'bass:bass keys:melody',
              'New band asks before putting a changed piece away, and is ' +
              `a beat, a bass and keys: ${band}`);

        /* The band's key: read back from its `key' scale, and a new one
           moves every player but the drums there, step for step. */
        const keyShown = await page.evaluate(() =>
            `${document.getElementById('bandroot').selectedOptions[0].text} ` +
            document.getElementById('bandmood').value);

        await page.selectOption('#bandroot', { label: 'C' });
        await until(text, (t) => /scale key "C3/.test(t));
        await page.selectOption('#bandmood', 'bright');
        const inC = await until(text,
            (t) => /scale key "C3 D3 E3 F3 G3 A3 B3"/.test(t));

        check(keyShown === 'A dark' &&
              inC.includes('notes = "C4 E4 F4 G4 B4 C5 E5"') &&
              inC.includes('notes = "C2 C2 G2"') &&
              inC.includes('notes = "C2";') && inC.includes('notes = "D2";'),
              `...its key reads ${keyShown}, and C bright moves the keys ` +
              'and the bass there and leaves the drums');

        await page.click('#bandadd');
        await page.click('#bandgallery button[data-idea="Slow changes"]');
        const chords = await until(text, (t) => /chain chords/.test(t));

        check(/chain chords \{[^]*scale = key;/.test(chords),
              '...and a chord player added names the key');

        await page.click('#bandadd');
        await page.click('#bandgallery button[data-idea="Game of Life"]');
        const added = await until(cards, (c) => c.includes('air:texture'));
        const withAir = await text();

        check(added.includes('air:texture') &&
              /instrument air \{ dsp "bowl\.dsp"/.test(withAir) &&
              /chain air \{[^]*gen::life/.test(withAir),
              '...the gallery adds a player on an instrument of its own');

        await page.selectOption('.player[data-chain="bass"] select.sound',
                                'fmbass.dsp');
        const resounded = await until(text,
            (t) => /instrument bass \{ dsp "fmbass\.dsp"/.test(t));

        check(/instrument bass \{ dsp "fmbass\.dsp"/.test(resounded),
              '...a card\'s menu puts its player on another graph');

        await page.click('.player[data-chain="air"] .out');
        const gone = await until(text, (t) => !/chain air/.test(t));

        check(!/chain air|instrument air/.test(gone),
              '...and its × takes the player out, instrument and all');

        await page.click('#undo');
        const back = await until(text, (t) => /chain air/.test(t));
        /* Written a second after the last change. */
        const kept = await until(() => page.evaluate(() =>
            JSON.parse(localStorage.getItem('thinksynth:piece'))?.text),
            (t) => t === back);

        check(/chain air/.test(back) && kept === back,
              '...which Undo puts back, and the session keeps the piece ' +
              'as it is now');
    }

    /* Scenes: a piece's sections as buttons, the one playing lit and one
       chosen played from; a rest in a scene written into it; + Scene
       copying the scene, and a scene renamed, resized and taken out. */
    {
        const text = () => page.evaluate(() => window.solo.genText());
        const scenes = () => page.$$eval('#bandscenes .scene:not(.add)',
            (b) => b.map((e) => (e.classList.contains('on') ? '*' : '') +
                                e.firstChild.textContent).join(' '));
        const block = (t, name) =>
        {
            const at = t.indexOf(`section ${name} `);

            return at < 0 ? '' : t.slice(at, t.indexOf('};', at));
        };

        await page.selectOption('#mode', 'band');
        await page.selectOption('#piece', 'mirrorball.gen');
        await page.evaluate(() => window.solo.settled());
        await page.click('#play');
        const lit = await until(scenes, (s) => s.startsWith('*intro'));
        const sounding = await (await page.waitForFunction(() =>
            document.querySelector('.player.sounding')?.dataset.chain,
            null, { polling: 'raf', timeout: 10000 })).jsonValue();

        check(['hats', 'guitar', 'swoop'].includes(sounding),
              'a card lights as its player sounds, and not one resting in ' +
              `the scene: ${sounding}`);

        /* Full, which is long enough that a slow machine still rests the
           guitar inside it. */
        await page.click('#bandscenes .scene:has-text("full")');
        const jumped = await until(scenes, (s) => s.includes('*full'));

        await page.click('.player[data-chain="guitar"] .moves ' +
                         'button:has-text("Rest")');
        const rested = await until(text,
            (t) => /guitar\s*=\s*0;/.test(block(t, 'full')));

        await page.click('#stop');
        await page.evaluate(() => window.solo.settled());

        check(lit.startsWith('*intro') && jumped.includes('*full') &&
              /guitar\s*=\s*0;/.test(block(rested, 'full')) &&
              !/guitar\s*=\s*0;/.test(block(rested, 'groove')),
              `scenes are the piece's sections, the one playing lit: ${lit}; ` +
              'a scene chosen is played from, and a rest is written into it');

        page.once('dialog', (d) => d.accept());
        await page.click('#bandnew');
        await until(text, (t) => t.startsWith('name "A new band"'));
        await page.evaluate(() => window.solo.settled());
        await page.click('#bandscenes .scene.add');
        await until(text, (t) => /section scene 8 bars/.test(t));
        const focus = () => page.evaluate(() =>
            document.activeElement.dataset.f);

        await page.focus('.player[data-chain="kick"] .rest');
        await page.keyboard.press('Enter');
        await until(text, (t) => /kick = 0;/.test(block(t, 'scene')));
        const kept = await focus();
        await page.click('#bandscenes .scene.add');
        const copied = await until(text, (t) => /section scene2/.test(t));

        page.once('dialog', (d) => d.accept('chorus'));
        await page.click('#bandscenes button:has-text("Rename")');
        await until(text, (t) => /section chorus/.test(t));
        await page.selectOption('#bandscenes select', '4');
        const sized = await until(text,
            (t) => /section chorus 4 bars/.test(t));

        await page.click('#bandscenes button[aria-label^="Take chorus"]');
        const out = await until(text, (t) => !/section chorus/.test(t));

        check(/kick = 0;/.test(block(copied, 'scene2')) &&
              /section chorus 4 bars/.test(sized) &&
              !/section chorus/.test(out) && /section scene /.test(out),
              '...+ Scene copies the scene, which can be renamed, resized ' +
              'and taken out');

        await page.focus('.player[data-chain="kick"] .out');
        await page.keyboard.press('Enter');
        const gone = await until(text, (t) => !/kick/.test(t));
        const next = await focus();

        check(!/kick/.test(gone) && /section scene /.test(gone),
              '...and a player resting in a scene can be taken out of ' +
              'the band');
        check(kept === 'kick:rest' && next === 'snare:pad',
              '...the keys staying on Rest as the cards are drawn again, ' +
              `and going to the next card's pad: ${kept}, ${next}`);
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
