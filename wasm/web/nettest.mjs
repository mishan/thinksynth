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
 * nettest.mjs -- a room played over a bad network, measured.
 *
 *   node wasm/web/nettest.mjs SCENARIO [options]
 *
 *   clock      pages play and move knobs: each page's clock offset against
 *              the relay's true clock, how early commands arrive, and how
 *              many land late
 *   mesh       the same with keys played from the first page: commands
 *              lost, notes left sounding, and when a pair falls back to
 *              the relay
 *   join       a page joins the room mid-run: does its tape come out the
 *              others', and how long it takes to catch up
 *   blackhole  the second page hears nothing for --hole seconds: does it
 *              rejoin, keep its seat and its edit, and end with one mesh
 *   throttle   the second page's CPU slowed --throttle times, and with
 *              --hidden its tab put behind another: do its clock and the
 *              lead hold
 *
 *   --pages N         pages in the room (2); `join' starts one more
 *   --rtt MS[,MS...]  each page's round trip to the relay (50)
 *   --jitter MS[,..]  each direction's delay varies by up to this, either
 *                     way (0)
 *   --loss PCT[,...]  each direction's packet loss (0)
 *   --loss-from S     the loss only from S seconds after Play, once the
 *                     mesh has opened on a clean link
 *   --up-share F[,..] the uplink's part of the round trip (0.5); not a
 *                     half is a path the clock cannot see the asymmetry of
 *   --seconds S       how long the room plays (30)
 *   --at S            when the join, blackhole or throttle comes, from
 *                     Play (seconds / 3)
 *   --hole S          the blackhole's length (10)
 *   --throttle R      the CPU slowdown (4)
 *   --hidden          throttle: the tab hidden too
 *   --piece NAME      the piece (airports.gen: a knob and a seat)
 *   --knob-ms MS      each page moves the knob this often (200); 0 never
 *   --note-ms MS      the first page plays a key this often (300 for
 *                     mesh, else 0)
 *   --hold-ms MS      and holds it this long (150)
 *   --mode M          how keys go: direct, quantised or ahead (direct)
 *   --mdns            leave Chromium's mDNS host candidates on
 *   --json FILE       everything measured, as JSON
 *   --build DIR       the site (build-web)
 *
 * A comma list gives page 1, 2, ... their own link; the last value goes for
 * the rest. One page's round trip to another is the two links end to end.
 *
 * Needs unprivileged user and network namespaces, and netem: the network is
 * scripts/nettest-ns.sh's, one namespace per page and the relay on the
 * bridge between them, and this script runs itself again inside it, under
 * scripts/headless.sh. Each page is a Chromium of its own in its page's
 * namespace, driven over a socket forwarded out of it, so nothing this
 * script asks crosses the impaired link. Real time throughout, which is
 * why this is a tool and not a ctest gate.
 *
 * The relay runs in the hub on CLOCK_MONOTONIC (relay.mjs, relayNow), and
 * so does every browser's performance.now() underneath its origin, so the
 * true offset of a page's clock to the relay's is measured here directly
 * and the page's own estimate held against it.
 */

/* Under scripts/headless.sh unless THINK_TEST_HEADLESS=0: see headless.mjs. */
import './headless.mjs';

import { spawn, spawnSync, execFileSync } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { chromium } from 'playwright';

import { tapeBefore } from '../tape.mjs';
import { KNOB_LEAD, TRANSPORT_LEAD } from './commands.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.join(here, '..', '..');
const self = fileURLToPath(import.meta.url);

function pipeTo (a, b)
{
    a.pipe(b).pipe(a);
    a.on('error', () => b.destroy());
    b.on('error', () => a.destroy());
}

function usage (why)
{
    process.stderr.write(`nettest.mjs: ${why}\n` +
        'usage: nettest.mjs clock|mesh|join|blackhole|throttle [--pages N] ' +
        '[--rtt MS] [--jitter MS] [--loss PCT] [--loss-from S] ' +
        '[--up-share F] [--seconds S] [--at S] [--hole S] [--throttle R] ' +
        '[--hidden] ' +
        '[--piece NAME] [--knob-ms MS] [--note-ms MS] [--hold-ms MS] ' +
        '[--mode M] [--mdns] [--json FILE] [--build DIR]\n');
    process.exit(2);
}

function parse (argv)
{
    const o = { scenario: argv[0], pages: 2, rtt: [50], jitter: [0],
                loss: [0], lossFrom: null, upShare: [0.5], seconds: 30,
                at: null, hole: 10, throttle: 4, hidden: false,
                piece: 'airports.gen',
                knobMs: 200, noteMs: null, holdMs: 150, mode: 'direct',
                mdns: false, json: null,
                build: path.join(top, 'build-web') };
    const list = (s) => s.split(',').map(Number);
    const flags = {
        '--pages': (v) => { o.pages = Number(v); },
        '--rtt': (v) => { o.rtt = list(v); },
        '--jitter': (v) => { o.jitter = list(v); },
        '--loss': (v) => { o.loss = list(v); },
        '--loss-from': (v) => { o.lossFrom = Number(v); },
        '--up-share': (v) => { o.upShare = list(v); },
        '--seconds': (v) => { o.seconds = Number(v); },
        '--at': (v) => { o.at = Number(v); },
        '--hole': (v) => { o.hole = Number(v); },
        '--throttle': (v) => { o.throttle = Number(v); },
        '--piece': (v) => { o.piece = v; },
        '--knob-ms': (v) => { o.knobMs = Number(v); },
        '--note-ms': (v) => { o.noteMs = Number(v); },
        '--hold-ms': (v) => { o.holdMs = Number(v); },
        '--mode': (v) => { o.mode = v; },
        '--json': (v) => { o.json = path.resolve(v); },
        '--build': (v) => { o.build = path.resolve(v); },
    };

    if (!['clock', 'mesh', 'join', 'blackhole', 'throttle']
        .includes(o.scenario))
        usage(`no scenario "${o.scenario ?? ''}"`);

    for (let i = 1; i < argv.length; i++)
    {
        if (argv[i] === '--hidden')
            o.hidden = true;
        else if (argv[i] === '--mdns')
            o.mdns = true;
        else if (flags[argv[i]] !== undefined && i + 1 < argv.length)
            flags[argv[i]](argv[++i]);
        else
            usage(`what is "${argv[i]}"?`);
    }

    o.at ??= o.seconds / 3;
    o.noteMs ??= o.scenario === 'mesh' ? 300 : 0;

    if (!(o.pages >= 2) || o.pages > 8)
        usage('--pages is 2 to 8');

    if (!['direct', 'quantised', 'ahead'].includes(o.mode))
        usage('--mode is direct, quantised or ahead');

    return o;
}

/* Each host's link, from the comma lists. */
function linkOf (o, i)
{
    const at = (a) => a[Math.min(i, a.length - 1)];
    const rttMs = at(o.rtt);
    const up = at(o.upShare);

    return { upMs: rttMs * up, downMs: rttMs * (1 - up),
             jitterMs: at(o.jitter), lossPct: at(o.loss) };
}

/* ---- the network ---- */

function netem (dev, pid, delayMs, jitterMs, lossPct)
{
    /* The default limit of a thousand packets is a quarter second of a
       busy link at 300 ms, and netem drops what does not fit. */
    const args = ['qdisc', 'replace', 'dev', dev, 'root', 'netem',
                  'limit', '100000', 'delay', `${delayMs}ms`];

    if (jitterMs > 0)
        args.push(`${jitterMs}ms`);

    if (lossPct > 0)
        args.push('loss', `${lossPct}%`);

    if (pid === null)
        execFileSync('tc', args);
    else
        execFileSync('nsenter', ['-t', String(pid), '-n', 'tc', ...args]);
}

/* Host `i''s link as `link' says, both ways: the hub's end of its veth is
   its downlink, its own end its uplink. */
function shape (pids, i, { upMs, downMs, jitterMs, lossPct })
{
    netem(`nt${i + 1}`, null, downMs, jitterMs, lossPct);
    netem('eth0', pids[i], upMs, jitterMs, lossPct);
}

function blackhole (pids, i)
{
    netem(`nt${i + 1}`, null, 0, 0, 100);
    netem('eth0', pids[i], 0, 0, 100);
}

/* Every page's site, each on its own loopback. */
const SITE_PORT = 8080;

const nowMs = () => Number(process.hrtime.bigint()) / 1e6;
const sleep = (ms) => new Promise((r) => setTimeout(r, Math.max(0, ms)));

async function until (test, ms, everyMs = 100)
{
    for (const end = nowMs() + ms; nowMs() < end; await sleep(everyMs))
        if (await test().catch(() => false))
            return true;

    return false;
}

/* ---- the browsers ---- */

/* A Chromium in host `i''s namespace, launched without Playwright's
 * switches and attached to with `noDefaults': Playwright's own launch
 * emulates focus on every page, which keeps a tab visible whatever is put
 * in front of it, and turns off the background throttling a hidden tab is
 * for. Its debugging port is in the host's namespace, and comes out to
 * the hub over a Unix socket.
 */
async function launch (env, i, o)
{
    const port = 9400 + i;
    const dir = path.join(env.tmp, `page${i}`);
    const sock = path.join(dir, 'cdp.sock');

    fs.mkdirSync(dir);

    const log = fs.openSync(path.join(dir, 'chrome.log'), 'w');
    const pid = String(env.pids[i]);
    const chrome = spawn('nsenter', [
        '-t', pid, '-n', '--', chromium.executablePath(),
        '--headless', '--no-sandbox', '--no-first-run',
        '--no-default-browser-check', `--user-data-dir=${dir}/profile`,
        `--remote-debugging-port=${port}`,
        '--autoplay-policy=no-user-gesture-required',
        /* The relay is on a private address and the page on loopback,
           which Chromium asks the person about; a real relay is
           public. */
        '--disable-features=LocalNetworkAccessChecks' +
            (o.mdns ? '' : ',WebRtcHideLocalIpsWithMdns'),
        'about:blank'], { stdio: ['ignore', log, log] });
    const forward = spawn('nsenter', ['-t', pid, '-n', '--', process.execPath,
                                      self, '--forward', sock, String(port)],
                          { stdio: 'ignore' });
    const server = net.createServer((c) => pipeTo(c, net.connect(sock)))
        .on('error', (e) => process.stdout.write(
            `nettest.mjs: p${i + 1}'s debugging port: ${e.message}\n`))
        .listen(port, '127.0.0.1');

    /* The site on the page's own loopback, which is a secure context
       without a certificate, and is not behind the page's link: what is
       under test is the room, not how fast a page loads. Playwright's
       route would do, but an AudioWorklet's module goes past it. */
    const site = spawn('nsenter', ['-t', pid, '-n', '--', process.execPath,
                                   path.join(here, 'serve.mjs'), o.build,
                                   '--port', String(SITE_PORT),
                                   '--relay', env.relayUrl],
                       { stdio: ['ignore', 'pipe', log] });

    env.procs.push(chrome, forward, site);
    env.servers.push(server);
    await new Promise((resolve) => site.stdout.once('data', resolve));

    let browser = null;

    await until(async () =>
    {
        browser = await chromium.connectOverCDP(`http://127.0.0.1:${port}`,
                                                { noDefaults: true,
                                                  timeout: 5000 });
        return true;
    }, 20000, 200);

    if (browser === null)
        throw new Error(`page ${i + 1}'s browser did not come up`);

    const context = browser.contexts()[0];
    const page = context.pages()[0] ?? await context.newPage();
    const errors = [];

    page.on('pageerror', (e) => errors.push(e.message));
    page.on('console', (m) =>
    {
        if (m.type() === 'error' && !/status of 404/.test(m.text()))
            errors.push(m.text());
    });

    return { i, label: `p${i + 1}`, browser, context, page, errors,
             truth: NaN, samples: [], margins: [], sent: [],
             paths: [], seen: new Set(), since: 0 };
}

