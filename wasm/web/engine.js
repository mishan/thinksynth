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
 * engine.js -- what a message means to an instance of the module.
 *
 * One switch: a `load', `instrument', `chanarg', `piece', `transport',
 * `begin', `at', `knob', `input', `midion', `midioff', `on', `off' or
 * `alloff' message, turned into the tw_ call that applies it. It used to live in
 * worklet.js, and moved here when there were two instances to apply it to
 * (JAM_M6.md, section 4).
 *
 * The two are the worklet, which renders, and the mirror, which is the
 * same module in a worker with a synth that never renders -- fed the same
 * messages so that it composes the same piece, so that the composer view
 * has real composer instances to draw. That they are fed the same stream
 * is not a promise anybody keeps by hand: host.js posts each message to
 * both ports, and this is the one place that says what a message does.
 *
 * `host' is how the answers get back to whoever asked. Two messages have
 * one -- a load says whether it parsed, a piece says what it declared --
 * and anything this cannot make sense of is a line for the log. The
 * mirror passes a log and nothing else: the page's answers come from the
 * worklet, which is the instance whose answer is the one that sounds.
 */

/* thinkweb.cpp's TransportOp. */
export const TRANSPORT = { start: 0, stop: 1, rewind: 2, tempo: 3 };

const NOWHERE = {
    loaded: () => {},
    piece: () => {},
    log: () => {},
};

/* True if the message was one of these, false if it is somebody else's --
 * `start' and `ping' are the worklet's own, and the mirror has `align' and
 * `step'. A caller that gets false has a message this does not know about,
 * which is not by itself an error.
 */
export function apply (M, m, host = NOWHERE)
{
    const { loaded, piece, log } = { ...NOWHERE, ...host };

    switch (m.type)
    {
        case 'load':
            loaded(m.id, M.ccall('tw_load', 'number', ['number', 'string'],
                                 [m.channel, m.text]) !== 0);
            return true;

        case 'instrument':
            M.ccall('tw_instrument', 'number', ['string', 'string'],
                    [m.name, m.text]);
            return true;

        case 'chanarg':
            /* The overrides half of a .patch (patch.js). `array' is the
               only pointer ccall takes, so the floats cross as the bytes
               of one -- it stack-allocates and copies, which is what a
               handful of numbers wants. */
            M.ccall('tw_chanarg', 'number',
                    ['number', 'string', 'array', 'number'],
                    [m.channel, m.name,
                     new Uint8Array(Float32Array.from(m.values).buffer),
                     m.values.length]);
            return true;

        case 'piece':
            piece(m.id, M.ccall('tw_piece_load', 'number',
                                ['string', 'number'],
                                [m.text, m.seed ?? -1]) !== 0);
            return true;

        case 'transport':
            /* Checked, because an op this does not know would reach the
               module as undefined, arrive as zero and start the transport
               -- a typo that plays the piece. */
            if (!Object.hasOwn(TRANSPORT, m.op))
            {
                log(`no transport op '${m.op}'`);
                return true;
            }

            M._tw_transport(m.frame, TRANSPORT[m.op], m.value ?? 0);
            return true;

        case 'begin':
            /* From the top, with transport zero at this frame exactly
               (thinkweb.cpp, tw_begin). */
            M._tw_begin(m.frame);
            return true;

        case 'at':
            /* A stop or a tempo at a transport time, inside the step. The
               other two ops are frame-stamped and go by 'transport'. */
            if (m.op !== 'stop' && m.op !== 'tempo')
            {
                log(`'${m.op}' cannot be stamped with a transport time`);
                return true;
            }

            M._tw_at(m.at, TRANSPORT[m.op], m.value ?? 0);
            return true;

        case 'knob':
            M._tw_knob(m.at, m.knob, m.value);
            return true;

        case 'input':
            /* A gesture on a stage's picture, at a transport time. The
               coordinates are the ones the composer's own draw was
               handed, with the size it was drawn at, so every instance
               inverts the same arithmetic and reaches the same cell
               (JAM_M6.md, section 5). */
            M._tw_input(m.at, m.chain, m.stage, m.kind, m.x, m.y, m.w, m.h,
                        m.button ?? 1);
            return true;

        case 'midion':
            M._tw_midi_on(m.frame, m.channel, m.note, m.velocity);
            return true;

        case 'midioff':
            M._tw_midi_off(m.frame, m.channel, m.note);
            return true;

        case 'on':
            M._tw_note_on(m.frame, m.channel, m.note, m.velocity);
            return true;

        case 'off':
            M._tw_note_off(m.frame, m.channel, m.note);
            return true;

        case 'alloff':
            M._tw_all_off();
            return true;
    }

    return false;
}
