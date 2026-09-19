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
 * knobs.js -- the knobs a piece declared, as sliders.
 *
 * Both pages draw the same row and moved it the same way; what differs is
 * where a move goes, which is the one callback. Nothing here reaches for a
 * synth: a knob move is a command like every other, so the page that made
 * it is the nearest peer and not a privileged one (docs/JAM.md, section 3).
 */

/* The value beside a slider, to the precision a slider is worth. */
export function knobText (value)
{
    return Number(value).toPrecision(3);
}

/* One row per knob, into `box', replacing whatever was there. `knobs' is
 * the list the worklet reported at the load, each carrying the index a
 * command names it by -- the module's own numbering, over every knob the
 * piece declared. `onChange(knob, value)' is called as one is dragged.
 */
export function showKnobs (box, knobs, onChange)
{
    box.replaceChildren();

    for (const k of knobs)
    {
        const label = document.createElement('label');
        const input = document.createElement('input');
        const shown = document.createElement('span');

        label.textContent = k.label || k.name;
        label.htmlFor = `knob-${k.name}`;

        input.id = `knob-${k.name}`;
        input.dataset.knob = k.knob;
        input.type = 'range';
        input.min = k.min;
        input.max = k.max;
        input.step = k.step > 0 ? k.step : (k.max - k.min) / 1000;
        input.value = k.value;

        shown.className = 'value';
        shown.textContent = knobText(k.value);

        input.addEventListener('input', () =>
        {
            shown.textContent = knobText(input.value);
            onChange(k.knob, Number(input.value));
        });

        /* Label, value, slider: the order a narrow screen wants, where
           the slider takes a line of its own under the two of them. A wide
           one puts the slider between them, which style.css does by
           placing it in the middle column rather than by a second
           ordering here. */
        box.append(label, shown, input);
    }
}

/* A slider moved by somebody else: the value and the number beside it,
 * without calling back. The span is the slider's previous sibling -- see
 * the order they are appended in above.
 */
export function setKnob (box, knob, value)
{
    const input = box.querySelector(`input[data-knob="${knob}"]`);

    if (input === null)
        return;

    input.value = value;
    input.previousElementSibling.textContent = knobText(value);
}
