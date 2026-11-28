/*
 * Copyright (C) 2004-2026 The thinksynth authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * sessionview.js -- the piece's form as a grid: a column per section in
 * play order, a row per chain and then per knob, and in each cell what
 * the section writes there (SessionModel, read by the worklet from the
 * piece's text).
 *
 * A column's heading cues its section: for the end of the one playing,
 * with Shift for the next bar line, and stopped, it goes there. A chain's
 * cell turns the chain off and back on; Enter types a level, or a knob's
 * value, and Delete takes a knob's value out of the section. A row's
 * name makes its channel the current one.
 *
 * One grid for the keyboard (WAI-ARIA's grid pattern): the arrows move
 * between cells, Home and End along a row, and only the cell last moved
 * to is in the tab order.
 */

/* thSessionEdit::Kind. */
export const LEVEL = 0;

/* Engine channels are 0-based; the page names them as the file does. */
const channelName = (c) => `channel ${c + 1}`;

const ENDING = {
    ends: (g) => `Ends after ${g.columns.at(-1)?.name ?? 'the last'}`,
    loops: (g) => `Loops to ${g.loopTo}`,
    cycles: () => 'Repeats from the first',
};

const shown = (v) => String(+v.toFixed(3));

export function createSessionView ({ root, said, onCue, onEdit, onToggle,
                                     onPick })
{
    let grid = null;
    let current = -1;
    let running = false;
    let at = { r: 1, c: 1 };
    let cued = -1;
    let marks = '';

    /* A grid that came while a value was being typed, drawn after. */
    let typing = false;
    let waiting = null;

    const cellAt = (r, c) =>
        root.querySelector(`[data-r="${r}"][data-c="${c}"]`);

    /* Emptied a frame first, so the same words twice are said twice. */
    const say = (text) =>
    {
        said.textContent = '';
        requestAnimationFrame(() => { said.textContent = text; });
    };

    function go (r, c, focus = true)
    {
        const rows = grid === null ? 0 : grid.rows.length;
        const cols = grid === null ? 0 : grid.columns.length;

        r = Math.max(0, Math.min(rows, r));
        c = Math.max(0, Math.min(cols, c));

        if (r === 0 && c === 0)
            c = 1;

        const was = cellAt(at.r, at.c);
        const to = cellAt(r, c);

        if (to === null)
            return;

        was?.setAttribute('tabindex', '-1');
        to.setAttribute('tabindex', '0');
        at = { r, c };

        if (focus)
            to.focus();
    }

    /* A cell's value typed: Enter keeps it, Escape or leaving puts the
       cell back as it was. */
    function type (td, row, column)
    {
        const input = document.createElement('input');
        const cell = row.cells[column];
        const section = grid.columns[column].name;

        input.inputMode = 'decimal';
        input.value = cell === null ? '' : String(cell);
        input.setAttribute('aria-label',
            `${row.knob ? '@' : ''}${row.name} in ${section}`);

        let done = false;

        const finish = (keep, focus = true) =>
        {
            if (done)
                return;

            done = true;
            typing = false;

            const value = Number(input.value.trim());

            input.remove();
            draw(waiting ?? grid, current, focus);
            waiting = null;

            if (!keep || input.value.trim() === '')
                return;

            if (!Number.isFinite(value))
            {
                say(`${input.value} is not a number`);
                return;
            }

            onEdit({ kind: LEVEL, section,
                     row: (row.knob ? '@' : '') + row.name, value });
        };

        input.addEventListener('keydown', (e) =>
        {
            e.stopPropagation();

            if (e.isComposing)
                return;

            if (e.key === 'Enter')
                finish(true);
            else if (e.key === 'Escape')
                finish(false);
        });
        input.addEventListener('blur', () => finish(false, false));
        typing = true;
        td.replaceChildren(input);
        input.focus();
        input.select();
    }

    function act (r, c, e)
    {
        if (grid === null)
            return;

        if (r === 0)
        {
            if (c > 0)
                onCue(c - 1, e.shiftKey);
            return;
        }

        const row = grid.rows[r - 1];

        if (c === 0)
        {
            if (row.channel >= 0)
                onPick(row.channel);
            else
                say(row.knob ? `@${row.name} is a knob, not a channel`
                             : `${row.name} plays no channel`);
            return;
        }

        const section = grid.columns[c - 1].name;

        if (row.knob || e.key === 'Enter')
            type(cellAt(r, c), row, c - 1);
        else
            onToggle(section, row.name);
    }

    root.addEventListener('click', (e) =>
    {
        const td = e.target.closest('[data-r]');

        if (td === null || e.target.closest('input') !== null)
            return;

        go(+td.dataset.r, +td.dataset.c);
        act(+td.dataset.r, +td.dataset.c, e);
    });

    root.addEventListener('keydown', (e) =>
    {
        const td = e.target.closest('[data-r]');

        if (td === null || grid === null)
            return;

        const r = +td.dataset.r, c = +td.dataset.c;
        const last = grid.columns.length;

        switch (e.key)
        {
            case 'ArrowLeft': go(r, c - 1); break;
            case 'ArrowRight': go(r, c + 1); break;
            case 'ArrowUp': go(r - 1, c); break;
            case 'ArrowDown': go(r + 1, c); break;
            case 'Home': e.ctrlKey ? go(0, 1) : go(r, 0); break;
            case 'End': go(e.ctrlKey ? grid.rows.length : r, last); break;
            case 'Enter':
            case ' ':
                act(r, c, e);
                break;
            case 'Delete':
            case 'Backspace':
            {
                const row = grid.rows[r - 1];

                if (r > 0 && c > 0 && row.knob && row.cells[c - 1] !== null)
                    onEdit({ kind: LEVEL, section: grid.columns[c - 1].name,
                             row: '@' + row.name, set: false });
                break;
            }
            default:
                return;
        }

        e.preventDefault();
    });

    /* The grid drawn again: what the worklet read, and which channel is
       current. Where the keyboard was stays where it was. */
    /* A heading as playing and cued, in its class and in words. */
    function marked (th, col)
    {
        th.className = (col.playing ? 'playing ' : '') +
                       (col.cued ? 'cued' : '');
        th.setAttribute('aria-description',
            (col.playing ? running ? 'playing; ' : 'starts here; ' : '') +
            (col.cued ? 'cued; ' : '') +
            'Enter cues it, Shift+Enter for the next bar');
    }

    function draw (g, channel, focus = null)
    {
        current = channel;

        if (typing)
        {
            waiting = g;
            return;
        }

        grid = g;
        marks = '';

        const focused = focus ?? root.contains(document.activeElement);

        if (g === null || !g.ok || g.columns.length === 0)
        {
            root.replaceChildren();
            root.textContent = g === null ? 'No piece is loaded.'
                             : g.ok ? 'This piece has no sections.'
                             : `The piece did not read: ${g.why}`;
            return;
        }

        const table = document.createElement('table');
        const caption = table.createCaption();
        const head = table.createTHead().insertRow();
        const body = table.createTBody();

        table.setAttribute('role', 'grid');
        table.setAttribute('aria-label', 'Session');
        caption.textContent = ENDING[g.ending](g) +
            (g.cueKnob === '' ? '' : `; @${g.cueKnob} cues the sections`);

        const corner = document.createElement('td');

        corner.dataset.r = '0';
        corner.dataset.c = '0';
        corner.setAttribute('role', 'columnheader');
        head.append(corner);

        g.columns.forEach((col, k) =>
        {
            const th = document.createElement('th');
            const name = document.createElement('span');
            const length = document.createElement('span');

            th.dataset.r = '0';
            th.dataset.c = String(k + 1);
            th.setAttribute('role', 'columnheader');
            marked(th, col);
            name.className = 'name';
            name.textContent = col.name;
            length.className = 'length';
            length.textContent = col.length + (col.hold ? ', hold' : '');
            th.append(name, length);
            head.append(th);
        });

        g.rows.forEach((row, i) =>
        {
            const tr = body.insertRow();
            const th = document.createElement('th');
            const mine = !row.knob && row.channel >= 0 && row.channel === channel;

            th.dataset.r = String(i + 1);
            th.dataset.c = '0';
            th.setAttribute('role', 'rowheader');
            th.className = (row.knob ? 'knob ' : '') +
                           (row.knob || row.notes ? '' : 'quiet ') +
                           (mine ? 'current' : '');
            if (mine)
            {
                const pointer = document.createElement('span');

                pointer.setAttribute('aria-hidden', 'true');
                pointer.textContent = '\u25b8 ';
                th.append(pointer);
            }

            th.append((row.knob ? '@' : '') + row.name);
            th.setAttribute('aria-description', [
                mine ? 'the current channel' : '',
                row.channel >= 0 ? channelName(row.channel) : '',
                !row.knob && !row.notes ? 'plays no notes' : '',
                row.cues ? `drives @${g.cueKnob}` : '',
            ].filter((s) => s !== '').join('; '));
            tr.append(th);

            row.cells.forEach((v, k) =>
            {
                const td = document.createElement('td');

                td.dataset.r = String(i + 1);
                td.dataset.c = String(k + 1);
                td.setAttribute('role', 'gridcell');
                td.className = (v === null ? 'unset ' : '') +
                               (!row.knob && v === 0 ? 'off' : '');
                td.textContent = v !== null ? shown(v) : row.knob ? '' : '1';
                td.setAttribute('aria-label',
                    v === null ? row.knob ? 'not set' : '1, not written'
                    : !row.knob && v === 0 ? '0, off' : shown(v));
                tr.append(td);
            });
        });

        for (const el of table.querySelectorAll('[data-r]'))
            el.setAttribute('tabindex', '-1');

        root.replaceChildren(table);

        const r = Math.min(at.r, g.rows.length);
        const c = Math.min(at.c, g.columns.length);

        at = { r: -1, c: -1 };
        go(r, c, focused);
    }

    return {
        draw,

        /* The scheduler's sections, as the tape says, and whether it
           plays: the headings marked, and the one cued that starts said. */
        at (section, sectionCued, playing)
        {
            const now = `${section} ${sectionCued} ${playing}`;

            if (now === marks)
                return;

            marks = now;
            running = playing;

            grid?.columns?.forEach((col, k) =>
            {
                col.playing = k === section;
                col.cued = k === sectionCued;

                const th = cellAt(0, k + 1);

                if (th !== null)
                    marked(th, col);
            });

            if (cued >= 0 && section === cued && sectionCued !== cued)
                say(`${grid?.columns[section]?.name ?? 'The section'} starts`);

            cued = sectionCued;
        },

        say,
    };
}