/* The true offset of this page's performance.now() to the relay's clock:
   the narrowest of a few brackets of hrtime around a read of it. */
async function trueOffset (p)
{
    let best = null;

    for (let k = 0; k < 20; k++)
    {
        const h0 = nowMs();
        const perf = await p.page.evaluate(() => performance.now());
        const h1 = nowMs();

        if (best === null || h1 - h0 < best.widthMs)
            best = { widthMs: h1 - h0, offset: (h0 + h1) / 2 - perf };
    }

    p.truth = best.offset;
    p.truthWidthMs = best.widthMs;
}

async function enter (env, p, o)
{
    await p.page.goto(`${env.url}&room=nettest&name=${p.label}` +
                      `&piece=${o.piece}`);
    await p.page.waitForFunction(
        () => !document.getElementById('roompanel').hidden,
        null, { timeout: 60000 });
    await p.page.click('#start');
    await p.page.waitForFunction(() => window.jam.ready(), null,
                                 { timeout: 60000 })
        .catch(async () =>
        {
            throw new Error(`${p.label} never became ready: ` +
                            JSON.stringify(await p.page.evaluate(
                                () => window.jam.state())));
        });
    p.readyAt = nowMs();

    /* performance.now() starts again with each document. */
    await trueOffset(p);
}

