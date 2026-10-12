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

/* A patch, growing: the solo page's Grow pane.
 *
 * The same moves a piece's gen::grow stage and its Evolve toward make, with
 * a person rather than a piece deciding when: Grow is one random edit of
 * the kinds ticked (host.js, grow), and Evolve a search toward another
 * patch's sound or a recording's (host.js, evolve). Every graph on the way
 * is a row of a list, the patch it began from first; choosing a row puts that
 * graph on the patch's channel, so the keys always play the row chosen and
 * any two can be compared by ear. A new graph from a row before the last
 * replaces the rows after it, as an edit after an undo does.
 *
 * The row chosen is the patch source's text, which is what the node editor
 * and the parameters show: `setGraph' puts it there and loads it, and
 * `loaded' is told of every load, so an edit there -- typed, or made on the
 * canvas -- is a row too. Keep downloads it as a .dsp file.
 *
 * `synth' is a getter for the page's; `name' the patch's file name;
 * `targets' the patches to evolve toward, `targetText' fetches one, and
 * `hear' puts a graph on the channel without making it the patch's.
 */

import { drawTrail } from './evolveui.js';

const $ = (id) => document.getElementById(id);

const RECORDING = 'A recording...';
const BY_HAND = 'edited by hand';

export function createGrow ({ synth, name, setGraph, hear, targets,
                              targetText, status })
{
    let rows = [];              /* { what, text }, where it began first */
    let chosen = 0;
    let searching = null;
    let sound = null;
    let hearing = false;        /* the target on the channel, not the row */

    /* A search for a list being put away ends with it. Marked done first:
       one still making its workers has nothing to cancel yet, and ends
       itself once it has (evolve). */
    function stop ()
    {
        if (searching !== null)
        {
            searching.done = true;
            searching.cancel();
        }

        searching = null;
    }

    function buttons ()
    {
        const ready = synth() !== null && rows.length > 0;
        const running = searching !== null && !searching.done;

        $('growstep').disabled = !ready || running;
        $('growtoward').disabled = !ready || running;
        $('growhow').disabled = !ready || running;
        $('growgo').disabled = !ready || running;
        $('growstop').disabled = !running;
        $('growtarget').disabled = !ready ||
                                  $('growtoward').value === RECORDING;
        $('growkeep').disabled = !ready;
        $('growtarget').setAttribute('aria-pressed', String(hearing));
    }

    function draw ()
    {
        const list = $('growlist');

        list.replaceChildren(...rows.map((r, i) =>
        {
            const li = document.createElement('li');
            const b = document.createElement('button');

            b.textContent = r.what;
            b.addEventListener('click', () => choose(i));
            li.classList.toggle('chosen', i === chosen);
            li.append(b);
            return li;
        }));

        buttons();
    }

    async function choose (i)
    {
        chosen = i;
        hearing = false;
        draw();
        await setGraph(rows[i].text);
    }

    /* A graph after the row chosen, replacing any rows after that, and
       chosen. */
    async function add (what, text)
    {
        rows = rows.slice(0, chosen + 1);
        rows.push({ what, text });
        await choose(rows.length - 1);
    }

    async function step ()
    {
        const kinds = [...document.querySelectorAll(
            'input[name="growkind"]:checked')].map((k) => k.value).join(' ');

        if (kinds === '')
        {
            status('Tick at least one kind of edit to grow by.', true);
            return;
        }

        $('growstep').disabled = true;

        const from = rows;
        const r = await synth().grow(rows[chosen].text, rows[0].text,
                                     (Math.random() * 2 ** 32) >>> 0, kinds);

        /* Asked for a patch since put away (reset). */
        if (rows !== from)
            return;

        if (r.grown.length === 0)
            status(`Nothing to grow: ${r.why || 'no edit of those kinds fits'}.`,
                   true);
        else
            await add(r.what[0], r.grown[0]);

        buttons();
    }

    async function evolve ()
    {
        const toward = $('growtoward').value;
        const recording = toward === RECORDING;

        if (recording && sound === null)
        {
            $('growsound').click();
            return;
        }

        const from = rows;
        const spec = {
            start: rows[chosen].text, startArgs: '',
            target: recording ? '' : await targetText(toward),
            targetArgs: '', grow: $('growhow').value === '1',
        };
        const workers = Math.max(1, Math.min(4,
            (navigator.hardwareConcurrency ?? 2) - 2));
        const run = { trail: [], best: Infinity, nearest: '', done: false,
                      ended: 0, searches: Infinity, result: null,
                      began: performance.now(), cancel: () => {},
                      toward: recording ? sound.name.replace(/^.*\//, '')
                                        : toward };

        if (rows !== from)
            return;

        searching = run;
        $('growstate').textContent = 'listening...';
        buttons();

        const { cancel, searches } = await synth().evolve(spec, {
            seed: (Math.random() * 2 ** 31) >>> 0, workers,
            sound: recording ? sound : null,
        }, (search, m) =>
        {
            if (searching !== run)
                return;

            if (m.type === 'nearer' && m.distance < run.best)
            {
                run.best = m.distance;
                run.nearest = m.graph;
                run.trail.push([m.seconds, m.distance]);
                $('growstate').textContent =
                    `nearest ${m.distance.toFixed(2)} dB`;
            }
            else if (m.type === 'done')
            {
                if (m.ok && (run.result === null ||
                             m.distance < run.result.distance))
                    run.result = m;

                if (++run.ended === run.searches)
                    end(run, m.why);
            }

            drawTrail($('growplot'), run);
        });

        run.cancel = cancel;
        run.searches = searches;

        if (run.done)
            cancel();
    }

    /* A search over, done or cancelled: the nearest it found, a row. */
    async function end (run, why)
    {
        if (run.done)
            return;

        run.done = true;

        const finished = run.result !== null &&
                         run.result.distance <= run.best;
        const found = finished ? run.result.found : run.nearest;
        const near = finished ? run.result.distance : run.best;

        drawTrail($('growplot'), run);

        if (found === '' || searching !== run)
        {
            $('growstate').textContent = why || 'the search found nothing';
            buttons();
            return;
        }

        if (found === rows[chosen].text)
        {
            $('growstate').textContent = 'nothing nearer than it began';
            buttons();
            return;
        }

        $('growstate').textContent =
            `${near.toFixed(2)} dB from ${run.toward}`;
        await add(`toward ${run.toward}, ${near.toFixed(2)} dB`, found);
    }

    /* The target on the channel in place of the row chosen, or back. */
    async function hearTarget ()
    {
        hearing = !hearing;
        buttons();

        if (hearing)
            await hear(await targetText($('growtoward').value));
        else
            await setGraph(rows[chosen].text);
    }

    function keep ()
    {
        const a = document.createElement('a');

        a.href = URL.createObjectURL(new Blob([rows[chosen].text],
                                              { type: 'text/plain' }));
        a.download = `${name().replace(/\.dsp$/, '')}-grown.dsp`;
        a.click();
        setTimeout(() => URL.revokeObjectURL(a.href), 10000);
    }

    $('growstep').addEventListener('click', () => step());
    $('growgo').addEventListener('click', () =>
        evolve().catch((e) =>
        {
            if (searching !== null && !searching.done)
                end(searching, e.message);
            else
                $('growstate').textContent = e.message;
        }));
    $('growstop').addEventListener('click', () =>
    {
        if (searching === null || searching.done)
            return;

        searching.cancel();
        end(searching, 'stopped');
    });
    $('growtarget').addEventListener('click', () => hearTarget());
    $('growkeep').addEventListener('click', keep);
    $('growtoward').addEventListener('change', () =>
    {
        if ($('growtoward').value === RECORDING)
            $('growsound').click();

        if (hearing)
            choose(chosen);
    });
    $('growsound').addEventListener('change', async () =>
    {
        const file = $('growsound').files[0];

        sound = file === undefined ? null
            : { name: `evolve/${file.name}`,
                bytes: new Uint8Array(await file.arrayBuffer()) };
        $('growstate').textContent = sound === null ? ''
            : `toward ${file.name}`;
    });

    return {
        /* A patch to grow from: its graph the first row, and nothing
           after. */
        reset (text)
        {
            stop();
            rows = [{ what: name(), text }];
            chosen = 0;
            hearing = false;
            $('growstate').textContent = '';
            drawTrail($('growplot'), null);
            draw();
        },

        /* Whether there is a list: what entering the mode again keeps. */
        started: () => rows.length > 0,

        stop,

        /* What the patch's channel was loaded with: a row of its own if it
           is not the row chosen -- an edit made in the source box or on
           the canvas, where a run of edits is one row as it goes. */
        loaded (text)
        {
            hearing = false;

            if (rows.length === 0 || text === rows[chosen].text)
            {
                buttons();
                return;
            }

            if (rows[chosen].what === BY_HAND && chosen === rows.length - 1)
                rows[chosen].text = text;
            else
            {
                rows = rows.slice(0, chosen + 1);
                rows.push({ what: BY_HAND, text });
                chosen = rows.length - 1;
            }

            draw();
        },

        /* The patches to evolve toward, once the page has their names. */
        fill ()
        {
            const was = $('growtoward').value;

            $('growtoward').replaceChildren(...[...targets(), RECORDING]
                .map((n) =>
                {
                    const o = document.createElement('option');

                    o.textContent = n;
                    return o;
                }));

            if ([...$('growtoward').options].some((o) => o.value === was))
                $('growtoward').value = was;

            buttons();
        },

        buttons,
    };
}
