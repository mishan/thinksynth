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
 * keyboard.js -- keys you can hit, for the pages that have no other kind.
 *
 * The desktop has src/gui/Keyboard.cpp: a DrawingArea covering all 128
 * notes at one of five fixed pixel sizes. A phone has neither 128 notes'
 * worth of width nor a fixed one, so this is the same idea measured the
 * other way round -- the caller says how much room there is, this says how
 * many octaves fit, and the keys come out a finger wide either way.
 *
 * An <svg> with a viewBox in *key units*: a white key is 1 wide, the keys
 * are as tall as the element's shape makes them, and the browser does the
 * scaling. So there are no pixel sizes here at all, and the geometry below
 * is the geometry of a piano rather than of a screen.
 *
 * The viewBox's own aspect is set to the element's, rather than stretching
 * a square one to fit with preserveAspectRatio="none". Stretching is what
 * the first version did and it scales x and y by different amounts, which
 * is fine for a rectangle and ruinous for everything else: the note names
 * came out squeezed to a third of their width and the key outlines were
 * thicker across than down.
 *
 * It does not know what a note is for. press(note) and release(note) go to
 * the caller, which is what routes them -- straight onto a channel in patch
 * mode, into the piece's `input midi' chains in piece mode -- and hold() is
 * how the caller paints keys this widget did not press, so a computer
 * keyboard and a finger light the same key the same way.
 */

const SVG = 'http://www.w3.org/2000/svg';

/* Semitone of each white key within an octave, and of each black one. */
const WHITE = [0, 2, 4, 5, 7, 9, 11];
const BLACK = [1, 3, 6, 8, 10];

/* Where each black key sits, as a left edge in white-key units from the
   start of the octave, and how wide it is. Not centred on the boundaries
   it straddles: on a piano the three-key group and the two-key group are
   each pushed outwards from their middle, which is what makes the gaps
   between them findable without looking. The height is a fraction of the
   white key's, whatever that has turned out to be. */
const BLACK_AT = [0.62, 1.83, 3.57, 4.71, 5.85];
const BLACK_W = 0.56;
const BLACK_H = 0.62;

/* A note name, as a fraction of a white key's width. Small: it is there to
   be found when looked for, not read. */
const LABEL = 0.34;

/* White keys to the octave, and the narrowest one worth aiming a finger
   at, as a fraction of the element's width. Below this the widget drops an
   octave rather than drawing keys nobody can hit. */
const PER_OCTAVE = 7;
const NARROWEST = 24;

const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#',
               'B'];

export function noteName (n)
{
    return NAMES[((n % 12) + 12) % 12] + (Math.floor(n / 12) - 1);
}

export class Keyboard
{
    /* `svg' is the element to draw in; `onPress' and `onRelease' are given
       a MIDI note. `lowest' is the note the leftmost key plays and is
       expected to be a C. */
    constructor (svg, { onPress, onRelease })
    {
        this.svg = svg;
        this.onPress = onPress;
        this.onRelease = onRelease;

        this.lowest = 48;
        this.octaves = 2;
        this.aspect = 0;                /* height / width, as laid out     */
        this.keys = new Map();          /* note -> the <rect> drawn for it */
        this.held = new Set();          /* what is painted as held         */
        this.touching = new Map();      /* pointerId -> the note it is on  */

        svg.addEventListener('pointerdown', (e) => this.down(e));
        svg.addEventListener('pointermove', (e) => this.move(e));
        svg.addEventListener('pointerup', (e) => this.up(e));
        svg.addEventListener('pointercancel', (e) => this.up(e));

        /* A drag that leaves the element still belongs to the key it
           started on until the finger lifts; without this the note hangs,
           because the pointerup arrives somewhere else. */
        svg.addEventListener('lostpointercapture', (e) => this.up(e));

        /* Redrawn when the room changes: a phone turned sideways is a
           different number of octaves, not the same ones stretched. */
        new ResizeObserver(() => this.fit()).observe(svg);
    }

    /* How many octaves the current width has room for, and a redraw if
       that has changed. */
    fit ()
    {
        const width = this.svg.clientWidth;
        const height = this.svg.clientHeight;

        if (width === 0 || height === 0)        /* not laid out yet */
            return;

        const octaves = Math.max(1, Math.min(
            4, Math.floor(width / (PER_OCTAVE * NARROWEST))));
        const aspect = height / width;

        /* The shape matters as much as the count: a phone turned sideways
           is both a different number of octaves and a different height,
           and the viewBox carries both. */
        if (octaves === this.octaves && Math.abs(aspect - this.aspect) < 1e-3
            && this.keys.size > 0)
            return;

        this.octaves = octaves;
        this.aspect = aspect;
        this.draw();
    }