/* What a page says now, for the clock series. */
async function sample (env, p)
{
    const s = await p.page.evaluate(() =>
    {
        const probe = window.jam.probe();
        const numbers = document.getElementById('numbers').textContent;
        const num = (re) => Number(re.exec(numbers)?.[1] ?? NaN);

        return { perf: probe.performanceNow, relay: probe.relayNow,
                 transport: probe.transportNow, running: probe.running,
                 rttMs: num(/relay round trip\s+([\d.]+) ms/),
                 spreadMs: num(/relay offset spread\s+([\d.]+) ms/),
                 overBudget: num(/quanta over budget\s+(\d+)/),
                 visibility: document.visibilityState };
    });

    s.t = (s.perf + p.truth - env.playAt) / 1000;
    s.errMs = s.relay - s.perf - p.truth;

    /* Where transport zero is on the true clock: the same on every page
       that is in time with the others. */
    if (s.running && s.transport >= 0)
        s.zeroMs = s.perf + p.truth - s.transport * 1000;

    p.samples.push(s);
}

/* A page is in its own list of peers, under its name. */
const isSelf = (p, q) => q.name.replace(/ \(guest\)$/, '') === p.label;

/* The bounded lists the page keeps (jam.js, KEEP), taken in full by
   asking often enough. */
async function collect (p)
{
    const got = await p.page.evaluate(() => ({
        margins: window.jam.margins(), sent: window.jam.sent(),
        peers: window.jam.peers(),
        log: document.getElementById('log').textContent,
        status: document.getElementById('status').textContent,
        seat: window.jam.seatNow() }));
    const at = nowMs();

    for (const m of got.margins)
        if (!p.seen.has(`m${m.from}:${m.seq}`))
        {
            p.seen.add(`m${m.from}:${m.seq}`);
            p.margins.push(m);
        }

    for (const c of got.sent)
        if (!p.seen.has(`s${c.from}:${c.seq}`))
        {
            p.seen.add(`s${c.from}:${c.seq}`);
            p.sent.push(c);
        }

    p.peersNow = got.peers;
    p.status = got.status;
    p.log = got.log;
    p.seat = got.seat;

    const shown = got.peers.filter((q) => !isSelf(p, q))
        .map((q) => `${q.name}:${q.path}`).sort().join(' ');

    if (p.paths.at(-1)?.shown !== shown)
        p.paths.push({ at, shown });
}

