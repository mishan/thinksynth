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
 * phonetest.mjs -- a phone played on the page, end to end, in Chromium:
 * the page and a phone's page (phone.html) on the built site, paired
 * through a relay in this process.
 *
 *   node wasm/web/phonetest.mjs [BUILD_DIR]
 *
 * The Phone button shows a link and its QR code; the phone's page opens
 * the link, taps Start and plays: a touch is C3 bent along the scale,
 * tilting brightens what it holds, the phone page hidden lets go at once,
 * its page reloaded comes back to play again, and Disconnect unpairs it.
 * The pane's form has a card for each finger saying what it does, Learn
 * opens the one put down, and an edit writes the player's profile and
 * plays at once; one that wouldn't play is refused, as is one over text
 * typed and not applied, and the text box's own comments are kept.
 * What the page plays is what it posts the worklet, as theremintest.mjs
 * reads it.
 *
 * Exit status is the number of failures.
 */

import './headless.mjs';

import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { chromium } from 'playwright';

import { relay } from '../../relay/relay.mjs';
import { serve } from './serve.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ?? path.join(here, '..', '..', 'build-web'));

let failures = 0;

function check (cond, what, detail = '')
{
    process.stdout.write(`${cond ? 'ok   ' : 'FAIL '} ${what}` +
                         `${!cond && detail ? `: ${detail}` : ''}\n`);
    failures += cond ? 0 : 1;
}

/* What the page posts the worklet: its notes, and a controller's actions. */
function capture ()
{
    window.posted = [];

    const post = MessagePort.prototype.postMessage;

    MessagePort.prototype.postMessage = function (m, ...rest)
    {
        if (m?.type === 'on' || m?.type === 'off')
            window.posted.push(`${m.type} ${m.note}`);
        else if (m?.type === 'hid')
            window.posted.push(`hid ${m.kind} ${m.note} ${m.aux ?? ''} ${m.t?.toFixed(2)}`);
        return post.call(this, m, ...rest);
    };
}

const q = await relay({ port: 0, host: '127.0.0.1' });
const site = await serve(build, 0, '127.0.0.1', `ws://127.0.0.1:${q.address().port}`);
const origin = `http://127.0.0.1:${site.address().port}`;
const browser = await chromium.launch(
    { args: ['--autoplay-policy=no-user-gesture-required'] });

const until = (page, f, arg, ms = 8000) => page.waitForFunction(f, arg, { timeout: ms })
    .then(() => true, () => false);
const posted = (page, text) => until(page, (t) => window.posted.includes(t), text);
const text = (page, id) => page.textContent(`#${id}`);

/* A finger on the phone's pad at (x across, y up), 0 to 1. */
async function touch (phone, x, y)
{
    const { width, height } = phone.viewportSize();

    await phone.mouse.move(x * width, (1 - y) * height);
    await phone.mouse.down();
}

/* The phone's page opened on the link, and Start tapped. */
async function joined (phone, link)
{
    await phone.goto(link);
    await phone.click('#start');
    return until(phone, () => /Playing/.test(document.getElementById('status').textContent));
}

