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
 * roll.js -- the piano roll: what the scheduler delivered, drawn on a
 * canvas as it arrives.
 *
 * Fed the worklet's tape messages. Only notes are drawn -- a chanarg write
 * moves a filter and has nothing to put on a roll -- and only the last
 * ROLL_SECONDS of them are kept, with now at the right edge. An epoch
 * change is a load or a rewind: `at' starts again from zero and everything
 * drawn so far is about a piece that is no longer running.
 *
 * Shared by the solo page and the room page, which draw the same thing.
 */

/* How much of the piece the roll shows, in seconds, and the pitches it has
   room for. Notes older than this scroll off the left. */
export const ROLL_SECONDS = 30;
const ROLL_LOW = 24, ROLL_HIGH = 108;

/* One colour per channel, so a piece's instruments are told apart. */
export const CHANNEL_COLOURS = [
    '#e05c4a', '#e0a13c', '#c9c93a', '#6fbf4a', '#3fb8a0', '#3f96d0',
    '#5a6fd8', '#8f5ad8', '#cf4fb0', '#d9607a', '#9a8f6a', '#6a9a8f',
    '#8a8a8a', '#c07a3a', '#4a8ac0', '#a0a04a',
];

export class Roll
{
    constructor (canvas, clock)
    {
        this.canvas = canvas;
        this.clock = clock;             /* the element the time goes in */
        this.epoch = -1;
        this.now = 0;
        this.running = false;
        this.notes = [];
    }

    clear ()
    {
        this.notes = [];
        this.now = 0;
    }

    /* A tape message. */
    tape (m)
    {
        this.now = m.now;
        this.running = m.running;

        if (m.epoch !== this.epoch)
        {
            this.epoch = m.epoch;
            this.notes = [];
        }

        for (const e of m.events)
            if (e.kind === 'N')
                this.notes.push(e);

        /* Whatever has ended before the left edge, wherever it sits: a
           long note at the front must not keep everything after it
           alive. */
        const first = this.now - ROLL_SECONDS;

        if (this.notes.some((e) => e.at + e.duration < first))
            this.notes = this.notes.filter((e) => e.at + e.duration >= first);
    }

    draw ()
    {
        const c = this.canvas;
        const g = c.getContext('2d');
        const w = c.width, h = c.height;

        g.clearRect(0, 0, w, h);

        /* The last ROLL_SECONDS, with now at the right edge. */
        const first = this.now - ROLL_SECONDS;
        const x = (t) => (t - first) / ROLL_SECONDS * w;
        const y = (n) => h - (n - ROLL_LOW) / (ROLL_HIGH - ROLL_LOW) * h;

        g.strokeStyle = getComputedStyle(c).getPropertyValue('--line') || '#ddd';
        g.lineWidth = 1;

        for (let n = ROLL_LOW; n <= ROLL_HIGH; n += 12)
        {
            g.beginPath();
            g.moveTo(0, Math.round(y(n)) + 0.5);
            g.lineTo(w, Math.round(y(n)) + 0.5);
            g.stroke();
        }

        const tall = Math.max(2, h / (ROLL_HIGH - ROLL_LOW));

        for (const e of this.notes)
        {
            const left = x(e.at);
            const wide = Math.max(2, (e.duration || 0.05) / ROLL_SECONDS * w);

            g.globalAlpha = 0.25 + 0.75 * Math.min(1, e.velocity / 110);
            g.fillStyle = CHANNEL_COLOURS[e.channel & 15];
            g.fillRect(left, y(e.note) - tall / 2, wide, tall);
        }

        g.globalAlpha = 1;

        if (this.clock)
        {
            const secs = Math.max(0, this.now);

            this.clock.textContent =
                `${Math.floor(secs / 60)}:` +
                `${(secs % 60).toFixed(1).padStart(4, '0')}` +
                (this.running ? '' : ' (stopped)');
        }
    }
}
