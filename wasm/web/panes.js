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
 *
 * THE TREE is splits with a direction and a fraction per child, and
 * leaves holding panes:
 *
 *     { dir: 'row', size: [0.62, 0.38], kids: [
 *         { dir: 'col', size: [0.7, 0.3], kids: [
 *             { tabs: ['composerview'] },
 *             { tabs: ['roll'] } ] },
 *         { tabs: ['paramview'] } ] }
 *
 * Which is data, and comes from the page: one default per mode, in the
 * page's own script, because this file ships no opinion about where the
 * keyboard goes. What a person does to it is kept under the page and the
 * mode, and a saved layout that names a pane this page has never heard
 * of drops it rather than being thrown away.
 *
 * A leaf holds more than one pane as tabs, and the panes no leaf holds
 * are the DRAWER: listed above the layout, one click from being put back.
 * Nothing is ever destroyed, and two canvases stacked as tabs is where
 * the tiling pays for itself -- the one behind stops drawing.
 */

/* The screen a tiled layout is worth having on. Both halves matter, and
   style.css already argues for the second: a finger is not a mouse, and a
   divider you cannot grab is worse than no divider. */
const TILED = '(min-width: 60em) and (pointer: fine)';

/* A divider's thickness and a leaf's own floor, both in CSS pixels and
   both also in panes.css: the arithmetic that refuses a drag has to agree
   with what the browser draws, and two numbers that have to agree are
   written where each is used. */
const SPLIT = 6;
const LEAF = 64;

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

