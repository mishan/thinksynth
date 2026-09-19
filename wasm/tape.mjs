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
 * The tape, read out of a module's heap and written the way genwav writes it.
 *
 * wasm/twevent.h is the other half: the C struct these offsets are of, and
 * the collector both Emscripten hosts hand their delivered events to.
 *
 * Two callers, on purpose. genwav.mjs renders a .gen under Node and prints
 * the tape; the browser's worklet reads the same struct out of the same
 * module, posts the events to the page, and wasm/web's check prints them
 * with tapeLine() below. M2's gate is that the two files are identical
 * (docs/JAM.md, section 6), which is a claim about the piece and not about two
 * spellings of a number, so there is one spelling.
 *
 * Nothing here is Node's: a worklet imports this file too.
 */

/* sizeof(twEvent); the offsets are in readEvent. */
export const EVENT_SIZE = 48;

export function readEvent (M, p)
{
    const f64 = M.HEAPF64, i32 = M.HEAP32, u32 = M.HEAPU32;

    return {
        at:       f64[p >> 3],
        duration: f64[(p + 8) >> 3],
        value:    f64[(p + 16) >> 3],
        kind:     String.fromCharCode(i32[(p + 24) >> 2]),
        channel:  i32[(p + 28) >> 2],
        note:     i32[(p + 32) >> 2],
        velocity: i32[(p + 36) >> 2],
        name:     M.UTF8ToString(u32[(p + 40) >> 2]),
        arg:      M.UTF8ToString(u32[(p + 44) >> 2]),
    };
}

/* Every event the module is holding, appended to `out', and then it is
   holding none.
 *
 * Appended rather than returned, because one of the two callers is on the
 * audio thread and the number of events a step can deliver has no bound --
 * the first step of a piece applies every instrument it declares. A
 * `push(...drain(M))' of that is an argument list the size of the burst,
 * and the stack it overruns is the one rendering the audio. */
export function drain (M, out = [])
{
    const base = M._tw_events();
    const n = M._tw_event_count();

    for (let i = 0; i < n; i++)
        out.push(readEvent(M, base + EVENT_SIZE * i));

    M._tw_events_clear();

    return out;
}

/* The loader's complaints after a load that returned 0: what did not parse,
   in its own words and with line numbers. The same three calls in every
   host that loads a piece, so they are made here. */
export function loadErrors (M)
{
    const errors = [];

    for (let i = 0; i < M._tw_error_count(); i++)
        errors.push(M.UTF8ToString(M._tw_error(i)));

    return errors;
}

/* Does a .gen pin its seed? `seed N;' as a statement of its own, wherever
   on the line it starts (thcGenFile.cpp). One answer for every gate that
   asks, so the pieces the native-against-wasm comparison covers are the
   pieces the browser-against-genwav one covers. */
export function seeded (text)
{
    return /^\s*seed\s+\d+\s*;/m.test(text);
}

/* printf("%.<n>f", x). toFixed(100) is the double's exact decimal expansion
   for anything genwav prints, and the rounding is then done here, half to
   even, which is what glibc does with an exact tie. */
export function fixed (x, n)
{
    const neg = x < 0 || Object.is(x, -0);
    const [whole, frac] = Math.abs(x).toFixed(100).split('.');
    const rest = frac.slice(n);
    let digits = BigInt(whole + frac.slice(0, n));

    if (rest[0] > '5' ||
        (rest[0] === '5' &&
         (/[1-9]/.test(rest.slice(1)) || digits % 2n === 1n)))
        digits += 1n;

    const s = digits.toString().padStart(n + 1, '0');

    return (neg ? '-' : '') +
           (n > 0 ? s.slice(0, -n) + '.' + s.slice(-n) : s);
}

/* genwav.cpp's writeEvent. */
export function tapeLine (e)
{
    const at = fixed(e.at, 3);

    switch (e.kind)
    {
        case 'N':
            return `N ${at} ${e.channel} ${e.note} ${e.velocity} ` +
                   `${fixed(e.duration, 3)}\n`;
        case 'C':
            return `C ${at} ${e.channel} ${e.name} ${fixed(e.value, 4)}\n`;
        case 'P':
            return `P ${at} ${e.channel} ${e.name}\n`;
        case 'E':
            return `E ${at} ${e.channel} ${e.name}.${e.arg} ` +
                   `${fixed(e.value, 4)}\n`;
        default:
            return `? ${at} ${e.channel} ${e.note}\n`;
    }
}

/* The tape up to a transport time, as text.
 *
 * Two hosts stepping the transport by windows of different lengths both
 * overshoot the time they were asked for, by up to one window of their own,
 * and what they share is what falls before it. The cut is on the printed
 * time rather than on the double behind it, so that an event whose `at'
 * rounds to exactly `seconds' is cut on both sides or on neither. */
export function tapeBefore (text, seconds)
{
    return text.split('\n')
        .filter((l) => l !== '' && parseFloat(l.split(' ')[1]) < seconds)
        .map((l) => l + '\n')
        .join('');
}
