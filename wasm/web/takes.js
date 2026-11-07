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

/* Takes: what was played, recorded as it was heard, to listen back to and
 * to save as audio or as MIDI.
 *
 * The audio is the worklet's own output (worklet.js, record), not a
 * MediaRecorder's, so a take saved as a WAV is the samples that were
 * played and not a codec's idea of them. The notes are the tape's -- every
 * player's, and the keys played along on them -- placed by the worklet's
 * marks of where in the take each run of the transport began, so the MIDI
 * lines up with the audio across a Play's load or a rewind.
 * Rendering the piece again from its text would not do: a pad moved, a
 * Vary or a key changed while it played is in neither.
 */

/* The longest take kept: ten minutes of 16-bit stereo is about 110 MB. */
export const TAKE_MAX_SECONDS = 600;

/* `chunks' of interleaved stereo 16-bit samples as a WAV file: a header
   and the chunks themselves, not a copy of them all, which for a long
   take would be another hundred megabytes. */
export function wavOf (chunks, sampleRate)
{
    const frames = chunks.reduce((n, c) => n + c.length, 0) / 2;
    const header = new Uint8Array(44);
    const v = new DataView(header.buffer);
    const text = (at, s) => [...s].forEach((ch, i) =>
        v.setUint8(at + i, ch.charCodeAt(0)));

    text(0, 'RIFF');
    v.setUint32(4, 36 + frames * 4, true);
    text(8, 'WAVE');
    text(12, 'fmt ');
    v.setUint32(16, 16, true);
    v.setUint16(20, 1, true);              /* PCM */
    v.setUint16(22, 2, true);
    v.setUint32(24, sampleRate, true);
    v.setUint32(28, sampleRate * 4, true);
    v.setUint16(32, 4, true);
    v.setUint16(34, 16, true);
    text(36, 'data');
    v.setUint32(40, frames * 4, true);

    return new Blob([header, ...chunks], { type: 'audio/wav' });
}

/* Floats to 16-bit, clipped. */
export function pcm16 (floats)
{
    const out = new Int16Array(floats.length);

    for (let i = 0; i < floats.length; i++)
        out[i] = Math.max(-32768, Math.min(32767,
                                           Math.round(floats[i] * 32767)));

    return out;
}

const TICKS = 480;

/* A variable-length quantity, MIDI's way of writing a number. */
function vlq (n)
{
    const out = [n & 0x7f];

    while ((n >>= 7) > 0)
        out.unshift((n & 0x7f) | 0x80);

    return out;
}

/* Notes ({ at, duration, channel, note, velocity }, seconds from the top
 * of the take) as a type 1 MIDI file: a track for the tempo and one for
 * each channel, named by `names' (channel to name) where it has one.
 */
