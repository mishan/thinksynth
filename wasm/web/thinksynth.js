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
 * thinksynth.js -- the engine for a page that is not this one: a game, a
 * toy, a visualizer. docs/SDK.md is the manual; this is the whole API.
 *
 * It is host.js with the decisions a page otherwise has to make for
 * itself made once: which files a piece needs, where transport zero is,
 * what has been composed but not yet heard, and when a change to what is
 * composed may land without the look-ahead's notes going wrong.
 */

import { createSynth } from './host.js';
import { horizonOf } from './horizon.js';

/* How long the look-ahead may take to load a piece the worklet loaded. */
const LOAD_TIMEOUT_MS = 10000;

/* Transport zero this far past the frame the worklet has reached when a
   start is asked for: room for the begin to arrive before its frame. */
const START_LEAD = 0.4;

/* A piece or a graph without its `#' comments, which may name files. */
const uncommented = (text) =>
    text.replace(/("[^"\n]*")|#.*$/gm, (m, quoted) => quoted ?? '');

export async function createEngine (ctx, { assets, bars = 2,
                                           output = ctx.destination,
                                           onLog = () => {} } = {})
{
    const engine = new Engine(ctx, assets ?? new URL('./', import.meta.url),
                              bars);

    engine.synth = await createSynth(ctx, {
        onLog,
        onTape: (m) => engine.tape(m),
        onMirror: (m) => engine.lookahead(m, onLog),
        mirrorScript: new URL('./lookahead.js', import.meta.url),
        onEdited: (m) => m.errors?.forEach((e) => onLog(`edit: ${e}`)),
    });
    engine.rate = engine.synth.sampleRate;
    engine.synth.node.connect(output);

    return engine;
}

class Engine
{
    constructor (ctx, assets, bars)
    {
        this.ctx = ctx;
        this.assets = new URL(assets, location.href);
        this.bars = bars;
        this.synth = null;
        this.rate = 0;

        /* The piece as the engine's edits have left it, and what it is. */
        this.text = '';
        this.tempo = 0;
        this.meter = 4;
        this.beatSeconds = 0;
        this.barSeconds = 0;
        this.chains = [];
        this.sections = [];
        this.knobs = new Map();
        this.instruments = new Map();

        this.origin = NaN;
        this.running = false;
        this.reached = 0;

        this.chainOf = new Map();
        this.tapeFrame = 0;
        this.listeners = new Map();
        this.sent = new Set();
        this.index = null;
        this.editing = Promise.resolve();
        this.setupAt = null;
        this.ended = false;
        this.ran = false;
        this.loaded = null;
        this.nextTape = [];
    }

    /* ---- loading --------------------------------------------------- */

    /* A piece, as text, and the instruments a page plays on itself as
       `voices': { name: "the body of an instrument block" }. Only the
       graphs and samples it names are fetched. In turn with the edits, so
       an edit asked for before it is not made on the piece it replaces. */
    load (text, options)
    {
        this.editing = this.editing.catch(() => {})
            .then(() => this.loadNow(text, options));

        return this.editing;
    }

    async loadNow (text, { seed = -1, voices = {} } = {})
    {
        const full = text + Object.entries(voices)
            .map(([name, body]) => `\ninstrument ${name} {\n${body}\n};\n`)
            .join('');

        await this.fetchAssets(full);

        const chains = new Promise((resolve) => { this.loaded = resolve; });
        const piece = await this.synth.loadPiece(full, seed);

        if (piece.errors.length > 0)
            throw new Error(piece.errors.join('\n'));

        const looked = await Promise.race([
            chains,
            new Promise((_, reject) => setTimeout(() => reject(
                new Error('the look-ahead did not load the piece')),
                LOAD_TIMEOUT_MS)),
        ]);

        this.text = full;
        this.tempo = looked.tempo;
        this.meter = looked.meter;
        this.reached = 0;
        this.beatSeconds = 60 / this.tempo;
        this.barSeconds = this.meter * this.beatSeconds;
        this.instruments = new Map(piece.instruments.map(
            (i) => [i.name, i.channel]));
        this.knobs = new Map(piece.knobs.map(
            (k) => [k.name.replace(/^@/, ''), k.knob]));
        this.chains = looked.chains.map((c, index) => ({ ...c, index }));
        this.chainOf.clear();

        for (const c of this.chains)
            for (const name of c.instruments)
                this.chainOf.set(this.instruments.get(name), c.name);

        this.sections = piece.sections.map((s) => ({
            name: s.name,
            at: s.before + s.beatsBefore * this.beatSeconds,
        }));
        this.synth.toMirror({ type: 'lookahead', bars: this.bars,
                              barSeconds: this.barSeconds });

        return this;
    }

    async fetchAssets (text)
    {
        this.index ??= await this.get('dsp/index.json')
            .then((r) => r.json()).then((names) => new Set(names));

        const graphs = [...new Set([...uncommented(text).matchAll(
            /\b(?:dsp|effect)\s+"([^"]+)"/g)].map((m) => m[1]))]
            .filter((n) => !this.sent.has(n));
        const texts = await Promise.all(graphs.map(
            (n) => this.get(`dsp/${n}`).then((r) => r.text())));
        const samples = new Set();

        graphs.forEach((name, i) =>
        {
            this.sent.add(name);
            this.synth.instrument(name, texts[i]);

            for (const [, files] of uncommented(texts[i]).matchAll(/"([^"]*\.wav[^"]*)"/g))
                for (const file of files.split(/\s+/))
                {
                    const sample = `samples/${file.replace(/@.*$/, '')}`;

                    if (this.index.has(sample) && !this.sent.has(sample))
                        samples.add(sample);
                }
        });

        await Promise.all([...samples].map(async (name) =>
        {
            const bytes = await this.get(`dsp/${name}`)
                .then((r) => r.arrayBuffer());

            this.sent.add(name);
            this.synth.sample(name, new Uint8Array(bytes));
        }));
    }

    async get (path)
    {
        const r = await fetch(new URL(path, this.assets));

        if (!r.ok)
            throw new Error(`${path}: ${r.status} ${r.statusText}`);

        return r;
    }

    /* ---- the transport --------------------------------------------- */

    /* From bar `fromBar'. Whatever `setup' changes is stamped at the
       start, before anything is composed past it. */
    async start ({ fromBar = 0, setup = () => {} } = {})
    {
        await this.ctx.resume();
        await new Promise((resolve) => this.nextTape.push(resolve));

        const from = fromBar * this.barSeconds;
        const at = this.tapeFrame + Math.round(START_LEAD * this.rate);

        this.origin = at - from * this.rate;
        this.reached = 0;
        this.ended = false;
        this.ran = false;
        this.synth.batch(() =>
        {
            this.synth.begin(at, from);
            this.setupAt = from;

            try
            {
                setup(this);
            }
            finally
            {
                this.setupAt = null;
            }
        });
        this.running = true;
    }

    stop ()
    {
        this.synth.transport('stop');
        this.running = false;
    }

    /* ---- time, in transport seconds -------------------------------- */

    /* What is coming out of the speakers now. */
    heard ()
    {
        const latency = this.ctx.outputLatency || this.ctx.baseLatency || 0;

        return this.transportAt(this.ctx.currentTime - latency);
    }

    /* What the worklet is about to render: a note stamped past this is
       played on time. */
    rendered ()
    {
        return this.transportAt(this.ctx.currentTime) + 0.01;
    }

    /* How far the look-ahead has composed. */
    written ()
    {
        return this.reached;
    }

    /* Where a change to what is composed lands if it is made now. */
    line ()
    {
        return horizonOf(this.transportAt(this.tapeFrame / this.rate),
                         this.barSeconds, this.bars);
    }

    barAt (t)
    {
        return Math.floor(t / this.barSeconds);
    }

    sectionAt (t)
    {
        return this.sections.findLast((s) => s.at <= t) ?? null;
    }

    /* ---- what is composed: lands on the line ----------------------- */

    mute (chain, on = true, { at } = {})
    {
        this.synth.mute({ at: this.stamp(at), chain: this.chain(chain).index,
                          on });
    }

    /* A piece knob. One that only an effect or an instrument reads is a
       sound and not a composition, and `now' lets it go at once. */
    knob (name, value, { at, now = false } = {})
    {
        if (!this.knobs.has(name))
            throw new Error(`no knob @${name}`);

        this.synth.knob(this.knobs.get(name), value, now ? -1 : this.stamp(at));
    }

    addStage (chain, stage, plugin, params = {}, { category = 'xform',
                                                   at } = {})
    {
        return this.edit([['stage', chain, stage, category, plugin,
                           ...Object.entries(params).map(([k, v]) =>
                               `${k}=${v}`)]],
                         () => this.chain(chain).stages.push(stage), at);
    }

    removeStage (chain, stage, { at } = {})
    {
        return this.edit(() => [['unstage', chain,
                                 String(this.stageIndex(chain, stage))]],
                         () => this.chain(chain).stages.splice(
                             this.stageIndex(chain, stage), 1), at);
    }

    setParam (chain, stage, param, value, { at } = {})
    {
        return this.edit(() => [['param', chain,
                                 String(this.stageIndex(chain, stage)),
                                 param, String(value)]], () => {}, at);
    }

    /* One at a time, each built on the text the one before left.
       Resolves to the time it was stamped at. */
    edit (ops, after, at)
    {
        this.editing = this.editing.catch(() => {}).then(async () =>
        {
            const list = typeof ops === 'function' ? ops() : ops;
            const r = await this.synth.genOps(this.text, list);

            if (!r.text)
                throw new Error(r.why || 'the edit was refused');

            const stamp = this.stamp(at);

            this.text = r.text;
            after();
            this.synth.edit(stamp, this.text);
            return stamp;
        });

        return this.editing;
    }

    /* ---- what is not: goes at once --------------------------------- */

    /* A note on an instrument no chain listens to: a sound and not a
       composition. `quantize' is in beats. Returns its time. */
    play (instrument, note, { velocity = 100, duration = 0.2, at,
                              quantize = 0 } = {})
    {
        const channel = this.instruments.get(instrument);

        if (channel === undefined)
            throw new Error(`no instrument ${instrument}`);

        this.playing();

        let t = at ?? this.rendered();

        if (quantize > 0)
        {
            const grid = quantize * this.beatSeconds;

            t = Math.ceil(t / grid) * grid;
        }

        this.synth.noteAt(t, channel, note, velocity, true, false, 0);
        this.synth.noteAt(t + duration, channel, note, 0, false, false, 0);

        return t;
    }

    /* ---- events ------------------------------------------------------
     *
     *   ahead   notes the look-ahead composed, before they sound
     *   played  notes the worklet delivered, as they sound
     *   end     the piece stopped itself
     *
     * A note is { at, duration, note, velocity, chain }. */
    on (type, fn)
    {
        if (!this.listeners.has(type))
            this.listeners.set(type, new Set());

        this.listeners.get(type).add(fn);
        return () => this.listeners.get(type).delete(fn);
    }

    emit (type, value)
    {
        for (const fn of this.listeners.get(type) ?? [])
            fn(value);
    }

    /* ---- inside ------------------------------------------------------ */

    /* Audio-clock seconds `t' in transport seconds. */
    transportAt (t)
    {
        if (Number.isNaN(this.origin))
            throw new Error('the engine has not been started');

        return t - this.origin / this.rate;
    }

    /* A stamped command waits for a transport that is not running, and a
       later start() begins it somewhere else. */
    playing ()
    {
        if (!this.running && this.setupAt === null)
            throw new Error('the engine is not playing');
    }

    /* Earlier than the line is a change the look-ahead may already have
       composed past. */
    stamp (at)
    {
        this.playing();

        const line = this.setupAt ?? this.line();

        if (at < line)
            throw new Error(`${at} is before the line, ${line}`);

        return at ?? line;
    }

    chain (name)
    {
        const c = this.chains.find((x) => x.name === name);

        if (c === undefined)
            throw new Error(`no chain ${name}`);

        return c;
    }

    stageIndex (chain, stage)
    {
        const i = this.chain(chain).stages.indexOf(stage);

        if (i < 0)
            throw new Error(`no stage ${stage} in ${chain}`);

        return i;
    }

    notes (events)
    {
        return events.filter((e) => e.kind === 'N' &&
                                    this.chainOf.has(e.channel))
            .map((e) => ({ at: e.at, duration: e.duration, note: e.note,
                           velocity: e.velocity,
                           chain: this.chainOf.get(e.channel) }));
    }

    tape (m)
    {
        this.tapeFrame = m.frame;

        for (const resolve of this.nextTape.splice(0))
            resolve();

        if (!this.running)
            return;

        const played = this.notes(m.events);

        if (played.length > 0)
            this.emit('played', played);

        this.ran ||= m.running;

        if (this.ran && !m.running && !this.ended)
        {
            this.ended = true;
            this.running = false;
            this.emit('end');
        }
    }

    lookahead (m, onLog)
    {
        if (m.type === 'chains')
            this.loaded?.(m);
        else if (m.type === 'ahead')
        {
            this.reached = Math.max(this.reached, m.reached);

            const ahead = this.notes(m.events);

            if (ahead.length > 0)
                this.emit('ahead', ahead);
        }
        else if (m.type === 'log')
            onLog(m.text);
    }
}