/* Gestures from inside the page, so a throttled page makes them as
   slowly as it would for a person. */
function gesture (p, { knobMs, noteMs, holdMs, notes })
{
    return p.page.evaluate(({ knobMs, noteMs, holdMs, notes }) =>
    {
        const timers = [];
        const knobbed = document.querySelector('#knobs .panelrow') !== null;

        if (knobMs > 0 && knobbed)
            timers.push(setInterval(
                () => window.jam.knob(0, Math.round(Math.random() * 1000) /
                                         1000), knobMs));

        if (noteMs > 0)
        {
            let k = 0;

            timers.push(setInterval(() =>
            {
                const n = notes[k++ % notes.length];

                window.jam.press(n, 90);
                setTimeout(() => window.jam.release(n), holdMs);
            }, noteMs));
        }

        window.nettestStop = () => timers.forEach(clearInterval);
    }, { knobMs, noteMs, holdMs, notes });
}

const NOTES = Array.from({ length: 60 }, (_, k) => 36 + k);

/* ---- the numbers ---- */

function quantile (xs, q)
{
    if (xs.length === 0)
        return NaN;

    const s = [...xs].sort((a, b) => a - b);

    return s[Math.min(s.length - 1, Math.floor(q * s.length))];
}

const r1 = (x) => Number.isFinite(x) ? Math.round(x * 10) / 10 : null;

function firstDifference (a, b)
{
    const x = a.split('\n'), y = b.split('\n');

    for (let k = 0; k < Math.max(x.length, y.length); k++)
        if (x[k] !== y[k])
            return `line ${k + 1}: "${x[k] ?? '(end)'}" v ` +
                   `"${y[k] ?? '(end)'}"`;

    return null;
}

const KIND = (c) =>
    c.type === 'note' || c.type === 'noteoff' ? `${c.mode ?? 'direct'}-key`
        : c.type === 'transport' ? 'transport' : 'knob';

/* What one page received of the others' commands. */
function received (p, pages)
{
    const own = new Set(p.sent.map((c) => c.from));
    const byTag = new Map(p.margins.map((m) => [`${m.from}:${m.seq}`, m]));
    const out = { kinds: {}, lost: 0, sent: 0, stuck: 0, reordered: 0 };

    for (const q of pages)
    {
        if (q === p)
            continue;

        /* What q stamped while the room played: what p had to apply. */
        for (const c of q.sent.filter((c) => c.at >= p.since &&
                                             !own.has(c.from)))
        {
            const k = KIND(c);
            const m = byTag.get(`${c.from}:${c.seq}`);
            const kind = out.kinds[k] ??= { sent: 0, lost: 0, margins: [] };

            kind.sent++;
            out.sent++;

            if (m === undefined)
            {
                kind.lost++;
                out.lost++;
            }
            else
                kind.margins.push(m.margin * 1000);
        }

        /* Keys: each press and the release that is its pair. A press
           whose release never came sounds until the next release of its
           note; so does a direct one whose release overtook it, since a
           direct key is played on arrival. */
        const keys = q.sent.filter((c) => c.at >= 0 &&
                                          (c.type === 'note' ||
                                           c.type === 'noteoff'));
        const order = new Map(p.margins.map((m, k) => [`${m.from}:${m.seq}`,
                                                       k]));

        for (let k = 0; k < keys.length; k++)
        {
            const on = keys[k];

            if (on.type !== 'note')
                continue;

            const off = keys.slice(k + 1).find((c) => c.type === 'noteoff' &&
                                                      c.note === on.note);
            const at = order.get(`${on.from}:${on.seq}`);
            const offAt = off === undefined
                ? undefined : order.get(`${off.from}:${off.seq}`);

            if (at === undefined)
                continue;

            if (offAt === undefined)
                out.stuck++;
            else if (offAt < at && (on.mode ?? 'direct') === 'direct')
            {
                out.stuck++;
                out.reordered++;
            }
        }
    }

    for (const kind of Object.values(out.kinds))
    {
        kind.leastMs = r1(Math.min(...kind.margins));
        kind.p1Ms = r1(quantile(kind.margins, 0.01));
        kind.medianMs = r1(quantile(kind.margins, 0.5));
        kind.lateCount = kind.margins.filter((m) => m < 0).length;
        delete kind.margins;
    }

    return out;
}

function clockOf (p, from = 0)
{
    const s = p.samples.filter((x) => x.t >= from && Number.isFinite(x.errMs));
    const abs = s.map((x) => Math.abs(x.errMs));

    return { samples: s.length, worstMs: r1(Math.max(...abs)),
             p95Ms: r1(quantile(abs, 0.95)), medianMs: r1(quantile(abs, 0.5)),
             lastMs: r1(s.at(-1)?.errMs), rttMs: r1(s.at(-1)?.rttMs),
             spreadMs: r1(s.at(-1)?.spreadMs),
             overBudget: s.at(-1)?.overBudget ?? null,
             visibility: [...new Set(s.map((x) => x.visibility))] };
}

