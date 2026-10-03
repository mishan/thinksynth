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
 */

/* MIDI out's controls, for the solo page and a room alike: the button that
 * asks for access and lets go of it, the delay, the line naming the
 * outputs, and a picker per MIDI instrument for where it plays in this
 * browser. What sends is midiout.js; this is what a page puts in front of
 * it and wires to its synth.
 *
 * Routes and the delay are this browser's (localStorage), not the piece's
 * or the room's: the ports are this machine's, and each peer in a room
 * sends what it composes to devices of its own.
 */

import { midiAvailable } from './midi.js';
import { MidiSender } from './midiout.js';
import { AudioClock } from './clock.js';

/* A route per instrument pattern -- an output's name, '@synth', or nothing
   for the pattern's own match -- and the delay in milliseconds. */
const ROUTES = 'thinksynth:midiroutes';
const DELAY = 'thinksynth:midioutdelay';

/* The outputs MIDI clock goes to, by name. */
const CLOCK = 'thinksynth:midiclock';

function saved ()
{
    try
    {
        return {
            routes: JSON.parse(localStorage.getItem(ROUTES) ?? '{}'),
            delay: Number(localStorage.getItem(DELAY) ?? 0) || 0,
            clock: JSON.parse(localStorage.getItem(CLOCK) ?? '[]'),
        };
    }
    catch
    {
        return { routes: {}, delay: 0, clock: [] };
    }
}

export class MidiOutControls
{
    /* `button', `delay' and `status' are the page's elements, and `clock'
       is where a check per output for MIDI clock goes. `instruments'
       answers the piece's instruments as the worklet described them, whose
       midiState this keeps current; `onChange' is told when a picker would
       read differently -- the outputs, or where an instrument plays. */
    constructor ({ button, delay, status, clock = null, instruments = () => [],
                   onChange = () => {} })
    {
        const { routes, delay: ms, clock: clockNames } = saved();

        this.clockBox = clock;
        this.clockNames = new Set(clockNames);
        this.button = button;
        this.delayInput = delay;
        this.statusLine = status;
        this.instruments = instruments;
        this.onChange = onChange;
        this.routes = routes;
        this.delay = ms;
        this.sender = null;
        this.synth = null;
        this.ctx = null;
        this.clock = null;
        this.timer = null;

        button.addEventListener('click', () => this.toggle());
        delay.value = String(ms);
        delay.addEventListener('change', () =>
        {
            this.delay = Math.max(0, Math.min(500, Number(delay.value) || 0));
            delay.value = String(this.delay);

            if (this.sender !== null)
                this.sender.delay = this.delay;

            this.save();
        });
    }

    /* The page's synth has started: this browser's routes go to the worklet
       before there are ports to route onto, so the first port list lands
       every instrument where it goes. `clock' is an AudioClock the page
       already keeps, or none and this keeps its own. */
    start (synth, ctx, clock = null)
    {
        this.synth = synth;
        this.ctx = ctx;
        this.clock = clock;
        this.button.disabled = !midiAvailable();

        if (!midiAvailable())
            this.say('needs Chromium or Firefox, over https or on localhost');

        for (const [pattern, to] of Object.entries(this.routes))
            synth.midiRoute(pattern, to);
    }

    get on ()
    {
        return this.sender !== null;
    }

    /* host.js onMidi. */
    take (msgs)
    {
        this.sender?.take(msgs);
    }

    /* host.js onMidiState: where each MIDI instrument plays now. */
    state (list)
    {
        for (const i of list)
        {
            const mine = this.instruments().find((p) => p.name === i.name);

            if (mine !== undefined)
                mine.midiState = i.midiState;
        }

        this.onChange();
    }

    say (text)
    {
        this.statusLine.textContent = text;
    }

    save ()
    {
        try
        {
            localStorage.setItem(ROUTES, JSON.stringify(this.routes));
            localStorage.setItem(DELAY, String(this.delay));
            localStorage.setItem(CLOCK, JSON.stringify([...this.clockNames]));
        }
        catch
        {
        }
    }

    /* MIDI clock to the checked outputs, and the worklet asked for it only
       where there is one. */
    applyClock ()
    {
        const outputs = this.sender?.outputs ?? [];
        const ids = outputs.filter((o) => this.clockNames.has(o.name))
                           .map((o) => o.id);

        this.sender?.setClock(ids);
        this.synth?.midiClock(ids.length > 0);
    }

