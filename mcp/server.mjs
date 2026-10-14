#!/usr/bin/env node
// thinksynth as MCP tools: catalogs, pieces, instruments and measurement,
// for a client that writes music it cannot hear. README.md says how to run
// it; every answer comes from the tree's own harnesses
// (lib/engine.mjs), and lib/analyze.mjs turns renders into numbers.

import { mkdtemp, readdir } from 'node:fs/promises';
import path from 'node:path';

import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { z } from 'zod';

import { Call, PLUGINS, REPO, inside, json, onEvict, source } from './lib/engine.mjs';
import { readTape } from './lib/tape.mjs';
import { readWav } from './lib/wav.mjs';
import { collapse, describe, findings, measure, wholeLoudness, windows }
    from './lib/analyze.mjs';

const server = new McpServer({ name: 'thinksynth', version: '0.1.0' });

const reply = (value) => ({ content: [{ type: 'text', text: JSON.stringify(value, null, 1) }] });
const failed = (message) => ({ isError: true, content: [{ type: 'text', text: message }] });

function tool (name, description, shape, fn)
{
    server.registerTool(name, { description, inputSchema: shape }, async (args, extra) =>
    {
        const call = new Call(extra.signal);

        try
        {
            return reply(await fn(args, call));
        }
        catch (e)
        {
            return failed(e.message);
        }
        finally
        {
            await call.end();
        }
    });
}

// The longest a render may be, in seconds, whoever asks for it.
const MAX_SECONDS = 1200;

const pieceArgs = {
    text: z.string().optional().describe('the .gen text'),
    path: z.string().optional().describe('or a .gen file, relative to the tree'),
};
const dspArgs = {
    text: z.string().optional().describe('the .dsp text'),
    path: z.string().optional().describe('or a .dsp file, e.g. dsp/taiko.dsp'),
};

// Renders keep their directory, so a later call can analyze one by id.
const renders = new Map();

onEvict(dir => renders.delete(path.basename(dir)));

// ---- catalogs ---------------------------------------------------------------

tool('catalog_instruments',
     'The instruments (.dsp files) the tree ships: file, name, description, category. ' +
     'With `controls`, each match also lists its chanargs -- label, range, default, units -- ' +
     'which are what a piece\'s instrument block may set.',
     { query: z.string().optional().describe('case-insensitive match on file, name, description or category'),
       controls: z.boolean().optional().describe('include chanargs; keep the query narrow') },
     async ({ query, controls }, call) =>
     {
         const cat = json(await call.run('dspcatalog', ['--json']), 'dspcatalog');
         const q = query?.toLowerCase();
         const out = [];

         for (const g of cat.groups)
             for (const e of g.entries)
                 if (!q || [e.file, e.name, e.desc, e.category].join(' ').toLowerCase().includes(q))
                     out.push({ file: path.basename(e.file), path: e.file, name: e.name,
                                description: e.desc, category: e.category, effect: e.effect });

         if (controls)
         {
             if (out.length > 12)
                 throw new Error(`${out.length} instruments match; narrow the query to 12 or fewer for controls`);

             for (const e of out)
                 e.controls = await controlsOf(call, path.join(REPO, e.path));
         }

         return out;
     });

async function controlsOf (call, file)
{
    const r = await call.run('panelcheck', ['-p', PLUGINS, '--describe', file]);
    const { panel, folded } = json(r, 'panelcheck --describe');

    return panel.rows.filter(row => row.id !== 'amp')
                     .map(row => ({ name: row.id, label: row.label, units: row.units,
                                    folded: folded.includes(row.id),
                                    default: row.value, min: row.lo, max: row.hi,
                                    group: row.group || undefined }));
}

tool('catalog_stages',
     'Every composer plugin -- generators (gen::) and transformers (xform::) -- with each param\'s ' +
     'type, range, default and units. A duration param takes a unit in a piece: s, ms or beats.',
     { kind: z.enum(['gen', 'xform', 'both']).optional(),
       name: z.string().optional() },
     async ({ kind, name }, call) =>
     {
         const all = json(await call.run('genwav', ['--stages']), 'genwav --stages');

         return all.filter(s => (!kind || s.kind === kind || s.kind === 'both') &&
                                (!name || s.name === name));
     });

