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

/* A double's exact value, as a sign, a mantissa and a power of two.
 *
 * Every double is a dyadic rational and so has a finite decimal expansion.
 * This is the way to that expansion, and the reason for the BigInt below is
 * that the expansion runs to 767 digits at its longest and there is nothing
 * else here that can hold one. */
const BITS = new DataView(new ArrayBuffer(8));

function exactly (value)
{
    BITS.setFloat64(0, value);

    const hi = BITS.getUint32(0), lo = BITS.getUint32(4);
    const biased = (hi >>> 20) & 0x7ff;

    let mantissa = (BigInt(hi & 0xfffff) << 32n) | BigInt(lo);

    /* A subnormal has no implicit leading one and its exponent is the
       smallest there is rather than one less than that. */
    const exponent = biased === 0 ? -1074 : biased - 1075;

    if (biased !== 0)
        mantissa |= 1n << 52n;

    return { negative: (hi >>> 31) !== 0, mantissa, exponent };
}

/* The number a row would show, spelled the way the module spells it.
 *
 * thPanelSpell, in JavaScript, and it has to stay that: what leaves here as
 * an edit is compared against what the module has, and a page that wrote its
 * numbers its own way would make every one of those comparisons a near miss
 * -- so a slider dragged back to where it started would count as an edit.
 *
 * Which is why this is not toFixed. The module spells with %.*f, and the two
 * disagree on a value that falls exactly half way: printf rounds a tie to the
 * even digit and toFixed rounds it up, so 0.25 at one decimal is "0.2" in the
 * module and "0.3" on the page, and 500.5 ms is 500 against 501. Ties are not
 * a curiosity here -- a control's travel is powers of ten and its values land
 * on them. Nor is toFixed's disagreement always in that direction: it reads
 * the shortest decimal that names the double rather than the double, so any
 * rounding of a spelling is the wrong answer for a different reason.
 *
 * So the double is taken apart and the rounding done on its exact value, in
 * integers: value * 10^decimals as a fraction, rounded half to even. That is
 * what printf's default rounding mode does, digit for digit.
 */
export function spell (value, decimals)
{
    const places = Math.max(0, Math.min(12, Math.trunc(decimals) || 0));
    const v = Number(value);

    /* What %f prints for these, so that a row holding one reads the same in
       both shells rather than "Infinity" in one of them. */
    if (Number.isNaN(v))
        return 'nan';

    if (!Number.isFinite(v))
        return v < 0 ? '-inf' : 'inf';

    const { negative, mantissa, exponent } = exactly(v);

    const scaled = mantissa * 10n ** BigInt(places);

    let n;

    if (exponent >= 0)
        n = scaled << BigInt(exponent);         /* an integer already */
    else
    {
        const half = 1n << BigInt(-exponent);
        const whole = scaled / half;
        const rest = (scaled % half) * 2n;

        n = (rest > half || (rest === half && (whole & 1n) === 1n))
            ? whole + 1n : whole;
    }

    let digits = n.toString();

    if (places > 0)
    {
        while (digits.length <= places)
            digits = '0' + digits;

        digits = digits.slice(0, -places) + '.' + digits.slice(-places);
    }

    const text = (negative ? '-' : '') + digits;

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

        /* Not into a box someone is typing in.
         *
         * The module's value arrives four times a second whether or not this
         * page asked for it, and half a typed number replaced by the value
         * that is still there is a number that can never be finished. The
         * slider beside it keeps following, since it is the box and not the
         * row that is being edited. */
        if (document.activeElement !== shown)
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
        if (document.activeElement !== input)
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

    /* A number has nothing to say about this row. The value poll carries one
       for every row in the panel, and writing it here put the string
       "undefined" in the box -- a TEXT row holds words and follows only
       words, which is the same split PanelView::setText draws on the
       desktop. Nor over what is being typed. */
    bound.set(row.id, (value, text) =>
    {
        if (text !== undefined && document.activeElement !== input)
            input.value = text;
    });

    return input;
}

/* Shown, not offered. Hiding an output outright would be worse: seeing what
   a patch produces is half of reading one. */
function makeReadonly (row, emit, bound)
{
    const span = document.createElement('span');

    /* Whether this row reads as its number or as words about it.
     *
     * Both are READONLY and they follow different things. An output the
     * plugin writes every window is a number and has to keep up with it; a
     * row standing in for a wire reads `driven by @cut' and would be
     * destroyed by having a number spelled over it. The module spells the
     * first kind itself, so a text that is exactly that spelling is the
     * number -- and the caller is left with nothing to know. */
    const words = row.text !== spell(row.value, row.decimals);

    span.className = 'paramwhat';
    span.textContent = row.text;

    bound.set(row.id, (value, text) =>
    {
        if (text !== undefined)
            span.textContent = text;
        else if (!words)
            span.textContent = spell(value, row.decimals);
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
 * edit nobody made. `text' is for the rows that hold words and is left out
 * for the rest -- a value poll carries numbers and nothing else, and which
 * rows have any use for one is decided here rather than by the caller.
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

    /* The groups panel.groups names, then any other group a row turns out to
       carry. That list is the order to draw them in and not the roll of which
       there are: drawing only what it names would leave a row off the page
       altogether, and off `bound' with it, so nothing would ever put a value
       in it and nothing would say why. The model keeps the two in step; this
       is the boundary where that stops being something to rely on. */
    const groups = [...panel.groups];

    for (const row of panel.rows)
        if (row.group !== '' && !groups.includes(row.group))
            groups.push(row.group);

    for (const group of groups)
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
