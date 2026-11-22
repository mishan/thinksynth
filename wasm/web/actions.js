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
 * actions.js -- a controller's actions played on the page: the Index's
 * (hid.js) and the phone's (phonepane.js), as the module answers them
 * (hid/thhHost.h, thhActionsJson).
 *
 * Notes go to the page's keys; a bend or an aux to the channel the note
 * sounds on, or nowhere for a note with no voice to bend; and a knob's
 * value, the latest of each, once a frame: a controller's axes move every
 * few milliseconds, and the synth needs only what is new.
 */

/* hid/thhMapper.h's thhAction::Kind, by the names thhActionsJson writes. */
export const KIND = { on: 0, off: 1, value: 2, bend: 3, aux: 4 };

export function actionPlayer ({ onNoteOn, onNoteOff, play, bendChannel })
{
    const values = new Map();       /* "channel arg" -> latest value */
    let flushing = 0, framed = false;

    const flush = () =>
    {
        flushing = 0;
        framed = false;
        for (const a of values.values())
            play(a);
        values.clear();
    };

    /* A frame asked for, then the page hidden: a hidden page draws none,
       so the values go by a timer instead of waiting till it's seen. */
    document.addEventListener('visibilitychange', () =>
    {
        if (document.hidden && framed)
        {
            cancelAnimationFrame(flushing);
            framed = false;
            flushing = setTimeout(flush, 15);
        }
    });

    const player = (actions) =>
    {
        for (const a of actions)
        {
            if (a.kind === 'on')
                onNoteOn(a.note, a.velocity);
            else if (a.kind === 'off')
                onNoteOff(a.note);
            else if (a.kind === 'value')
            {
                values.set(`${a.channel} ${a.arg}`, { ...a, kind: KIND.value });
                /* A hidden tab draws no frames, and a controller still plays
                   in one. */
                if (flushing === 0)
                {
                    framed = !document.hidden;
                    flushing = framed ? requestAnimationFrame(flush)
                                      : setTimeout(flush, 15);
                }
            }
            else if (KIND[a.kind] !== undefined)
            {
                const on = bendChannel(a.note);

                if (on !== null)
                    play({ ...a, kind: KIND[a.kind], channel: on, frame: -1 });
            }
        }
    };

    /* Values not yet sent, dropped: everything has just been let go. */
    player.forget = () => values.clear();
    return player;
}
