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
 */

/*
 * hidtest.mjs -- Valve Index controllers on the page, through a fake
 * navigator.hid (hid.js).
 *
 *   node wasm/web/hidtest.mjs [BUILD_DIR]
 *
 * Dongles that answer as the real ones do -- a feature report turned away
 * once before it is answered, the configuration in pieces, deflated --
 * and the messages a controller sends, built here. What is held is what
 * the page posts the worklet (a note's 'on' and 'off', a value's or a
 * bend's 'hid') and what it fetches: with no controller granted the
 * module is never asked for, with one it is, and a dongle is switched,
 * plays, says its controller has gone and is asked again. The decoding
 * itself is hidcheck.mjs's and scripts/hidcheck's.
 *
 * Exit status is the number of failures.
 */

import './headless.mjs';

import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';

import { chromium } from 'playwright';

import { serve } from './serve.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ??
                           path.join(here, '..', '..', 'build-web'));

let failures = 0;
const check = (ok, what) =>
{
    process.stdout.write(`${ok ? 'ok   ' : 'FAIL '} ${what}\n`);
    failures += ok ? 0 : 1;
};

const config = (hand, serial) => [...zlib.deflateSync(Buffer.from(JSON.stringify(
    { model_number: `Knuckles ${hand}`, device_serial_number: serial })))];

/* In the page: navigator.hid, with the devices `granted' at start and
   those `offered' by the button: a dongle (its product id, serial,
   whether a controller is on it, how many switches it turns down first),
   or one of a wired controller's interfaces (`part'); `configs' each
   serial's configuration, deflated; `absent' no WebHID at all. */
function fake ({ granted, offered, configs, absent })
{
    if (absent)
    {
        delete Navigator.prototype.hid;
        return;
    }

    const refused = () => Promise.reject(new DOMException('no', 'NotAllowedError'));

    class Dongle extends EventTarget
    {
        constructor ({ productId, serial, on, turnsDown = 0 })
        {
            super();
            Object.assign(this, { vendorId: 0x28de, productId, serial, on,
                                  turnsDown,
                                  productName: 'Watchman dongle',
                                  opened: false, turned: 0, switched: false,
                                  collections: [{ usagePage: 0xff00,
                                                  inputReports: [{ reportId: 0x23 }],
                                                  featureReports: [] }] });
            this.config = configs[serial];
            this.at = 0;
        }

        async open () { this.opened = true; }

        async sendFeatureReport (id, data)
        {
            if (id === 0xff && this.turnsDown-- > 0)
                throw new DOMException('turned down', 'NotAllowedError');
            window.hidSent.push(`${this.serial} ${id.toString(16)} ` +
                                [...data.slice(0, 5)].map((b) => b.toString(16)).join(' '));
            if (id === 0xff && data[0] === 0x87)
                this.switched = true;
        }

        /* Each asked for is turned away once, then answered, as the real
           ones are, and only while a controller is on the radio. */
        async receiveFeatureReport (id)
        {
            if (!this.on || this.turned++ % 2 === 0)
                return refused();

            const r = new Uint8Array(64);

            r[0] = id;
            if (id === 0x10)
            {
                r[1] = this.config.length >> 8;
                r[2] = this.config.length & 0xff;
                this.at = 0;
            }
            else
            {
                const part = this.config.slice(this.at, this.at + 62);

                r[1] = part.length;
                r.set(part, 2);
                this.at += part.length;
            }
            return new DataView(r.buffer);
        }

        /* A radio message, as report 0x23. */
        send (payload)
        {
            const r = new Uint8Array(29);

            r.set([0x10, payload.length + 1, this.tick = (this.tick ?? 0) + 1]);
            r.set(payload, 3);
            this.raw(0x23, r);
        }

        raw (reportId, bytes)
        {
            const e = new Event('inputreport');

            Object.assign(e, { device: this, reportId,
                               data: new DataView(Uint8Array.from(bytes).buffer) });
            this.dispatchEvent(e);
        }
    }

    /* A wired controller's three interfaces, as WebHID gives them: the
       IMU's, which takes the feature reports, the light sensors', and the
       input's, whose reports have no ID. */
    class Wired extends Dongle
    {
        constructor ({ serial, part })
        {
            super({ productId: 0x2300, serial, on: true });
            this.productName = 'Index Controller';
            this.part = part;
            this.collections = [{
                usagePage: 0xff00,
                inputReports: [{ reportId: part === 'imu' ? 0x20 : part === 'input' ? 0 : 0x21 }],
                featureReports: part === 'imu' ? [{ reportId: 0x10 }, { reportId: 0x11 }] : [],
            }];
        }

        /* The input report, its buttons byte at 0x08. */
        press (a)
        {
            const r = new Uint8Array(64);

            r.set([0x01, 0, 0x07, 0x3c]);
            r[0x08] = a ? 0x04 : 0;
            this.raw(0, r);
        }
    }

    const make = (list) => list.map((d) => d.part ? new Wired(d) : new Dongle(d));
    const hid = new EventTarget();
    const grants = make(granted);
    const offers = make(offered);

    hid.getDevices = async () => grants;
    hid.requestDevice = async () => offers;
    Object.defineProperty(Navigator.prototype, 'hid', { get: () => hid });
    window.dongles = [...grants, ...offers];
    window.unplug = (device) => hid.dispatchEvent(
        Object.assign(new Event('disconnect'), { device }));
    window.hidSent = [];

    /* What the page posts the worklet: notes and a controller's actions. */
    window.posted = [];

    const post = MessagePort.prototype.postMessage;

    MessagePort.prototype.postMessage = function (m, ...rest)
    {
        if (['on', 'off', 'midion', 'midioff'].includes(m?.type))
            window.posted.push(`${m.type} ${m.note}`);
        else if (m?.type === 'hid')
            window.posted.push(`hid ${m.kind} ${m.channel} ${m.arg ?? m.note}`);
        return post.call(this, m, ...rest);
    };
}