try
{
    const host = await browser.newPage();
    const errors = [];

    host.on('pageerror', (e) => errors.push(e.message));
    await host.addInitScript(capture);
    await host.goto(`${origin}/index.html?panes=0`);
    await host.click('#start');
    await host.waitForFunction(() => !document.getElementById('loadpiece').disabled,
                               null, { timeout: 60000 });
    await host.selectOption('#mode', 'patch');
    await host.evaluate(() => window.solo.settled());

    /* Twice before the first has got anywhere: one pairing. */
    await host.evaluate(() =>
    {
        document.getElementById('phone').click();
        document.getElementById('phone').click();
    });
    check(await until(host, () => !document.getElementById('phonelink').hidden &&
                                  document.querySelector('#phoneqr svg title') !== null) &&
          q.pairs.size === 1,
          'the Phone button shows a link to open on the phone, and its QR code, ' +
          'one pairing however often it is pressed while it starts',
          `${await text(host, 'phonestatus')}; ${q.pairs.size} pairings`);

    /* The page reloaded before anyone opened the code: it is shown again. */
    const shown = await host.getAttribute('#phonelink', 'href');

    await host.reload();
    await host.click('#start');
    await host.waitForFunction(() => !document.getElementById('loadpiece').disabled,
                               null, { timeout: 60000 });
    await host.selectOption('#mode', 'patch');
    await host.evaluate(() => window.solo.settled());
    check(await until(host, () => !document.getElementById('phonelink').hidden) &&
          await host.getAttribute('#phonelink', 'href') === shown && q.pairs.size === 1,
          'the page reloaded with its code unopened shows the same code again');

    /* New code: the old link is retired, and the new one plays. */
    await host.click('#phoneagain');
    await until(host, (was) => document.getElementById('phonelink').href !== was &&
                               !document.getElementById('phonelink').hidden, shown);

    const link = await host.getAttribute('#phonelink', 'href');
    const stale = await browser.newPage({ viewport: { width: 400, height: 800 } });

    await stale.goto(shown);
    await stale.click('#start');
    check(link !== shown &&
          await until(stale, () => /used or gone/.test(document.getElementById('status').textContent)),
          'New code retires the old link: a phone opening it is told so',
          await text(stale, 'status'));
    await stale.close();

    check(/\/phone\.html#pair=[A-Za-z0-9_-]{22}$/.test(link),
          'the link is the phone\'s page with the code in its fragment', link);

    const phone = await browser.newPage({ viewport: { width: 400, height: 800 } });

    check(await joined(phone, link) &&
          await until(host, () => /A phone is playing/.test(
              document.getElementById('phonestatus').textContent)) &&
          await host.isHidden('#phonelink'),
          'the phone opens it and taps Start; the page says a phone is playing ' +
          'and puts the code away', await text(host, 'phonestatus'));

    await touch(phone, 0.2, 0.9);
    check(await posted(host, 'on 48') && await posted(host, 'hid 3 48 0 7.00'),
          'a finger a fifth of the way across plays C3, a fifth up',
          JSON.stringify(await host.evaluate(() => window.posted.slice(-6))));

    await phone.evaluate(() => window.dispatchEvent(new DeviceMotionEvent('devicemotion',
        { accelerationIncludingGravity: { x: -9.81 * 0.5, y: 0, z: 9.81 * 0.86 } })));
    check(await posted(host, 'hid 4 48 1 0.75'),
          'tilting the phone right brightens the note it holds (aux1)',
          JSON.stringify(await host.evaluate(() => window.posted.filter((p) => p.startsWith('hid 4')).slice(-3))));

    await phone.mouse.up();
    check(await posted(host, 'off 48'), 'and lifting the finger ends it');

    /* Hidden with a finger down: let go of at once, not a second later. */
    await host.evaluate(() => { window.posted.length = 0; });
    await touch(phone, 0.5, 0.5);
    await posted(host, 'on 48');
    await host.evaluate(() => { window.posted.length = 0; });

    const t0 = Date.now();

    await phone.evaluate(() =>
    {
        Object.defineProperty(document, 'hidden', { value: true, configurable: true });
        document.dispatchEvent(new Event('visibilitychange'));
    });
    check(await posted(host, 'off 48') && Date.now() - t0 < 700,
          'the phone\'s page hidden lets its note go at once', `${Date.now() - t0} ms`);
    await phone.mouse.up();

    /* The phone's page reloaded: it comes back with its token. */
    await phone.reload();
    check(await joined(phone, link), 'the phone\'s page reloaded comes back to play');
    await host.evaluate(() => { window.posted.length = 0; });
    await touch(phone, 0.2, 0.5);
    check(await posted(host, 'on 48'), 'and plays again');
    await phone.mouse.up();

    /* The form, with the phone playing. */
    const PROFILE = 'thinksynth:hid:phone';
    const mine = () => host.evaluate((k) => localStorage.getItem(k), PROFILE);
    const row = (c) => host.evaluate((c) =>
    {
        const f = document.querySelector(`[aria-label="${c} now"]`)?.parentElement;

        return f && { whose: f.querySelector('.hint').textContent,
                      action: f.querySelector('select')?.value,
                      learned: f.classList.contains('learned') };
    }, c);
    const change = (label, value) => host.evaluate(([label, value]) =>
    {
        const e = document.querySelector(`[aria-label="${label}"]`);

        e.value = value;
        e.dispatchEvent(new Event('change'));
    }, [label, value]);
    const press = (label) => host.evaluate((label) =>
        document.querySelector(`[aria-label="${label}"]`).click(), label);

    await host.evaluate(() =>
    {
        document.getElementById('phonepane').open = true;
        document.getElementById('phoneform').open = true;
        document.getElementById('phonetext').open = true;
    });
    await until(host, () => document.querySelectorAll('#phonerows .formcontrol').length === 17);

    const cards = await host.evaluate(() => [...document.querySelectorAll('#phonerows .formgroup > summary')]
        .map((s) => s.textContent.replace(/\s+/g, ' ')));

    check(cards.length === 6 &&
          cards[0] === 'Finger 1 tap: note 0 x: bend, pentatonic y: aux 3' &&
          cards[4] === 'Finger 5 tap: nothing x: nothing y: nothing' &&
          cards[5] === 'Tilt x: aux 1 y: nothing',
          'the form has a card for each finger and tilt, saying in a line what each does',
          JSON.stringify(cards));
    check((await row('touch_1'))?.whose === "the phone's own" &&
          (await row('touch_1')).action === 'note' && (await row('touch_5'))?.action === undefined,
          'every control is on one, the phone\'s own bindings as its own',
          JSON.stringify(await row('touch_1')));
    check(await host.evaluate(() => document.querySelector('[aria-label="Add touch_1"]') === null &&
                                    document.querySelector('[aria-label="Add touch_5"]') !== null &&
                                    document.querySelector('[aria-label="Add touch_1_x"]') !== null),
          'a bound finger has no Add, as a button does one thing; an axis does');

    await host.click('#phonelearn');
    await touch(phone, 0.2, 0.5);
    check(await until(host, () => document.querySelector('.formcontrol.learned output')
                                      ?.ariaLabel === 'touch_1 now' &&
                                  document.querySelector('.formgroup').open),
          'Learn opens the card of the finger put down, and marks it',
          `${await text(host, 'phoneformproblem')} ${JSON.stringify(await row('touch_1'))}`);
    check(await until(host, () =>
              document.querySelector('[aria-label="touch_1 now"]').textContent === 'down' &&
              document.querySelector('[aria-label="touch_1_x now"]').textContent === '0.20'),
          'and its card shows the finger down, and where across',
          await host.evaluate(() => document.querySelector('[aria-label="touch_1_x now"]').textContent));
    await phone.mouse.up();

    await change('touch_1 1 semis', '12');
    check(await until(host, (k) => localStorage.getItem(k) ===
                                   'device phone {\n    touch_1 = note 12;\n};\n', PROFILE) &&
          await host.inputValue('#phoneprofile') === await mine() &&
          await until(host, () => document.querySelector('[aria-label="touch_1 now"]')
                                      .parentElement.querySelector('.hint').textContent === 'yours'),
          'an edit writes it as the player\'s, in the text box too', await mine());
    await host.evaluate(() => { window.posted.length = 0; });
    await touch(phone, 0.2, 0.5);
    check(await posted(host, 'on 60'), 'and the phone plays it at once, an octave up');
    await phone.mouse.up();

    const was = await mine();

    await change('touch_1 1 semis', '200');
    check(await until(host, () => document.getElementById('phoneformproblem').textContent !== '') &&
          await mine() === was, 'an edit that wouldn\'t play is refused, and says why',
          await text(host, 'phoneformproblem'));

    const typed = '# my phone\ndevice phone {\n    touch_1 = note 12;  # up\n};\n';

    await host.fill('#phoneprofile', typed);
    await press('Off touch_2');
    check(await until(host, () => /Apply/.test(document.getElementById('phoneformproblem').textContent)) &&
          await mine() === was && await host.inputValue('#phoneprofile') === typed,
          'an edit while the text box has changes not applied is refused, the typing kept',
          await text(host, 'phoneformproblem'));

    await host.click('#phoneapply');
    await until(host, (k) => localStorage.getItem(k).startsWith('# my phone'), PROFILE);
    await press('Off touch_2');
    check(await until(host, (k) => localStorage.getItem(k) ===
              '# my phone\ndevice phone {\n    touch_1 = note 12;  # up\n    ignore touch_2;\n};\n',
              PROFILE) && await until(host, () => document.querySelector(
              '[aria-label="Off touch_2"]') === null),
          'the text box applied, the form edits it with its comments kept', await mine());

    await press('Reset touch_1');
    await until(host, () => document.querySelector('[aria-label="Reset touch_1"]') === null);
    await press('Reset touch_2');
    check(await until(host, (k) => localStorage.getItem(k) === '# my phone\ndevice phone {\n    # up\n};\n',
                      PROFILE), 'Reset gives each its own back', await mine());

    await host.click('#phoneend');
    check(await until(phone, () => /Unpaired/.test(document.getElementById('status').textContent)) &&
          await until(host, () => /unpaired/.test(document.getElementById('phonestatus').textContent)),
          'Disconnect unpairs the phone, and both pages say so',
          `${await text(host, 'phonestatus')} / ${await text(phone, 'status')}`);

    check(errors.length === 0, `no page errors${errors.length ? ': ' + errors.join('; ') : ''}`);
}
catch (e)
{
    check(false, `threw: ${e.stack}`);
}

await browser.close();
q.shutdown();
site.close();
process.stdout.write(`\n${failures} failure${failures === 1 ? '' : 's'}\n`);
process.exit(failures);