/* How far apart the pages' transport zeros are on the true clock, from
   samples taken in the same round. */
function skewOf (pages, rounds)
{
    const spreads = [];

    for (let k = 0; k < rounds; k++)
    {
        const zs = pages.map((p) => p.samples.find((s) => s.round === k))
            .filter((s) => s?.zeroMs !== undefined).map((s) => s.zeroMs);

        if (zs.length === pages.length && pages.length > 1)
            spreads.push(Math.max(...zs) - Math.min(...zs));
    }

    return { worstMs: r1(Math.max(...spreads)),
             p95Ms: r1(quantile(spreads, 0.95)),
             medianMs: r1(quantile(spreads, 0.5)) };
}

/* ---- the run ---- */

async function main ()
{
    const o = parse(process.argv.slice(2));
    const hosts = o.pages + (o.scenario === 'join' ? 1 : 0);

    if (process.env.NETTEST_PIDS === undefined)
    {
        const run = spawnSync(path.join(top, 'scripts', 'nettest-ns.sh'),
                              [String(hosts), process.execPath, self,
                               ...process.argv.slice(2)],
                              { stdio: 'inherit' });

        process.exit(run.status ?? 1);
    }

    if (!fs.existsSync(path.join(o.build, 'jam.js')))
    {
        process.stderr.write(`nettest.mjs: no room page in ${o.build}\n`);
        process.exit(1);
    }

    const env = { pids: process.env.NETTEST_PIDS.split(' ').map(Number),
                  hub: process.env.NETTEST_HUB, procs: [], servers: [],
                  tmp: fs.mkdtempSync(path.join(os.tmpdir(), 'nettest-')),
                  playAt: 0 };
    env.relayUrl = `ws://${env.hub}:8787`;
    const relayLog = fs.openSync(path.join(env.tmp, 'relay.log'), 'w');
    const relay = spawn(process.execPath,
                        [path.join(here, 'relay.mjs'), '--port', '8787',
                         '--host', env.hub, '--tree', top],
                        { stdio: ['ignore', relayLog, relayLog] });

    env.procs.push(relay);

    env.url = `http://127.0.0.1:${SITE_PORT}/jam.html?panes=0`;

    const links = Array.from({ length: hosts }, (_, i) => linkOf(o, i));
    const pages = [];
    const result = { options: o, links, knobLeadMs: KNOB_LEAD * 1000,
                     transportLeadMs: TRANSPORT_LEAD * 1000, events: [] };
    const event = (what) =>
    {
        const t = r1((nowMs() - env.playAt) / 1000);

        result.events.push({ t, what });
        process.stdout.write(`  ${String(t).padStart(6)} s  ${what}\n`);
    };

    let failed = null;

    try
    {
        if (!await until(() => new Promise((resolve) =>
        {
            const s = net.connect(8787, env.hub, () =>
            {
                s.end();
                resolve(true);
            });

            s.on('error', () => resolve(false));
        }), 10000))
            throw new Error('the relay did not come up');

        /* Shaped before the pages load: the clock keeps its best sample
           of the last sixteen, and one taken on a clean link would be
           believed for that long. */
        links.forEach((l, i) => shape(env.pids, i, o.lossFrom === null
                                          ? l : { ...l, lossPct: 0 }));

        const all = [];

        for (let i = 0; i < hosts; i++)
            all.push(await launch(env, i, o));

        pages.push(...all.slice(0, o.pages));
        await Promise.all(pages.map((p) => enter(env, p, o)));

        /* The mesh gets the ten seconds it gives itself (mesh.js,
           OPEN_WITHIN) and a little more. */
        await until(() => Promise.all(pages.map((p) => p.page.evaluate(
            () => window.jam.peers().every((q) => q.path !== 'connecting'))))
            .then((ok) => ok.every(Boolean)), 15000, 250);

        for (const p of pages)
            await collect(p);

        result.meshAtStart = pages.map(
            (p) => ({ page: p.label, peers: p.paths.at(-1)?.shown }));

        const [A] = pages;

        if (o.noteMs > 0 || o.scenario === 'blackhole')
        {
            const seated = o.scenario === 'blackhole' ? pages[1] : A;
            const seat = await seated.page.evaluate(
                () => window.jam.instruments()[0]?.channel);

            await seated.page.evaluate((s) => window.jam.seat(s), seat);
            await until(() => seated.page.evaluate(
                (s) => window.jam.seatNow() === s, seat), 10000);
            await seated.page.evaluate((m) => window.jam.mode(m), o.mode);
            result.seat = seat;
        }

        await A.page.evaluate(() => window.jam.play());
        env.playAt = nowMs() + TRANSPORT_LEAD * 1000;
        await until(() => Promise.all(pages.map((p) => p.page.evaluate(
            () => window.jam.probe().running))).then((r) => r.every(Boolean)),
                    10000);
        event(`playing; mesh ${result.meshAtStart.map((m) =>
            `${m.page} -> ${m.peers}`).join(', ')}`);

        for (const [k, p] of pages.entries())
            await gesture(p, { knobMs: o.knobMs,
                               noteMs: k === 0 ? o.noteMs : 0,
                               holdMs: o.holdMs, notes: NOTES });

        /* The run: samples twice a second, the lists once, a tempo from
           a page in turn every six seconds, and the scenario's own
           business at --at. */
        let round = 0;
        let nextCollect = 0;
        let nextTempo = 6;
        const end = env.playAt + o.seconds * 1000;
        const scenario = runScenario(env, o, pages, all, links, result, event);

        while (nowMs() < end)
        {
            const t = (nowMs() - env.playAt) / 1000;
            const k = round++;

            await Promise.all(pages.map((p) => sample(env, p)
                .then(() => { p.samples.at(-1).round = k; })
                .catch(() => {})));

            if (t >= nextCollect)
            {
                await Promise.all(pages.map((p) => collect(p)
                    .catch(() => {})));
                nextCollect = t + 1;
            }

            if (t >= nextTempo)
            {
                const p = pages[Math.floor(nextTempo / 6) % pages.length];

                await p.page.evaluate((bpm) => window.jam.tempo(bpm),
                                      nextTempo % 12 === 0 ? 100 : 112)
                    .catch(() => {});
                nextTempo += 6;
            }

            if (o.lossFrom !== null && t >= o.lossFrom && !result.lossFromS)
            {
                links.forEach((l, i) => shape(env.pids, i, l));
                result.lossFromS = r1(t);
                event(`loss on: ${o.loss.join(',')}%`);
            }

            scenario.tick?.(t);
            await sleep(500 - (nowMs() - env.playAt) % 500);
        }

        result.rounds = round;
        await scenario.done;

        for (const p of pages)
            await p.page.evaluate(() => window.nettestStop?.())
                .catch(() => {});

        await sleep(o.holdMs + 1000);
        await A.page.evaluate(() => window.jam.stop());
        await sleep(3000 + 2 * Math.max(...o.rtt));

        for (const p of pages)
        {
            await collect(p);
            Object.assign(p, await p.page.evaluate(() => ({
                tape: window.jam.tape(), late: window.jam.late(),
                heard: window.jam.heard(),
                numbers: document.getElementById('numbers').textContent })));
        }

        report(o, pages, result, env.playAt);
    }
    catch (e)
    {
        failed = e;
        process.stdout.write(`nettest.mjs: ${e.stack}\n`);
    }
    finally
    {
        for (const p of pages)
            if (p.errors.length > 0)
                process.stdout.write(`  ${p.label} said: ` +
                                     `${p.errors.slice(0, 5).join(' | ')}\n`);

        for (const s of env.servers)
        {
            s.closeAllConnections?.();
            s.close();
        }

        for (const c of env.procs)
            c.kill();

        if (o.json !== null)
            fs.writeFileSync(o.json, JSON.stringify(result, null, 2) + '\n');

        fs.rmSync(env.tmp, { recursive: true, force: true });
    }

    process.exit(failed === null ? 0 : 1);
}