tool('catalog_nodes',
     'The DSP nodes an instrument graph is built from, with every arg\'s direction, units, range ' +
     'and default -- what docs/NODES.md shows, as data.',
     { query: z.string().optional().describe('match on the node name, e.g. "filt::" or "svf"') },
     async ({ query }, call) =>
     {
         const nodes = json(await call.run('dspnodes', ['-p', PLUGINS, '--json']), 'dspnodes');

         return query ? nodes.filter(n => n.node.includes(query)) : nodes;
     });

tool('catalog_pieces',
     'The pieces the tree ships, by category: worked examples of every language feature.',
     {},
     async (_, call) => json(await call.run('gencheck', ['--json', 'gen']), 'gencheck --json'));

// ---- pieces -----------------------------------------------------------------

async function check (call, file)
{
    return json(await call.run('genwav', ['--check', '--json', '-', file]), 'genwav --check');
}

tool('piece_check',
     'Load a piece without rendering it: errors and warnings with line numbers, and what it ' +
     'declares -- knobs, chains, instruments, sections.',
     pieceArgs,
     async (args, call) =>
     {
         const dir = await call.workdir('check');

         return check(call, await source(args, dir, 'piece.gen'));
     });

const moveArgs = z.array(z.object({
    at: z.number().min(0).describe('seconds into the piece'),
    knob: z.string(),
    value: z.number(),
})).optional();

function knobArgs (knobs = {}, timeline = [])
{
    const out = [];

    for (const [name, value] of Object.entries(knobs))
        out.push('--knob-at', `0:${name}=${value}`);

    for (const m of timeline)
        out.push('--knob-at', `${m.at}:${m.knob}=${m.value}`);

    return out;
}

tool('piece_render',
     'Render a piece to a WAV. Returns loudness (LUFS) and peak per instrument and for the mix, ' +
     'RMS per section, the sections as they actually played, and a render id for audio_analyze. ' +
     '`timeline` moves knobs while it plays, the way a game drives a piece; `profile` adds what ' +
     'each instrument costs to render against real time.',
     { ...pieceArgs,
       seconds: z.number().positive().max(MAX_SECONDS).default(60),
       knobs: z.record(z.string(), z.number()).optional().describe('knob values from the start'),
       timeline: moveArgs,
       profile: z.boolean().optional(),
       from: z.number().min(0).optional().describe('write audio from here (seconds)'),
       length: z.number().positive().optional().describe('and this long: a loop is the second pass') },
     async (args, call) =>
     {
         const dir = await call.workdir('render');
         const file = await source(args, dir, 'piece.gen');
         const wav = path.join(dir, 'mix.wav');
         const stems = path.join(dir, 'stems');
         const cli = ['-q', '-s', String(args.seconds), '--json', '-', '-o', wav,
                      '--stems', stems, ...knobArgs(args.knobs, args.timeline)];

         if (args.profile)
             cli.push('--profile');

         if (args.from !== undefined)
             cli.push('--from', String(args.from));

         if (args.length !== undefined)
             cli.push('--length', String(args.length));

         const result = await call.run('genwav', [...cli, file]);
         const summary = json(result, 'genwav');

         if (!summary.ok)
             return summary;

         const id = path.basename(dir);

         renders.set(id, { dir, wav, stems, summary });
         call.keep(dir);

         return { render: id, wav, clippedSamples: summary.clipped, ...summary };
     });

