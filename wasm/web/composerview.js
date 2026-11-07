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
                                      onGesture, onParamEdit, onMix,
                                      onKnob, onMove, onSection, onFreeze,
                                      onSeek, onGrowBack, onKeep,
                                      keepMix = false })
{
    const $ = (id) => root.getElementById(id);

    const view = createCanvasView({
        scroller: $('composerscroll'),
        canvas: $('composer'),
        send: toMirror,
    });

    /* The chains by name, as the last piece reported them, and -- where
       `keepMix' asks for it -- which of them were muted and soloed, by
       name, to put back when the piece is loaded again: a load clears
       both, and an edit is not a reason to hear every chain. forgetMix()
       is what choosing another piece calls. */
    let chainNames = [];
    let pieces = 0;               /* pieces the mirror has reported      */
    const mix = { mute: new Set(), solo: new Set() };

    const forgetMix = () =>
    {
        mix.mute.clear();
        mix.solo.clear();
    };

    /* A chain to come back muted from the next load: what a freeze does
       to the chain it froze. */
    const muteOnLoad = (name) =>
    {
        if (keepMix)
            mix.mute.add(name);
    };

    const setMix = (type, chain, on) =>
    {
        const name = chainNames[chain];

        if (keepMix && name !== undefined)
        {
            if (on)
                mix[type].add(name);
            else
                mix[type].delete(name);
        }

        onMix?.(type, chain, on);
    };

    /* The stages whose picture is a control, by "chain.stage", and which
       one is enlarged now. */
    let stages = new Map();
    let enlarged = { chain: -1, stage: -1 };

    const status = () =>
    {
        const key = `${enlarged.chain}.${enlarged.stage}`;
        const which = stages.get(key);
        const menu = $('composerpaint');

        /* The menu says what is being painted on, or nothing. */
        menu.value = which !== undefined ? key : '';

        $('composerstatus').textContent = which !== undefined
            ? `Painting ${which}. Drag on it; Escape, or the menu, puts it ` +
              'back. What you paint goes out as a command and arrives at ' +
              'its time, here as on every peer.'
            : stages.size > 0
                ? 'Paint on a picture from the menu, or double-click it.'
                : '';
    };

    /* One menu for every picture that can be painted on, rather than a
       button each: a piece with a dozen of them had a dozen buttons,
       taller on a phone than the canvas they were for. The first entry
       is the piece itself, which is how a finger leaves a picture. */
    $('composerpaint').addEventListener('change', (e) =>
    {
        const [chain, stage] = e.target.value === ''
            ? [-1, -1] : e.target.value.split('.').map(Number);

        toMirror({ type: 'enlarge', chain, stage });
    });

    /* One button per stage that can be painted on. The canvas enlarges a
       stage on a double-click and puts it back on Escape; these are the
       same two things for a finger, and they are also how anybody finds
       out that a picture is a control at all. */
    const offer = (chains) =>
    {
        const menu = $('composerpaint');
        const top = document.createElement('option');

        stages = new Map();
        top.value = '';
        top.textContent = 'Paint\u2026';
        menu.replaceChildren(top);

        for (const chain of chains)
        {
            const group = document.createElement('optgroup');

            group.label = chain.name;

            for (const stage of chain.stages)
            {
                if (!stage.takesInput)
                    continue;

                const option = document.createElement('option');
                const key = `${chain.chain}.${stage.stage}`;

                stages.set(key, `${stage.name} in ${chain.name}`);
                option.value = key;
                option.textContent = `${stage.name} in ${chain.name}`;
                group.append(option);
            }

            if (group.children.length > 0)
                menu.append(group);
        }

        menu.hidden = stages.size === 0;
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

    /* The same for a chain's M (which 0) or S (1), and each chain's
       mute and solo as the mirror holds them. */
    let chipAsked = null, mixAsked = null, knobAsked = null,
        stageAsked = null, activityAsked = null, layoutAsked = null,
        laneAsked = null;

    const chipOf = (chain, which) => new Promise((resolve) =>
    {
        chipAsked = resolve;
        toMirror({ type: 'chip', chain, which });
    });

    const askMix = () => new Promise((resolve) =>
    {
        mixAsked = resolve;
        toMirror({ type: 'mix' });
    });

    /* A knob's track and port, [x0, x1, y, portX, portY], and a stage's
       box, [x, y, w, h], in the canvas element's pixels. */
    const knobAt = (name) => new Promise((resolve) =>
    {
        knobAsked = resolve;
        toMirror({ type: 'knobat', name });
    });

    const lane = () => new Promise((resolve) =>
    {
        laneAsked = resolve;
        toMirror({ type: 'lane' });
    });

    /* With `arrow' ({ chain, stage }), also where that stage's output
       arrow is. */
    const layout = (arrow) => new Promise((resolve) =>
    {
        layoutAsked = resolve;
        toMirror({ type: 'layout', arrow });
    });

    const activity = () => new Promise((resolve) =>
    {
        activityAsked = resolve;
        toMirror({ type: 'activity' });
    });

    const stageAt = (chain, stage) => new Promise((resolve) =>
    {
        stageAsked = resolve;
        toMirror({ type: 'stageat', chain, stage });
    });

    /* The roll's notes by the chain that made them. Said to the mirror,
       which draws both the roll and this canvas; nothing is heard
       differently, so it is not a command. */
    $('rollbychain')?.addEventListener('change', (e) =>
        toMirror({ type: 'bychain', on: e.target.checked }));

    /* Every chain to one short line, or every one open again: a view
       setting, the mirror's canvas's own. */
    $('collapseall')?.addEventListener('click', () =>
        toMirror({ type: 'collapseall', on: true }));
    $('expandall')?.addEventListener('click', () =>
        toMirror({ type: 'collapseall', on: false }));

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
            case 'chip':
                chipAsked?.(m);
                chipAsked = null;
                return true;
            case 'mix':
                mixAsked?.(m.chains);
                mixAsked = null;
                return true;
            case 'knobat':
                knobAsked?.(m.at);
                knobAsked = null;
                return true;
            case 'stageat':
                stageAsked?.(m.at);
                stageAsked = null;
                return true;
            case 'activity':
                activityAsked?.(m);
                activityAsked = null;
                return true;
            case 'layout':
                layoutAsked?.(m);
                layoutAsked = null;
                return true;
            case 'lane':
                laneAsked?.(m);
                laneAsked = null;
                return true;

            /* A section's block: the transport to where it begins. */
            case 'canvasseek':
                onSeek?.(m.at, m.section);
                return true;

            /* A chain's F. */
            case 'canvasfreeze':
                onFreeze?.(m.chain, m.chainName);
                return true;

            /* An arrangement cell pressed: a `section' command. */
            case 'canvassection':
                onSection?.(m.section, m.chain, m.level);
                return true;

            /* A knob node's track dragged: a knob command, by index. */
            case 'canvasknob':
                onKnob?.(m.knob, m.value, m.commit, m.name);
                return true;

            /* A stage box dropped elsewhere in its chain. */
            case 'canvasmove':
                onMove?.(m.chainName, m.from, m.to);
                return true;

            /* A wire from a knob dropped on a stage. */
            case 'canvasbind':
                showBind(m);
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
                chainNames = m.chains.map((c) => c.name);
                pieces++;

                if (keepMix)
                    for (const type of ['mute', 'solo'])
                        m.chains.forEach((c) =>
                        {
                            if (mix[type].has(c.name))
                                onMix?.(type, c.chain, true);
                        });

                /* Said again with every piece: a mirror made since the box
                   was ticked has not heard it. */
                if ($('rollbychain')?.checked)
                    toMirror({ type: 'bychain', on: true });

                return true;

            /* A press, drag or release the canvas took on an enlarged
               picture, already in the coordinates that picture was drawn
               in. What happens to it is the page's. */
            case 'input':
                onGesture(m);
                return true;

            /* A chain's M or S was pressed. What it becomes is the
               page's: a command, like a gesture. */
            case 'mute':
            case 'solo':
                setMix(m.type, m.chain, m.on);
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

    /* What a gen::grow stage's chain has grown (thinkweb.cpp,
     * tw_growth_json), under its rows: for each instrument the chain
     * reaches, the edits on its declared graph in words, the one playing
     * last; with a target, how far each graph was heard from it over time;
     * and Keep, which writes the graph playing into the piece. A row goes
     * back to that graph (onGrowBack), a command like any other. Drawn the
     * way the desktop's GrowthView is. */
    const drawGrowth = (box, growth) =>
    {
        box.replaceChildren();

        for (const g of growth)
        {
            const head = document.createElement('div');

            head.className = 'paramwhat';
            head.textContent = g.stack.length < 2
                ? `${g.instrument}: playing the declared graph`
                : `${g.instrument}: edits on the declared graph`;
            box.append(head);

            g.stack.forEach((what, depth) =>
            {
                if (g.stack.length < 2)
                    return;

                const playing = depth === g.stack.length - 1;
                const row = document.createElement(
                    playing || !onGrowBack ? 'div' : 'button');

                row.className = playing ? 'growthrow playing' : 'growthrow';
                row.textContent = (depth === 0 ? 'the declared graph'
                                               : `${depth}. ${what}`) +
                                  (playing ? '  (playing)' : '');

                if (row.tagName === 'BUTTON')
                    row.addEventListener('click',
                                         () => onGrowBack(g.channel, depth));

                box.append(row);
            });

            const near = g.log.filter((s) => s.distance !== null ||
                                             s.from !== null);

            if (near.length > 0)
            {
                const first = near[0].from ?? near[0].distance;
                const now = g.log[g.log.length - 1].distance;
                const db = (d) => d === null ? 'not heard'
                                             : `${Math.round(d)} dB`;
                const said = document.createElement('div');

                said.className = 'paramwhat';
                said.textContent = `From the target: ${db(now)} now, ` +
                                   `${db(first)} at first; nearer is higher.`;
                box.append(growthPlot(g.log), said);
            }

            if (g.stack.length > 1 && onKeep)
            {
                const keep = document.createElement('button');

                keep.textContent = `Keep ${g.instrument}'s grown graph`;
                keep.title = 'Write the graph playing into the piece, as ' +
                             'its dsp { }';
                keep.addEventListener('click', () => onKeep(g.instrument));
                box.append(keep);
            }
        }
    };

    /* Time across, distance down from nearest at the top: the edits heard
       at each step as rings, the one taken filled, and the graph playing
       as a line. */
    const growthPlot = (log) =>
    {
        const canvas = document.createElement('canvas');
        const w = 240, h = 70, pad = 6;
        const cx = canvas.getContext('2d');
        let t0 = Infinity, t1 = -Infinity, lo = Infinity, hi = -Infinity;
        const take = (t, d) =>
        {
            if (d === null)
                return;

            t0 = Math.min(t0, t);
            t1 = Math.max(t1, t);
            lo = Math.min(lo, d);
            hi = Math.max(hi, d);
        };

        canvas.width = w;
        canvas.height = h;
        canvas.className = 'growthplot';
        cx.fillStyle = '#14161a';
        cx.fillRect(0, 0, w, h);

        for (const s of log)
        {
            take(s.at, s.distance);
            take(s.at, s.from);
            s.heard.forEach((d) => take(s.at, d));
        }

        const span = Math.max(t1 - t0, 1), range = Math.max(hi - lo, 0.5);
        const x = (t) => pad + (t - t0) / span * (w - 2 * pad);
        const y = (d) => pad + (d - lo) / range * (h - 2 * pad);

        for (const s of log)
            s.heard.forEach((d, i) =>
            {
                if (d === null)
                    return;

                cx.beginPath();
                cx.arc(x(s.at), y(d), i === s.taken ? 3.5 : 2.5, 0, 2 * Math.PI);

                if (i === s.taken)
                {
                    cx.fillStyle = '#59a6ff';
                    cx.fill();
                }
                else
                {
                    cx.strokeStyle = 'rgba(204, 204, 204, 0.5)';
                    cx.stroke();
                }
            });

        cx.beginPath();

        /* Where each step started, then where it went; a graph nobody
           heard is a gap, not a guess. */
        let down = false;
        const to = (t, d) =>
        {
            if (down)
                cx.lineTo(x(t), y(d));
            else
                cx.moveTo(x(t), y(d));

            down = true;
        };

        for (const s of log)
        {
            if (s.from !== null)
                to(s.at, s.from);

            if (s.distance !== null)
                to(s.at, s.distance);
            else
                down = false;
        }

        cx.strokeStyle = '#e6e6e6';
        cx.lineWidth = 1.5;
        cx.stroke();

        return canvas;
    };

    /* The rows into the popover, replacing whatever was there. `panel' is
       null for a stage with nothing to show -- a dsp node run at control
       rate, a plugin that did not load -- which is said rather than left as
       an empty popover somebody presses twice. */
    const draw = (panel, growth) =>
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

        /* Its own box too, for the same reason, and redrawn on its own
           when what grew changes. */
        const grown = document.createElement('div');

        box.append(grown);
        params.grown = grown;
        params.growth = growth ?? null;

        if (growth)
            drawGrowth(grown, JSON.parse(growth));

        params.panel = panel;
        params.setValue = showPanel(
            body, panel,
            (row, text) => onParamEdit(about.chain, about.stage, row, text,
                                       { chainName: about.chainName,
                                         stageName: about.stageName }));
    };

    const showParams = (m) =>
    {
        const box = $('composerparams');

        params = { chain: m.chain, stage: m.stage, chainName: m.chainName,
                   stageName: m.stageName,
                   panel: null, setValue: () => {} };

        draw(m.panel === null ? null : JSON.parse(m.panel), m.growth);

        /* Beside the box, in the page's own coordinates: the canvas said
           where in its own pixels and the element says where it is. Held
           inside the window by placePopover, since a pane can be
           narrower than this panel is. */
        const at = $('composer').getBoundingClientRect();

        placePopover(box,
                     at.left + window.scrollX + m.at.x + m.at.w + 6,
                     at.top + window.scrollY + m.at.y);
    };

    /* ---- a knob's wire, dropped on a stage ----
     *
     * Which param it is for is a question, since a stage with six numbers
     * has six honest answers: one button per param a knob can drive, from
     * the stage's own panel. Picking one is a param edit whose text is the
     * binding, sent the way a typed value is, so the piece's text is
     * written by the same door.
     */
    const showBind = (m) =>
    {
        const box = $('composerparams');
        const title = document.createElement('div');
        const panel = m.panel === null ? null : JSON.parse(m.panel);
        const rows = panel === null ? []
                                    : panel.rows.filter((r) => r.bindable);

        params = null;
        box.replaceChildren();

        title.className = 'menutitle';
        title.textContent = `@${m.knob} → ${panel?.title ?? 'stage'} in ` +
                            m.chainName;
        box.append(title);

        for (const row of rows)
        {
            const button = document.createElement('button');

            button.dataset.row = row.id;
            button.textContent = row.knob === m.knob
                ? `${row.label}  (already @${m.knob})`
                : row.knob ? `${row.label}  (now @${row.knob})` : row.label;

            if (row.desc)
                button.title = row.desc;

            button.addEventListener('click', () =>
            {
                box.hidden = true;
                onParamEdit(m.chain, m.stage, row.id, `@${m.knob}`,
                            { chainName: m.chainName,
                              stageName: m.stageName });
            });
            box.append(button);
        }

        if (rows.length === 0)
        {
            const none = document.createElement('div');

            none.className = 'paramwhat';
            none.textContent = 'this stage has nothing a knob can drive';
            box.append(none);
        }

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
        if (params === null || m.chain !== params.chain ||
            m.stage !== params.stage)
            return;

        /* An edit has put another stage at this index: its values under
           this title would be wrong, and a write goes by name. */
        if (m.chainName !== params.chainName ||
            m.stageName !== params.stageName)
        {
            $('composerparams').hidden = true;
            params = null;
            return;
        }

        if (m.panel === null)
            return;

        const panel = JSON.parse(m.panel);

        /* The rows themselves have changed -- a value became a binding, a
           knob was declared -- so the widgets are not the right ones any
           more. Where the popover is pointing has not changed with them. */
        if (params.panel === null || panel.shape !== params.panel.shape)
        {
            draw(panel, m.growth);
            return;
        }

        if ((m.growth ?? null) !== params.growth)
        {
            params.growth = m.growth ?? null;
            drawGrowth(params.grown, m.growth ? JSON.parse(m.growth) : []);
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

    return { fromMirror, show, handleOf, chipOf, mix: askMix, forgetMix,
             muteOnLoad,
             knobAt, stageAt, activity, layout, lane, pollParams,

             /* How many pieces the mirror has loaded, for a harness to
                wait on one. */
             pieces: () => pieces,
             /* Whether the frame loop is running, which is the whole
                point of asking a pane whether anybody is looking. */
             visible: () => view.visible(),

             /* What the popover is showing, for a harness to read. */
             params: () => [...$('composerparams').querySelectorAll(
                 '.panelrow')].map((r) => r.textContent) };
}