/* What each scenario does to the room while it plays: a `tick' called
   twice a second with the time from Play, and `done', which the run waits
   for before it stops. */
function runScenario (env, o, pages, all, links, result, event)
{
    if (o.scenario === 'join')
        return joinLate(env, o, pages, all, result, event);

    if (o.scenario === 'blackhole')
        return holeIn(env, o, pages, links, result, event);

    if (o.scenario === 'throttle')
        return slowDown(o, pages, result, event);

    return { done: Promise.resolve() };
}

/* The last host's page joins at --at, presses Start, and catches up. */
function joinLate (env, o, pages, all, result, event)
{
    const C = all[o.pages];
    let started = null;

    return {
        tick: (t) =>
        {
            if (t < o.at || started !== null)
                return;

            started = (async () =>
            {
                event(`${C.label} joins`);

                const t0 = nowMs();

                await enter(env, C, o);
                event(`${C.label} ready ${r1((C.readyAt - t0) / 1000)} s ` +
                      'after it arrived');

                const caught = await until(() => C.page.evaluate(
                    () => !window.jam.catching() &&
                          window.jam.probe().running), 60000);
                const t1 = nowMs();

                C.since = await pages[0].page.evaluate(
                    () => window.jam.transportNow());
                result.join = { readyS: r1((C.readyAt - t0) / 1000),
                                caughtUpS: caught ? r1((t1 - t0) / 1000)
                                                  : null,
                                quanta: await C.page.evaluate(
                                    () => window.jam.catchQuanta()) };
                event(caught ? `${C.label} caught up ` +
                               `${r1((t1 - t0) / 1000)} s after it arrived`
                             : `${C.label} never caught up`);
                pages.push(C);
                await gesture(C, { knobMs: o.knobMs, noteMs: 0,
                                   holdMs: o.holdMs, notes: NOTES });
            })().catch((e) => event(`the join failed: ${e.message}`));
        },
        get done ()
        {
            return started ?? Promise.resolve();
        },
    };
}

