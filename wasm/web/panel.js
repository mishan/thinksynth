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
 * panel.js -- a thPanel, as DOM.
 *
 * The page's half of the split src/PanelModel.h describes, and the other of
 * the two shells over it; the desktop's is src/gui/PanelView.cpp. What
 * arrives is the module's own description of a panel -- the rows, what each
 * is worth in the unit it was written in, how many decimals are worth
 * showing, which group it belongs to, whether it may be changed -- and what
 * is here is only what that looks like in a browser.
 *
 * None of the arithmetic is here, and that is the point. The rows used to be
 * built three times over in this directory, each from a different C ABI and
 * each guessing at the parts it had no access to: knobs.js rounds with
 * toPrecision(3), composerview.js with toPrecision(4), and neither of them
 * has ever known that a duration is stored in samples. A row now carries its
 * own spelling and its own resolution, so there is nothing left to guess.
 *
 * An edit does not write either. `onEdit(row, text)' is handed the row's id
 * and the authored spelling, and the caller sends that as a command: the
 * module validates it and every peer applies it, the page included, because
 * the page is the nearest peer and not a privileged one (docs/JAM.md).
 */

/* thPanelRow::Kind. */
export const SLIDER = 0;
export const CHOICE = 1;
export const NUMBER = 2;
export const TEXT = 3;
export const READONLY = 4;
export const TOGGLE = 5;

/* The number a row would show, spelled the way the module spells it.
 *
 * thPanelSpell, in JavaScript, and it has to stay that: what leaves here as
 * an edit is compared against what the module has, and a page that wrote its
 * numbers its own way would make every one of those comparisons a near miss
 * -- so a slider dragged back to where it started would count as an edit.
 * toFixed is %.*f with the same rounding.
 */
export function spell (value, decimals)
{
    const text = Number(value).toFixed(Math.max(0, Math.min(12, decimals)));

    /* -0 is a value nothing means and every rounding of a small negative
       produces. */
    return /^-[0.]*$/.test(text) ? text.slice(1) : text;
}

/* The control for one row, and how to put a value into it afterwards.
 *
 * Each returns the element to place and registers itself in `bound' under
 * the row's id, so that a value arriving from behind the panel -- a knob,
 * another peer, a MIDI controller -- can be pushed in without going looking
 * for it.
 */
function makeSlider (row, emit, bound)
{
    const box = document.createElement('span');
    const input = document.createElement('input');
    const shown = document.createElement('input');

    box.className = 'panelcontrol';

    input.type = 'range';
    input.min = row.lo;
    input.max = row.hi;
    input.step = row.step > 0 ? row.step : 'any';
    input.value = row.value;
    input.disabled = !row.editable;

    /* A number box beside the slider, not a label: a filter cutoff is
       something to type as well as to drag, and the desktop's panel has had
       both since there was one. Sized from the row's own valueChars, which
       is the width the widest value in this range needs. */
    shown.type = 'number';
    shown.className = 'value';
    shown.min = row.lo;
    shown.max = row.hi;
    shown.step = row.step > 0 ? row.step : 'any';
    shown.value = spell(row.value, row.decimals);
    shown.disabled = !row.editable;
    shown.style.width = `${row.valueChars + 2}ch`;

    /* `input' rather than `change' on the range: a drag is a stream of
       edits and each of them is heard, which is what a slider is for. The
       number box waits for `change', since a half-typed number is not a
       value anybody meant. */
    input.addEventListener('input', () =>
    {
        shown.value = spell(input.value, row.decimals);
        emit(spell(input.value, row.decimals));
    });

    shown.addEventListener('change', () =>
    {
        input.value = shown.value;
        emit(shown.value);
    });

    box.append(input, shown);

    bound.set(row.id, (value) =>
    {
        input.value = value;
        shown.value = spell(value, row.decimals);
    });

    return box;
}

function makeNumber (row, emit, bound)
{
    const input = document.createElement('input');

    input.type = 'number';
    input.className = 'value';
    input.min = row.lo;
    input.max = row.hi;
    input.step = row.step > 0 ? row.step : 'any';
    input.value = spell(row.value, row.decimals);
    input.disabled = !row.editable;
    input.style.width = `${row.valueChars + 2}ch`;

    input.addEventListener('change', () => emit(input.value));

    bound.set(row.id, (value) =>
    {
        input.value = spell(value, row.decimals);
    });

    return input;
}

