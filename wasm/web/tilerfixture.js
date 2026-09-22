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
 * tilerfixture.js -- the page tilercheck.mjs drives.
 *
 * The least a consumer of panes.js can be: a catalog, a layout per mode,
 * an onShow that does something observable, and a handle for a harness to
 * ask questions through. It is what the module's README would show, and
 * it is deliberately not this project -- nothing here knows what a synth
 * is, so a claim it proves is a claim about the tiler.
 *
 * The two canvases are the point of the file. `onShow' is the contract
 * that makes tiling pay for itself rather than cost, and the only way to
 * hold anything to it is to have work that visibly stops: each of these
 * asks for a frame while it is in front of somebody and asks for nothing
 * when it is not, and says which through `drawing()'.
 */

import { createPanes } from './panes.js';
import { placePopover } from './popover.js';

const $ = (id) => document.getElementById(id);

const CATALOG = ['fx-doc', 'fx-paint', 'fx-plot', 'fx-wide', 'fx-list',
                 'fx-notes', 'fx-only-one', 'fx-only-two'];

/* Which pane belongs to which mode. Everything not named is in both. */
const ONLY = { one: ['fx-only-one'], two: ['fx-only-two'] };

const LAYOUTS = {
    one: {
        dir: 'row', size: [0.55, 0.45], kids: [
            { dir: 'col', size: [0.45, 0.35, 0.2], kids: [
                { tabs: ['fx-paint'] },
                { tabs: ['fx-doc'] },
                { tabs: ['fx-only-one'] }] },
            { dir: 'col', size: [0.4, 0.3, 0.3], kids: [
                { tabs: ['fx-plot'] },
                { tabs: ['fx-wide'] },
                { tabs: ['fx-list'] }] }],
    },
    two: {
        dir: 'row', size: [0.5, 0.5], kids: [
            { tabs: ['fx-doc'] },
            { dir: 'col', size: [0.5, 0.5], kids: [
                { tabs: ['fx-paint'] },
                { tabs: ['fx-only-two'] }] }],
    },
};

/*
 * A canvas that draws while it is looked at.
 *
 * One frame is a moved rectangle and a counter; what matters is that it
 * is asked for at all. `asking' is what the harness reads, and a loop
 * that has been told to stop leaves it false until it is told otherwise
 * -- an onShow that fired and changed nothing would look exactly like
 * one that never fired, which is the mistake this is shaped to catch.
 */
function loop (canvas)
{
    const ctx = canvas.getContext('2d');
    let asking = false;
    let frames = 0;

    const frame = () =>
    {
        if (!asking)
            return;

        frames++;
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        ctx.fillStyle = '#5050ff';
        ctx.fillRect((frames * 3) % canvas.width, 20, 40, 40);
        requestAnimationFrame(frame);
    };

    return {
        show: (on) =>
        {
            if (on === asking)
                return;

            asking = on;

            if (on)
                requestAnimationFrame(frame);
        },
        asking: () => asking,
        frames: () => frames,
    };
}

const paint = loop($('fx-paint-c'));
const plot = loop($('fx-plot-c'));

/* Every onShow the page was told, in order, so a harness can hold the
   tiler to having said it rather than to the state that came of it. */
const told = [];

const panes = createPanes({
    root: $('root'),
    catalog: CATALOG,
    layouts: LAYOUTS,
    mode: $('mode').value,
    store: 'tilerfixture',
    on: true,
    onShow: (id, on) =>
    {
        told.push(`${id}:${on ? 'on' : 'off'}`);

        if (id === 'fx-paint')
            paint.show(on);
        else if (id === 'fx-plot')
            plot.show(on);
    },
});

$('mode').addEventListener('change', () =>
{
    const mode = $('mode').value;

    for (const [name, ids] of Object.entries(ONLY))
        for (const id of ids)
            panes.available(id, name === mode);

    panes.mode(mode);
    $('say').textContent = `mode ${mode}`;
});

/* A popover, placed beside the button that asked for it and put in the
   layout's overlay -- a pane is a box that scrolls and would clip it. */
panes.overlay().append($('fx-menu'));

$('fx-pop').addEventListener('click', (e) =>
{
    const menu = $('fx-menu');

    if (!menu.hidden)
    {
        menu.hidden = true;
        return;
    }

    placePopover(menu, e.clientX + scrollX + 8, e.clientY + scrollY + 8);
});

/* What a harness may ask. The same shape the two real pages offer, for
   the same reason: a browser test that reaches into a module's internals
   is a test of the internals. */
window.tiler = {
    panes: () => [...CATALOG],
    layout: () => panes.layout(),
    pane: (what, ...args) => panes[what](...args),
    tiled: () => panes.tiled(),
    drawing: () => ({ paint: paint.asking(), plot: plot.asking() }),
    frames: () => ({ paint: paint.frames(), plot: plot.frames() }),
    told: () => [...told],
    forget: () => told.splice(0, told.length),
};

$('say').textContent = 'ready';
