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
 * tapediff.js -- the worklet's tape against the mirror's, event for event.
 *
 * Two instances of the same module, on the same messages, composing the
 * same piece: that is what the mirror is for, and it is a claim that can
 * be checked continuously and for free while somebody plays. The two
 * tapes arrive in batches, from two threads, at different times -- the
 * mirror is a batch behind by construction -- so what is held here is
 * one queue per side and the comparison happens on whatever both have
 * delivered.
 *
 * Zero, continuously, is a determinism check the jam gets for nothing.
 * Non-zero is a number worth showing and, for now, nothing more: a resync
 * is a fast-forward, and nothing here fast-forwards yet.
 *
 * An epoch is a run: a rewind or a load bumps it, and the two sides bump
 * it at the same point in the stream but not at the same moment. So an
 * event is never compared across epochs -- whichever side is still in the
 * older one is dropped forward, and the events dropped that way are
 * counted separately, because a page that showed them as disagreements
 * would light up red at every rewind.
 */

import { tapeLine } from './tape.js';

/* If one side stops delivering, the other's queue is what grows. A minute
   of a busy piece is a few thousand events; past this, the oldest are
   dropped and counted, which keeps a stalled mirror from being a leak. */
const LIMIT = 8192;

export class TapeDiff
{
    constructor ()
    {
        this.queues = { worklet: [], mirror: [] };

        /* Events held against each other, those that did not match, and
           the last few that did not, for saying which. The count of
           comparisons is shown too: "none" out of nothing is not a
           check, and a page that did not say so would look reassuring
           while the mirror was dead. */
        this.compared = 0;
        this.disagreements = 0;
        this.recent = [];

        /* Dropped rather than compared: at an epoch change, and when a
           queue ran past LIMIT. Neither is a disagreement. */
        this.skipped = 0;
    }

    /* A tape batch from one side. `which' is 'worklet' or 'mirror'. */
    take (which, batch)
    {
        const queue = this.queues[which];

        for (const e of batch.events ?? [])
            queue.push({ epoch: batch.epoch, line: tapeLine(e) });

        if (queue.length > LIMIT)
            this.skipped += queue.splice(0, queue.length - LIMIT).length;

        this.compare();
    }

    compare ()
    {
        const a = this.queues.worklet;
        const b = this.queues.mirror;

        while (a.length > 0 && b.length > 0)
        {
            /* One side is still finishing the run the other has left. Its
               events belong to a tape the other will never deliver. */
            if (a[0].epoch !== b[0].epoch)
            {
                const behind = a[0].epoch < b[0].epoch ? a : b;

                this.skipped++;
                behind.shift();
                continue;
            }

            const left = a.shift();
            const right = b.shift();

            this.compared++;

            if (left.line === right.line)
                continue;

            this.disagreements++;

            if (this.recent.length >= 3)
                this.recent.shift();

            this.recent.push(`${left.line.trim()} / ${right.line.trim()}`);
        }
    }

    /* What a page shows beside its other numbers. */
    summary ()
    {
        const of = `of ${this.compared} event` +
                   `${this.compared === 1 ? '' : 's'}`;

        if (this.disagreements === 0)
            return `none ${of}`;

        return `${this.disagreements} ${of}: ${this.recent.join(', ')}`;
    }
}