const site = await serve(build, 0, '127.0.0.1', null);
const url = `http://127.0.0.1:${site.address().port}/index.html?panes=0`;
const browser = await chromium.launch(
    { args: ['--autoplay-policy=no-user-gesture-required'] });
const configs = { 'D-1': config('Right', 'LHR-R'), 'D-2': config('Left', 'LHR-L'),
                  'D-3': config('', 'LHR-3'), 'D-4': config('', 'LHR-4') };

/* A page with the given fake, started, and the module's fetches kept; in
   `context', a browser whose storage is kept from page to page. */
const open = async (given, context = null) =>
{
    const page = await (context ?? await browser.newContext()).newPage();
    const fetched = [];
    const errors = [];

    page.on('request', (r) => fetched.push(r.url()));
    page.on('pageerror', (e) => errors.push(e.message));
    await page.addInitScript(fake, { granted: [], offered: [], configs, ...given });
    await page.goto(url);
    await page.click('#start');
    await page.waitForFunction(() => !document.getElementById('loadpiece').disabled,
                               null, { timeout: 60000 });
    return { page, fetched, errors, module: () => fetched.some((u) => u.endsWith('/thinkhid.js')) };
};
const statusIs = (page, text) => page.waitForFunction(
    (t) => document.getElementById('hidstatus').textContent.includes(t), text,
    { timeout: 10000 }).then(() => true, () => false);
const postedHas = (page, text) => page.waitForFunction(
    (t) => window.posted.includes(t), text, { timeout: 5000 }).then(() => true, () => false);

