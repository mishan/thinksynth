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

/* Evolve toward, for the solo page and a room alike: the desktop's Evolve
 * window on the page's terms. A search on as many workers as the machine
 * has cores to spare (host.js, evolve.js) -- one search scored on all of
 * them, or where the browser cannot, a search each on seeds of their own
 * -- the best kept as they go: the nearest graph for Listen, the trail
 * for the plot, and at the end the nearest result. Cancel ends the workers
 * and keeps the nearest found.
 *
 * The search is this browser's; what it finds is the page's to put on the
 * channel (`put'), in place of what this search put there last -- a
 * command at the next window on the solo page, and a room's command on
 * every peer in one -- and to keep into the piece (`keep'), as Keep does
 * on a grow stage. Discard takes it off.
 *
 * `synth' and `piece' are getters for the page's; `loading' says a load
 * is under way, and `mayChange' whether this page may change the sound --
 * a room's spectator may not. buttons() is for when that changes.
 */

const $ = (id) => document.getElementById(id);

export function createEvolve ({ synth, piece, loading, put, keep,
                                mayChange = () => true })
{
    let evolving = null;
    let graph = '';
    let what = '';
    let forPiece = null;
    let sound = null;

    /* Graphs go on the channel one at a time: each says what it replaces,
       which is what the one before it put there. */
    let putting = Promise.resolve();

    function buttons ()
    {
        const running = evolving !== null && !evolving.done;
        const may = mayChange();

        $('evolvego').disabled = running || piece() === null || !may;
        $('evolvestop').disabled = !running;
        $('evolvehear').disabled = !running || !may;
        $('evolvekeep').disabled = running || graph === '' || !may;
        $('evolvedrop').disabled = running || graph === '' || !may;
    }

    /* The piece's instruments to choose among, and what a load leaves of
       a search. A load puts the declared graphs back, so nothing of it is
       on a channel; a search goes on through a reload of the same piece --
       a stage moved, an effect chosen -- and ends with another. */
    function show ()
    {
        const names = (piece()?.instruments ?? []).map((i) => i.name);
        const fill = (select, extra) =>
        {
            const was = select.value;

            select.replaceChildren(...[...names, ...extra].map((n) =>
            {
                const o = document.createElement('option');

                o.textContent = n;
                return o;
            }));

            if ([...names, ...extra].includes(was))
                select.value = was;
        };

        if (evolving !== null &&
            (piece()?.name !== forPiece || !names.includes(what)))
        {
            evolving.cancel();
            evolving = null;
            $('evolvestate').textContent = '';
        }

        graph = '';
        fill($('evolvewhat'), []);
        fill($('evolvetoward'), ['A recording...']);

        if ($('evolvetoward').value === $('evolvewhat').value &&
            names.length > 1)
            $('evolvetoward').selectedIndex = 1;

        for (const id of ['evolvewhat', 'evolvetoward', 'evolvehow'])
            $(id).disabled = piece() === null;

        buttons();
    }

    function draw ()
    {
        const canvas = $('evolveplot');
        const trail = evolving?.trail ?? [];
        const cx = canvas.getContext('2d');
        const w = canvas.width, h = canvas.height, pad = 6;

        canvas.hidden = trail.length === 0;
        cx.fillStyle = '#14161a';
        cx.fillRect(0, 0, w, h);

        if (trail.length === 0)
            return;

        const end = Math.max(evolving.done ? trail[trail.length - 1][0]
                             : (performance.now() - evolving.began) / 1000,
                             1);
        const ds = trail.map((p) => p[1]);
        const lo = Math.min(...ds);
        const range = Math.max(Math.max(...ds) - lo, 0.5);
        const x = (t) => pad + t / end * (w - 2 * pad);
        const y = (d) => pad + (d - lo) / range * (h - 2 * pad);
        let held = trail[0][1];

        cx.beginPath();
        cx.moveTo(x(0), y(held));

        for (const [t, d] of trail)
        {
            cx.lineTo(x(t), y(held));
            cx.lineTo(x(t), y(d));
            held = d;
        }

        cx.lineTo(x(end), y(held));
        cx.strokeStyle = '#59a6ff';
        cx.lineWidth = 1.5;
        cx.stroke();
    }

    /* `found' on the instrument's channel, in place of what this search
       put there last; `strict', as Listen is, refused once the channel has
       moved on from that. '' takes it off. */
    function putFound (found, strict)
    {
        const done = putting.then(async () =>
        {
            const why = await put(what, found, graph, strict);

            if (why === '')
                graph = found;

            return why;
        });

        putting = done.catch(() => {});

        return done;
    }

    async function start ()
    {
        if (evolving !== null && !evolving.done)
            return;

        const instrument = $('evolvewhat').value;
        const toward = $('evolvetoward').value;
        const recording = toward === 'A recording...';

        if (recording && sound === null)
        {
            $('evolvesound').click();
            return;
        }

        $('evolvego').disabled = true;

        const spec = await synth().evolveSpec(instrument,
                                              recording ? '' : toward);

        if (spec.why !== '')
        {
            $('evolvestate').textContent = spec.why;
            buttons();
            return;
        }

        spec.grow = $('evolvehow').value === '1';

        const workers = Math.max(1, Math.min(4,
            (navigator.hardwareConcurrency ?? 2) - 2));
        const run = { trail: [], best: Infinity, nearest: '', started: null,
                      done: false, ended: 0, searches: Infinity,
                      result: null, began: performance.now(),
                      cancel: () => {} };

        evolving = run;
        what = instrument;
        forPiece = piece()?.name ?? null;
        graph = '';
        $('evolvestate').textContent = 'listening...';

        const searching = synth().evolve(spec, {
            seed: piece()?.seed ?? 1, workers,
            sound: recording ? sound : null,
        }, (search, m) =>
        {
            if (evolving !== run)
                return;

            if (m.type === 'started')
                run.started ??= m.distance;
            else if (m.type === 'nearer' && m.distance < run.best)
            {
                run.best = m.distance;
                run.nearest = m.graph;
                run.trail.push([m.seconds, m.distance]);
                $('evolvestate').textContent =
                    `nearest ${m.distance.toFixed(2)} dB`;
            }
            else if (m.type === 'done')
            {
                if (m.ok && (run.result === null ||
                             m.distance < run.result.distance))
                    run.result = m;

                if (++run.ended === run.searches)
                    end(run, run.result?.why ?? m.why);
            }

            draw();
        });

        buttons();

        const { cancel, searches } = await searching;

        run.cancel = cancel;
        run.searches = searches;

        /* Cancelled, or let go of by a load, while the workers were being
           made. */
        if (run.done || evolving !== run)
            cancel();
    }

    /* A search over, all its searches in or cancelled: the nearest it
       found on the channel, to be kept or discarded. */
    async function end (run, why)
    {
        if (run.done)
            return;

        run.done = true;

        /* The nearest any search came: a finished one's result, or what
           one still going had found when it was cancelled. */
        const finished = run.result !== null &&
                         run.result.distance <= run.best;
        const found = finished ? run.result.found : run.nearest;
        const near = finished ? run.result.distance : run.best;

        if (found === '')
        {
            $('evolvestate').textContent = why || 'the search found nothing';
            buttons();
            return;
        }

        /* Onto the piece it was for, once a load under way has finished
           -- and not onto another one that load brought. */
        while (loading())
            await new Promise((r) => setTimeout(r, 100));

        if (evolving !== run)
            return;

        const failed = await putFound(found, false);

        $('evolvestate').textContent = failed !== '' ? failed
            : `${run.started?.toFixed(2) ?? '?'} dB from the target, now ` +
              `${near.toFixed(2)}: playing on ${what}; Keep or Discard`;
        buttons();
        draw();
    }

    $('evolvego').addEventListener('click', () =>
    {
        start().catch((e) =>
        {
            $('evolvestate').textContent = e.message;
            buttons();
        });
    });
    $('evolvestop').addEventListener('click', () =>
    {
        if (evolving === null || evolving.done)
            return;

        evolving.cancel();
        end(evolving, 'stopped');
    });
    $('evolvehear').addEventListener('click', async () =>
    {
        if (evolving?.nearest)
        {
            const why = await putFound(evolving.nearest, true);

            if (why !== '')
                $('evolvestate').textContent = why;
        }
    });
    $('evolvekeep').addEventListener('click', async () =>
    {
        /* Left to keep or discard if the keeping is refused. */
        if (await keep(what, graph))
        {
            graph = '';
            buttons();
        }
    });
    $('evolvedrop').addEventListener('click', async () =>
    {
        const why = await putFound('', false);

        graph = '';
        $('evolvestate').textContent = why === '' ? 'put back as it was'
                                                  : why;
        buttons();
    });
    $('evolvetoward').addEventListener('change', () =>
    {
        if ($('evolvetoward').value === 'A recording...')
            $('evolvesound').click();
    });
    $('evolvesound').addEventListener('change', async () =>
    {
        const file = $('evolvesound').files[0];

        sound = file === undefined ? null
            : { name: `evolve/${file.name}`,
                bytes: new Uint8Array(await file.arrayBuffer()) };
        $('evolvestate').textContent = sound === null ? ''
            : `toward ${file.name}`;
    });

    return { show, buttons };
}
