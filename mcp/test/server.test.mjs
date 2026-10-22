// The server as a client sees it: started over stdio, each tool called
// with the fixture or a shipped instrument, and each answer checked for
// what it claims -- not for its exact numbers, which belong to the engine.

import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { after, before, test } from 'node:test';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const fixture = { path: 'mcp/test/fixture.gen' };
let client;

before(async () =>
{
    client = new Client({ name: 'test', version: '0' });
    await client.connect(new StdioClientTransport({
        command: process.execPath, args: [path.join(here, '..', 'server.mjs')] }));
});

after(() => client.close());

async function call (name, args = {})
{
    const r = await client.callTool({ name, arguments: args });

    assert.ok(!r.isError, `${name}: ${r.content[0].text}`);

    return JSON.parse(r.content[0].text);
}

test('catalogs list what the tree ships', async () =>
{
    const stages = await call('catalog_stages', { name: 'euclid' });

    assert.equal(stages[0].kind, 'gen');
    assert.ok(stages[0].params.some(p => p.name === 'period' && p.units === 's'));

    const nodes = await call('catalog_nodes', { query: 'osc::simple' });

    assert.equal(nodes[0].values ?? nodes[0].args.find(a => a.name === 'waveform').values['1'],
                 'Sawtooth');

    const toms = await call('catalog_instruments', { query: 'kit tom', controls: true });

    assert.ok(toms[0].controls.some(c => c.name === 'decay' && c.max > c.min));
    assert.ok((await call('catalog_pieces')).groups.length > 0);
});

test('a broken piece says where', async () =>
{
    const r = await call('piece_check', { text: 'chain c { stage s gen::nope { }; sink { channel = 1; }; };' });

    assert.equal(r.ok, false);
    assert.match(r.errors.join('\n'), /:1:/);

    const ok = await call('piece_check', fixture);

    assert.deepEqual(ok.chains, ['kick', 'pad']);
    assert.deepEqual(ok.sections.map(s => s.name), ['a', 'b']);
});

test('the tape reads in note names, bars and beats', async () =>
{
    const r = await call('piece_tape', { ...fixture, seconds: 4, instruments: ['kick'] });
    const first = r.events[0];

    assert.equal(first.note, 'C2');
    assert.deepEqual([first.bar, first.beat], [1, 1]);
    assert.ok(r.events.some(e => e.bar === 2 && e.beat === 4));
});

test('a render, and an analysis that finds the buried pad', async () =>
{
    const r = await call('piece_render', { ...fixture, seconds: 4 });

    assert.deepEqual(r.timeline.map(t => t.section), ['a', 'b']);

    const a = await call('audio_analyze', { render: r.render, window: 'section' });

    // The same loudness genwav measured, from the same samples.
    assert.ok(Math.abs(a.lufs - r.mix.lufs) < 0.5, `${a.lufs} against ${r.mix.lufs}`);
    assert.ok(a.findings.some(f => /^pad is \d+ LU under kick \(a, bar 1\)$/.test(f)),
              a.findings.join('; '));
    // Muted in b, the pad's release still rings into it, quieter.
    const pad = (w) => a.windows[w].instruments.find(i => i.instrument === 'pad')?.lufs ?? -Infinity;

    assert.ok(pad(1) < pad(0), `${pad(1)} < ${pad(0)}`);
});