export function midiOf (notes, { tempo = 120, meter = 4, names = new Map() }
                        = {})
{
    const ticksOf = (s) => Math.max(0, Math.round(s * tempo / 60 * TICKS));
    const bytes = (s) => [...new TextEncoder().encode(s)];
    const chunk = (type, data) => [...bytes(type),
                                   (data.length >>> 24) & 0xff,
                                   (data.length >>> 16) & 0xff,
                                   (data.length >>> 8) & 0xff,
                                   data.length & 0xff, ...data];
    const track = (events) =>
    {
        const data = [];
        let last = 0;

        for (const e of events.sort((a, b) => a.tick - b.tick ||
                                              a.order - b.order))
        {
            data.push(...vlq(e.tick - last), ...e.bytes);
            last = e.tick;
        }

        data.push(0, 0xff, 0x2f, 0);
        return chunk('MTrk', data);
    };

    const us = Math.round(60e6 / tempo);
    const tracks = [track([
        { tick: 0, order: 0, bytes: [0xff, 0x51, 3, (us >> 16) & 0xff,
                                     (us >> 8) & 0xff, us & 0xff] },
        { tick: 0, order: 0, bytes: [0xff, 0x58, 4, meter, 2, 24, 8] }])];

    for (const ch of [...new Set(notes.map((n) => n.channel))].sort(
             (a, b) => a - b))
    {
        const c = ch & 0x0f;
        const name = bytes(names.get(ch) ?? `channel ${ch + 1}`);
        const events = [{ tick: 0, order: 0,
                          bytes: [0xff, 0x03, ...vlq(name.length), ...name] }];

        const mine = notes.filter((m) => m.channel === ch)
            .sort((a, b) => a.at - b.at);

        for (const [i, n] of mine.entries())
        {
            const key = Math.max(0, Math.min(127, n.note));
            const on = ticksOf(n.at);
            /* A key struck again while it still sounds: MIDI has one
               note per key, so the first ends where the second starts,
               or its off would end the second. */
            const next = mine.slice(i + 1).find((m) =>
                Math.max(0, Math.min(127, m.note)) === key);
            const off = Math.max(on + 1, Math.min(
                ticksOf(n.at + n.duration),
                next === undefined ? Infinity : ticksOf(next.at)));
            const vel = Math.max(1, Math.min(127, n.velocity));

            /* An off before an on at the same tick: a note repeated
               back to back is two notes, not one held. */
            events.push({ tick: on, order: 2, bytes: [0x90 | c, key, vel] },
                        { tick: off, order: 1, bytes: [0x80 | c, key, 0] });
        }

        tracks.push(track(events));
    }

    return new Uint8Array([
        ...chunk('MThd', [0, 1, 0, tracks.length, (TICKS >> 8) & 0xff,
                          TICKS & 0xff]),
        ...tracks.flat()]);
}

/* Keys pressed and let go ({ at, channel, note, velocity, on }) as notes
   with a duration; one still held at `end' lasts until then. */
export function keyNotes (keys, end)
{
    const held = new Map();
    const out = [];

    for (const k of keys)
    {
        const id = `${k.channel}:${k.note}`;

        if (k.on)
            held.set(id, k);
        else if (held.has(id))
        {
            const on = held.get(id);

            held.delete(id);
            out.push({ ...on, duration: k.at - on.at });
        }
    }

    for (const on of held.values())
        out.push({ ...on, duration: end - on.at });

    return out;
}

const clockOf = (s) => `${Math.floor(s / 60)}:` +
                       `${String(Math.floor(s % 60)).padStart(2, '0')}`;

/* The Record button and the takes under it.
 *
 * `synth' records (host.js, record); `play' starts the transport when a
 * take is asked for with it stopped; `about' says, when a take starts,
 * the piece's name, tempo and meter and its players' names by channel;
 * `say' tells the page's status. The page hands in what the worklet sends
 * (recorded) and every tape batch (tape).
 */
