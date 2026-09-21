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
 * keyfocus.js -- who owns the computer keyboard, the page or the
 * instrument.
 *
 * The page is an instrument whose keys are Z to / and Q to P, and every
 * control on it is a focusable element that would rather have those
 * letters for itself. Choosing a patch left the focus on the <select>,
 * and from then on the keyboard was dead until you found somewhere
 * harmless to click -- which is not a thing anybody should have to know.
 *
 * So the rule here, and it is the whole file:
 *
 *   The instrument keeps the keys unless somebody deliberately asked a
 *   control for them.
 *
 * Deliberately is the word doing the work. Tabbing to a control is
 * asking: the person is navigating, the arrows and the letters are the
 * control's, and Escape hands them back. Clicking a control is not
 * asking -- a click is one act, already finished by the time the finger
 * is off the button, and nobody who clicks the patch list means "and
 * from now on my keyboard belongs to this list."
 *
 * Text is the exception both ways round: a box you type into owns the
 * keys however the focus got there, because typing is what it is for.
 *
 * A control clicked with the pointer is also let go of when it is done
 * with -- the selection made, the button pressed -- so the focus ring
 * does not sit on a <select> nobody is looking at and Enter does not
 * press Load a second time.
 */

/* Input types that are a box you type into. Everything else an <input>
   can be -- range, checkbox, radio, button, colour -- is a control you
   operate, and operating it is over when the pointer comes off. */
const TEXTUAL = new Set([
    'text', 'number', 'search', 'url', 'email', 'password', 'tel',
    'date', 'time', 'datetime-local', 'month', 'week',
]);

/* A <select> is let go of once it has changed, and it is the only thing
   here that is.
 *
 * A list is the control whose idea of a letter is furthest from a note --
 * it jumps to an option beginning with it -- and it is the one the bug was
 * reported against. A slider and a button keep the focus they were clicked
 * with, because nothing is lost by it: the arrows still move the slider,
 * the letters are still notes, and the two do not overlap. */
const RELEASE_ON = { change: 'select' };

export function createKeyFocus ({ editing = '', indicator = null,
                                  onRelease = () => {} } = {})
{
    /* Whether the focus that a control has now arrived by pointer. Set
       on the way down, before focus moves, so the focus handler that
       follows it knows which kind of act it is part of. */
    let byPointer = false;

    const isTextual = (el) =>
    {
        if (el.isContentEditable)
            return true;

        const tag = el.tagName;

        if (tag === 'TEXTAREA')
            return true;

        if (tag === 'INPUT')
            return TEXTUAL.has(el.type);

        /* Whatever else the page called a text box -- the room's code
           editor is a div full of them. */
        return editing !== '' && el.closest(editing) !== null;
    };

    /* Does the element the keys would go to want them?
     *
     * Note the two halves: a text box always does, and anything else
     * only while the focus in it was asked for with the Tab key. */
    const claims = (el) =>
    {
        if (!(el instanceof Element))
            return false;

        if (isTextual(el))
            return true;

        return !byPointer && el.matches(
            'select, input, button, [tabindex]:not([tabindex="-1"])');
    };

    /* What the indicator says. Empty while the instrument has the keys:
       the ordinary state is not worth a word, and a line that is always
       there is a line nobody reads. */
    const show = () =>
    {
        if (indicator === null)
            return;

        indicator.textContent =
            claims(document.activeElement) ? 'keys are typing — Escape plays'
                                           : '';
    };

    /* Hands the keys back: whatever has the focus loses it, and the page
       says so. Called by Escape, and by a control that has finished. */
    const release = () =>
    {
        const el = document.activeElement;

        if (el instanceof HTMLElement && el !== document.body)
            el.blur();

        show();
    };

    document.addEventListener('pointerdown', () => { byPointer = true; },
                              true);

    /* Tab is the one key that moves the focus on purpose. Anything else
       leaves the flag where it was -- typing into a box must not turn
       that box into something the pointer opened. */
    document.addEventListener('keydown', (e) =>
    {
        if (e.key === 'Tab')
            byPointer = false;
    }, true);

    for (const [event, selector] of Object.entries(RELEASE_ON))
        document.addEventListener(event, (e) =>
        {
            if (!byPointer || !(e.target instanceof Element) ||
                e.target !== document.activeElement ||
                !e.target.matches(selector) || isTextual(e.target))
                return;

            release();
        }, true);

    /* Escape from a control, anywhere.
     *
     * Last rather than first, and only if nothing else wanted it: a code
     * editor closes its completion list with Escape and a canvas puts an
     * enlarged stage back with it, and either would be a worse thing to
     * lose than a keypress spent handing the keyboard over. Neither is
     * taken away here -- what is left after them is an Escape that meant
     * "I am done with this box". */
    document.addEventListener('keydown', (e) =>
    {
        if (e.key !== 'Escape' || e.defaultPrevented ||
            !claims(document.activeElement))
            return;

        release();
        onRelease();
        e.preventDefault();
    });

    document.addEventListener('focusin', show);
    document.addEventListener('focusout', () => setTimeout(show, 0));

    show();

    return { claims, release, textual: isTextual };
}
