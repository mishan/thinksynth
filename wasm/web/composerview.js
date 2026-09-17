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
 * knobs.js are one file each: two copies of a thing two pages have to
 * agree on is how they stop agreeing.
 *
 * What differs between them is only the last step. The solo page sends a
 * gesture to its own worklet for the next window; the room page stamps it
 * with the knob lead and broadcasts it, and it arrives everywhere -- here
 * included -- at the time it names. Hence `onGesture'.
 */

import { createCanvasView } from './canvasview.js';

export function createComposerView ({ root = document, toMirror,
                                      onGesture })
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

    /* True if the message was this view's. */
    const fromMirror = (m) =>
    {
        switch (m.type)
        {
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
               in. What happens to it is the page's (JAM_M6.md, section
               5). */
            case 'input':
                onGesture(m);
                return true;
        }

        return false;
    };

    /* Shown when its own box is open and the page is where it belongs.
       Folded away, the view asks for no frames: a picture nobody is
       looking at is a piece's worth of drawing per animation frame for
       nobody. */
    const show = (on) => view.show(on && $('composerview').open);

    $('composerview').addEventListener('toggle', () => show(true));

    return { fromMirror, show };
}