/* The second page cut off for --hole seconds; an edit typed into its
 * document meanwhile, which nobody else can see yet; then the link back,
 * and what came of it.
 */
function holeIn (env, o, pages, links, result, event)
{
    const B = pages[1];
    const others = pages.filter((p) => p !== B);
    const mark = `# typed during the blackhole ${Date.now()}`;
    let state = 'before';
    let restoredAt = 0;
    let done = null;
    const out = result.blackhole = { mark };

    const watch = async () =>
    {
        const seatBefore = out.seatBefore;

        /* Back in, as the status says, or still on the room socket it
           had: the relay lets a socket go only at its heartbeat
           (relay.mjs, HEARTBEAT_MS), and the page notices nothing until
           the relay's reset reaches it. Every page collected meanwhile,
           or what each keeps runs past what was taken. */
        const back = await until(async () =>
        {
            await Promise.all(pages.map(collect));
            return /Back in/.test(B.status);
        }, 45000, 250);

        out.idAfter = B.sent.at(-1)?.from;
        out.socketKept = out.idAfter === out.idBefore;
        out.rejoinS = back ? r1((nowMs() - restoredAt) / 1000) : null;
        event(back ? `${B.label} rejoined ${out.rejoinS} s after the link ` +
                     'came back'
                   : out.socketKept
                   ? `${B.label} kept its room socket; no rejoin`
                   : `${B.label} has a new peer id but never said it was ` +
                     `back (status "${B.status.trim()}")`);

        const mesh = await until(async () =>
        {
            await Promise.all(pages.map(collect));
            return pages.every((p) => p.peersNow.every(
                (q) => q.path !== 'connecting'));
        }, 20000, 250);

        await Promise.all(pages.map(collect));
        out.meshSettled = mesh;
        out.seatAfter = B.seat;
        out.seatKept = seatBefore !== null && B.seat === seatBefore;

        /* One of everybody, both ways: no ghost of B's old socket in
           anyone's list, and B's list one peer per other page. */
        out.views = pages.map((p) => ({
            page: p.label,
            peers: p.peersNow.filter((q) => !isSelf(p, q))
                .map((q) => `${q.name}:${q.path}`) }));
        out.oneMesh = out.views.every((v) =>
            v.peers.length === pages.length - 1 &&
            new Set(v.peers.map((s) => s.split(':')[0])).size ===
                pages.length - 1);

        const kept = await until(() => Promise.all(others.map((p) =>
            p.page.evaluate((m) => window.jam.file(window.jam.piece())
                .includes(m), mark))).then((r) => r.every(Boolean)),
                                 30000, 500);

        out.editKept = kept;
        event(`seat ${out.seatKept ? 'kept'
                                   : `${seatBefore} -> ${B.seat}`}; ` +
              `${out.oneMesh ? 'one mesh' : 'not one mesh'} ` +
              `(${out.views.map((v) => `${v.page}: ${v.peers.join(' ')}`)
                  .join('; ')}); the edit ` +
              `${kept ? 'reached' : 'did not reach'} the others`);
    };

    return {
        tick: (t) =>
        {
            if (state === 'before' && t >= o.at)
            {
                state = 'holed';
                collect(B).catch(() => { B.failedPolls++; }).then(() =>
                {
                    out.seatBefore = B.seat;
                    out.idBefore = B.sent.at(-1)?.from;
                    blackhole(env.pids, B.i);
                    event(`${B.label} blackholed for ${o.hole} s`);
                    B.page.evaluate((m) =>
                    {
                        const name = window.jam.piece();

                        window.jam.setFile(name, `${m}\n` +
                                                 window.jam.file(name));
                    }, mark).catch((e) => event(`no edit: ${e.message}`));
                });
            }
            else if (state === 'holed' && t >= o.at + o.hole)
            {
                state = 'after';
                shape(env.pids, B.i, links[B.i]);
                restoredAt = nowMs();
                event(`${B.label}'s link back`);
                done = watch().catch((e) => event(`watching: ${e.message}`));
            }
        },
        get done ()
        {
            return done ?? Promise.resolve();
        },
    };
}

/* The second page's CPU slowed at --at, and with --hidden a tab opened
   over it, which is what hides a tab. */
function slowDown (o, pages, result, event)
{
    const B = pages[1];
    let started = null;

    return {
        tick: (t) =>
        {
            if (t < o.at || started !== null)
                return;

            started = (async () =>
            {
                const cdp = await B.context.newCDPSession(B.page);

                await cdp.send('Emulation.setCPUThrottlingRate',
                               { rate: o.throttle });

                if (o.hidden)
                {
                    const browser = await B.browser.newBrowserCDPSession();

                    await browser.send('Target.createTarget',
                                       { url: 'about:blank',
                                         newWindow: false });
                }

                result.throttle = { fromS: r1(t), rate: o.throttle,
                                    hidden: o.hidden };
                event(`${B.label} slowed ${o.throttle}x` +
                      (o.hidden ? ' and hidden' : ''));
            })().catch((e) => event(`no throttle: ${e.message}`));
        },
        get done ()
        {
            return started ?? Promise.resolve();
        },
    };
}