export function createTakes ({ button, list, synth, play, about, say })
{
    const takes = [];
    let take = null;
    let timer = null;

    function show ()
    {
        const on = take !== null;

        /* Named Record whatever it shows: pressed says it is on. */
        button.setAttribute('aria-pressed', String(on));
        button.setAttribute('aria-label', 'Record');
        button.textContent = on ? `■ Stop  ${clockOf(take.frames /
                                             (take.sampleRate || 1))}`
                                : '● Record';
        button.title = on ? 'Stop recording, and keep the take'
                          : 'Record what is played, to listen back to ' +
                            'and to save';
    }

    function start ()
    {
        if (synth() === null)
        {
            say('Press Start for sound first.');
            return;
        }

        take = { n: takes.length + 1, ...about(), sampleRate: 0, frames: 0,
                 chunks: [], marks: [], notes: [], keys: [], ran: false,
                 stopping: false };
        synth().record(true);
        timer = setInterval(show, 250);
        show();
        play();
        say('Recording');
    }

    function stop ()
    {
        if (take === null || take.stopping)
            return;

        take.stopping = true;
        synth().record(false);
    }

    function recorded (m)
    {
        if (take === null)
            return;

        take.marks.push(...m.marks);
        take.sampleRate = m.sampleRate;

        if (m.samples.length > 0)
        {
            take.chunks.push(pcm16(m.samples));
            take.frames += m.samples.length / 2;
        }

        if (!m.done && take.frames >= TAKE_MAX_SECONDS * m.sampleRate)
            stop();

        if (m.done)
            keep();
    }

    /* Every note the tape carries while recording, with its run's
       epoch; placed in the take when it is kept. */
    function tape (m)
    {
        if (take === null)
            return;

        for (const e of m.events)
            if (e.kind === 'N')
                take.notes.push({ at: e.at, duration: e.duration,
                                  channel: e.channel, note: e.note,
                                  velocity: e.velocity, epoch: m.epoch });

        for (const k of m.keys ?? [])
            take.keys.push({ ...k, epoch: m.epoch });

        /* The transport stopped ends the take; not before it has run, as
           a take asked for with it stopped starts before Play does. */
        if (m.running)
            take.ran = true;
        else if (take.ran)
            stop();
    }

    function keep ()
    {
        const t = take;
        const seconds = t.frames / t.sampleRate;

        take = null;
        clearInterval(timer);
        show();

        if (t.frames === 0)
        {
            say('Nothing was recorded: nothing played.');
            return;
        }

        /* In seconds from the top of the take, by the mark of the run
           each was in; one from before the take, or after it, is not in
           it. The transport starts inside a quantum, so the first one
           recorded can already be past a note at its start: a quantum's
           worth before the top is the top. */
        const early = 128 / t.sampleRate;
        const place = (list) => list.flatMap((e) =>
        {
            const mark = t.marks.find((k) => k.epoch === e.epoch);
            const at = mark === undefined ? -1
                : mark.frame / t.sampleRate + e.at - mark.now;

            return at >= -early && at < seconds
                ? [{ ...e, at: Math.max(0, at) }] : [];
        });

        t.seconds = seconds;
        t.notes = [...place(t.notes).map((n) => ({
                       ...n, duration: Math.min(n.duration,
                                                seconds - n.at) })),
                   ...keyNotes(place(t.keys), seconds)];
        t.wav = URL.createObjectURL(wavOf(t.chunks, t.sampleRate));
        t.chunks = null;
        takes.push(t);
        draw();
        say(`Take ${t.n} kept: ${clockOf(seconds)}`);
    }

    function save (blob, file)
    {
        const a = document.createElement('a');

        a.href = typeof blob === 'string' ? blob : URL.createObjectURL(blob);
        a.download = file;
        a.click();

        if (typeof blob !== 'string')
            setTimeout(() => URL.revokeObjectURL(a.href), 10000);
    }

    function row (t)
    {
        const el = document.createElement('div');
        const audio = document.createElement('audio');
        const stem = `${(t.name || 'band').replace(/[^\w.-]+/g, '_')}-` +
                     `take${t.n}`;
        const action = (text, title, on) =>
        {
            const b = document.createElement('button');

            b.textContent = text;
            b.title = title;
            b.addEventListener('click', on);
            return b;
        };

        el.className = 'take';
        el.setAttribute('role', 'group');
        el.setAttribute('aria-label', `Take ${t.n}`);
        audio.controls = true;
        audio.preload = 'metadata';
        audio.src = t.wav;
        audio.setAttribute('aria-label', `Take ${t.n}, to listen to`);

        const out = action('×', `Throw take ${t.n} away`, () =>
        {
            URL.revokeObjectURL(t.wav);
            takes.splice(takes.indexOf(t), 1);
            draw();
        });

        out.setAttribute('aria-label', out.title);
        el.append(
            Object.assign(document.createElement('b'),
                          { textContent: `Take ${t.n}` }),
            Object.assign(document.createElement('span'),
                          { textContent: clockOf(t.seconds) }),
            audio,
            action('Save audio', 'The take as a WAV file',
                   () => save(t.wav, `${stem}.wav`)),
            action('Save MIDI', 'Every player\'s notes, a track each',
                   () => save(new Blob([midiOf(t.notes, t)],
                                       { type: 'audio/midi' }),
                              `${stem}.mid`)),
            out);
        return el;
    }

    function draw ()
    {
        list.replaceChildren(...takes.map(row));
        list.hidden = takes.length === 0;
    }

    button.addEventListener('click', () => (take === null ? start()
                                                          : stop()));
    show();
    draw();

    return { recorded, tape, recording: () => take !== null,
             takes: () => takes };
}