/* Named values are a list, not a range. Six waveforms have no order worth
   dragging through, and five sixths of the travel between them means
   nothing. */
function makeChoice (row, emit, bound)
{
    const select = document.createElement('select');

    for (const c of row.choices)
        select.append(new Option(c.name, String(c.value)));

    /* A stored value the plugin does not implement has no option, and
       nothing is selected rather than the first one -- which would put
       "Sine" on screen beside an arg holding 4. */
    const pick = (value) =>
    {
        const want = String(Math.trunc(value));

        select.value = want;

        if (select.value !== want)
            select.selectedIndex = -1;
    };

    pick(row.value);

    select.disabled = !row.editable;
    select.addEventListener('change', () =>
    {
        if (select.selectedIndex >= 0)
            emit(select.options[select.selectedIndex].text);
    });

    bound.set(row.id, pick);

    return select;
}

function makeText (row, emit, bound)
{
    const input = document.createElement('input');

    input.type = 'text';
    input.value = row.text;
    input.disabled = !row.editable;

    /* On change rather than on every keystroke: half of a note set is a
       note set the module would refuse, and a refusal per character is not
       a thing to put in front of anyone. */
    input.addEventListener('change', () => emit(input.value));

    bound.set(row.id, (value, text) => { input.value = text; });

    return input;
}

/* Shown, not offered. Hiding an output outright would be worse: seeing what
   a patch produces is half of reading one. */
function makeReadonly (row, emit, bound)
{
    const span = document.createElement('span');

    span.className = 'paramwhat';
    span.textContent = row.text;

    bound.set(row.id, (value, text) =>
    {
        span.textContent = text ?? spell(value, row.decimals);
    });

    return span;
}

function makeToggle (row, emit, bound)
{
    const input = document.createElement('input');

    input.type = 'checkbox';
    input.checked = row.value !== 0;
    input.disabled = !row.editable;

    input.addEventListener('change', () => emit(input.checked ? '1' : '0'));

    bound.set(row.id, (value) => { input.checked = value !== 0; });

    return input;
}

const CONTROLS = {
    [SLIDER]: makeSlider,
    [CHOICE]: makeChoice,
    [NUMBER]: makeNumber,
    [TEXT]: makeText,
    [READONLY]: makeReadonly,
    [TOGGLE]: makeToggle,
};

function makeRow (row, onEdit, bound)
{
    const line = document.createElement('div');
    const label = document.createElement('label');

    line.className = 'panelrow';

    /* The unit belongs with the name, not beside the number: it is a
       property of the parameter, the same on every row of it, and it costs
       no width in the value column here. */
    label.textContent = row.units === '' ? row.label
                                         : `${row.label} (${row.units})`;

    /* What the author said it is for, and failing that what the file calls
       it -- which is worth having when the label is something else. */
    line.title = row.desc === '' ? row.id : row.desc;

    const emit = (text) => onEdit(row.id, text);
    const control = (CONTROLS[row.kind] ?? makeReadonly)(row, emit, bound);

    line.append(label, control);

    return line;
}

/* The whole panel into `box', replacing whatever was there.
 *
 * Returns a function that puts a value into one row without calling back:
 * `setValue(id, value, text)'. The panel following the arg is not a person
 * editing it, and the two being told apart is what stops a page reporting an
 * edit nobody made.
 *
 * Grouped rows go in a <details> per group, in the order the panel lists
 * them, open to begin with -- a parameter you cannot see is a parameter you
 * will not remember the patch has. Which rows are grouped and which groups
 * survive is the model's, not this file's: a declared group beats an
 * inferred one and a group of one is not a group, and both of those are
 * settled before any of this runs.
 */
export function showPanel (box, panel, onEdit)
{
    const bound = new Map();

    box.replaceChildren();

    const loose = document.createElement('div');

    loose.className = 'panelrows';

    for (const row of panel.rows)
        if (row.group === '')
            loose.append(makeRow(row, onEdit, bound));

    if (loose.childElementCount > 0)
        box.append(loose);

    for (const group of panel.groups)
    {
        const block = document.createElement('details');
        const title = document.createElement('summary');
        const rows = document.createElement('div');

        block.open = true;
        block.className = 'panelgroup';
        title.textContent = group;
        rows.className = 'panelrows';

        for (const row of panel.rows)
            if (row.group === group)
                rows.append(makeRow(row, onEdit, bound));

        block.append(title, rows);
        box.append(block);
    }

    return (id, value, text) => bound.get(id)?.(value, text);
}