    /* The note the leftmost key plays. Everything sounding is let go
       first: the keys under the caller's fingers are about to be other
       notes. */
    setLowest (note)
    {
        if (note === this.lowest)
            return;

        this.releaseAll();
        this.lowest = note;
        this.draw();
    }

    /* The range on show, as the caller would write it. */
    get range ()
    {
        return [this.lowest, this.lowest + this.octaves * 12];
    }

    /* Paint a note as held, or not. The caller's, because a note may be
       held by something that is not this widget -- a computer keyboard,
       and later a peer. */
    hold (note, on)
    {
        if (on)
            this.held.add(note);
        else
            this.held.delete(note);

        this.keys.get(note)?.classList.toggle('held', on);
    }

    draw ()
    {
        const { svg } = this;
        const wide = this.octaves * PER_OCTAVE;

        /* One extra C on the right, the way a keyboard ends on one, and a
           height in the same units, so nothing is scaled unevenly. */
        const across = wide + 1;
        const tall = across * this.aspect;

        svg.setAttribute('viewBox', `0 0 ${across} ${tall}`);
        svg.replaceChildren();
        this.keys.clear();

        const rect = (cls, note, x, y, w, h) =>
        {
            const r = document.createElementNS(SVG, 'rect');

            r.setAttribute('class', cls);
            r.setAttribute('x', x);
            r.setAttribute('y', y);
            r.setAttribute('width', w);
            r.setAttribute('height', h);
            r.dataset.note = note;

            svg.append(r);
            this.keys.set(note, r);
        };

        /* Whites first and blacks over them, which is the order a pointer
           is hit-tested in too: a finger on the overlap is on the black
           key, as it is on a piano. */
        for (let i = 0; i <= wide; i++)
        {
            const octave = Math.floor(i / PER_OCTAVE);
            const note = this.lowest + octave * 12 + WHITE[i % PER_OCTAVE];

            rect(WHITE[i % PER_OCTAVE] === 0 ? 'white c' : 'white',
                 note, i, 0, 1, tall);
        }

        for (let o = 0; o < this.octaves; o++)
            for (let k = 0; k < BLACK.length; k++)
                rect('black', this.lowest + o * 12 + BLACK[k],
                     o * PER_OCTAVE + BLACK_AT[k], 0, BLACK_W,
                     BLACK_H * tall);

        /* Every C named, which is as much as fits and as much as anyone
           needs to find where they are. */
        for (let o = 0; o <= this.octaves; o++)
        {
            const t = document.createElementNS(SVG, 'text');

            t.setAttribute('x', o * PER_OCTAVE + 0.5);
            t.setAttribute('y', tall - LABEL * 0.6);
            t.setAttribute('font-size', LABEL);
            t.textContent = noteName(this.lowest + o * 12);

            svg.append(t);
        }

        /* Whatever was held is still held; it just has a new rectangle. */
        for (const note of this.held)
            this.keys.get(note)?.classList.add('held');
    }

    /* The note under a point, or undefined. elementFromPoint rather than
       the event's target, because a finger that slid onto another key is
       still delivering its events to the key it started on. */
    noteAt (x, y)
    {
        const el = document.elementFromPoint(x, y);

        if (el === null || !this.svg.contains(el))
            return undefined;

        const note = el.dataset?.note;

        return note === undefined ? undefined : Number(note);
    }

    down (e)
    {
        const note = this.noteAt(e.clientX, e.clientY);

        if (note === undefined)
            return;

        /* So that a finger sliding off the element keeps reporting, and
           so that the page does not take the drag for a scroll. Capture
           throws if the pointer is no longer active -- a tap fast enough
           to have ended before this ran -- and the note should still
           sound, so it is not worth a failed press. */
        try
        {
            this.svg.setPointerCapture(e.pointerId);
        }
        catch
        {
            /* Nothing: without capture a drag off the keys is lost, and
               the pointerup will still arrive. */
        }

        e.preventDefault();

        this.touching.set(e.pointerId, note);
        this.onPress(note);
    }

    /* A finger dragged from one key to the next is a glissando: the note
       it left stops and the note it reached starts. */
    move (e)
    {
        if (!this.touching.has(e.pointerId))
            return;

        const was = this.touching.get(e.pointerId);
        const now = this.noteAt(e.clientX, e.clientY);

        if (now === was)
            return;

        this.onRelease(was);

        if (now === undefined)
            this.touching.delete(e.pointerId);
        else
        {
            this.touching.set(e.pointerId, now);
            this.onPress(now);
        }
    }

    up (e)
    {
        const note = this.touching.get(e.pointerId);

        if (note === undefined)
            return;

        this.touching.delete(e.pointerId);
        this.onRelease(note);
    }

    /* Every finger lifted, for anything that changes what the keys mean. */
    releaseAll ()
    {
        for (const note of this.touching.values())
            this.onRelease(note);

        this.touching.clear();
    }
}