try
{
    {
        const { page, module } = await open({});

        await page.waitForTimeout(1500);
        check(!await page.isDisabled('#hid') && !module(),
              'with no controller granted the button is offered and the module never fetched');
        await page.context().close();
    }

    {
        const { page } = await open({ absent: true });

        check(await statusIs(page, 'Chrome or Edge') && await page.isDisabled('#hid'),
              'with no WebHID the button stays off and says why');
        await page.context().close();
    }

    {
        const { page, module, errors } = await open(
            { granted: [{ productId: 0x2102, serial: 'D-1', on: true }] });

        check(await statusIs(page, 'A Valve Index controller is playing') && module(),
              'a dongle granted before is taken up at start, and the module fetched');
        check(await page.evaluate(() => window.hidSent.includes('D-1 ff 87 3 0 1 0')),
              'and its controller switched to the messages read');

        await page.evaluate(() => window.dongles[0].send([0x20, 0xf1, 0x10]));
        check(await postedHas(page, 'midion 60'),
              'in a sequence, A pressed goes in as its input, as the keys\' do');
        await page.evaluate(() => window.dongles[0].send([0x20, 0xf1, 0x00]));
        await postedHas(page, 'midioff 60');

        await page.selectOption('#mode', 'patch');
        await page.evaluate(() => window.solo.settled());
        await page.evaluate(() => window.dongles[0].send([0x20, 0xf1, 0x10]));
        check(await postedHas(page, 'on 60'),
              'on a patch, A pressed plays its note through the page\'s keys');

        /* A fist, and a turn of the wrist: the IMU's gyro turning about
           the front, sample after sample. */
        await page.evaluate(() =>
        {
            const d = window.dongles[0];

            d.send([0x20, 0xf0, 0xa1, 0x00, 0xff, 0xff, 0xff, 0, 0, 0]);
            for (let i = 0; i < 60; i++)
                d.send([0x80, 0, 0x00, 0xf0, 0, 0, 0, 0, 0x00, 0x40, 0, 0, 0, 0]);
        });
        check(await postedHas(page, 'hid 3 0 60'),
              'a fist turned bends the note it holds, on the patch\'s channel');

        await page.evaluate(() => window.dongles[0].send([0x20, 0xf1, 0x00]));
        check(await postedHas(page, 'off 60'), 'and A let go of lets it go');

        await page.evaluate(() => window.dongles[0].send(
            [0x20, 0xf0, 0xa1, 0x00, 0, 0, 0, 0, 0xff, 0]));
        check(await postedHas(page, 'hid 2 0 level'),
              'a squeeze is a value for the patch\'s level, on the patch\'s channel');

        /* The controller turned off, held note and all: the dongle says so,
           and answers nothing until it is turned on again. */
        await page.evaluate(() =>
        {
            window.posted.length = 0;
            window.dongles[0].send([0x20, 0xf1, 0x10]);
        });
        await page.waitForFunction(() => window.posted.some((p) => p.startsWith('on ')));
        await page.evaluate(() =>
        {
            const d = window.dongles[0];

            window.posted.length = 0;
            d.on = false;
            d.raw(0x26, [1]);
        });
        check(await page.waitForFunction(
                  () => window.posted.some((p) => p.startsWith('off ')),
                  null, { timeout: 5000 }).then(() => true, () => false) &&
              await statusIs(page, 'No controller on the dongle'),
              'the dongle saying its controller has gone lets its notes go');
        await page.evaluate(() => { window.hidSent.length = 0; window.dongles[0].on = true; });
        check(await page.waitForFunction(() => window.hidSent.includes('D-1 ff 87 3 0 1 0'),
                                         null, { timeout: 5000 }).then(() => true, () => false) &&
              await statusIs(page, 'A Valve Index controller is playing'),
              'and turned on again, it is asked, switched and played again');
        check(errors.length === 0, `no page errors${errors.length ? ': ' + errors.join('; ') : ''}`);
        await page.context().close();
    }

    {
        const { page } = await open(
            { granted: [{ productId: 0x2102, serial: 'D-1', on: true, turnsDown: 1 }] });

        check(await statusIs(page, 'A Valve Index controller is playing'),
              'a dongle that turns the switch down once is asked again, and plays');
        await page.evaluate(() => window.dongles[0].send([0x20, 0xf1, 0x10]));
        await postedHas(page, 'midion 60');
        await page.evaluate(() => window.unplug(window.dongles[0]));
        check(await postedHas(page, 'midioff 60') && await statusIs(page, ''),
              'and unplugged with a note held, lets it go');
        await page.context().close();
    }

    {
        const { page } = await open({ granted: [{ productId: 0x2102, serial: 'D-1', on: false }] });

        check(await statusIs(page, 'No controller on the dongle'),
              'a dongle with no controller on it says so');
        await page.evaluate(() => { window.dongles[0].on = true; });
        check(await statusIs(page, 'A Valve Index controller is playing'),
              'and plays the controller once one is turned on');
        await page.context().close();
    }

    {
        const { page } = await open({ offered: ['light', 'input', 'imu'].map((part) =>
            ({ serial: 'D-1', part })) });

        await page.click('#hid');
        check(await statusIs(page, 'A Valve Index controller is playing') &&
              await page.evaluate(() => window.hidSent.includes('D-1 4 0 0 0 0') &&
                                        window.hidSent.includes('D-1 7 3 0 0 0')),
              'a wired controller picked is woken on its IMU\'s interface and plays');
        await page.evaluate(() => window.dongles.find((d) => d.part === 'input').press(true));
        check(await postedHas(page, 'midion 60'), 'its A, on its input\'s interface, plays');
        await page.context().close();
    }

    {
        /* Two wired, which nothing in WebHID tells apart: the second's
           interfaces are turned away, so its A plays nothing. */
        const { page } = await open({ offered: ['light', 'input', 'imu', 'imu', 'input',
                                                'light'].map((part, i) =>
            ({ serial: 'D-1', part, second: i > 2 })) });

        await page.click('#hid');

        /* The first playing, so that a note from the second would sound. */
        await page.evaluate(() => window.dongles.filter((d) => d.part === 'input')[0].press(true));
        await postedHas(page, 'midion 60');
        await page.evaluate(() =>
        {
            const first = window.dongles.filter((d) => d.part === 'input')[0];

            first.press(false);
        });
        await postedHas(page, 'midioff 60');
        await page.evaluate(() =>
        {
            window.posted.length = 0;
            window.dongles.filter((d) => d.part === 'input')[1].press(true);
        });
        await page.waitForTimeout(500);
        check(await page.evaluate(() => !window.posted.some((p) => p.startsWith('midion'))),
              'a second wired controller is turned away, its A playing nothing');
    }

    {
        /* The pane: its rows, live; the player's profile, kept; what is
           wrong with one, said. */
        const context = await browser.newContext();
        const { page } = await open(
            { granted: [{ productId: 0x2102, serial: 'D-1', on: true }] }, context);

        await statusIs(page, 'A Valve Index controller is playing');
        await page.click('#hidpane summary');
        await page.evaluate(() => window.dongles[0].send([0x20, 0xf1, 0x10]));
        check(await page.waitForFunction(() => [...document.querySelectorAll('#hidrows tr')]
                  .some((tr) => tr.cells[0].textContent === 'a' &&
                                tr.cells[1].textContent === 'down' &&
                                tr.cells[2].textContent.startsWith('note')),
                  null, { timeout: 5000 }).then(() => true, () => false),
              'the pane shows each binding, and A down as it is pressed');
        await page.evaluate(() => window.dongles[0].send([0x20, 0xf1, 0x00]));

        await page.fill('#hidprofile', 'device index {\n    a = note 5;\n};\n');
        await page.click('#hidreload');
        await page.evaluate(() => window.dongles[0].send([0x20, 0xf1, 0x10]));
        check(await postedHas(page, 'midion 65'), 'a profile of the player\'s plays');
        await page.close();

        const again = (await open(
            { granted: [{ productId: 0x2102, serial: 'D-1', on: true }] }, context)).page;

        await statusIs(again, 'A Valve Index controller is playing');
        await again.evaluate(() => window.dongles[0].send([0x20, 0xf1, 0x10]));
        check(await again.inputValue('#hidprofile') === 'device index {\n    a = note 5;\n};\n' &&
              await postedHas(again, 'midion 65'),
              'and is kept for the next page, which plays it');
        await again.click('#hidpane summary');
        await again.fill('#hidprofile', 'device index {\n    nonsense = note 0;\n};\n');
        await again.click('#hidreload');
        check(/your profile:2/.test(await again.textContent('#hidproblem')),
              'and one that is wrong says where');
        await context.close();
    }

    {
        const { page } = await open({ granted: [{ productId: 0x2102, serial: 'D-1', on: false }] });

        await page.click('#hidpane summary');
        await page.waitForSelector('#hidpair:not([hidden])');
        await page.click('#hidpair');
        check(await page.waitForFunction(() => window.hidSent.includes('D-1 ff ad 3 1 30 75'),
                                         null, { timeout: 5000 }).then(() => true, () => false) &&
              await statusIs(page, 'Pairing: turn the controller off'),
              'Pair a controller puts the dongle with none to listening, and says what to do');
        await page.evaluate(() => { window.dongles[0].on = true; });
        check(await statusIs(page, 'A Valve Index controller is playing'),
              'and the controller paired, plays, and the pairing is over');
        await page.context().close();
    }

    {
        /* Two that say no hand: given one each, and swapped. */
        const context = await browser.newContext();
        const { page } = await open({ granted: ['D-3', 'D-4'].map((serial) =>
            ({ productId: 0x2102, serial, on: true })) }, context);

        await statusIs(page, '2 Valve Index controllers are playing');
        await page.click('#hidpane summary');
        await page.waitForSelector('#hidswap:not([hidden])');

        const before = await page.textContent('#hidrows');

        await page.click('#hidswap');
        check(await page.waitForFunction((b) => document.getElementById('hidrows')
                  .textContent !== b, before, { timeout: 5000 }).then(() => true, () => false) &&
              /LHR-3=(left|right)/.test(await page.evaluate(
                  () => localStorage.getItem('thinksynth:hid:hands'))),
              'a pair that says no hands can be swapped, and the swap is kept');
        await context.close();
    }

    for (const order of [['D-1', 'D-2'], ['D-2', 'D-1']])
    {
        const { page } = await open({ offered: order.map((serial) =>
            ({ productId: 0x2102, serial, on: true })) });

        await page.click('#hid');
        check(await statusIs(page, '2 Valve Index controllers are playing'),
              `two dongles picked, ${order.join(' then ')}, both play`);
        await page.context().close();
    }
}
finally
{
    await browser.close();
    site.close();
}

process.stdout.write(`\n${failures} failure${failures === 1 ? '' : 's'}\n`);
process.exit(failures);
