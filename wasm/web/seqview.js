/*
 * Copyright (C) 2004-2026 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * seqview.js -- the piece as tracks, for clicking.
 *
 * The composer view draws the whole piece: chains as rows of boxes, the
 * wires between them, a picture inside every stage that has one. It is
 * the right picture of a piece and the wrong one for the job somebody is
 * doing when they want to hear a pattern -- there they are looking at one
 * kind of stage, `gen::grid', and they want all of them at once, big
 * enough to hit, with nothing else on the screen.
 *
 * So this shows exactly that: one row per grid in the piece, each one its
 * own stage picture at the pane's full width. No chains, no stages, no
 * sinks, no words from the composer at all -- a row is a track, a column
 * is a step, and the numbers down the side are the channels they are
 * heard on.
 *
 * NOTHING HERE DRAWS. The picture is the plugin's own composer_draw,
 * recorded by cairo-canvas2d in the mirror worker and replayed here, the
 * same list the composer canvas replays -- so the grid a person clicks on
 * this page is pixel for pixel the one the desktop draws, and stays that
 * way without anybody keeping two drawings in step.
 *
 * AND NOTHING HERE DECIDES WHAT A CLICK MEANS. A press, a drag and a
 * release go out as `input' commands, stamped and applied at their time
 * on every instance that is composing -- the worklet, the mirror, and in
 * a room every peer. This view does no hit-testing beyond which track was
 * hit: where in the picture the pointer landed is handed over in the
 * coordinates the draw was given, and the plugin inverts its own
 * arithmetic. That is what keeps one tape.
 */

import { replay } from './replay.js';

/* A row of the grid, in CSS pixels, and what a track may be at its
   shortest and tallest. A drum track is one row and a keys track is
   eight; both want to be hittable without either running the pane off
   the screen. */
const ROW = 18;
const MIN_H = 26;
const MAX_H = 240;

