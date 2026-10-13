/*
 * Copyright (C) 2004-2026 The thinksynth authors
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
   the screen. 24 is the smallest target WCAG 2.5.8 allows. */
const ROW = 24;
const MIN_H = 26;
const MAX_H = 240;

/* Longer than an edit from the keys takes to reach the grid's cells,
   beyond the lead it is stamped with. */
const LANDS_MS = 2000;

/* thcInputType. */
const PRESS = 0, DRAG = 1, RELEASE = 2;

/* Where the pointer is in the picture's own coordinates, which are the
   canvas's CSS pixels: the element is exactly as big as the draw it was
   given, so there is no arithmetic here at all. */
const at = (canvas, e) =>
{
    const rect = canvas.getBoundingClientRect();

    return { x: e.clientX - rect.left, y: e.clientY - rect.top };
};

/* A stage's picture on `canvas', as a control: each press, drag and
 * release handed to `send(kind, where, button)'. Returns what ends a
 * gesture in progress, for a rebuild that takes the canvas out from under
 * a drag -- and the capture with it, told to the document rather than
 * here.
 *
 * The pointer whose press this canvas took, and the button it came down
 * with, are held between a press and its release.
 *
 * Only a drag belonging to that pointer is an edit. "A button is down
 * somewhere" was the test before and it is not the same thing: a drag
 * begun on the track above, or anywhere on the page outside one, goes on
 * reporting moves while it crosses this canvas, and every one of them
 * painted. The composer canvas has always gated its drags this way -- it
 * feeds the plugin between a press it took and the release that ends it,
 * and nothing in between that it did not start -- and a gesture is a
 * command that travels, so the gate belongs on the sender.
 *
 * The button is the press's rather than whichever one the move reports,
 * for the reason ComposerCanvas gives: a release naming a different button
 * than its press is a pair no plugin can match up. A move reports none at
 * all.
 */
export function gestureOn (canvas, send)
{
    let held = null;
    let button = 1;
    let where = { x: 0, y: 0 };

    const tell = (kind, e) =>
    {
        if (e !== undefined)
            where = at(canvas, e);

        send(kind, where, button);
    };

    canvas.addEventListener('pointerdown', (e) =>
    {
        held = e.pointerId;
        button = e.button + 1;

        canvas.setPointerCapture(e.pointerId);
        tell(PRESS, e);
        e.preventDefault();
    });

    canvas.addEventListener('pointermove', (e) =>
    {
        if (e.pointerId === held)
            tell(DRAG, e);
    });

    const ended = (e) =>
    {
        if (e.pointerId !== held)
            return;

        held = null;

        if (canvas.hasPointerCapture(e.pointerId))
            canvas.releasePointerCapture(e.pointerId);

        tell(RELEASE, e);
    };

    canvas.addEventListener('pointerup', ended);

    /* A gesture the browser took away -- a capture lost, a touch
       cancelled, a scroll the page decided it was after all. It still has
       to end, or the plugin is left holding a drag that the next press
       arrives in the middle of. */
    canvas.addEventListener('pointercancel', ended);
    canvas.addEventListener('lostpointercapture', ended);

    /* The secondary button erases, so the menu that would otherwise open
       over the picture has to not. */
    canvas.addEventListener('contextmenu', (e) => e.preventDefault());

    return () =>
    {
        if (held === null)
            return;

        held = null;
        tell(RELEASE);
    };
}

/* What a cell of a grid is, as the `cells' param writes it: off, a note,
   an accent, and a tie that holds the note before it on. */
const SAID = { '.': 'empty', 'x': 'note', 'X': 'accent', '-': 'held' };

/* `cells' as rows of characters, the top row first, as the param has it. */
const gridOf = (text, rows, steps) =>
{
    const read = (text ?? '').replace(/[| ]/g, '').split('/');

    return Array.from({ length: rows }, (_, r) =>
        Array.from({ length: steps }, (_, s) => read[r]?.[s] ?? '.'));
};

/* The pitch of each row, top first, from the grid's `notes' as the grid
   reads it (grid.cpp, parseLadder: numbers, only on the MIDI range, and
   middle C for none), climbing from the bottom an octave each time round
   it (noteOf). null for a row above the range, which plays nothing. */
