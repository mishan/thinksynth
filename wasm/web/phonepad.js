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
 * phonepad.js -- what the phone's page makes of its sensors and fingers,
 * and when it says so (docs/PHONE.md): no DOM, so Node can hold it to
 * what it means.
 *
 * A frame is the device `phone' (hid/thhPhone.h) whole: { id, seq, axes,
 * down }, its 12 axes tilt_x, tilt_y and each of five fingers' x and y.
 */

export const FINGERS = 5;
export const AXES = 2 + 2 * FINGERS;

/* Gravity, about. */
const G = 9.81;

/* While anything moves, and for a moment after so its last place gets
   there; and while nothing does, a heartbeat, so the page knows the
   phone is still there (thhPhone's GONE_SECONDS is four of them). */
export const BUSY_MS = 1000 / 60;
export const AFTER_MS = 200;
export const HEARTBEAT_MS = 250;

/* A tilt that moves less than this hasn't moved: a phone held still
   reads a little differently every time. */
export const STILL = 0.01;

/* Tilt in the screen's own axes, -1 to 1: right and up as the screen is
   held. `g' is accelerationIncludingGravity, x right and y up the
   device's natural portrait, z out of its face; `angle' the screen's
   turn from that, in degrees (screen.orientation.angle); `sign' -1 where
   the platform reports the opposite way. Gravity rather than Euler
   angles, which lose roll near upright. */
export function tilt (g, angle = 0, sign = 1)
{
    const a = angle * Math.PI / 180;
    const x = -sign * g.x / G, y = sign * g.y / G;
    const clamp = (v) => Math.max(-1, Math.min(1, v));

    return [clamp(x * Math.cos(a) + y * Math.sin(a)),
            clamp(y * Math.cos(a) - x * Math.sin(a))];
}

/* Fingers by the slots they take: the lowest free one as a finger
   lands, kept until it lifts or is cancelled, so a second finger never
   becomes the first. A sixth finger has no slot and plays nothing. */
export class Fingers
{
    constructor ()
    {
        this.slots = new Array(FINGERS).fill(null);     /* pointer ids */
        this.at = new Array(FINGERS).fill(null).map(() => [0, 0]);
    }

    /* `x' across and `y' up the screen, 0 to 1. The slot, or -1. */
    down (id, x, y)
    {
        const k = this.slots.indexOf(null);

        if (k < 0 || this.slots.includes(id))
            return -1;
        this.slots[k] = id;
        this.at[k] = [x, y];
        return k;
    }

    move (id, x, y)
    {
        const k = this.slots.indexOf(id);

        if (k >= 0)
            this.at[k] = [x, y];
    }

    up (id)
    {
        const k = this.slots.indexOf(id);

        if (k >= 0)
            this.slots[k] = null;
    }

    /* Every finger lifted: the page hidden, or the phone put down. */
    clear ()
    {
        this.slots.fill(null);
    }

    /* A bit for each slot with a finger in it. */
    get bits ()
    {
        return this.slots.reduce((bits, id, k) => bits | (id === null ? 0 : 1 << k), 0);
    }

    /* A frame's axes: `t' the tilt, then each slot's place, the last it
       had for one empty. */
    axes (t)
    {
        return [t[0], t[1], ...this.at.flat()];
    }
}

/* When the page sends, and on which channels: `next' is asked each time
   anything may have changed, and on a timer, with the state now, and
   answers the frame to send and whether it goes on both channels (a
   finger landing or lifting: a frame the page must not miss), or null. */
export class Sender
{
    constructor (id = (Math.random() * 2 ** 32) >>> 0)
    {
        this.id = id;
        this.seq = 0;
        this.sent = null;           /* the last frame sent */
        this.sentAt = -Infinity;
        this.changedAt = -Infinity;
    }

    next (axes, down, now)
    {
        const last = this.sent;
        const fingersMoved = last !== null && down === last.down &&
            axes.slice(2).some((v, k) => v !== last.axes[2 + k] &&
                                         (down >> (k >> 1)) & 1);
        const tilted = last !== null &&
            Math.max(Math.abs(axes[0] - last.axes[0]),
                     Math.abs(axes[1] - last.axes[1])) >= STILL;
        const landed = last === null || down !== last.down;

        if (landed || fingersMoved || tilted)
            this.changedAt = now;

        const busy = now - this.changedAt <= AFTER_MS;
        const due = now - this.sentAt >= (busy ? BUSY_MS : HEARTBEAT_MS);

        if (!landed && !due)
            return null;

        const frame = { id: this.id, seq: ++this.seq >>> 0, axes: [...axes], down };

        this.sent = frame;
        this.sentAt = now;
        return { frame, both: landed };
    }
}