test('a part that drops out for a window, or goes quiet on one side, is found', async () =>
{
    // A tone struck every beat, its right channel a twentieth of its left,
    // and a knob taking it down 30 dB over the second of three windows.
    const text = `tempo 120; seed 1; @lvl = 1;
instrument tone {
    dsp {
        @gain = 1;
        node ionode { channels = 2; out0 = g->out; out1 = side->out; play = env->play; };
        io ionode;
        node o osc::simple { freq = ionode->note; waveform = 0; amp = env->out; };
        node g math::mul { in0 = o->out; in1 = @gain; };
        node side math::mul { in0 = g->out; in1 = 0.05; };
        node env env::ad { a = 1 ms; d = 400 ms; trigger = ionode->trigger; };
    };
    amp = 30; gain = @lvl;
};
chain c {
    stage s gen::euclid { steps = 1; fills = 1; notes = "A3"; period = 1 beats;
        hold = 0.2 beats; vel = 100; };
    sink { instrument = tone; };
};`;
    const r = await call('piece_render', { text, seconds: 6, timeline: [
        { at: 1.5, knob: 'lvl', value: 0.03 }, { at: 4, knob: 'lvl', value: 1 }] });
    const a = await call('audio_analyze', { render: r.render, window: 2 });

    assert.ok(a.findings.some(f => /^tone drops [23]\d LU under the windows either side \(2\.0 s\)$/.test(f)),
              a.findings.join('; '));
    assert.ok(a.findings.some(f => /^tone's right is 2\d dB under its left \(0\.0 s to 6\.0 s\)$/.test(f)),
              a.findings.join('; '));
});

test('a trimmed render is analyzed where its audio is', async () =>
{
    // Section b alone, against the same two seconds of the whole render:
    // the pad's tail measures the same in both.
    const pad = (w) => w.instruments.find(i => i.instrument === 'pad')?.lufs;
    const whole = await call('piece_render', { ...fixture, seconds: 4 });
    const cut = await call('piece_render', { ...fixture, seconds: 4, from: 2, length: 2 });
    const a = await call('audio_analyze', { render: whole.render, window: 2 });
    const b = await call('audio_analyze', { render: cut.render });

    // Numbered as the piece numbers its bars, not from the cut.
    assert.deepEqual(b.windows.map(w => w.label), ['b, bar 2']);

    const bars = await call('audio_analyze', { render: cut.render, window: 'bar' });

    assert.deepEqual(bars.windows.map(w => w.label), ['bar 2']);

    // Cut before b begins, the analysis does not report it.
    const early = await call('piece_render', { ...fixture, seconds: 4, length: 2 });

    assert.deepEqual((await call('audio_analyze', { render: early.render })).windows
                         .map(w => w.label), ['a, bar 1']);
    assert.ok(Math.abs(pad(a.windows[1]) - pad(b.windows[0])) < 1,
              `${pad(b.windows[0])} against ${pad(a.windows[1])}`);

    // genwav's own levels are of the audio it wrote, not of all it rendered.
    const meter = cut.channels.find(c => c.instrument === 'pad').lufs;

    assert.ok(Math.abs(meter - pad(b.windows[0])) < 1, `${meter} against ${pad(b.windows[0])}`);
});

test('a knob moved mid-piece changes what plays from then on', async () =>
{
    const quiet = await call('piece_render', { ...fixture, seconds: 4,
                                               timeline: [{ at: 0, knob: 'pad', value: 0 }] });
    const pad = (r) => r.channels.find(c => c.instrument === 'pad')?.lufs ?? null;

    assert.equal(pad(quiet), null);

    const cmp = await call('piece_compare', { ...fixture, knob: 'pad', a: 0, b: 3, seconds: 4 });

    assert.ok(cmp.channels.find(c => c.instrument === 'pad').diffRms > 0);
    assert.equal(cmp.channels.find(c => c.instrument === 'kick').diffRms, 0);
});

test('an instrument heard: harder is louder, longer decay rings longer', async () =>
{
    const tom = { path: 'dsp/kit_tom.dsp' };
    const checked = await call('dsp_check', tom);

    assert.ok(checked.ok, checked.load);

    const a = await call('dsp_audition', { ...tom, notes: ['C2'], velocities: [127, 30] });

    assert.ok(a.strikes[0].peakDb > a.strikes[1].peakDb + 6);

    const sweep = await call('dsp_sweep', { ...tom, control: 'decay', note: 'C2', points: 3 });
    const rings = sweep.points.map(p => p.minus40DbAfter ?? p.ringsPast * 10);

    assert.ok(rings[0] < rings[2], rings.join(' < '));

    const levels = await call('dsp_levels', { ...tom, voices: 2 });

    assert.deepEqual(levels.map(l => l.voices), [1, 2]);
});

test('a last window that goes quiet is a lapse only where the edges count', async () =>
{
    const { lapses } = await import('../lib/analyze.mjs');
    const parts = [{ lufs: -14 }, { lufs: -14 }, { lufs: -30 }];

    assert.deepEqual(lapses('x', parts).flat(), []);
    assert.deepEqual(lapses('x', parts, { edges: true }).flat(),
                     ['x drops 16 LU under the window beside it']);
});

test('a breeding report hears every champion after the declared graph', async () =>
{
    // Bred toward itself, the declared graph is where the target is.
    const r = await call('breed_report', { path: 'mcp/test/gain.dsp', targetPath: 'mcp/test/gain.dsp',
                                           seeds: [1], generations: 2, population: 3 });
    const run = r.runs[0];

    assert.ok(r.declared.distance < 0.5, `${r.declared.distance}`);
    assert.equal(run.generations.length, 2);

    for (const g of run.generations)
    {
        assert.ok(g.distance >= 0 && g.sound.lufs < 0 && Array.isArray(g.findings),
                  JSON.stringify(g));
        assert.ok(Math.abs(g.end - g.start - r.phraseSeconds) < 0.002);
    }

    const { readWav } = await import('../lib/wav.mjs');
    const wav = await readWav(run.wav);

    assert.ok(Math.abs(wav.channels[0].length / wav.rate - 3 * r.phraseSeconds) < 0.01);
    assert.deepEqual(r.summary.map(g => g.generation), [1, 2]);
});

test('a control in dB is set in dB', async () =>
{
    const a = await call('dsp_audition', { path: 'mcp/test/gain.dsp', notes: ['A3'],
                                           velocities: [100], controls: { gain: -20 } });
    const b = await call('dsp_audition', { path: 'mcp/test/gain.dsp', notes: ['A3'],
                                           velocities: [100], controls: { gain: 0 } });
    const drop = b.strikes[0].peakDb - a.strikes[0].peakDb;

    assert.ok(Math.abs(drop - 20) < 1, `${drop} dB apart`);
});

test('only the newest work is kept', async () =>
{
    const small = new Client({ name: 'keep', version: '0' });

    await small.connect(new StdioClientTransport({
        command: process.execPath, args: [path.join(here, '..', 'server.mjs')],
        env: { ...process.env, THINK_MCP_KEEP: '1' } }));

    const first = JSON.parse((await small.callTool({ name: 'piece_render',
        arguments: { ...fixture, seconds: 1 } })).content[0].text);

    // A call that keeps nothing takes nothing's place.
    await small.callTool({ name: 'piece_check', arguments: fixture });
    assert.ok(!(await small.callTool({ name: 'audio_analyze',
                                       arguments: { render: first.render } })).isError);

    await small.callTool({ name: 'piece_render', arguments: { ...fixture, seconds: 1 } });

    const r = await small.callTool({ name: 'audio_analyze', arguments: { render: first.render } });

    // A call's work is not taken away while it runs, though a call that
    // started after it and finished first is past what is kept.
    const [long, short] = await Promise.all([
        small.callTool({ name: 'dsp_audition', arguments: {
            text: readFileSync(path.join(here, 'gain.dsp'), 'utf8'), notes: ['A3', 'C4', 'E4'],
            velocities: [100, 60] } }),
        small.callTool({ name: 'piece_check', arguments: fixture }) ]);

    await small.close();

    assert.ok(!long.isError, long.content[0].text);
    assert.ok(!short.isError, short.content[0].text);

    assert.ok(r.isError && /no longer kept/.test(r.content[0].text));
    assert.equal(existsSync(path.dirname(first.wav)), false);
});

test('what cannot be asked is refused before any work', async () =>
{
    const gain = { path: 'mcp/test/gain.dsp' };

    for (const [name, args, why] of [
        ['dsp_audition', { ...gain, notes: Array(100).fill('C3'), velocities: [100], gap: 20 },
         /longer than 1200 s/],
        ['dsp_audition', { ...gain, notes: ['C3"; amp = 1'] }, /not a note name/],
        ['dsp_audition', { ...gain, controls: { 'gain = 1; x': 0 } }, /no control/],
        ['piece_check', { path: '../outside.gen' }, /outside the tree/],
        ['audio_analyze', { wav: '/etc/hostname' }, /outside the tree/],
    ])
    {
        const r = await client.callTool({ name, arguments: args });

        assert.ok(r.isError && why.test(r.content[0].text), `${name}: ${r.content[0].text}`);
    }
});

test('the profile says what each instrument costs', async () =>
{
    const p = await call('piece_profile', { ...fixture, seconds: 4 });
    assert.ok(p.meanMs > 0 && p.windowMs > 20);
    assert.deepEqual(p.channels.slice(0, 2).map(c => c.name).sort(), ['kick', 'pad']);
    assert.ok(p.overheadShare >= 0 && p.overheadShare < 1);
});
