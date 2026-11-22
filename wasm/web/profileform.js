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
 * profileform.js -- the configurator's form over the player's profile of a
 * device (docs/PHONE.md, "The configurator"): a card for each part of it,
 * a finger or a trigger, saying in a line what its controls do, and opened
 * to change them. What it shows and every edit are the module's
 * (thinkhid.cpp's th_form and th_form_edit, hid/thhForm.h); this draws
 * them, and hands the text each edit makes to `changed'.
 */

const LOOPS = ['record', 'undo', 'clear'];

/* The longest profile handed the module, whose strings go on its stack:
   at most three bytes a character, well inside it. */
export const MAX_PROFILE = 16384;

/* How far an axis moves for Learn to take it as the one meant: past a
   hand's shake, short of a deliberate move. */
const LEARN_MOVE = 0.25;

function make (tag, props = {}, ...children)
{
    const e = Object.assign(document.createElement(tag), props);

    e.append(...children);
    return e;
}

function choice (label, options, value)
{
    const s = make('select', { ariaLabel: label });

    for (const [v, text] of options)
        s.append(make('option', { value: v, textContent: text, selected: v === value }));
    return s;
}

/* A binding in a few words: its action and what it is of. */
function said (b, f)
{
    const param = f.kinds.find((k) => k.name === b.kind)?.param;

    return b.kind === 'value' ? b.arg
        : `${b.kind}${param ? ` ${b[param]}` : ''}${b.scale ? `, ${b.scale}` : ''}`;
}

/* What a control does in a few words: for a control bound only in a layer
   or for one hand, where. */
function whatItDoes (c, f)
{
    return c.patched ? "the patch's" : c.off ? 'off'
        : c.bindings.length ? c.bindings.map((b) => said(b, f)).join(' + ')
        : `in ${[...new Set(c.elsewhere.map((e) => e.split(':')[0]))].join(', ')}`;
}

/* `form(text)' and `edit(text, op, control, which, fields)' are the
   module's, the latter {err} where it won't; `args()' the patch's knob
   names; `text()' the profile now. */
export function profileForm ({ rows, problem, learn, form, edit, args, text,
                               changed })
{

    /* Each control's { control, now, axis, card }, in the module's order,
       which its values come in. */
    let drawn = [];
    const open = new Set(); /* the cards and option lists opened */
    /* The values Learn started from, [] until the device is there, or null
       when it isn't learning. */
    let learning = null;
    let last = [];

    const say = (why) => { problem.textContent = why; };

    /* The actions `c' takes that the device plays. */
    const playable = (f, c) => f.kinds.filter((k) => (c.axis ? k.axis : k.button) &&
                                                     !f.cannot.includes(k.name));

    const made = (op, control, which, fields = '') =>
    {
        const r = edit(text(), op, control, which, fields);

        if (r.err !== undefined)
            draw(r.err);
        else
            changed(r.text);
    };

    /* What a details remembers of being open, by `key', across redraws. */
    const kept = (details, key) =>
    {
        details.open = open.has(key);
        details.addEventListener('toggle', () =>
            details.open ? open.add(key) : open.delete(key));
        return details;
    };

    /* One binding: its action and what it is of, and its options under
       More. */
    const binding = (f, c, b, which, knobs) =>
    {
        /* Its own kind too, if the device doesn't play it: it is still
           what the text says. */
        const kinds = playable(f, c);

        if (!kinds.some((k) => k.name === b.kind))
            kinds.push(f.kinds.find((k) => k.name === b.kind));
        const kind = f.kinds.find((k) => k.name === b.kind);
        const name = `${c.control} ${which + 1}`;
        const set = (fields) => made('set', c.control, which, fields);
        const field = (key, input, get = () => input.value) =>
        {
            input.addEventListener('change', () => set(`${key}=${get()}`));
            return input;
        };
        const num = (key, value, step = 'any') =>
            field(key, make('input', { type: 'number', step, ariaLabel: `${name} ${key}`,
                                       value: value ?? '', placeholder: 'none' }));
        const kindSel = choice(`${name} action`, kinds.map((k) => [k.name, k.name]), b.kind);
        const li = make('li', {}, kindSel);

        kindSel.addEventListener('change', () =>
            set(`kind=${kindSel.value}` +
                (kindSel.value === 'value' && knobs.length ? `\narg=${knobs[0]}` : '')));
        if (kind.param === 'semis')
            li.append(' ', num('semis', b.semis, 1));
        else if (kind.param === 'layer')
            li.append(' ', field('layer', choice(`${name} layer`,
                                                 f.layers.map((l) => [l, l]), b.layer)));
        else if (kind.param === 'arg')
        {
            const names = b.kind === 'loop' ? LOOPS
                : knobs.includes(b.arg) || b.arg === '' ? knobs : [b.arg, ...knobs];

            li.append(' ', field('arg', choice(`${name} arg`, names.map((a) => [a, a]), b.arg)));
        }

        const more = kept(make('details', {}, make('summary', { textContent: 'More' })),
                          `${name} more`);

        for (const key of kind.takes)
        {
            const label = make('label', { textContent: `${key} ` });

            if (key === 'latch' || key === 'both')
            {
                const box = make('input', { type: 'checkbox', checked: b[key],
                                            ariaLabel: `${name} ${key}` });

                label.append(field(key, box, () => (box.checked ? 1 : 0)));
            }
            else if (key === 'step')
                label.append(field(key, choice(`${name} step`,
                    [['semitone', 'semitone'], ['octave', 'octave']], b.step)));
            else if (key === 'shape')
                label.append(field(key, choice(`${name} shape`,
                    [['linear', 'linear'], ['exp', 'exp']], b.shape)));
            else if (key === 'scale')
                label.append(field(key, choice(`${name} scale`,
                    [['', 'none'], ...f.scales.map((s) => [s, s])], b.scale)));
            else
                label.append(num(key, b[key], key === 'channel' || key === 'range' ? 1 : 'any'));
            more.append(label, ' ');
        }

        const remove = make('button', { textContent: 'Remove', ariaLabel: `Remove ${name}` });

        remove.addEventListener('click', () => made('remove', c.control, which));
        li.append(' ', remove);
        if (kind.takes.length)
            li.append(more);
        return li;
    };

    /* One control on its card: what it does, its value now, its bindings,
       and Add, Reset and Off. */
    const control = (f, c, knobs) =>
    {
        const now = make('output', { ariaLabel: `${c.control} now` });

        /* Its value changes ten times a second: not one to be read out. */
        now.setAttribute('aria-live', 'off');
        const whose = make('span', { className: 'hint',
            textContent: (c.patched ? "the patch's plays over " : '') +
                (c.off ? 'off' : c.yours ? 'yours' : c.bindings.length ? f.own : 'nothing') });
        const div = make('div', { className: 'formcontrol' },
                         make('strong', { textContent: c.role }), ' ', now, ' ', whose,
                         make('ul', {}, ...c.bindings.map((b, i) => binding(f, c, b, i, knobs))));
        const button = (label, op) =>
        {
            const b = make('button', { textContent: label, ariaLabel: `${label} ${c.control}` });

            b.addEventListener('click', () => op());
            div.append(b, ' ');
        };

        for (const l of c.elsewhere)
            div.append(make('p', { className: 'hint', textContent: l }));
        /* A button does one thing. An axis is first a knob, where there are
           knobs and the device plays them. */
        const first = playable(f, c).map((k) => k.name)
            .find((k) => k !== 'value' || knobs.length);

        if ((c.axis || c.bindings.length === 0) && first)
            button('Add', () => made('add', c.control, -1,
                `kind=${first}${first === 'value' ? `\narg=${knobs[0]}` : ''}`));
        if (c.yours || c.off)
            button('Reset', () => made('reset', c.control, -1));
        if (!c.off && c.bindings.length)
            button('Off', () => made('off', c.control, -1));
        return { div, now };
    };

    /* Drawn again from the module's form. `why' an edit was refused, shown
       until the next. Focus stays on what had it. */
    let drawing = 0, focused;
    /* Whether the device drawn has its buttons up when it comes back. */
    let backUp = false;

    const draw = async (why = '') =>
    {
        /* Taken before the first of draws at once makes the rows inert,
           which takes the focus. */
        if (drawing++ === 0)
            focused = document.activeElement?.ariaLabel;
        rows.inert = true;
        try
        {
            await redraw(why);
        }
        finally
        {
            if (--drawing === 0)
                rows.inert = false;
        }
        if (drawing === 0 && focused)
            [...rows.querySelectorAll('[aria-label]')]
                .find((e) => e.ariaLabel === focused)?.focus();
    };

    const redraw = async (why) =>
    {
        const knobs = await args().catch(() => []);
        const f = form(text());
        const groups = new Map();

        say(f.problem || why);
        backUp = f.backUp;
        drawn = new Array(f.controls.length);
        for (const c of f.controls)
        {
            const g = c.card;

            if (!groups.has(g))
                groups.set(g, []);
            groups.get(g).push(c);
        }
        rows.replaceChildren(...[...groups].map(([g, cs]) =>
        {
            const doing = cs.filter((c) => c.patched || c.off || c.bindings.length ||
                                           c.elsewhere.length);
            const line = make('span', { className: 'hint', textContent: doing.length
                ? doing.map((c) => `${c.role}: ${whatItDoes(c, f)}`).join('   ')
                : 'nothing' });
            const card = kept(make('details', { className: 'formgroup' },
                make('summary', {}, make('strong', { textContent: g }), ' ', line)), g);

            for (const c of cs)
            {
                const { div, now } = control(f, c, knobs);

                card.append(div);
                drawn[f.controls.indexOf(c)] = { control: c.control, now, axis: c.axis,
                                                 card: c.card, details: card };
            }
            return card;
        }));
    };

    const stopLearning = () =>
    {
        learning = null;
        learn.setAttribute('aria-pressed', 'false');
    };

    /* `v', each control's value in the form's order; none with no device there. */
    const values = (v) =>
    {
        /* Values of another device than the one drawn, as after a switch
           made with the form closed, are none of these. */
        drawn.forEach((d, i) =>
        {
            d.now.textContent = v.length !== drawn.length ? '-'
                : d.axis ? v[i].toFixed(2) : v[i] ? 'down' : 'up';
        });
        last = v;
        if (learning === null)
            return;
        /* A device gone and come back is a new start, with every button up. */
        if (v.length !== drawn.length)
        {
            learning = [];
            return;
        }
        if (learning.length === 0)
            learning = v.map((x, i) => (drawn[i].axis || !backUp ? x : 0));

        const moved = v.findIndex((x, i) =>
            Math.abs(x - learning[i]) > (drawn[i].axis ? LEARN_MOVE : 0.5));

        if (moved < 0)
            return;
        stopLearning();
        say('');
        for (const d of drawn)
            d.now.parentElement.classList.remove('learned');

        const d = drawn[moved];

        d.details.open = true;
        open.add(d.card);
        d.now.parentElement.classList.add('learned');
        d.now.parentElement.scrollIntoView({ block: 'nearest' });
        d.now.parentElement.querySelector('select, button')?.focus();
    };

    learn.addEventListener('click', () =>
    {
        if (learning !== null)
        {
            stopLearning();
            say('');
            return;
        }
        learning = last.length === drawn.length ? last : [];
        learn.setAttribute('aria-pressed', 'true');
        say('Move the control you mean.');
    });

    return { draw, values };
}
