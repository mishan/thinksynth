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
 * A pane is closed to it by the cross on its own tab, by Alt W, or by
 * dragging its tab onto the drawer, and reopened by the button there
 * with its name on. Nothing is ever destroyed -- closing a pane is
 * putting it away, which is why there is nowhere here that makes one.
 *
 * Two canvases stacked as tabs is where the tiling pays for itself: the
 * one behind stops drawing.
 *
 * WHAT IS POLICY AND NOT MECHANISM is an argument, with this page's
 * answer as the default: the screen worth tiling on (`media'), a
 * divider's thickness and a leaf's floor (`split', `leaf'), how much of
 * a leaf's edge is an edge (`edge'), the query parameter that turns it
 * on (`param'), where a layout is kept (`storage'), and the chords
 * (`keys'). None of them is a rule -- they are what this page would have
 * hardcoded, written where somebody else can disagree.
 */

/* The screen a tiled layout is worth having on. Both halves matter, and
   style.css already argues for the second: a finger is not a mouse, and a
   divider you cannot grab is worse than no divider. */
const MEDIA = '(min-width: 60em) and (pointer: fine)';

/* A divider's thickness and a leaf's own floor, in CSS pixels. The first
   of these is also drawn, and the drawing reads it from the root as
   `--pane-split' rather than being told it twice -- the arithmetic that
   refuses a drag and the line the browser paints have to be the same
   number, and a comment asking two numbers to agree is not a mechanism. */
const SPLIT = 6;
const LEAF = 64;

/* How much of a leaf's edge is an edge: the outer fifth, which is enough
   to aim at with a pointer and leaves the middle -- the common answer,
   "put it in this one" -- most of the box. */
const EDGE = 0.2;

/* The attribute that means a pane's mode is not up.
 *
 * Its own and not `hidden'. Reading `hidden' is right for a page that
 * uses the attribute for this and nothing else, and wrong the moment a
 * page uses it for ordinary showing and hiding: a pane something else
 * hid would leave the layout without anything having said so, and the
 * layout would not put it back. panes.css draws the consequence, so the
 * attribute hides the element in the document as well.
 */
const OFF = 'data-pane-off';

const off = (el) => el.hasAttribute(OFF);

/*
 * What a command is, which is the one piece of this a page is most
 * likely to want different.
 *
 * Every command is a chord with Alt in it, and that is not this page's
 * peculiarity: anywhere the bare letters are already something -- notes
 * here, a chat composer elsewhere -- a tiler that took W for itself
 * would have taken them. It stays the default for that reason and is an
 * argument for when it is wrong.
 *
 * By `code' rather than by `key': a command is a place on the keyboard,
 * and Alt over a letter is a different letter on half the layouts there
 * are. The arrows are not in here because they are directions rather
 * than letters, and a layout where Up is not up is not a layout.
 */
const KEYS = {
    chord: (e) => e.altKey && !e.ctrlKey && !e.metaKey,
    splitRow: ['Backslash'],
    splitCol: ['Minus'],
    zoom: ['Enter', 'NumpadEnter'],
    close: ['KeyW'],
    reset: ['Digit0'],
};

/* Where a layout is kept between visits. localStorage by default and an
   object with the same three methods when a page has somewhere better --
   a profile on a server is the first thing anybody with accounts wants.
   Read lazily: a browser that refuses storage altogether throws on the
   property, and every call here is inside a try. */
const KEEP = {
    getItem: (k) => localStorage.getItem(k),
    setItem: (k, v) => localStorage.setItem(k, v),
    removeItem: (k) => localStorage.removeItem(k),
};

/* What the page is asking for, before the screen gets a say: `?panes=1'
   turns the tiler on, `?panes=0' refuses it, neither leaves the page's own
   default. The screen decides separately, so a narrow window with
   ?panes=1 is still the document -- which is the fallback working rather
   than the query string being ignored. */
function asked (param, fallback)
{
    const p = new URLSearchParams(location.search).get(param);

    return p === null ? fallback : p === '1';
}

/*
 * A popover at a page coordinate, held inside the window.
 *
 * What asks for one of these is a canvas, and what it says is where in
 * its own pixels -- so the page adds where the canvas is and gets a page
 * coordinate, which is what these are positioned in and has not changed
 * with tiling. What has changed is what is around them: a pane can be
 * narrower than the popover's own maximum width and is a box that
 * scrolls, so one placed beside a handle near the right of a pane went
 * off the window rather than merely off the box. It is a bug that was
 * there before and that a 60em document rarely showed.
 *
 * Shown first and measured after, because a popover's size is what is in
 * it and what is in it was just written.
 */
export function placePopover (box, x, y)
{
    const pad = 8;

    box.hidden = false;

    const left = Math.max(scrollX + pad,
                          Math.min(x, scrollX + innerWidth -
                                      box.offsetWidth - pad));
    const top = Math.max(scrollY + pad,
                         Math.min(y, scrollY + innerHeight -
                                     box.offsetHeight - pad));

    box.style.left = `${left}px`;
    box.style.top = `${top}px`;
}

export function createPanes ({ root, catalog, layouts, mode,
                               store = 'panes', editing = '',
                               onShow = () => {}, on = false,
                               media = MEDIA, split = SPLIT, leaf = LEAF,
                               edge = EDGE, param = 'panes',
                               storage = KEEP, keys = {} })
{
    /* The commands, with a page's own over the defaults rather than
       instead of them: overriding the one chord that clashes should not
       cost you the other five. */
    const keymap = { ...KEYS, ...keys };

    /* The class every rule in panes.css keys on, put here rather than
       asked of the page: what the layout is drawn into is whatever
       element this was handed, and a stylesheet naming the id one page
       happened to choose was a stylesheet naming its one consumer. */
    root.classList.add('panesroot');

    /* And the one number the drawing and the arithmetic share, written
       where the drawing can read it. */
    root.style.setProperty('--pane-split', `${split}px`);

    /* Where a key means editing rather than a command: a chord typed
       into a text box is text. Wider than keyfocus.js's idea of a text
       box on purpose -- a list and a slider answer to the arrow keys
       themselves, and Alt with an arrow is close enough to those to
       leave alone. */
    const editable = ['textarea', 'input', 'select', '[contenteditable]',
                      ...(editing ? [editing] : [])].join(', ');

    /* id -> the element, where it came from, and what it is worth. In
       the catalog's order, which is the document's. */
    const panes = new Map();

    /* id -> what onShow was last told, so it is told only of changes. */
    const shown = new Map();

    /* id -> the leaf it was last in, so that reopening a pane is putting
       it back rather than dropping it wherever the pointer last was.
       The leaf itself and not an address: a split collapses when the
       last pane leaves it, and every index around it moves. A leaf the
       tree no longer holds is no answer, and then the drawer falls back
       to the leaf that was last touched -- which is what it always
       did. */
    const home = new Map();

    const screen = matchMedia(media);
    const wanted = asked(param, on);

    let tiled = false;
    let above = null;           /* the overlay, once anything wants one */
    let hint = null;            /* where a dragged tab would land */

    /* The layout that is up, and which of the page's it came from. */
    let tree = null;
    let where = mode;

    /* What each render found: which panes are in front of somebody, which
       were put into the document at all, and which node each rendered
       leaf is. */
    let onScreen = new Set();
    let attached = new Set();
    let seen = new Map();

    /* The leaf the next pane out of the drawer goes into, and the one
       the keyboard is in. */
    let focus = null;

    /* The leaf filling the layout, if one is. Zoom is what makes tiling
       bearable on a laptop and it costs nothing: the rest are not drawn,
       and onShow fires for everything that just left the screen. */
    let zoom = null;

    for (const id of catalog)
    {
        const el = document.getElementById(id);

        /* A page that does not have this one. The catalogs are two
           lists over two documents that share most of their panes, and
           the one that is missing here is not an error -- a name listed
           by a page and marked in no element of it is catalogcheck.mjs's
           to catch, before a browser is ever opened. */
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
     * three: the mode it belongs to is not up (the attribute available()
     * sets), it is folded away, or -- once there is a layout to be out
     * of -- it is not in it. */
    const visible = (p) =>
        !off(p.el) &&
        (tiled ? onScreen.has(p.id) : p.summary === null || p.el.open);

    /* Whether a pane is in play at all: this page has it and the mode it
       belongs to is up. Everything the layout does is over these. */
    const playable = (id) =>
    {
        const p = panes.get(id);

        return p !== undefined && !off(p.el);
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

    /* What a fraction is worth as `flex-grow': its share of the live
     * children's, rather than the number itself.
     *
     * The numbers are shares of a split and a split's children come and
     * go -- one closed to the drawer, one whose mode is not up -- so
     * what is left of them sums to less than one. A `flex-grow' under
     * one is the CSS rule nobody means: the children take that much of
     * the box and the remainder is a gap with no pane in it and no
     * divider to drag. Closing the third of three panes used to leave a
     * fifth of the column behind that way, dead and unreclaimable.
     * Dividing by what they come to is the whole of the fix.
     */
    const grow = (node, i) =>
    {
        const total = liveKids(node).reduce(
            (a, k) => a + node.size[node.kids.indexOf(k)], 0);

        return String(node.size[i] / (total > 0 ? total : 1));
    };

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
                       : leaf;

        const kids = liveKids(node);
        const mins = kids.map((k) => minAcross(k, row));

        return (node.dir === 'row') === row
            ? mins.reduce((a, b) => a + b, 0) + (kids.length - 1) * split
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

            return tabs.length === 0 ? null
                 : { tabs,
                     active: Math.min(node.active ?? 0, tabs.length - 1) };
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
            storage.setItem(key(), JSON.stringify(tree));
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
            saved = JSON.parse(storage.getItem(key()));
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

        /* What was in front stays in front, which is an index that
           shifts when the tab leaving sat before it. Clamping alone
           would leave the index where it was and raise the neighbor
           instead. Counted over the tabs in play, since that is what
           `active' is an index into. */
        const at = liveTabs(leaf).indexOf(id);
        const was = leaf.active ?? 0;

        home.set(id, leaf);

        leaf.tabs.splice(leaf.tabs.indexOf(id), 1);
        leaf.active = Math.max(0, Math.min(at !== -1 && at < was ? was - 1
                                                                : was,
                                           liveTabs(leaf).length - 1));

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
                     (row ? panes.get(id).min : leaf) + split;

        return (row ? rect.width : rect.height) >= want;
    };

    /* ---- dragging a tab ---- */

    /* Where a tab would land if it were let go here: a leaf to be moved
       into, an edge of one to be split off, or the drawer. */
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
        /* The outer `edge' of the box on each side, which is enough to
           aim at with a pointer and leaves the middle -- the common
           answer, "put it in this one" -- most of the box. */
        const side =
            fx < edge ? ['row', false] : fx > 1 - edge ? ['row', true]
          : fy < edge ? ['col', false] : fy > 1 - edge ? ['col', true]
          : null;

        if (side === null || !splittable(leaf, id, side[0]))
            return { drop: 'into', leaf, box };

        return { drop: 'beside', leaf, box, dir: side[0], after: side[1] };
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
                tab.classList.add('panedragging');
                where = under(m.clientX, m.clientY, id);
                mark(where);
            };

            const up = () =>
            {
                tab.removeEventListener('pointermove', move);
                tab.removeEventListener('pointerup', up);
                tab.removeEventListener('pointercancel', up);
                tab.classList.remove('panedragging');
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

            /* The tab and the cross that closes it, in a wrapper of
               their own. A tablist's children are tabs, and a cross is
               not one -- `presentation' is what lets a tablist hold the
               pair and go on owning the tab inside it. It is also what
               keeps a tab's text the pane's title and nothing else. */
            const wrap = el('panetabwrap');
            const tab = document.createElement('button');
            const shut = document.createElement('button');
            const front = i === leaf.active;

            wrap.setAttribute('role', 'presentation');

            tab.type = 'button';
            tab.className = 'panetab';
            tab.id = `panetab-${id}`;
            tab.textContent = p.title;
            tab.setAttribute('role', 'tab');
            tab.setAttribute('aria-controls', host.id);
            tab.setAttribute('aria-selected', String(front));

            /* Roving: one pane, one stop -- and the cross beside it, so
               closing a pane needs no chord to be found. Tabbing through
               a layout should pass the panes, not every tab of every one
               of them. */
            tab.tabIndex = front ? 0 : -1;
            tab.addEventListener('click', () => raise(leaf, i));
            tab.addEventListener('keydown', (e) => along(e, leaf, ids, i));
            grab(tab, id);

            shut.type = 'button';
            shut.className = 'paneshut';
            shut.id = `paneshut-${id}`;
            shut.textContent = '\u00d7';
            shut.title = `Close ${p.title}`;
            shut.setAttribute('aria-label', `Close ${p.title}`);
            shut.tabIndex = front ? 0 : -1;
            shut.addEventListener('click', () => shutTab(id, leaf));

            host.hidden = !front;
            host.setAttribute('aria-labelledby', tab.id);

            if (front)
                onScreen.add(id);

            attached.add(id);
            wrap.append(tab, shut);
            strip.append(wrap);
            box.append(host);
        });

        /* Which leaf the next thing out of the drawer goes into, and
           which one the keyboard is in. Captured, because a press that
           lands in a canvas never reaches this box otherwise. */
        box.addEventListener('pointerdown', () => { focus = leaf; }, true);

        box.style.minWidth = `${minAcross(leaf, true)}px`;
        box.style.minHeight = `${leaf}px`;
        box.prepend(strip);
        seen.set(box, leaf);

        return box;
    };

    /* A pane put away from its own tab, which is the drawer and not the
     * bin.
     *
     * The focus follows it, onto the drawer button that brings it back
     * -- the one thing left on the page that names it. A cross that
     * closed a pane and left the keyboard somewhere up the document is
     * how a person ends up not finding it again, and the drawer is only
     * an answer to that if it is where they are looking.
     */
    const shutTab = (id, leaf) =>
    {
        drawer(id);
        focus = leaf;
        save();
        render();
        root.querySelector(`#panereopen-${id}`)?.focus();
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
            const sum = node.size[ia] + node.size[ib];

            /* Two panes that cannot both have what they asked for: the
               browser has already overflowed them and there is no
               fraction that makes it better. */
            if (ceiling < floor)
                return;

            const now = Math.max(floor, Math.min(ceiling, was + pixels));

            node.size[ia] = sum * (now / both);
            node.size[ib] = sum - node.size[ia];
            ea.style.flexGrow = grow(node, ia);
            eb.style.flexGrow = grow(node, ib);
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

            child.style.flexGrow = grow(node, node.kids.indexOf(k));
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
       from being put back, into the leaf they left or -- where that leaf
       closed with them -- whichever one was last touched. Nothing here
       is a pane that has gone; a drawer is what makes closing one
       something other than losing it. */
    const drawerOf = (out) =>
    {
        const box = el('panedrawer');

        box.hidden = out.length === 0;

        if (out.length > 0)
        {
            /* Said, rather than left to be inferred from a row of
               dashed buttons: what these are is the panes that are not
               on the screen, and a person who has just closed one is
               looking for exactly that sentence. */
            const said = document.createElement('span');

            said.className = 'panedrawerlabel';
            said.textContent = 'Closed:';
            box.append(said);
        }

        for (const id of out)
        {
            const button = document.createElement('button');

            button.type = 'button';
            button.className = 'paneclosed';
            button.id = `panereopen-${id}`;
            button.textContent = panes.get(id).title;
            button.title = `Reopen ${panes.get(id).title}`;
            button.addEventListener('click', () =>
            {
                const back = home.get(id);

                into(id, back !== undefined && holds(back)
                             ? back : focus ?? firstLeaf());
                save();
                render();
                root.querySelector(`#panetab-${id}`)?.focus();
            });

            grab(button, id);
            box.append(button);
        }

        return box;
    };

    /* Which panes the tree holds, whether or not this render drew them:
       what the drawer lists is what no leaf has, and a zoomed pane has
       not closed the rest. */
    let inLayout = new Set();

    const holding = (node, found = new Set()) =>
    {
        if (isLeaf(node))
            node.tabs.filter(playable).forEach((id) => found.add(id));
        else
            node.kids.forEach((k) => holding(k, found));

        return found;
    };

    /* The drawer's list: every pane the mode has that the layout does
       not. Closing one puts it here and nothing else happens to it. */
    const closed = () => [...panes.keys()].filter(
        (id) => playable(id) && !inLayout.has(id));

    /* Whether a node is still part of the tree: a split that collapsed
       took its children's addresses with it. */
    const holds = (target, node = tree) =>
        node === target ||
        (!isLeaf(node) && node.kids.some((k) => holds(target, k)));

    const render = () =>
    {
        const was = document.activeElement;
        const from = was instanceof Element
            ? was.closest('.pane')?.id.replace(/^pane-/, '') ?? null : null;
        const tab = was instanceof Element &&
                    was.classList.contains('panetab') ? was.id : null;

        if (!tiled)
        {
            for (const p of panes.values())
                restore(p);

            onScreen = new Set();
            attached = new Set();
            inLayout = new Set();
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

        if (zoom !== null && (!holds(zoom) || !alive(zoom)))
            zoom = null;

        onScreen = new Set();
        attached = new Set();
        seen = new Map();
        hint = null;
        inLayout = holding(tree);

        const shown = zoom ?? tree;
        const out = closed();
        const made = alive(shown) ? nodeOf(shown) : el('paneleaf');

        /* A layout every pane has been closed out of, which is a blank
           box and reads as a broken page rather than an empty one. The
           drawer above it holds all of them; this says so. */
        if (!alive(shown) && out.length > 0)
        {
            const note = el('paneempty');

            note.textContent = 'Every pane is closed. Reopen one from ' +
                               'the row above.';
            made.append(note);
        }

        /* The ones this render did not draw, kept out of sight but in the
           document. Out of the document they would be out of
           getElementById too, and every module on this page was handed
           its element by name -- a pane put away is not a pane taken
           apart. */
        const kept = el('panekeep');

        kept.hidden = true;

        for (const p of panes.values())
            if (!attached.has(p.id))
                kept.append(p.host);

        /* A leaf that went away takes the focus with it: a split that
           collapsed is not a place to put the next pane into. */
        if (focus !== null && boxOf(focus) === undefined)
            focus = null;

        root.replaceChildren(drawerOf(out), made, kept);
        settle();
        refocus(was, from, tab);
    };

    /* Focus, after the layout has moved under it.
     *
     * Never stolen and never dropped: what somebody was in is still what
     * they are in, unless the pane holding it has just gone off the
     * screen -- and then it is that pane's own tab rather than the top of
     * the page, which is where the browser would have put it.
     */
    const refocus = (was, from, tab) =>
    {
        if (!(was instanceof HTMLElement) || was === document.body)
            return;

        if (was.isConnected && was.checkVisibility())
        {
            if (document.activeElement !== was)
                was.focus();

            return;
        }

        /* `||' and not `??': the first of these is `false' whenever the
           focus was not on a tab, and a nullish fallback does not fall
           through a `false'. */
        const back = (tab !== null && root.querySelector(`#${tab}`)) ||
                     (from !== null &&
                      root.querySelector(`#panetab-${from}`));

        if (back)
            back.focus();
    };

    /* ---- driving it from the keys ----
     *
     * With a constraint this page has and most do not: the letters are
     * the instrument. TypingKeys binds Z-/ and Q-P to notes and - and =
     * to the octave, so every command here is a chord with Alt in it,
     * never a bare letter -- and every one of them is inert while the
     * focus is in a text box or an editor, which is the same rule
     * keyboard.js applies to a note.
     *
     * By `code' rather than by `key', for keyboard.js's reason: a command
     * is a place on the keyboard, and Alt over a letter is a different
     * letter on half the layouts there are.
     */
    const WAY = {
        ArrowLeft: [-1, 0], ArrowRight: [1, 0],
        ArrowUp: [0, -1], ArrowDown: [0, 1],
    };

    /* The leaf a command is about: the one the focus is in, or the one
       last pressed in, or the first there is. */
    const current = () =>
    {
        const at = document.activeElement;
        const box = at instanceof Element ? at.closest('.paneleaf') : null;

        return seen.get(box) ?? (focus !== null && boxOf(focus) !== undefined
                                     ? focus : firstLeaf());
    };

    /* The leaf that way: of the ones whose middle lies in the direction
       the arrow points, the nearest. */
    const toward = (leaf, [dx, dy]) =>
    {
        const here = boxOf(leaf)?.getBoundingClientRect();

        if (here === undefined)
            return null;

        const cx = here.left + here.width / 2;
        const cy = here.top + here.height / 2;
        let best = null;
        let near = Infinity;

        for (const [box, node] of seen)
        {
            if (node === leaf)
                continue;

            const r = box.getBoundingClientRect();
            const x = r.left + r.width / 2 - cx;
            const y = r.top + r.height / 2 - cy;

            if (dx !== 0 && (Math.sign(x) !== dx || Math.abs(x) < Math.abs(y)))
                continue;

            if (dy !== 0 && (Math.sign(y) !== dy || Math.abs(y) < Math.abs(x)))
                continue;

            if (Math.hypot(x, y) < near)
            {
                near = Math.hypot(x, y);
                best = node;
            }
        }

        return best;
    };

    const raiseTab = (leaf) =>
        boxOf(leaf)?.querySelector('.panetab[aria-selected="true"]')?.focus();

    const done = (leaf) =>
    {
        focus = leaf;
        save();
        render();
        raiseTab(leaf);
    };

    const command = (e) =>
    {
        if (!tiled || !keymap.chord(e))
            return;

        if (e.target instanceof Element &&
            e.target.closest(editable) !== null)
            return;

        const leaf = current();
        const ids = liveTabs(leaf);
        const id = ids[leaf.active];
        const way = WAY[e.code];

        /* Everything below Alt and a plain arrow is about a pane, and a
           layout with nothing in it has none. */
        if (id === undefined && !(way !== undefined && !e.shiftKey))
            return;

        if (way !== undefined && !e.shiftKey)
        {
            const to = toward(leaf, way);

            if (to !== null)
            {
                focus = to;
                raiseTab(to);
            }
        }
        else if (way !== undefined)
        {
            /* This pane, that way: into the leaf the arrow points at, or
               -- where there is none -- off the edge of this one into a
               half of its own. */
            const to = toward(leaf, way);
            const dir = way[0] !== 0 ? 'row' : 'col';

            if (to !== null)
                into(id, to);
            else
                beside(id, leaf, dir, way[0] + way[1] > 0);

            done(leafWith(id) ?? leaf);
        }
        else if (keymap.splitRow.includes(e.code) ||
                 keymap.splitCol.includes(e.code))
        {
            /* Split: the pane in front moves into a half of its own. A
               leaf with nothing else in it has nothing to split off, so
               it opens the first pane in the drawer there instead. */
            const dir = keymap.splitRow.includes(e.code) ? 'row' : 'col';
            const other = closed()[0];

            if (ids.length > 1)
                beside(id, leaf, dir, true);
            else if (other !== undefined)
                beside(other, leaf, dir, true);
            else
                return;

            done(leaf);
        }
        else if (keymap.zoom.includes(e.code))
        {
            zoom = zoom === null ? leaf : null;
            render();
            raiseTab(zoom ?? leaf);
        }
        else if (keymap.close.includes(e.code))
        {
            drawer(id);
            done(current());
        }
        else if (keymap.reset.includes(e.code))
        {
            try
            {
                storage.removeItem(key());
            }
            catch
            {
                /* Nothing to forget, which is the same as forgetting. */
            }

            zoom = null;
            tree = null;
            focus = null;
            render();
        }
        else
            return;

        e.preventDefault();
    };

    window.addEventListener('keydown', command);

    const apply = () =>
    {
        const want = wanted && screen.matches;

        if (want === tiled)
            return;

        tiled = want;
        document.body.classList.toggle('tiled', tiled);
        render();
    };

    screen.addEventListener('change', apply);

    tiled = wanted && screen.matches;
    document.body.classList.toggle('tiled', tiled);
    render();

    return {
        /* Whether the mode this pane belongs to is up. Availability is
         * not visibility: an unavailable pane leaves the layout without
         * being forgotten by it, so switching to a piece and back puts
         * the patch's source box where it was.
         *
         * Through `data-pane-off' rather than `hidden'. The attribute is
         * this module's, which is the point: a page that uses `hidden'
         * for ordinary showing and hiding -- and most do -- would
         * otherwise take panes out of the layout by accident and never
         * get them back. panes.css hides the element either way, so a
         * page that starts a pane off says so in the markup with the
         * same attribute. */
        available: (id, on) =>
        {
            const p = panes.get(id);

            if (p === undefined || off(p.el) === !on)
                return;

            p.el.toggleAttribute(OFF, !on);

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

        /* Raised: in front of whatever leaf holds it, and out of the
         * drawer if that is where it was. Nothing to raise it above
         * without a layout -- untiled, the page is the document it
         * always was and every pane is already on it.
         *
         * With the focus by default, since what asks for a pane by name
         * is usually somebody who wants to be in it. `focus: false' is
         * for the other caller: a page raising a box to show somebody
         * a message they did not ask for. A .dsp that did not parse is
         * read by whoever was editing it, and taking the cursor out of
         * the text box to point at the reason costs them their place
         * in it.
         */
        present: (id, { focus: take = true } = {}) =>
        {
            if (!tiled || !panes.has(id) || !playable(id))
                return;

            /* Where it goes when the layout does not have it at all.
               Asked for, it goes where the person is. Raised at them, it
               goes anywhere but there: a box put in front of the .dsp
               somebody was editing, at the moment they pressed a button
               in it, answers one question by hiding another. */
            const at = take ? null : document.activeElement;
            const busy = at instanceof Element
                ? seen.get(at.closest('.paneleaf')) : undefined;
            const away = busy === undefined
                ? null : [...seen.values()].find((n) => n !== busy) ?? null;

            const leaf = leafWith(id) ?? away ?? focus ?? firstLeaf();

            if (leafWith(id) === null)
                into(id, leaf);
            else
                leaf.active = liveTabs(leaf).indexOf(id);

            focus = leaf;
            save();
            render();

            if (take)
                root.querySelector(`#panetab-${id}`)?.focus();
        },

        /* And put away, which is the drawer and not the bin. There is
           no drawer without a layout, for the same reason. */
        close: (id) =>
        {
            if (!tiled || !panes.has(id))
                return;

            drawer(id);
            save();
            render();
        },

        /* What its tab says. The markup's title is what a pane opens
           with; this is for a pane whose title is a file somebody has
           just opened in it. */
        setTitle: (id, text) =>
        {
            const p = panes.get(id);

            if (p === undefined)
                return;

            p.title = text;
            p.host?.setAttribute('aria-label', text);
            render();
        },

        tiled: () => tiled,
    };
}
