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
 * render.mjs -- the browser build's module, called directly rather than
 * through a worklet.
 *
 * The same calls worklet.js makes, in the same order, a block at a time:
 * which is what lets browsertest.mjs hold a browser's render against this
 * one to the bit, and what check.mjs uses to play every shipped patch
 * without a browser at all.
 */

/* A fresh module per render: a fresh synth, and a fresh plugin load, which
   is what restarts osc::static's noise. */
export async function renderDirect (createThinkWeb,
                                    { rate = 48000, windowlen = 256,
                                      block = 128, text, events = [],
                                      frames })
{
    const log = [];
    const M = await createThinkWeb({
        print: (s) => log.push(s),
        printErr: (s) => log.push(s),
    });

    const took = M._tw_create(rate, windowlen, block);
    const ok = M.ccall('tw_load', 'number', ['string'], [text]) !== 0;

    for (const e of events)
    {
        if (e.on)
            M._tw_note_on(e.frame, e.note, e.velocity);
        else
            M._tw_note_off(e.frame, e.note);
    }

    const out = new Float32Array(frames * 2);

    for (let done = 0; done < frames; done += block)
    {
        const n = Math.min(block, frames - done);
        const p = M._tw_render(n) >> 2;

        out.set(M.HEAPF32.subarray(p, p + n * 2), done * 2);
    }

    return { ok, log, out, windowlen: took };
}