/* ---- the report ---- */

function report (o, pages, result, playAt)
{
    const [A] = pages;
    const stopAt = A.sent.find((c) => c.type === 'transport' &&
                                      c.op === 'stop')?.at;
    const want = stopAt === undefined ? '' : tapeBefore(A.tape, stopAt);
    const after = result.throttle?.fromS ?? o.at;

    result.pages = pages.map((p) =>
    {
        const tape = stopAt === undefined ? '' : tapeBefore(p.tape, stopAt);

        return {
            page: p.label,
            /* The smaller of two ids offers (mesh.js). */
            id: p.sent.at(-1)?.from,
            link: result.links[p.i],
            truthWidthMs: r1(p.truthWidthMs),
            clock: clockOf(p, 0),
            clockAfter: o.scenario === 'throttle' ? clockOf(p, after)
                                                  : undefined,
            received: received(p, pages),
            late: { worklet: p.late.worklet, page: p.late.seen },
            heldAtEnd: p.heard,
            tapeEvents: tape.split('\n').length - 1,
            tapeSame: tape === want,
            tapeDiff: tape === want ? null : firstDifference(want, tape),
            paths: p.paths.map((x) => ({ t: r1((x.at - playAt) / 1000),
                                         shown: x.shown })),
            fallbacks: [...p.log.matchAll(
                /^(.*): through the relay \((.*)\)$/gm)]
                .map((m) => `${m[1]}: ${m[2]}`),
        };
    });
    result.skew = skewOf(pages.slice(0, o.pages), result.rounds);

    /* The lead each kind of command would have needed: its own less the
       least margin any page saw it arrive with. */
    const need = (kind, lead) =>
    {
        const least = Math.min(...result.pages.map(
            (p) => p.received.kinds[kind]?.leastMs ?? Infinity));

        return Number.isFinite(least) ? r1(lead * 1000 - least) : null;
    };

    result.leadNeededMs = { knob: need('knob', KNOB_LEAD),
                            transport: need('transport', TRANSPORT_LEAD) };

    const w = (s) => process.stdout.write(s + '\n');
    const kinds = (r) => Object.entries(r.kinds).map(([k, v]) =>
        `${k} ${v.sent - v.lost}/${v.sent}` +
        (k.startsWith('direct')
             ? ` delay median ${r1(-v.medianMs)} worst ${r1(-v.leastMs)} ms`
             : ` margin least ${v.leastMs} p1 ${v.p1Ms} ms, ${v.lateCount} ` +
               'late')).join('; ');

    w('');
    w(`${o.scenario}: ${o.pages} pages, rtt ${o.rtt.join(',')} ms, ` +
      `jitter ±${o.jitter.join(',')} ms, loss ${o.loss.join(',')}%, ` +
      `up share ${o.upShare.join(',')}, ${o.seconds} s`);

    for (const p of result.pages)
    {
        w(`  ${p.page}: clock error worst ${p.clock.worstMs} p95 ` +
          `${p.clock.p95Ms} median ${p.clock.medianMs} ms ` +
          `(rtt ${p.clock.rttMs}, spread ${p.clock.spreadMs}, truth ±` +
          `${r1(p.truthWidthMs / 2)} ms)` +
          (p.clockAfter ? `; after ${after} s worst ${p.clockAfter.worstMs} ` +
                          `p95 ${p.clockAfter.p95Ms} ms, ` +
                          `${p.clockAfter.visibility.join('/')}` : ''));
        w(`      got ${kinds(p.received)}`);
        w(`      late ${p.late.worklet} by the worklet, ${p.late.page} by ` +
          `the page; stuck keys ${p.received.stuck}` +
          (p.received.reordered
               ? ` (${p.received.reordered} overtaken)` : '') +
          `, held at the end [${p.heldAtEnd.join(' ')}]`);
        w(`      tape ${p.tapeSame ? 'same' : `DIFFERS (${p.tapeDiff})`}, ` +
          `${p.tapeEvents} events; paths ` +
          p.paths.map((x) => `${x.t.toFixed(1)}s ${x.shown}`).join(' | ') +
          (p.fallbacks.length > 0 ? `; fell back: ${p.fallbacks.join('; ')}`
                                  : ''));
    }

    w(`  transport skew between pages: worst ${result.skew.worstMs} ` +
      `p95 ${result.skew.p95Ms} median ${result.skew.medianMs} ms`);
    w(`  lead needed: knob ${result.leadNeededMs.knob} ms of ` +
      `${KNOB_LEAD * 1000}, transport ${result.leadNeededMs.transport} ms ` +
      `of ${TRANSPORT_LEAD * 1000}`);
}

/* The forwarder this script is in a page's namespace: a Unix socket, which
   every namespace sees, onto the browser's debugging port, which only its
   own does. */
if (process.argv[2] === '--forward')
{
    const [sock, port] = process.argv.slice(3);

    net.createServer((c) => pipeTo(c, net.connect(Number(port), '127.0.0.1')))
        .listen(sock);
}
else
    await main();