tool('piece_tape',
     'What a piece played, event by event: notes by name with bar, beat, velocity and length in ' +
     'beats, on the instruments the piece named; chanarg moves too. Filter by instrument and bars.',
     { ...pieceArgs,
       seconds: z.number().positive().max(600).default(30),
       instruments: z.array(z.string()).optional(),
       fromBar: z.number().int().min(1).optional(),
       toBar: z.number().int().min(1).optional(),
       limit: z.number().int().positive().max(5000).default(400),
       timeline: moveArgs },
     async (args, call) =>
     {
         const dir = await call.workdir('tape');
         const file = await source(args, dir, 'piece.gen');
         const piece = await check(call, file);

         if (!piece.ok)
             return piece;

         const r = await call.run('genwav', ['-q', '-s', String(args.seconds), '-t', '-',
                                             ...knobArgs({}, args.timeline), file]);

         if (r.code === 4)
             throw new Error('a voice went non-finite and was dropped, so this is not the piece\'s ' +
                             'tape; piece_render says on which instrument');

         if (r.code !== 0 && r.code !== 3)
             throw new Error(r.stderr.slice(0, 2000) || `genwav exited ${r.code}`);

         let events = readTape(r.stdout, piece);

         if (args.instruments)
             events = events.filter(e => args.instruments.includes(e.instrument));

         events = events.filter(e => (!args.fromBar || e.bar >= args.fromBar) &&
                                     (!args.toBar || e.bar <= args.toBar));

         return { tempo: piece.tempo, meter: piece.meter, total: events.length,
                  truncated: events.length > args.limit, events: events.slice(0, args.limit) };
     });

tool('piece_compare',
     'Render a piece twice with one knob at two values, on the same seed, and say how much each ' +
     'instrument changed (RMS of the difference). Zero is a knob that does nothing to it.',
     { ...pieceArgs,
       knob: z.string(), a: z.number(), b: z.number(),
       seconds: z.number().positive().max(600).default(30) },
     async (args, call) =>
     {
         const dir = await call.workdir('compare');
         const file = await source(args, dir, 'piece.gen');
         const s = json(await call.run('genwav', ['-q', '-s', String(args.seconds), '--json', '-',
                                                  '--knob', `${args.knob}=${args.a},${args.b}`, file]),
                        'genwav --knob');

         if (!s.ok)
             return s;

         return { knob: args.knob, a: args.a, b: args.b, mixDiffRms: s.mix.diffRms,
                  channels: s.channels.map(c => ({ instrument: c.instrument, rms: c.rms,
                                                   diffRms: c.diffRms })) };
     });

// ---- listening, in numbers ----------------------------------------------------

tool('audio_analyze',
     'Measure a render the way a listener would describe it: per window (each section as it ' +
     'played, each bar, or a number of seconds), the mix\'s loudness, brightness (spectral ' +
     'centroid), low/mid/high balance and busyness (onsets per second), the same per instrument ' +
     'from the stems, and plain findings -- an instrument buried far under another, two fighting ' +
     'over one band, clipping.',
     { render: z.string().optional().describe('an id from piece_render'),
       wav: z.string().optional().describe('or any 16-bit WAV, with no stems'),
       window: z.union([z.enum(['section', 'bar']), z.number().positive()]).default('section') },
     async ({ render, wav, window }, call) =>
     {
         const r = render ? renders.get(render) : null;

         if (render && !r)
             throw new Error(`no render ${render}: not from this session, or one of the ` +
                             `oldest and no longer kept`);

         if (!r && !wav)
             throw new Error('give a render id or a wav');

         if (r)
             call.use(r.dir);

         const s = r?.summary ?? { tempo: 120, meter: 4 };
         const mix = await measured(r ? r.wav : inside(wav), seconds =>
             windows(window, { seconds, tempo: s.tempo, meter: s.meter, timeline: s.timeline,
                               from: s.from }));
         const stems = [];

         if (r)
             for (const f of (await readdir(r.stems)).sort())
                 stems.push({ instrument: f.replace(/^\d+-/, '').replace(/\.wav$/, ''),
                              ...await measured(path.join(r.stems, f), () => mix.spans) });

         const per = mix.spans.map((w, index) =>
         {
             const parts = stems.map(st => ({ instrument: st.instrument, ...st.per[index] }))
                                .filter(p => p.lufs !== null);
             const found = findings(parts);

             if (mix.peak >= 0.999 && mix.per[index].peakDb >= -0.01)
                 found.push('the mix reaches full scale');

             return { index, label: w.label, start: +w.start.toFixed(3), end: +w.end.toFixed(3),
                      mix: mix.per[index], instruments: parts, findings: found };
         });

         return { lufs: mix.lufs, peakDb: +(20 * Math.log10(mix.peak || 1e-9)).toFixed(1),
                  findings: collapse(per), windows: per };
     });

