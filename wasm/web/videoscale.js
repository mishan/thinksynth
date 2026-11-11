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
 * videoscale.js -- how many workers a picture wants, second by second.
 *
 * videoworker.js draws with as few workers as keep up: one, itself, until
 * the picture falls behind, and another each time it still does, up to the
 * cores it may use; one fewer once the rest would still have room. It asks
 * this once a second with what the second was like, and this says whether
 * to add a worker, let one go, or neither. Nothing here knows about
 * workers, so it is tested by the seconds it is told about.
 */

/* Busier than this, or a frame skipped, is behind. */
export const HOT = 0.8;

/* With one worker fewer, the busiest would still be under this: room. */
export const ROOM = 0.6;

/* Behind this many seconds running before another worker. */
export const GROW_S = 2;

/* Room this many seconds running before one fewer, doubled when a worker
   let go had to come back within BOUNCE_S, up to MOST_S. */
export const SHRINK_S = 10, BOUNCE_S = 30, MOST_S = 120;

export class Scaler
{
    /* `limit' the most workers, the leader among them; `fixed' a number of
       workers the page asked for, which is kept to. */
    constructor ({ limit, fixed = null })
    {
        this.limit = Math.max(1, limit);
        this.fixed = fixed === null ? null : Math.max(1, fixed);
        this.hot = 0;
        this.cool = 0;
        this.wait = SHRINK_S;
        this.shrunk = -Infinity;
    }

    /* Whether another worker could still be added: until then a skip is
       this one's to answer, not the page's, which would shrink the
       picture. */
    canGrow (workers)
    {
        return this.fixed === null && workers < this.limit;
    }

    /* A second gone, at `now' seconds: the busiest worker's fraction of it,
       the frames skipped in it, and how many workers drew. 1 to add one,
       -1 to let one go, 0 for neither. */
    tick ({ busy, skips, workers, now })
    {
        if (this.fixed !== null)
            return Math.sign(Math.min(this.fixed, this.limit) - workers);

        const behind = skips > 0 || busy > HOT;
        const room = workers > 1 && busy * workers / (workers - 1) < ROOM;

        this.hot = behind ? this.hot + 1 : 0;
        this.cool = room ? this.cool + 1 : 0;
        if (this.hot >= GROW_S && this.canGrow(workers))
        {
            if (now - this.shrunk < BOUNCE_S)
                this.wait = Math.min(this.wait * 2, MOST_S);
            this.hot = 0;
            this.cool = 0;
            return 1;
        }
        if (this.cool >= this.wait)
        {
            this.shrunk = now;
            this.hot = 0;
            this.cool = 0;
            return -1;
        }
        return 0;
    }
}
