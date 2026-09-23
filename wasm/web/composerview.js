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
 * composerview.js -- the piece's picture, on either page.
 *
 * The canvas itself is canvasview.js's and the drawing is the mirror's;
 * this is the little that is particular to the composer view: the buttons
 * that enlarge a stage whose picture is a control, the line that says what
 * is being painted on, and turning what the canvas hands back into
 * whatever the page does with a command.
 *
 * Both pages have it, and it is one file for the reason keyboard.js and
 * panel.js are one file each: two copies of a thing two pages have to
 * agree on is how they stop agreeing.
 *
 * What differs between them is only the last step. The solo page sends a
 * gesture to its own worklet for the next window; the room page stamps it
 * with the knob lead and broadcasts it, and it arrives everywhere -- here
 * included -- at the time it names. Hence `onGesture'.
 */

import { createCanvasView } from './canvasview.js';
import { placePopover } from './popover.js';
import { showPanel } from './panel.js';

export function createComposerView ({ root = document, toMirror,
                                      onGesture, onParamEdit })
{
    const $ = (id) => root.getElementById(id);

    const view = createCanvasView({
        scroller: $('composerscroll'),
        canvas: $('composer'),
        send: toMirror,
    });

    /* The stages whose picture is a control, by "chain.stage", and which
       one is enlarged now. */
    let stages = new Map();
    let enlarged = { chain: -1, stage: -1 };

    const status = () =>
    {
        const which = stages.get(`${enlarged.chain}.${enlarged.stage}`);

        $('composerstatus').textContent = which !== undefined
            ? `Painting ${which}. Drag on it; Escape puts it back. What ` +
              'you paint goes out as a command and arrives at its time, ' +
              'here as on every peer.'
            : stages.size > 0
                ? 'Double-click a picture that is a control to enlarge it.'
                : '';
    };

    /* One button per stage that can be painted on. The canvas enlarges a
       stage on a double-click and puts it back on Escape; these are the
       same two things for a finger, and they are also how anybody finds
       out that a picture is a control at all. */
    const offer = (chains) =>
    {
        const row = $('composerstages');

        row.replaceChildren();
        stages = new Map();

        for (const chain of chains)
            for (const stage of chain.stages)
            {
                if (!stage.takesInput)
                    continue;

                const button = document.createElement('button');

                stages.set(`${chain.chain}.${stage.stage}`,
                           `${stage.name} in ${chain.name}`);

                button.textContent = `Paint ${stage.name} in ${chain.name}`;
                button.addEventListener('click', () => toMirror(
                    { type: 'enlarge', chain: chain.chain,
                      stage: stage.stage }));
                row.append(button);
            }

        enlarged = { chain: -1, stage: -1 };
        status();
    };

    /* Where a stage's params handle is, answered by the worker: what a
       page presses to open the popover without laying the canvas out a
       second time. */
    let handleAsked = null;

    const handleOf = (chain, stage) => new Promise((resolve) =>
    {
        handleAsked = resolve;
        toMirror({ type: 'handle', chain, stage });
    });

    /* True if the message was this view's. */
    const fromMirror = (m) =>
    {
        /* Two canvases are drawn in the mirror now and both send `draw'.
           The other one tags itself (rollview.js); everything untagged is
           this one's, which is the composer view's by seniority. */
        if (m.canvas !== undefined)
            return false;

        switch (m.type)
        {
            case 'handle':
                handleAsked?.(m);
                handleAsked = null;
                return true;
            case 'draw':
                view.frame(m);

                if (m.enlarged.chain !== enlarged.chain ||
                    m.enlarged.stage !== enlarged.stage)
                {
                    enlarged = m.enlarged;
                    status();
                }

                return true;

            /* A piece loaded in the mirror: its chains, and which of
               their pictures are controls. */
            case 'piece':
                offer(m.chains);
                return true;

            /* A press, drag or release the canvas took on an enlarged
               picture, already in the coordinates that picture was drawn
               in. What happens to it is the page's. */
            case 'input':
                onGesture(m);
                return true;

            /* Somebody clicked a stage's params handle. The canvas says
               which stage and where its box is; this is the form, which
               is HTML for the same reason it is gtkmm on the desktop. */
            case 'params':
                showParams(m);
                return true;

            /* The open panel, asked for again. */
            case 'panel':
                followParams(m);
                return true;
        }

        return false;
    };

    /* ---- a stage's parameters ----
     *
     * What it is playing, beside the box that is playing it -- and settable,
     * which it was not. The rows are the module's description of the stage
     * (src/StagePanel.cpp) and they are drawn by panel.js, which is the same
     * renderer the knob strip and the channel's parameters use: one class
     * set, one spelling of a number, one idea of what a duration is worth in
     * milliseconds.
     *
     * An edit does not write anything here. It leaves as a command, and
     * every instance applies it -- the worklet, so it sounds; the mirror, so
     * the picture follows; every peer, so the room stays one piece. The page
     * that typed it is the nearest peer and not a privileged one, so its own
     * box moves because the module took the edit (docs/JAM.md).
     */

    /* The panel that is up: which stage it is over, where the popover is
       pointed, what the module said the rows were, and the function
       showPanel handed back for putting a value into one of them. */
    let params = null;

    /* The rows into the popover, replacing whatever was there. `panel' is
       null for a stage with nothing to show -- a dsp node run at control
       rate, a plugin that did not load -- which is said rather than left as
       an empty popover somebody presses twice. */
    const draw = (panel) =>
    {
        const box = $('composerparams');
        const title = document.createElement('div');

        box.replaceChildren();

        title.className = 'menutitle';
        title.textContent = panel === null
            ? `in ${params.chainName}`
            : `${panel.title} in ${params.chainName}`;
        box.append(title);

        if (panel === null)
        {
            const none = document.createElement('div');

            none.className = 'paramwhat';
            none.textContent = 'no parameters';
            box.append(none);
            params.panel = null;

            return;
        }

        /* The rows go in a box of their own: showPanel replaces everything
           in what it is given, and the line above it saying which stage
           this is about is not one of its rows. */
        const body = document.createElement('div');

        box.append(body);

        /* Which stage this panel is about, held rather than read back off
         * `params' when an edit arrives.
         *
         * The two are the same object right now and stop being it a moment
         * later: a box commits its value on `change', which fires when the
         * focus leaves -- and clicking away from the popover is how the
         * focus leaves. That click closes the popover first, in a capture
         * handler that sets `params' to null, so reading it here threw and
         * the edit somebody had just typed went nowhere. */
        const about = params;

        params.panel = panel;
        params.setValue = showPanel(
            body, panel,
            (row, text) => onParamEdit(about.chain, about.stage, row, text));
    };

    const showParams = (m) =>
    {
        const box = $('composerparams');

        params = { chain: m.chain, stage: m.stage, chainName: m.chainName,
                   panel: null, setValue: () => {} };

        draw(m.panel === null ? null : JSON.parse(m.panel));

        /* Beside the box, in the page's own coordinates: the canvas said
           where in its own pixels and the element says where it is. Held
           inside the window by placePopover, since a pane can be
           narrower than this panel is. */
        const at = $('composer').getBoundingClientRect();

        placePopover(box,
                     at.left + window.scrollX + m.at.x + m.at.w + 6,
                     at.top + window.scrollY + m.at.y);
    };

    /* The panel following the piece.
     *
     * A stage's rows come out of the .gen and move when somebody edits it,
     * which is what the shape is for: an edit from a peer, or this page's
     * own coming back round, changes the line and the rows are described
     * again. The one number that moves on its own is a param read through a
     * knob, and it arrives in the same description -- so there is no value
     * poll here, unlike the channel's panel, and the rebuild is the poll.
     *
     * Asked of the mirror rather than the worklet: this popover is over a
     * stage the canvas pointed at, and the canvas is the mirror's. Both
     * instances hold the same piece and apply the same commands, so there is
     * no third answer for them to disagree with.
     */
    const followParams = (m) =>
    {
        if (params === null || m.panel === null ||
            m.chain !== params.chain || m.stage !== params.stage)
            return;

        const panel = JSON.parse(m.panel);

        /* The rows themselves have changed -- a value became a binding, a
           knob was declared -- so the widgets are not the right ones any
           more. Where the popover is pointing has not changed with them. */
        if (params.panel === null || panel.shape !== params.panel.shape)
        {
            draw(panel);
            return;
        }

        for (const row of panel.rows)
            params.setValue(row.id, row.value, row.text);
    };

    /* Asked for on the frames the view already runs, and only while the
       popover is up: a panel nobody is looking at is a description built
       for nobody. */
    const pollParams = () =>
    {
        if (params !== null && !$('composerparams').hidden)
            toMirror({ type: 'panel', chain: params.chain,
                       stage: params.stage });
    };

    /* A popover closes when something else is pressed: the next gesture
       is the answer to it. */
    window.addEventListener('pointerdown', (e) =>
    {
        const panel = $('composerparams');

        if (!panel.hidden && !panel.contains(e.target))
        {
            panel.hidden = true;
            params = null;
        }
    }, true);

    /* Shown when its own box is open and the page is where it belongs.
       Folded away, the view asks for no frames: a picture nobody is
       looking at is a piece's worth of drawing per animation frame for
       nobody. */
    let wanted = false;

    const show = (on) =>
    {
        wanted = on;
        view.show(on && $('composerview').open);
    };

    /* The disclosure opening is not the page saying it wants this view --
       the page says that, and said it last at the mode switch. Read as a
       yes, it resumed a piece's worth of drawing per animation frame in
       patch mode, of a piece that is not loaded any more. */
    $('composerview').addEventListener(
        'toggle', () => view.show(wanted && $('composerview').open));

    return { fromMirror, show, handleOf, pollParams,
             /* Whether the frame loop is running, which is the whole
                point of asking a pane whether anybody is looking. */
             visible: () => view.visible(),

             /* What the popover is showing, for a harness to read. */
             params: () => [...$('composerparams').querySelectorAll(
                 '.panelrow')].map((r) => r.textContent) };
}