// One WAV measured over each window, and only the numbers kept: a long
// render's stems held whole at once would be gigabytes.
async function measured (file, spansFor)
{
    const m = measure(await readWav(file));
    const spans = spansFor(m.seconds);

    return { spans, peak: m.peak, lufs: wholeLoudness(m),
             per: spans.map(w => describe(m, w.start, w.end)) };
}

// ---- instruments ------------------------------------------------------------

tool('dsp_check',
     'Load an instrument graph and run it: parse errors, whether it renders finite audio, whether ' +
     'every control is safe across its whole range, and its controls.',
     dspArgs,
     async (args, call) =>
     {
         const dir = await call.workdir('dsp');
         const file = await source(args, dir, 'instrument.dsp');
         const loads = await call.run('dspcheck', ['-p', PLUGINS, file]);
         const ok = loads.code === 0;
         const sweep = ok ? await call.run('dspsweep', ['-p', PLUGINS, '-n', '3', '-w', '4', file]) : null;

         return { ok: ok && sweep.code === 0, file,
                  load: (loads.stdout + loads.stderr).trim(),
                  sweep: sweep ? (sweep.stdout + sweep.stderr).trim() : undefined,
                  controls: ok ? await controlsOf(call, file) : undefined };
     });

tool('dsp_levels',
     'How loud an instrument is as voices stack: peak and how much the output stage has to shape, ' +
     'for 1 to `voices` notes at once. A pad that clips four deep needs less gain.',
     { ...dspArgs, voices: z.number().int().min(1).max(16).default(4) },
     async (args, call) =>
     {
         const dir = await call.workdir('dsp');
         const file = await source(args, dir, 'instrument.dsp');
         const r = await call.run('dsplevel', ['-p', PLUGINS, '-v', String(args.voices), file]);
         const rows = [...r.stdout.matchAll(/(\d+) voices?:\s+peak\s+([\d.]+)\s+shaped\s+([\d.]+)% of samples\s+peak cut\s+(>?[\d.]+) dB/g)]
             .map(m => ({ voices: +m[1], peak: +m[2], shapedPercent: +m[3], peakCutDb: m[4] }));

         if (!rows.length)
             throw new Error((r.stderr || r.stdout).slice(0, 2000));

         return rows;
     });

// A piece around one instrument: each strike its own chain, started when
// it should sound, so any notes and velocities can be heard in turn. A
// control in a unit the language folds is written with it, as a piece has
// to: the panel gives the value in that unit, and says which those are.
function auditionPiece (file, controls, spec, strikes, length)
{
    const values = Object.entries(controls ?? {}).map(([k, v]) =>
        `    ${k} = ${v}${spec[k]?.folded ? ' ' + spec[k].units : ''};\n`).join('');
    let text = `name "audition";\ntempo 60;\ninstrument x {\n    dsp "${file}";\n    amp = 20;\n${values}};\n`;

    strikes.forEach((s, i) =>
    {
        text += `chain s${i} { start = ${s.at} s;\n` +
                `    stage n gen::euclid { steps = 1; fills = 1; notes = "${s.note}";\n` +
                `        period = 10000 s; hold = ${length} s; vel = ${s.vel}; };\n` +
                `    sink { instrument = x; }; };\n`;
    });

    return text;
}

