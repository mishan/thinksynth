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
 * canvasview.js -- the page's half of a canvas whose drawing is in wasm.
 *
 * The shell, and nothing but: an element, a scroller, the pointer, the
 * keyboard, the wheel, and replaying the list that comes back. Everything
 * that decides what the canvas looks like or what a click means is the
 * C++ the desktop runs -- one drawing, one behaviour, two shells.
 *
 * It does not care which canvas it is showing or where the drawing is
 * done. `send' takes a message somewhere -- the mirror worker for the
 * composer view, the page's own instance for the node editor later -- and
 * `frame' is handed whatever comes back. So the two canvases share this
 * file, as they share CanvasContent on the other side.
 *
 * The element is as big as the drawing and the div around it scrolls,
 * which is what a Gtk scrolled window does to the widget on the desktop;
 * pointer coordinates relative to the element are therefore the shell
 * pixels the content class expects, with no arithmetic here at all. The
 * one number this page does own is the device pixel ratio: the element is
 * sized in CSS pixels, its backing store is that times the ratio, and the
 * ratio is the first transform the replayer applies.
 *
 * Three canvases use it now: the composer view, the node editor and the
 * piano roll. What the roll asks of it that the other two do not is the
 * bare wheel (`wheelZooms'), because it is the one whose drawing is always
 * exactly its view and therefore has nothing to scroll.
 *
 * `fitOnShow' is whether opening the view scales the drawing into it. The
 * composer view does, to its width. The node editor does not: a patch is
 * wide -- ts1 is 1888 pixels of graph -- so any fit into a page-width box
 * is a halving or worse, and half-size node labels are a picture of a
 * patch rather than a patch you can work on. It opens at 1:1 and has a
 * Fit button for the other question.
 */

import { replay } from './replay.js';

/* A drag sends at most one motion per animation frame. A pointer moves
   faster than a mesh wants, and a knob's slider already produces about
   this rate. */
/* CanvasContent::Mod bits: Shift, and Ctrl or Cmd. */
export const modsOf = (e) =>
    (e.shiftKey ? 1 : 0) | (e.ctrlKey || e.metaKey ? 2 : 0);

/* `takes' says which keys the content is sent; the rest stay the page's,
   arrow scrolling and Ctrl+A among them. */
export function createCanvasView ({ scroller, canvas, send,
                                    fitOnShow = true,
                                    wheelZooms = false,
                                    takes = (e) => e.key === 'Escape',
                                    pans = false,
                                    hit = () => true,
                                    onFrame = () => {} })
{
    const ctx = canvas.getContext('2d');

    let width = 0, height = 0, dpr = 0;
    let pending = null;         /* a motion waiting for the next frame */
    let running = false;
    let visible = false;

    const ratio = () => globalThis.devicePixelRatio || 1;

    /* Where the pointer is, in the element's own CSS pixels. */
    const at = (e) =>
    {
        const box = canvas.getBoundingClientRect();

        return { x: e.clientX - box.left, y: e.clientY - box.top };
    };

    /* What the content is allowed to lay out against: the part of the
       drawing that can actually be seen. The enlarged stage follows it,
       so this goes over on every scroll and every resize. */
    const viewport = (fit = false) =>
        send({ type: 'view', x: scroller.scrollLeft, y: scroller.scrollTop,
               w: scroller.clientWidth, h: scroller.clientHeight,
               dpr: ratio(), fit });

    /* ---- what comes back ---- */

    /* One drawn frame from wherever the drawing happens: the list, the
       strings it indexes, the surfaces it blits, and how big the drawing
       is now. */
    const frame = (m) =>
    {
        if (m.width !== width || m.height !== height || ratio() !== dpr)
        {
            width = m.width;
            height = m.height;
            dpr = ratio();

            /* The element in CSS pixels, its backing store in device
               ones. Setting either clears the canvas, so this happens
               before the replay and not after. */
            canvas.style.width = `${width}px`;
            canvas.style.height = `${height}px`;
            canvas.width = Math.round(width * dpr);
            canvas.height = Math.round(height * dpr);
        }

        replay(ctx, m.ops, m.strings, m.surfaces,
               { width: m.w, height: m.h, dpr });

        onFrame(m);
    };

    /* ---- the pointer, the keys and the wheel ---- */

    /* The click count, kept here: a pointer event's `detail' is 0 by
     * specification -- the count belongs to mouse events, and
     * `pointerdown' carries none -- and a content class that answers a
     * double-click (the roll goes back to live, the composer canvas
     * enlarges a stage) has to hear of one. A press soon enough after the
     * last and near enough to it is its second, as the desktop's click
     * gesture counts: the same presses on both platforms, where a
     * `dblclick' listener used to add a third one of its own after the
     * second release. */
    const DOUBLE_MS = 400, DOUBLE_PX = 5;
    let last = { t: -Infinity, x: 0, y: 0, n: 0 };

    /* The pointer a gesture is made with. A second finger landing while
       one is down is not a second gesture: the content has one press
       and one drag at a time, and a second press would restart the
       drag from wherever that finger is. */
    let pointer = null;

    /* The press the content hears, from wherever it came. */
    const press = (id, p, button, mods, t) =>
    {
        const again = t - last.t < DOUBLE_MS &&
                      Math.abs(p.x - last.x) <= DOUBLE_PX &&
                      Math.abs(p.y - last.y) <= DOUBLE_PX;

        pointer = id;
        last = { t, x: p.x, y: p.y, n: again ? last.n + 1 : 1 };
        send({ type: 'press', ...p, button, nPress: last.n, mods });
    };

    /* ---- the view dragged ----
     *
     * With `pans', the scroller is moved here and the content never hears
     * of it, which is the shell's half of the desktop's MOD_PAN: the
     * middle button, or Space held with the main one, drags the view. On
     * a touch screen a finger on nothing does (`hit' says what is under
     * it); held still a moment first, it draws the band a mouse drag on
     * nothing draws; two fingers pinch. A finger on a box is a press like
     * any other. */
    const HOLD_MS = 500, STILL_PX = 8;
    let space = false;
    let pan = null;             /* the drag, while the view is dragged */
    let pinch = 0;              /* the fingers' last distance, pinching */
    const fingers = new Map();  /* touches down, by pointer, for a pinch */

    const apart = () =>
    {
        const [a, b] = [...fingers.values()];

        return Math.hypot(a.x - b.x, a.y - b.y);
    };

    const endPan = () =>
    {
        clearTimeout(pan?.hold);
        pan = null;
        canvas.style.cursor = space ? 'grab' : '';
    };

    canvas.addEventListener('pointerdown', (e) =>
    {
        if (pans && e.pointerType === 'touch')
        {
            fingers.set(e.pointerId, { x: e.clientX, y: e.clientY });

            /* A second finger: a pinch, unless the first is already
               pressing on something, which keeps its one gesture. */
            if (fingers.size === 2 && pointer === null)
            {
                endPan();
                pinch = apart();
                e.preventDefault();
                return;
            }
        }

        if (pointer !== null || pan !== null || pinch > 0)
            return;

        const p = at(e);
        const touch = e.pointerType === 'touch';

        canvas.setPointerCapture(e.pointerId);
        canvas.focus();
        e.preventDefault();

        if (pans && (e.button === 1 || (space && e.button === 0) ||
                     (touch && !hit(p.x, p.y))))
        {
            pan = { id: e.pointerId, x: e.clientX, y: e.clientY,
                    left: scroller.scrollLeft, top: scroller.scrollTop,
                    moved: !touch, at: p, t: e.timeStamp };
            canvas.style.cursor = 'grabbing';

            if (touch)
                pan.hold = setTimeout(() =>
                {
                    if (pan === null || pan.moved)
                        return;

                    const { id, at: where, t } = pan;

                    endPan();
                    press(id, where, 1, 0, t);
                }, HOLD_MS);

            return;
        }

        press(e.pointerId, p, e.button + 1, modsOf(e), e.timeStamp);
    });

    canvas.addEventListener('pointermove', (e) =>
    {
        if (fingers.has(e.pointerId))
            fingers.set(e.pointerId, { x: e.clientX, y: e.clientY });

        if (pinch > 0 && fingers.size === 2)
        {
            const now = apart();

            if (now > 0)
                send({ type: 'zoomBy', by: now / pinch });

            pinch = now;
            return;
        }

        if (pan !== null && e.pointerId === pan.id)
        {
            const dx = e.clientX - pan.x, dy = e.clientY - pan.y;

            pan.moved ||= Math.hypot(dx, dy) > STILL_PX;

            if (pan.moved)
            {
                scroller.scrollLeft = pan.left - dx;
                scroller.scrollTop = pan.top - dy;
            }

            return;
        }

        if (pointer !== null && e.pointerId !== pointer)
            return;

        /* Held for the next animation frame, and the last one wins: what
           matters about a drag is where it is now. */
        pending = at(e);
    });

    const release = (e) =>
    {
        fingers.delete(e.pointerId);

        if (pinch > 0)
        {
            if (fingers.size < 2)
                pinch = 0;

            return;
        }

        /* A touch on nothing let go where it landed is a tap: what a click
           on nothing does, which is to let go of the selection. */
        if (pan !== null && e.pointerId === pan.id)
        {
            const { id, at: where, t, moved } = pan;

            endPan();

            if (!moved)
            {
                press(id, where, 1, 0, t);
                pointer = null;
                send({ type: 'release', ...where, button: 1 });
            }

            return;
        }

        if (e.pointerId !== pointer)
            return;

        pointer = null;

        if (pending !== null)
        {
            send({ type: 'motion', ...pending });
            pending = null;
        }

        send({ type: 'release', ...at(e), button: e.button + 1 });
    };

    canvas.addEventListener('pointerup', release);
    canvas.addEventListener('pointercancel', release);

    canvas.addEventListener('keydown', (e) =>
    {
        if (pans && e.key === ' ')
        {
            space = true;
            canvas.style.cursor = pan !== null ? 'grabbing' : 'grab';
            e.preventDefault();
            return;
        }

        if (!takes(e))
            return;

        /* A content that says it did not take the key leaves it to the
           page: Escape with nothing to let go of gives the keyboard back. */
        if (send({ type: 'key', key: e.key, code: e.code,
                   mods: modsOf(e) }) !== false)
            e.preventDefault();
    });

    const spaceUp = () =>
    {
        space = false;
        canvas.style.cursor = pan !== null ? 'grabbing' : '';
    };

    canvas.addEventListener('keyup', (e) =>
    {
        if (e.key === ' ')
            spaceUp();
    });
    canvas.addEventListener('blur', spaceUp);

    /* Ctrl+wheel zooms, as it does on the desktop; a plain wheel scrolls,
       which is the scroller's own business and not ours.
     *
       Unless there is nothing to scroll. The piano roll's drawing is
       always exactly its view, so a bare wheel over it would scroll the
       page out from under the thing being read -- and the desktop's roll
       has always taken a bare wheel, for the same reason. `wheelZooms'
       is that one difference, and the message is the same either way:
       the content is told how much bigger to draw and decides what that
       means to it. */
    canvas.addEventListener('wheel', (e) =>
    {
        if (!wheelZooms && !e.ctrlKey)
            return;

        send({ type: 'zoomBy', by: e.deltaY < 0 ? 1.1 : 1 / 1.1 });
        e.preventDefault();
    }, { passive: false });

    scroller.addEventListener('scroll', () => viewport());

    const observer = new ResizeObserver(() => viewport());

    observer.observe(scroller);

    /* ---- the frame loop ---- */

    /* A frame is asked for on every animation frame the view is on
       screen, and none at all when it is not: what a composer draws is
       its state, which moves whether or not anybody touched the canvas,
       and a hidden tab that went on asking would cost a piece's worth of
       drawing for nobody. */
    const tick = () =>
    {
        if (!running)
            return;

        if (pending !== null)
        {
            send({ type: 'motion', ...pending });
            pending = null;
        }

        send({ type: 'draw' });
        requestAnimationFrame(tick);
    };

    const show = (on) =>
    {
        if (on === visible)
            return;

        visible = on;

        if (!on)
        {
            running = false;
            return;
        }

        viewport(fitOnShow);

        if (!running)
        {
            running = true;
            requestAnimationFrame(tick);
        }
    };

    return { frame, show, viewport, visible: () => visible };
}
