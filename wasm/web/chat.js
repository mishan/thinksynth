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
 * chat.js -- the room's text: what people type, and a line for what the
 * room did, in one feed.
 *
 * A line typed here goes to the relay and is shown when the relay sends
 * it back, with the name the relay says sent it -- so what is on this
 * screen is what reached everyone. The rest of the feed is this page's
 * own account of what it saw: who came and went, who sat down, who
 * pressed Play. Nothing in it is a command, and none of it reaches the
 * worklet, the tape or the run a late joiner is handed.
 */

/* How long a line waits for the relay to send it back. A relay that
   predates chat drops it without a word. */
const ECHO_WAIT = 5000;

/* Lines kept on screen; the oldest go first. */
const KEEP = 500;

/* Where the transport is at `at' seconds as "bar.beat", from a tape report,
   or null when there is no running transport to say it of. */
export function barBeat (tape, at)
{
    if (tape === null || !tape.running || at < 0 || !(tape.tempo > 0) ||
        !(tape.meter > 0))
        return null;

    const beat = Math.max(0, tape.beat + (at - tape.now) * tape.tempo / 60);
    const bar = Math.floor(beat / tape.meter + 1e-6);

    return `${bar + 1}.${Math.floor(beat - bar * tape.meter + 1e-6) + 1}`;
}

/* The seconds of a seek, as the page's clock shows them. */
function clock (secs)
{
    return `${Math.floor(secs / 60)}:${(secs % 60).toFixed(1).padStart(4, '0')}`;
}

/*
 * `feed' is the list lines go into, `form' and `input' what one is typed
 * into, and `note' where a refusal is said. `send(text)' hands a line to
 * the relay, `self()' is this peer's id, `colorOf(name)' a name's color,
 * `visible()' whether anybody can see the feed, and `title(text)' puts the
 * unread count where the pane's name is.
 */
export function createChat ({ feed, form, input, note, send, self, colorOf,
                              visible, title })
{
    const pending = [];         /* { text, timer } awaiting their echo */
    let unread = 0;
    let known = null;           /* id -> { name, seat }, as last seen */

    const showTitle = () => title(unread > 0 ? `Chat (${unread})` : 'Chat');

    const add = (li) =>
    {
        const atEnd = feed.scrollHeight - feed.scrollTop - feed.clientHeight
                      < 4;

        feed.append(li);

        while (feed.children.length > KEEP)
            feed.firstElementChild.remove();

        if (atEnd)
            feed.scrollTop = feed.scrollHeight;
    };

    const activity = (text) =>
    {
        const li = document.createElement('li');

        li.className = 'chatactivity';
        li.textContent = text;
        add(li);
    };

    /* A line that did not go: back into the box if nothing else is being
       typed there, so it is not lost. */
    const unsent = (why) =>
    {
        const p = pending.shift();

        if (p === undefined)
            return;

        clearTimeout(p.timer);
        note.textContent = why;

        if (input.value === '')
            input.value = p.text;
    };

    form.addEventListener('submit', (e) =>
    {
        e.preventDefault();

        const text = input.value.trim();

        if (text === '')
            return;

        pending.push({ text, timer: setTimeout(
            () => unsent('The relay did not send that back; it may have ' +
                         'no chat.'), ECHO_WAIT) });
        note.textContent = '';
        input.value = '';
        send(text);
    });

    return {
        activity,

        /* A line from the relay, ours included. */
        said ({ from, name, text, bar })
        {
            const li = document.createElement('li');
            const who = document.createElement('span');

            li.className = 'chatline';

            if (bar !== undefined)
            {
                const at = document.createElement('span');

                at.className = 'chatbar';
                at.textContent = bar;
                li.append(at, ' ');
            }

            who.className = 'chatname';
            who.textContent = name;
            who.style.color = colorOf(name);
            li.append(who, `: ${text}`);
            add(li);

            if (from === self())
            {
                const p = pending.shift();

                if (p !== undefined)
                    clearTimeout(p.timer);
            }
            else if (!visible())
            {
                unread++;
                showTitle();
            }
        },

        refused: unsent,

        /* The feed has come into view, or gone out of it. */
        shown (on)
        {
            if (on && unread > 0)
            {
                unread = 0;
                showTitle();
            }
        },

        /* Who is here and where they sit, as the relay last said:
           against what was here before, a line for each change. The
           first map is the room as it was found, and says nothing. */
        peers (map)
        {
            const now = new Map([...map].map(([id, p]) =>
                [id, { name: p.name, seat: p.seat }]));
            const was = known;

            known = now;

            if (was === null)
                return;

            for (const [id, p] of was)
                if (!now.has(id))
                    activity(`${p.name} left`);

            for (const [id, p] of now)
            {
                const before = was.get(id);

                if (before === undefined)
                    activity(`${p.name} joined`);

                if ((before?.seat ?? null) === p.seat)
                    continue;

                if (before?.seat !== null && before?.seat !== undefined)
                    activity(`${p.name} left seat ${before.seat}`);

                if (p.seat !== null)
                    activity(`${p.name} took seat ${p.seat}`);
            }
        },

        /* A transport command or an edit, by `name', as this page got it;
           `tape' is the last report, for the bar an edit lands on. */
        command (name, cmd, tape)
        {
            if (cmd.type === 'edit')
            {
                const at = barBeat(tape, cmd.at);

                activity(at === null ? `${name} applied an edit`
                                     : `${name}'s edit lands at bar ` +
                                       at.split('.')[0]);
            }
            else if (cmd.op === 'start')
                activity(cmd.seek > 0 ? `${name} played from ` +
                                        clock(cmd.seek)
                                      : `${name} pressed Play`);
            else if (cmd.op === 'stop')
                activity(`${name} pressed Stop`);
            else if (cmd.op === 'tempo')
                activity(`${name} set the tempo to ${cmd.bpm}`);
        },
    };
}