// In a directory of its own under the caller's, so a sweep's points are one
// call's work and not one each.
async function audition (call, file, controls, notes, velocities, length, gap, within)
{
    const strikes = [];

    for (const note of notes)
        for (const vel of velocities)
            strikes.push({ note, vel, at: strikes.length * gap });

    if (strikes.length * gap > MAX_SECONDS)
        throw new Error(`${strikes.length} strikes ${gap} s apart is longer than ${MAX_SECONDS} s`);

    // Written into the piece's text, which has no escapes.
    if (/["\n]/.test(file))
        throw new Error(`${file}: a path with a quote or a line break in it cannot be named in a piece`);

    for (const note of notes)
        if (!/^[A-Ga-g][#b]?-?\d+$/.test(note))
            throw new Error(`'${note}' is not a note name`);

    const spec = Object.fromEntries((await controlsOf(call, file)).map(c => [c.name, c]));

    for (const name of Object.keys(controls ?? {}))
        if (!spec[name])
            throw new Error(`the instrument has no control '${name}'`);

    const dir = await mkdtemp(path.join(within, 'audition-'));
    const gen = await source({ text: auditionPiece(file, controls, spec, strikes, length) },
                             dir, 'audition.gen');
    const wav = path.join(dir, 'audition.wav');
    const s = json(await call.run('genwav', ['-q', '-s', String(strikes.length * gap), '--json', '-',
                                             '-o', wav, gen]), 'genwav');

    if (!s.ok)
        throw new Error(s.errors.join('\n'));

    const m = measure(await readWav(wav));

    return { wav, strikes: strikes.map(st => ({ ...st, ...strike(m, st.at, gap) })) };
}

// One struck note: how loud, how bright at the front and after, and how long
// it takes to fall 40 dB.
function strike (m, at, gap)
{
    const front = describe(m, at, at + 0.05), body = describe(m, at + 0.15, at + Math.min(gap, 0.6));
    const i0 = Math.floor(at * m.rate), i1 = Math.floor((at + gap) * m.rate);
    let peak = 0, peakAt = i0;

    for (let i = i0; i < i1; i++)
        for (const c of m.channels)
            if (Math.abs(c[i]) > peak)
            {
                peak = Math.abs(c[i]);
                peakAt = i;
            }

    let gone = null;
    const block = Math.round(m.rate * 0.01);

    for (let i = peakAt; i + block < i1; i += block)
    {
        let p = 0;

        for (const c of m.channels)
            for (let k = i; k < i + block; k++)
                p = Math.max(p, Math.abs(c[k]));

        if (p < peak * 0.01)
        {
            gone = +((i - i0) / m.rate).toFixed(3);
            break;
        }
    }

    // Still ringing when the next strike comes says so, rather than a null
    // that reads as silence.
    return { peakDb: peak > 0 ? +(20 * Math.log10(peak)).toFixed(1) : null,
             attackCentroid: front.centroid ?? null, bodyCentroid: body.centroid ?? null,
             minus40DbAfter: gone, ...(gone === null && peak > 0 ? { ringsPast: gap } : {}) };
}

tool('dsp_audition',
     'Play an instrument: each note at each velocity, one after another, and measure each strike -- ' +
     'peak, brightness at the front and in the body, and how long it takes to fall 40 dB. ' +
     '`controls` sets chanargs as a piece\'s instrument block would. Returns the WAV.',
     { ...dspArgs,
       notes: z.array(z.string()).max(128).default(['C3']).describe('note names, e.g. ["C2", "G2"]'),
       velocities: z.array(z.number().int().min(1).max(127)).max(16).default([120, 80, 40]),
       controls: z.record(z.string(), z.number()).optional(),
       length: z.number().positive().max(10).default(0.5).describe('seconds each note is held'),
       gap: z.number().positive().max(20).default(2).describe('seconds from one strike to the next') },
     async (args, call) =>
     {
         const dir = await call.workdir('dsp');
         const file = await source(args, dir, 'instrument.dsp');

         const a = await audition(call, file, args.controls, args.notes, args.velocities,
                                  args.length, args.gap, dir);

         call.keep(dir);

         return a;
     });

tool('dsp_sweep',
     'What one control does: the same note played with the control at `points` values across ' +
     'its range, measured as dsp_audition measures, so its effect on level, brightness and ' +
     'length is a set of numbers rather than a guess.',
     { ...dspArgs,
       control: z.string(),
       note: z.string().default('C3'),
       velocity: z.number().int().min(1).max(127).default(100),
       points: z.number().int().min(2).max(9).default(5),
       length: z.number().positive().max(10).default(0.5),
       gap: z.number().positive().max(20).default(2) },
     async (args, call) =>
     {
         const dir = await call.workdir('dsp');
         const file = await source(args, dir, 'instrument.dsp');
         const c = (await controlsOf(call, file)).find(x => x.name === args.control);

         if (!c)
             throw new Error(`the instrument has no control '${args.control}'`);

         const out = [];

         for (let i = 0; i < args.points; i++)
         {
             const value = +(c.min + (c.max - c.min) * i / (args.points - 1)).toPrecision(4);
             const a = await audition(call, file, { [args.control]: value }, [args.note],
                                      [args.velocity], args.length, args.gap, dir);

             const { peakDb, attackCentroid, bodyCentroid, minus40DbAfter, ringsPast } = a.strikes[0];

             out.push({ value, peakDb, attackCentroid, bodyCentroid, minus40DbAfter, ringsPast });
         }

         return { control: c, note: args.note, velocity: args.velocity, points: out };
     });

tool('dsp_match',
     'Compare an instrument against a target recording. `score` says how far the graph is from ' +
     'the target (spectral, envelope and noise distances; lower is closer). `match` fits the ' +
     'graph\'s chanargs to the target and writes the found sound and settings beside the render.',
     { ...dspArgs,
       target: z.string().describe('a WAV of the sound wanted, one note'),
       mode: z.enum(['score', 'match']).default('score'),
       note: z.string().optional().describe('the note the target plays; match detects it') },
     async (args, call) =>
     {
         const dir = await call.workdir('match');
         const file = await source(args, dir, 'instrument.dsp');
         const target = inside(args.target);
         const cli = ['-p', PLUGINS];

         if (args.note)
             cli.push('-n', String(midi(args.note)));

         if (args.mode === 'match')
             cli.push('-o', path.join(dir, 'match'));

         const r = await call.run('dspmatch', [...cli, args.mode, target, file], { timeout: 1_800_000 });

         if (args.mode === 'match')
             call.keep(dir);
         const score = /total\s+([\d.]+)\s+spectral\s+([\d.]+)\s+envelope\s+([\d.]+)\s+noise\s+([\d.]+)/
             .exec(r.stdout);

         return { mode: args.mode, exit: r.code,
                  score: score ? { total: +score[1], spectral: +score[2], envelope: +score[3],
                                   noise: +score[4] } : undefined,
                  output: (r.stdout + r.stderr).trim().split('\n').slice(-40).join('\n'),
                  files: args.mode === 'match' ? (await readdir(dir)).filter(f => f.startsWith('match'))
                                                                    .map(f => path.join(dir, f))
                                               : undefined };
     });

function midi (name)
{
    const m = /^([A-Ga-g])([#b]?)(-?\d+)$/.exec(name.trim());

    if (!m)
        throw new Error(`'${name}' is not a note name`);

    const base = { c: 0, d: 2, e: 4, f: 5, g: 7, a: 9, b: 11 }[m[1].toLowerCase()];

    return base + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0) + (+m[3] + 1) * 12;
}

// ---- the engine's cost ------------------------------------------------------

tool('piece_profile',
     'What a piece costs to render, against real time: the mean, 99th-percentile and worst window ' +
     'as milliseconds of a window\'s budget, the slowest second, and each instrument\'s share. ' +
     'A worst window over its budget is a click on a slow machine; a share is where to look.',
     { ...pieceArgs, seconds: z.number().positive().max(600).default(60) },
     async (args, call) =>
     {
         const dir = await call.workdir('profile');
         const file = await source(args, dir, 'piece.gen');
         const s = json(await call.run('genwav', ['-q', '-s', String(args.seconds), '--profile',
                                                  '--json', '-', file]), 'genwav --profile');

         if (!s.ok)
             return s;

         const p = s.profile;

         // The channels' shares leave out the mix and the synth's own work,
         // which is what is left.
         return { ...p, meanShareOfRealTime: p.meanMs / p.windowMs,
                  worstShareOfRealTime: p.worstMs / p.windowMs,
                  overheadShare: 1 - p.channels.reduce((sum, c) => sum + c.share, 0),
                  channels: [...p.channels].sort((a, b) => b.share - a.share) };
     });

await server.connect(new StdioServerTransport());
