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
 * panes.js -- the page as tiles rather than as a scroll.
 *
 * What this page shows are panels somebody wants side by side: the piece,
 * the keys, the parameters, the graph. The document stacks them, because
 * that is what a document does, and on a screen with room for four of
 * them at once that is three of them scrolled out of sight.
 *
 * It adopts the document; it creates no content. Every element it shows
 * is one already in the page, marked `data-pane', moved into a layout and
 * put back where it was when the layout goes away. Every id survives and
 * every module keeps the element it was handed, which is what lets the
 * page work with the tiler off -- and the page with the tiler off is the
 * fallback, not a second mobile layout.
 *
 * THE CONTRACT THAT MATTERS is `onShow'. A pane that is not in front of
 * anybody is a pane whose work can stop: composerview.js and canvasview.js
 * already say so for a folded-away <details>, and a tab nobody has
 * selected is the same claim with more to gain from it. So the one thing
 * the page has to do about tiling is answer onShow, and the one thing this
 * file promises is to ask.
 *
 * WHAT IT DOES NOT DO: lay anything out itself. The tree is nested flex
 * boxes with a fraction each, the browser does the arithmetic, and the
 * two components that care about their own width -- the canvases and the
 * keys -- observe their own elements exactly as they do now. There is no
 * measurement loop here and no per-frame JavaScript.
 */

/* The screen a tiled layout is worth having on. Both halves matter, and
   style.css already argues for the second: a finger is not a mouse, and a
   divider you cannot grab is worse than no divider. */
const TILED = '(min-width: 60em) and (pointer: fine)';

/* What the page is asking for, before the screen gets a say: `?panes=1'
   turns the tiler on, `?panes=0' refuses it, neither leaves the page's own
   default. The screen decides separately, so a narrow window with
   ?panes=1 is still the document -- which is the fallback working rather
   than the query string being ignored. */
function asked (fallback)
{
    const p = new URLSearchParams(location.search).get('panes');

    return p === null ? fallback : p === '1';
}

export function createPanes ({ root, catalog, onShow = () => {},
                               on = false })
{
    /* id -> the element, where it came from, and what it is worth. In
       the catalog's order, which is the document's. */
    const panes = new Map();

    /* id -> what onShow was last told, so it is told only of changes. */
    const shown = new Map();

    const media = matchMedia(TILED);
    const wanted = asked(on);

    let tiled = false;
    let over = null;

    for (const id of catalog)
    {
        const el = document.getElementById(id);

        /* A page that does not have this one. The catalogs are two
           lists over two documents that share most of their panes, and
           the one that is missing is not an error -- see panecheck.mjs,
           which is where a name that is in neither is caught. */
        if (el === null || !el.hasAttribute('data-pane'))
            continue;

        /* A <details>'s summary is its title, already written and already
           right; the attribute is for everything else. */
        const summary = el.tagName === 'DETAILS'
            ? el.querySelector(':scope > summary') : null;

        panes.set(id, {
            id, el, summary,
            title: el.dataset.paneTitle ?? summary?.textContent.trim() ?? id,
            min: Number(el.dataset.paneMin) || 240,

            /* Where it came from. A remembered sibling is no address:
               the panes around this one are being moved too, so by the
               time this is put back the element it used to be before may
               itself be somewhere else. A <template> left in its place
               is an address that cannot move and shows nothing. */
            slot: document.createElement('template'),

            host: null,             /* the section it is shown in */
            wasOpen: true,          /* the fold it had before adoption */
        });

        if (summary !== null)
            el.addEventListener('toggle', () => settle());
    }

    /* Whether a pane's work is worth doing.
     *
     * Three ways for the answer to be no and one thing done about all
     * three: the mode it belongs to is not up (`hidden', which is what
     * available() sets), it is folded away, or -- once there is a layout
     * to be out of -- it is not in it. */
    const visible = (p) =>
        !p.el.hidden && (tiled || p.summary === null || p.el.open);

    /* What each pane's work is told, and only where the answer changed.
       Called after anything that could have moved one: a fold, a mode, a
       layout. */
    const settle = () =>
    {
        for (const [id, p] of panes)
        {
            const now = visible(p);

            if (shown.get(id) !== now)
            {
                shown.set(id, now);
                onShow(id, now);
            }
        }
    };

    /* ---- adopting, and putting back ---- */

    const adopt = (p) =>
    {
        if (p.host !== null)
            return p.host;

        const head = document.createElement('div');
        const body = document.createElement('div');

        head.className = 'panehead';
        head.textContent = p.title;
        body.className = 'panebody';

        p.host = document.createElement('section');
        p.host.className = 'pane';
        p.host.id = `pane-${p.id}`;
        p.host.setAttribute('role', 'region');
        p.host.setAttribute('aria-label', p.title);
        p.host.style.minWidth = `${p.min}px`;

        p.el.replaceWith(p.slot);
        body.append(p.el);
        p.host.append(head, body);

        /* Forced open, and the summary hidden: the pane's own header is
           the disclosure now. What it was folded to is kept for the way
           back. */
        if (p.summary !== null)
        {
            p.wasOpen = p.el.open;
            p.el.open = true;
            p.summary.hidden = true;
        }

        return p.host;
    };

    const restore = (p) =>
    {
        if (p.host === null)
            return;

        if (p.summary !== null)
        {
            p.el.open = p.wasOpen;
            p.summary.hidden = false;
        }

        p.slot.replaceWith(p.el);
        p.host.remove();
        p.host = null;
    };

    /* ---- the layout ---- */

    /* One column in the catalog's order, which is the document's: what
       lands first is the adoption and the contract, not the tiling. */
    const render = () =>
    {
        if (!tiled)
        {
            for (const p of panes.values())
                restore(p);

            root.replaceChildren();
            settle();

            return;
        }

        root.replaceChildren(...[...panes.values()].map(adopt));
        settle();
    };

    const apply = () =>
    {
        const want = wanted && media.matches;

        if (want === tiled)
            return;

        tiled = want;
        document.body.classList.toggle('tiled', tiled);
        render();
    };

    media.addEventListener('change', apply);

    tiled = wanted && media.matches;
    document.body.classList.toggle('tiled', tiled);
    render();

    return {
        /* Whether the mode this pane belongs to is up. Availability is
           not visibility: an unavailable pane leaves the layout without
           being forgotten by it, so switching to a piece and back puts
           the patch's source box where it was. */
        available: (id, on) =>
        {
            const p = panes.get(id);

            if (p === undefined || p.el.hidden === !on)
                return;

            p.el.hidden = !on;
            settle();
        },

        /* Whether a pane is in front of anybody now -- the same answer
           onShow was last given, for a page that has to ask rather than
           wait to be told. */
        visible: (id) =>
        {
            const p = panes.get(id);

            return p !== undefined && visible(p);
        },

        /* One element over every pane, for the things that sit beside
           what they belong to rather than inside it: the composer
           canvas's params, the node canvas's port menu. A pane scrolls,
           and a popover inside a scroller is clipped by it.
         *
           At the document's origin and of no size, so what is placed in
           it is placed in page coordinates exactly as it was when the
           body held it. */
        overlay: () =>
        {
            if (over === null)
            {
                over = document.createElement('div');
                over.className = 'paneoverlay';
                document.body.append(over);
            }

            return over;
        },

        tiled: () => tiled,
    };
}
