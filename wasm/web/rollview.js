/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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
 * rollview.js -- the piano roll, on either page.
 *
 * The canvas is canvasview.js's and the drawing is the mirror's, which is
 * the desktop's RollCanvas compiled to wasm (src/RollCanvas.h). This is
 * the little that is particular to the roll: tagging its half of the
 * canvas-view protocol so one shell can drive two panes, and the clock.
 *
 * Both pages have it, and it is one file for the reason keyboard.js and
 * composerview.js are one file each: two copies of a thing two pages have
 * to agree on is how they stop agreeing.
 *
 * What this replaced was roll.js, which drew the worklet's tape by hand in
 * JavaScript, in colours of its own, showing thirty seconds of the past
 * and nothing else. The past is the half a tape can answer. The other half
 * -- what the piece has already decided and not yet played -- is in the
 * scheduler's pending queue, which lives in the mirror, which is why the
 * drawing moved there rather than the tape growing a field.
 *
 * The clock stays here, as it was roll.js's, but as a function of its
 * own: it belongs to no pane and has to go on ticking whether or not
 * anybody has the roll open. It reads the *worklet's* tape and not the
 * mirror's, because the mirror is stepped to wherever the worklet last
 * said it had got to and is therefore always a message behind -- which
 * would be wrong in the one place on the page a person reads a number.
 */

import { createCanvasView } from './canvasview.js';

/* The transport's time, in `el', from a tape message. Both pages have
   the same span in the same row and neither has any other use for one. */
export function showClock (el, { now, running })
{
    if (el === null || el === undefined)
        return;

    const secs = Math.max(0, now);

    el.textContent =
        `${Math.floor(secs / 60)}:` +
        `${(secs % 60).toFixed(1).padStart(4, '0')}` +
        (running ? '' : ' (stopped)');
}

export function createRollView ({ root = document, toMirror })
{
    const $ = (id) => root.getElementById(id);

    /* Every message this view sends is tagged, and the mirror routes on
       the tag: the composer view's canvas and this one speak the same
       protocol into the same worker. */
    const view = createCanvasView({
        scroller: $('roll'),
        canvas: $('rollcanvas'),
        send: (m) => toMirror({ ...m, canvas: 'roll' }),

        /* The roll's drawing is always exactly its view, so there is
           nothing to fit and nothing for a bare wheel to scroll --
           which is what lets the wheel mean time, as it does on the
           desktop. */
        fitOnShow: false,
        wheelZooms: true,
    });

    /* What the mirror last said about the view, for a harness: where the
       now-line is and whether it is still live. */
    let where = { now: 0, following: true, spanPast: 0, spanFuture: 0 };

    /* True if the message was this view's. */
    const fromMirror = (m) =>
    {
        if (m.type !== 'draw' || m.canvas !== 'roll')
            return false;

        view.frame(m);
        where = { now: m.now, following: m.following,
                  spanPast: m.spanPast, spanFuture: m.spanFuture };

        return true;
    };

    return {
        fromMirror,
        show: (on) => view.show(on),
        visible: () => view.visible(),

        /* Where the now-line is, for a harness that has just dragged on
           the roll and wants to say that it scrubbed. */
        where: () => where,
    };
}
