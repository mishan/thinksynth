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
 * account.js -- what the relay and the page agree on about accounts: the
 * API's address, how a name is cleaned up and compared, and how a key is
 * read back from however it was typed. No DOM and no Node: both import it.
 *
 * An account is a handle and a key; no email, password or real name. The
 * relay makes the key -- eight words -- and keeps only its hash, so the key
 * is the account, and logging in trades it for a session the page keeps.
 */

/* Where the relay serves the account routes, beside its sockets. */
export const ACCOUNT_API = '/api/account';

/* Words in a key, and the longest key a request may carry: eight long
   words and their separators, with room to spare. */
export const KEY_WORDS_PER_KEY = 8;
const KEY_INPUT_MAX = 200;

/* A name -- a handle, or a guest's -- in characters (grapheme clusters),
   and in UTF-16 units, which is what bounds a run of long emoji. */
export const NAME_MAX = 32;
const NAME_UNITS_MAX = 64;
const NAME_INPUT_MAX = 200;

/* Stripped outright: control characters other than whitespace, format
   characters other than the zero-width joiner and non-joiner, private-use,
   unassigned and lone-surrogate code points, and marks that only ever
   render invisibly. */
const STRIPPED = /(?!\s)\p{Cc}|(?!\u200C|\u200D)\p{Cf}|[\p{Co}\p{Cn}\p{Cs}\u034F\u17B4\u17B5]/gu;

/* A run of whitespace, or of characters that render blank without being
   whitespace to Unicode: the Hangul fillers, the Mongolian vowel separator
   and the braille blank. */
const BLANK_RUN = /[\s\u115F\u1160\u180E\u2800\u3164\uFFA0]+/gu;

const JOINER_RUN = /(?:\u200C|\u200D)+/gu;

/* A joiner run stays only as a single joiner between two non-space
   characters: emoji sequences, Persian and Indic text. */
function keepJoiner (run, at, text)
{
    const before = text[at - 1];
    const after = text[at + run.length];

    return run.length === 1 && before !== undefined && after !== undefined &&
           before !== ' ' && after !== ' ' ? run : '';
}

let graphemes;

/* The first `max' characters of `text' that fit in `units' UTF-16 units:
   grapheme clusters, or code points where Intl.Segmenter is missing. */
function truncate (text, max, units)
{
    graphemes ??= typeof Intl.Segmenter === 'function'
        ? new Intl.Segmenter(undefined, { granularity: 'grapheme' }) : null;

    const chars = graphemes ? Array.from(graphemes.segment(text),
                                         (s) => s.segment)
                            : Array.from(text);
    let out = '';

    for (const c of chars.slice(0, max))
    {
        if (out.length + c.length > units)
            break;

        out += c;
    }

    return out;
}

/* A name as people will see it, or null if nothing visible is left: no
   invisible or direction-changing characters, blanks as single spaces,
   composed, at most two stacked marks, trimmed and cut to NAME_MAX. What
   two names are told apart by is foldName's. */
export function normalizeName (raw)
{
    if (typeof raw !== 'string' || raw.length > NAME_INPUT_MAX)
        return null;

    const cleaned = raw.replace(STRIPPED, '')
                       .replace(BLANK_RUN, ' ')
                       .replace(JOINER_RUN, keepJoiner)
                       .replace(/ {2,}/g, ' ')
                       .normalize('NFC')
                       .replace(/(\p{M}{2})\p{M}+/gu, '$1')
                       .trim();
    const name = truncate(cleaned, NAME_MAX, NAME_UNITS_MAX).trim();

    return name === '' ? null : name;
}

/* The form two names clash in. Compatibility forms (full-width letters,
   ligatures) fold to their plain letters and case does not count;
   upper-casing first catches what lower-casing alone misses (`ß' and
   `SS'), and the dot `İ' leaves on its `i' is dropped. */
export function foldName (name)
{
    return name.normalize('NFKC').toUpperCase().toLowerCase()
               .replace(/i\u0307/g, 'i').normalize('NFC');
}

/* A key as typed or pasted, in its one form: lowercase words joined by
   hyphens, whatever separated them. Null if it is not eight words of
   letters. */
export function normalizeKey (raw)
{
    if (typeof raw !== 'string' || raw.length > KEY_INPUT_MAX)
        return null;

    const words = raw.toLowerCase().split(/[\s-]+/).filter(Boolean);

    return words.length === KEY_WORDS_PER_KEY &&
           words.every((w) => /^[a-z]+$/.test(w)) ? words.join('-') : null;
}

/* A name as the room shows it: a guest's says so. `account' is the
   relay's word, and a relay from before accounts says nothing, which
   marks nobody. */
export function shownName ({ name, account })
{
    return account === false ? `${name} (guest)` : name;
}

/* The relay's HTTP origin, for its WebSocket URL: wss://host/ is
   https://host. Null for a URL that is not one. */
export function apiOriginOf (relayUrl)
{
    try
    {
        const url = new URL(relayUrl);

        url.protocol = { 'wss:': 'https:', 'ws:': 'http:' }[url.protocol] ??
                       url.protocol;

        return /^https?:$/.test(url.protocol) ? url.origin : null;
    }
    catch
    {
        return null;
    }
}