    /* A check per output for MIDI clock: any piece can drive a drum
       machine, with or without an instrument of its own on one. */
    showClock ()
    {
        if (this.clockBox === null)
            return;

        this.clockBox.replaceChildren();

        for (const name of this.sender?.names ?? [])
        {
            const label = document.createElement('label');
            const check = document.createElement('input');

            check.type = 'checkbox';
            check.className = 'midiclock';
            check.checked = this.clockNames.has(name);
            check.addEventListener('change', () =>
            {
                if (check.checked)
                    this.clockNames.add(name);
                else
                    this.clockNames.delete(name);

                this.save();
                this.applyClock();
            });

            label.className = 'hint';
            label.append(check, ` clock to ${name}`);
            this.clockBox.append(label);
        }
    }

    /* An instrument that names a MIDI port: where it plays in this
       browser, and why. */
    picker (inst)
    {
        const pick = document.createElement('select');
        const state = document.createElement('span');
        const to = this.routes[inst.midi] ?? '';
        const ports = this.sender?.names ?? [];

        pick.className = 'midiroute';
        pick.dataset.pattern = inst.midi;
        pick.title = `Where ${inst.name} plays in this browser.`;
        pick.append(new Option(`Match "${inst.midi}"`, ''));

        for (const name of ports)
            pick.append(new Option(name, name));

        if (to !== '' && to !== '@synth' && !ports.includes(to))
            pick.append(new Option(`${to} (not connected)`, to));

        pick.append(new Option(inst.dsp ? `This synth: ${inst.dsp}`
                                        : 'Nothing (no dsp)', '@synth'));
        pick.value = to;

        pick.addEventListener('change', () =>
        {
            if (pick.value === '')
                delete this.routes[inst.midi];
            else
                this.routes[inst.midi] = pick.value;

            this.save();
            this.synth?.midiRoute(inst.midi, pick.value);
        });

        state.className = 'hint midistate';
        state.textContent = inst.midiState;

        return [pick, state];
    }

    /* On asks for access, keeps the audio clock the stamps are read
     * through sampled, and hands the worklet the output ports, which puts
     * each MIDI instrument onto its device; off ends what the devices hold
     * and puts every instrument back on its dsp.
     *
     * The button is off while access is asked for -- a permission prompt
     * can sit there -- and a sender that is no longer this one's by the
     * time its access arrives closes itself rather than going on alone. */
    async toggle ()
    {
        if (this.sender !== null)
        {
            this.sender.close();
            this.sender = null;
            clearInterval(this.timer);
            this.timer = null;
            this.synth?.midiClock(false);
            this.synth?.midiPorts([], false, 0);
            this.showClock();
            this.button.textContent = 'MIDI out';
            this.say('');
            this.onChange();
            return;
        }

        if (this.synth === null || this.ctx === null)
            return;

        const ctx = this.ctx;

        this.clock ??= new AudioClock(ctx.sampleRate);

        const clock = this.clock;
        const sampleClock = () =>
        {
            const t = ctx.getOutputTimestamp();

            clock.sample(t.contextTime, t.performanceTime);
        };

        const sender = new MidiSender({
            clock,
            delay: this.delay,
            onPorts: (names, generation) =>
            {
                if (this.sender !== sender)
                    return;

                this.synth.midiPorts(names, true, generation);
                this.say(names.length > 0 ? names.join(', ')
                                          : 'no MIDI outputs; plug one in');
                this.showClock();
                this.applyClock();
                this.onChange();
            },
        });

        this.say('asking...');
        this.button.disabled = true;
        this.sender = sender;

        try
        {
            /* A stamp is only as good as the clock it is read through, and
               a context that has just started reports no time yet: a
               sample first, waited for, so the first note is not sent on a
               guess. */
            sampleClock();
            this.timer = setInterval(sampleClock, 1000);

            for (let i = 0; i < 20 && clock.count === 0; i++)
            {
                await new Promise((r) => setTimeout(r, 50));
                sampleClock();
            }

            await sender.open();

            if (this.sender !== sender)
            {
                sender.close();
                return;
            }

            this.button.textContent = 'MIDI out: on';
        }
        catch (e)
        {
            if (this.sender === sender)
            {
                this.sender = null;
                clearInterval(this.timer);
                this.timer = null;
                this.say(e.message);
            }
        }
        finally
        {
            this.button.disabled = false;
        }
    }
}