const pitchesOf = (text, rows) =>
{
    const ladder = [];

    for (const t of (text ?? '').split(','))
    {
        const n = parseInt(t, 10);

        if (Number.isNaN(n))
            break;

        if (n >= 0 && n <= 127)
            ladder.push(n);
    }

    if (ladder.length === 0)
        ladder.push(60);

    return Array.from({ length: rows }, (_, r) =>
    {
        const y = rows - 1 - r;
        const p = ladder[y % ladder.length] + 12 * Math.floor(y / ladder.length);

        return p <= 127 ? p : null;
    });
};

/* A pitch as it is said: in flats, as a piece spells them
   (thcGenLoader::noteName), and "E flat 4", which a reader says right,
   rather than "Eb4". */
const FLATS = ['C', 'D flat ', 'D', 'E flat ', 'E', 'F', 'G flat ', 'G',
               'A flat ', 'A', 'B flat ', 'B'];

const spoken = (p) => FLATS[p % 12] + (Math.floor(p / 12) - 1);

export function createSeqView ({ root = document, toMirror, onGesture,
                                 describeChannel = () => '',
                                 chooserFor = () => null,
                                 onMeasure = () => {},
                                 leadMs = () => 0 })
{
    const $ = (id) => root.getElementById(id);

    const box = $('tracks');
    const hint = $('seqhint');

    /* What a track's keys say: where the cursor is and what is there,
       after a move or an edit. Out of sight; a screen reader's. */
    const told = document.createElement('p');

    told.className = 'unseen';
    told.setAttribute('role', 'status');

    /* How a track's keys go, for every track to point at. */
    const help = document.createElement('p');

    help.className = 'unseen';
    help.id = 'seqkeys';
    help.textContent = 'Arrows move between cells, Home and End along ' +
        'the row. Enter or Space puts a note in a cell, accents it, or ' +
        'clears it; Delete erases it. Shift with Right or Left holds a ' +
        'note a step longer or shorter.';
    box.after(told, help);

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

    /* When the heights were last asked about.
     *
     * A track is as tall as its grid has rows, and `rows' is a param --
     * so it moves. The page sets it when the instrument under a track
     * turns out to ignore the note it is sent; in a room a peer sets it,
     * or a piece knob does. Asked rather than remembered, like everything
     * else the module is the authority on, and asked slowly: a height is
     * not a frame, and four tracks twice a second is nothing beside the
     * pictures. */
    let measured = 0;

    const MEASURE_MS = 500;

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
                               name: chain.name, label: stage.label,
                               channel: chain.channel,
                               rows: 1, rowsParam: -1,
                               steps: 16, grid: [[]], pitches: [null],
                               cursor: { r: 0, s: 0 },
                               text: '', landsBy: 0,
                               canvas: null, ctx: null, mark: null,
                               w: 0, h: 0, dpr: 0 });

        return out;
    };

    const build = (chains) =>
    {
        for (const track of tracks)
            track.drop?.();

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
            const frame = document.createElement('div');
            const mark = document.createElement('div');

            canvas.className = 'trackgrid';
            canvas.tabIndex = 0;

            /* Its own keys, which a screen reader passes through, and the
               track by its number and what is on it. */
            canvas.setAttribute('role', 'application');
            canvas.ariaLabel = `Track ${tracks.indexOf(track) + 1}: ` +
                               track.name;
            canvas.setAttribute('aria-describedby', help.id);

            /* The cell the keys are on, drawn over the picture while the
               track has the focus. */
            frame.className = 'trackframe';
            mark.className = 'trackcursor';
            frame.append(canvas, mark);
            track.mark = mark;

            /* A canvas with no size is 300x150, and at `width: 100%' that
               ratio made each track half the pane's width tall for the
               frame before the first fit. */
            canvas.style.height = `${MIN_H}px`;

            track.canvas = canvas;
            track.ctx = canvas.getContext('2d');

            listen(track);
            keys(track);

            row.append(head, frame);
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

    /* A finger taps and has no other button to erase with. */
    const touch = matchMedia('(pointer: coarse)').matches;
    const tap = touch ? 'Tap' : 'Click';

    const say = () =>
    {
        hint.textContent = tracks.length === 0
            ? 'This piece has no grid tracks. Open a piece with ' +
              'gen::grid stages in it -- Scratch is five of them -- and ' +
              'they appear here.'
            : `${tap} a cell for a note, again to accent it, again to ` +
              'clear it. Drag from a note to the right to hold it over ' +
              'the steps you cover, and back to shorten it. Drag across ' +
              'empty cells to draw a run of notes' +
              (touch ? '. '
                     : ', and use the other button to erase. Or Tab to ' +
                       'a track and use the arrows, Enter and Delete. ') +
              'What you draw goes out as a command and arrives at its ' +
              'time, here as on every peer.';
    };

    /* ---- the pointer ---- */

    /* By name as well as by index, for a room: an edit stamped before the
       gesture may have moved the stage (namedStage). */
    const listen = (track) =>
    {
        track.drop = gestureOn(track.canvas, (kind, where, button) =>
        {
            /* What the pointer does is not foretold: the frames say it. */
            track.landsBy = 0;
            onGesture({
                chain: track.chain, stage: track.stage,
                chainName: track.name, stageName: track.label, kind,
                ...where,
                w: track.w, h: track.h,
                button,
            });
        });
    };

    /* ---- the keys ----
     *
     * A cursor on a cell, and a gesture at that cell's middle for each
     * key, the same press and release a pointer sends: a command, so it
     * reaches the plugin and every peer as a click does, and the grid's
     * own rules decide what it does (grid.cpp: a press on an empty cell
     * paints a note, a tap on a note accents it, on an accent clears it;
     * the other button erases; a drag from a note along its row holds it).
     * What the cell then is, is said from those rules rather than waited
     * for, since the edit lands at its time and not at once. */
    const middle = (track, r, s) =>
        ({ x: (s + 0.5) * track.w / track.steps,
           y: (r + 0.5) * track.h / track.rows });

    /* A press at `from', a drag through the steps in `through', and a
       release at the last of them. */
    const gesture = (track, button, r, from, through = []) =>
    {
        const send = (kind, s) => onGesture({
            chain: track.chain, stage: track.stage,
            chainName: track.name, stageName: track.label, kind,
            ...middle(track, r, s), w: track.w, h: track.h, button });

        send(PRESS, from);

        for (const s of through)
            send(DRAG, s);

        send(RELEASE, through.at(-1) ?? from);

        /* Until it has landed, what is said is the grid as it will be. */
        track.landsBy = performance.now() + leadMs() + LANDS_MS;
    };

    /* How many steps the note at a cell holds: itself and its ties. */
    const held = (row, s) =>
    {
        let n = 1;

        while (row[s + n] === '-')
            n++;

        return n;
    };

    const tell = (track) =>
    {
        const { r, s } = track.cursor;
        const row = track.grid[r] ?? [];
        const c = row[s] ?? '.';
        const pitch = track.pitches[r];
        const n = c === 'x' || c === 'X' ? held(row, s) : 1;

        told.textContent =
            `${pitch === null ? `row ${r + 1}` : spoken(pitch)}, ` +
            `step ${s + 1}: ${SAID[c] ?? 'empty'}` +
            (n > 1 ? `, held ${n} steps` : '');
    };

    const place = (track) =>
    {
        const { r, s } = track.cursor;
        const w = track.w / track.steps;
        const h = track.h / track.rows;

        Object.assign(track.mark.style, {
            left: `${s * w}px`, top: `${r * h}px`,
            width: `${w}px`, height: `${h}px` });
    };

    const keys = (track) =>
        track.canvas.addEventListener('keydown', (e) =>
    {
        if (e.altKey || e.ctrlKey || e.metaKey)
            return;

        const at = track.cursor;

        /* A row before the cells have been read is an empty one. */
        const row = track.grid[at.r] ??=
            Array.from({ length: track.steps }, () => '.');
        const c = row[at.s] ?? '.';
        const note = c === 'x' || c === 'X';
        const go = (r, s) =>
        {
            at.r = Math.max(0, Math.min(track.rows - 1, r));
            at.s = Math.max(0, Math.min(track.steps - 1, s));
        };

        /* Shift is a hold only on Left and Right; on anything else it is
           the key alone, as on the desktop. */
        const hold = e.shiftKey &&
                     (e.key === 'ArrowLeft' || e.key === 'ArrowRight');

        switch (hold ? `Shift+${e.key}` : e.key)
        {
            case 'ArrowLeft':  go(at.r, at.s - 1); break;
            case 'ArrowRight': go(at.r, at.s + 1); break;
            case 'ArrowUp':    go(at.r - 1, at.s); break;
            case 'ArrowDown':  go(at.r + 1, at.s); break;
            case 'Home':       go(at.r, 0); break;
            case 'End':        go(at.r, track.steps - 1); break;

            case 'Enter':
            case ' ':
                gesture(track, 1, at.r, at.s);
                row[at.s] = { '.': 'x', '-': 'x', 'x': 'X' }[c] ?? '.';

                if (c === 'X')
                    for (let s = at.s + 1; row[s] === '-'; s++)
                        row[s] = '.';
                break;

            case 'Delete':
            case 'Backspace':
                gesture(track, 3, at.r, at.s);
                row[at.s] = '.';

                for (let s = at.s + 1; note && row[s] === '-'; s++)
                    row[s] = '.';
                break;

            /* A note held over the next step, or let go of its last; up
               to the next note, which a hold stops at. */
            case 'Shift+ArrowRight':
            case 'Shift+ArrowLeft':
            {
                if (!note)
                    return;

                const n = held(row, at.s);
                const to = e.key === 'ArrowRight' ? n : n - 2;

                if (to < 0 || at.s + to >= track.steps ||
                    (to >= n && row[at.s + to] !== '.'))
                    return;

                /* Back to one step is a drag that leaves the note and
                   comes back to it: a press and release on one note
                   without leaving it is a tap, which accents it. */
                gesture(track, 1, at.r, at.s,
                        to === 0 ? [at.s + 1, at.s] : [at.s + to]);

                for (let s = 1; s < Math.max(n, to + 1); s++)
                    row[at.s + s] = s <= to ? '-' : '.';
                break;
            }

            default:
                return;
        }

        e.preventDefault();
        place(track);
        tell(track);
    });

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
        place(track);
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

        const now = performance.now();

        if (now - measured >= MEASURE_MS)
        {
            measured = now;

            for (const track of tracks)
                toMirror({ type: 'stageparams', chain: track.chain,
                           stage: track.stage });
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

            /* The same piece after an edit, whose indices may have moved. */
            case 'chains':
                build(m.chains);
                return true;

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

                /* By index, because the index is what a command naming
                   this param has to carry: the list is in the plugin's
                   own order (mirror.js). */
                const p = m.params.findIndex((x) => x.name === 'rows');
                const param = (name) => m.params.find((x) => x.name === name);

                track.rowsParam = p;
                /* Cut and held in range as the grid does (grid.cpp), so a
                   gesture's columns and rows are the grid's. */
                track.rows = p < 0 ? 1
                    : Math.max(1, Math.min(32, Math.trunc(m.params[p].value)));
                track.steps = Math.max(1, Math.min(64,
                    Math.trunc(param('steps')?.value ?? 16)));
                track.pitches = pitchesOf(param('notes')?.text, track.rows);

                /* The cells as written, except while an edit from the keys
                   has not landed: until then, only a frame that shows the
                   grid said. Frames lag the edits, so one that does not
                   can be from before any of them, or between two -- and
                   one that does can be too, after edits that came back
                   to where they started, so a match does not end it. A
                   change of size is the grid's own, and always taken. */
                const text = param('cells')?.text ?? '';
                const grid = gridOf(text, track.rows, track.steps);
                const said = (g) => g.map((row) => row.join('')).join('/');

                if (performance.now() >= track.landsBy)
                    track.landsBy = 0;

                if (text !== track.text || track.landsBy > 0)
                {
                    const shape = track.grid.length === track.rows &&
                                  track.grid[0].length === track.steps;

                    if (track.landsBy === 0 || !shape ||
                        said(grid) === said(track.grid))
                    {
                        track.text = text;
                        track.grid = grid;
                    }
                }

                track.cursor.r = Math.min(track.cursor.r, track.rows - 1);
                track.cursor.s = Math.min(track.cursor.s, track.steps - 1);
                place(track);

                onMeasure();
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

        /* What the pane ended up showing -- for the harnesses, and for
           the page, which decides how tall a track's grid ought to be
           from what is playing it and needs to know what it is now. */
        tracks: () => tracks.map((t) => ({ chain: t.chain, stage: t.stage,
                                           channel: t.channel,
                                           rows: t.rows,
                                           rowsParam: t.rowsParam })),
    };
}
