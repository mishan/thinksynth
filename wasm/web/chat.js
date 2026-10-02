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
    const t = Math.round(secs * 10) / 10;

    return `${Math.floor(t / 60)}:${(t % 60).toFixed(1).padStart(4, '0')}`;
}

/*
 * `feed' is the list lines go into, `form' and `input' what one is typed
 * into, and `note' where a refusal is said. `send(text, n)' hands line n
 * to the relay and says whether there was a connection to hand it to,
 * `self()' is this peer's id, `colorOf(name)' a name's color, `visible()'
 * whether anybody can see the feed, and `title(text)' puts the unread
 * count where the pane's name is.
 */
export function createChat ({ feed, form, input, note, send, self, colorOf,
                              visible, title, wait = ECHO_WAIT })
{
    /* n -> { text, timer, late }: lines sent and not yet sent back. A late
       one has been given up on and put back in the box, and is kept in
       case the relay was only slow. */
    const pending = new Map();
    let sent = 0;
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

    /* A line that did not go, back into the box -- after whatever is
       being typed there, so neither is lost. */
    const restore = (text) =>
    {
        input.value = input.value === '' ? text : `${input.value} ${text}`;
    };

    form.addEventListener('submit', (e) =>
    {
        e.preventDefault();

        const text = input.value.trim();

        if (text === '')
            return;

        const n = ++sent;

        if (!send(text, n))
        {
            note.textContent = 'Not connected to the relay.';
            return;
        }

        const p = { text, late: false };

        p.timer = setTimeout(() =>
        {
            p.late = true;
            restore(text);
            note.textContent = 'The relay did not send that back; it may ' +
                               'have no chat.';
        }, wait);
        pending.set(n, p);
        note.textContent = '';
        input.value = '';
    });

    return {
        activity,

        /* A line from the relay, ours included. */
        said ({ from, name, text, bar, n })
        {
            const li = document.createElement('li');

            /* Isolated, so a name full of direction marks cannot turn the
               line after it round. */
            const who = document.createElement('bdi');

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
                const p = pending.get(n);

                if (p === undefined)
                    return;

                clearTimeout(p.timer);
                pending.delete(n);

                /* Given up on and put back, and it went after all: out of
                   the box again, unless it has been typed over since. */
                if (p.late && input.value === p.text)
                {
                    input.value = '';
                    note.textContent = '';
                }
            }
            else if (!visible())
            {
                unread++;
                showTitle();
            }
        },

        /* The relay's no to line n, and why. */
        refused ({ n, why })
        {
            const p = pending.get(n);

            note.textContent = why;

            if (p === undefined)
                return;

            clearTimeout(p.timer);
            pending.delete(n);

            if (!p.late)
                restore(p.text);
        },

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
                    activity(`${p.name} left channel ${before.seat + 1}`);

                if (p.seat !== null)
                    activity(`${p.name} took channel ${p.seat + 1}`);
            }
        },

        /* A transport command or an edit, by `name', as this page got it.
           An edit is stamped for the next bar (jam.js, applyEdit), and
           which bar that is depends on tempo changes still to come. */
        command (name, cmd)
        {
            if (cmd.type === 'edit')
                activity(cmd.at >= 0 ? `${name}'s edit lands at the next bar`
                                     : `${name} applied an edit`);
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