export function createPanes ({ root, catalog, layouts, mode,
                               store = 'panes', onShow = () => {},
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
    let above = null;           /* the overlay, once anything wants one */
    let hint = null;            /* where a dragged tab would land */

    /* The layout that is up, and which of the page's it came from. */
    let tree = null;
    let where = mode;

    /* What each render found: which panes are in front of somebody, which
       are in the tree at all, and which node each rendered leaf is. */
    let onScreen = new Set();
    let placed = new Set();
    let seen = new Map();

    /* The leaf the next pane out of the drawer goes into, and the one
       the keyboard is in. */
    let focus = null;

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
        !p.el.hidden &&
        (tiled ? onScreen.has(p.id) : p.summary === null || p.el.open);

    /* Whether a pane is in play at all: this page has it and the mode it
       belongs to is up. Everything the layout does is over these. */
    const playable = (id) =>
    {
        const p = panes.get(id);

        return p !== undefined && !p.el.hidden;
    };

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

        const body = document.createElement('div');

        body.className = 'panebody';

        /* The leaf's tab strip is this pane's header, so the pane itself
           is the region and nothing more: a box with what was adopted in
           it, labelled by the tab that raises it. */
        p.host = document.createElement('section');
        p.host.className = 'pane';
        p.host.id = `pane-${p.id}`;
        p.host.setAttribute('role', 'region');
        p.host.setAttribute('aria-label', p.title);

        p.el.replaceWith(p.slot);
        body.append(p.el);
        p.host.append(body);

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

    /* A leaf is a node with panes in it; anything else is a split. */
    const isLeaf = (node) => Array.isArray(node.tabs);

    /* What is left of a node once the panes the mode does not have are
       taken out of it. The tree keeps them -- an unavailable pane leaves
       the layout without being forgotten by it, so coming back to a mode
       puts its panes where they were -- and every walk below asks this
       rather than reading `kids' and `tabs' directly. */
    const liveTabs = (leaf) => leaf.tabs.filter(playable);

    const alive = (node) =>
        isLeaf(node) ? liveTabs(node).length > 0 : node.kids.some(alive);

    const liveKids = (node) => node.kids.filter(alive);

    /* How narrow a node may be made: a pane's own minimum, a row's the
       sum of its children's with the dividers between them, a column's
       the widest of them. And the same the other way up, where what a
       leaf asks for is a header and a line under it -- a pane has a
       width it is worth having and no height that means anything. */
    const minAcross = (node, row) =>
    {
        if (isLeaf(node))
            return row ? Math.max(...liveTabs(node).map(
                             (id) => panes.get(id).min))
                       : LEAF;

        const kids = liveKids(node);
        const mins = kids.map((k) => minAcross(k, row));

        return (node.dir === 'row') === row
            ? mins.reduce((a, b) => a + b, 0) + (kids.length - 1) * SPLIT
            : Math.max(...mins);
    };

    /* ---- what a layout is worth keeping ---- */

    /* A saved layout, read back. Anything that is not a tree of the
       shape this file writes is not one: localStorage is a place other
       things write too, and a layout half-read is worse than the
       default. */
    const sane = (node) =>
        node !== null && typeof node === 'object' &&
        (Array.isArray(node.tabs)
            ? node.tabs.every((id) => typeof id === 'string')
            : ['row', 'col'].includes(node.dir) &&
              Array.isArray(node.kids) && node.kids.length > 1 &&
              Array.isArray(node.size) &&
              node.size.length === node.kids.length &&
              node.size.every((f) => typeof f === 'number' && f > 0) &&
              node.kids.every(sane));

    /* And a pane this page has never heard of, dropped -- which is not an
       error and resets nothing. The other way round is the drawer: a pane
       the layout has never seen is simply not in it. */
    const known = (node) =>
    {
        if (isLeaf(node))
        {
            const tabs = node.tabs.filter((id) => panes.has(id));

            return tabs.length === 0
                ? null
                : { tabs, active: Math.min(node.active ?? 0, tabs.length - 1) };
        }

        const kids = [], size = [];

        node.kids.forEach((k, i) =>
        {
            const kept = known(k);

            if (kept !== null)
            {
                kids.push(kept);
                size.push(node.size[i]);
            }
        });

        return kids.length === 0 ? null
             : kids.length === 1 ? kids[0]
             : { dir: node.dir, size, kids };
    };

    const key = () => `${store}:${where}`;

    const save = () =>
    {
        try
        {
            localStorage.setItem(key(), JSON.stringify(tree));
        }
        catch
        {
            /* A browser that refuses to remember is a browser that
               opens on the default, which is a layout and not a
               failure. */
        }
    };

    /* The layout for the mode that is up: what somebody last left, or the
       page's own default for it. */
    const load = () =>
    {
        let saved = null;

        try
        {
            saved = JSON.parse(localStorage.getItem(key()));
        }
        catch
        {
            saved = null;
        }

        const from = saved !== null && sane(saved)
            ? saved
            : structuredClone(layouts?.[where] ?? { tabs: [...panes.keys()] });

        tree = known(from) ?? { tabs: [...panes.keys()] };
    };

    /* ---- moving a pane about ---- */

    /* Which leaf holds a pane, and which split holds a node. The tree is
       small -- a handful of leaves -- so it is walked rather than kept
       with parent links, which would be one more thing for a saved
       layout to be wrong about. */
    const leafWith = (id, node = tree) =>
        isLeaf(node)
            ? (node.tabs.includes(id) ? node : null)
            : node.kids.reduce((f, k) => f ?? leafWith(id, k), null);

    const parentOf = (target, node = tree) =>
        isLeaf(node) ? null
            : node.kids.includes(target) ? node
            : node.kids.reduce((f, k) => f ?? parentOf(target, k), null);

    const firstLeaf = (node = tree) =>
        isLeaf(node) ? node : firstLeaf(node.kids[0]);

    /* The element a leaf was last drawn as, which is how anything that
       needs pixels -- whether a split would fit, where a drop would land
       -- asks the browser rather than working them out again. */
    const boxOf = (leaf) =>
        [...seen].find(([, node]) => node === leaf)?.[0];

    /* A leaf with nothing left in it, taken out of the tree, and the
       split above it collapsed if that leaves it with one child --
       a split of one is not a split. */
    const empty = (leaf) =>
    {
        const up = parentOf(leaf);

        if (up === null)
        {
            tree = { tabs: [], active: 0 };
            return;
        }

        const i = up.kids.indexOf(leaf);

        up.kids.splice(i, 1);
        up.size.splice(i, 1);

        if (up.kids.length > 1)
            return;

        const only = up.kids[0];
        const over = parentOf(up);

        if (over === null)
            tree = only;
        else
            over.kids[over.kids.indexOf(up)] = only;
    };

    /* Out of the layout: into the drawer, which is where every pane not
       in the tree is. Nothing is destroyed -- a drawer is the whole
       reason closing a pane is not losing it. */
    const drawer = (id) =>
    {
        const leaf = leafWith(id);

        if (leaf === null)
            return;

        leaf.tabs.splice(leaf.tabs.indexOf(id), 1);
        leaf.active = Math.max(0, Math.min(leaf.active ?? 0,
                                           leaf.tabs.length - 1));

        if (leaf.tabs.length === 0)
            empty(leaf);
    };

    /* Into a leaf, as the tab in front of it. */
    const into = (id, leaf) =>
    {
        if (leafWith(id) === leaf && leaf.tabs.length === 1)
            return;

        drawer(id);
        leaf.tabs.push(id);
        leaf.active = leaf.tabs.length - 1;
    };

    /* And beside one, which is what splitting is: the leaf is replaced by
       a split holding it and the newcomer, each with half of what the
       leaf had. Refused where the two could not both have their minimum
       -- a split nobody can see either side of is not a split. */
    const beside = (id, leaf, dir, after) =>
    {
        const from = leafWith(id);

        if (from === leaf && leaf.tabs.length === 1)
            return;

        drawer(id);

        const made = { tabs: [id], active: 0 };
        const split = { dir, size: [0.5, 0.5],
                        kids: after ? [leaf, made] : [made, leaf] };
        const up = parentOf(leaf);

        if (up === null)
            tree = split;
        else
            up.kids[up.kids.indexOf(leaf)] = split;
    };

    /* Whether a leaf has room to be split in a direction: both halves
       have to fit what the panes under them asked for. */
    const splittable = (leaf, id, dir) =>
    {
        const box = boxOf(leaf);

        if (box === undefined)
            return false;

        const rect = box.getBoundingClientRect();
        const row = dir === 'row';
        const want = minAcross(leaf, row) +
                     (row ? panes.get(id).min : LEAF) + SPLIT;

        return (row ? rect.width : rect.height) >= want;
    };

    /* ---- dragging a tab ---- */

    /* Where a tab would land if it were let go here: a leaf to be moved
       into, an edge of one to be split off, or the drawer.
     *
     * An edge is the outer fifth of the leaf, which is enough to aim at
     * with a pointer and small enough that the middle -- the common
       answer, "put it in this one" -- is most of the box.
     */
    const EDGE = 0.2;

    const under = (x, y, id) =>
    {
        const at = document.elementFromPoint(x, y);

        if (at === null)
            return null;

        if (at.closest('.panedrawer') !== null)
            return { drop: 'drawer' };

        const box = at.closest('.paneleaf');
        const leaf = seen.get(box);

        if (leaf === undefined)
            return null;

        const r = box.getBoundingClientRect();
        const fx = (x - r.left) / r.width;
        const fy = (y - r.top) / r.height;
        const edge =
            fx < EDGE ? ['row', false] : fx > 1 - EDGE ? ['row', true]
          : fy < EDGE ? ['col', false] : fy > 1 - EDGE ? ['col', true]
          : null;

        if (edge === null || !splittable(leaf, id, edge[0]))
            return { drop: 'into', leaf, box };

        return { drop: 'beside', leaf, box, dir: edge[0], after: edge[1] };
    };

    /* What the drop would do, drawn over the pane it would do it to. */
    const mark = (where) =>
    {
        if (hint === null)
        {
            hint = el('panedrop');
            hint.hidden = true;
            root.append(hint);
        }

        if (where === null)
        {
            hint.hidden = true;
            return;
        }

        const r = (where.box ?? root).getBoundingClientRect();
        const o = root.getBoundingClientRect();
        const half = where.drop === 'beside';
        const row = where.dir === 'row';

        hint.hidden = false;
        hint.style.left = `${r.left - o.left +
            (half && row && where.after ? r.width / 2 : 0)}px`;
        hint.style.top = `${r.top - o.top +
            (half && !row && where.after ? r.height / 2 : 0)}px`;
        hint.style.width = `${half && row ? r.width / 2 : r.width}px`;
        hint.style.height = `${half && !row ? r.height / 2 : r.height}px`;
    };

    /* A tab, dragged. Pointer events rather than the browser's own drag:
       what is being moved is a box in a layout, and what has to be shown
       while it moves is where it would land, which the browser's drag
       image knows nothing about. */
    const grab = (tab, id) =>
    {
        tab.addEventListener('pointerdown', (e) =>
        {
            const from = { x: e.clientX, y: e.clientY };
            let dragging = false;
            let where = null;

            const move = (m) =>
            {
                if (!dragging &&
                    Math.hypot(m.clientX - from.x, m.clientY - from.y) < 5)
                    return;

                dragging = true;
                tab.classList.add('dragging');
                where = under(m.clientX, m.clientY, id);
                mark(where);
            };

            const up = () =>
            {
                tab.removeEventListener('pointermove', move);
                tab.removeEventListener('pointerup', up);
                tab.removeEventListener('pointercancel', up);
                tab.classList.remove('dragging');
                mark(null);

                if (!dragging || where === null)
                    return;

                if (where.drop === 'drawer')
                    drawer(id);
                else if (where.drop === 'into')
                    into(id, where.leaf);
                else
                    beside(id, where.leaf, where.dir, where.after);

                save();
                render();
            };

            /* The capture is what makes elementFromPoint the question
               being asked: without it the tab stops hearing the pointer
               the moment it leaves its own box. */
            tab.setPointerCapture(e.pointerId);
            tab.addEventListener('pointermove', move);
            tab.addEventListener('pointerup', up);
            tab.addEventListener('pointercancel', up);
        });
    };

    /* ---- drawing it ---- */

    const el = (cls) =>
    {
        const d = document.createElement('div');

        d.className = cls;

        return d;
    };

    /* A leaf: a strip of tabs and a host per pane, with the one in front
     * shown and the rest kept beside it.
     *
     * The strip is the pane's header when there is one pane, and a row of
     * them when there are more; it is the same element either way, which
     * is why a <details> adopted here can hand its disclosure over to it
     * without the page having two kinds of header to style.
     */
    const leafOf = (leaf) =>
    {
        const ids = liveTabs(leaf);
        const box = el('paneleaf');
        const strip = el('panetabs');

        leaf.active = Math.min(Math.max(leaf.active ?? 0, 0), ids.length - 1);
        strip.setAttribute('role', 'tablist');
        strip.setAttribute('aria-label', 'Panes');

        ids.forEach((id, i) =>
        {
            const p = panes.get(id);
            const host = adopt(p);
            const tab = document.createElement('button');
            const front = i === leaf.active;

            tab.type = 'button';
            tab.className = 'panetab';
            tab.id = `panetab-${id}`;
            tab.textContent = p.title;
            tab.setAttribute('role', 'tab');
            tab.setAttribute('aria-controls', host.id);
            tab.setAttribute('aria-selected', String(front));

            /* Roving: one stop for the strip, which is the tab in front.
               Tabbing through a layout should pass the panes, not every
               tab of every one of them. */
            tab.tabIndex = front ? 0 : -1;
            tab.addEventListener('click', () => raise(leaf, i));
            tab.addEventListener('keydown', (e) => along(e, leaf, ids, i));
            grab(tab, id);

            host.hidden = !front;
            host.setAttribute('aria-labelledby', tab.id);

            if (front)
                onScreen.add(id);

            placed.add(id);
            strip.append(tab);
            box.append(host);
        });

        /* Which leaf the next thing out of the drawer goes into, and
           which one the keyboard is in. Captured, because a press that
           lands in a canvas never reaches this box otherwise. */
        box.addEventListener('pointerdown', () => { focus = leaf; }, true);

        leaf.active = Math.min(leaf.active, ids.length - 1);
        box.style.minWidth = `${minAcross(leaf, true)}px`;
        box.style.minHeight = `${LEAF}px`;
        box.prepend(strip);
        seen.set(box, leaf);

        return box;
    };

    /* The tab in front of a leaf, with the focus left where the person
       put it: a tab they clicked is a tab they are on. */
    const raise = (leaf, i) =>
    {
        const id = liveTabs(leaf)[i];

        if (leaf.active === i)
            return;

        leaf.active = i;
        save();
        render();
        root.querySelector(`#panetab-${id}`)?.focus();
    };

    /* Along the strip. Moving the focus moves the tab, which is what a
       tablist does when what a tab shows costs nothing to show. */
    const along = (e, leaf, ids, i) =>
    {
        const step = { ArrowLeft: -1, ArrowRight: 1 }[e.key];
        const to = e.key === 'Home' ? 0
                 : e.key === 'End' ? ids.length - 1
                 : step === undefined ? -1
                 : (i + step + ids.length) % ids.length;

        if (to < 0)
            return;

        raise(leaf, to);
        e.preventDefault();
    };

    /* A divider between two of a split's children, which is the one
       control the layout has of its own.
     *
     * A drag moves a fraction and nothing else: the two panes on either
     * side share what they had between them, every other fraction in the
     * tree is untouched, and the browser lays the result out. Neither
     * side goes below its minimum, which for a row is the widest pane
     * under it and for a column is a header and a line.
     */
    const dividerOf = (node, ia, ib, ea, eb) =>
    {
        const row = node.dir === 'row';
        const bar = el('panesplit');

        const told = () =>
        {
            const share = node.size[ia] / (node.size[ia] + node.size[ib]);

            bar.setAttribute('aria-valuenow', String(Math.round(share * 100)));
        };

        bar.setAttribute('role', 'separator');
        bar.setAttribute('aria-orientation', row ? 'vertical' : 'horizontal');
        bar.setAttribute('aria-valuemin', '0');
        bar.setAttribute('aria-valuemax', '100');
        bar.tabIndex = 0;
        told();

        /* By pixels rather than by fractions: what the two are worth now
           is what the browser made of them, so the drag follows the
           pointer exactly whatever else is in the split. */
        const by = (pixels) =>
        {
            const ra = ea.getBoundingClientRect();
            const rb = eb.getBoundingClientRect();
            const was = row ? ra.width : ra.height;
            const both = was + (row ? rb.width : rb.height);
            const floor = minAcross(node.kids[ia], row);
            const ceiling = both - minAcross(node.kids[ib], row);
            const now = Math.max(floor, Math.min(ceiling, was + pixels));
            const sum = node.size[ia] + node.size[ib];

            if (ceiling < floor)
                return;

            node.size[ia] = sum * (now / both);
            node.size[ib] = sum - node.size[ia];
            ea.style.flexGrow = String(node.size[ia]);
            eb.style.flexGrow = String(node.size[ib]);
            told();
        };

        bar.addEventListener('pointerdown', (e) =>
        {
            let from = row ? e.clientX : e.clientY;

            const move = (m) =>
            {
                const to = row ? m.clientX : m.clientY;

                by(to - from);
                from = to;
            };

            const up = () =>
            {
                bar.removeEventListener('pointermove', move);
                bar.removeEventListener('pointerup', up);
                bar.removeEventListener('pointercancel', up);
                save();
            };

            bar.setPointerCapture(e.pointerId);
            bar.addEventListener('pointermove', move);
            bar.addEventListener('pointerup', up);
            bar.addEventListener('pointercancel', up);
            e.preventDefault();
        });

        /* A fraction moved without a pointer, which is the whole of what
           a separator is for to anybody driving this from the keys. */
        bar.addEventListener('keydown', (e) =>
        {
            const step = { ArrowLeft: -1, ArrowUp: -1,
                           ArrowRight: 1, ArrowDown: 1 }[e.key];

            if (step === undefined)
                return;

            by(step * 16);
            save();
            e.preventDefault();
        });

        /* Both sides of it, equal. */
        bar.addEventListener('dblclick', () =>
        {
            const kids = liveKids(node);
            const share = kids.reduce(
                (a, k) => a + node.size[node.kids.indexOf(k)], 0) /
                kids.length;

            for (const k of kids)
                node.size[node.kids.indexOf(k)] = share;

            save();
            render();
        });

        return bar;
    };

    /* A split: its live children with their fractions as `flex-grow',
       and a divider between each pair. A split with one live child is
       that child -- there is nothing to divide. */
    const nodeOf = (node) =>
    {
        if (isLeaf(node))
            return leafOf(node);

        const kids = liveKids(node);

        if (kids.length === 1)
            return nodeOf(kids[0]);

        const box = el('panebox');
        const made = [];

        box.dataset.dir = node.dir;

        for (const k of kids)
        {
            const child = nodeOf(k);

            child.style.flexGrow = String(node.size[node.kids.indexOf(k)]);
            made.push(child);
        }

        made.forEach((child, i) =>
        {
            if (i > 0)
                box.append(dividerOf(node, node.kids.indexOf(kids[i - 1]),
                                     node.kids.indexOf(kids[i]),
                                     made[i - 1], child));

            box.append(child);
        });

        return box;
    };

    /* The panes no leaf has room for, listed above the layout: one click
       from being put back, into whichever leaf was last touched. Nothing
       here is a pane that has gone -- a drawer is what makes closing one
       something other than losing it. */
    const drawerOf = () =>
    {
        const box = el('panedrawer');
        const out = [...panes.keys()].filter(
            (id) => playable(id) && !placed.has(id));

        box.hidden = out.length === 0;

        for (const id of out)
        {
            const button = document.createElement('button');

            button.type = 'button';
            button.className = 'paneclosed';
            button.textContent = panes.get(id).title;
            button.addEventListener('click', () =>
            {
                into(id, focus ?? firstLeaf());
                save();
                render();
            });

            grab(button, id);
            box.append(button);
        }

        return box;
    };

    const render = () =>
    {
        if (!tiled)
        {
            for (const p of panes.values())
                restore(p);

            onScreen = new Set();
            placed = new Set();
            seen = new Map();
            hint = null;
            root.replaceChildren();
            settle();

            return;
        }

        if (tree === null)
            load();

        /* Adopted whether or not the layout has room for it: a pane in
           the drawer is one this page still owns and still has to be
           able to put back. */
        for (const p of panes.values())
            adopt(p);

        onScreen = new Set();
        placed = new Set();
        seen = new Map();
        hint = null;

        const made = alive(tree) ? nodeOf(tree) : el('paneleaf');

        /* The ones the layout has no room for, kept out of sight but in
           the document. Out of the document they would be out of
           getElementById too, and every module on this page was handed
           its element by name -- a pane in the drawer is put away, not
           taken apart. */
        const kept = el('panekeep');

        kept.hidden = true;

        for (const p of panes.values())
            if (!placed.has(p.id))
                kept.append(p.host);

        /* A leaf that went away takes the focus with it: a split that
           collapsed is not a place to put the next pane into. */
        if (focus !== null && boxOf(focus) === undefined)
            focus = null;

        root.replaceChildren(drawerOf(), made, kept);
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

            if (tiled)
                render();
            else
                settle();
        },

        /* Which layout is up. The modes have different panes, so they
           have a layout each -- a default of the page's and whatever
           somebody has made of it since, kept under the page and the
           mode. */
        mode: (name) =>
        {
            if (name === where)
                return;

            where = name;
            tree = null;

            if (tiled)
                render();
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
            if (above === null)
            {
                above = document.createElement('div');
                above.className = 'paneoverlay';
                document.body.append(above);
            }

            return above;
        },

        /* The layout as it stands, which is what is kept and what comes
           back. A copy: what somebody reads it for is to compare it with
           itself later. */
        layout: () => (tree === null ? null : structuredClone(tree)),

        tiled: () => tiled,
    };
}