export function createSeqView ({ root = document, toMirror, onGesture,
                                 describeChannel = () => '',
                                 chooserFor = () => null })
{
    const $ = (id) => root.getElementById(id);

    const box = $('tracks');
    const hint = $('seqhint');

    /* One per grid stage in the piece, in the piece's own order. */
    let tracks = [];
    let visible = false;
    let frameId = null;

    /* How many stagedraws are out and not back. The mirror answers in
       order, and asking for another round before the last one landed
       would queue frames a worker is already behind on -- a pane of
       eight tracks at sixty frames a second is four hundred and eighty
       messages, and the playhead moves eight times a second. */
    let waiting = 0;

    const ratio = () => globalThis.devicePixelRatio || 1;

    /* ---- the rows ---- */

    /* What the piece has that this pane can show: every `gen::grid', with
       the chain it is in and the channel that chain is heard on.
     *
     * By plugin name, and only that one. A pane that showed every stage
     * with a picture would be the composer canvas with the wires rubbed
     * out; what makes this view worth having is that every row in it is
     * the same kind of thing and can be read the same way. */
    const gridsIn = (chains) =>
    {
        const out = [];

        for (const chain of chains ?? [])
            for (const stage of chain.stages)
                if (stage.name === 'grid')
                    out.push({ chain: chain.chain, stage: stage.stage,
                               name: chain.name, channel: chain.channel,
                               rows: 1, canvas: null, ctx: null,
                               w: 0, h: 0, dpr: 0 });

        return out;
    };

    const build = (chains) =>
    {
        tracks = gridsIn(chains);
        box.replaceChildren();

        for (const track of tracks)
        {
            const row = document.createElement('div');
            const head = document.createElement('div');
            const num = document.createElement('span');
            const what = document.createElement('span');

            row.className = 'track';
            head.className = 'row trackhead';

            /* The file's numbering, 1-16, which is what the channels
               list and the keys' selector say. */
            num.className = 'chan';
            num.textContent = track.channel >= 0
                ? String(track.channel + 1) : '-';

            what.className = 'what';

            head.append(num, what);
            dress(track, head);

            const canvas = document.createElement('canvas');

            canvas.className = 'trackgrid';
            canvas.tabIndex = 0;

            track.canvas = canvas;
            track.ctx = canvas.getContext('2d');

            listen(track);

            row.append(head, canvas);
            box.append(row);
        }

        say();

        /* Their heights come from each grid's own `rows', which is the
           plugin's answer and not this page's to guess. */
        for (const track of tracks)
            toMirror({ type: 'stageparams', chain: track.chain,
                       stage: track.stage });
    };

    /* What plays this track, in the heading: the menu that chooses it
     * where the page is allowed to choose, and the name of what is there
     * where it is not.
     *
     * Which is the same rule the channels list follows, for the same
     * reason -- a channel a piece filled is the piece's, and offering to
     * replace its instrument would be the page overriding the file. The
     * sequence mode's own piece declares no instruments, so there every
     * track has a menu, which is the whole point of that mode. */
    const dress = (track, head) =>
    {
        const menu = chooserFor(track.channel);

        if (menu !== null)
        {
            menu.className = 'trackpick';
            head.append(menu);
            return;
        }

        head.querySelector('.what').textContent =
            describeChannel(track.channel) || track.name;
    };

    const say = () =>
    {
        hint.textContent = tracks.length === 0
            ? 'This piece has no grid tracks. Open a piece with ' +
              'gen::grid stages in it -- Scratch is five of them -- and ' +
              'they appear here.'
            : 'Click a cell for a note, again to accent it, again to ' +
              'clear it. Drag from a note to the right to hold it over ' +
              'the steps you cover, and back to shorten it. Drag across ' +
              'empty cells to draw a run of notes, and use the other ' +
              'button to erase. What you click goes out as a command and ' +
              'arrives at its time, here as on every peer.';
    };

    /* ---- the pointer ---- */

    /* Where the pointer is in the picture's own coordinates, which are
       the canvas's CSS pixels: the element is exactly as big as the draw
       it was given, so there is no arithmetic here at all. */
    const at = (canvas, e) =>
    {
        const rect = canvas.getBoundingClientRect();

        return { x: e.clientX - rect.left, y: e.clientY - rect.top };
    };

    /* thcInputType. */
    const PRESS = 0, DRAG = 1, RELEASE = 2;

    const listen = (track) =>
    {
        const canvas = track.canvas;

        /* The pointer whose press this track took, and the button it
         * came down with. Null between gestures.
         *
         * Only a drag belonging to that pointer is an edit. "A button is
         * down somewhere" was the test before and it is not the same
         * thing: a drag begun on the track above, or anywhere on the
         * page outside one, goes on reporting moves while it crosses
         * this canvas, and every one of them painted. The composer
         * canvas has always gated its drags this way -- it feeds the
         * plugin between a press it took and the release that ends it,
         * and nothing in between that it did not start -- and a gesture
         * is a command that travels, so the gate belongs on the sender.
         *
         * The button is the press's rather than whichever one the move
         * reports, for the reason ComposerCanvas gives: a release naming
         * a different button than its press is a pair no plugin can
         * match up. A move reports none at all.
         */
        let held = null;
        let button = 1;

        const send = (kind, e) => onGesture({
            chain: track.chain, stage: track.stage, kind,
            ...at(canvas, e),
            w: track.w, h: track.h,
            button,
        });

        canvas.addEventListener('pointerdown', (e) =>
        {
            held = e.pointerId;
            button = e.button + 1;

            canvas.setPointerCapture(e.pointerId);
            send(PRESS, e);
            e.preventDefault();
        });

        canvas.addEventListener('pointermove', (e) =>
        {
            if (e.pointerId === held)
                send(DRAG, e);
        });

        const ended = (e) =>
        {
            if (e.pointerId !== held)
                return;

            held = null;

            if (canvas.hasPointerCapture(e.pointerId))
                canvas.releasePointerCapture(e.pointerId);

            send(RELEASE, e);
        };

        canvas.addEventListener('pointerup', ended);

        /* A gesture the browser took away -- a capture lost, a touch
           cancelled, a scroll the page decided it was after all. It
           still has to end, or the plugin is left holding a drag that
           the next press arrives in the middle of. */
        canvas.addEventListener('pointercancel', ended);

        /* The secondary button erases, so the menu that would otherwise
           open over the track has to not. */
        canvas.addEventListener('contextmenu', (e) => e.preventDefault());
    };

    /* ---- frames ---- */

    /* The size a track should be drawn at now: the pane's width, and a
       height that gives each of the grid's own rows something clickable.
       Sizing the element here rather than in CSS is what makes the
       picture's coordinates and the pointer's the same numbers. */
    const fit = (track) =>
    {
        const w = Math.max(1, Math.round(track.canvas.clientWidth));
        const h = Math.min(MAX_H, Math.max(MIN_H, track.rows * ROW));
        const dpr = ratio();

        if (w === track.w && h === track.h && dpr === track.dpr)
            return;

        track.w = w;
        track.h = h;
        track.dpr = dpr;

        /* The element in CSS pixels, its backing store in device ones.
           Setting either clears the canvas, so this happens before the
           replay and not after. */
        track.canvas.style.height = `${h}px`;
        track.canvas.width = Math.round(w * dpr);
        track.canvas.height = Math.round(h * dpr);
    };

    const ask = () =>
    {
        frameId = null;

        if (!visible)
            return;

        if (waiting === 0)
            for (const track of tracks)
            {
                fit(track);
                waiting++;
                toMirror({ type: 'stagedraw', chain: track.chain,
                           stage: track.stage, w: track.w, h: track.h,
                           dpr: track.dpr });
            }

        frameId = requestAnimationFrame(ask);
    };

    const run = (on) =>
    {
        if (on && frameId === null)
            frameId = requestAnimationFrame(ask);
        else if (!on && frameId !== null)
        {
            cancelAnimationFrame(frameId);
            frameId = null;
            waiting = 0;
        }
    };

    /* ---- what the mirror says ---- */

    const fromMirror = (m) =>
    {
        switch (m.type)
        {
            case 'piece':
                build(m.chains);
                return false;    /* the composer view wants it too */

            case 'stagedraw':
            {
                const track = tracks.find((t) => t.chain === m.chain &&
                                                 t.stage === m.stage);

                if (waiting > 0)
                    waiting--;

                /* A list drawn for a size the pane has since left is a
                   frame behind; the next one is already on its way. And
                   no list at all is a stage the mirror no longer has --
                   answered so the count above comes down, and nothing to
                   replay. */
                if (track === undefined || m.ops === null ||
                    m.w !== track.w || m.h !== track.h)
                    return true;

                replay(track.ctx, m.ops, m.strings, m.surfaces,
                       { width: m.w, height: m.h, dpr: track.dpr });
                return true;
            }

            case 'stageparams':
            {
                const track = tracks.find((t) => t.chain === m.chain &&
                                                 t.stage === m.stage);

                if (track === undefined)
                    return true;

                const rows = m.params.find((p) => p.name === 'rows');

                track.rows = Math.max(1, Math.round(rows?.value ?? 1));
                return true;
            }
        }

        return false;
    };

    return {
        fromMirror,

        /* A pane nobody is looking at asks for no frames, which is the
           whole reason a tiler pays for itself rather than costing. */
        show (on)
        {
            visible = on;
            run(on);
        },

        /* What is on a channel can change without the piece reloading --
           somebody aims one by hand -- and the rows say what plays them.
           Cheap enough to redraw every heading rather than work out
           which one moved. */
        refresh ()
        {
            const heads = box.querySelectorAll('.trackhead');

            tracks.forEach((track, i) =>
            {
                const head = heads[i];

                if (head === undefined)
                    return;

                head.querySelector('.trackpick')?.remove();
                dress(track, head);
            });
        },

        /* Whether it is asking for frames, which is what the tiler's
           claim that a hidden pane costs nothing is gated on. */
        visible: () => visible,

        /* For the harnesses: what the pane ended up showing. */
        tracks: () => tracks.map((t) => ({ chain: t.chain, stage: t.stage,
                                           channel: t.channel,
                                           rows: t.rows })),
    };
}
